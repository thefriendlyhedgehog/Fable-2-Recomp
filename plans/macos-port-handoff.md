# macOS port: handoff and working notes

State as of 2026-10-12, branch `claude/elegant-ride-r7ako0` (fork
thefriendlyhedgehog/Fable-2-Recomp). Written so work can continue on the Mac
itself (a local Claude Code instance can build, run and look at the game
directly) without the history of the cloud sessions. Background and the
reasoning behind each SDK change: `docs/macos_port.md`. Tools for offline
shader checks: `tools/sdk_offline/README.md`.

## Resume here

The **debug sweep** (key 0, see below) ran once (capture 2026-10-12): all
10 steps saved. Average brightness per step (0-255) from the log: 00
baseline 80, 01-06 mip 0-5 forced 76 / 78 / 80 / 77 / 70 / 78, 07 whole FSI
quads 47, 08 without the shadow shader 78, 09 without the lighting passes 94.
Two things stand out: whole quads darkens the picture a lot (80 to 47;
running the FSI shader on helper lanes changes what is written, so a
helper-lane or quad-coverage difference is in play), and the brightness
alone does not show which mip level breaks.

The contact sheet was not made that time: the game's PNG writer wrote the
deflate block lengths big-endian (fixed, they are little-endian), which
zlib rejects. `tools/mac_sweep_sheet.py` now reads those older shots too,
so the sheet can be made from an existing sweep folder without rerunning:
`python3 tools/mac_sweep_sheet.py out/capture/sweep`.

Next step (or rerun the sweep):

1. Build (see "Building and running") and run `tools/mac_capture.sh
   --merge_controllers --vulkan_hoist_shared_memory_uploads`.
2. Load the save (it starts at the brazier in the snowy town), stand still
   with the hero and some snow piles in view, press **0**, wait ~30 s, quit.
3. Look at `out/capture/sweep/sheet.png` (opened automatically) and the
   per-step difference numbers at the end of `out/capture/summary.txt`.
   Tile order: 00 baseline, 01-06 mip level 0-5 forced, 07 whole FSI quads,
   08 without the shadow shader, 09 without the lighting passes.
4. The first forced mip level that looks broken (squares, garbage, wrong
   colors) is where to dig (see "Open problems" 1).

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

