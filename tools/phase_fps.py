#!/usr/bin/env python3
"""Frame rate per value of a switch toggled during one run (A/B testing).

Usage: python3 -I tools/phase_fps.py <game log> <cvar> [initial value]

Phases start at each "[input] command file: set <cvar> <value>" line; the
statistics lines ("FSI stats over N frames (T s, ...): ..., R render pass
(re)starts/frame ...") are assigned to the phase they fall in entirely, and
frames / seconds are summed per value. Alternating the value several times
evens out other load on the GPU. Shorten the windows with
vulkan_stats_frames (e.g. 60).
"""
import re
import sys
from collections import defaultdict
from datetime import datetime

log, cvar = sys.argv[1], sys.argv[2]
initial = sys.argv[3] if len(sys.argv) > 3 else "(initial)"
stamp = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]")
stats = re.compile(r"FSI stats over (\d+) frames \(([\d.]+) s, .*?(\d+) render pass \(re\)starts/frame")
setter = re.compile(r"command file: set " + re.escape(cvar) + r" (\S+)")
in_gameplay = re.compile(r"(\d+) draws/frame")

changes = []  # (time, value)
windows = []  # (start, end, frames, seconds, restarts, draws)
for line in open(log, errors="replace"):
    m = stamp.match(line)
    if not m:
        continue
    t = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()
    s = setter.search(line)
    if s:
        changes.append((t, s.group(1)))
        continue
    w = stats.search(line)
    if w:
        d = in_gameplay.search(line)
        windows.append((t - float(w.group(2)), t, int(w.group(1)), float(w.group(2)),
                        int(w.group(3)), int(d.group(1)) if d else 0))
if not changes:
    sys.exit(f"no 'set {cvar}' commands in {log}")
bounds = [(float("-inf"), initial)] + changes + [(float("inf"), None)]
totals = defaultdict(lambda: [0, 0.0, 0, 0])
for start, end, frames, seconds, restarts, draws in windows:
    if draws < 1000:
        continue  # not in gameplay yet
    for (t0, value), (t1, _) in zip(bounds, bounds[1:]):
        if t0 <= start and end <= t1:
            total = totals[value]
            total[0] += frames
            total[1] += seconds
            total[2] += restarts * frames
            total[3] += 1
            break
for value, (frames, seconds, restarts, count) in totals.items():
    print(f"{cvar} = {value}: {frames / seconds:5.2f} fps over {seconds:6.1f} s "
          f"({count} windows), {restarts / frames:5.0f} render pass (re)starts/frame")
