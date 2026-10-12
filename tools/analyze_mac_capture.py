#!/usr/bin/env python3
"""Summarize a macOS capture: the Metal shaders MoltenVK compiled, and one
frame's draws (vulkan_frame_report), short enough to paste.

Capture (see docs/macos_port.md, "Capturing a frame"):
    MVK_CONFIG_SHADER_DUMP_DIR=$PWD/out/capture/mvk ./run_and_watch.sh \\
        --game_data_root="$PWD" --dump_shaders="$PWD/out/capture/rex"
    (in game: fn+F9 once, wait a second, quit)
    python3 tools/analyze_mac_capture.py

Shaders: for the fragment shaders that emulate EDRAM with fragment shader
interlock (they use xe_edram), whether Metal got the raster order group that
serializes overlapping fragments (without it, overlapping fragments race and
flicker), whether they read the sample mask, and whether vertex shaders have
an invariant position. Draws: the "[frame]" lines of the newest log, grouped
by pixel shader and render state with counts, and every resolve. Textures:
the textures the frame's draws sample (address, format, size, layout), and
for every resolve, the draws that then sample what it wrote and as which
format (how shadow maps and other render-to-texture results are read back).
"""
import argparse
import collections
import glob
import hashlib
import os
import re
import sys


def newest_log(log_dir):
    logs = sorted(glob.glob(os.path.join(log_dir, "fable_2_*.log")), key=os.path.getmtime)
    return logs[-1] if logs else None


def guest_hashes_by_spirv(rex_dir):
    """Maps sha1(SPIR-V) -> guest shader name from the SDK's dump_shaders files
    (shader_<ucode hash>_<modification>.<prefix>.bin.<stage>)."""
    result = {}
    for path in glob.glob(os.path.join(rex_dir, "shader_*.bin.*")):
        with open(path, "rb") as f:
            result[hashlib.sha1(f.read()).hexdigest()] = os.path.basename(path)
    return result


def analyze_shaders(mvk_dir, rex_dir, show, detail=False):
    metal_files = sorted(glob.glob(os.path.join(mvk_dir, "*.metal")))
    if not metal_files:
        print(f"shaders: no .metal files in {mvk_dir} (was MVK_CONFIG_SHADER_DUMP_DIR set?)")
        return
    by_spirv = guest_hashes_by_spirv(rex_dir) if rex_dir and os.path.isdir(rex_dir) else {}
    fsi = []
    vs_total = vs_invariant = 0
    vs_not_invariant = []
    failed = 0
    for path in metal_files:
        with open(path, errors="replace") as f:
            text = f.read()
        name = os.path.basename(path)
        if text.startswith("Failed to convert"):
            failed += 1
            continue
        if "-vs-" in name or "-tes-" in name:
            vs_total += 1
            if re.search(r"\[\[\s*position\s*,\s*invariant\s*\]\]", text):
                vs_invariant += 1
            else:
                spv = path[:-len(".metal")] + ".spv"
                guest = ""
                if by_spirv and os.path.exists(spv):
                    with open(spv, "rb") as f:
                        guest = by_spirv.get(hashlib.sha1(f.read()).hexdigest(), "")
                vs_not_invariant.append((name, guest or "not a guest shader (SDK internal)"))
            continue
        if "-fs-" not in name or "xe_edram" not in text:
            continue
        # The attributes of every declaration that mentions xe_edram.
        decls = [line.strip() for line in text.splitlines()
                 if "xe_edram" in line and "[[" in line]
        rog = any("raster_order_group" in d for d in decls)
        spv = path[:-len(".metal")] + ".spv"
        guest = ""
        if by_spirv and os.path.exists(spv):
            with open(spv, "rb") as f:
                guest = by_spirv.get(hashlib.sha1(f.read()).hexdigest(), "")
        fsi.append((name, rog, "sample_mask" in text, decls, guest, path))
    print(f"shaders: {len(metal_files)} Metal files, {failed} failed to convert")
    print(f"  vertex/tessellation: {vs_total}, with an invariant position: {vs_invariant}")
    if detail:
        for name, guest in vs_not_invariant:
            print(f"    not invariant: {name}  <- {guest}")
    with_rog = sum(1 for s in fsi if s[1])
    print(f"  FSI (EDRAM) fragment shaders: {len(fsi)}, with raster_order_group on xe_edram: "
          f"{with_rog}, without: {len(fsi) - with_rog}")
    print(f"  FSI fragment shaders reading the sample mask: {sum(1 for s in fsi if s[2])}")
    without = [s for s in fsi if not s[1]]
    for name, rog, sample_mask, decls, guest, path in (without or fsi)[:show]:
        print(f"  {'WITHOUT' if not rog else 'with'} raster order group: {name}"
              f"{'  <- ' + guest if guest else ''}")
        for d in decls[:3]:
            print(f"      {d[:160]}")
    if fsi:
        print(f"  example FSI shader for a closer look: {(without or fsi)[0][5]}")
    if detail and without:
        # Every line of the shader that touches the EDRAM buffer, and the
        # declarations of the other EDRAM shaders for comparison.
        with open(without[0][5], errors="replace") as f:
            lines = f.read().splitlines()
        print(f"  {without[0][0]}: {len(lines)} lines; lines with xe_edram:")
        for i, line in enumerate(lines):
            if "xe_edram" in line:
                print(f"    {i + 1:5d}: {line.strip()[:170]}")
        with_rog = [s_ for s_ in fsi if s_[1]]
        if with_rog:
            print(f"  for comparison, {with_rog[0][0]}:")
            for d in with_rog[0][3][:2]:
                print(f"      {d[:170]}")


