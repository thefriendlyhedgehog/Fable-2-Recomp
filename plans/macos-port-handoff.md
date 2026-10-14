# macOS port: handoff and working notes

State as of 2026-10-12, branch `claude/elegant-ride-r7ako0` (fork
thefriendlyhedgehog/Fable-2-Recomp). Written so work can continue on the Mac
itself (a local Claude Code instance can build, run and look at the game
directly) without the history of the cloud sessions. Background and the
reasoning behind each SDK change: `docs/macos_port.md`. Tools for offline
shader checks: `tools/sdk_offline/README.md`.

## Resume here

**Fixed (2026-10-12): wrong mip levels and the white edge lines.** Both came
from the FSI early quad skip. It decided with fine derivatives, which Metal
computes like coarse ones, so single pixels ran without the rest of their
quad and got garbage texture derivatives. The SDK now decides with subgroup
quad operations (`vulkan_fsi_quad_skip_quad_ops`, on by default; details in
`docs/macos_port.md`, "FSI early quad skip with quad operations"). Verified
with a sweep: snowflakes are round dots, the hero's face is right, no edge
lines, and the whole-quads tile now differs from baseline by only 1.9% of
pixels (before the fix, whole quads changed the picture a lot).

**Open: black / dark shadow areas that flicker.** What is known:

- The mask resolve at `0x12B17000` (one channel, 1120x720, sampled as `t8`
  by ~1100-1600 later draws) is a plausible sun-shadow mask: white lit,
  black shadowed. Snow in its shadow still gets ambient light, so the mask
  alone does not make areas black.
- Both shadow maps (t6 `0x12BE1000`, t7 `0x12FE1000`: a near and a wider
  cascade, 1024x1024 depth) look sane in the dumps.
- A dark area with stepped edges on the ground next to the glowing trail
  (its outline looks like the shadow of the roof on the right) **flickers**:
  the user confirms, and a flicker test caught it (16 baseline shots 250 ms
  apart while standing still, `--debug_sweep_steps="f00;f01;...;f15"
  --debug_sweep_settle_ms=250`; shots 12 and 15 have it, the others not: 6%
  of pixels vs. under 2% for snowflakes). So some input of the ground /
  lighting draws differs between frames.
- Skipping ps `401A01FD4E5F8757` (sweep tile 08) removed the dark area in
  one sweep but not in the next; with the flicker that test says nothing.
  That shader is a general material / lighting shader (base pass fmt 3,
  lighting fmt 12, also the hero), not only screen-space shadows.

**Cause found (2026-10-12): upload hoisting.** The flicker test with
`--vulkan_hoist_shared_memory_uploads` still flickers after the ordering
fixes below; the same test without it does not (all 16 shots within
1.3-1.8% of shot 00, snowflakes only, and the ground by the trail is soft
and lit in every shot). The resolves are not the cause: dumps of 24
consecutive frames (`dump 24`) change by 0.2% (mask), 1.4% / 0.6% (shadow
maps) and 0.2% (scene depth) per frame. So hoisting still reorders some
non-resolve data the ground draws read from shared memory (vertex or
heightmap data most likely), even with only first uses of pages hoisted.
Hoisting also brought no measurable speed: heavy scenes ran 12-14 fps with
and without it (log 070 with, 071 without; not identical scenes). **Do not
pass `--vulkan_hoist_shared_memory_uploads`**; the capture commands no
longer do. Making it safe is only worth it if a profile shows the pass
restarts it avoids matter; xenia-edge's zero-copy shared memory
(`VK_EXT_external_memory_host`) would remove most uploads instead.

