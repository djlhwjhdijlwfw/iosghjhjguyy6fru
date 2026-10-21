#!/bin/bash
# Build the A30s boot image that boots xnu: the phone ADT (tools/mkadt.py),
# the payload (tools/mkpayload.py) and the shim with the payload appended
# (shim/build.sh). Run under WSL.
#
#   tools/a30s-boot.sh KERNEL [BOOT-ARGS]
#
# Then from TWRP: tools/a30s-shim-flash.sh, adb reboot. Volume Up held for
# a second restarts into TWRP whatever xnu does (the shim's A53 watches it).
#
# Default boot-args: one CPU, the console on screen, no watchdog, and
# SweetBattery / cpufreq off (topaz's Qualcomm I2C, GCC, TLMM and cpufreq
# registers; see A30S.md).
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
KERNEL=$(realpath "${1:?kernel Mach-O}")
ARGS=${2:-debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress wdt=-1 sweetbattery=0 sweetcpufreq=0}
O=$HERE/xnu/out/a30s
mkdir -p "$O"
TC=$HOME/ztos-build/a30s/toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
[ -n "${CROSS:-}" ] || [ ! -x "${TC}gcc" ] || export CROSS=$TC

python3 "$HERE/tools/mkadt.py" "$O/a30s.adt"
python3 "$HERE/tools/mkpayload.py" "$KERNEL" "$O/a30s.adt" "$O/payload.bin" --cmdline "$ARGS" > /dev/null
echo "payload: $O/payload.bin ($(stat -c %s "$O/payload.bin") bytes), boot-args: $ARGS"
bash "$HERE/shim/build.sh" "$O/payload.bin" | grep -v '^-rw'
