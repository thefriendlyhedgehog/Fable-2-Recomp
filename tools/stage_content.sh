#!/usr/bin/env bash
# Stage the game content next to the built executable so the build folder
# runs in place (the macOS/Linux counterpart of tools/stage_content.cmd).
# Incremental: later runs only copy files that changed.
#
# Usage: tools/stage_content.sh <content_root> <dest_dir>
#   e.g. tools/stage_content.sh . out/build/mac-arm64-release
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: stage_content.sh <content_root> <dest_dir>" >&2
    exit 1
fi
ROOT="$1"
DEST="$2"

if [ ! -f "$ROOT/default.xex" ]; then
    echo "Error: no default.xex in $ROOT - extract the game content there first." >&2
    exit 1
fi
mkdir -p "$DEST"

cp -p "$ROOT/default.xex" "$DEST/"
for d in data nxeart '$SystemUpdate'; do
    [ -d "$ROOT/$d" ] || continue
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --update "$ROOT/$d/" "$DEST/$d/"
    else
        mkdir -p "$DEST/$d"
        cp -Rp "$ROOT/$d/." "$DEST/$d/"
    fi
done
echo "Staged content from $ROOT -> $DEST"
