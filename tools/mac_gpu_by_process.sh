#!/usr/bin/env bash
# GPU busy share per process over an interval, from the AGX user clients'
# accumulatedGPUTime in the I/O Registry (no root needed). Run it before GPU
# measurements: remote viewing (RustDesk, Screen Sharing) runs the hardware
# video encoder (VTEncoderXPCService), which can take a third or more of the
# GPU and makes the game's GPU time and the system GPU power meaningless.
#
# Usage: tools/mac_gpu_by_process.sh [seconds]   (default 5)
set -euo pipefail
secs="${1:-5}"
snap() {
    # One entry per AGXDeviceUserClient: its AppUsage (every queue's
    # accumulatedGPUTime, ns) comes before its IOUserClientCreator.
    ioreg -r -c AGXDeviceUserClient -w0 -l 2>/dev/null | awk '
        /\+-o AGXDeviceUserClient/ { t = 0 }
        /"AppUsage"/ { line = $0
            while (match(line, /accumulatedGPUTime"=[0-9]+/)) {
                t += substr(line, RSTART + 20, RLENGTH - 20); line = substr(line, RSTART + RLENGTH) } }
        /IOUserClientCreator/ { match($0, /pid [0-9]+, [^"]*/); a[substr($0, RSTART, RLENGTH)] += t; t = 0 }
        END { for (k in a) print a[k] "\t" k }'
}
before="$(mktemp)"; after="$(mktemp)"
trap 'rm -f "$before" "$after"' EXIT
snap > "$before"; sleep "$secs"; snap > "$after"
awk -F'\t' -v s="$secs" 'NR == FNR { a[$2] = $1; next }
    { d = $1 - a[$2]; if (d > 0) printf "%6.1f%%  %s\n", d / 1e7 / s, $2 }' "$before" "$after" | sort -rn | head -10
