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

# data/dir.manifest is merged rather than copied: the build may already have
# written $DEST/data/dir.manifest listing only its own scripts\recomp\*
# entries (tools/ensure_recomp_manifest.cmake). That file is newer than the
# content's, so a newer-wins copy would keep it and leave every game file
# unlisted (invisible to the game). The merge keeps every content line in
# order, then the destination-only lines, compared case-insensitively with /
# and \ alike, in the content manifest's line ending. Idempotent.
MANIFEST_BACKUP=""
if [ -f "$DEST/data/dir.manifest" ]; then
    MANIFEST_BACKUP="$(mktemp "${TMPDIR:-/tmp}/fable2_dir_manifest.XXXXXX")"
    cp -p "$DEST/data/dir.manifest" "$MANIFEST_BACKUP"
fi

cp -p "$ROOT/default.xex" "$DEST/"
for d in data nxeart '$SystemUpdate'; do
    [ -d "$ROOT/$d" ] || continue
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --update --exclude=/dir.manifest "$ROOT/$d/" "$DEST/$d/"
    else
        mkdir -p "$DEST/$d"
        cp -Rp "$ROOT/$d/." "$DEST/$d/"
    fi
done

if [ -f "$ROOT/data/dir.manifest" ]; then
    MERGED="$DEST/dir.manifest.staging"
    # FNR == NR: first file (the content manifest). The line ending is taken
    # from its first line (the shipped manifest uses CRLF).
    awk '
        FNR == 1 && FNR == NR { eol = ($0 ~ /\r$/) ? "\r\n" : "\n" }
        {
            line = $0
            sub(/\r$/, "", line)
            if (line == "") next
            key = tolower(line)
            gsub(/\\/, "/", key)
            if (key in seen) next
            seen[key] = 1
            printf "%s%s", line, eol
        }
    ' "$ROOT/data/dir.manifest" ${MANIFEST_BACKUP:+"$MANIFEST_BACKUP"} > "$MERGED"
    mv "$MERGED" "$DEST/data/dir.manifest"
elif [ -n "$MANIFEST_BACKUP" ]; then
    cp -p "$MANIFEST_BACKUP" "$DEST/data/dir.manifest"
fi
if [ -n "$MANIFEST_BACKUP" ]; then
    rm -f "$MANIFEST_BACKUP"
fi
echo "Staged content from $ROOT -> $DEST"
