#!/usr/bin/env bash
# Frame rate of the render target paths (fbo / fsi) at the trail and at the
# heavy view (the camera turned back to the building, about 4300 draws), one
# run per path, uncapped (guest refresh 120 Hz, so the game's own 30 fps
# cap moves to 60). Screen captures of the window at both views go to
# out/ab_paths/<path>_{trail,heavy}.png.
# Usage: tools/mac_ab_paths.sh [paths...]   (default: fbo fsi)
set -u
cd "$(dirname "$0")/.."
OUT=out/ab_paths
mkdir -p "$OUT"
PATHS=("$@")
[ ${#PATHS[@]} -eq 0 ] && PATHS=(fbo fsi)
LOGDIR=out/build/mac-arm64-release/logs
CMDS="set vulkan_submission_log true;wait 25000;press RL 500;wait 300;press RL 500;wait 300;press RL 500;wait 300;press RL 500;wait 1000;sleep 60"
for P in "${PATHS[@]}"; do
  AUTOTEST_FRAME_MS=0 tools/mac_autotest.sh "$CMDS" --merge_controllers \
    --video_mode_refresh_rate=120 --render_target_path_vulkan="$P" > "$OUT/$P.txt" 2>&1 &
  RUN=$!
  sleep 5
  LOG=""
  for _ in $(seq 1 100); do
    sleep 3
    LOG="$LOGDIR/$(ls -t "$LOGDIR" | head -1)"
    grep -q "command file: set vulkan_submission_log" "$LOG" 2>/dev/null && break
  done
  sleep 15
  screencapture -x -R 640,345,1280,750 "$OUT/${P}_trail.png"
  sleep 25
  screencapture -x -R 640,345,1280,750 "$OUT/${P}_heavy.png"
  wait $RUN
  echo "$P: $LOG"
done
