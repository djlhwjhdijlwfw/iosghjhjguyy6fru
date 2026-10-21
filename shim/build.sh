#!/bin/bash
# Build the a30s shim and wrap it in the Android boot image v1 that the
# Galaxy A30s's S-BOOT takes, with ztOS's packer (devices/a30s/mkbootimg.py):
# base 0x10000000, 2 KiB pages, no DTB in the image (S-BOOT passes the one
# from the DTB partition), Samsung's SEANDROIDENFORCE trailer.
#
# Milestone 1 is diagnostics only: no xnu payload yet (loader.c, mmu.c and
# ufs.c are topaz's and not built).
#
#   CROSS  aarch64 toolchain prefix (default aarch64-linux-gnu-)
#   ZTOS   the ztOS checkout with devices/a30s (default /mnt/d/ios30/ztOS)
set -euo pipefail
cd "$(dirname "$0")"
CROSS=${CROSS:-aarch64-linux-gnu-}
ZTOS=${ZTOS:-/mnt/d/ios30/ztOS}
MKBOOTIMG=${MKBOOTIMG:-$ZTOS/devices/a30s/mkbootimg.py}
CC=${CROSS}gcc
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-tree-loop-distribute-patterns -Wall -Wextra"
OBJS=
mkdir -p out
for f in start.S shim.c console.c fdt.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
	OBJS="$OBJS out/${f%.*}.o"
done
# S-BOOT runs the image wherever it loads it with no relocation pass: only
# the header's image_size (start.o, linked at 0) may be an absolute address.
abs=$(${CROSS}readelf -r out/shim.o out/console.o out/fdt.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	$OBJS -o out/shim.elf
${CROSS}objcopy -O binary out/shim.elf out/Image
# S-BOOT copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$((16#$(${CROSS}nm out/shim.elf | awk '$3=="_end"{print $1}')))
truncate -s "$size" out/Image
# An empty ramdisk, as ztOS's boot image has.
printf '' | gzip -n > out/ramdisk.gz
python3 -I "$MKBOOTIMG" --kernel out/Image --ramdisk out/ramdisk.gz \
	--cmdline "console=null" --board SRPSF18A003RU --base 0x10000000 \
	--kernel-offset 0x00008000 --ramdisk-offset 0x01000000 --second-offset 0x00f00000 \
	--tags-offset 0x00000100 --pagesize 2048 --max-size 37748736 -o out/shim-a30s-boot.img
sha256sum out/shim-a30s-boot.img | tee out/shim-a30s-boot.img.sha256
# Host render of the console layout.
gcc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test
ls -l out/Image out/shim-a30s-boot.img
