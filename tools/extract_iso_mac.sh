#!/usr/bin/env bash
# Extract default.xex + data/ from a Fable 2 GOTY disc image on macOS and
# verify the executable against the supported-version list.
#
# Usage: tools/extract_iso_mac.sh <game.iso> [--dest <dir>] [--force] [--skip-hash]
#
# The default destination is the repository root (where tools/run_mac.sh and
# the game look for the content). The default extraction tool is
# extract-xiso (https://github.com/XboxDev/extract-xiso), built on first use
# under out/tooling/; set EXTRACT_XISO=/path/to/extract-xiso to use another
# copy (or put one on PATH).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO=$PWD

usage() {
    sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

ISO=""
DEST=$REPO
FORCE=0
SKIP_HASH=0
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage 0 ;;
        --dest) DEST=${2:?--dest needs a directory}; shift 2 ;;
        --dest=*) DEST=${1#--dest=}; shift ;;
        --force) FORCE=1; shift ;;
        --skip-hash) SKIP_HASH=1; shift ;;
        -*) echo "unknown option: $1" >&2; usage 1 ;;
        *) ISO=$1; shift ;;
    esac
done
[ -n "$ISO" ] || usage 1
[ -f "$ISO" ] || { echo "Error: no such disc image: $ISO" >&2; exit 1; }
ISO=$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)

if { [ -e "$DEST/default.xex" ] || [ -e "$DEST/data" ]; } && [ "$FORCE" != 1 ]; then
    echo "Error: $DEST already contains game content." >&2
    echo "Use --dest <empty dir>, or --force to extract over it." >&2
    exit 1
fi

# 1. Locate or build extract-xiso (the same tool the Windows launcher's
#    extractor is derived from).
TOOL=${EXTRACT_XISO:-}
if [ -z "$TOOL" ]; then
    if command -v extract-xiso >/dev/null 2>&1; then
        TOOL=$(command -v extract-xiso)
    elif [ -x "$REPO/out/tooling/extract-xiso/build/extract-xiso" ]; then
        TOOL=$REPO/out/tooling/extract-xiso/build/extract-xiso
    else
        for dep in git cmake; do
            command -v "$dep" >/dev/null 2>&1 || {
                echo "Error: $dep is needed to build extract-xiso" >&2
                echo "Either install it, or set EXTRACT_XISO to an extract-xiso binary." >&2
                exit 1
            }
        done
        SRC=$REPO/out/tooling/extract-xiso
        echo "Fetching and building extract-xiso ..."
        [ -d "$SRC/.git" ] || git clone --depth 1 https://github.com/XboxDev/extract-xiso "$SRC"
        cmake -S "$SRC" -B "$SRC/build" >/dev/null
        cmake --build "$SRC/build" --parallel >/dev/null
        TOOL=$SRC/build/extract-xiso
    fi
fi
[ -x "$TOOL" ] || { echo "Error: $TOOL is not executable" >&2; exit 1; }

# 2. Extract the whole disc tree; the game uses default.xex + data/ from it.
echo "Extracting $ISO -> $DEST"
"$TOOL" -x "$ISO" -d "$DEST"

# 3. Check the executable against the supported GOTY list in
#    constants/game_versions.h (the game itself rejects anything else).
if [ ! -f "$DEST/default.xex" ]; then
    echo "Error: extraction finished without $DEST/default.xex" >&2
    exit 1
fi
if [ "$SKIP_HASH" = 1 ]; then
    echo "Skipping the default.xex hash check (--skip-hash)."
else
    ACTUAL=$(shasum -a 256 "$DEST/default.xex" | awk '{print $1}')
    MATCH=$(grep -m1 "\"$ACTUAL\"" constants/game_versions.h || true)
    if [ -z "$MATCH" ]; then
        echo "Error: unsupported default.xex (sha256 $ACTUAL)." >&2
        echo "The supported dumps are listed in constants/game_versions.h and docs/GERMAN_GOTY_SUPPORT.md." >&2
        echo "Use --skip-hash only if you know the image is fine." >&2
        exit 1
    fi
    EDITION=$(printf '%s\n' "$MATCH" | awk -F'"' '{print $6}')
    echo "OK: default.xex matches ${EDITION:-a supported edition} ($ACTUAL)"
fi

echo
if [ "$DEST" = "$REPO" ]; then
    echo "Done. Start the game with: tools/run_mac.sh"
else
    echo "Done. Start the game with: tools/run_mac.sh --game_data_root \"$DEST\""
    echo "(or set game_data_root in out/build/mac-arm64-release/fable_2.toml)"
fi
