#!/usr/bin/env python3
"""Summarize a macOS capture: the Metal shaders MoltenVK compiled, and one
frame's draws (vulkan_frame_report), short enough to paste.

Capture with tools/mac_capture.sh (see docs/macos_port.md, "Capturing a
frame"); it runs this afterwards. To analyze the last capture again:
    python3 tools/analyze_mac_capture.py --mvk out/capture/mvk --rex out/capture/rex
The log is read with its rotated parts (fable_2_NNN.1.log, ...), since the
SDK's log rotates every 5 MB by default.

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
    # Base logs only (fable_2_056.log), not their rotated parts (fable_2_056.1.log).
    logs = [p for p in glob.glob(os.path.join(log_dir, "fable_2_*.log"))
            if re.search(r"fable_2_\d+\.log$", p)]
    logs.sort(key=os.path.getmtime)
    return logs[-1] if logs else None


def log_parts(path):
    """The log and its rotated parts, oldest first. The SDK's log rotates at
    log_max_file_size_mb (5 MB): the full file is renamed name.1.log (the
    older ones move up to name.2.log, ...) and logging goes on in name.log."""
    base, ext = os.path.splitext(path)
    parts = []
    for part in glob.glob(glob.escape(base) + ".*" + ext):
        m = re.match(re.escape(base) + r"\.(\d+)" + re.escape(ext) + "$", part)
        if m:
            parts.append((int(m.group(1)), part))
    return [part for _, part in sorted(parts, reverse=True)] + [path]


def read_log(path):
    lines = []
    for part in log_parts(path):
        with open(part, errors="replace") as f:
            lines.extend(f.read().splitlines())
    return lines


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
    listed = without if (without or not detail) else fsi
    for name, rog, sample_mask, decls, guest, path in listed[:show]:
        print(f"  {'WITHOUT' if not rog else 'with'} raster order group: {name}"
              f"{'  <- ' + guest if guest else ''}")
        for d in decls[:3]:
            print(f"      {d[:160]}")
    if listed:
        print(f"  example FSI shader for a closer look: {listed[0][5]}")
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


def analyze_frame(log, top, find=(), report=-1):
    parts = log_parts(log)
    lines = read_log(log)
    print(f"log: {os.path.basename(log)}"
          f"{f' and {len(parts) - 1} rotated parts' if len(parts) > 1 else ''}, {len(lines)} lines")
    # Debug key presses that reached the game (fn+F6 / F7 / F8 / F9).
    keys = [line for line in lines if re.search(r"\[input\] F[5-9]", line)]
    if keys:
        print(f"  keys that reached the game: {len(keys)}")
        for line in keys[-12:]:
            stamp = re.match(r"\[\d{4}-\d\d-\d\d ([\d:.]+)\]", line)
            print(f"    {stamp.group(1) if stamp else ''} {line.split('[input] ', 1)[1][:150]}")
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
        chosen = complete[max(-len(complete), min(report, len(complete) - 1))]
        start, end = chosen
        stamp = re.match(r"\[(\d{4}-\d\d-\d\d [\d:.]+)\]", lines[start])
        print(f"frame: {lines[start].split('[frame] ')[1]} ... {lines[end].split('[frame] ')[1]}"
              f" (report {complete.index(chosen) + 1} of {len(complete)} complete ones"
              f"{', logged ' + stamp.group(1) if stamp else ''}; --report picks another)")
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
    draws = parse_draws(frame)
    wireframe = collections.Counter()
    for d in draws:
        mode = d["polymode"]
        if mode and mode[0] == 1 and d["prim"] not in (1, 2, 3, 12) and (
                (mode[1] < 2 and "F" not in d["cull"]) or (mode[2] < 2 and "B" not in d["cull"])):
            wireframe[f"ps {d['ps']} vs {d['vs']} polymode {mode[0]} {mode[1]}{mode[2]} "
                      f"cull {d['cull']}"] += 1
    if any(d["polymode"] for d in draws):
        print(f"  draws whose polygons the guest draws as lines or points: "
              f"{sum(wireframe.values())}")
        for k, n in wireframe.most_common(8):
            print(f"    {n:4d}x {k}")
    analyze_multipass(draws)
    analyze_particles(draws)
    analyze_textures(frame, textures, sampled, top)


PRIMITIVES = {1: "points", 2: "lines", 3: "line strip", 4: "triangles", 5: "fan", 6: "strip",
              7: "triangles w", 8: "rectangles", 12: "line loop", 13: "quads", 14: "quad strip",
              15: "polygon"}
BLEND_FACTORS = {0: "0", 1: "1", 4: "src", 5: "1-src", 6: "srcA", 7: "1-srcA", 8: "dst",
                 9: "1-dst", 10: "dstA", 11: "1-dstA", 12: "const", 13: "1-const",
                 14: "constA", 15: "1-constA", 16: "srcAsat"}
BLEND_OPS = {0: "+", 1: "-", 2: "min", 3: "max", 4: "rev-"}


def blend_name(value):
    """RB_BLENDCONTROL as 'src*F op dst*G' for color (alpha if different)."""
    value = int(value, 16)

    def part(shift):
        src = BLEND_FACTORS.get(value >> shift & 0x1F, "?")
        op = BLEND_OPS.get(value >> (shift + 5) & 0x7, "?")
        dst = BLEND_FACTORS.get(value >> (shift + 8) & 0x1F, "?")
        if (src, op, dst) == ("1", "+", "0"):
            return "opaque"
        return f"src*{src} {op} dst*{dst}"

    color, alpha = part(0), part(16)
    return color if color == alpha else f"{color}, alpha {alpha}"


def parse_draws(frame):
    draws = []
    for position, line in enumerate(frame):
        m = DRAW_RE.search(line)
        if not m:
            continue
        (vs, ps, prim, count, packet, samples, pipeline_samples, pitch, targets, dfmt, dbase,
         ztest, zfunc, zwrite, stencil, atest, afunc, a2c, cull, po_f, po_b, new_pass) = m.groups()
        blends = re.findall(r"rt(\d) fmt (\d+) base \d+ mask ([0-9A-F]) blend ([0-9A-F]{8})", targets)
        polymode = re.search(r"\| polymode (\d) (\d)(\d)", line)
        draws.append({
            "index": len(draws), "vs": vs, "ps": ps, "prim": int(prim), "count": int(count),
            "msaa": samples, "pitch": pitch, "dbase": dbase, "ztest": ztest == "1",
            "zfunc": zfunc, "zwrite": zwrite == "1", "atest": atest == "1", "afunc": afunc,
            "a2c": a2c == "1", "cull": cull or "none", "poly_offset": "1" in (po_f + po_b),
            "color": bool(blends), "blends": blends, "textures": textures_of(line),
            # Guest PA_SU_SC_MODE_CNTL: poly_mode 1 (dual) with a front or back
            # type below 2 (0 points, 1 lines) draws polygons as points or lines.
            "polymode": tuple(int(g) for g in polymode.groups()) if polymode else None})
    return draws


def analyze_multipass(draws):
    """Meshes drawn in a depth-only pass and again with color on the same
    surface (Fable II: a depth prepass, the base pass, then additive lighting
    passes with a greater-or-equal depth test). The later passes only cover
    the pixels where they compute exactly the depth the prepass stored, so
    anything that differs between the passes (vertex shader, MSAA, polygon
    offset, culling) can leave lines along triangle edges."""
    meshes = collections.defaultdict(list)
    for d in draws:
        if d["count"] >= 30 and d["ztest"]:
            meshes[(d["prim"], d["count"], d["pitch"], d["dbase"])].append(d)
    multipass = {k: v for k, v in meshes.items()
                 if any(not d["color"] for d in v) and any(d["color"] for d in v)}
    print(f"  meshes drawn depth-only and again with color on the same surface "
          f"(same primitive and index count): {len(multipass)}")
    if not multipass:
        return
    checks = collections.Counter()
    examples = collections.defaultdict(list)
    for key, passes in multipass.items():
        depth_vs = {d["vs"] for d in passes if not d["color"]}
        color_vs = {d["vs"] for d in passes if d["color"]}
        found = []
        if depth_vs != color_vs:
            found.append("depth-only and color passes use different vertex shaders")
        if len({d["msaa"] for d in passes}) > 1:
            found.append("MSAA differs between passes")
        if len({d["poly_offset"] for d in passes}) > 1:
            found.append("polygon offset in some passes only")
        if len({d["cull"] for d in passes}) > 1:
            found.append("culling differs between passes")
        if not found:
            found.append("all passes consistent")
        for f in found:
            checks[f] += 1
            examples[f].append(passes)
    for f, n in checks.most_common():
        print(f"    {n:4d} meshes: {f}")
    for f in checks:
        if f == "all passes consistent":
            continue
        for passes in examples[f][:2]:
            print(f"      e.g. {PRIMITIVES.get(passes[0]['prim'], passes[0]['prim'])} "
                  f"x{passes[0]['count']} pitch {passes[0]['pitch']}:")
            for d in passes[:6]:
                what = (", ".join(f"rt{rt} fmt {fmt} {blend_name(b)}" for rt, fmt, _, b in d["blends"])
                        or "depth only")
                print(f"        draw {d['index']}: vs {d['vs']} ps {d['ps']} msaa {d['msaa']}x "
                      f"f{d['zfunc']}{' write' if d['zwrite'] else ''}"
                      f"{' poly offset' if d['poly_offset'] else ''} cull {d['cull']} | {what}")


def analyze_particles(draws):
    """Blended draws that do not write depth (particles such as snow, fire,
    glows) and non-triangle primitives, by pixel shader, with what they
    sample."""
    groups = collections.OrderedDict()
    for d in draws:
        blended = [b for _, _, _, b in d["blends"] if blend_name(b) != "opaque"]
        if not ((blended and not d["zwrite"]) or d["prim"] not in (4, 5, 6)):
            continue
        key = (PRIMITIVES.get(d["prim"], str(d["prim"])), d["ps"],
               ", ".join(f"fmt {fmt} {blend_name(b)}" for _, fmt, _, b in d["blends"]) or "no color",
               (" alpha test f" + d["afunc"] if d["atest"] else "") + (" A2C" if d["a2c"] else ""))
        g = groups.setdefault(key, {"draws": 0, "count": 0, "textures": collections.Counter()})
        g["draws"] += 1
        g["count"] += d["count"]
        for (address, description), fetch in d["textures"]:
            g["textures"][f"t{fetch} {description}"] += 1
    print(f"  blended draws without depth write, and non-triangle primitives: {len(groups)} groups")
    for (prim, ps, target, alpha), g in sorted(groups.items(), key=lambda kv: -kv[1]["draws"])[:12]:
        print(f"    {g['draws']:4d}x {prim} ({g['count']} indices) ps {ps} | {target}{alpha}")
        for t, n in g["textures"].most_common(2):
            print(f"          samples {t[:180]}")


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
    parser.add_argument("--report", type=int, default=-1,
                        help="which complete frame report of the log (default -1, the last; "
                             "1 is the first, -2 the one before the last)")
    args = parser.parse_args()
    log = args.log or newest_log(os.path.join(repo, "out/build/mac-arm64-release/logs"))
    if os.path.isdir(args.mvk):
        analyze_shaders(args.mvk, args.rex, args.show, args.detail)
    else:
        print(f"shaders: {args.mvk} does not exist (was MVK_CONFIG_SHADER_DUMP_DIR set?)")
    if log:
        analyze_frame(log, args.top, args.find, args.report - 1 if args.report > 0 else args.report)
    else:
        print("frame: no log found")


if __name__ == "__main__":
    sys.exit(main())
