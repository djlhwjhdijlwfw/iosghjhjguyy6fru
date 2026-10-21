#!/bin/bash
# Build a local xnu-8796.141.3 tree with the patch series applied the way
# darwin-xnu-build does it (staging scripts first, then every patch in
# order with git apply), committed as a baseline. New patches are made by
# editing that tree and `git diff`-ing it against the baseline.
#
#   tools/xnu-tree.sh [TREE]          default ~/xnu-a30s
#   tools/xnu-tree.sh [TREE] --upto N stop after patch N (to rework an old one)
#
# Needs network for the pristine source and the staging scripts' sources.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
TREE=${1:-$HOME/xnu-a30s}
UPTO=${3:-9999}
[ "${2:-}" = --upto ] || UPTO=9999

if [ ! -d "$TREE/.git" ]; then
	git clone -q --depth 1 -b xnu-8796.141.3 https://github.com/apple-oss-distributions/xnu "$TREE"
fi
cd "$TREE"
git reset -q --hard
git clean -q -fdx
git checkout -q --detach "$(git rev-list --max-parents=0 HEAD | tail -1)" 2>/dev/null || true
git reset -q --hard
git clean -q -fdx

export MACHINE_CONFIG=SWEET MACOS_VERSION=13.5
# Off macOS: the staging scripts only copy TargetConditionals.h from the SDK
# (for builds); an empty stand-in is enough for a tree to read and diff
if ! command -v xcrun > /dev/null; then
	fake=$(mktemp -d)
	mkdir -p "$fake/bin" "$fake/sdk/usr/include"
	: > "$fake/sdk/usr/include/TargetConditionals.h"
	printf '#!/bin/sh\necho %s/sdk\n' "$fake" > "$fake/bin/xcrun"
	chmod +x "$fake/bin/xcrun"
	export PATH="$fake/bin:$PATH"
fi
for s in "$HERE"/xnu/patches/*.sh; do
	echo "script $(basename "$s")"
	bash "$s"
done
for p in "$HERE"/xnu/patches/*.patch; do
	n=$(basename "$p" | cut -c1-4)
	[ "$((10#$n))" -le "$UPTO" ] || break
	if git apply --check "$p" 2>/dev/null; then
		git apply "$p"
	else
		echo "SKIPPED $(basename "$p")" >&2
	fi
done
git add -A
git -c user.name=xnu-tree -c user.email=xnu-tree@localhost commit -q -m "baseline: series applied" --allow-empty
echo "tree at $TREE: $(git log --oneline -1)"
