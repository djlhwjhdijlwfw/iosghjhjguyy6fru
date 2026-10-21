#!/bin/bash
# Read xnu's persistent console log (sweet_ufs.c "sweetlog", written with
# ufsrw=1) from the A30s in TWRP: the last 1025 blocks (4 KiB) of userdata,
# decoded by tools/sweet-log.py. Only reads.
#
#   tools/a30s-log-pull.sh OUT.txt     (raw area kept as OUT.txt.bin)
#
# The eMMC transport (xnu 0145) keeps the partition in whole 4 KiB blocks
# from its 4 KiB aligned start, so "the last 1025 whole blocks of the block
# device" is the same area the kernel writes. Run under WSL or Git Bash.
#   ZTOS    the ztOS checkout (default /mnt/d/ios30/ztOS)
#   ADB     adb to use (default: adb on PATH, else C:\platform-tools\adb.exe)
#   PYTHON  python 3 (default: python3, else python)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: a30s-log-pull.sh OUT.txt}
ZTOS=${ZTOS:-/mnt/d/ios30/ztOS}
[ -d "$ZTOS" ] || ZTOS=/d/ios30/ztOS
if [ -z "${ADB:-}" ] && ! command -v adb > /dev/null 2>&1 && ! command -v adb.exe > /dev/null 2>&1; then
	for a in /mnt/c/platform-tools/adb.exe /c/platform-tools/adb.exe; do
		[ -x "$a" ] && ADB=$a && break
	done
fi
. "$ZTOS/devices/a30s/device.sh"
. "$A30S_DIR/adb.sh"
if [ -z "${PYTHON:-}" ]; then
	PYTHON=python3
	"$PYTHON" -c '' > /dev/null 2>&1 || PYTHON=python
fi

require_twrp
NODE=$(part_node userdata)
bytes=$(adb_sh blockdev --getsize64 "$NODE")
blocks=$((bytes / 4096))
[ "$blocks" -gt 2050 ] || { echo "$NODE: only $blocks blocks" >&2; exit 1; }
skip=$((blocks - 1025))
"$ADB" exec-out "dd if=$NODE bs=4096 skip=$skip count=1025 2>/dev/null" > "$out.bin"
echo "$NODE: $bytes bytes, read blocks $skip..$((blocks - 1)) ($(wc -c < "$out.bin") bytes)" >&2
# Git Bash runs the Windows python: it needs Windows paths
pypath() { if command -v cygpath > /dev/null 2>&1; then cygpath -w "$1"; else printf '%s\n' "$1"; fi; }
"$PYTHON" "$(pypath "$HERE/tools/sweet-log.py")" "$(pypath "$out.bin")" "$(pypath "$out")"
