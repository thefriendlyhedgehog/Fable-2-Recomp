#!/usr/bin/env python3
"""GPU timeline of a few command buffers of one process in a Metal System
Trace, in wall-clock order: each long render encoder on its own line, the
short encoders between them as one run (R render, D dispatch, I copy to
image, B buffer copy) with the time from the run's start to its end.

Usage (macOS): python3 -I tools/gputrace_frame.py <trace> [--process fable_2]
    [--count 2] [--long-ms 0.3]
(record as in tools/summarize_gputrace.py). Prints the command buffers in
the middle of the trace with more than 30 encoders (the game's frames; with
the Fable II trail about two per game frame). The unlabeled "Blit Command"
intervals on the compute channel are left out: they overlap the other
encoders instead of running between them.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from summarize_gputrace import export_table, parse_table, to_ns  # noqa: E402

SHORT = {
    "vkCmdBeginRenderPass RenderEncoder": "R",
    "Metal renderpass restart RenderEncoder": "R",
    "vkCmdDispatch ComputeEncoder": "D",
    "vkCmdCopyBufferToImage BlitEncoder": "I",
    "vkCmdCopyBuffer BlitEncoder": "B",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--process", default="fable_2")
    parser.add_argument("--count", type=int, default=2)
    parser.add_argument("--long-ms", type=float, default=0.3)
    args = parser.parse_args()
    xml = export_table(args.trace, 1, "metal-gpu-intervals")
    if xml is None:
        sys.exit("no metal-gpu-intervals table in the trace")
    columns, rows = parse_table(xml)
    index = {name: i for i, (name, _) in enumerate(columns)}

    def get(row, column, raw=False):
        return row[index[column]][1 if raw else 0]

    by_buffer = defaultdict(list)
    for row in rows:
        label = get(row, "event-label")
        if args.process in label and "Blit Command" not in label:
            by_buffer[get(row, "cmdbuffer-id")].append(row)
    buffers = []
    for buffer_rows in by_buffer.values():
        encoders = []
        for row in sorted(buffer_rows, key=lambda r: to_ns(get(r, "start", True))):
            encoder = get(row, "encoder-id")
            start = to_ns(get(row, "start", True))
            end = start + to_ns(get(row, "duration", True))
            if encoders and encoders[-1][0] == encoder:
                encoders[-1][2] = min(encoders[-1][2], start)
                encoders[-1][3] = max(encoders[-1][3], end)
            else:
                label = get(row, "event-label").split(":", 1)[-1]
                label = re.sub(r"\s+\(.*", "", re.sub(r" \d+", " #", label)).strip()
                encoders.append([encoder, SHORT.get(label, "?"), start, end])
        buffers.append(encoders)
    buffers = sorted((b for b in buffers if len(b) > 30), key=lambda b: b[0][2])
    if not buffers:
        sys.exit("no command buffers with more than 30 encoders")
    middle = len(buffers) // 2
    for encoders in buffers[middle:middle + args.count]:
        t0 = encoders[0][2]
        print("--- command buffer: %d encoders, %.2f ms" %
              (len(encoders), (max(e[3] for e in encoders) - t0) / 1e6))
        run, run_start, run_end = [], None, None
        for _, kind, start, end in encoders + [[None, None, None, None]]:
            if kind is None or (kind == "R" and end - start > args.long_ms * 1e6):
                if run:
                    print("%8.2f  %6.2f ms  %3d short encoders: %s" %
                          ((run_start - t0) / 1e6, (run_end - run_start) / 1e6, len(run),
                           "".join(run)[:90]))
                run, run_start = [], None
                if kind is not None:
                    print("%8.2f  %6.2f ms  render pass" % ((start - t0) / 1e6, (end - start) / 1e6))
                continue
            if run_start is None:
                run_start = start
            run.append(kind)
            run_end = end


if __name__ == "__main__":
    main()
