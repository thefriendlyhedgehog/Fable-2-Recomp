#!/usr/bin/env bash
# One run that captures what is needed to debug rendering offline: the Metal
# shaders MoltenVK compiles (MVK_CONFIG_SHADER_DUMP_DIR), the SDK's shader dump
# (--dump_shaders) and one frame's draws (F9 -> vulkan_frame_report), then
# prints a short summary (tools/analyze_mac_capture.py) and copies it to the
# clipboard. Every frame report also dumps the resolves listed in DUMP_RESOLVES
# (the shadow mask, the scene depth and both shadow maps) to
# out/capture/dumps; tools/mac_resolve_dumps.py turns them into PNGs.
# A script (or Claude) can drive the running game by writing commands to
# out/capture/command (debug_command_file: report, sweep, quit, press <button>
# [ms], wait <ms>; write another name and rename it).
#
# Usage: tools/mac_capture.sh [extra game flags...]
#   Usually: tools/mac_capture.sh --merge_controllers --vulkan_hoist_shared_memory_uploads
#   In game: load the save, wait until the problem shows, then keep still for
#   30 seconds (a frame is reported every 20 seconds by itself; 9 / fn+F9 or
#   the command "report" reports the next one at once, 0 or "sweep" runs the
#   debug sweep), then quit (Cmd+Q or the command "quit"). The summary uses
#   the last complete report.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

EXE=out/build/mac-arm64-release/fable_2
OUT=out/capture
if [ ! -x "$EXE" ]; then
    echo "Error: $EXE not found; build first (./build.sh -release fable_2)." >&2
    exit 1
fi
rm -rf "$OUT"
mkdir -p "$OUT/mvk" "$OUT/rex" "$OUT/dumps"
DUMP_RESOLVES=0x12B17000,0x1A02A000,0x12BE1000,0x12FE1000

echo "Starting the game. Load the save, wait until the problem shows, then keep still for"
echo "30 seconds (a frame is reported every 20 s; 9 or fn+F9 reports one at once) and quit"
echo "(Cmd+Q). Debug keys: 6 / 7 / 8 (or fn+F6 / F7 / F8); 0 runs the debug sweep (stand still"
echo "about 25 seconds: it saves a screenshot per setting). The summary appears here afterwards."
MVK_CONFIG_SHADER_DUMP_DIR="$PWD/$OUT/mvk" "$EXE" --game_data_root="$PWD" \
    --dump_shaders="$PWD/$OUT/rex" --vulkan_frame_report_interval=20 \
    --log_max_file_size_mb=100 --debug_sweep_dir="$PWD/$OUT/sweep" \
    --vulkan_debug_dump_dir="$PWD/$OUT/dumps" --vulkan_debug_dump_resolves="$DUMP_RESOLVES" \
    --debug_command_file="$PWD/$OUT/command" "$@" \
    > "$OUT/stdout.log" 2>&1
echo "Game exited ($?)."

python3 tools/analyze_mac_capture.py --mvk "$OUT/mvk" --rex "$OUT/rex" > "$OUT/summary.txt" 2>&1
if ls "$OUT"/sweep/[0-9][0-9]_*.png >/dev/null 2>&1; then
    python3 tools/mac_sweep_sheet.py "$OUT/sweep" >> "$OUT/summary.txt" 2>&1
    if command -v open >/dev/null 2>&1 && [ -f "$OUT/sweep/sheet.png" ]; then
        open "$OUT/sweep/sheet.png"
    fi
fi
if ls "$OUT"/dumps/resolve_*.bin >/dev/null 2>&1; then
    python3 tools/mac_resolve_dumps.py "$OUT/dumps" >> "$OUT/summary.txt" 2>&1
fi
cat "$OUT/summary.txt"
if command -v pbcopy >/dev/null 2>&1; then
    pbcopy < "$OUT/summary.txt"
    echo "(summary copied to the clipboard; full capture in $OUT)"
fi
