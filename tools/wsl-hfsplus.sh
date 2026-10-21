#!/bin/bash
# hfsplus.ko for the running WSL2 kernel, which is built without HFS+
# (tools/a30s-mkroot.sh loop-mounts an HFS+ image). No sudo needed to build;
# a30s-mkroot.sh insmods ~/wslkernel/hfsplus.ko, or: wsl -u root insmod ...
#
#   tools/wsl-hfsplus.sh            (WSL; ~2.5 GB source + ~25 min build)
#
# Microsoft's source at the running tag, the running config plus
# CONFIG_HFSPLUS_FS=m, vmlinux built (with BTF, so struct module has the
# kernel's size). Our gcc differs from Microsoft's, and genksyms then gives
# most exported symbols other CRCs (7740 of 12107 on 6.18.54.1) although the
# config and the layouts are the same; so Module.symvers takes the CRCs out
# of the running kernel image (C:\Program Files\WSL\tools\kernel) before the
# module is linked. Checked 2026-10-21: loads, mkfs/mount/hard links/fsck OK.
set -euo pipefail
REL=$(uname -r)
TAG=linux-msft-wsl-${REL%%-*}
W=$HOME/wslkernel
mkdir -p "$W"
cd "$W"
if [ ! -d src ]; then
	curl -sfL --retry 3 -o src.tar.gz "https://github.com/microsoft/WSL2-Linux-Kernel/archive/refs/tags/$TAG.tar.gz"
	mkdir src.tmp
	tar -xzf src.tar.gz -C src.tmp --strip-components=1
	mv src.tmp src
	rm -f src.tar.gz
fi
cd src
zcat /proc/config.gz > .config
scripts/config --module HFSPLUS_FS
make olddefconfig > /dev/null
[ "$(make -s kernelrelease)" = "$REL" ] || { echo "release mismatch: $(make -s kernelrelease) vs $REL" >&2; exit 1; }
make -j"$(nproc)" vmlinux > build.log 2>&1 || { tail -40 build.log; exit 1; }
[ -s Module.symvers ] || cp vmlinux.symvers Module.symvers
cp Module.symvers Module.symvers.built
make -j"$(nproc)" modules_prepare > /dev/null

# The running kernel's CRCs: __ksymtab(_gpl) (PREL32 kernel_symbol) paired
# with __kcrctab(_gpl) in the extracted vmlinux.
cp "/mnt/c/Program Files/WSL/tools/kernel" "$W/running-bzImage"
scripts/extract-vmlinux "$W/running-bzImage" > "$W/running-vmlinux"
python3 - "$W/running-vmlinux" Module.symvers.built Module.symvers <<'EOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
shoff, = struct.unpack_from('<Q', data, 0x28)
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
secs = [struct.unpack_from('<IIQQQQ', data, shoff + i * shentsize) for i in range(shnum)]
stroff = secs[shstrndx][4]
name = lambda n: data[stroff + n:data.index(b'\0', stroff + n)].decode()
by = {name(s[0]): s for s in secs}
def off(va):
    for s in secs:
        if s[3] and s[3] <= va < s[3] + s[5]:
            return s[4] + va - s[3]
    raise KeyError(hex(va))
crc = {}
for tab, kcrc in (('__ksymtab', '__kcrctab'), ('__ksymtab_gpl', '__kcrctab_gpl')):
    t, c = by[tab], by[kcrc]
    for i in range(t[5] // 12):
        ent = t[3] + 12 * i
        noff, = struct.unpack_from('<i', data, t[4] + 12 * i + 4)
        o = off(ent + 4 + noff)
        crc[data[o:data.index(b'\0', o)].decode()] = '0x%08x' % struct.unpack_from('<I', data, c[4] + 4 * i)
out = []
for line in open(sys.argv[2]):
    f = line.rstrip('\n').split('\t')
    f[0] = crc.get(f[1], f[0])
    out.append('\t'.join(f))
open(sys.argv[3], 'w').write('\n'.join(out) + '\n')
print('symbol CRCs from the running kernel:', sum(1 for f in out if f.split('\t')[1] in crc), 'of', len(out))
EOF
make -s M=fs/hfsplus clean
make -j"$(nproc)" M=fs/hfsplus modules > hfsplus.log 2>&1 || { tail -40 hfsplus.log; exit 1; }
cp fs/hfsplus/hfsplus.ko "$W/hfsplus.ko"
modinfo "$W/hfsplus.ko" | grep vermagic
echo "$W/hfsplus.ko"
