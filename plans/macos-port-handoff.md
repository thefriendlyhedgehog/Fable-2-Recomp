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

**Black shadow areas: not narrowed down yet (2026-10-12).** The mask
resolve at `0x12B17000` is a plausible sun-shadow mask (white lit, black
shadowed); snow in its shadow still gets ambient light. Both shadow maps (t6
`0x12BE1000`, t7 `0x12FE1000`, a near and a wider cascade) look sane in the
dumps. A dark band with stepped edges on the ground next to the glowing trail
went away without ps `401A01FD4E5F8757` (a general material / lighting
shader, also the hero) in one sweep, but was there without it in the next,
so that is not established; its outline looks like the shadow of the roof on
the right. **It flickers** (the user confirms; flicker test 2026-10-12: 16 baseline
shots 250 ms apart with `--debug_sweep_steps="f00;f01;...;f15"
--debug_sweep_settle_ms=250`, standing still; shots 12 and 15 have the dark
area, the others not, 6% of pixels vs. under 2% for snowflakes). So some
input of the ground / lighting draws changes between frames. Candidates:
upload hoisting (`vulkan_hoist_shared_memory_uploads`, always on in these
runs), a resolve sampled before it is complete or a stale copy (mask
`0x12B17000`, shadow maps), the readback of a resolve.

**Older note: problem 1 below (black shadow areas).** With correct derivatives,
shadowed areas are now plainly pitch black (a whole building side in the
last sweep). On the 360 they are soft and lit by ambient light.

In the last sweep, tile 08 (without the screen-space shadow shader
`401A01FD4E5F8757`) still had the black building, so the black likely comes
from elsewhere: the one-channel mask (resolve to `0x12B17000`) that ~1100
base-pass and lighting draws sample, or the base pass's own lighting.

1. Run the capture, stand where a wall or building is in shadow, wait for a
   frame report (every 20 s, or press 9), press **0**, quit.
2. `tools/mac_capture.sh` dumps the mask, the scene depth (`0x1A02A000`) and
   the first shadow map (`0x12BE1000`) at every frame report
   (`vulkan_debug_dump_resolves`), to `out/capture/dumps/*.png`, with value
   statistics at the end of `out/capture/summary.txt`
   (`tools/mac_resolve_dumps.py`). Compare the mask with the screenshot: if
   it is black (0) where the picture is black, find what draws it (the draws
   before resolve 24 in the frame report); if not, the base pass is next.

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

## Driving the game without the keyboard

`tools/mac_capture.sh` passes `--debug_command_file=out/capture/command`.
The input poll reads and deletes that file every 250 ms (no window focus
needed), one command per line: `report` (= 9), `sweep` (= 0), `quit`,
`press <button> [ms]`, `wait <ms>`. Write it atomically, e.g.
`printf 'sweep\n' > out/capture/command.tmp && mv out/capture/command.tmp out/capture/command`.
Done this way, the user only loads the save; Claude sends `report`, then
`sweep`, waits for `[sweep] done` in the log, then `quit`. (The keys 9 / 0
only work while the game window has focus; twice they did not register.)

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
| - | `vulkan_fsi_quad_skip_quad_ops` | quad-uniform early quad skip with subgroup quad ops (on; needs a restart; off = the old derivative check) |
| - | `vulkan_snap_vertex_positions` | snap vertex screen positions to 1/16 pixel (did not help, off) |
| - | `vulkan_debug_skip_depth_only_draws` | skip the depth prepass (turns the picture white: the prepass also writes the stencil the base pass tests) |
| - | `vulkan_fsi_end_interlock_in_function` | the raster order group fix (on by default on macOS) |
| - | `vulkan_invariant_position` | invariant vertex position (on) |
| - | `vulkan_hoist_shared_memory_uploads` | uploads at submission start instead of mid-pass (opt-in; the user always passes it) |
| - | `vulkan_debug_dump_resolves`, `vulkan_debug_dump_dir` | at every frame report, read back the resolves to the listed addresses and write them untiled (the capture script sets both) |
| - | `vulkan_long_frame_log` | per-area breakdown of frames over 100 ms |

## Open problems, with evidence

### (fixed) Wrong mip levels and white edge lines

Both came from the FSI early quad skip (see "Resume here"). The sweep showed
the mip data is fine (forced mips 1-5 look sane, just blurrier) and that
running whole quads removed both symptoms. Kept for reference: snowflakes
are point sprites (ps `718C12FF9E1B888B`, 64x64 DXT1, packed mips 0-6), the
hero's textures are built by resolves (`plans/hero-dog-realtime-texture-morphing.md`).

### 1. Large black shadow areas

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

### 2. Performance

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

- FSI early quad skip with subgroup quad operations instead of fine
  derivatives (`vulkan_fsi_quad_skip_quad_ops`): fixes the wrong mip levels
  (framed snowflakes, the hero's face) and the white edge lines.
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