Ordering holes found by a review against upstream Xenia and xenia-edge and
fixed anyway (see `docs/macos_port.md`, "Shared memory and texture load
ordering"):
1. Upload hoisting could reorder uploads of the same page and overtake
   earlier reads and GPU writes. Now only first uses of pages in a
   submission are hoisted; not enough, see above.
2. Stale textures: a texture could be marked up to date with data the CPU
   had already changed again (xenia-edge 9781a75a), or two loads of one
   image in a submission could land in either order. Both fixed.
3. Resolve readback is unlikely to matter: memexport readback is off on
   macOS and the only forced resolve readback (`0x12704000`) needs
   `[patches] hero_dog_texture_readback`, which is off. (Background:
   `docs/macos_port.md`, "Readback defaults on macOS".)

Resolve numbers differ between frames (the mask was resolve 15, 18, 23 or
24 in different reports); the addresses stay the same, so refer to
resolves by address.

## Building and running (on the Mac)

Repo: `/Volumes/MacMiniExternal/Games/fable2/Fable-2-Recomp`. Game content
(default.xex, data/) lives in the repo root, so runs need
`--game_data_root="$PWD"`. The user usually starts the game with their own
`./run_and_watch.sh`; `tools/mac_capture.sh` passes that flag itself.

- SDK (GPU plugin and runtime) from source with the Mac patch, then the game:
  `tools/build_sdk_mac.sh && ./build.sh -release fable_2`
  (`build_sdk_mac.sh` is incremental; it stages the plugin next to the game,
  and `build.sh` re-stages it after every game build.)
- Capture run: `tools/mac_capture.sh [game flags]` - runs the game with the
  MoltenVK and SDK shader dumps, a frame report every 20 s
  (`--vulkan_frame_report_interval=20`), 100 MB log files and the sweep
  directory, then writes `out/capture/summary.txt` (copied to the
  clipboard) via `tools/analyze_mac_capture.py`, plus the sweep sheet.
- Re-analyze the last capture without running the game:
  `python3 tools/analyze_mac_capture.py --mvk out/capture/mvk --rex out/capture/rex [--report N] [--find <hash>] [--detail]`
- Logs: `out/build/mac-arm64-release/logs/fable_2_NNN.log`. The SDK's log
  rotates at `log_max_file_size_mb` (default 5): older parts are
  `fable_2_NNN.1.log`, `.2`, ... The analyzer reads all parts.
- Metal System Trace summary: `tools/summarize_gputrace.py <trace>` (see its
  docstring for the `xctrace record` line).

The user's settings (`render_target_path_vulkan = "fsi"` is already the
app's default on macOS): `fable2_config.toml` has `[patches] game_msaa = 0` (forces the
game's main scene to 1x MSAA; the game's own mode is 4x) and
`merge_controllers` is passed on the command line (a Steam Controller shows up
twice through SDL; without it the stick does not reach player 1).

### Changing the SDK

The SDK tree is `thirdparty/rexglue-sdk-src` (v0.10.0 + the patch applied as
working-tree changes; gitignored). The patch is
`thirdparty/sdk_mac_vulkan_fixes.patch` (26 files, all under `src/` and
`include/`).

1. Edit files under `thirdparty/rexglue-sdk-src/src` or `/include`.
2. `tools/sdk_patch_refresh.sh` - regenerates the patch from the tree (new
   files included) and checks it reverse-applies. Do this before pulling a
   newer patch or re-running `build_sdk_mac.sh`: when the tree does not
   match the patch, `build_sdk_mac.sh` resets it to the tag (it saves a
   `pre-reset-<time>.diff` backup of what it reset).
3. `tools/build_sdk_mac.sh && ./build.sh -release fable_2`.

The game compiles against the SDK's prebuilt headers, so do not change the
layout of SDK classes the app uses (append members only, or add
namespace-scope things); classes only the SDK uses (the Vulkan command
processor, texture cache, translator) are free to change.

### Talking to the user

They run things on the Mac and paste results. They prefer: every command in
its own code block, starting with `cd /Volumes/MacMiniExternal/Games/fable2/Fable-2-Recomp && ...`,
commands that belong together joined with `&&`, output commands ending in
`| tee /dev/tty | pbcopy` (prints and copies), no `#` comments inside commands
(zsh), no `VAR="--flag $PWD"` indirections (word splitting broke them), and
short outputs. Always show the exact command before asking them to run it.
On their keyboard F-keys need fn; the debug keys are therefore also on the
number keys.

## Driving the game without the keyboard

