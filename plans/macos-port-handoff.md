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

**Fixed (2026-10-14):** vertex buffers that skip their shared memory
request (same address and size as before, an upstream shortcut that lasts
across frames) did not mark their pages used in the submission, so an
upload of those pages could be hoisted ahead of an earlier draw that read
the old data. `VulkanSharedMemory::NoteRangeUsed` now marks them. Two
flicker tests with hoisting (`fable_2_102`, `fable_2_105`): all 30 shots
within 1.0-2.1% of shot 00 (snow), picture correct. And hoisting now pays:
**+38% fps** (9.09 vs. 6.57, 81 vs. 258 render pass restarts per frame;
`fable_2_104.log`, eight alternating 30 s phases with 60-frame statistics,
`tools/phase_fps.py`; absolute numbers low because another process held
~49% of the GPU). The earlier "no measurable speed" was before the quad
patch fix, when tessellation passes dominated. **On by default on macOS**
since then (`fable_2_106.log`: 80 restarts per frame without the flag).

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
  docstring for the `xctrace record` line); busy/idle time, gaps and GPU
  time by encoder kind for the game alone: `tools/gputrace_timeline.py`.

The user's settings (`render_target_path_vulkan = "fbo"`, the app's
default on macOS since 2026-10-15; it was `fsi`): `fable2_config.toml` has `[patches] game_msaa = 0` (forces the
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
`press <button>[+<button>...] [ms]` (buttons, or stick directions LU/LD/LL/LR
and RU/RD/RL/RR for the left stick and the camera), `wait <ms>`,
`dump <frames>`, `set <cvar> <value>`.
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
| - | `vulkan_debug_depth_only_tiny_scissor`, `vulkan_debug_depth_only_first_triangle` | draw the same prepass draws with a 1x1 scissor (vertex work and draw overhead stay), and with the second also only their first triangle (only the per-draw and render pass costs stay); the picture is white as with the skip |
| - | `vulkan_fsi_end_interlock_in_function` | the raster order group fix (on by default on macOS; restart) |
| - | `vulkan_invariant_position` | invariant vertex position (on; restart) |
| - | `vulkan_hoist_shared_memory_uploads` | uploads at submission start instead of mid-pass (on by default on macOS; flicker fixed 2026-10-14, +38% fps; earlier: **caused the flickering dark ground areas, do not pass it**) |
| - | `vulkan_debug_dump_resolves`, `vulkan_debug_dump_dir` | at every frame report, copy the resolves to the listed addresses out of the GPU's shared memory (in the command stream, no queue drain, guest memory untouched) and write them untiled once the frame completes (the capture script sets both) |
| - | `direct_host_resolve` | fbo: the fast 32bpp resolve of a 1x color render target reads it directly instead of dumping it to the EDRAM buffer first (on) |
| - | `vulkan_direct_resolve_depth` | `direct_host_resolve` also for 1x depth render targets (on) |
| - | `vulkan_pass_log` | every 5 s, `[passes]` (one frame's render passes per submission), `[pass areas]` and `[pass commands]` (P pass, B barrier, D dispatch, C/I copies, X clear, in order) lines, and Metal render encoders labeled `<submission>.<pass>` for `tools/mac_pass_costs.py` |
| - | `draw_extent_log` | every 5 s, `[extent]` lines: draws whose estimated bottom is 1024 or more, by vertex shader, scissor and what limited the estimate (viewport, vertices, or why the CPU estimate gave up) |
| - | `vulkan_tfetch_implicit_lod` | pixel shader fetches with a computed LOD sample with an implicit LOD and a bias instead of explicit gradients (on on macOS; restart) |
| - | `vulkan_tfetch_specialize_signs` | host render targets: pixel shaders built for the signedness of their first 7 textures when it is all unsigned, all signed or gamma RGB + unsigned alpha, so fetches sample one view with constant signs (on on macOS) |
| - | `vulkan_gpu_time` | timestamps around each submission; logs "GPU time: N ms/frame, M% of the time" after every frame statistics line (on; restart) |
| - | `vulkan_pass_gpu_time` | with `vulkan_gpu_time`: times the render passes and resolves of one frame in ten and logs "GPU passes" lines (by kind, the 12 costliest by target and pixel shader); off |
| - | `vulkan_transfer_log` | every 5 s, one frame's `[transfers]` lines: each render target ownership transfer (destination, source, EDRAM tiles, and the draw that caused it: shaders, primitive, depth/stencil and color state), plus resolves and the frame's draw count |
| - | `clear_quad_transfer_cutout` | a draw that overwrites a rectangle of its render target outright (clear quad: rectangle list, depth ALWAYS with write and stencil REPLACE, or color mask F without blending) cuts that rectangle out of the transfers it causes (on on macOS) |
| - | `clear_quad_in_place` | with the cutout: a clear quad with one value whose tiles a same-format render target of another MSAA mode owns clears that target instead, and the draw is skipped (on on macOS); `vulkan_transfer_log` shows `in-place clear` lines |
| - | `vulkan_transfers_in_draw_pass` | host render targets: a draw's ownership transfers run in the draw's own render pass, which the draw continues, instead of a pass per destination (on on macOS); `vulkan_transfer_log` shows `not in the draw's render pass` lines with the reason |
| - | `vulkan_skip_draw_barriers_after_writes` | with `vulkan_skip_disjoint_draw_barriers`: after resolves and in-stream uploads, a shared memory barrier only before a draw whose known vertex and index ranges overlap what they wrote (on on macOS) |
| - | `vulkan_debug_skip_transfers`, `debug_skip_resolved_texture_loads` | skip all ownership transfers / the reloads of textures from resolved memory, to measure what they cost (wrong picture); `debug_skip_resolved_texture_loads_format=<n>` limits the latter to one guest format |
| - | `vulkan_resolve_into_texture` | a resolve also writes the texture at its destination, which then isn't reloaded (on on macOS; `vulkan_resolve_log` says why not, per destination) |
| - | `vulkan_reuse_texture_descriptor_sets` | bind a texture/sampler descriptor set written earlier in the frame with the same bindings instead of writing a new one per draw (on; counts on the `Command processor CPU` line) |
| - | `type0_register_ranges` | type-0 packets write consecutive registers through the range path (constants in bulk) (on) |
| - | `frame_timing_fast_clock` | `vulkan_long_frame_log` times its areas with `mach_absolute_time` instead of `steady_clock` (on on macOS) |
| - | `vulkan_resolve_into_texture_recreate` | a texture at a resolve destination created without a storage image is created again with one (on) |
| - | `debug_resolve_into_texture_exclude` | a guest address whose resolves don't write into the texture (it is reloaded as before), for comparisons (0 = none) |
| - | `vulkan_resolve_into_texture_full` | with the above: also the resolves from the EDRAM buffer with format conversion (8/16/32/64 bpp, mip levels), not only the direct 32 bpp ones (on) |
| - | `shared_memory_exact_gpu_write_watches` | a GPU write (resolve) invalidates only the textures it overlaps byte by byte, not all in the same 16 KB host pages (on on macOS) |
| - | `vulkan_fix_layer_drawable_size` | macOS: after creating a swapchain, set the CAMetalLayer's drawableSize back to the swapchain size when MoltenVK left it at 1x1 (on; off = black or grey window after some resizes) |
| - | `vsync_display_link`, `vsync_display_link_phase` | macOS: the guest vblank follows the window's display refresh (CVDisplayLink) when that rate is a whole multiple of `video_mode_refresh_rate` or the reverse, fired the phase (0.5) of a refresh after the callback; other rates keep the timer (on, restart) |
| - | `vulkan_present_pacing_log` | every N seconds, how many display refreshes each guest frame stayed on screen and how far apart presents are (VK_GOOGLE_display_timing; 0 = off) |
| - | `vulkan_tight_render_area` | begin each render pass with the union of its scissors instead of the whole EDRAM-tall framebuffer; bounds transfer and unclipped draw scissors (on) |
| - | `execute_unclipped_draw_vs_on_cpu` | estimate the rows of unclipped draws by running their vertex shader on the CPU (on since 2026-10-16; off made them claim the whole EDRAM) |
| - | `vulkan_resolve_log` | every 5 s, `[resolves]` lines: resolves by copy shader, size, destination format and the render targets they read (`direct` = no dump) |
| - | `vulkan_debug_skip_resolve_dumps`, `vulkan_debug_skip_resolve_copies` | skip the fbo dumps / the resolve copies, to measure what they cost (wrong picture) |
| - | `vulkan_long_frame_log` | per-area breakdown of frames over 100 ms |
| - | `vulkan_defer_gpu_progress`, `vulkan_defer_gpu_progress_submit` | make GPU progress visible to the guest only once the host GPU ran the commands before it, without waiting (correct with zero-copy, ~12 fps); the `[gpu-progress]` lines show delays and what the idle command processor waited behind |
| - | `vulkan_debug_skip_adaptive_patches` | skip adaptively tessellated patch draws (the ground goes missing), to measure what they cost |
| - | `vulkan_debug_end_render_pass_every` | end the render pass before every Nth draw, to measure what restarts cost (~0.12 ms each) |
| - | `vulkan_fsi_specialize` | FSI pixel shaders built for the draw's render target formats, 1x MSAA and no blending (restart; no fps change at the trail) |
| - | `vulkan_gpu_progress_wait` | submit and wait for the host GPU before every guest-visible GPU progress write (fixes zero-copy corruption, costs ~35% fps) |
| - | `vulkan_zero_copy_race_log` | every 5 s, `[race]` lines: guest pages the CPU rewrote while a pending submission read them, with what for and how many used bytes changed (restart; works with or without zero-copy) |

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
- **Trace of the current copy-mode build (2026-10-14,
  `out/profile/copy_1014.trace`, 10 s at the trail, uncapped;
  `tools/gputrace_timeline.py`):** the GPU is busy with the game 79% of
  the time, ~90% without one 1.4 s hitch, so idle gaps are no longer the
  big loss. GPU time: render encoders ~84% (69.8% opened by
  vkCmdBeginRenderPass, 14.3% MoltenVK render pass restarts), uploads
  ~12% (the "Blit Command #" intervals on the compute channel are the
  copies themselves, 10%; vkCmdCopyBuffer encoders 1.8%,
  vkCmdCopyBufferToImage 0.9%), compute dispatches 2%. Per Metal frame
  (about two per game frame): ~200 render passes, ~30 restarts, ~100
  shared memory upload encoders. **Other apps used the GPU during the
  runs:** RuneLite 24% of all GPU time in the trace, the Claude app 5%;
  close them for fps comparisons (today's copy mode measured 12.8 fps vs.
  13.4 before).
- **What a render pass restart costs (2026-10-14,
  `vulkan_debug_end_render_pass_every`, live; `fable_2_101.log`, one
  uncapped run alternating off / every 12th draw / off / every 23rd / off):**
  ~0.12-0.13 ms of GPU time each (off 9.4-9.7 fps, ~104 ms per frame;
  +~190 forced restarts per frame 7.8 fps, +24 ms; +~100 per frame 8.6 fps,
  +12 ms). The baseline was low because another process held ~32% of the
  GPU (GamePolicyAgent, with RuneLite running), but the two settings agree,
  and the figure predicts the zero-copy gain (190 fewer restarts, ~24 ms:
  12.8 -> ~18.5 fps; measured 17.4). So the ~267 restarts per frame cost
  ~33 ms, and the ~200 caused by uploads are the biggest single lever. In
  that run the forced ends were counted after the statistics; they now
  show in the restart count.
  **Lead for upload hoisting's flicker:** a hoisted upload may move to the
  start of the submission only if nothing earlier in it used the page
  (`ArePagesUsedInSubmission`), but vertex buffers that skip RequestRange
  (same address and size as before, the upstream shortcut that lasts
  across frames) never mark their pages used (`NoteRangeUsed` only feeds
  the race log). An upload of such a page can then overtake an earlier
  draw that read the old data. Marking those pages too, then rerunning the
  flicker test with `vulkan_hoist_shared_memory_uploads`, is the next step.
  (Done: that was it, see "Fixed (2026-10-14)" under problem 1.)
- **With upload hoisting, where frames go (2026-10-14, `fable_2_107.log`,
  GPU free of other load, uncapped, 60-frame windows):** the trail runs at
  **16.9 fps** (59 ms per frame). Remaining render pass ends per frame: 52
  barriers, 33 resolves/transfers, 20 texture loads, 2 uploads, 1
  submission (~0.12 ms each). Next levers, largest first:
  1. **The 44 adaptive quad patch draws** (depth prepass, base color) still
     use MoltenVK tessellation: skipping them (`vulkan_debug_skip_adaptive_patches`,
     alternated 4x) gives 20.4 fps, so they cost **~10 ms per frame
     (17%)**, most of it the tessellation compute passes and the render
     pass splits around them. **Done:** drawn with the vertex shader grid
     at the maximum factor (`vulkan_adaptive_quad_patches_in_vs`, on with
     the quad patch path): **19.4 fps vs. 16.8** (`fable_2_109.log`), same
     picture (the host path already used the maximum factor for them: its
     adaptive vertex shader reads the float factors as integers;
     `docs/macos_port.md`).
  2. **Barrier ends by source** (`Barrier render pass ends per frame by
     pushing code`, symbolize with `xcrun atos -o
     out/build/mac-arm64-release/librexgpu-xenos.dylib -arch arm64 -l 0x0
     <offset>`): `VulkanSharedMemory::Use` 28 per frame (~3.4 ms; the
     draw's switch between kRead and kGuestDrawReadWrite: after a draw with
     memory export, every next draw gets a barrier whether or not it reads
     the exported range, which an overlap check could skip);
     `VulkanRenderTargetCache::RequestPixelShaderInterlockBarrier` 24 per
     frame (~2.9 ms; render target layouts changing over EDRAM written
     before, inherent to FSI); `VulkanTextureCache::RequestTextures` 1.
     **Done (2026-10-15): `vulkan_skip_disjoint_draw_barriers`** (default
     on, live). By transition (`Shared memory usage barriers per frame`):
     ~26 memory export draws per frame, 23 of them right after another
     one, 3 reads after one. `VulkanSharedMemory::UseForDraw` now tracks
     the ranges draws read (vertex fetch, guest DMA indices) and write
     (memory export) since the last barrier, and inserts one only on an
     overlap (read or write after a draw write, write after a draw read),
     after untracked reads (compute, transfer, pixel shader vertex fetch)
     or other writes. Barrier render pass ends 52 -> 24 per frame;
     **19.80 fps vs. 19.44** (alternated 3x, `fable_2_111.log`); 12
     sweep shots off/on all within 1.7-2.0% (snowflakes). The "render pass
     (re)starts" counter doesn't move: these ends weren't counted as
     restarts (separate passes around the export draws).
  3. **Texture loads: done (2026-10-15), `vulkan_hoist_texture_loads`**
     (default on macOS, takes effect from the next submission). Loads
     whose result nothing earlier in the submission can affect (the image
     not sampled or loaded in the command stream yet, its guest memory not
     written by the GPU in the submission: resolves, memory export,
     uploads that stayed in the stream; not scaled resolve, not 3D) are
     recorded into a second list replayed at the start of the submission,
     after the hoisted uploads, with scratch memory from their own pool;
     they leave the image in the sampled layout. Of ~61 loads per frame,
     21 are hoisted, 25 reload a texture already sampled earlier in the
     submission, 15 read what a resolve wrote in it (`Texture loads
     hoisted per frame` line). Texture load render pass ends 20 -> 7,
     restarts 82 -> 69 per frame: **20.83 fps vs. 20.10** (alternated 3x,
     `fable_2_114.log`); 12 sweep shots off/on within 1.8-2.1% (the trail
     orbs come and go with the animation in both).
     Remaining render pass ends per frame: 33 resolves/transfers, 24 FSI
     barriers, 7 texture loads, ~5 uploads.
  4. **Resolve batching: not possible (checked 2026-10-15)** on the trail
     frame of `fable_2_109.log` (frame report). Moving a resolve later
     to share one render pass end with the next requires no draw in
     between to overwrite its EDRAM source or sample its result; for all
     33 resolves the blocking draw comes before the next resolve,
     usually the first draw after it. ~20 of them are one-draw
     post-processing steps (mip/blur chains: a draw renders into EDRAM,
     the resolve copies it out, the next draw samples the copy and
     renders into the same EDRAM). What could still be cut is the cost
     of each step (render pass, resolve dispatch, texture load dispatch,
     copy into the image, each a separate Metal encoder), e.g. a load
     writing the image directly instead of through a scratch buffer and
     a copy; measure the chain's GPU time with a trace first.
  5. **Trace of the chain (2026-10-15, `out/profile/chain_1015.trace`,
     5 s at the trail, uncapped, ~21 fps, GPU 96.5% busy with the game;
     `tools/gputrace_frame.py` prints a frame's encoders in wall-clock
     order).** A game frame is two command buffers, 13.2 + 33.7 ms. The
     post-processing chain (resolves 17-31) is one 2.0 ms run of 55 short
     encoders near the end, plus ~1 ms of shorter runs earlier: **~3 ms,
     ~6% of the frame**, so even a perfect resolve/load path gains at
     most that. **One render encoder takes 17.7 ms (38% of the frame)**:
     most likely the main color pass (~1024 draws at 1120x720, 1x MSAA,
     targets alternating between formats 3 and 12, FSI); then passes of
     4.1, 2.1, 2.1 ms and many of 0.3-0.9 ms. Fragment work is 59% of
     channel time. The unlabeled "Blit Command" intervals that
     `gputrace_timeline.py` counts as 14% of GPU time overlap the other
     encoders (they start with a dispatch and run on past the next render
     pass), so they are not wall time. Next: what costs inside that pass
     (shader profiler, or the frame report's draws of that pass by
     shader), not render pass ends.
  6. **Cost by pixel shader (2026-10-15, `vulkan_debug_skip_ps` groups
     alternated twice for 15 s each, uncapped; `fable_2_118.log`,
     `fable_2_119.log`; frame 46.7-47.8 ms).** Frame time saved by
     skipping: the snow (`718C12FF9E1B888B`, 22 point list draws x 4096
     points, additive, 64x64 texture) **6.4 ms**; `A4A965C189287B99`
     (48 one-point draws writing neither color nor depth, likely the
     memory export draws, plus ~12 clear rectangles) 5.7 ms; the main
     geometry (`8D900846800943C8`, ~680 draws) 7.4 ms; the terrain patch
     shaders 1.7 + 1.8 ms; the hero/house shader ~0. The snow's cost does
     not depend on its size (`vulkan_debug_point_size_scale` 1 / 0.25 /
     0.02: 21.4 / 21.2 / 21.4 fps, `fable_2_120.log`; shots confirm the
     flakes shrink), so it is vertex work: each point runs the procedural
     vertex shader (`19A01C01290E20A7`, 581 ALU instructions, no fetches,
     262 KB of SPIR-V, the largest of 70) 4 times. **Its MSL (SPIRV-Cross)
     has 511 calls to the `optnone` precise-math helpers** (`spvFMul`,
     `spvFAdd`, ...) generated for the translator's NoContraction
     decorations: nearly every instruction becomes a call Metal's compiler
     can't optimize. The FSI pixel shaders too: all 89 checked, ~45000
     calls in total, 495 in the main geometry shader, 1166 in the terrain
     one, up to 2198. (Checked with the local `spirv-cross`; confirm at
     MoltenVK's pinned commit, `tools/sdk_offline/README.md`.) **Next
     lever: xenia-edge 3ec3e8a05 `spirv_moltenvk_allow_contraction`**
     (drop NoContraction on MoltenVK), checking the depth prepass still
     matches the later passes (z-fighting) first.
  7. **Done (2026-10-15): `spirv_moltenvk_allow_contraction`** (default
     on, restart; ported from xenia-edge 3ec3e8a05: `SpirvBuilder` skips
     the decoration in its two NoContraction helpers when the translator's
     `Features::allow_float_contraction` is set, i.e. the switch is on and
     the driver is MoltenVK). In the dumps (`--dump_shaders`) the snow
     vertex shader goes from 511 helper calls to 0, the main geometry
     pixel shader from 495 to 13 (`spvFindUMSB`, not optnone); positions
     stay `[[position, invariant]]`, compiled with `preserveInvariance`.
     **28.43 fps vs. 20.97** at the trail (runs alternated off/on/off/on,
     40 s each, uncapped: `fable_2_124`-`127`; frame 47.7 -> 35.2 ms).
     Six shots each way within 1.7-1.9% (snow), no z-fighting stripes on
     walls, ground or roofs (`out/contract_off`, `out/contract_on`).
  8. **Trace after contraction (2026-10-15,
     `out/profile/contract_1015.trace`, 5 s at the trail, uncapped,
     `fable_2_128.log` 29.1 fps).** GPU time per game frame 29.1-31.2 ms
     (p10-p90, max 32.6): under the game's 33.3 ms, so the trail is at
     the cap on average. The main color pass is 7.2 ms (was 17.7), the
     next 1.5 and 1.4 ms (was 4.1), the post-processing chain 1.6 ms. The
     GPU is busy 86% of the time; the idle is mostly 1-10 ms gaps.
     **What's left is pacing:** frame start to start is 34.8 ms median
     but 68 ms at p90 (24 of ~140 frames took 40-105 ms) while their GPU
     time was a normal 29-32 ms: they missed the 33 ms interval and
     waited for the next. A frame is two submissions, and the GPU idles
     between them for 4.2 ms median (6.5 p90, up to 10): it finishes the
     first (~10 ms of work) before the CPU has recorded and submitted
     the second (~19 ms of work). 29.7 + 4.2 ms > 33.3 ms. Next: find
     why the frame is split there and submit the second part in pieces
     (or earlier) so the GPU doesn't wait for the CPU's recording; and
     the CPU cost of recording the main pass.
  9. **Why the frame is split (2026-10-15, `vulkan_submission_log` +
     `vulkan_long_frame_log`, `gpu_wait_reg_mem_log`; `fable_2_129`,
     `fable_2_130`).** Every frame: the first submission at +1.2 ms (446
     draws, sent at the end of the primary buffer, `ExecutePrimaryBuffer`
     -> `OnPrimaryBufferEnd`), then the CP sits in **WAIT_REG_MEM for
     16-20 ms on guest memory 0x1FC83004 == 0** (15-17 polls, the handler
     sleeps 1 ms between polls), then records the other ~1960 draws and
     submits them with the swap at +27-29 ms. So the CP isn't slow to
     record: the guest releases the rest of the frame late, and a
     submission split can't raise the frame rate. A `sample` of the
     process (5 s at the trail) shows why: the game's render thread
     ("3D Engine") is 96% busy, of which `fable2::functrace::trace` is
     21% of all samples (the app's REX_FUNC_PROLOGUE hook, called on every
     guest function entry even with tracing off), `GpuProgressCheck_82B9BF90`
     18% (spinning on GPU progress) and `FrameLimiterWait_82242628` 5%;
     GameThread is 55% busy (functrace its top leaf too), the CP 59%.
     **Next:** make the hook free when tracing is off (an inline check of
     a global flag in the macro instead of a call; changes the PCH, so the
     whole recompiled code rebuilds), then remeasure the render thread and
     the WAIT_REG_MEM time; the 1 ms poll sleep may also add up to ~1 ms of
     latency per frame.
  10. **Hook made free when off (2026-10-15):** `REX_FUNC_PROLOGUE()` now
     reads a constant-initialized global (`fable2::functrace::g_enabled`,
     set from `FABLE2_FUNC_TRACE` at static initialization) inline and
     calls `Fable2FuncTraceCall` only when tracing is on (it was a call plus
     a function-local static's guard check on every guest function entry:
     a 25 s traced run counts 55 M entries of `__savegprlr_29` alone).
     Rebuilds all recompiled code (13 min). Samples at the trail
     (`fable_2_133`): the hook is gone; GameThread 518 -> 435 busy
     samples of 939; the render thread stays 96% busy, but the freed time
     went into waiting: `GpuProgressCheck_82B9BF90` 165 -> 236 samples,
     `FrameLimiterWait_82242628` 49 -> 76. So the trail is unchanged
     (29.0-29.1 fps, 11-17 frames of 120 over 40 ms, WAIT_REG_MEM 18.9 ms
     median); the gain is CPU headroom for heavier scenes. At the trail
     the render thread waits for GPU progress (written by the CP as it
     processes the ring) about a quarter of its time: what it waits for
     is next. Tracing still works (`FABLE2_FUNC_TRACE=1` writes the
     summary).
  11. **Frame pacing: two fixes (2026-10-15).** The [wait_reg_mem] log now
     says when a wait ended relative to the last guest vblank
     (`fable_2_135`): the 0x1FC83004 waits end 0.13-1.13 ms after a vblank
     (p50 0.55, the 1 ms poll sleep), so the guest's vblank handler
     releases the frame, every 2 vblanks (a 30 fps cap; the render thread's
     `FrameLimiterWait_82242628` / `GpuProgressCheck_82B9BF90` spin on the
     frame-end fence that follows).
     (a) **Guest clock ran 3% slow.** Releases came every 34.38 ms, not
     33.33: the vblank timer (guest ticks) ran at 58.2 Hz. `UpdateGuestClock`
     (src/core/clock.cpp) converted each call's host tick delta with integer
     division and dropped the remainder (24 MHz host, 50 MHz guest: up to
     11/12 of a guest tick per call), and threads polling the time make
     that add up. It now carries the remainder. Result (`fable_2_136`):
     30.0 fps in hitch-free windows (was 29.0-29.1), releases every 33.3 ms;
     game time runs at the right speed too.
     (b) **Paint held the queue while waiting for the display.** The
     submission log now prints each frame's areas (`vulkan_submission_log`
     turns on the frame timing). Frames over 40 ms had ~6 ms in the swap
     and the CP's "submit" was ~10-12 ms per frame. A `sample` showed why:
     the paint thread's vkQueueSubmit holds the shared queue mutex while
     MoltenVK fetches the swapchain's CAMetalDrawable lazily
     (`-[CAMetalLayer nextDrawable]` blocks until the display frees one),
     and the CP's submissions wait for that mutex. Fix:
     `vulkan_fetch_drawable_before_submit` (default on, live) enables
     VK_EXT_metal_objects and exports the acquired image's MTLTexture right
     after vkAcquireNextImageKHR, outside the lock. A/B in one run
     (`fable_2_140`, 4 phases): frames over 40 ms 10.3% -> 0.5%, CP submit
     11.5 -> 1.7 ms per frame, swap stall gone; the mean stays at the
     30 fps cap. The CP now has ~10 ms of slack per frame at the trail.
     Picture checked on screen.
     **Next:** the trail is at the console's 30 fps; the remaining levers
     matter for heavier scenes (GPU ~29-31 ms per frame here), or an
     unlocked frame rate if the guest pacing is ever patched.
  12. **Heavy-scene pass (2026-10-15).** `press` in the command file now
     takes stick directions, so a test can turn the camera: at the trail,
     `press RL 500;wait 300` four times looks back at the wooden building
     behind the hero (`fable_2_142`-`148`). That view draws ~4280 per frame
     (1289 depth prepass, 1194 main geometry, 369 shadow map) and runs
     **26-27 fps**: the only spot in reach below 30 (walking down the trail
     stays at 30). Trace (`out/profile/heavy_1015.trace`): GPU busy 93%,
     two command buffers of 25.6 + 12.3 ms, main color pass 10.6 ms,
     fragment 59% of channel time. **Uncapped measurements:**
     `--video_mode_refresh_rate=120` moves the game's vblank cap to 60 fps
     (the heavy view then runs 36.9 ms per frame), so skips are not
     clipped at 33.3 ms. Saved per frame by skipping
     (`vulkan_debug_skip_ps` groups; an entry `hash/N` now matches only
     primitive type N; `vulkan_debug_skip_depth_only_draws`): depth
     prepass 4.2 ms, main geometry `8D900846800943C8` 3.2 ms, snow 1.5 ms,
     other base-pass shaders 1.2 ms, `401A01FD4E5F8757` 0.5 ms;
     `A4A965C189287B99` 7.1 ms in total, but split: its 72 one-point draws
     (vs `B6C9863F710683EC`: one point at the center, color masked, no
     depth: dummy kick draws) 0.8 ms, and its ~17 rectangle-list clears
     3.8 ms, which is mostly later draws failing against the uncleared
     depth/stencil (the clears are of small targets, the hero/dog texture
     morph levels), not the clears' own cost. So no single draw group
     dominates: it is spread over thousands of draws at ~3 us each.
     Other findings: the 200-290 ms hitches every 20 s in capture runs are
     `tools/mac_capture.sh`'s automatic frame report
     (`--vulkan_frame_report_interval=20`), not the game; **the `fbo` path
     no longer reaches the title screen** (stays on the logos at 25 draws
     per frame, `swap_texture_view=NULL`), so it can't be compared now.
     Host occlusion queries (`occlusion_query_enable`) wait for the GPU at
     every query end (`AwaitAllQueueOperationsCompletion`), but Fable II
     issues none here (2 submissions per frame).
     **Next levers:** per-draw cost (CPU encoding is off the critical path
     now; GPU-side per-draw overhead of FSI: pipeline/descriptor rebinds,
     raster order group waits), the depth prepass under FSI (depth-only
     draws still run an interlocked fragment shader), or fixing `fbo` to
     compare hardware depth/blending.
  13. **The `fbo` path works, and costs far less GPU (2026-10-15).** It
     was never stuck: the statistics line counted only FSI draws, so it
     said 0 draws per frame on `fbo` and `tools/mac_autotest.sh` (which
     detects the title screen and gameplay from that count) gave up; the
     window showed the logos and the title screen normally. The
     statistics now count draws on both paths. `tools/mac_ab_paths.sh`
     runs each path once, uncapped (guest refresh 120 Hz), at the trail
     and at the heavy view, with screen captures (`out/ab_paths/`):
     **trail 20.0 ms per frame (50 fps) on `fbo` vs. 29.8 ms (33.5 fps) on
     `fsi`; heavy view 27.2 ms (36.7 fps) vs. 37.8 ms (26.5 fps)**
     (`fable_2_150`, `fable_2_151`). So at the 30 fps cap `fbo` keeps
     the GPU about a third less busy and the heavy view gets back under
     33.3 ms. Picture: same scene, lighting, fog, orbs, hero; flicker test
     clean on both (16 shots 250 ms apart: `fbo` 1.6-2.0%, `fsi`
     1.9-2.1%, `out/ab_paths/flicker_*`). One difference: the ground right
     of the trail next to the building is a dark brown patch on `fbo` in
     every shot, lighter and snowier on `fsi` (4.6% of pixels differ
     between the paths, most of it snow and the moving orbs). That is
     where problem 1's flickering dark area with roof-shaped edges was, so
     `fbo` may draw the building's shadow consistently where `fsi` did
     only sometimes; needs a console reference.
     **Scene check, then the default switched to `fbo` (2026-10-15):** the
     pause menu, walking into the town (crates, barrels, chickens, the
     hero) and the heavy view match `fsi` (`fable_2_154` / `155`, screen
     captures every 2.5 s). Not reachable from the trail save: interiors,
     the dog. `Fable2App::OnPostInitLogging` now defaults
     `render_target_path_vulkan` to `fbo`, and the user's `fable_2.toml`
     (which pinned `fsi`) was switched too.
     At the 30 fps cap at the trail (`fable_2_156` default `fbo`,
     `fable_2_157` `fsi`): both 30.0 fps; macOS "Device Utilization"
     71-80% on `fbo` vs. 93-96% on `fsi` (`ioreg -r -d 1 -c
     IOAccelerator`, sampled every 2 s).
  14. **Trace and costs on `fbo` (2026-10-15/16, heavy view, uncapped
     with `--video_mode_refresh_rate=120`).** Trace
     `out/profile/fbo_heavy_1015.trace` (`fable_2_158`): GPU busy 70%,
     30.4 ms per frame; `tools/gputrace_kinds.py` (wall time by encoder
     kind per command buffer, two per frame): render 9.9 ms (74 encoders),
     dispatches 0.8 ms (92), copies 0.7 ms, **idle between encoders 3.4
     ms** (FSI: render 16.3 ms in 41 encoders, idle 1.1 ms). The SDK counts
     72 render pass (re)starts per frame; barrier ends come from
     `VulkanRenderTargetCache::DumpRenderTargets` (80 per frame: fbo copies
     the host render target into the EDRAM buffer before each of the ~40
     resolves) and `PerformTransfersAndResolveClears` (52, ownership
     transfers). Saved per frame by skipping (`fable_2_159`, base 26.7 ms):
     depth prepass 4.3 ms, snow 2.0, main geometry `8D900846800943C8` 1.4,
     other base-pass shaders 0.8, `401A01FD4E5F8757` 0.7, the 72 dummy
     points ~0. Adaptive quad patches alone (`vulkan_debug_skip_adaptive_patches`,
     `fable_2_161`): 1.6 ms at the heavy view, 0.7 ms at the trail.
     **Vertex work is the lever now:** quad patches (prim 18) are drawn as
     a non-indexed triangle list at the maximum factor (16: 17x17 cells x 6
     vertices = 1734 vertex shader runs per patch for 324 distinct grid
     points); at the heavy view ~340 single-patch continuous draws plus 106
     adaptive draws with ~1980 patches (factors 2-16), about 4 M vertex
     shader runs per frame. And point sprites (snow: 23 draws x 4096
     points, 581-instruction procedural vertex shader) are expanded in the
     vertex shader to 4-vertex strips (`expand_point_sprites_in_vs_`, no
     geometry shaders on Metal), so that shader runs 4x per point.
     **Next:** an indexed grid for quad patches (one vertex shader run per
     grid point if Apple's post-transform reuse applies; up to ~5x fewer),
     or the patch's own factor for adaptive patches; host point primitives
     with `PointSize` for point sprites (needs `largePoints`, square
     points only, point coordinates in the pixel shader).
  15. **Done (2026-10-16): indexed quad patch grid**
     (`vulkan_quad_patches_indexed`, default on, restart; `docs/macos_port.md`,
     "Quad patches without host tessellation"): one instance per patch over
     a cached grid index buffer instead of 6 vertex shader runs per cell.
     Uncapped on `fbo` (`tools/mac_ab_paths.sh
     "indexed=--vulkan_quad_patches_indexed=true"
     "cells=--vulkan_quad_patches_indexed=false"`; `fable_2_163` / `162`):
     **trail 18.9 vs. 20.0 ms per frame, heavy view 24.9 vs. 26.5 ms**;
     terrain unchanged in the captures. Left of the terrain cost: adaptive
     patches still use the maximum factor (their own factors are 2-16).
  16. **Done (2026-10-16): native point sprites**
     (`vulkan_native_point_sprites`, default on, restart; `docs/macos_port.md`,
     "Point sprites as host points"): point lists are host points with
     `gl_PointSize` / `gl_PointCoord` instead of 4-vertex strips. Uncapped
     on `fbo` (`tools/mac_ab_paths.sh "points=--vulkan_native_point_sprites=true"
     "quads=--vulkan_native_point_sprites=false"`; `fable_2_164` / `165`):
     **trail 17.2 vs. 19.5 ms per frame, heavy view 23.6 vs. 24.6 ms**.
     Snow and the trail orbs look the same; flicker test clean (16 shots
     1.7-2.0%; a first run showed the game's exposure still settling after
     the load in the first shots, a repeat did not). Since the fbo switch
     the trail went 20.0 -> 17.2 ms and the heavy view 26.7 -> 23.6 ms of
     GPU per frame.
  17. **Depth prepass: not the lever (2026-10-16, heavy view, uncapped,
     `fbo`).** The 4.3 ms item 14 saved by skipping it was mostly the
     base pass: the prepass also writes the stencil the base pass tests,
     so without it the picture is white and the base pass shades nothing.
     Two new switches keep the downstream the same (white) and change only
     the prepass (`tools/mac_ab_paths.sh`; `fable_2_170`-`173`, median of
     the last 120 frames over 4000 draws): normal 23.1 ms, 1x1 scissor
     23.0, 1x1 scissor and one triangle per draw 21.9, skipped 18.7. So
     the prepass's vertex work (1275 strip draws, ~950 K indices, mostly
     `F160B4DA459A6D40`, a 4-`dp4` position transform) is **~1.1 ms**;
     its pixels can't be separated from the base pass. The other 3.2 ms
     (one-triangle vs. skipped) is the ~1340 draws' fixed cost plus the
     render passes that hold only them (shadow maps at `z@0` pitch 1040
     and 320, a 1280-pitch pass, a 4x pass): Metal System Trace
     (`out/profile/pre_base.trace`, `pre_skip.trace`) has 10 fewer render
     encoders per frame with the skip, and GPU busy 17.0 vs. 14.1 ms per
     frame (74% / 70% of the wall clock). The per-term zero guard in
     `dp4` (`ZeroIfAnyOperandIsZero`) is a few ALU ops per term on a
     shader that is mostly fetch, so ~0.2 ms at most; not worth the
     semantics change. **Encoder count is the bigger lever:** the base
     trace has ~340 Metal encoders per frame (137 render, 141 compute,
     58 copy to image) and 1.8 ms of idle gaps between encoders per
     command buffer; the short runs alternate dispatch / copy to image
     (`DIDIDI...`): each hoisted texture load records dispatch, barrier,
     copy to image, barrier (`texture_cache.cpp`), so batching them (all
     dispatches, one barrier, all copies) would remove ~100 encoders.
  18. **Done (2026-10-16): batched texture load copies**
     (`vulkan_batch_texture_load_copies`, default on, takes effect at the
     next submission; `docs/macos_port.md`, "Hoisted texture loads"): the
     ~64 hoisted loads per frame run as all dispatches, one barrier, all
     copies, with their image barriers merged into one before and one
     after the copies. A first version that only moved the copies after
     the dispatches gave nothing: the per-texture barriers between the
     copies still split the blit encoder at each one. Uncapped on `fbo`,
     two runs in both orders (`tools/mac_ab_paths.sh
     batch=--vulkan_batch_texture_load_copies=true
     perload=--vulkan_batch_texture_load_copies=false`; `fable_2_180`-`183`):
     **heavy view 20.6 / 20.1 vs. 22.7 / 22.8 ms per frame, trail 16.6 /
     16.6 vs. 16.9 / 17.2 ms**. Trace (`out/profile/pre_batch2.trace`):
     1658 copy-to-image encoders per second vs. 3360. Sweep alternating the
     switch 16 times (`out/sweep_batch`): shots within 1.7-2.0% of the
     first (snow), the last one off only because the camera turned. Left
     in the stream: ~36 loads per frame (textures sampled earlier in the
     submission, or data the GPU resolved in it), which still alternate
     dispatch / copy (`DIDIDI` runs later in the frame); ~100 texture loads
     per frame in total is itself worth a look (what changes every frame).
  19. **Texture reloads (2026-10-16; `docs/macos_port.md`, "Why textures
     reload every frame").** `texture_load_log` at the heavy view: ~100
     loads per frame, ~40 of them render targets reloaded after their
     resolves (needed), ~60 small textures outdated by CPU writes elsewhere
     in the same 16 KB host page, with the same guest data every time.
     `texture_skip_unchanged_reloads` (off by default) skips those by hash:
     59 loads per frame fewer and the same picture (sweep alternating it,
     `out/sweep_skip`, all within 1.3-2.1%), but no measurable change in
     frame time (`fable_2_190`-`193`) or CPU time, so it stays opt-in.
     **What the resolve-to-texture path costs instead** (trace
     `out/profile/pre_batch2.trace`, heavy view, per command buffer, about
     two per frame): compute work 1.24 ms (52 encoders: the fbo dumps of
     render targets into the EDRAM buffer, the resolves, the loads), copy
     to image 0.41 ms (16 encoders), and **idle gaps** between dependent
     encoders: 0.82 ms dispatch to dispatch (28.5 x 29 us), 0.60 ms
     dispatch to copy (14.3 x 42 us), 0.21 ms copy to render pass. So each
     resolve-then-sample step costs about 4 encoders and ~100 us of idle
     GPU. **Levers:** (a) load resolved textures straight into the image
     (storage image writes) instead of a scratch buffer and a copy, which
     drops the copy encoder and the dispatch-to-copy gap (~1 ms of the ~3.3 above
     per command buffer); the load shaders exist here only as prebuilt
     SPIR-V (`src/graphics/shaders/vulkan_spirv/texture_load_*_cs.h`), so
     it needs Xenia's `texture_load_*.xesl` sources or new GLSL ones
     (`glslangValidator` is installed); (b) on fbo, resolve from the host
     render target straight into the texture when the resolve covers the
     whole texture with the same format, skipping the EDRAM dump, the
     shared memory and the load (bigger win, harder to keep exact).
  20. **Done (2026-10-16): texture loads straight into the image**
     (`vulkan_texture_load_to_image`, default on, hot reload; option (a)
     of item 19; `docs/macos_port.md`, "Texture loads straight into the
     image"). New GLSL load shaders writing storage images
     (`tools/build_texture_load_image_shaders.sh` rebuilds their headers).
     ~28 loads per frame at the heavy view skip the scratch buffer and the
     copy. Trace, per command buffer: copy encoders 1.6 vs. 14.5, idle
     gaps 2.17 vs. 3.26 ms. Sweep alternating it: same picture.
     Uncapped, both orders (`tools/mac_ab_paths.sh
     direct=--vulkan_texture_load_to_image=true
     copy=--vulkan_texture_load_to_image=false`; `fable_2_205`-`208`, GPU
     otherwise 9-16% busy): **heavy view 20.0 / 19.5 vs. 20.9 / 20.7 ms
     per frame, trail 16.6 / 16.7 vs. 16.7 / 16.9 ms**. Since the fbo
     switch: heavy view 26.7 -> ~19.8 ms, trail 20.0 -> ~16.6 ms.
     **Open:** one run of five with it on
     (`fable_2_197`, while those games ran) never reached gameplay: tens of
     thousands of `Unhandled guest access violation` reads of guest
     0x00000000 / 0x00000020 during the load (the log rotated past its
     start); the other four, and two repeats, were fine. No run without
     it crashed, but none of them ran under that load either. If it
     happens again, rerun with the switch off to tell. The four timing
     runs (two with it on) were fine.
  21. **Done (2026-10-16): resolves straight from the host render target**
     (`direct_host_resolve`, default on, hot reload; option (b) of item
     19, as far as the resolve; `docs/macos_port.md`, "Resolves straight
     from the host render target"). The fast 32bpp resolve of a 1x color
     render target (8_8_8_8, 2_10_10_10, 32_FLOAT) owning the whole region
     reads the render target with a new GLSL shader instead of dumping it
     into the EDRAM buffer first: 16 of the ~25 fast copies per frame at
     the heavy view (`vulkan_resolve_log` tags them `direct`). Sweep
     alternating it: same picture. Uncapped, both orders
     (`tools/mac_ab_paths.sh direct=--direct_host_resolve=true
     dump=--direct_host_resolve=false`; `fable_2_216`-`219`): **heavy view
     18.9 / 18.9 vs. 19.4 / 19.3 ms per frame, trail unchanged (16.6)**.
     Since the fbo switch: heavy view 26.7 -> ~18.9 ms, trail 20.0 -> ~16.6
     ms. What is left of the resolves at the heavy view: the depth copies
     (1024x1024 shadow maps, 1120x720 and 560x360 depth), the 4x MSAA ones,
     and the full copies (shaders 4-7: the 1120x720 `k_8`, the 1_5_5_5 mip
     chain, 2_10_10_10_FLOAT at 280x184 and 1120x720), ~9 dumps per frame.
     The next step along this line would be the depth and 4x MSAA variants
     of the same shader, or sampling resolved render targets without the
     shared memory round trip at all.
  22. **Done (2026-10-16): direct resolves of depth**
     (`vulkan_direct_resolve_depth`, default on, hot reload;
     `docs/macos_port.md`, same section). The 1x depth render targets (the
     two 1024x1024 shadow maps and the 1120x720 depth per frame at the heavy
     view) resolve straight from the depth and stencil views, packed like
     the depth dump (D24S8 and 20e4). Same picture (sweep, shadows
     unchanged). Frame time within the noise (`fable_2_221`-`228`); trace
     per command buffer: compute 1.50 vs. 1.63 ms, so ~0.3 ms of GPU time
     per frame. Still dumped at the heavy view: the 4x MSAA depth (560x360,
     640x360, 1280x720) and color (280x184, 256x256), and the full copies
     (shaders 4-7). A 4x MSAA variant would read `texelFetch(..., sample)`
     from multisampled views (the dump's sample mapping: bit 0 horizontal,
     bit 1 vertical), but those copies are small.
  23. **Done (2026-10-16): per-pass breakdown, and render passes limited to
     what they draw** (`docs/macos_port.md`, "Render pass areas and the
     EDRAM rows draws claim"). `vulkan_pass_log` labels every Metal render
     encoder with its pass position and logs the frame's passes;
     `tools/mac_pass_costs.py <trace> <log>` gives GPU time per pass.
     It showed ~67 passes per frame with at most one draw (mostly ownership
     transfers) costing ~5.4 ms of fragment time per frame, because every
     pass loaded and stored the whole EDRAM-tall framebuffer and because
     unclipped screen-space draws claimed their render target down to the
     end of the EDRAM. Fixed with `vulkan_tight_render_area` (render area =
     union of the pass's scissors; transfer and draw scissors bounded) and
     `execute_unclipped_draw_vs_on_cpu` now on by default (as in Xenia).
     Same picture. Uncapped: **heavy view 16.6 / 16.5 vs. 19.8 / 19.6 ms**
     (`fable_2_239`-`244`), now at the 60 fps cap like the trail; GPU **86%
     -> 67% busy** at the heavy view (render encoder time per command buffer
     14.4 -> 6.6 ms). Since the fbo switch: heavy view 26.7 -> 16.6 ms
     (capped). The ~17 full-height single-draw passes per frame seen in
     `out/profile/passes_vs.trace` (clear-like shader `A4A965C189287B99`
     and depth-only draws at 320x8192, 160x4096, 560x4688) were from before
     the draw scissor clamp; with it (checked 2026-10-16, `fable_2_252`,
     `253`, `draw_extent_log` and the per-pass height estimates now in the
     `[passes]` lines) only the 1024-row shadow maps begin taller than
     1024 rows, and transfers with at most one draw total 1.3 ms of fragment
     time per frame (~30 us each). The draws without an estimate are the
     groups of 24 that use no render target (16x16 scissors). What is left
     per pass (`out/profile/passes_now.trace`, GPU 70% busy at the 60 fps
     cap): the base pass ~1.0 ms vertex + 1.0 ms fragment, the prepass ~0.7
     ms, the 2-draw full-screen pass `1.8` (1120x720, shaders
     `1B4B5AE12EF270CA`, `0F99810F072BF0FC`) 0.44 ms of fragment time, and
     one 280x180 post-processing draw (`28DBE18F313350BF`) 0.23 ms; the
     other passes are under 0.1 ms each. Shading cost, not pass overhead,
     is what remains.
  24. **Done (2026-10-16): texture fetches with implicit LOD**
     (`vulkan_tfetch_implicit_lod`, on by default on macOS, restart;
     `docs/macos_port.md`, "Texture fetches: implicit LOD instead of
     explicit gradients"). Pixel shader fetches whose LOD the guest computes
     sample with an implicit LOD plus the same bias instead of explicit
     gradients. Same picture (sweeps in both modes); full-screen shadow mask
     pass -18% fragment time, blur -8%, base pass -3 to -6%, GPU 70% -> ~66%
     busy at the heavy view. The other per-fetch overhead (the unsigned and
     signed samples behind uniform branches and the per-component sign
     switches on system constants) is still there; specializing it per
     pipeline would mean more pipelines.
  25. **Done (2026-10-17): texture signs specialized per pipeline**
     (`vulkan_tfetch_specialize_signs`, on by default on macOS, hot reload;
     `docs/macos_port.md`, "Texture fetches: signedness specialized per
     pipeline"). On the host render target path the pixel shader
     modification's unused FSI bits hold 2 bits per texture binding (the
     first 7): all unsigned, all signed or gamma RGB with unsigned alpha
     (about 80%, 12% and 3% of the fetches at the trail), so those fetches
     use constant signs and sample one host view. Same picture (two
     sweeps switching it, `out/sign_check{,2}`). Uncapped
     (`--vsync=false`) at a 2321-draw trail view 80.6 -> 84.7 fps; constant
     signs for every texture (the upper bound) had cut the base pass
     fragment time by a third. Note: Metal System Traces leave a ~2 GB
     `instruments*.ktrace` per trace in `$(getconf DARWIN_USER_TEMP_DIR)`
     on the system disk (`TMPDIR` doesn't move it); measure without traces
     where possible, and remove those files after tracing.
  26. **Done (2026-10-17): GPU time in the log** (`vulkan_gpu_time`, on,
     restart; `docs/macos_port.md`, "GPU time per frame without traces").
     A timestamp at the start and end of each submission, read once its
     fence signals, gives a "GPU time" line after each frame statistics
     line. Use it instead of Metal traces at the default settings (the
     game's 30 fps): logos 0.7 ms/frame (2%), title screen 4.7 ms (14%),
     gameplay at the trail (~2300 draws) 10.6 ms (32%), `fable_2_284`.
  27. **Done (2026-10-17): GPU time per render pass in the log**
     (`vulkan_pass_gpu_time`, off, hot reload; `docs/macos_port.md`, same
     section). Times the passes of one frame in ten (timing makes that
     frame's passes run one after another; ~3% on the frame average). At
     the trail at 30 fps (`fable_2_291`): 63 transfers per frame take 3.2
     ms of the timed frames, more than the main scene passes (2.4 ms; the
     466-draw pass `8D900846800943C8` 1.8 ms); 11 color transfers into a
     320x8192 target alone 0.57 ms. The title screen: 25 transfers, 1.15
     ms. Next: fewer or cheaper transfers.
  28. **Done (2026-10-17): the frame limiter sleeps instead of spinning**
     (`[perf] frame_wait_sleep_us`, default 250, 0 = spin;
     `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp`). The game
     paces its frames by spinning on `GpuProgressCheck_82B9BF90` until the
     GPU progress counter (advanced by the emulated vblank) reaches the
     deadline; at the 30 fps cap that kept the "3D Engine" thread 92% busy
     (`sample`: the check and the limiter were its top functions). Sleeping
     250 us between checks: the thread 50% busy, still 30 fps. The rest of
     the CPU (~2.3 cores at the trail): "GPU Commands" (the command
     processor) ~90%, "GameThread" ~70%.
  29. **Done (2026-10-17): clear quads cut their rectangle out of the
     transfers** (`clear_quad_transfer_cutout`, on by default on macOS, hot
     reload; `docs/macos_port.md`, "Transfers: clear quads cut out what
     they overwrite"; log with `vulkan_transfer_log`). The game clears
     the EDRAM with rectangle-list quads (vs `1E6883FCCDE1F688`), often the
     same area at 1x and then at 4x MSAA, so each clear first copied the
     old contents into the new render target. A draw that overwrites its
     whole rectangle now cuts that rectangle (from running its vertex
     shader on the CPU) out of the transfers, as resolve clears already
     did. At the trail: ~37 clear quads per frame qualify, transfers 93 ->
     83 per frame, transfer passes 63 -> 57, their time in timed frames
     3.7 -> 2.9 ms (`fable_2_300`). Same picture (sweep, `out/cutout_check`,
     on/off pairs 1.3-2.0% apart like on/on). **But the frame GPU time
     doesn't move**: 11.5-12.0 ms either way, between runs
     (`fable_2_302`-`305`) and alternating within one run (`fable_2_306`).
     The pass timer overstates small passes (in timed frames each one
     waits for the one before). Left on: less work, same picture.
  30. **Done (2026-10-17): waits for several objects no longer spin**
     (`src/core/threading_posix.cpp`; `docs/macos_port.md`, "CPU: busy
     waits"). The polling wait for several objects slept
     `min(remaining, 1 ms)` with the remaining time truncated to whole
     milliseconds, and alertable waits pass it 1 ms slices: under 1 ms left
     became `sleep_for(0)`, so every alertable wait for several objects
     spun. The audio worker waits that way for its semaphores and used 79%
     of a core (with audio muted). It now sleeps the real remaining time,
     and the alertable slices round up (a timed wait ends up to 1 ms late; a
     signal still ends it at once). At the trail at 30 fps: 2.34 -> 1.55
     cores, the process's CPU energy 9.8 -> 5.9 W. Listen for audio
     problems (the user's `fable_2.toml` mutes audio).
  31. **Done (2026-10-17): the game's main loop sleeps while no step is
     due** (`[perf] game_loop_sleep_us`, default 250, 0 = yield only;
     `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp`). The loop
     yields and re-reads the timebase until the next simulation step is
     due, which kept GameThread 90% busy (27% in the kernel, from the
     yields). Sleeping 250 us after the yield: GameThread 14%, the process
     0.82 cores and 3.1 W, still 30 fps, same picture (`out/cpu_check`),
     same loading time.
  32. **Done (2026-10-17): smaller command processor costs.**
     `RegisterFile::GetRegisterInfo` (called on every register write to
     decide on a debug message) is a table instead of a switch over the
     register list (it was ~6% of the thread); IssueDraw checks for
     memexport ranges before looking up the readback cvar by name (a mutex,
     a string and a map lookup per draw). The frame statistics now log
     `Command processor CPU: N ms/frame, M% of the time` (the thread's CPU
     clock): ~7 ms per frame, 21%, at the trail. Note `vulkan_long_frame_log
     = true` in the user's `fable_2.toml` makes every draw read the clock
     ~10 times (~4% of the thread). Measure CPU with
     `tools/mac_thread_cpu.c` (per thread, plus the process's energy and
     instructions), not `sample` at its default 1 ms interval: that cut
     the game's CPU use by more than half and showed waiting threads as
     running. What is left (0.8 cores): command processor 22% (MoltenVK
     encoding at submission about a quarter of it), 3D Engine 14%,
     GameThread 14%, two XThreads ~7% each.
  33. **Done (2026-10-17): where the GPU time goes** (`docs/macos_port.md`,
     "Transfers: clear quads cleared in place"). Switching parts off at the
     trail (~10.5 ms per frame): resolve copies 3.2-3.5 ms (reloads of
     resolved textures ~1 ms of it, `debug_skip_resolved_texture_loads`),
     transfers ~2.3 ms (`vulkan_debug_skip_transfers`), depth prepass ~1.7
     ms, resolve dumps ~0.3 ms: the EDRAM emulation is over half of it.
     `vulkan_pass_gpu_time` now also times each resolve (kind "resolves":
     ~35 per frame, 70-150 us each whatever the size). GPU power from the
     kernel without root: `tools/mac_gpu_power.c` (IOReport GPU energy,
     clock residency and the average clock; build line in the file); it
     measures the whole system, so alternate A and B within one run.
     Transfers alone: 5.87 vs 4.92 W.
  34. **Done (2026-10-17): clear quads cleared in place**
     (`clear_quad_in_place`, on by default on macOS, hot reload;
     `docs/macos_port.md`, same section). The game clears a 1x target with a
     1x strip and then a 4x quad over the same EDRAM, so each 4x clear moved
     the tiles to a 4x target and the next 1x draw moved them back. A clear
     quad with one value whose tiles a same-format target of another sample
     count owns now clears that target instead (vkCmdClearAttachments, the
     draw skipped). At the trail: 8 per frame, transfers 83 -> 62, tiles
     13879 -> 9489 (`fable_2_354`), timed frames 20.1-21.5 -> 16.1-16.7
     ms, render passes 14.5 -> 10.3 ms (`fable_2_356`), GPU power 7.57 ->
     7.06 W over four alternating pairs (`fable_2_357`). Same picture at the trail,
     after walking and in the pause menu. **MoltenVK trap:** a depth clear
     between draws of one render pass loses the depth of the draws after it
     (the shadow maps came out empty); the clears now end the render pass
     first. Next on the GPU: the strip clears' remaining transfers, the
     fixed cost per resolve, the reloads of resolved textures.
  35. **Done (2026-10-17): the strip clears** (`docs/macos_port.md`,
     "Transfers: the strip clears (tried, dropped)"). Two strips were not
     taken as clear quads (Z -3.7e-9 with clipping disabled; the host
     clamps rather than clips then): now they are, transfers 62 -> 60 per
     frame (`fable_2_366`). Holding the strips' transfers until the next
     clear and cutting that clear out too was implemented and dropped: same
     picture, no gain (`fable_2_365`). The leftover slivers (columns between
     the cleared width and the pitch) must still be copied, and transfers
     cost per pass (~0.1-0.25 ms in timed frames) rather than per pixel.
     **Next on the GPU: fewer passes**, e.g. a draw's transfers into all of
     its render targets in one render pass with the draw (now one pass per
     destination, then the draw's), and the per-resolve cost. Measure only
     alternating within a run: other apps kept the GPU 75% busy at idle.
  36. **Done (2026-10-17): transfers in the draw's render pass**
     (`vulkan_transfers_in_draw_pass`, on by default on macOS, hot reload;
     `docs/macos_port.md`, "Transfers: in the draw's render pass"). A draw's
     transfers now run in the draw's own render pass (all its render targets
     attached, each transfer writing its own attachment), and the draw
     continues it, instead of one pass per destination and then the draw's.
     Not when a transfer reads one of the draw's render targets, or a color
     target's transfer format differs from its draw format (k_32_FLOAT, 2
     per frame; `vulkan_transfer_log` says why). At the trail: transfer
     passes 49 -> 13 per frame (the 11 in-place clears and those 2), about
     139 -> 114 passes, timed frames ~20.0 -> ~18.5 ms (`fable_2_368`),
     untimed GPU time ~11.1 -> ~9.6 ms per frame, GPU power 6.91 -> 6.77 W
     over four alternating pairs (`fable_2_371`; within the noise of the
     other apps). Same picture at the trail. **Next:** about 7 passes per
     frame end on the shared memory barrier a draw pushes after a resolve
     (compute write -> read, `VulkanSharedMemory::UseImpl`), even when the
     draw reads nothing the resolve wrote; skipping it then would let the
     passes holding only transfers continue into the draw.
  37. **Done (2026-10-17): no shared memory barrier before draws not reading
     what a resolve wrote** (`vulkan_skip_draw_barriers_after_writes`, on by
     default on macOS, hot reload; `docs/macos_port.md`, end of
     "Transfers: in the draw's render pass"). `VulkanSharedMemory` keeps the
     ranges written by compute and transfers (resolves, uploads in the
     command stream) since the last barrier making everything visible to
     draws; a draw whose known vertex and index ranges miss them, and which
     writes nothing, skips the barrier. At the trail: compute write -> read
     barriers 33 -> 25 per frame, passes ~118 -> ~110, timed frames ~17.8
     -> ~16.4 ms (`fable_2_372`), GPU power 6.64 -> 6.46 W over four
     alternating pairs (`fable_2_375`). Same picture; flicker test (16
     shots 250 ms apart) 2.3-3.0% vs 2.5-3.0% with it off (`fable_2_373`,
     `fable_2_374`), animation only. Next on the GPU: the per-resolve fixed
     cost (~35 resolves per frame, 70-150 us each), the reloads of resolved
     textures (~1 ms).
  38. **Done (2026-10-17): resolves write the texture too, and GPU writes
     invalidate only textures they touch** (`vulkan_resolve_into_texture`
     and `shared_memory_exact_gpu_write_watches`, both on by default on
     macOS, hot reload; `docs/macos_port.md`, "Resolves: written straight
     into the texture").
     - A direct 32 bpp resolve also writes the texture at its destination
       (its storage image from `vulkan_texture_load_to_image`). That texture
       is then up to date and not reloaded: 17 loads per frame.
     - The fusion only paid once watches stopped firing per 16 KB host page
       for GPU writes. Before, the second shadow map's resolve invalidated
       the first shadow map and 0x133E1000, and those were reloaded anyway.
     - At the trail (`fable_2_395`, three alternating triples): GPU time
       8.46 -> 7.40 ms per frame, GPU power 2.33 -> 2.13 W, CP CPU -0.2 ms.
       The exact watches alone account for 0.3 ms of it.
     - Same picture; flicker test clean.
     - Also measured: MoltenVK ends the Metal encoder at every barrier
       outside a pass, so each resolve takes ~3 compute encoders.
       Argument buffers off saved 0.5 ms GPU but no power, and cost CPU.
     - **Next:** the reloads left, about 14 per frame. The k_1_5_5_5 mip
       chain at 0x12704000 is 0.4 ms, the 64-bit float textures 0.6 ms
       (a 64 bpp fused shader), the k_8 mask 0.15 ms.
  39. **Done (2026-10-17): the resolves with format conversion write the
     texture too** (`vulkan_resolve_into_texture_full`, on by default, hot
     reload; `docs/macos_port.md`, end of "Resolves: written straight into
     the texture").
     - The full resolves (8/16/32/64 bpp, from the EDRAM buffer) have a
       variant that also stores the texels into the texture's storage view,
       mip levels included. The shaders are the upstream SPIR-V decompiled
       with spirv-cross, plus the stores.
     - GPU-write reloads 13.9 -> 3.0 per frame. GPU time only -0.12 ms
       (7.87 -> 7.76 ms, four alternating pairs, `fable_2_402`); power
       flat. Both switches together against off: 8.32 -> 7.42 ms, 1.93 ->
       1.73 W (`fable_2_401`).
     - Same picture (block-wise diff and flicker test).
     - Left: the two formats sharing 0x12CFD000, and 0x1ECFF000 (no
       storage image).
     - Testing note: one flicker run moved the hero, because
       `--merge_controllers` passes on a controller used for another app.
  40. **Done (2026-10-18): smaller command processor CPU costs**
     (`docs/macos_port.md`, "CPU: the command processor, and what didn't
     help"; all three switches on by default, hot reload).
     - `vulkan_reuse_texture_descriptor_sets`: binds a texture descriptor set
       written earlier in the frame with the same bindings again. At the
       trail, 1458 -> 282 sets written per frame.
     - `type0_register_ranges`: type-0 packets go through the range path, as
       upstream does.
     - `frame_timing_fast_clock`: the long-frame log's timer reads
       `mach_absolute_time`.
     - Command processor 6.47 -> 5.61 ms per frame (about -13%, alternating
       within a run, `fable_2_424`, `_431`). Same picture.
     - Tried and not kept: QoS classes below user interactive (nothing moved
       to the efficiency cores; utility doubled the GPU time), and
       `-mcpu=apple-m1` for the game code (no change).
     - **Measuring:** pass `--vulkan_frame_report_interval=0`. The capture
       script's 20 s frame reports cost a 200 ms frame each and CPU;
       `mac_autotest.sh` no longer waits for one then. `MAC_CAPTURE_EXE`
       picks another binary for `mac_capture.sh`.
  41. **Done (2026-10-18): the frontbuffer copy is written by its resolve
     too** (`vulkan_resolve_into_texture_recreate`, on, hot reload;
     `docs/macos_port.md`, "Resolves: written straight into the texture").
     - The 1280x720 texture at 0x1ECFF000, which the swap fetches, was
       created before its memory was GPU-written, so it had no storage image
       and was reloaded every frame.
     - Such textures at a resolve destination are now taken out of the cache
       (kept until the GPU is done with them) and created again with a
       storage image.
     - Reloads after a GPU write 3.0 -> 2.0 per frame; same picture, flicker
       test clean.
     - GPU time not measurable this time: remote screen sharing loaded the
       GPU. Expect little; one 1280x720 reload per frame.
     - Left: 0x12CFD000, resolved in two formats.
     - Note: on 2026-10-18 the save loaded once into a spot past the trail
       (3180 draws per frame, `fable_2_439`); later runs were back at the
       trail. Check the draw count before comparing numbers across runs.
  42. **Done (2026-10-18): the window stays black or grey after a resize**
     (`vulkan_fix_layer_drawable_size`, on, restart; `docs/macos_port.md`,
     "Window resize: MoltenVK leaves the layer at 1x1").
     - Seen by the user when maximizing the window. Reproduced by resizing
       through `System Events`. About 1 resize in 4 left the window black
       (or flat grey) until the next resize. After a move to the 60 Hz second
       display, every resize did.
     - Cause: when the old swapchain still has unpresented images, MoltenVK
       sets the CAMetalLayer's drawableSize to 1x1 after setting the new
       size. The log line `CAMetalLayer drawableSize 1x1 after creating ...`
       showed this on 30 of 30 swapchain creations.
     - Fix: the presenter sets drawableSize back to the swapchain size. Since
       then, 30 of 30 resizes on both displays show the picture.
     - Ruled out on the way:
       - not `vulkan_fetch_drawable_before_submit` (fails the same with it
         off);
       - not Core Animation transactions (a `[CATransaction flush]` after
         presents changed nothing).
  43. **Measured (2026-10-18): this round's switches, all off vs. all on**
     - Switches: `vulkan_transfers_in_draw_pass`,
       `vulkan_skip_draw_barriers_after_writes`, `vulkan_resolve_into_texture`,
       `shared_memory_exact_gpu_write_watches`,
       `vulkan_reuse_texture_descriptor_sets`, `type0_register_ranges` and
       `frame_timing_fast_clock`.
     - Method: alternated within one sweep, 4 pairs of 35 s each, in two runs
       (`fable_2_441`, `fable_2_442`). Power came from `out/mac_gpu_power`
       and `out/mac_thread_cpu` every 5 s.
     - Scene: the save now loads into the Bowerstone intro (Murgo's wagon,
       ~5300 draws per frame, a cutscene that changes the draw count between
       2000 and 5600), so the pairs are noisy. A shell `sleep` before the
       sweep (`"sleep 220;sweep"`) gets past the cutscene; the in-game
       `wait` does not.
     - Means over both runs, off -> on:

       | Measure | Off | On | Change |
       | --- | --- | --- | --- |
       | GPU time (ms/frame) | 14.8 | 11.4 | -23% |
       | Command processor CPU (ms/frame) | 13.0 | 10.3 | -21% |
       | GPU power (W) | 3.00 | 2.79 | -7% |
       | Process CPU power (W) | 5.33 | 4.86 | -9% |
       | Cores | 1.16 | 1.04 | |

       The 30 fps cap held throughout. 6 of 8 pairs were lower in both GPU
       and CPU power.
     - Hitches at 30 fps: pipeline compiles for pipelines the storage doesn't
       have yet (new area). These are about 100 ms each, and 893 ms once for
       several in one frame. They go away once stored (683 -> 709 pipelines
       created from storage between two runs).
  44. **Measured (2026-10-18): the 60 fps cap holds at the trail**
     (`--video_mode_refresh_rate=120`, restart). Runs of 300 s each at the
     trail (2313 draws per frame), `fable_2_453` (30) and `fable_2_454` (60):

     | Measure | 30 fps cap | 60 fps cap |
     | --- | --- | --- |
     | Frame rate | 30.0 | 60.0 |
     | Frames over 40 ms (per ~290 s) | 2 | 0 |
     | Longest frame | 47 ms | 19 ms |
     | GPU time (ms/frame) | 7.6 | 10.8 |
     | Command processor CPU (ms/frame) | 5.7 | 5.5 |
     | Process CPU power (W) | 3.05 | 4.20 |
     | Cores | 0.75 | 1.07 |
     | GPU power, whole system (W) | 5.9 | 7.6 |

     - The GPU was shared, so GPU power and GPU time are upper bounds (other
       games and apps were running). The per-process shares first given here
       came from a bug in `tools/mac_gpu_by_process.sh` (fixed in item 45),
       which credited each process with the next one's GPU time. The same
       applies to the `VTEncoderXPCService` share in the first try; the
       3.3 W of idle GPU power during remote viewing was real.
     - Game busy share of the GPU: ~23% at 30 fps vs ~65% at 60 fps.
     - The first try (`fable_2_452`, 47-52 fps) ran while a remote viewing
       session's video encoder took 24-72% of the GPU.
     - Not checked yet:
       - game speed at 60 (whether the game logic follows the frame rate);
       - the heavy view behind the hero (26-27 fps at the 30 cap);
       - the tick-rate patches (`high_tick_rate`, `higher_hf_tick_rate`),
         which run the UI and physics faster.
  45. **Measured (2026-10-18): game speed and smoothness at 60 fps.** All at
     the trail, `--video_mode_refresh_rate=120`.
     - **Speed is real time.** Holding forward for 3 s (the command file
       times presses with the host clock) ends at the same spot at 30 fps,
       at 60 fps, and at 60 fps with both tick-rate patches
       (`out/ab_paths/walk`). The game's ticks follow the clock, not the
       frame rate.
     - **60 distinct images per second.** A 3 s screen recording while
       walking (window on the 60 Hz second display, consecutive-frame
       differences with ffmpeg):
       - at 30 fps every second recorded frame is an exact repeat;
       - at 60 fps none is, and the change per frame halves.
       - So the renderer moves the camera and characters every frame, not
         only at the 30 Hz physics tick.
       - The game's own stats: 60.0 fps, longest frame 18 ms, with and
         without the patches.
     - **Game-only cost** (`tools/mac_gpu_by_process.sh`, `out/mac_thread_cpu`,
       150 s each):

       | | Cores | CPU power | GPU share |
       | --- | --- | --- | --- |
       | 30 fps | 0.78 | 3.03 W | 24% |
       | 60 fps | 1.24 | 4.26 W | n/a (tool bug) |
       | 60 fps + tick patches | ~1.47 | ~4.75 W | n/a (tool bug) |

     - **Tick-rate patches**: LF tick 15 -> 30 Hz (menus, input), HF tick
       30 -> 60 Hz (physics).
       - Speed stays correct with them on.
       - Cost: about +0.5 W of CPU at 60 fps.
       - One patched run (`fable_2_466`) ended in an endless guest access
         violation (read of 0x24, guest thread 0xF800002C) after the quit
         command. A 280 s patched run with a watchdog (`fable_2_468`) did
         not repeat it. Cause unknown; the loop itself is fixed (item 46).
     - **Display pacing.** The main display runs at 144 Hz, so 60 fps frames
       stay on screen for 2 or 3 refreshes alternately (and 30 fps for 4 or
       5), which looks like judder. At 120 Hz both divide evenly.
       - The guest vblank is a free-running timer at
         `video_mode_refresh_rate`, not tied to the display.
       - Next step for smoothness: drive the guest vblank from the
         display's refresh (CADisplayLink / CVDisplayLink), so frames line
         up with refreshes at any rate.
  46. **Done (2026-10-18): an unhandled fault no longer loops forever.**
     - Before: when no handler took a SIGSEGV/SIGBUS/SIGILL,
       `src/core/exception_handler_posix.cpp` returned from the signal
       handler. The instruction ran again, faulted again and logged again:
       `fable_2_466` wrote 2.3 GB of "Unhandled guest access violation"
       lines in about 90 s (rotated into 21 files) and did not quit.
     - Now the handler logs `Unhandled signal N at pc ..., lr ...; passing it
       on` once and puts back the handler that was installed before it (or
       the default action). The next fault then ends the process with a
       macOS crash report.
     - Windows already crashed in this case (EXCEPTION_CONTINUE_SEARCH).
     - Also fixed `tools/mac_gpu_by_process.sh`: each registry entry lists
       its usage before its process name, so the script credited each
       process with the next one's GPU time. It also counted only the first
       queue.
  47. **Done (2026-10-18): even frame pacing** (`docs/macos_port.md`,
     "Frame pacing: idle repaints and the display refresh").
     - **Measuring:** `--vulkan_present_pacing_log=10` logs, from
       VK_GOOGLE_display_timing (Metal's `presentedTime`):
       - how many refreshes each guest frame stayed on screen;
       - how far apart presents are.
     - **Idle repaints (the main cause).** The achievement toast dialog was
       registered with the ImGui drawer for the whole session, so the
       presenter repainted the same guest frame on the UI thread about 125
       times a second (1200 presents per 10 s at 30 fps).
       - At 60 fps that showed 13-33 frames per 10 s for twice as long
         (stutter).
       - Now the dialog registers only while a toast is queued.
       - Presents now equal guest frames:
         - 30 fps: 300 of 300 frames on screen for exactly 33.3 ms;
         - 60 fps: no doubled frames, about 1 ms of jitter.
       - Game GPU share at 30 fps 24.3% -> 18.5%, cores 0.78 -> 0.72.
     - **The main display (nominally 144 Hz) is variable refresh.** At 30 fps
       frames stay up exactly 33.3 ms, not a 27.8/34.7 ms mix. So the
       earlier "144 Hz judder" (item 45) was wrong: the free-running timer
       is already even there.
     - **Fixed-refresh displays: `vsync_display_link`** (on; restart).
       - When the refresh of the window's display is a whole multiple of
         `video_mode_refresh_rate` or the reverse (60 or 120 Hz displays),
         the guest vblank follows that display's CVDisplayLink:
         - every Nth refresh;
         - or N per refresh (60 fps on a 60 Hz display: 2 per refresh);
         - `vsync_display_link_phase` of a refresh after the callback
           (0.5).
       - Other rates keep the timer, so the frame rate never changes. A
         display that stops refreshing falls back to the timer. The choice
         is made again when the window moves between displays.
       - On the 60 Hz second display, at 30 fps: timer 0-17 frames per 10 s
         as short/long pairs; link at phase 0 34-38 (the flip lands on the
         display's deadline); link at phase 0.5 or 0.25: 0-2.
       - At 60 fps on that display: 598-601 of 600 frames on screen for one
         refresh.
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
  used to snapshot it). **Confirmed zero-copy's (2026-10-14):** the same
  test without it (`fable_2_083.log`) has all 15 shots within 1.1-1.8% of
  shot 00 (snowflakes); with it, shots 02 and 12 are 12-15% off (scored
  with the same script; zero-copy shots kept in `out/flicker_zero_copy/`).
  Likely mechanism: the guest reuses memory once it sees the GPU's
  completion (fence/event writes), which the command processor reports
  when it records the draws, before the host GPU runs them.
  **Racing data found (2026-10-14, `--vulkan_zero_copy_race_log=true`,
  `fable_2_085.log`, gameplay at the trail, capped):** every 5 s, the
  CPU rewrites the bytes pending draws read in a handful of pages, all
  vertex data except one texture:
  - 0x1A568000 and 0x1A5E8000 (vertex fetches from about 0x1A56AE00 and
    0x1A5EAE00): ~4.5-5.4 KB changed, every window. Looks like a
    double-buffered dynamic vertex buffer refilled per frame.
  - 0x1F434000 (vertex fetch at 0x1F437000): 20-30 bytes.
  - 0x1FAA0000 (texture): ~1.5 KB, most windows.
  On the console the game reuses these only after the GPU's fence, which
  the emulator writes as soon as the draws are recorded
  (`ExecutePacketType3_EVENT_WRITE_SHD` stores the value immediately).
  xenia-edge has the same design (zero-copy default on ARM64) and the same
  gap. Fix options: (1) hold guest-visible GPU progress (EVENT_WRITE_SHD,
  MEM_WRITE, INTERRUPT, the write half of WAIT_REG_MEM, scratch writeback)
  until the host submission completes, first as a test that submits and
  waits there; (2) on a CPU write fault to a page a pending submission
  reads (what the detector sees), make the writer wait for that
  submission. One load with the detector on (`fable_2_084.log`) crashed
  with a guest call to address 0 in the hero/dog texture builder thread;
  the rerun loaded fine (intermittent; zero-copy lets GPU resolves write
  guest memory directly, which copy mode never does).
  **Writers (`fable_2_087.log`, the race log names the faulting host
  code):** both big vertex buffers are filled by guest 0x822BEAA0 (a
  large CPU vertex generator, unrolled by 4; called via 0x821C3CD0 >
  0x821C36E0 > 0x821BA2D0 > 0x822A9E60), the small vertex range by
  sub_82CD3C50 (via 0x82C44F50 ... 0x82C08310), an index page by
  sub_822964F8.
  **Fence test confirms the cause (`vulkan_gpu_progress_wait`, live):**
  submitting and waiting for the host GPU before every guest-visible
  progress write (EVENT_WRITE_SHD, MEM_WRITE, COND_WRITE to memory,
  scratch writeback, INTERRUPT) leaves no changed bytes in the race log
  and a clean flicker test (`fable_2_088.log`: all 15 shots 1.2-2.0% off
  shot 00, `tools/shot_diff.py`). But it waits ~26 times per frame and
  costs more than zero-copy gains: **11.2 fps** with it vs. 17.4 without
  (zero-copy, uncapped, `fable_2_089.log`; copy mode ~13.4).
  **Deferred progress (`vulkan_defer_gpu_progress`, live; 2026-10-14):**
  those writes and interrupts queue, in order, until the submission with
  the commands before them completes (applied between packets, at swaps,
  and while the command processor idles or polls in WAIT_REG_MEM, which
  also submits what they wait for; COND_WRITE flushes before reading
  memory; `vulkan_defer_gpu_progress_submit` also submits at each
  delayed write). Correct: no race, clean flicker test (`fable_2_090.log`,
  shots 1.5-2.1%), no deadlock, nothing let through early. But **12.2
  fps** (11.7 with the early submit; zero-copy alone 17.3, copy mode 12.8
  on the same build, `fable_2_091`-`096`). The command processor then
  idles ~48% of the time behind two alternating writes, 0x1FC83004 and
  0x1FC84004 (a per-frame fence the game waits on every frame).
  **Why no correct zero-copy is faster here:** at the racing writes the
  GPU is 1-4 submissions behind (`fable_2_095.log`, uncapped); making
  only those writers wait would mean ~1.5 waits per frame of ~80 ms on
  average. The game rewrites its dynamic vertex buffers every frame
  expecting the GPU to be about a frame behind at most; copy mode hides
  that by snapshotting, zero-copy can only keep the GPU that close, which
  loses the CPU/GPU overlap. Zero-copy pays only once the GPU is faster
  (or with a shader-side redirect of the racing vertex fetches to a
  snapshot buffer). So copy mode stays the default; GPU time (FSI
  specialization, NoContraction) is the next lever.
  The detector (`vulkan_zero_copy_race_log`, restart) works with or
  without zero-copy: per page it keeps the last submission that read it,
  what for (`SharedMemory::RequestKind`, set by each caller) and the used
  byte span; a CPU write fault to a page whose submission is pending saves
  the page first, and once the submission completes it compares the used
  bytes. Vertex buffers that skip RequestRange (same address and size as
  before, an upstream shortcut that lasts across frames) are noted with
  `NoteRangeUsed`.
- Done: pipeline store loading, thread QoS, async pipeline waits. Tried and
  dropped: upload hoisting (`vulkan_hoist_shared_memory_uploads`; no
  measurable gain, causes flicker).
- Ideas not done: profile which passes cost most (Instruments with the
  frame report's pass list), the host render target path (`fbo`) for
  comparison, fewer FSI restarts, `game_msaa` effects on render targets that
  stay 4x.

## Upstream (checked 2026-10-14)

ReXGlue does not take GPU emulation changes upstream (#456 closed as not
planned; `development` has no Vulkan or shared memory fixes), so GPU fixes
come from xenia-edge, xenia-canary or open PRs. Still worth porting, most
useful first (taken: the two bugs in the fixed list):
- xenia-edge 3ec3e8a05 `spirv_moltenvk_allow_contraction`: SPIRV-Cross
  turns NoContraction ops into `optnone` helpers; our position,
  interpolator and quad patch code use them. Check the MSL and depth
  prepass equality first (`tools/sdk_offline`).
- AArch64 memory ordering in codegen (`development` bd833a2a fences for
  sync/lwsync/eieio, 7f7c92ef stwcx. as a compare-and-swap, issue #462);
  needs `generated/` regenerated.
- xenia-edge 8d75a1779 / 830e60518: FSI pixel shaders specialized for the
  render target formats, blending and sample count. **Done our way and
  measured, no gain** (`vulkan_fsi_specialize`, restart, off): the
  pixel modification carries each written target's format, "guest MSAA
  is 1x" and "nothing blends" (18 bits: the 15 free ones plus
  depth_stencil_mode's, which only the host render target path uses;
  modification version 13), and the translator emits them as constants
  so Metal's compiler drops the other formats, samples and the blending
  path. Shaders are specialized (dumps under `--dump_shaders`), the image
  is unchanged (1.8% vs. unspecialized, snow), but the trail stays at
  12.6-13.0 fps (12.7-12.8 without; `fable_2_097`/`098`). So FSI
  branching is not what costs GPU time there; likely the EDRAM buffer
  traffic itself and the ~268 render pass restarts per frame (removing
  most restarts is what gave zero-copy its +30%).
- PR #501: WaitMultiple sleeps instead of polling (audio worker 75% of a
  core to 2%).
- PR #490: texture `exp_adjust` read from the wrong fetch constant word
  (`spirv_translator_fetch.cpp`).
- MoltenVK 1.4.1 to 1.4.3 (`development` e9d44256).
Nothing upstream for render passes ended by uploads or for MoltenVK's
adaptive tessellation cost.

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

- Adaptive quad patches drawn as triangle lists like the continuous ones
  (`vulkan_adaptive_quad_patches_in_vs`): no MoltenVK tessellation left at
  the trail, +16% fps.
- Upload hoisting flicker: vertex buffers that skip their request now mark
  their pages used in the submission, so no hoisted upload overtakes their
  reads; hoisting is clean in two flicker tests and +38% fps, and on by
  default on macOS.
- Two upstream bugs (found by checking xenia-edge and the ReXGlue
  issues): the presenter never recorded the format of its final effect
  pipeline, so every paint waited for the previous one and recreated it
  (rexglue #507); the index buffer cache dropped an entry overlapped by a
  CPU write only if the entry ended past the write (xenia-edge 1b0e9d00e).
  Not yet measured.
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
