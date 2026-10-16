# macOS port

**Status:** builds and runs on an M4 Mac mini (macOS, full Xcode, SDK
v0.10.0 built from source with `thirdparty/sdk_mac_vulkan_fixes.patch`).
With FSI render targets the game renders and plays. Current state, open
problems with the evidence so far, and next steps:
`plans/macos-port-handoff.md` (this document explains why each change
exists).

## Prior work

- The only public Mac attempt is by AARosson48 (M4, Sept 2026), reported in
  [himdo/Fable-2-Recomp#9](https://github.com/himdo/Fable-2-Recomp/issues/9),
  [Fable2Recomp/Fable2Recomp#23](https://github.com/Fable2Recomp/Fable2Recomp/issues/23)
  and [rexglue/rexglue-sdk#446](https://github.com/rexglue/rexglue-sdk/issues/446).
  They got intro menus and loading working after reworking the keyboard
  module; terrain rendered see-through. Their code is not public, and himdo#9
  is labeled "Not Planned For a While".
- Other Fable II recomps (Sept 2026), none with macOS support:
  [Fable2Recomp/Fable2Recomp](https://github.com/Fable2Recomp/Fable2Recomp),
  [FenrisSkoll/Fable2Recomp](https://github.com/FenrisSkoll/Fable2Recomp)
  (Windows/D3D12 only, needs its own SDK fork),
  [TheSaltTrader/A-Legend-Never-dies---Fable-2-Recompilation](https://github.com/TheSaltTrader/A-Legend-Never-dies---Fable-2-Recompilation)
  (Windows; fixed a ~3.5 min freeze by switching to ROV render targets, the
  D3D12 counterpart of FSI) and
  [Oery/fable-ii-recomp](https://github.com/Oery/fable-ii-recomp) (Linux).
- Other ReXGlue games that run on macOS, each on its own SDK fork:
  [skate3recomp](https://github.com/mchughalex/skate3recomp),
  [MCX360-EDITION](https://github.com/DevZer0D4Y/MCX360-EDITION) and
  [RaymanOriginsRecomp](https://github.com/BelmanteGu/RaymanOriginsRecomp/pull/23).
- ReXGlue SDK **v0.10.0** is the first release with macOS support (Vulkan on
  MoltenVK) and ships prebuilt `mac-arm64` / `mac-amd64` SDKs. This project is
  pinned to v0.10.0 (the source pin `f5337cd` is the `v0.10.0` tag).

## Building on a Mac

The port uses the SDK built from source with the Mac patch
(`tools/build_sdk_mac.sh && ./build.sh -release fable_2`, see "Building the
SDK from source on macOS"); that needs a full Xcode install (it builds
MoltenVK with xcodebuild). The prebuilt-SDK build below is the fallback: it
lacks the Mac patch (render fixes, debug switches). Requirements for it:
macOS 13.3+ (the prebuilt SDK's minimum), Xcode 15+ command line tools,
CMake >= 3.25, Ninja, and your own copy of the game content (see the
README).

```sh
xcode-select --install          # clang, if not already installed
brew install cmake ninja
./build.sh -release fable_2     # fetches the SDK, runs codegen, builds
tools/stage_content.sh . out/build/mac-arm64-release
out/build/mac-arm64-release/fable_2
```

- `tools/stage_content.sh` copies `default.xex`, `data/`, `nxeart/` and
  `$SystemUpdate/` next to the executable, only what changed. It merges
  `data/dir.manifest` instead of copying it: the build already writes one
  there listing only its own `scripts\recomp\*` files, which is newer than
  the game's, so a newer-wins copy left every game file unlisted (and the app
  then refused to start, "game data was not found or is incomplete").
- `tools/setup_sdk.sh` downloads the prebuilt SDK for the host
  (`mac-arm64`, `mac-amd64` or `linux-amd64`) into
  `thirdparty/rexglue-sdk-prebuilt/`.
  `REXGLUE_SDK_VERSION=<x.y.z>` selects a later release; versions before
  0.10.0 are refused. Nightlies need the tag too, for example
  `REXGLUE_SDK_VERSION=0.10.0.15-dev.g5cf287f REXGLUE_SDK_TAG=nightly-20260925-5cf287f4`
  (MoltenVK 1.4.3 instead of v0.10.0's 1.4.1; the host code compiles against
  it). **That nightly is not usable yet:** its codegen names the project's
  files `fable2_*` instead of `fable_2_*` (manifest, `fable2_pch.h`,
  `fable2_codegen`), which nothing in the project references, so `build.sh`
  stops with a message saying so. Using it would mean renaming the project
  (manifest `name`, `src/main.cpp`, the hotfunc includes, `CMakeLists.txt`).
  `thirdparty/rexglue-sdk` is the SDK *source* submodule upstream's Windows
  build uses; macOS/Linux use the prebuilt SDK only.
- `build.sh` mirrors `build.cmd` (`-release`/`-r`, target argument). On a
  fresh checkout it runs `rexglue codegen` once to create
  `generated/rexglue.cmake`, which `CMakeLists.txt` includes, and again when
  the installed SDK differs from the one recorded in
  `generated/.rexglue_sdk_version`. `CC`/`CXX` override the preset's
  compilers.
- The SDK's CMake helpers stage `librexruntime.dylib`, the GPU plugin, and
  MoltenVK + its ICD next to the executable, and the runtime points the Vulkan
  loader at them itself; no launcher script is needed.
- Vulkan is the only GPU backend on macOS; the default `xenos` plugin selects
  it automatically.

## What changed for non-Windows hosts

- **Keyboard/mouse input** (`src/input/keyboard_gamepad.h`): the driver
  polled Win32 (`GetAsyncKeyState`, cursor recentering), so off Windows the
  keyboard did nothing. It now also listens to the SDK window's key, mouse
  and focus events and uses SDL relative mouse mode for mouse look; when the
  pointer cannot be locked it recenters the cursor instead (and tries the
  lock again on the next capture). Cursor and pointer-lock calls run on the
  UI thread, and the driver detaches when the window closes. Same
  `keyboard_gamepad_map` / `mouse_look*` cvars, same F4/F5 behavior. The
  Windows path is unchanged.
- **Guest memory reads** (`src/diagnostics/guest_memory.h`): probes that run on
  their own threads hardcoded the guest arena at `0x100000000`. On 64-bit
  macOS that is where the executable is loaded, so the base now comes from the
  runtime, and off Windows reads go through `mach_vm_read_overwrite` (macOS) /
  `process_vm_readv` (Linux) so an unmapped page fails instead of crashing.
  Used by the always-on alloc-watch monitor and the Debug-build state
  classifier, which previously read unchecked memory on non-Windows hosts.
- **Compile fixes**: `TCP_NODELAY` header, a self-referential JSON type that
  only MSVC's STL accepted, and Windows-only code (SEH, `VirtualAlloc`,
  `CreateThread`) in env-gated diagnostic probes. Those probes are disabled
  off Windows, as the existing ones already were.
- `CMakePresets.json`: macOS presets target macOS 13.3, matching the SDK.

Verified in a Linux container against the prebuilt `linux-amd64` v0.10.0 SDK:
every host translation unit compiles in Debug and Release. That is the
closest check available without a Mac; it does not cover Apple clang/libc++
or anything at runtime.

## Graphics: the MSAA sample-count bug on MoltenVK (fixed)

Symptom: the top half of the frame corrupted or blown out, the bottom half
correct (the boundary matched the 384-row predicated-tiling split). Metal's
debug layer (`MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog`) reported
`The raster sample count (1) does not match the renderPipelineState's raster
sample count (4)` on tens of thousands of draws per minute.

Root cause: with the fragment shader interlock (`fsi`) path, EDRAM lives in
a storage buffer and draws run in an attachment-less render pass with the
guest's MSAA sample count on the pipeline. Metal needs the sample count of an
attachment-less pass explicitly (`defaultRasterSampleCount`); MoltenVK
learns it from the pipelines created against a `VkRenderPass` object, keeps
one value per pass (`MVKPipeline.mm` `setDefaultSampleCount`, default 1), and
has no pass object at all with dynamic rendering. The SDK used one FSI pass
for every sample count and dynamic rendering by default, so 4x draws were
rasterized at 1x.

Fix (both parts needed):

1. `thirdparty/sdk_mac_vulkan_fixes.patch`: one attachment-less FSI render
   pass per host sample count (1x, 2x, 4x); each pipeline is created against
   the pass of its sample count, and each draw enters the pass of the
   pipeline it binds. Pipelines that MoltenVK does not rasterize (rasterizer
   discard, or both faces culled with a triangle topology) always get a Metal
   sample count of 1, so they use the 1x pass. The command processor logs an
   `FSI stats` line every 300 frames (draws per frame, render pass restarts
   per frame, draws whose pipeline sample count differs from the guest MSAA
   mode).
2. `src/core/fable_2_app.h`: `REX_VULKAN_DYNAMIC_RENDERING=false` on macOS,
   so the pass objects exist.

Result on the M4: the scene renders correctly. The first version of the fix
(pipelines per pass, but draws still entering the pass of the guest MSAA
mode) left about 30k "(1) vs (4)" and 25k "(4) vs (1)" debug-layer messages
per run; the second half is the non-rasterizing pipelines above. The stats
line tells whether any pass/pipeline disagreement remains on the ReXGlue
side; a remaining count of Metal messages with zero mismatches there points
into MoltenVK (tessellation pipelines are the candidate).

What the SDK's Vulkan backend already handles on MoltenVK (from the v0.10.0
source):

- No geometry shaders: quad lists are converted to triangle lists and
  point/rectangle lists are expanded in the vertex shader
  (`vulkan_require_geometry_shader` defaults to false on macOS).
- `Metal does not support disabling primitive restart` (rexglue-sdk#446):
  MoltenVK warns whenever a strip is drawn with restart disabled. Metal
  always restarts on index `0xFFFF`/`0xFFFFFFFF`, which only matters if a
  game uses that value as a real vertex index. Noise.
- Apple GPUs have no `D24_UNORM_S8`; 24-bit depth is stored as
  `D32_SFLOAT_S8` (`GetDepthVulkanFormat`). Not the cause of the corruption
  (the `fsi` path keeps depth in the EDRAM buffer).

### Results on an M4 Mac mini

- Default render target path (`fbo`): at first audio played and the screen
  only flashed white; with the SDK patch it renders correctly.
- `render_target_path_vulkan=fsi`: everything renders once the sample-count
  fix is in. It was the macOS default until 2026-10-15; the app now
  defaults to `fbo` (`REX_RENDER_TARGET_PATH_VULKAN=fbo`, set in
  `Fable2App::OnPostInitLogging` unless already set), which needs about a
  third less GPU time per frame (trail 20.0 vs. 29.8 ms, uncapped). Pass
  `--render_target_path_vulkan=fsi` to compare.
- MoltenVK's own primitive-restart warning goes to stderr on every strip draw,
  so the app also defaults `MVK_CONFIG_LOG_LEVEL=1` (errors only) on macOS,
  and seeds `vulkan_log_debug_messages=false` (pass
  `--vulkan_log_debug_messages` for validation runs).
- The guest arena is mapped at `0x7000000000` on macOS (not `0x100000000`),
  confirming the probes must not hardcode the Windows base.
- Device limits worth watching: no geometry shaders,
  `maxPerStageDescriptorSamplers: 16`, no sparse binding (512 MB shared
  memory buffer).

### Pipeline store never loaded on macOS (fixed)

Every launch recompiled the same pipelines (a 150 to 500 ms stall each on
MoltenVK, where a pipeline is a full Metal shader compile), although the
on-disk store `cache/shaders/shareable/<title>.fsi.vk.xpso` existed. The SDK
opens the store and the guest shader file in `"a+b"` mode and reads the
header from the current position. glibc and MSVC start an append-plus stream
at the beginning; the BSD libc on macOS starts it at the end, so the header
read failed, the file was treated as new and truncated, and only the current
run's pipelines were ever saved. The patch seeks to the start after opening
both files. The stall pattern showed in the `FSI stats` line as a longest
frame of 150 to 500 ms per window with only a handful of frames over 40 ms.

### Thread scheduling on Apple Silicon (patched)

No runtime thread had a QoS class, so macOS could schedule the guest
threads, the GPU worker, the vblank timer and audio on the efficiency cores
under load, and the emulated vblank was a 1 ms polling loop whose wakeups
are late by several milliseconds there. The patch gives every runtime
thread `QOS_CLASS_USER_INTERACTIVE`, puts the vblank thread under a Mach
time-constraint policy and sleeps it until just before the next vblank is
due, converting the remaining guest ticks with the guest time scalar.
(The class is set for every runtime thread, guest threads included: the
game's main logic thread is as latency-sensitive as the GPU thread. A
guest `SetPriority` that ends in `pthread_setschedparam` takes that thread
out of the QoS system.)

The presenter picks Vulkan immediate mode when the driver offers it, and
MoltenVK does (`presentation mode 0` in the log): frames are shown as soon
as they are ready, unrelated to the display refresh. The app now seeds
`vulkan_allow_present_mode_immediate`, `_mailbox` and `_fifo_relaxed` to
false on macOS, so the presenter uses plain vsync (`presentation mode 2`),
which measured smoother. Any of them in `fable_2.toml` or on the command line
wins.

### Readback defaults on macOS (changed)

Two SDK features copy GPU results back to guest memory, and both drained
the whole GPU queue on MoltenVK:

- **Memexport readback** (`readback_memexport`, on by default in the SDK).
  Every draw whose shader writes memory through memexport copies the result
  back. The double-buffered fast path falls back to a full queue drain for
  any new address or a second draw to the same address in one frame; in busy
  scenes that was hundreds of draws and 200 to 500 ms frames ("GPU waits" in
  the long-frame log). The fast path also writes the previous frame's
  results into guest memory. Xenia runs Fable II without it. The app now
  seeds `readback_memexport = false` on macOS.
- **Resolve readback** (`vulkan_readback_resolve = true` means the "fast"
  mode: every resolve, one frame late). Upstream now fixes the black
  hero/dog textures without any readback (`[patches]
  realtime_texture_morphing`, on by default). The older fix,
  `[patches] hero_dog_texture_readback` (off by default), needs only one
  resolve read back: the SDK patch adds `readback_resolve_force_addresses`,
  and the app seeds it with `0x12704000:color:fmt=8_8_8_8:blit` on macOS.
  Each entry is an address (matching destinations in the next 64 KiB) with
  optional qualifiers that must all hold: `color`/`depth`, `fmt=<format>`
  (the `_A` and `_AS_16_16_16_16` variants of a format match it), `blit`
  (a `PM4_DRAW_INDX_2` 3-index rectangle-list copy, how Direct3D 9
  resolves) and `span=<bytes>`. A resolve inside an entry's window that
  its qualifiers reject is logged (`Resolve to 0x... not read back`, first
  16 times), so a filter that is too strict shows up in the log.

The Vulkan aliases `vulkan_readback_resolve` and `vulkan_readback_memexport`
override the shared cvars whenever they differ from their default (false),
so `true` in an old `fable_2.toml` brings the full cost back; the app logs a
warning when it sees them. Stale one-frame-late data from the fast paths is
also the prime suspect for flickering shadows (a shadow map resolved one
frame late) and broken polygons on skinned characters.

### Long-frame log

With `--vulkan_long_frame_log` (off by default, hot-reloadable; it adds a
few clock reads per draw) the SDK patch logs every frame over 100 ms as
`Long frame N ms: draws, pipelines, textures, shared memory, resolves, GPU
waits, GPU-bound frame throttle, submit, completed submissions, swap, idle
waiting for guest commands, WAIT_REG_MEM, other`. Each millisecond is
counted once, in the innermost area (a resolve inside a draw counts as
resolves, a fence wait inside a resolve as GPU waits), so the areas add up
to the frame. "GPU-bound frame throttle" is the wait before opening a frame
for the GPU to finish the frame two back: the GPU is the bottleneck. "GPU
waits" are the other blocking fence waits (readbacks, resource reuse).
"Pipelines" includes waiting for pipelines that are still being created.
"Idle waiting for guest commands" means the game had not submitted the
next work, so the time went to the guest CPU side. The `FSI stats` line
every 300 frames (fps, longest frame, frames over 40 ms) is always on.

### Capturing a frame

Instead of one flag per run, one capture gives the shaders Metal actually ran
and every draw of one frame, which can then be analyzed offline:

- `MVK_CONFIG_SHADER_DUMP_DIR=<dir>` (MoltenVK) writes the SPIR-V and the
  converted Metal source of every shader it compiles.
- `--dump_shaders=<dir>` (SDK) writes the guest ucode and, with the patch,
  the translated SPIR-V named by guest shader hash, so the Metal files can be
  matched to the game's shaders.
- `vulkan_frame_report` (SDK patch): changing its value logs every draw and
  resolve of the next frame as `[frame]` lines (shaders, render targets with
  formats and blend state, guest MSAA, depth, alpha test and alpha to
  coverage, and every texture the shaders sample: guest address, format,
  size, tiling, signedness, swizzle, endianness, mip levels with packed
  mips, filters and clamping). The app changes it on F9 (fn+F9 on a Mac
  keyboard).
- `tools/analyze_mac_capture.py` summarizes both: which EDRAM (FSI) fragment
  shaders got Metal's raster order group (without it, overlapping fragments
  race), whether vertex positions are invariant, the frame's draws grouped
  by pixel shader and state, the textures sampled most, and for every
  resolve the later draws that sample what it wrote, with the format they
  read it as (how shadow maps and other render-to-texture results are used).
  `--find <guest shader hash>` lists one shader's draws and textures.

`tools/mac_capture.sh` does the whole run: the game with both dumps and
`vulkan_frame_report_interval=20` (SDK patch: a frame report every 20
seconds by itself, so the capture does not depend on F9 reaching the game),
then the summary of the last complete report (copied to the clipboard).
The SDK's log rotates at `log_max_file_size_mb` (5 MB by default: the full
file becomes `fable_2_NNN.1.log`, and so on), and a frame report with
textures is a few MB, so the analyzer reads a log together with its rotated
parts, and the capture script raises the limit to 100 MB. To analyze the
last capture again without running the game:
`python3 tools/analyze_mac_capture.py --mvk out/capture/mvk --rex out/capture/rex`
(`--report N` picks another of the log's reports). Besides the groups, the
summary checks every mesh drawn depth-only and again with color on the same
surface (Fable II's depth prepass, base pass and greater-or-equal lighting
passes) for differences between the passes that can leave lines along
triangle edges (vertex shader, MSAA, polygon offset, culling), and lists the
blended draws without depth write and the non-triangle primitives
(particles such as snow) with their blend, alpha state and textures.

### Live A/B switches

Hot-reloadable debug switches (SDK patch) that the app flips on keys (the
number keys 6 / 7 / 8 / 9 or fn+F6 ... fn+F9: without fn, a Mac keyboard's
F keys are system and media keys that macOS does not pass to the game), so
one run compares the picture with and without something while standing
still (each press logs `[input] F6: <cvar> = true/false`):

| Key (fn+) | cvar | Effect |
|---|---|---|
| F6 | `vulkan_debug_fsi_whole_quads` | Runs FSI pixel shaders for every 2x2 quad, also those where no pixel passed the early depth / stencil test. The skip now uses subgroup quad operations (`vulkan_fsi_quad_skip_quad_ops`); with the old derivative check, this switch removed the edge lines and wrong mips, which is how that bug was found. |
| F7 | `vulkan_debug_force_mip` | Cycles the mip level every texture is sampled at: off, 0, 1, ... 6, off (`vulkan_debug_base_mip_only` is level 0 only). Forcing each level showed the mip data is fine; the wrong levels came from the quad skip's derivatives (see "FSI early quad skip with quad operations"). |
| F8 | `vulkan_debug_skip_ps_group` | Cycles through the groups of `vulkan_debug_skip_ps` (guest pixel shader hashes, comma-separated, groups separated by `;`), skipping the draws of one group at a time, then none. Each press logs the group. |

**Debug sweep (key 0 or fn+F10).** Judging a setting live is hard, so the
sweep steps through `debug_sweep_steps` (app cvar:
`label:cvar=value,...;label:...`, each step applied on top of the previous;
by default baseline, mip levels 0 to 5, whole quads, without the shadow
shader, without the lighting passes), waits `debug_sweep_settle_ms` (1500)
after each step, saves the game's own frame as `NN_label.png` in
`debug_sweep_dir` (the capture script uses `out/capture/sweep`) and then
restores every cvar it touched. It uses the same guest output readback as
the debug-build remote control `screenshot` command, without opening any
port. `tools/mac_sweep_sheet.py` (run by the capture script, which opens the
result) tiles them into one numbered `sheet.png` at half size and prints how
much each step differs from the baseline.

The analyzer lists the key presses the game logged, and the draws whose
polygons the guest draws as lines or points (`PA_SU_SC_MODE_CNTL` dual
polygon mode, now in the frame report as `polymode`).

`vulkan_debug_skip_depth_only_draws` (no key) skips the depth prepass, but
Fable II's prepass also writes the stencil the later passes test, so the
picture turns white (the bare color buffer) - which is also what the edge
lines are: pixels the base pass did not draw.

### Vertex position snapping (tried for the white edge lines)

Fable II draws the depth prepass with a position-only vertex shader and the
base and lighting passes (greater-or-equal depth test, stencil test against
the prepass) with other vertex shaders (401 of 404 meshes at the brazier).
Their positions differ in the last bits. The Xbox 360 rasterizer converts
screen positions to fixed point with 1/16 pixel precision (PA_SU_VTX_CNTL),
which absorbs that; host GPUs use a finer grid (about 1/256 pixel), so
pixels whose centers lie between the two slightly different edges are
covered by the prepass but not by the base pass, or the other way around,
and the base pass leaves them unpainted: white dashed lines along triangle
edges that flicker with the slightest motion. A `vulkan_snap_vertex_positions`
switch snapped the screen position of every vertex to the 1/16 pixel grid
in the translated vertex shader (plus 1/512 pixel so the host's own
quantization rounds it the same way everywhere). It did not remove the
lines (their cause was the quad skip, below) and added work to every vertex
shader even when off, so it was removed again. SPIRV-Cross keeps the translator's no-contraction arithmetic
exact in MSL (`fma(a, b, 0)` in `optnone` helpers), so the remaining
difference between two shaders is the guest's own math.

MoltenVK compiles shaders with Metal's fast math unless a shader asks for
strict floating point. `MVK_CONFIG_FAST_MATH_ENABLED=0` (environment) turns
it off for a run, to check whether two vertex shaders computing the same
position (Fable II's depth prepass and color passes use different ones)
round it differently.

### Raster order groups for every EDRAM shader

MoltenVK's SPIRV-Cross (and its upstream main) gives a fragment shader's
interlock section a Metal raster order group by finding the storage buffers
accessed between the begin and the end. When the begin is inside control
flow of the entry point, the analysis marks nothing, so `xe_edram` got no
raster order group and overlapping fragments raced (1 of 118 FSI shaders at
the brazier load point). It can happen when the shader tests depth late
(it writes depth or exports to memory) and has ucode jumps, loops or calls,
since the main loop's exits then hide the begin's position. With
`vulkan_fsi_end_interlock_in_function` (SDK patch, on by default on macOS,
needs a restart) the translator ends the section in a separate function,
which makes SPIRV-Cross take its conservative path and put every storage
buffer the shader accesses in the group. The begin and the end compile to
nothing in MSL. Checked offline with SPIRV-Cross at MoltenVK's commit on a
hand-built shader of that kind: no raster order group without the option,
`raster_order_group(0)` on `xe_edram` with it, and spirv-val accepts both.

### FSI early quad skip with quad operations (wrong mips, edge lines)

With early depth / stencil, an FSI pixel shader skips its body when no pixel
of its 2x2 quad passed the test. Xenia decides this with derivatives of
`passed ? 1 : 0` (`OpDPdxFine`, then `OpDPdyCoarse`). SPIRV-Cross emits plain
`dfdx` / `dfdy` for every derivative op, and on Apple GPUs these act like
coarse derivatives. So in a quad where, say, only the bottom-right pixel
passed, both derivatives are 0, the other three lanes skip, and that pixel
runs alone. Its texture derivatives (mip selection) are then garbage.

The debug sweep (2026-10-12) showed it: with the whole-quads switch
(`vulkan_debug_fsi_whole_quads`), the snowflakes became round dots instead of
framed squares, the hero's face looked right, and the white dashed lines
along triangle edges were gone. Forcing mip 0 fixed the first two but not
the lines. Forcing mips 1-5 showed blurrier but sane textures, so the mip
data itself is fine.

`vulkan_fsi_quad_skip_quad_ops` (SDK patch, on by default, needs a restart)
makes the decision with two `OpGroupNonUniformQuadSwap`s over the sample mask
(MSL `quad_shuffle_xor`) when the device reports quad operations in fragment
shaders (`VkPhysicalDeviceSubgroupProperties`, logged as "subgroup quad
operations in fragment shaders"). The decision is then exactly the same for
all four lanes. The derivative path stays as the fallback. Checked offline
with `tools/sdk_offline/fsitest.cpp` (`ps_color_{deriv,quad}.spv`): spirv-val
accepts both, and SPIRV-Cross emits `quad_shuffle_xor(value, 1)` and `(value, 2)`. In the
game (M4, MoltenVK reports subgroup size 32 with quad operations in fragment
shaders) the snowflakes, the hero and the edges look right, and the
whole-quads switch changes only 1.9% of pixels.

### Shared memory and texture load ordering (flickering data)

Dark shadow areas flicker between frames while standing still (flicker
test in the handoff), so some input of the draws differs between frames.
A review of the patch against upstream Xenia and xenia-edge found three
ordering holes:

- **Upload hoisting** (`vulkan_hoist_shared_memory_uploads`, opt-in) records
  shared memory uploads in a separate list that runs at the start of the
  submission, so a mid-frame upload does not end the render pass. Without
  further care that list could hold two copies of the same page with no
  barrier between them (undefined order, so the older copy can win and the
  page stays stale until the CPU writes it again), and an upload could
  overtake a read, a resolve or a memexport recorded before it in the
  submission. Now `VulkanSharedMemory` keeps a bitmap of the pages used in
  the current submission (requested by draws through a new
  `SharedMemory::OnRangesRequested` hook, written through `Use()`, or
  uploaded); an upload is hoisted only if none of its pages were used
  earlier in the submission, otherwise it goes into the command stream as
  upstream does.
- **Texture validity after the request** (xenia-edge 9781a75a). The texture
  cache requests the shared memory of all of a draw's textures first, then
  loads and watches each texture. If the CPU writes a texture's memory in
  between, the texture was marked up to date and watched, with data that
  was already stale, and nothing would reload it until the next CPU write.
  `MakeUpToDateAndWatch` now checks `SharedMemory::IsRangeValid` under the
  global lock and leaves the texture outdated (reloaded on the next use).
- **Same image loaded twice in a submission** (xenia-edge 3087c4c5,
  c8f214e2): the second copy into the image had no barrier after the first
  (both in `TRANSFER_DST`, so the usage did not change); now a
  transfer-write to transfer-write barrier orders them.

With all three fixed, hoisting still flickers and turning it off does not,
while the resolves the shadow draws sample barely change between frames
(dumps of 24 consecutive frames). So hoisting still moves some upload ahead
of something that needed the old data, most likely vertex or heightmap
data the ground draws read from shared memory. It brought no measurable
speed either (heavy scenes 12-14 fps both ways), so it stays opt-in and
the tools no longer pass it. The fixes stay: they are correct without
hoisting too.

### Ported from himdo/rexglue-sdk

Upstream's Windows build moved to the himdo/rexglue-sdk fork. Its fixes
that matter on macOS are in the SDK patch:

- `async_pipeline_wait` (default on), implemented for Vulkan: a draw whose
  pipeline is still being compiled in the background waits for it (helping
  with the creation queue) instead of being skipped. Skipped draws left
  objects missing for a frame and corrupted textures rendered by the skipped
  pass (noise, streaks, flickering shadows when new effects first appear),
  and with `vulkan_async_skip_incomplete_frames` the frame was not shown at
  all. `async_pipeline_wait_timeout_ms` (default 5000) bounds one wait. With
  the wait on, new pipelines are now created right away instead: the
  placeholder was one more full Metal compile, and waiting on the creation
  queue could build other pipelines first and time out (dropping the draw).
- `depth_bias_shader` / `depth_bias_shader_units` for Vulkan's host render
  target (`fbo`) path, so `[patches] dog_fur_depth_bias` takes effect there.
  The FSI path stores the guest's 24-bit depth and needs no bias.
- The input system lock (a crash when spamming buttons on a SpinnerBox),
  `readback_resolve_half_pixel_offset` on by default, and
  `readback_memexport_fast_wait`.

### Quad patches without host tessellation

MoltenVK has no tessellation in Metal's render pipeline order: for every
tessellated draw it ends the render encoder, runs the vertex and
tessellation control stages as a compute pass ("vkCmdDraw (vertex and tess
control stages) ComputeEncoder" in a Metal System Trace), and starts a new
render encoder. Fable II draws its ground as quad patches, 258 draws per
frame at the trail, so this split the frame into hundreds of encoders with
the GPU idle between them.

The non-adaptive ones (continuous, factor 16 = `VGT_HOS_MAX_TESS_LEVEL` + 1,
one patch per draw: the ground lighting passes) produce a uniform grid: a
quad domain with all factors an even integer N under fractional-even
spacing (or any integer under equal spacing) is N x N equal cells. The
primitive processor now turns such draws into
`HostVertexShaderType::kQuadPatchAsTriangleList`: a non-indexed triangle
list of N * N * 6 vertices per patch, with the guest index (the patch
index) loaded in the vertex shader like for point sprite expansion
(`kHostBuiltinForDMA`, no host index buffer). The translated guest domain
shader runs as a vertex shader with the same registers as
`kQuadDomainPatchIndexed` (r0.x patch index, r0.yz domain location, r1.x
0), the location computed from the vertex index, the factor read from the
system constants (`tessellation_factor_range_max`, now also in the SPIR-V
system constant block). Adaptive draws still tessellate.

The cell diagonals must be the tessellator's: the lighting passes draw over
the depth of the adaptive (still tessellated) depth pass with a "greater or
equal" test, and the two triangulations of a non-planar cell lie wholly
above or below each other, so a wrong diagonal fails whole cells (the
ground turned into blue and white squares: blue is the ground with no
light added). `vulkan_quad_patch_diagonals` picks the pattern, live; a
sweep of all four against the tessellator found pattern 2, (0,0)-(1,1) in
the top-left and bottom-right quarters of the patch and (1,0)-(0,1) in the
other two (1.8% of pixels differ from the tessellator, the same as two
tessellated shots: falling snow). Both switches are live, for A/B sweeps.
`tools/sdk_offline/vstest.cpp` translates a shader in this mode.

Result at the trail, one run: 10.8 fps tessellated, 13.1-13.7 fps with the
grid. Related: where the adaptive depth pass uses a lower factor than the
lighting passes, their surfaces differ in the same way, which may be what
the original flickering dark squares on the ground were (not verified).

**Adaptive patches too** (`vulkan_adaptive_quad_patches_in_vs`, live, on
with `vulkan_quad_patches_in_vs`): the 44 adaptive draws per frame (depth
prepass and base color; 4 edge factors per patch in the index buffer, 2-25
patches per draw) are drawn the same way at the maximum factor. That is
what the host tessellation path already did: its adaptive vertex shader
(`GetTessellationAdaptiveVertexShaderGlsl`) converts the factor with
`float(value)`, the integer value of the float's bits, where Xenia's
`asfloat` reinterprets them, so every non-zero factor clamps to the maximum
(16). The patch index is the patch number in the draw plus the base index,
masked and clamped, as in the adaptive control shader, so these draws are
handled as auto-indexed (the factors are not read). Screenshots with and
without it differ by 1.6%, like two shots with the same setting (snow);
at the trail 19.4 fps vs. 16.8 (`fable_2_108`, `fable_2_109`). Every ground
pass now builds the same grid.

**Indexed grid (`vulkan_quad_patches_indexed`, default on, restart).** The
triangle list above runs the vertex shader 6 times per grid cell: at factor
16, 1734 invocations per patch for 324 distinct grid points. Instead each
patch is now one instance of an indexed draw: a 16-bit index buffer per
grid size and diagonal mode (`vulkan_quad_patch_diagonals`, cached in
`GetQuadPatchIndexBuffer`) holds the cells' triangles over the grid points
`y * (N + 1) + x`, the vertex shader takes the grid point from
`gl_VertexIndex` and the patch from `gl_InstanceIndex`
(`Features::quad_patch_indexed_grid`), and the GPU can reuse shaded grid
points. Same positions as before (the domain coordinates are computed with
the same operations). On `fbo`, uncapped: trail 18.9 ms per frame vs. 20.0,
heavy view 24.9 vs. 26.5 (`fable_2_163`, `fable_2_162`).

### Point sprites as host points

Without geometry shaders the SDK expands every point of a point list into a
4-vertex strip in the vertex shader (`kPointListAsTriangleStrip`), so the
guest vertex shader runs 4 times per point. Fable II's snow (23 draws of
4096 points per frame, a 581-instruction procedural vertex shader) then
costs ~2 ms per frame on `fbo`. With `vulkan_native_point_sprites` (default
on, restart; only without geometry shaders and with `largePoints`; Apple:
1-511 pixels) point lists stay host point lists: the vertex shader writes
`gl_PointSize` (the guest diameter from the vertex shader if it wrote one,
otherwise `PA_SU_POINT_SIZE`'s width, times the resolution scale, clamped
to the host range), the pipeline has point topology and no geometry
shader, and pixel shaders with point sprite coordinates (PsParamGen) read
`gl_PointCoord`. Differences from the expansion: host points are square
(guest points with a different width and height use the width), and the
host may drop a point whose center leaves the clip volume; user clip
planes are not re-evaluated per corner. `--vulkan_native_point_sprites=false`
(or `vulkan_force_expand_point_sprites_in_vs`) restores the expansion.
Uncapped on `fbo`: trail 17.2 ms per frame vs. 19.5, heavy view 23.6 vs.
24.6 (`fable_2_164`, `fable_2_165`); flicker test clean, 2.0% of pixels
differ from the expansion (snow), like two runs of either.

### Shared memory barriers between draws

`VulkanSharedMemory::Use` inserted a buffer barrier after every draw with
memory export (and on every switch between reading and exporting draws).
A pipeline barrier between draws ends the render pass on MoltenVK. Fable
II has ~26 export draws per frame, mostly back to back, each writing its
own range. `vulkan_skip_disjoint_draw_barriers` (default on, live):
`UseForDraw` takes the draw's read ranges (vertex fetch constants, guest
DMA indices, bounded by the larger vertex count at 32 bits) and its export
extent, keeps the reads and writes of draws since the last barrier, and
inserts a barrier (whole buffer, source stages of both draw usages) only
on an overlap: read or write after a draw's write, write after a draw's
read. Unknown reads (compute, transfer, the guest DMA index copy, pixel
shader vertex fetch) and non-draw writes keep the old behavior. 28 fewer
render pass ends per frame, 19.8 fps vs. 19.4 at the trail, same picture.

### Floating-point contraction

The translator decorates nearly every float operation NoContraction so that
passes computing the same values in different shaders get bit-identical
results (with the Invariant outputs). SPIRV-Cross has no MSL equivalent and
wraps each such operation in a helper function marked
`[[clang::optnone]]` (`spvFMul`, `spvFAdd`, ...), which Metal's compiler
can neither optimize nor inline: hundreds to thousands of calls per shader
(511 in the snow's vertex shader, 495 in the main geometry pixel shader).
`spirv_moltenvk_allow_contraction` (default on, restart; from xenia-edge
3ec3e8a05) omits the decoration on MoltenVK. Invariance between passes
comes from the Invariant position output instead: MoltenVK compiles such
shaders with `preserveInvariance`. At the trail 28.4 fps vs. 21.0, no
z-fighting.

### Hoisted texture loads

A texture load (compute from the shared memory into a scratch buffer, then
a copy into the image) in the middle of a frame ends the render pass.
`vulkan_hoist_texture_loads` (default on macOS) records a load into a
separate list replayed at the start of the submission, after the hoisted
uploads and their barriers, when moving it there can't change what any
command sees: the image wasn't sampled or loaded in the command stream of
this submission yet (`VulkanTexture::main_stream_submission`), and the
GPU wrote none of its guest memory in this submission so far
(`VulkanSharedMemory::AreBytesWrittenInSubmission`, set by every `Use`
with a written range: resolves, memory export, uploads that stayed in the
stream). Hoisted loads bind their pipelines directly, take scratch memory
from their own pool (32 MB pages, reclaimed when the submission
completes; larger loads stay in the stream) and transition the image to
the sampled layout themselves, so the draws need no barrier. At the trail
21 of ~61 loads per frame qualify: the others reload textures sampled
earlier in the submission or read data resolved in it. 20.8 fps vs. 20.1.

### Frame pacing: guest clock and the drawable wait

Fable II paces itself on the guest vblank: the command processor waits
(WAIT_REG_MEM on 0x1FC83004) until the guest's vblank handler releases the
next frame, every second vblank. Two host problems disturbed that:

- **Guest clock drift.** `UpdateGuestClock` (`src/core/clock.cpp`) turns
  each host tick delta into guest ticks with an integer division. With
  Apple Silicon's 24 MHz host clock and the 50 MHz guest clock (ratio
  25/12), every call dropped up to 11/12 of a guest tick, and with threads
  polling the time all the time the guest clock (and the vblank timer that
  runs on it) ran about 3% slow: 58.2 Hz vblanks, 29.1 fps. The remainder
  is now carried to the next update (reset when the ratio changes).
- **The paint thread waits for a drawable while holding the queue.**
  MoltenVK gets the swapchain image's CAMetalDrawable lazily, while
  encoding the paint's command buffer inside vkQueueSubmit, and
  `nextDrawable` blocks until the display gives one back. The presenter
  holds the queue mutex around vkQueueSubmit, so the command processor's
  submissions waited too (6 ms in the swap of 1 frame in 10, ~10 ms of
  "submit" per frame). With `vulkan_fetch_drawable_before_submit` (on by
  default) the device enables VK_EXT_metal_objects and the presenter
  exports the acquired image's MTLTexture right after
  vkAcquireNextImageKHR, which makes MoltenVK fetch the drawable there,
  outside the lock.

### Performance (open)

GPU at 100% with a low frame rate after the fix. Things known to cost:

- `MTL_DEBUG_LAYER=1` itself: measure frame rate without it.
- The readbacks above (now off by default on macOS). Note that
  `--no-vulkan_readback_memexport` alone never turned memexport readback
  off: it only returns the alias to its default, which hands control to
  `readback_memexport` (default on).
- The game's own 4x MSAA. Xenia's Fable II patch file has "Disable MSAA"
  (be8 `0x8238DF3F` = 1, the immediate of `li r9, 2`); here it is the
  mid-asm hook `fable2_hook_game_msaa` behind `[patches] game_msaa` in
  `fable2_config.toml` (-1 = the game's 4x, the default; 0 = none, 1 = 2x,
  Xenia's value). With 0, normal scenes ran at the 30 fps cap; the heavy
  scenes stayed GPU-bound (about 300 ms per frame, all of it frame throttle).
- The `fsi` path shades every sample (sample-rate shading at 4x MSAA) with
  interlock and storage-buffer traffic; it is the slow path on every host.
  Without dynamic rendering every sample-count change and every barrier
  restarts the Metal render encoder; the `FSI stats` line shows how often.

## Renderer review: Vulkan on MoltenVK vs. a native Metal backend

Reviewed against the v0.10.0 SDK source, the M4's reported device features
and MoltenVK 1.4.1 (bundled). Sizes: `src/graphics/vulkan` 21k lines,
`src/ui/vulkan` 7k, the Xenos-to-SPIR-V shader translator ~12k of the 26k
line `pipeline/shader` tree, plus 5k shared.

What the Xenos emulation needs from the GPU and what Metal/MoltenVK gives it:

| Need | On the M4 via MoltenVK | Where it matters |
|---|---|---|
| Fragment shader interlock (FSI path's EDRAM emulation) | Exposed (`fragmentShaderPixelInterlock`, `SampleInterlock`), translated to Metal raster order groups | Whole FSI path |
| Fragment/vertex stores and atomics | Exposed | Hard requirement, met |
| Geometry shaders (point sprites, rect lists, quad lists) | Absent; SDK falls back to vertex-shader expansion. The rectangle fallback re-implements the geometry shader's corner detection (longest edge = hypotenuse), reviewed and equivalent | Fullscreen passes, particles |
| `D24_UNORM_S8` depth | Absent, stored as `D32_SFLOAT_S8` | Host-render-target (`fbo`) path only; the FSI path keeps depth in the EDRAM buffer and never uses a host depth image |
| Disabling primitive restart | Impossible in Metal (the per-draw warning) | Only a 16-bit index buffer using vertex 65535 as a real vertex is affected: negligible |
| Sparse buffers, custom border colors, null descriptors, cull distance, point polygons | Absent; every use site checks the feature and falls back | None observed |
| Barriers inside a render pass | Unsupported on Apple GPUs; the SDK never emits one (`SubmitBarriers()` ends the pass first, the FSI render pass has no self-dependency) | Not applicable |
| Dynamic rendering | Exposed | Fine |
| 16 samplers per stage (`maxPerStageDescriptorSamplers`) | Not checked by the SDK; Metal would refuse the pipeline and it would be logged. None logged | Not the cause so far |
| Exact float semantics (35 `NoContraction` / NaN-preserve sites in the translator; float24 depth bit tricks) | MoltenVK compiles with fast math by default (`MVK_CONFIG_FAST_MATH_ENABLED=2`, per-shader limits) | Unverified; cheap to test with `=0` |

Upstream's own Vulkan status: the Windows build ships D3D12 only; the
Vulkan plugin is "Stage 2, in progress", and the local SDK patch still
carries "TEMPDIAG" code from a Vulkan black-screen diagnosis. (When this
review was written, this SDK's Vulkan backend had not been shown to render
Fable 2 correctly here; it does now, after the MSAA fix above.) Oery/fable-ii-recomp (Linux, therefore Vulkan on a
native driver) reports the game playable, so the backend can render it.

**Verdict: stay on Vulkan through MoltenVK.** Every hard requirement of the
FSI path is met, the two missing features have reviewed fallbacks, and the
one Metal-specific limitation is harmless. A native Metal backend would
replace ~40k lines (command processor, render target cache, texture cache,
primitive processor, pipeline cache, presenter) and still need a shader path,
which would be SPIRV-Cross to MSL, i.e. exactly what MoltenVK already does.
Xenia's Vulkan backend, which this is, took years; every other ReXGlue macOS
port (Minecraft 360, Rayman Origins, Skate 3's Xenos path) also runs on
MoltenVK. Metal-native EDRAM emulation (tile shaders / imageblocks) is a
research project for the SDK, not a port task. Fixes belong in the SDK's
Vulkan backend, built from source on macOS, where they also help Linux.

What that left for the corruption at the time (since fixed: MSAA sample
counts, raster order groups, the quad skip): either a generic bug in this
backend or a MoltenVK translation issue. Neither is
found by flag-flipping; the tools that find them, all usable with the
prebuilt SDK:

1. **Vulkan validation + synchronization validation** (LunarG macOS SDK;
   the runtime requests `VK_LAYER_KHRONOS_validation` with
   `--vulkan_validation_enabled`, needs `--vulkan_log_debug_messages` and
   `VK_LAYER_PATH`).
2. **Metal API validation** (`MTL_DEBUG_LAYER=1`), for what MoltenVK asks of
   Metal.
3. **Xcode GPU frame capture** through MoltenVK
   (`MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=2`), with `--gpu_debug_markers` so
   draws are labelled; shows the EDRAM buffer and each resolve.
4. **A newer MoltenVK without a rebuild**: the runtime honours
   `VK_DRIVER_FILES` if already set, so the LunarG SDK's ICD can replace the
   bundled 1.4.1.
5. **The translated shaders** (`--dump_shaders=<dir>`) run through
   `spirv-cross --msl` to confirm the EDRAM buffer gets
   `[[raster_order_group]]`, the one mechanism the FSI path depends on.
6. **The SDK built from source on macOS** (its CI does; Debug build enables
   the `assert_always` checks in the EDRAM state machine) once something
   needs fixing.
7. **The same plugin on a native Vulkan driver** (any Linux or Windows box
   with a GPU), the decisive Mac-vs-generic test.

## Validation findings (LunarG 1.4.357.1 layer, sync validation on, M4)

Run on the corrupted scene with `--vulkan_validation_enabled
--vulkan_log_debug_messages`, `VK_LAYER_PATH` at the SDK's
`explicit_layer.d` and `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`:

1. **`VUID-VkGraphicsPipelineCreateInfo-layout-07990`, `xe_system_cbuffer`
   descriptor type mismatch** in the vertex and tessellation control stages of
   every tessellation pipeline. Root cause in `src/graphics/vulkan/pipeline_cache.cpp`:
   the GLSL helpers for tessellation declare the system constants at
   `set = 0, binding = 0`, but set 0 holds the shared-memory and EDRAM storage
   buffers; the constants are set 1 (`kDescriptorSetConstants`). Tessellated
   draws therefore read vertex index parameters and tessellation factor
   limits out of guest RAM. Fixed by `thirdparty/sdk_mac_vulkan_fixes.patch`,
   applied by `tools/build_sdk_mac.sh`. Generic Vulkan-backend bug (D3D12 has
   its own tessellation path), not MoltenVK-specific.
2. `VUID-VkGraphicsPipelineCreateInfo-pStages-06894`: pipelines with
   `rasterizerDiscardEnable` still carry a fragment stage. Spec violation;
   Metal has no rasterizer discard, MoltenVK emulates it. Fixed by the patch
   (the fragment stage is dropped from those pipelines).
3. `VUID-vkCmdEndQuery-None-07007` / `-01923`: an occlusion query ended
   outside a render pass / before it was begun (once each). Not fixed yet.
4. `SYNC-HAZARD-WRITE-AFTER-WRITE` on `vkCmdCopyBuffer` to the same buffer
   (10+). Two transfer writes without a barrier; ordered within a Metal blit
   encoder in practice. Not fixed yet.
5. `VUID-vkDestroySwapchainKHR-swapchain-01282` once, on a window resize.

KosmicKrisp (LunarG's Mesa driver, also in the SDK) was tried via
`VK_DRIVER_FILES`: it does not expose fragment shader interlock, so the SDK
silently falls back to the host-render-target path, which froze (as it
flashed white on MoltenVK). Not usable for this game until it gains interlock.

## Building the SDK from source on macOS

`tools/build_sdk_mac.sh` clones the exact `v0.10.0` tag (shallow, with
submodules; the SDK builds its whole Vulkan stack including MoltenVK, so a
full Xcode is required), applies `thirdparty/sdk_mac_vulkan_fixes.patch`,
builds `rexgpu-xenos` and `rexruntime`, and stages both dylibs next to the
game with `.prebuilt` backups (`-restore` puts them back, `-debug` builds
the Debug SDK with its asserts for the Debug game).

The SDK's CMake helpers copy the prebuilt plugin and runtime next to the
executable on every link, which used to undo the staging whenever the game
was rebuilt. `build.sh` now runs `tools/build_sdk_mac.sh -stage` after each
build when the `.prebuilt` backups show the source SDK was staged.

Options:

- `-codegen` also builds the recompiler (`rexglue`) with the patch's codegen
  fixes (currently the upstream `vpkuhus`/`vpkuwus` aliasing fix, rexglue-sdk
  6319e23) and regenerates `generated/`; only files whose output changes are
  rewritten. The script records the binary in
  `thirdparty/rexglue-sdk-src/codegen-tool.path`, and `build.sh` passes it as
  `FABLE2_CODEGEN_TOOL` (upstream's name for the override) so later codegen
  runs keep the fix. Worth it only if the game uses those
  instructions with the destination as a source; see the check below.
- `-mvk-private` builds MoltenVK with `MVK_USE_METAL_PRIVATE_API` and stages
  it in `vulkan/lib`. MoltenVK can then honour "primitive restart disabled"
  (Metal otherwise restarts strips at index 0xFFFF). Experimental.

Check for the codegen bug (prints any `vpkuhus`/`vpkuwus` whose destination
register is also a source):

```sh
grep -rhoE "// vpku[hw]us(128)? v[0-9]+, ?v[0-9]+, ?v[0-9]+" generated | awk -F'[ ,]+' '{d=$3; if ($4==d || $5==d) print}' | sort | uniq -c
```

## Known gaps

- **Hero/dog black textures:** fixed upstream by realtime texture morphing
  (no readback). The older readback fix needs the source-built plugin (the
  prebuilt one has no `readback_resolve_force_addresses`, and the app logs
  "cvar readback_resolve_force_addresses rejected" if
  `hero_dog_texture_readback` is turned on with it).
- **Flickering dark shadow areas:** open; evidence and candidates in
  `plans/macos-port-handoff.md`.
- **Keys:** the SDK binds F3 (debug overlay) and F4 (settings overlay, mouse
  unlock); the default `keyboard_gamepad_map` puts the d-pad on the arrow
  keys. Mac keyboards need fn for F-keys unless "Use F1, F2, etc. keys as
  standard function keys" is on, so the debug keys are also on 6-9 and 0.