# Xenos texture formats (TextureFormat; resolve destination formats use the
# same numbers).
TEXTURE_FORMATS = (
    "1_REVERSE 1 8 1_5_5_5 5_6_5 6_5_5 8_8_8_8 2_10_10_10 8_A 8_B 8_8 Cr_Y1_Cb_Y0_REP "
    "Y1_Cr_Y0_Cb_REP 16_16_EDRAM 8_8_8_8_A 4_4_4_4 10_11_11 11_11_10 DXT1 DXT2_3 DXT4_5 "
    "16_16_16_16_EDRAM 24_8 24_8_FLOAT 16 16_16 16_16_16_16 16_EXPAND 16_16_EXPAND "
    "16_16_16_16_EXPAND 16_FLOAT 16_16_FLOAT 16_16_16_16_FLOAT 32 32_32 32_32_32_32 32_FLOAT "
    "32_32_FLOAT 32_32_32_32_FLOAT 32_AS_8 32_AS_8_8 16_MPEG 16_16_MPEG 8_INTERLACED "
    "32_AS_8_INTERLACED 32_AS_8_8_INTERLACED 16_INTERLACED 16_MPEG_INTERLACED "
    "16_16_MPEG_INTERLACED DXN 8_8_8_8_AS_16_16_16_16 DXT1_AS_16_16_16_16 "
    "DXT2_3_AS_16_16_16_16 DXT4_5_AS_16_16_16_16 2_10_10_10_AS_16_16_16_16 "
    "10_11_11_AS_16_16_16_16 11_11_10_AS_16_16_16_16 32_32_32_FLOAT DXT3A DXT5A CTX1 "
    "DXT3A_AS_1_1_1_1 8_8_8_8_GAMMA_EDRAM 2_10_10_10_FLOAT_EDRAM").split()
BYTES_PER_PIXEL = {"8_8_8_8": 4, "2_10_10_10": 4, "16_16": 4, "16_16_FLOAT": 4, "32_FLOAT": 4,
                   "24_8": 4, "24_8_FLOAT": 4, "10_11_11": 4, "11_11_10": 4,
                   "16_16_16_16": 8, "16_16_16_16_FLOAT": 8, "32_32_FLOAT": 8, "8": 1,
                   "8_8": 2, "5_6_5": 2, "16": 2, "16_FLOAT": 2,
                   "32_32_32_32_FLOAT": 16}


def format_name(number):
    number = int(number)
    return TEXTURE_FORMATS[number] if number < len(TEXTURE_FORMATS) else str(number)


TEX_RE = re.compile(
    r"(v?)t(\d+): 0x([0-9A-F]+) fmt (\d+) (\d+)x(\d+)x(\d+) dim (\d) (\w+) sign (\d+) "
    r"num (\d) swz ([0-9A-F]+) exp (-?\d+) filt (\d+) endian (\d) pitch (\d+) "
    r"mips (\d+)-(\d+)( packed)? mipaddr 0x([0-9A-F]+) clamp (\d+) border (\d)")
RESOLVE_RE = re.compile(
    r"resolve (\d+): dest 0x([0-9A-F]+) format (\d+) pitch/height 0x([0-9A-F]+), source (\d+) "
    r"\(sample select (\d+)\), msaa (\d+)x")


