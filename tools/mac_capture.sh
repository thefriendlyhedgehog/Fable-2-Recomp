#!/usr/bin/env bash
# One run that captures what is needed to debug rendering offline: the Metal
# shaders MoltenVK compiles (MVK_CONFIG_SHADER_DUMP_DIR), the SDK's shader dump
# (--dump_shaders) and one frame's draws (F9 -> vulkan_frame_report), then
# prints a short summary (tools/analyze_mac_capture.py) and copies it to the
# clipboard.
#
# Usage: tools/mac_capture.sh [extra game flags...]
#   In game: load the save, wait until the problem shows, then keep still for
#   30 seconds (a frame is reported every 20 seconds by itself, and fn+F9
#   reports the next one at once), then quit (Cmd+Q). The summary uses the
#   last complete report.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

EXE=out/build/mac-arm64-release/fable_2
OUT=out/capture
if [ ! -x "$EXE" ]; then
    echo "Error: $EXE not found; build first (./build.sh -release fable_2)." >&2
    exit 1
fi
rm -rf "$OUT"
mkdir -p "$OUT/mvk" "$OUT/rex"

echo "Starting the game. Load the save, wait until the problem shows, then keep still for"
echo "30 seconds (a frame is reported every 20 s; fn+F9 reports one at once) and quit (Cmd+Q)."
echo "The summary appears here afterwards."
MVK_CONFIG_SHADER_DUMP_DIR="$PWD/$OUT/mvk" "$EXE" --game_data_root="$PWD" \
    --dump_shaders="$PWD/$OUT/rex" --vulkan_frame_report_interval=20 \
    --log_max_file_size_mb=100 "$@" \
    > "$OUT/stdout.log" 2>&1
echo "Game exited ($?)."

python3 tools/analyze_mac_capture.py --mvk "$OUT/mvk" --rex "$OUT/rex" > "$OUT/summary.txt" 2>&1
cat "$OUT/summary.txt"
if command -v pbcopy >/dev/null 2>&1; then
    pbcopy < "$OUT/summary.txt"
    echo "(summary copied to the clipboard; full capture in $OUT)"
fi
