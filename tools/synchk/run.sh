#!/bin/bash
# Syntax-check xnu's bsd/dev/sweet_ufs.c without a Codemagic run: clang with
# xnu's warning flags (makedefs/MakeInc.def WARNFLAGS_STD) for arm64-apple,
# against prelude.h instead of the kernel headers (every #include becomes an
# empty stub). Catches typos and type errors in the driver, not link errors.
#
#   tools/synchk/run.sh [FILE]      (WSL; default ~/xnu-a30s/bsd/dev/sweet_ufs.c)
#
# CLANG_ROOT: clang 14 unpacked without sudo (default ~/clang14/root):
#   apt-get download clang-14 libclang-cpp14 libclang-common-14-dev
#   for d in *.deb; do dpkg -x $d ~/clang14/root; done
# xnu builds this file without -Werror, so warnings are reported, not fatal.
# Known on the unchanged file: -Wcast-align in sweet_ufs_ioctl and
# -Wmissing-noreturn on sweet_gpt_watch; -Wdeclaration-after-statement is hidden.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
FILE=${1:-$HOME/xnu-a30s/bsd/dev/sweet_ufs.c}
CLANG_ROOT=${CLANG_ROOT:-$HOME/clang14/root}
LLVM=$CLANG_ROOT/usr/lib/llvm-14
export LD_LIBRARY_PATH=$CLANG_ROOT/usr/lib/x86_64-linux-gnu:$LLVM/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

STUBS=$(mktemp -d)
trap 'rm -rf "$STUBS"' EXIT
grep -oE '^#include <[^>]+>' "$FILE" | sed -E 's/#include <(.*)>/\1/' | while read -r h; do
	mkdir -p "$STUBS/$(dirname "$h")"
	: > "$STUBS/$h"
done

"$LLVM/bin/clang" -target arm64-apple-macos13 -ffreestanding -nostdinc \
	-isystem "$LLVM/lib/clang/14.0.0/include" -I "$STUBS" -include "$HERE/prelude.h" \
	-fsyntax-only -std=gnu11 \
	-Weverything -Wno-pedantic -Wno-bad-function-cast -Wno-bitwise-instead-of-logical \
	-Wno-c++-compat -Wno-c++98-compat -Wno-conditional-uninitialized \
	-Wno-covered-switch-default -Wno-disabled-macro-expansion \
	-Wno-documentation-unknown-command -Wno-extra-semi-stmt -Wno-format-non-iso \
	-Wno-language-extension-token -Wno-missing-variable-declarations -Wno-packed \
	-Wno-padded -Wno-partial-availability -Wno-reserved-id-macro -Wno-shift-sign-overflow \
	-Wno-switch-enum -Wno-unaligned-access -Wno-undef -Wno-unused-macros \
	-Wno-used-but-marked-unused -Wno-variadic-macros -Wno-vla -Wno-zero-length-array \
	-Wno-unknown-warning-option -Wno-anon-enum-enum-conversion \
	-Wno-declaration-after-statement -Wno-sign-compare -Wno-sign-conversion \
	-Wpointer-arith "$FILE"
echo "synchk: $FILE parsed"
