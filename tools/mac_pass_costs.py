#!/usr/bin/env python3
"""GPU time per host render pass, from a Metal System Trace and the game log.

With --vulkan_pass_log=true the game labels every Metal render encoder with
the position of its render pass in the frame ("<submission>.<pass> ...",
through a copy of the VkRenderPass named after it, as MoltenVK uses the render
pass's debug name as the encoder label), and every 5 seconds logs the passes of
one frame ("[passes]": target, draws, end reason, the most used pixel shaders)
and the render areas they were begun with ("[pass areas]").

Usage (macOS, the game running at the view to look at):
    xcrun xctrace record --template 'Metal System Trace' --attach fable_2 \\
        --time-limit 5s --output out/profile/passes.trace
    python3 tools/mac_pass_costs.py out/profile/passes.trace \\
        out/build/mac-arm64-release/logs/fable_2_NNN.log [top]

Prints the median vertex and fragment time of each pass position (top N by
their sum, default 30) with the log's description of that position in the last
logged frame, then the per-frame totals by kind of pass. Encoders of one
command buffer overlap on the GPU, so the times add up to more than the frame;
compare them between runs rather than with the frame time. The encoder names
are fixed when a position is first seen, so only their position is used.
"""
import os
import re
import statistics
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_gputrace import export_table, parse_table, to_ns  # noqa: E402


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    trace, log = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 30

    cols, rows = parse_table(export_table(trace, 1, "metal-gpu-intervals"))
    ix = {name: i for i, (name, _) in enumerate(cols)}

    def get(row, column, raw=False):
        return row[ix[column]][1 if raw else 0]

    encoders = defaultdict(lambda: {"v": 0, "f": 0, "label": ""})
    for row in rows:
        label = get(row, "event-label")
        if "fable_2" not in label or "MTLCommandBuffer" not in label:
            continue
        m = re.search(r":(.*?)\s{2,}\(", label)
        enc = encoders[(get(row, "cmdbuffer-id"), get(row, "encoder-id"))]
        enc["label"] = m.group(1) if m else ""
        channel = get(row, "channel-name")
        duration = to_ns(get(row, "duration", True))
        if channel == "Vertex":
            enc["v"] += duration
        elif channel == "Fragment":
            enc["f"] += duration

    per_pass = defaultdict(list)
    for enc in encoders.values():
        m = re.match(r"(\d+)\.(\d+) ", enc["label"])
        if m:
            per_pass[(int(m.group(1)), int(m.group(2)))].append((enc["v"], enc["f"]))
    if not per_pass:
        print("No labeled render encoders: was the game run with --vulkan_pass_log=true?")
        sys.exit(1)

    lines = [line.split("[passes]", 1)[1].rstrip() for line in open(log, errors="replace")
             if "[passes]" in line]
    starts = [i for i, line in enumerate(lines) if re.match(r"\s*submission 0:", line)]
    description = {}
    for line in lines[starts[-1] if starts else 0:]:
        m = re.match(r"\s+(\d+)\.(\d+) (.*)", line)
        if m:
            description[(int(m.group(1)), int(m.group(2)))] = m.group(3)

    frames = max(len(v) for v in per_pass.values())
    print(f"{frames} frames in the trace (most occurrences of one pass position)")
    table = []
    for pos, samples in per_pass.items():
        vertex = statistics.median(s[0] for s in samples) / 1e3
        fragment = statistics.median(s[1] for s in samples) / 1e3
        table.append((vertex + fragment, pos, vertex, fragment, len(samples)))
    print(f"{'pass':<7}{'vert us':>8}{'frag us':>9}{'n':>6}  log")
    for _, pos, vertex, fragment, count in sorted(table, reverse=True)[:top]:
        print(f"{pos[0]}.{pos[1]:<5}{vertex:8.0f}{fragment:9.0f}{count:6d}  "
              f"{description.get(pos, '(not in the logged frame)')[:140]}")

    kinds = defaultdict(lambda: [0.0, 0.0, 0.0])
    for pos, samples in per_pass.items():
        desc = description.get(pos, "")
        m = re.search(r"\| (\d+) draws", desc)
        draws = int(m.group(1)) if m else -1
        if not desc:
            kind = "not in the logged frame"
        elif desc.startswith("transfer"):
            kind = "transfer, <= 1 draw" if draws <= 1 else "transfer + draws"
        elif draws <= 2:
            kind = "1-2 draws"
        elif draws < 100:
            kind = "3-99 draws"
        else:
            kind = "100+ draws"
        kinds[kind][0] += len(samples) / frames
        kinds[kind][1] += sum(s[0] for s in samples) / frames / 1e6
        kinds[kind][2] += sum(s[1] for s in samples) / frames / 1e6
    print()
    print("Per frame by kind of pass: passes, vertex ms, fragment ms")
    for kind, (count, vertex, fragment) in sorted(kinds.items(), key=lambda k: -k[1][2]):
        print(f"  {kind:<24}{count:6.1f}{vertex:7.2f}{fragment:7.2f}")


if __name__ == "__main__":
    main()
