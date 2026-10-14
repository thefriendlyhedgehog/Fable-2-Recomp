#!/usr/bin/env bash
# One unattended test run: starts the game through tools/mac_capture.sh,
# loads the save from the title screen (A, Down, A through the command file),
# waits until gameplay renders, runs the given commands, then quits. The game
# only runs (and loads the GPU) for the length of the test.
#
# Usage: tools/mac_autotest.sh "<commands>" [extra game flags...]
#   <commands>: debug_command_file lines separated by ";", run once gameplay
#   renders, e.g. "wait 5000;report;sweep" (report, sweep, dump N, set <cvar>
#   <value>, press <button> [ms], wait <ms>). "sleep <seconds>" pauses the
#   script itself between batches (press and wait are timed in the game, the
#   other commands run as soon as they are read). The script then waits for
#   the sweep (if any) and for a frame report to complete, and sends "quit".
#   Example (diagonal test):
#     tools/mac_autotest.sh "wait 5000;sweep" --merge_controllers \
#       --debug_sweep_steps="tess:vulkan_quad_patches_in_vs=false;d0:vulkan_quad_patches_in_vs=true"
#   Example (frame rate with and without a switch, 300 frames each):
#     tools/mac_autotest.sh "set vulkan_quad_patches_in_vs false;sleep 45;set
#       vulkan_quad_patches_in_vs true;sleep 45" --merge_controllers
# Readiness is read from the FSI statistics lines (every 300 frames): the
# logos draw about 25 draws per frame, the title screen about 280 (input is
# only taken from then on), gameplay over a thousand.
# Results: out/capture (summary.txt, sweep/, dumps/), as with mac_capture.sh.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

COMMANDS="${1:-}"
shift || true
OUT=out/capture
LOGS=out/build/mac-arm64-release/logs
COMMAND_FILE="$OUT/command"
BOOT_TIMEOUT=180
LOAD_TIMEOUT=240
TEST_TIMEOUT=300

send() {
    mkdir -p "$OUT"
    printf '%s\n' "$@" > "$COMMAND_FILE.tmp" && mv "$COMMAND_FILE.tmp" "$COMMAND_FILE"
}

# Draws per frame of the newest FSI statistics line in the log (0 if none).
draws_per_frame() {
    grep -h "FSI stats" "$LOG" 2>/dev/null | tail -1 | sed -nE 's/.*: ([0-9]+) draws\/frame.*/\1/p' |
        grep . || echo 0
}
count() {
    local n
    n=$(grep -c "$1" "$LOG" 2>/dev/null)
    echo "${n:-0}"
}

newest_log() { ls -t "$LOGS"/*.log 2>/dev/null | head -1; }

BEFORE="$(newest_log)"
tools/mac_capture.sh "$@" > out/autotest_capture.log 2>&1 &
CAPTURE_PID=$!

finish() {
    send quit
    for _ in $(seq 1 60); do
        kill -0 "$CAPTURE_PID" 2>/dev/null || break
        sleep 2
    done
    if kill -0 "$CAPTURE_PID" 2>/dev/null; then
        echo "autotest: the game did not quit; stopping it"
        pkill -x fable_2
        wait "$CAPTURE_PID" 2>/dev/null
    fi
    echo "autotest: $1"
    exit "$2"
}

# Wait for the new log, then for the first statistics line (rendering runs).
LOG=""
for _ in $(seq 1 30); do
    LOG="$(newest_log)"
    [ -n "$LOG" ] && [ "$LOG" != "$BEFORE" ] && break
    LOG=""
    sleep 1
done
[ -n "$LOG" ] || finish "no new log in $LOGS" 1
echo "autotest: log $LOG"
start=$SECONDS
until [ "$(draws_per_frame)" -ge 200 ]; do
    [ $((SECONDS - start)) -lt $BOOT_TIMEOUT ] || finish "the title screen did not appear" 1
    sleep 2
done

# Title screen: A, Down, A (continue the last save).
echo "autotest: loading the save"
send "wait 3000" "press A" "wait 4000" "press DOWN" "wait 1500" "press A"
start=$SECONDS
until [ "$(draws_per_frame)" -ge 1000 ]; do
    [ $((SECONDS - start)) -lt $LOAD_TIMEOUT ] || finish "gameplay did not start (last statistics: $(draws_per_frame) draws per frame)" 1
    sleep 2
done
echo "autotest: in gameplay ($(draws_per_frame) draws per frame)"

# The test.
sweeps_before=$(count "\[sweep\] done")
reports_before=$(count "\[frame\] end")
IFS=';' read -r -a lines <<< "$COMMANDS"
batch=()
for line in "${lines[@]}" ""; do
    if [ -z "$line" ] || [[ "$line" == sleep\ * ]]; then
        if [ ${#batch[@]} -gt 0 ]; then
            while [ -e "$COMMAND_FILE" ]; do sleep 1; done
            send "${batch[@]}"
            batch=()
        fi
        [ -n "$line" ] && sleep "${line#sleep }"
    else
        batch+=("$line")
    fi
done
wants_sweep=0
case ";$COMMANDS;" in *";sweep;"*) wants_sweep=1 ;; esac
start=$SECONDS
while :; do
    [ $((SECONDS - start)) -lt $TEST_TIMEOUT ] || finish "the test did not complete in time" 1
    sweeps=$(count "\[sweep\] done")
    reports=$(count "\[frame\] end")
    if [ "$sweeps" -ge $((sweeps_before + wants_sweep)) ] && [ "$reports" -gt "$reports_before" ] &&
        [ ! -e "$COMMAND_FILE" ]; then
        break
    fi
    sleep 2
done
finish "done; summary in $OUT/summary.txt" 0