def textures_of(line):
    """[(key, description)] for the textures listed on a draw line."""
    if "| tex" not in line:
        return []
    result = []
    for m in TEX_RE.finditer(line.split("| tex", 1)[1]):
        (vertex, fetch, address, fmt, width, height, depth, dim, layout, sign, num, swizzle,
         exp_adjust, filters, endian, pitch, mip_min, mip_max, packed, mip_address, clamp,
         border) = m.groups()
        description = (f"{'vertex ' if vertex else ''}0x{address} {format_name(fmt)} "
                       f"{width}x{height}{'x' + depth if depth != '1' else ''} {layout}"
                       f"{' sign ' + sign if sign != '0000' else ''}{' int' if num == '1' else ''}"
                       f" swz {swizzle}{' exp ' + exp_adjust if exp_adjust != '0' else ''}"
                       f" endian {endian} mips {mip_min}-{mip_max}{' packed' if packed else ''}"
                       f" filt {filters} clamp {clamp}")
        result.append(((int(address, 16), description), int(fetch)))
    return result


DRAW_RE = re.compile(
    r"\[frame\] draw \d+: vs (\w+) ps (\w+) prim (\d+) count (\d+) \((\w+)\) \| msaa (\d+)x "
    r"\(pipeline (\d+)x\) pitch (\d+) \|(.*?) depth fmt (\d+) base (\d+) test (\d+) func (\d+) "
    r"write (\d+) stencil (\d+) \| alpha test (\d+) func (\d+) to-coverage (\d+) \| cull (\w*) "
    r"poly offset (\d)(\d)( \| new pass)?")


def analyze_frame(log, top, find=()):
    with open(log, errors="replace") as f:
        lines = f.read().splitlines()
    begins = [i for i, line in enumerate(lines) if "[frame] begin" in line]
    if not begins:
        f9 = sum(1 for line in lines if "F9: logging" in line)
        print(f"frame: no '[frame] begin' in {log}; F9 presses that reached the game: {f9} "
              f"(tools/mac_capture.sh also reports a frame every 20 s by itself)")
        return
    # The newest complete report (the game may have quit in the middle of one).
    complete = []
    for start in begins:
        end = next((i for i in range(start + 1, len(lines))
                    if "[frame] end" in lines[i] or "[frame] begin" in lines[i]), None)
        if end is not None and "[frame] end" in lines[end]:
            complete.append((start, end))
    if not complete:
        start, end = begins[-1], len(lines) - 1
        print(f"frame: {lines[start].split('[frame] ')[1]} ... (no end)")
    else:
        start, end = complete[-1]
        stamp = re.match(r"\[(\d{4}-\d\d-\d\d [\d:.]+)\]", lines[start])
        print(f"frame: {lines[start].split('[frame] ')[1]} ... {lines[end].split('[frame] ')[1]}"
              f" (the last of {len(complete)} complete reports"
              f"{', logged ' + stamp.group(1) if stamp else ''})")
    frame = lines[start:end + 1]
    groups = collections.Counter()
    msaa = collections.Counter()
    primitives = 0
    found = collections.Counter()
    found_textures = collections.defaultdict(collections.Counter)
    # Texture -> [draw count, set of pixel shaders]; and every draw's position
    # in the frame with the addresses it samples, for the resolves.
    textures = collections.defaultdict(lambda: [0, set()])
    sampled = []
    for position, line in enumerate(frame):
        m = DRAW_RE.search(line)
        if not m:
            continue
        draw_textures = textures_of(line)
        for key, _ in draw_textures:
            textures[key][0] += 1
            textures[key][1].add(m.group(2))
        sampled.append((position, m.group(2), draw_textures))
        for h in find:
            if h.upper() in (m.group(1).upper(), m.group(2).upper()):
                found[re.sub(r" \| tex.*$", "", re.sub(r"^draw \d+: ", "", line.split("[frame] ", 1)[1]))] += 1
                for key, fetch in draw_textures:
                    found_textures[h][f"t{fetch} {key[1]}"] += 1
        (vs, ps, prim, count, packet, samples, pipeline_samples, pitch, targets, dfmt, dbase,
         ztest, zfunc, zwrite, stencil, atest, afunc, a2c, cull, po_f, po_b, new_pass) = m.groups()
        primitives += int(count)
        msaa[samples] += 1
        targets = re.sub(r"base \d+ ", "", targets.strip())
        key = (f"ps {ps} msaa {samples}x{' pipeline ' + pipeline_samples + 'x' if pipeline_samples != samples else ''}"
               f" | {targets or 'no color'} | depth {'test f' + zfunc if ztest == '1' else 'off'}"
               f"{' write' if zwrite == '1' else ''}{' stencil' if stencil == '1' else ''}"
               f"{' | alpha test f' + afunc if atest == '1' else ''}{' | A2C' if a2c == '1' else ''}"
               f"{' | poly offset' if '1' in (po_f + po_b) else ''}")
        groups[key] += 1
    print(f"  draws by guest MSAA: {dict(sorted(msaa.items()))}, indices/vertices: {primitives}")
    for key, n in groups.most_common(top):
        print(f"  {n:5d}x {key}")
    for h in find:
        hits = [(k, n) for k, n in found.items() if h.upper() in k.upper()]
        print(f"  draws using {h}: {sum(n for _, n in hits)}")
        for k, n in sorted(hits, key=lambda kv: -kv[1])[:10]:
            shown = re.sub(r" count \d+", "", k)[:230]
            print(f"    {n:4d}x {shown}")
        for k, n in found_textures[h].most_common(12):
            print(f"      samples {n:4d}x {k[:200]}")
    resolves = [line.split("[frame] ")[1] for line in frame if "[frame] resolve" in line]
    print(f"  resolves: {len(resolves)}")
    for r in resolves[:40]:
        print(f"    {r[:200]}")
    analyze_textures(frame, textures, sampled, top)


