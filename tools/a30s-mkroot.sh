#!/bin/bash
# Build the A30s iOS root image in WSL: tools/mkiosroot.sh (OS + SystemOS +
# AppOS of the iPhone X 16.7.16 IPSW into one HFS+ volume) and then
# tools/sweet-root-tweaks.py on it, as README "Build the iOS root image" does.
#
#   sudo bash tools/a30s-mkroot.sh [IPSW_DIR] [OUT.img] [SIZE]
#
#   IPSW_DIR   the members tools/remotezip.py fetched
#              (default /mnt/d/ios16s30/ipsw/iPhone10,3_16.7.16_20H392)
#   OUT.img    default ~/ios16-root-20H392.img of the sudo user: on WSL's own
#              disk, the image is loop-mounted
#   SIZE       default 16G (userdata is 23628 MiB; ztOS's flash-root.sh needs
#              whole 256 MiB pieces)
#   APFS_FUSE  apfs-fuse binary (default ~/apfs-build/apfs-fuse/build/apfs-fuse)
#   HFSPLUS_KO hfsplus module when the kernel has none (WSL's has not;
#              default ~/wslkernel/hfsplus.ko, built from Microsoft's source)
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run it with sudo" >&2; exit 1; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
U=${SUDO_USER:-root}
UH=$(getent passwd "$U" | cut -d: -f6)
IPSW=${1:-/mnt/d/ios16s30/ipsw/iPhone10,3_16.7.16_20H392}
OUT=${2:-$UH/ios16-root-20H392.img}
SIZE=${3:-16G}
APFS_FUSE=${APFS_FUSE:-$UH/apfs-build/apfs-fuse/build/apfs-fuse}
HFSPLUS_KO=${HFSPLUS_KO:-$UH/wslkernel/hfsplus.ko}

for t in mkfs.hfsplus fsck.hfsplus rsync; do
	command -v "$t" > /dev/null || { echo "$t missing: apt-get install hfsprogs rsync" >&2; exit 1; }
done
[ -x "$APFS_FUSE" ] || { echo "no apfs-fuse at $APFS_FUSE" >&2; exit 1; }
if ! grep -qw hfsplus /proc/filesystems; then
	modprobe hfsplus 2> /dev/null || insmod "$HFSPLUS_KO"
	grep -qw hfsplus /proc/filesystems || { echo "the kernel cannot mount HFS+" >&2; exit 1; }
fi
# iPhone10,3 20H392 (BuildManifest, D22AP Erase identity): OS, SystemOS, AppOS
OS=$IPSW/087-86698-044.dmg
SYSOS=$IPSW/087-86593-047.dmg
APPOS=$IPSW/087-86715-047.dmg
for f in "$OS" "$SYSOS" "$APPOS"; do
	[ -s "$f" ] || { echo "missing $f" >&2; exit 1; }
done

PATH=$(dirname "$APFS_FUSE"):$PATH bash "$HERE/tools/mkiosroot.sh" "$OS" "$SYSOS" "$APPOS" "$OUT" "$SIZE"

M=$(mktemp -d)
mount -t hfsplus -o loop,force "$OUT" "$M"
if ! python3 "$HERE/tools/sweet-root-tweaks.py" "$M"; then
	umount "$M"
	exit 1
fi
df -h "$M" | tail -1
umount "$M"
rmdir "$M"
fsck.hfsplus -f -y "$OUT" | grep -v 'Illegal name' || true
fsck.hfsplus -f -n "$OUT"
chown "$U": "$OUT"
sha256sum "$OUT" | tee "$OUT.sha256"
chown "$U": "$OUT.sha256"
echo "A30S_ROOT_OK $OUT"
