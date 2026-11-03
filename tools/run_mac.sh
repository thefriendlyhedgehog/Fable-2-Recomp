#!/usr/bin/env bash
# Launch the macOS build with the repository root as the game content root.
# Usage: tools/run_mac.sh [game flags...]
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

GAME=out/build/mac-arm64-release/fable_2
LOGDIR=out/build/mac-arm64-release/logs
if [ ! -x "$GAME" ]; then
    echo "No macOS build at $GAME." >&2
    echo "Build it first: ./build.sh -release fable_2" >&2
    exit 1
fi

# Game content stays in the repository root. The build's fable_2.toml usually
# points there already, but a fresh build does not have one, so default the
# flag unless the caller passed their own.
has_root=0
for arg in "$@"; do
    case "$arg" in
        --game_data_root|--game_data_root=*) has_root=1 ;;
    esac
done
[ "$has_root" = 1 ] || set -- --game_data_root="$PWD" "$@"

mkdir -p "$LOGDIR"
echo "Starting $GAME (logs: $LOGDIR)"
exec "$GAME" "$@"
