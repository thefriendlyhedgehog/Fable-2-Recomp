#!/usr/bin/env python3
"""Summarize a Metal System Trace (Instruments / xctrace): where the GPU time went.

Usage (macOS):
    xcrun xctrace record --template 'Metal System Trace' --attach fable_2 \\
        --time-limit 10s --output heavy.trace
    python3 tools/summarize_gputrace.py heavy.trace

For each GPU table in the trace (GPU intervals per encoder and channel, and the
shader profiler's per-shader intervals when present) this exports the table
with `xcrun xctrace export` and prints the total GPU time grouped by each of
the table's text columns (channel, encoder or command buffer label, shader
name, ...), largest first. Short enough to paste. The tables' column names are
read from the export itself, so nothing depends on a particular Xcode version.
Add --columns to list every table's columns, or --table <schema> to summarize
another table.
"""
import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

DEFAULT_TABLES = [
    "metal-gpu-intervals",
    "metal-shader-profiler-intervals",
    "gpu-shader-profiler-interval",
]
# Column types that are times or numbers rather than something to group by.
NON_GROUP_TYPES = {
    "start-time", "event-time", "duration", "sample-time", "fixed-decimal",
    "uint64", "uint32", "sint64", "sint32", "double", "boolean", "size-in-bytes",
    "percent", "time-since-epoch", "start", "count",
}


def export_table(trace, run, schema):
    xpath = f'/trace-toc/run[@number="{run}"]/data/table[@schema="{schema}"]'
    result = subprocess.run(
        ["xcrun", "xctrace", "export", "--input", trace, "--xpath", xpath],
        capture_output=True, text=True)
    if result.returncode != 0 or "<row" not in result.stdout:
        return None
    return result.stdout


def parse_table(xml_text):
    """Returns (columns, rows): columns = [(mnemonic, engineering type)], each
    row a list of (display text, raw text) per column. Resolves id/ref."""
    root = ET.fromstring(xml_text)
    columns = []
    schema = root.find(".//schema")
    if schema is not None:
        for col in schema.findall("col"):
            mnemonic = (col.findtext("mnemonic") or col.findtext("name") or "?").strip()
            etype = (col.findtext("engineering-type") or "").strip()
            columns.append((mnemonic, etype))
    by_id = {}

    def value_of(elem):
        ref = elem.get("ref")
        if ref is not None:
            return by_id.get(ref, ("", ""))
        text = (elem.text or "").strip()
        display = elem.get("fmt") or text
        if not display:
            # Composite cells (e.g. a process with a pid child): use the
            # first child's display text.
            for child in elem:
                display = value_of(child)[0]
                if display:
                    break
        value = (display, text)
        if elem.get("id") is not None:
            by_id[elem.get("id")] = value
        # Register nested ids too (later refs may point at them).
        for child in elem.iter():
            if child is not elem and child.get("id") is not None and child.get("id") not in by_id:
                ctext = (child.text or "").strip()
                by_id[child.get("id")] = (child.get("fmt") or ctext, ctext)
        return value

    rows = []
    for row in root.iter("row"):
        rows.append([value_of(cell) for cell in row])
    return columns, rows


def to_ns(raw):
    try:
        return int(raw)
    except ValueError:
        try:
            return int(float(raw))
        except ValueError:
            return 0


def summarize(name, columns, rows, top):
    duration_index = None
    for i, (mnemonic, etype) in enumerate(columns):
        if etype == "duration" or mnemonic == "duration":
            duration_index = i
            break
    print(f"\n=== {name}: {len(rows)} intervals")
    if duration_index is None:
        print("    (no duration column; columns: "
              + ", ".join(m for m, _ in columns) + ")")
        return
    total = sum(to_ns(r[duration_index][1]) for r in rows if duration_index < len(r))
    print(f"    total GPU time {total / 1e6:.1f} ms")
    for i, (mnemonic, etype) in enumerate(columns):
        if i == duration_index or etype in NON_GROUP_TYPES:
            continue
        groups = defaultdict(lambda: [0, 0])
        for r in rows:
            if i >= len(r) or duration_index >= len(r):
                continue
            key = r[i][0] or "(empty)"
            groups[key][0] += to_ns(r[duration_index][1])
            groups[key][1] += 1
        if len(groups) <= 1 and mnemonic not in ("channel", "channel-name"):
            continue  # one value for every row: nothing to compare
        print(f"  by {mnemonic} ({len(groups)} values):")
        for key, (ns, count) in sorted(groups.items(), key=lambda kv: kv[1][0], reverse=True)[:top]:
            pct = 100.0 * ns / total if total else 0.0
            print(f"    {ns / 1e6:9.1f} ms {pct:5.1f}%  {count:6d}x  {key[:90]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("trace")
    parser.add_argument("--run", default="1", help="run number in the trace (default 1)")
    parser.add_argument("--table", action="append", help="table schema to summarize")
    parser.add_argument("--top", type=int, default=8, help="values shown per column")
    parser.add_argument("--columns", action="store_true", help="only list the columns")
    args = parser.parse_args()
    tables = args.table or DEFAULT_TABLES
    found = False
    for schema in tables:
        xml_text = export_table(args.trace, args.run, schema)
        if xml_text is None:
            print(f"\n=== {schema}: not in this trace (or empty)")
            continue
        found = True
        columns, rows = parse_table(xml_text)
        if args.columns:
            print(f"\n=== {schema}")
            for mnemonic, etype in columns:
                print(f"    {mnemonic:40s} {etype}")
            continue
        summarize(schema, columns, rows, args.top)
    if not found:
        sys.exit(1)


if __name__ == "__main__":
    main()