def analyze_textures(frame, textures, sampled, top):
    if not textures:
        print("  textures: none listed (the build predates texture info in the frame report)")
        return
    print(f"  textures: {len(textures)} distinct; by draws:")
    for (address, description), (count, shaders) in sorted(
            textures.items(), key=lambda kv: -kv[1][0])[:min(top, 25)]:
        shown = ", ".join(sorted(shaders)[:3]) + (" ..." if len(shaders) > 3 else "")
        print(f"    {count:5d}x {description} | ps {shown}")
    # Every resolve, and what samples the memory it wrote afterwards (until
    # the next resolve to the same place), with the format it is read as.
    print("  resolve -> sampled later in the frame as:")
    resolves = []
    for position, line in enumerate(frame):
        m = RESOLVE_RE.search(line)
        if m:
            resolves.append((position, m))
    not_sampled = []
    for i, (position, m) in enumerate(resolves):
        index, dest, fmt, pitch_height, source, sample_select, samples = m.groups()
        dest = int(dest, 16)
        pitch_height = int(pitch_height, 16)
        pitch, height = pitch_height & 0x3FFF, (pitch_height >> 16) & 0x3FFF
        size = max(pitch, 1) * max(height, 1) * BYTES_PER_PIXEL.get(format_name(fmt), 4)
        end = next((p for p, n in resolves[i + 1:] if int(n.group(2), 16) == dest), len(frame))
        readers = collections.Counter()
        for draw_position, ps, draw_textures in sampled:
            if not position < draw_position < end:
                continue
            for (address, description), fetch in draw_textures:
                if dest <= address < dest + size:
                    readers[f"t{fetch} {description} | ps {ps}"] += 1
        if not readers:
            not_sampled.append(index)
            continue
        what = "depth" if source == "4" else f"color {source}"
        print(f"    resolve {index}: {what} {samples}x -> 0x{dest:08X} {format_name(fmt)} "
              f"{pitch}x{height}: {sum(readers.values())} draws")
        for reader, n in readers.most_common(2):
            print(f"        {n:4d}x {reader[:190]}")
    if not_sampled:
        print(f"    not sampled later in this frame: resolves {', '.join(not_sampled)}")


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--mvk", default=os.path.join(repo, "out/capture/mvk"))
    parser.add_argument("--rex", default=os.path.join(repo, "out/capture/rex"))
    parser.add_argument("--log", default=None, help="log file (default: the newest)")
    parser.add_argument("--top", type=int, default=40, help="draw groups shown")
    parser.add_argument("--show", type=int, default=5, help="shaders listed")
    parser.add_argument("--detail", action="store_true",
                        help="list non-invariant vertex shaders and the EDRAM lines of the "
                             "fragment shader without a raster order group")
    parser.add_argument("--find", action="append", default=[],
                        help="guest shader hash: list the frame's draws that use it")
    args = parser.parse_args()
    log = args.log or newest_log(os.path.join(repo, "out/build/mac-arm64-release/logs"))
    if os.path.isdir(args.mvk):
        analyze_shaders(args.mvk, args.rex, args.show, args.detail)
    else:
        print(f"shaders: {args.mvk} does not exist (was MVK_CONFIG_SHADER_DUMP_DIR set?)")
    if log:
        analyze_frame(log, args.top, args.find)
    else:
        print("frame: no log found")


if __name__ == "__main__":
    sys.exit(main())
