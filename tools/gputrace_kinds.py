#!/usr/bin/env python3
"""Per command buffer of one process in a Metal System Trace: wall time by
encoder kind (render pass, dispatch, copy to image, buffer copy; overlaps
counted once, by the encoder that started first), the idle time between
encoders inside the buffer, and how many encoders of each kind. Averaged
over the command buffers with more than 30 encoders (the game's frames).

Usage (macOS): python3 -I tools/gputrace_kinds.py <trace> [--process fable_2]
"""
import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_gputrace import export_table, parse_table, to_ns  # noqa: E402

KINDS = {
    "vkCmdBeginRenderPass RenderEncoder": "render",
    "Metal renderpass restart RenderEncoder": "render",
    "vkCmdDispatch ComputeEncoder": "dispatch",
    "vkCmdCopyBufferToImage BlitEncoder": "copy to image",
    "vkCmdCopyBuffer BlitEncoder": "buffer copy",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--process", default="fable_2")
    args = parser.parse_args()
    xml = export_table(args.trace, 1, "metal-gpu-intervals")
    if xml is None:
        sys.exit("no metal-gpu-intervals table in the trace")
    columns, rows = parse_table(xml)
    index = {name: i for i, (name, _) in enumerate(columns)}

    def get(row, column, raw=False):
        return row[index[column]][1 if raw else 0]

    by_buffer = defaultdict(dict)
    for row in rows:
        label = get(row, "event-label")
        if args.process not in label or "Blit Command" in label:
            continue
        start = to_ns(get(row, "start", True))
        end = start + to_ns(get(row, "duration", True))
        encoders = by_buffer[get(row, "cmdbuffer-id")]
        encoder = get(row, "encoder-id")
        if encoder in encoders:
            e = encoders[encoder]
            e[1] = min(e[1], start)
            e[2] = max(e[2], end)
        else:
            name = label.split(":", 1)[-1]
            name = re.sub(r"\s+\(.*", "", re.sub(r" \d+", " #", name)).strip()
            encoders[encoder] = [KINDS.get(name, "other"), start, end]
    totals = defaultdict(float)
    counts = defaultdict(float)
    idle = span = 0.0
    frames = 0
    for encoders in by_buffer.values():
        if len(encoders) <= 30:
            continue
        frames += 1
        items = sorted(encoders.values(), key=lambda e: e[1])
        cursor = items[0][1]
        span += items[-1][2] - items[0][1] if items else 0
        span = span
        end_all = max(e[2] for e in items)
        span += 0
        for kind, start, end in items:
            counts[kind] += 1
            if start > cursor:
                idle += start - cursor
                cursor = start
            if end > cursor:
                totals[kind] += end - cursor
                cursor = end
        span += end_all - items[-1][2]
    if not frames:
        sys.exit("no command buffers with more than 30 encoders")
    print("%d command buffers; per command buffer:" % frames)
    for kind in sorted(totals, key=lambda k: -totals[k]):
        print("  %-14s %6.2f ms  %5.1f encoders" % (kind, totals[kind] / frames / 1e6,
                                                   counts[kind] / frames))
    print("  %-14s %6.2f ms" % ("idle between", idle / frames / 1e6))


if __name__ == "__main__":
    main()
