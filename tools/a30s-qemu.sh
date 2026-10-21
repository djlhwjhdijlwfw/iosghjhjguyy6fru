#!/bin/bash
# Boot an xnu kernel the way the A30s would, but in QEMU's virt board with a
# GIC-400-like GICv2: the --qemu ADT (tools/mkadt.py), a payload, and
# tools/qemu-sweet.py. Run under WSL (QEMU's QMP socket needs a Linux file
# system, so the work directory is in $HOME).
#
#   tools/a30s-qemu.sh KERNEL [SECONDS] [BOOT-ARGS]
#
#   QEMU      qemu-system-aarch64 (default: ~/qemu-local/root, unpacked from
#             the Ubuntu packages with its libraries)
# Writes ~/q/a30s/run/fb.png (the 720x1560 screen), regs.txt, qemu.log.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
KERNEL=$(realpath "${1:?kernel Mach-O}")
SECS=${2:-60}
ARGS=${3:-debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress wdt=-1 sweetbattery=0 sweetcpufreq=0}
W=$HOME/q/a30s
mkdir -p "$W"
if [ -z "${QEMU:-}" ]; then
	R=$HOME/qemu-local/root
	export LD_LIBRARY_PATH=$R/usr/lib/x86_64-linux-gnu:$R/lib/x86_64-linux-gnu
	export QEMU=$R/usr/bin/qemu-system-aarch64
fi
python3 "$HERE/tools/mkadt.py" "$W/qemu.adt" --qemu
python3 "$HERE/tools/mkpayload.py" "$KERNEL" "$W/qemu.adt" "$W/payload.bin" --cmdline "$ARGS" > /dev/null
rm -rf "$W/run"
python3 "$HERE/tools/qemu-sweet.py" "$W/payload.bin" "$W/run" --gic 2 --smp ${SMP:-2} --secs "$SECS" || true
grep -c 'Taking exception' "$W/run/qemu.log" | sed 's/^/exceptions: /' || true
echo "screen: $W/run/fb.png"
