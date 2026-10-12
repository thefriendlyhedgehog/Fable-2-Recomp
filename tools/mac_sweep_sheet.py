#!/usr/bin/env python3
"""Tile the debug sweep screenshots into one numbered contact sheet.

The game's debug sweep (the 0 key / fn+F10 on macOS, see
src/diagnostics/fable2_debug_sweep.h) saves NN_label.png for every step of
debug_sweep_steps. This halves each (2x2 average), tiles them two per row
with the step number drawn in the corner, writes sheet.png next to them,
and prints for every step how much it differs from step 00 (the baseline):
the mean absolute difference and the share of pixels that changed by more
than 24 of 255. Pure Python (no Pillow); tools/mac_capture.sh runs it.

Usage: python3 tools/mac_sweep_sheet.py [sweep_dir]   (default out/capture/sweep)
"""
import glob
import os
import struct
import sys
import zlib

# 3x5 digit glyphs, rows top to bottom, 3 bits each (MSB = left).
DIGITS = {
    "0": (7, 5, 5, 5, 7), "1": (2, 6, 2, 2, 7), "2": (7, 1, 7, 4, 7), "3": (7, 1, 7, 1, 7),
    "4": (5, 5, 7, 1, 1), "5": (7, 4, 7, 1, 7), "6": (7, 4, 7, 5, 7), "7": (7, 1, 1, 1, 1),
    "8": (7, 5, 7, 5, 7), "9": (7, 5, 7, 1, 7),
}


def read_png(path):
    """(width, height, rows of RGB bytes) for 8-bit RGB / RGBA, non-interlaced PNGs."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    idat = []
    width = height = color_type = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color_type not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {color_type})")
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break
    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = row[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        previous = row
        if channels == 4:
            rgb = bytearray(width * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = row[0::4], row[1::4], row[2::4]
            row = rgb
        rows.append(bytes(row))
    return width, height, rows


def halve(width, height, rows):
    """2x2 box average."""
    out_w, out_h = width // 2, height // 2
    out = []
    for y in range(out_h):
        a, b = rows[2 * y], rows[2 * y + 1]
        row = bytearray(out_w * 3)
        for c in range(3):
            # Sum of the two source rows per channel, then of each pixel pair.
            sa = a[c::3]
            sb = b[c::3]
            col = [sa[i] + sb[i] for i in range(out_w * 2)]
            row[c::3] = bytes((col[2 * i] + col[2 * i + 1] + 2) >> 2 for i in range(out_w))
        out.append(bytes(row))
    return out_w, out_h, out


def difference(base_rows, rows):
    """(mean absolute difference, share of pixels changed by more than 24)."""
    total = changed = count = 0
    for ra, rb in zip(base_rows, rows):
        for i in range(0, min(len(ra), len(rb)) - 2, 3 * 4):  # every 4th pixel
            d = max(abs(ra[i] - rb[i]), abs(ra[i + 1] - rb[i + 1]), abs(ra[i + 2] - rb[i + 2]))
            total += d
            changed += d > 24
            count += 1
    return (total / count, changed / count) if count else (0.0, 0.0)


def draw_number(canvas, canvas_w, x0, y0, text, scale=4):
    for n, ch in enumerate(text):
        glyph = DIGITS.get(ch)
        if not glyph:
            continue
        gx = x0 + n * 4 * scale
        for gy, bits in enumerate(glyph):
            for bx in range(3):
                color = b"\xff\xff\x00" if bits & (4 >> bx) else b"\x00\x00\x00"
                for sy in range(scale):
                    for sx in range(scale):
                        px = gx + bx * scale + sx
                        py = y0 + gy * scale + sy
                        o = (py * canvas_w + px) * 3
                        canvas[o:o + 3] = color


def write_png(path, width, height, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 3:(y + 1) * width * 3]) for y in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sweep_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "out/capture/sweep")
    paths = sorted(p for p in glob.glob(os.path.join(sweep_dir, "[0-9][0-9]_*.png")))
    if not paths:
        print(f"sweep: no screenshots in {sweep_dir} (press 0 in game to run the sweep)")
        return 1
    tiles = []
    for path in paths:
        w, h, rows = read_png(path)
        tiles.append((os.path.basename(path)[:-4],) + halve(w, h, rows))
    base = tiles[0]
    print(f"sweep: {len(tiles)} screenshots in {sweep_dir}; difference from {base[0]}:")
    for name, w, h, rows in tiles:
        mean, share = difference(base[3], rows)
        print(f"  {name:28s} mean {mean:5.1f}, {100 * share:5.1f}% of pixels changed")
    tile_w = max(t[1] for t in tiles)
    tile_h = max(t[2] for t in tiles)
    gap = 6
    columns = 2
    rows_n = (len(tiles) + columns - 1) // columns
    sheet_w = columns * tile_w + (columns + 1) * gap
    sheet_h = rows_n * tile_h + (rows_n + 1) * gap
    canvas = bytearray(sheet_w * sheet_h * 3)
    for index, (name, w, h, rows) in enumerate(tiles):
        x0 = gap + (index % columns) * (tile_w + gap)
        y0 = gap + (index // columns) * (tile_h + gap)
        for y, row in enumerate(rows):
            o = ((y0 + y) * sheet_w + x0) * 3
            canvas[o:o + len(row)] = row
        draw_number(canvas, sheet_w, x0 + 6, y0 + 6, name[:2])
    out = os.path.join(sweep_dir, "sheet.png")
    write_png(out, sheet_w, sheet_h, canvas)
    print(f"sweep: contact sheet {out} ({sheet_w}x{sheet_h}); tiles numbered by step")
    return 0


if __name__ == "__main__":
    sys.exit(main())