`tools/mac_capture.sh` passes `--debug_command_file=out/capture/command`.
The input poll reads and deletes that file every 250 ms (no window focus
needed), one command per line: `report` (= 9), `sweep` (= 0), `quit`,
`press <button> [ms]`, `wait <ms>`, `dump <frames>`, `set <cvar> <value>`.
Write it atomically, e.g.
`printf 'sweep\n' > out/capture/command.tmp && mv out/capture/command.tmp out/capture/command`.
(The keys 9 / 0 only work while the game window has focus.)

**Unattended runs: `tools/mac_autotest.sh "<commands>" [game flags]`.** It
starts the game, waits for the title screen (about 280 draws per frame in
the FSI statistics; the logos draw about 25 and ignore input), loads the
save with A, Down, A, waits for gameplay (over 1000 draws per frame), sends
the `;`-separated commands (`sleep <s>` pauses between batches), waits for
the sweep and a frame report, and quits. No user step, and the GPU is only
busy for the test. Use it instead of asking the user to load the save. See
its header for a sweep example and a frame rate A/B example (`set` a cvar,
`sleep 45`, so each setting gets a full 300-frame statistics window). In
gameplay it caps the frame rate to 5 fps (`vulkan_min_frame_time_ms` =
`AUTOTEST_FRAME_MS`, default 200) so test runs do not load the GPU fully;
pass `AUTOTEST_FRAME_MS=0` for frame rate measurements. (macOS still
reports ~89% "Device Utilization" at 5 fps, presumably at a lower clock;
GPU power needs `sudo powermetrics`.)

## Debug switches (SDK/app cvars; hot-reloadable unless marked restart)

Keys work only while the game window is focused; every press is logged as
`[input] ...` and listed at the top of the analyzer summary.

| Key | cvar | What it does |
|---|---|---|
| 6 / fn+F6 | `vulkan_debug_fsi_whole_quads` | FSI pixel shaders run for every 2x2 quad (no early quad skip) |
| 7 / fn+F7 | `vulkan_debug_force_mip` | cycles the mip level all textures are sampled at: off, 0..6, off (`vulkan_debug_base_mip_only` = level 0); the mip data proved fine |
| 8 / fn+F8 | `vulkan_debug_skip_ps_group` | cycles the `;`-separated groups of `vulkan_debug_skip_ps` (guest pixel shader hashes), skipping one group's draws at a time |
| 9 / fn+F9 | `vulkan_frame_report` | logs every draw and resolve of the next frame (`[frame]` lines) |
| 0 / fn+F10 | (app) | debug sweep: `debug_sweep_steps`, `debug_sweep_dir`, `debug_sweep_settle_ms` |
| - | `vulkan_fsi_quad_skip_quad_ops` | quad-uniform early quad skip with subgroup quad ops (on; needs a restart; off = the old derivative check) |
| - | `vulkan_debug_skip_depth_only_draws` | skip the depth prepass (turns the picture white: the prepass also writes the stencil the base pass tests) |
| - | `vulkan_fsi_end_interlock_in_function` | the raster order group fix (on by default on macOS; restart) |
| - | `vulkan_invariant_position` | invariant vertex position (on; restart) |
| - | `vulkan_hoist_shared_memory_uploads` | uploads at submission start instead of mid-pass (opt-in; **causes the flickering dark ground areas, do not pass it**) |
| - | `vulkan_debug_dump_resolves`, `vulkan_debug_dump_dir` | at every frame report, copy the resolves to the listed addresses out of the GPU's shared memory (in the command stream, no queue drain, guest memory untouched) and write them untiled once the frame completes (the capture script sets both) |
| - | `vulkan_long_frame_log` | per-area breakdown of frames over 100 ms |

## Open problems, with evidence

### (fixed) Wrong mip levels and white edge lines

Both came from the FSI early quad skip (see "Resume here"). The sweep showed
the mip data is fine (forced mips 1-5 look sane, just blurrier) and that
running whole quads removed both symptoms. Kept for reference: snowflakes
are point sprites (ps `718C12FF9E1B888B`, 64x64 DXT1, packed mips 0-6), the
hero's textures are built by resolves (`plans/hero-dog-realtime-texture-morphing.md`).

