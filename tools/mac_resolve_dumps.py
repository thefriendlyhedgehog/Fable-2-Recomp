#!/usr/bin/env python3
"""Turn resolve dumps into PNGs and print value statistics.

The SDK writes a resolve's result during frame reports when its destination
address is listed in vulkan_debug_dump_resolves (tools/mac_capture.sh sets
it, together with vulkan_debug_dump_dir). Each file is
resolve_<n>_r<resolve>_<address>_f<format>_e<endian>_<w>x<h>_<bpp>bpp.bin:
untiled rows of guest pixels, bytes as stored in guest memory.

For every dump this writes <name>.png next to it and prints min / max /
mean and the share of zero and of full-scale values per channel:
- 8 bpp (e.g. format 2, k_8): gray, the byte as stored.
- 32 bpp: the dword after undoing the copy endian swap (e 0 none, 1 8in16,
  2 8in32, 3 16in32) as R = bits 0-7, G = 8-15, B = 16-23, A = 24-31, and
  also <name>_hi24.png, bits 8-31 stretched from min to max (24-bit depth
  of a depth resolve is in the upper bits).
Other sizes are only summarized. Pure Python (no Pillow).

Usage: python3 tools/mac_resolve_dumps.py [dump_dir]   (default out/capture/dumps)
"""
import glob
import os
import re
import struct
import sys
import zlib

NAME = re.compile(r"resolve_(\d+)_r(\d+)_([0-9A-F]{8})_f(\d+)_e(\d+)_(\d+)x(\d+)_(\d+)bpp\.bin$")


def write_png(path, width, height, channels, rows):
    """rows: list of bytes, width * channels each (8-bit gray or RGBA)."""
    color_type = {1: 0, 4: 6}[channels]
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def stats(label, values, full):
    n = len(values)
    if not n:
        return f"{label}: empty"
    zero = sum(1 for v in values if v == 0)
    top = sum(1 for v in values if v == full)
    return (f"{label}: min {min(values)} max {max(values)} mean {sum(values) / n:.1f}, "
            f"{100.0 * zero / n:.1f}% zero, {100.0 * top / n:.1f}% at {full}")


def unswap32(data, endian):
    if endian == 1:  # 8in16
        return b"".join(data[i + 1:i + 2] + data[i:i + 1] for i in range(0, len(data), 2))
    if endian == 2:  # 8in32
        return b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))
    if endian == 3:  # 16in32
        return b"".join(data[i + 2:i + 4] + data[i:i + 2] for i in range(0, len(data), 4))
    return data


def convert(path):
    m = NAME.search(os.path.basename(path))
    if not m:
        return f"{os.path.basename(path)}: name not understood"
    _, resolve, address, fmt, endian, w, h, bpp = m.groups()
    fmt, endian, w, h, bpp = int(fmt), int(endian), int(w), int(h), int(bpp)
    with open(path, "rb") as f:
        data = f.read()
    head = f"resolve {resolve} to 0x{address} format {fmt} endian {endian} {w}x{h} {bpp} bpp"
    base = path[:-4]
    lines = [head]
    if bpp == 8 and len(data) == w * h:
        write_png(base + ".png", w, h, 1, [data[y * w:(y + 1) * w] for y in range(h)])
        lines.append("  " + stats("value", data, 255))
    elif bpp == 32 and len(data) == w * h * 4:
        data = unswap32(data, endian)
        dwords = struct.unpack(f"<{w * h}I", data)
        write_png(base + ".png", w, h, 4, [data[y * w * 4:(y + 1) * w * 4] for y in range(h)])
        for shift, name in ((0, "R"), (8, "G"), (16, "B"), (24, "A")):
            lines.append("  " + stats(name, [(d >> shift) & 0xFF for d in dwords], 255))
        hi = [d >> 8 for d in dwords]
        lo, top = min(hi), max(hi)
        span = max(top - lo, 1)
        gray = bytes((v - lo) * 255 // span for v in hi)
        write_png(base + "_hi24.png", w, h, 1, [gray[y * w:(y + 1) * w] for y in range(h)])
        lines.append("  " + stats("bits 8-31", hi, 0xFFFFFF))
    else:
        lines.append(f"  {len(data)} bytes; no PNG for this size")
    return "\n".join(lines)


def main():
    dump_dir = sys.argv[1] if len(sys.argv) > 1 else "out/capture/dumps"
    paths = sorted(glob.glob(os.path.join(dump_dir, "resolve_*.bin")))
    if not paths:
        print(f"resolve dumps: none in {dump_dir}")
        return 0
    print(f"resolve dumps: {len(paths)} in {dump_dir} (PNGs written next to them)")
    for path in paths:
        print(convert(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