The user's settings: `fable_2.toml` sets `render_target_path_vulkan =
"fsi"`; `fable2_config.toml` has `[patches] game_msaa = 0` (forces the
game's main scene to 1x MSAA; the game's own mode is 4x) and
`merge_controllers` is passed on the command line (a Steam Controller shows up
twice through SDL; without it the stick does not reach player 1).

### Changing the SDK

The SDK tree is `thirdparty/rexglue-sdk-src` (v0.10.0 + the patch applied as
working-tree changes; gitignored). The patch is
`thirdparty/sdk_mac_vulkan_fixes.patch` (19 files, all under `src/` and
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

## Debug switches (all hot-reloadable SDK/app cvars)

Keys work only while the game window is focused; every press is logged as
`[input] ...` and listed at the top of the analyzer summary.

| Key | cvar | What it does |
|---|---|---|
| 6 / fn+F6 | `vulkan_debug_fsi_whole_quads` | FSI pixel shaders run for every 2x2 quad (no early quad skip, which relies on fine derivatives) |
| 7 / fn+F7 | `vulkan_debug_force_mip` | cycles the mip level all textures are sampled at: off, 0..6, off (`vulkan_debug_base_mip_only` = level 0) |
| 8 / fn+F8 | `vulkan_debug_skip_ps_group` | cycles the `;`-separated groups of `vulkan_debug_skip_ps` (guest pixel shader hashes), skipping one group's draws at a time |
| 9 / fn+F9 | `vulkan_frame_report` | logs every draw and resolve of the next frame (`[frame]` lines) |
| 0 / fn+F10 | (app) | debug sweep: `debug_sweep_steps`, `debug_sweep_dir`, `debug_sweep_settle_ms` |
| - | `vulkan_snap_vertex_positions` | snap vertex screen positions to 1/16 pixel (did not help, off) |
| - | `vulkan_debug_skip_depth_only_draws` | skip the depth prepass (turns the picture white: the prepass also writes the stencil the base pass tests) |
| - | `vulkan_fsi_end_interlock_in_function` | the raster order group fix (on by default on macOS) |
| - | `vulkan_invariant_position` | invariant vertex position (on) |
| - | `vulkan_hoist_shared_memory_uploads` | uploads at submission start instead of mid-pass (opt-in; the user always passes it) |
| - | `vulkan_long_frame_log` | per-area breakdown of frames over 100 ms |

## Open problems, with evidence

### 1. Smaller mip levels are wrong (strongest lead)

- Sampling only the base level (`vulkan_debug_base_mip_only`, or one press
  of key 7 = `vulkan_debug_force_mip` 0) made the snowflakes soft round dots
  instead of framed squares, made the hero look right (confirmed twice, the
  second time with key 7 once), and removed some of the white edge lines. So
  levels >= 1 hold wrong data or get selected wrong. Which level first goes
  wrong is not known yet (the sweep shows it).
- Snow: 23 draws per frame of ~4000 **point sprites** each, ps
  `718C12FF9E1B888B`, additive (src*1 + dst*1), sampling a **64x64 DXT1**
  texture at `0x14450000` with **packed mips 0-6** (the 360 stores levels
  of 16x16 and smaller packed together in one tile at the mip address).
  Without geometry shaders the SDK expands points in the vertex shader
  (`kPointListAsTriangleStrip`); that path was read and looks correct.
- Hero: with `[patches] realtime_texture_morphing` (default on) the hero's
  and dog's textures are rendered and **their mips built on the GPU by
  resolves** (`plans/hero-dog-realtime-texture-morphing.md`). In the frame:
  resolves 0-3 to `0x12704000` (256x256), `0x12724000` (128), `0x1272C000`
  (64), `0x1272E000` (32), format 1_5_5_5, sampled as a 256x256 texture with
  mips 0-3 by ps `06547412E4B72251`. The Windows notes already mention
  "small flickering light rectangles" on the dog
  (`plans/hero-dog-shifty-texture-mipmap.md`), so this may not be Mac-only.
- The Vulkan load path (`VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl`)
  is Xenia's: one compute dispatch per level into a scratch buffer, then
  one `vkCmdCopyBufferToImage` with a region per level; packed levels copy
  sub-rectangles out of the loaded tail (`GetPackedMipOffset`). Candidates:
  MoltenVK's copy of small block-compressed regions (2x2 and 1x1 levels of
  BC formats, offsets inside the tail); stale mip data for textures whose
  mips are written by resolves (texture invalidation of the mip range); the
  mip range requested from shared memory.
- Next: the sweep tells which level breaks. Then dump a texture's levels
  (e.g. read the host image back per level, or log the load's copy regions
  for the snowflake texture) and compare with the guest data.

### 2. White dashed lines along triangle edges

- They flicker and are visible on snow piles and walls. They are the white
  of the bare color buffer: skipping the depth prepass turns the whole
  picture that white. So they are pixels the base pass did not paint.
- 401 of 404 meshes draw the depth prepass with vs `F160B4DA459A6D40`
  (position only) and the base/lighting passes with other vertex shaders
  (e.g. `ECD66A10092E6562`). Depth test is greater-or-equal with write and
  stencil in all of them.
- Tried, no visible change: invariant position (on), raster order group fix
  (now 118/118 FSI shaders have it), snapping vertex positions to 1/16 pixel,
  turning fast math off (`MVK_CONFIG_FAST_MATH_ENABLED=0`; SPIRV-Cross keeps
  the translator's no-contraction arithmetic exact anyway).
- Some lines went away with the base-mip-only switch, which points at edge
  pixels selecting a wrong (broken) mip level: quads straddling an edge
  have helper lanes, and the FSI early quad skip uses fine derivatives
  (`DPdxFine`), which Metal may implement coarsely. Confirmed half of that:
  SPIRV-Cross (adec7ac, `spirv_msl.cpp` "Derivatives") emits plain `dfdx` /
  `dfdy` for the fine, coarse and default ops alike, so whether the skip is
  quad-uniform depends on what Apple's `dfdx` does. If it is coarse, a quad
  with one passing pixel in the bottom row runs that pixel alone and its
  texture derivatives (mip selection) are undefined. A derivative-free skip
  would use quad operations (the SDK's own TODO there: "Use
  GroupNonUniformQuad operations where supported"). The whole-quads switch
  (key 6, sweep tile 07) changed the picture a lot (brightness 80 to 47),
  which a pure skip removal should not do; look at tile 07 before
  concluding.
- Guest polygon mode is fill for every draw (frame report `polymode`), so
  not a wireframe pass.

### 3. Large black shadow areas

- Shadows are screen-space: ps `401A01FD4E5F8757` (29-32 draws, depth test
  greater, no write, blend src*1 + dst*(1-srcA) into the lighting buffer)
  samples both 1024x1024 shadow maps (resolves 12 and 13: depth to
  `0x12BE1000` / `0x12FE1000`) and the scene depth (resolve 17, 1120x720 to
  `0x1A02A000`), all as `24_8_FLOAT` (float24 depth, decoded to R32 float by
  the texture cache, same as Xenia).
- The shadow maps are drawn with polygon offset (ps 0 draws with `poly
  offset`), applied in the FSI shader the same way as Xenia's ROV path.
- Resolve 18 writes a one-channel 1120x720 mask that ~1600 base-pass and
  lighting draws then sample (`t8`, `0x12B17000`). If that mask is wrong
  (black where it should be lit), whole areas go black: check what draws
  into it before resolve 18 and what it contains (dump it).
- Skipping the screen-space shadow shader (sweep tile 08) shows what it
  contributes. If the black area goes away, compare its shadow-map reads
  (format, filter `filt 001`, integer number format, swizzle) and the depth
  resolve path.

### 4. Performance

- GPU at 100%; ~29 fps at the brazier with upload hoisting; heavy scenes
  were 130-400 ms per frame before the fixes. A Metal System Trace put
  fragment work at 84% of GPU time (FSI: every pixel shader does the EDRAM
  read-modify-write with interlock).
- Done: pass restarts from mid-frame uploads (`vulkan_hoist_shared_memory_uploads`),
  pipeline store loading, thread QoS, async pipeline waits.
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
| ~30 | `401A01FD4E5F8757` | screen-space shadows (also some opaque and constant-blend draws) |
| ~23 | `718C12FF9E1B888B` | snow point sprites |
| ~130 | `8D900846800943C8` at 4x | a 256x256 4x render target (resolve 15) |

Resolves: 0-11 hero/dog texture morph levels (256 down to 32, format
1_5_5_5); 12-13 shadow maps; 17 scene depth (1120x720 to `0x1A02A000`); 18 a
**one-channel** 1120x720 texture (format 8) to `0x12B17000`, sampled as `t8`
by ~1600 later base-pass and lighting draws (a screen-space mask, likely
shadows or ambient occlusion); 19-34 post-processing chain (bloom,
luminance at 280x180 / 560x360).

## Fixed on this branch (most recent first)

- Debug tooling: sweep (`620d151`), forced mip level (`b46bb90`), number-key
  aliases (`7a2c33a`), skip groups / whole quads / polymode (`214a560`),
  live switches (`4550081`), rotated-log analysis (`a57d5ce`), timed frame
  reports (`44fa4da`), capture tooling (`259c100`, `4cca847`, `b33ab03`).
- Raster order group for every FSI shader: SPIRV-Cross (MoltenVK's commit
  and upstream main) marks no resources when the interlock begins inside
  control flow of the entry point; the translator now ends the interlock in
  a helper function (`ca65995`). Reproduced and verified offline
  (`tools/sdk_offline`).
- Invariant vertex position (`31582b2`), upload hoisting (`ce9de4e`),
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
  shaders: snapping to the guest's 1/16 pixel grid did not change the lines.
- The point sprite expansion and its point coordinates (code read).
- Shadow map decode format (matches Xenia).