### 1. Black / dark shadow areas (flicker)

See "Resume here" for the evidence and candidates. Reference data:
ps `401A01FD4E5F8757` samples both shadow maps (t6, t7) and the scene depth
(t13, `0x1A02A000`), all as `24_8_FLOAT` (float24 depth, decoded to R32
float by the texture cache, same as Xenia). The shadow maps are drawn with
polygon offset (ps 0 draws with `poly offset`), applied in the FSI shader
the same way as Xenia's ROV path.

### 2. Performance

- GPU at 100%; ~28 fps in light scenes, 12-14 fps in heavy ones (with or
  without upload hoisting); heavy scenes were 130-400 ms per frame before
  the fixes. A Metal System Trace put fragment work at 84% of GPU time,
  but the `fbo` path is no faster than `fsi` at the trail (14 fps both),
  and no pixel shader takes over 7% of shader time.
- **The GPU idles 41% of the time** (Metal System Trace at the trail,
  2026-10-12, `out/profile`, summarized with `tools/summarize_gputrace.py`
  plus per-encoder analysis): ~1000 encoder gaps per frame, from ~350 render
  encoders and ~430 compute encoders per frame. 258 of them per frame were
  MoltenVK's tessellation emulation (a compute pass for the vertex and
  tessellation control stages of every tessellated draw, splitting the
  render pass): Fable II draws the ground as quad patches (prim 18), 214
  single-patch continuous draws at factor 16 (lighting passes) and 44
  adaptive ones (depth pass and base color).
