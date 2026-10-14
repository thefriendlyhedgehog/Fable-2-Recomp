#!/usr/bin/env python3
"""Timeline view of one process in a Metal System Trace: how busy the GPU is
with it, where it idles, and which kinds of encoders take the time.

Usage (macOS): python3 -I tools/gputrace_timeline.py <trace> [--process fable_2]
(record as in tools/summarize_gputrace.py).

From the metal-gpu-intervals table: the union of the process's GPU intervals
over all channels (busy time) against the recorded span, the idle gaps between
them (count and total, by gap length), the GPU time other processes took while
this one idled, and GPU time and interval counts by encoder label (with
MoltenVK, the label names the Vulkan command that opened the encoder, such as
vkCmdDraw or vkCmdCopyBuffer) and by channel.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_gputrace import export_table, parse_table, to_ns  # noqa: E402


def normalize_label(label):
    """'vkQueueSubmit MTLCommandBuffer on Queue 0-0:Blit Command 493   (fable_2
    (23334)) 0xc46f...' -> 'Blit Command #': the encoder kind without ids."""
    if not label:
        return "(none)"
    label = label.split(":", 1)[-1]
    label = re.split(r"\s{2,}|\s\(", label, maxsplit=1)[0]
    return re.sub(r"\d+", "#", label).strip() or "(none)"


def union(intervals):
    merged = []
    for start, end in sorted(intervals):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("trace")
    parser.add_argument("--process", default="fable_2")
    parser.add_argument("--run", default="1")
    parser.add_argument("--top", type=int, default=15)
    args = parser.parse_args()
    xml_text = export_table(args.trace, args.run, "metal-gpu-intervals")
    if xml_text is None:
        sys.exit("no metal-gpu-intervals table in the trace")
    columns, rows = parse_table(xml_text)
    index = {mnemonic: i for i, (mnemonic, _) in enumerate(columns)}

    def cell(row, name, raw=False):
        i = index.get(name)
        if i is None or i >= len(row):
            return ""
        return row[i][1] if raw else row[i][0]

    mine, others = [], []
    by_label = defaultdict(lambda: [0, 0])
    by_channel = defaultdict(lambda: [0, 0])
    frames = set()
    for row in rows:
        start = to_ns(cell(row, "start", raw=True))
        duration = to_ns(cell(row, "duration", raw=True))
        if not duration:
            continue
        if cell(row, "process").startswith(args.process):
            mine.append((start, start + duration))
            label = normalize_label(cell(row, "event-label"))
            by_label[label][0] += duration
            by_label[label][1] += 1
            channel = cell(row, "channel-name") or "(none)"
            by_channel[channel][0] += duration
            by_channel[channel][1] += 1
            frame = cell(row, "frame-number")
            if frame:
                frames.add(frame)
        else:
            others.append((start, start + duration))
    if not mine:
        sys.exit(f"no GPU intervals of {args.process}")
    busy = union(mine)
    span_start, span_end = busy[0][0], busy[-1][1]
    span = span_end - span_start
    busy_ns = sum(end - start for start, end in busy)
    gaps = [(busy[i][1], busy[i + 1][0]) for i in range(len(busy) - 1)]
    frame_count = max(len(frames), 1)
    print(f"{args.process}: {span / 1e6:.0f} ms recorded, GPU busy with it "
          f"{busy_ns / 1e6:.0f} ms ({100.0 * busy_ns / span:.1f}%), "
          f"{len(frames)} frames ({span / 1e6 / frame_count:.1f} ms per frame)")
    print(f"idle gaps: {len(gaps)} ({len(gaps) / frame_count:.0f} per frame), "
          f"{sum(e - s for s, e in gaps) / 1e6:.0f} ms in total")
    buckets = [(0, 10_000, "< 10 us"), (10_000, 100_000, "10-100 us"),
               (100_000, 1_000_000, "0.1-1 ms"), (1_000_000, 10_000_000, "1-10 ms"),
               (10_000_000, 1 << 62, ">= 10 ms")]
    for low, high, name in buckets:
        selected = [e - s for s, e in gaps if low <= e - s < high]
        print(f"  {name:>9}: {len(selected):7d} gaps, {sum(selected) / 1e6:8.1f} ms")
    # Other processes' GPU work inside this process's idle gaps.
    other_busy = union(others)
    overlap = 0
    j = 0
    for gap_start, gap_end in gaps:
        while j < len(other_busy) and other_busy[j][1] <= gap_start:
            j += 1
        k = j
        while k < len(other_busy) and other_busy[k][0] < gap_end:
            overlap += min(gap_end, other_busy[k][1]) - max(gap_start, other_busy[k][0])
            k += 1
    print(f"other processes' GPU work inside those gaps: {overlap / 1e6:.0f} ms")
    total = sum(v[0] for v in by_label.values())
    print(f"by channel (GPU time, overlapping channels add up; {total / 1e6:.0f} ms):")
    for name, (ns, count) in sorted(by_channel.items(), key=lambda kv: -kv[1][0]):
        print(f"  {ns / 1e6:9.1f} ms {100.0 * ns / total:5.1f}% {count / frame_count:8.1f}/frame  {name}")
    print("by encoder label:")
    for name, (ns, count) in sorted(by_label.items(), key=lambda kv: -kv[1][0])[:args.top]:
        print(f"  {ns / 1e6:9.1f} ms {100.0 * ns / total:5.1f}% {count / frame_count:8.1f}/frame  {name[:90]}")


if __name__ == "__main__":
    main()
