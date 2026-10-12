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
by pixel shader and render state with counts, and every resolve.
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
        print(f"frame: no '[frame] begin' in {log} (press fn+F9 in game)")
        return
    start = begins[-1]
    end = next((i for i in range(start, len(lines)) if "[frame] end" in lines[i]), len(lines))
    frame = lines[start:end + 1]
    print(f"frame: {lines[start].split('[frame] ')[1]} ... "
          f"{lines[end].split('[frame] ')[1] if end < len(lines) else '(no end)'}")
    groups = collections.Counter()
    msaa = collections.Counter()
    primitives = 0
    found = collections.Counter()
    for line in frame:
        m = DRAW_RE.search(line)
        if not m:
            continue
        for h in find:
            if h.upper() in (m.group(1).upper(), m.group(2).upper()):
                found[re.sub(r"^draw \d+: ", "", line.split("[frame] ", 1)[1])] += 1
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
    resolves = [line.split("[frame] ")[1] for line in frame if "[frame] resolve" in line]
    print(f"  resolves: {len(resolves)}")
    for r in resolves[:40]:
        print(f"    {r[:200]}")


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