- **Done: quad patches as triangle lists** (`vulkan_quad_patches_in_vs`, on
  on macOS; `docs/macos_port.md`, "Quad patches without host
  tessellation"): the 214 continuous draws no longer tessellate. Same run,
  same spot: 10.8 fps tessellated, 13.1-13.7 fps with it. The 44 adaptive
  draws still tessellate.
- Metal System Trace with the quad patch path (`out/profile/grid.trace`,
  10 s): tessellation compute passes 17522 -> 5236 (the 44 adaptive draws
  per frame), but **~32000 `vkCmdCopyBuffer` blit encoders** (~250 per
  frame): shared memory uploads, each ending the render pass (the 216
  restarts "after a barrier, copy or upload" per frame). Fragment work is
  ~70% of the time.
- **Zero-copy shared memory, ported from xenia-edge, opt-in**
  (`--vulkan_shared_memory_zero_copy=true`, restart): guest physical memory
  (the 512 MB view at `TranslatePhysical(0)`, 16 KB aligned) is imported
  with `VK_EXT_external_memory_host` as the shared memory buffer; uploads
  only mark pages valid. At the trail: **17.4 fps** (from ~13.4), render
  pass (re)starts 253 -> 76 per frame, picture correct
  (`fable_2_080.log`). But the flicker test (16 shots, capped, log
  `fable_2_082.log`) had **2 corrupted frames** (shots 02 and 12: blue hair
  and lantern, stray light shafts; 13-16% of pixels): presumably the GPU
  now reads memory the CPU rewrites after the draw was recorded (the copy
  used to snapshot it). Next: run the same flicker test without zero-copy
  to confirm the corruption is zero-copy's; then find which data races
  (e.g. dynamic vertex or constant buffers the game rewrites per frame),
  and either snapshot those ranges or delay CPU writes until the GPU is
  done with them.
- Done: pipeline store loading, thread QoS, async pipeline waits. Tried and
  dropped: upload hoisting (`vulkan_hoist_shared_memory_uploads`; no
  measurable gain, causes flicker).
- Ideas not done: profile which passes cost most (Instruments with the
  frame report's pass list), the host render target path (`fbo`) for
  comparison, fewer FSI restarts, `game_msaa` effects on render targets that
  stay 4x.

## Frame anatomy at the brazier (frame report, 1x main scene)

About 4000 draws and 34-39 resolves per frame.

| Draws | Pixel shader | Role |
|---|---|---|
| ~1200 | none (SDK depth-only FSI shader) | depth prepass, greater-or-equal, write, stencil |
| ~900 + ~190 | `8D900846800943C8`, `648B965C200A5113`, ... | base pass, fmt 3 (2_10_10_10_FLOAT), opaque |
| ~150 + ~100 | none, polygon offset | shadow maps (surface pitch 1040, depth to 1024x1024) |
| ~100 each | `9C2144E42E95A76F`, `4755A2E4A05BA678`, `578342593A37DD35`, `04F23292B2F44077`, `AF1E16EE57E4BB06`, `45243B758F9272AB` | lighting on the tessellated ground (prim 18 = quad patches, heightmap via vertex fetch), fmt 12, src*srcA + dst*1 |
| ~30 | `401A01FD4E5F8757` | material / lighting shader sampling both shadow maps (base pass, lighting, the hero) |
| ~23 | `718C12FF9E1B888B` | snow point sprites |
| ~130 | `8D900846800943C8` at 4x | a 256x256 4x render target (resolve 15) |

Resolves: 0-11 hero/dog texture morph levels (256 down to 32, format
1_5_5_5); 12-13 shadow maps; 17 scene depth (1120x720 to `0x1A02A000`); 18 a
**one-channel** 1120x720 texture (format 8) to `0x12B17000`, sampled as `t8`
by ~1100-1600 later base-pass and lighting draws (a sun-shadow mask, see
the dumps); resolve numbers shift between frames; 19-34 post-processing chain (bloom,
luminance at 280x180 / 560x360).

## Fixed on this branch (most recent first)

- Ordering fixes from a review against upstream Xenia and xenia-edge:
  hoisted uploads only for first uses of pages in a submission, texture
  validity checked before watching, same-image load barrier; pipelines
  created right away when draws wait for them; resolve dumps no longer
  drain the queue or write guest memory; vertex snapping removed.
- FSI early quad skip with subgroup quad operations instead of fine
  derivatives (`vulkan_fsi_quad_skip_quad_ops`, `016e929`): fixes the wrong
  mip levels (framed snowflakes, the hero's face) and the white edge lines.
- Debug tooling: resolve dumps (`06d2031`), debug command file (`cc0b118`).
- Debug tooling: sweep (`620d151`), forced mip level (`b46bb90`), number-key
  aliases (`7a2c33a`), skip groups / whole quads / polymode (`214a560`),
  live switches (`4550081`), rotated-log analysis (`a57d5ce`), timed frame
  reports (`44fa4da`), capture tooling (`259c100`, `4cca847`, `b33ab03`).
- Raster order group for every FSI shader: SPIRV-Cross (MoltenVK's commit
  and upstream main) marks no resources when the interlock begins inside
  control flow of the entry point; the translator now ends the interlock in
  a helper function (`ca65995`). Reproduced and verified offline
  (`tools/sdk_offline`).
- Invariant vertex position (`31582b2`), upload hoisting (`ce9de4e`; opt-in, not used since it causes flicker),
  `merge_controllers` (`9962d5c`), render pass end and FSI statistics
  (`2b93bfe`, `b537784`), himdo/rexglue-sdk fixes and async pipeline waits
  (`1882857`), exclusive long-frame timing, qualified forced readbacks and
  vblank pacing (`a66f50d`), input driver lifetime, manifest staging and
  `game_msaa` range (`f4029fd`).

## Not the cause (checked)

- Race from a missing raster order group: fixed, lines unchanged.
- Wireframe: no draw uses a line or point polygon mode;
  `vulkan_tessellation_wireframe` is off.
- Fast math rounding: SPIRV-Cross emits no-contraction ops as `optnone`
  helpers (`fma(a, b, 0)`), and turning fast math off did not remove the lines.
- Sub-pixel position differences between the prepass and color vertex
  shaders: snapping to the guest's 1/16 pixel grid did not change the lines
  (the switch has since been removed).
- The point sprite expansion and its point coordinates (code read).
- Shadow map decode format (matches Xenia).
