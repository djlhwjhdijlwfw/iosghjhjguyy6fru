#!/usr/bin/env python3
"""Symbolicate shared-cache addresses from SWEET logs without the ipsw tool.

  dsc-sym.py CACHE_DIR SLIDE ADDR...        addresses as printed (slid)
  dsc-sym.py CACHE_DIR SLIDE - < log        every 0x1... address on 'SMP'/'CRASH' lines

CACHE_DIR holds dyld_shared_cache_arm64 and its subcaches (.01 ... .NN,
.dylddata, .dyldlinkedit, .symbols), e.g. the SystemOS cryptex mounted with
apfs-fuse: MNT/root/System/Library/Caches/com.apple.dyld. SLIDE is the
shared region slide from the log line (`SMP name[pid] slide S mh M`).
Prints `addr image+offset symbol+offset`. Symbols: each image's LC_SYMTAB
(exports) and the .symbols file's local symbols (static functions, ObjC
methods).
"""
import bisect, os, re, struct, sys

MAGIC = b'dyld_v1'


class Cache:
    def __init__(self, d):
        self.files = []         # (fileobj, mappings[(addr, size, off)])
        names = sorted(n for n in os.listdir(d) if n.startswith('dyld_shared_cache_arm64')
                       and not n.endswith(('.map', '.atlas')))
        self.symfile = None
        for n in names:
            f = open(os.path.join(d, n), 'rb')
            hdr = f.read(0x200)
            if not hdr.startswith(MAGIC):
                continue
            if n.endswith('.symbols'):
                self.symfile = f
                continue
            moff, mcnt = struct.unpack_from('<II', hdr, 0x10)
            f.seek(moff)
            maps = [struct.unpack_from('<QQQ', f.read(32)) for _ in range(mcnt)]
            self.files.append((f, maps))
            if n == 'dyld_shared_cache_arm64':
                self.main = hdr
                self.mainf = f
        self.base = self.files[0][1][0][0] if self.files else 0
        self.base = min(m[0] for _, ms in self.files for m in ms)

    def read(self, addr, n):
        for f, maps in self.files:
            for a, s, o in maps:
                if a <= addr and addr + n <= a + s:
                    f.seek(o + addr - a)
                    return f.read(n)
        raise KeyError(hex(addr))

    def cstr(self, addr, maxlen=1024):
        b = self.read(addr, 1)
        out = bytearray()
        while True:
            try:
                chunk = self.read(addr, 64)
            except KeyError:
                chunk = self.read(addr, 1)
            i = chunk.find(b'\0')
            if i >= 0:
                out += chunk[:i]
                return out.decode('utf-8', 'replace')
            out += chunk
            addr += len(chunk)
            if len(out) > maxlen:
                return out.decode('utf-8', 'replace')

    def images(self):
        h = self.main
        off, cnt = struct.unpack_from('<II', h, 0x1c0)
        if cnt == 0:
            off, cnt = struct.unpack_from('<II', h, 0x18)
        f = self.mainf
        f.seek(off)
        raw = f.read(32 * cnt)
        out = []
        for i in range(cnt):
            addr, _, _, poff = struct.unpack_from('<QQQI', raw, 32 * i)
            f.seek(poff)
            path = f.read(512).split(b'\0', 1)[0].decode()
            out.append((addr, path))
        return out


def macho(cache, addr):
    """__TEXT range, LINKEDIT (vmaddr, fileoff) and symtab of the image at addr."""
    hdr = cache.read(addr, 32)
    _, _, _, _, ncmds, sizeofcmds = struct.unpack_from('<IiiIII', hdr, 0)
    cmds = cache.read(addr + 32, sizeofcmds)
    text = linkedit = symtab = None
    o = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', cmds, o)
        if cmd == 0x19:                 # LC_SEGMENT_64
            name = cmds[o + 8:o + 24].split(b'\0', 1)[0]
            vmaddr, vmsize, fileoff = struct.unpack_from('<QQQ', cmds, o + 24)
            if name == b'__TEXT':
                text = (vmaddr, vmsize)
            elif name == b'__LINKEDIT':
                linkedit = (vmaddr, fileoff)
        elif cmd == 0x2:                # LC_SYMTAB
            symtab = struct.unpack_from('<IIII', cmds, o + 8)
        o += size
    return text, linkedit, symtab


