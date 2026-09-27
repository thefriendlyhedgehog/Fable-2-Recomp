#!/usr/bin/env python3
"""Summarize a macOS `sample` report: per thread, where the time went.

Usage (macOS):
    sample fable_2 10 -file fable2_sample.txt      # 10 s while the game runs
    python3 tools/summarize_sample.py fable2_sample.txt

`sample` prints a call tree per thread, each line carrying the number of
samples that were inside that frame. This script computes, per thread, the
self time of every function (samples in the function itself, not in its
callees) and prints the threads with the most samples and their top
functions, short enough to paste. Waiting frames (mach_msg, cond waits,
nanosleep, ...) are listed like any other, so a thread that mostly waits
shows its wait call at the top.
"""
import re
import sys
from collections import defaultdict

THREAD_RE = re.compile(r"^\s{4}(\d+)\s+(Thread_\d+.*)$")
# A frame line: tree prefix ("+ ! : |" and spaces), count, symbol, "(in lib)".
FRAME_RE = re.compile(r"^(\s*(?:[+!:|]\s+)*)(\d+)\s+(.+?)\s+\(in ([^)]+)\)")
# Frames with no library info, e.g. "??? (in <unknown binary>)" is covered
# above; bare addresses look like "123 0x1234" and are kept as-is.
BARE_RE = re.compile(r"^(\s*(?:[+!:|]\s+)*)(\d+)\s+(\?\?\?|0x[0-9a-fA-F]+)\s*")


def parse(path):
    threads = []  # (name, total, {symbol: self_count})
    cur = None
    stack = []  # [(depth, count, key, child_sum)]

    def pop_to(depth):
        # Close frames at >= depth, crediting self time.
        while stack and stack[-1][0] >= depth:
            d, count, key, child = stack.pop()
            cur["self"][key] += max(0, count - child)
            if stack:
                pd, pc, pk, pch = stack[-1]
                stack[-1] = (pd, pc, pk, pch + count)

    in_graph = False
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("Call graph:"):
                in_graph = True
                continue
            if not in_graph:
                continue
            if line.strip() == "" or line.startswith("Total number in stack") or \
                    line.startswith("Sort by top of stack"):
                if cur is not None:
                    pop_to(-1)
                if line.startswith("Total number in stack") or \
                        line.startswith("Sort by top of stack"):
                    break
                continue
            m = THREAD_RE.match(line)
            if m:
                if cur is not None:
                    pop_to(-1)
                cur = {"name": m.group(2).strip(), "total": int(m.group(1)),
                       "self": defaultdict(int)}
                threads.append(cur)
                stack = []
                continue
            if cur is None:
                continue
            m = FRAME_RE.match(line) or BARE_RE.match(line)
            if not m:
                continue
            depth = len(m.group(1))
            count = int(m.group(2))
            sym = m.group(3)
            lib = m.group(4) if m.re is FRAME_RE else "?"
            # Drop "+ offset" and argument lists to group by function.
            sym = re.sub(r"\s*\+\s*\d+.*$", "", sym)
            key = f"{sym} [{lib}]"
            pop_to(depth)
            stack.append((depth, count, key, 0))
    if cur is not None:
        pop_to(-1)
    return threads


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    threads = parse(sys.argv[1])
    if not threads:
        print("No 'Call graph:' threads found; is this a `sample` report?")
        sys.exit(1)
    top_threads = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    top_funcs = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    grand = sum(t["total"] for t in threads)
    print(f"{len(threads)} threads, {grand} samples in total")
    for t in sorted(threads, key=lambda t: t["total"], reverse=True)[:top_threads]:
        print(f"\n{t['total']:6d}  {t['name'][:90]}")
        for key, n in [kv for kv in sorted(t["self"].items(), key=lambda kv: kv[1], reverse=True) if kv[1] > 0][:top_funcs]:
            pct = 100.0 * n / t["total"] if t["total"] else 0.0
            print(f"        {pct:5.1f}%  {key[:100]}")


if __name__ == "__main__":
    main()
