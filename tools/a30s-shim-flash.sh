#!/bin/bash
# Put the a30s shim on BOOT from TWRP, keeping what BOOT held before.
#
#   tools/a30s-shim-flash.sh             save BOOT once, then write shim/out/shim-a30s-boot.img
#   tools/a30s-shim-flash.sh --restore   write the saved BOOT back
#
# The TWRP checks and the verified write are ztOS's (devices/a30s: adb.sh,
# flash-boot.sh, which also insists on the stock backups). Never reboots:
# after it, `adb reboot` starts the shim. Run under WSL or Git Bash.
#   ZTOS  the ztOS checkout (default /mnt/d/ios30/ztOS)
#   ADB   adb to use (default: adb on PATH, else C:\platform-tools\adb.exe)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
ZTOS=${ZTOS:-/mnt/d/ios30/ztOS}
if [ -z "${ADB:-}" ] && ! command -v adb > /dev/null 2>&1 && ! command -v adb.exe > /dev/null 2>&1; then
	for a in /mnt/c/platform-tools/adb.exe /c/platform-tools/adb.exe; do
		[ -x "$a" ] && ADB=$a && break
	done
fi
. "$ZTOS/devices/a30s/device.sh"
. "$A30S_DIR/adb.sh"
SHIM=$HERE/shim/out/shim-a30s-boot.img
SAVED=$HERE/shim/out/boot-before-shim.img

if [ "${1:-}" = --restore ]; then
	[ -s "$SAVED" ] || { echo "no saved BOOT at $SAVED" >&2; exit 1; }
	exec "$A30S_DIR/flash-boot.sh" "$SAVED"
fi

[ -s "$SHIM" ] || { echo "$SHIM missing: run shim/build.sh first" >&2; exit 1; }
require_twrp
NODE=$(part_node boot)
if [ ! -s "$SAVED" ]; then
	# what is there now (ztOS, or whatever was flashed last), checked like backup.sh does
	want=$(adb_sh sha256sum "$NODE" | cut -d' ' -f1)
	"$ADB" exec-out "dd if=$NODE bs=1048576 2>/dev/null" > "$SAVED.part"
	[ "$(sha256sum "$SAVED.part" | cut -d' ' -f1)" = "$want" ] || { rm -f "$SAVED.part"; echo "BOOT copy does not match the phone" >&2; exit 1; }
	if grep -q -a 'a30s milestone' "$SAVED.part"; then
		rm -f "$SAVED.part"
		echo "BOOT already holds the shim: restore ztOS first (ztOS flash-boot.sh <its boot image>)" >&2
		exit 1
	fi
	mv "$SAVED.part" "$SAVED"
	echo "saved BOOT ($NODE) -> $SAVED"
fi
"$A30S_DIR/flash-boot.sh" "$SHIM"
echo "next: adb reboot   (the shim starts; hold Volume Up 1 s to come back to TWRP)"