def nlist_syms(raw_nl, strtab, count):
    syms = []
    for i in range(count):
        strx, ntype, nsect, _, value = struct.unpack_from('<IBBHQ', raw_nl, 16 * i)
        if ntype & 0xe0 or nsect == 0 or value == 0:    # N_STAB, undefined
            continue
        end = strtab.find(b'\0', strx)
        syms.append((value, strtab[strx:end].decode('utf-8', 'replace')))
    return syms


class Symbolizer:
    def __init__(self, d):
        self.c = Cache(d)
        self.imgs = []          # (text_start, text_end, path, header addr)
        for addr, path in self.c.images():
            try:
                text, _, _ = macho(self.c, addr)
            except KeyError:
                continue
            if text:
                self.imgs.append((text[0], text[0] + text[1], path, addr))
        self.imgs.sort()
        self.starts = [i[0] for i in self.imgs]
        self.symcache = {}
        self.local = self._local_index()

    def _local_index(self):
        f = self.c.symfile
        if f is None:
            return {}
        f.seek(0)
        h = f.read(0x200)
        lso, lss = struct.unpack_from('<QQ', h, 0x48)
        f.seek(lso)
        nlo, nlc, so, ss, eo, ec = struct.unpack('<6I', f.read(24))
        f.seek(lso + eo)
        ents = f.read(16 * ec)
        idx = {}
        for i in range(ec):
            doff, start, cnt = struct.unpack_from('<QII', ents, 16 * i)
            idx[self.c.base + doff] = (start, cnt)
        self.loc = (lso + nlo, lso + so, ss)
        return idx

    def syms(self, img):
        if img[3] in self.symcache:
            return self.symcache[img[3]]
        out = []
        _, linkedit, symtab = macho(self.c, img[3])
        if symtab and linkedit:
            symoff, nsyms, stroff, strsize = symtab
            nl = self.c.read(linkedit[0] + symoff - linkedit[1], 16 * nsyms)
            st = self.c.read(linkedit[0] + stroff - linkedit[1], strsize)
            out += nlist_syms(nl, st, nsyms)
        if img[3] in self.local:
            start, cnt = self.local[img[3]]
            nlo, so, ss = self.loc
            f = self.c.symfile
            f.seek(nlo + 16 * start)
            nl = f.read(16 * cnt)
            f.seek(so)
            st = f.read(ss)
            out += nlist_syms(nl, st, cnt)
        out.sort()
        self.symcache[img[3]] = (out, [v for v, _ in out])
        return self.symcache[img[3]]

    def lookup(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0 or addr >= self.imgs[i][1]:
            return None
        img = self.imgs[i]
        name = img[2].rsplit('/', 1)[-1]
        syms, vals = self.syms(img)
        j = bisect.bisect_right(vals, addr) - 1
        sym = f'{syms[j][1]}+{addr - syms[j][0]:#x}' if j >= 0 else '?'
        return f'{name}+{addr - img[0]:#x} {sym}'


def main():
    d, slide = sys.argv[1], int(sys.argv[2], 16)
    s = Symbolizer(d)
    if sys.argv[3:] == ['-']:
        for line in sys.stdin:
            if not line.startswith(('SMP', 'CRASH')):
                continue
            print(line.rstrip())
            for a in re.findall(r'0x1[0-9a-f]{8}\b', line):
                r = s.lookup(int(a, 16) - slide)
                if r:
                    print(f'    {a}  {r}')
        return
    for a in sys.argv[3:]:
        r = s.lookup(int(a, 16) - slide)
        print(f'{a}  {r or "(not in the cache)"}')


if __name__ == '__main__':
    main()
