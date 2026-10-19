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

`vulkan_batch_texture_load_copies` (default on, any time) then orders the
hoisted loads as all the compute dispatches, one barrier, all the copies.
MoltenVK needs a new Metal encoder at every switch between compute and
blit work and after a barrier between two copies, so a load recorded as
dispatch, barrier, copy, barrier cost two encoders per texture (about 64
hoisted loads per frame at the heavy view). The loads now record the
copies into a second list (`texture_load_copy_command_buffer_`) and hand
their image barriers to the command processor, which submits all of the
before-copy transitions with the dispatch-to-copy barrier and all of the
after-copy ones in one barrier after the copies. An image is loaded at
most once per submission this way (a second load in the same submission
stays in the stream), so no image is in one barrier twice. The trace's
start of a frame went from `BDIIIIIIII...` to `BDI`; copy-to-image
encoders per second halved.

### Why textures reload every frame

`texture_load_log` (hot reload) logs, every 300 frames, why textures were
loaded (first load, after a CPU write, after a GPU write such as a
resolve) and the 30 loaded most often, with how many reloads found the
same guest data as the texture's previous load (a hash of its base and mip
ranges). At the heavy view about 100 loads per frame (fable_2_188):

- **~40 after a GPU write**: render-to-texture targets read back after
  every resolve: the three 256x256 `1_5_5_5` targets (4 per frame each),
  the 280x180 and 560x360 post-processing chain, the 1120x720 depth (2.5
  per frame) and the two 1024x1024 shadow maps (2 each). These are
  needed: the resolve writes the shared memory, and the texture is loaded
  from it.
- **~60 after a CPU write, all with unchanged data**: small `DXT1`
  textures, 16x16 linear ones at 0x1A55C000-0x1A567000 (next to the
  per-frame vertex buffers at 0x1A568000) and a 16x16 cube. Write watches
  are per host page, 16 KB on Apple Silicon (4 guest pages), so a CPU
  write anywhere in it outdates every texture there.

`texture_skip_unchanged_reloads` (hot reload; on by default on macOS since
"CPU: MoltenVK encoding on its own thread", off before) skips such
a reload when the texture's guest data hashes the same as at its last
load, and only re-arms the watch. It only applies when no page of the
texture holds GPU-written data (`SharedMemory::IsRangeGpuWritten`), now
and at the load the hash came from, so the shared memory equals the guest
memory both times. A CPU write after the upload makes the range invalid,
and `MakeUpToDateAndWatch` keeps the texture outdated as before. It skips
59 of the ~100 loads per frame at the heavy view, the picture stays the
same, but GPU and CPU time per frame don't change measurably (heavy view
19.8 / 20.5 vs. 20.3 / 20.1 ms, CPU texture time 0.8 ms either way): the
skipped loads are small, and they were already batched at the start of
the submission.


### Texture loads straight into the image

`vulkan_texture_load_to_image` (default on, hot reload) loads textures that
are render targets resolved to memory without the scratch buffer and the
copy: a texture created while its first page holds GPU-written data
(`SharedMemory::IsRangeGpuWritten`) gets `VK_IMAGE_USAGE_STORAGE_BIT`
(with `MUTABLE_FORMAT` and `EXTENDED_USAGE`, plus the storage format in the
format list), and its loads in the command stream run a shader that writes
the texels into the image through an unsigned integer view of the same
size (`r8ui`, `r16ui`, `r32ui`, `rg32ui`), so the image gets the same bits
as from the copy. The shaders are
`src/graphics/shaders/texture_load_to_image.cs.glsl` in the SDK tree,
compiled into `vulkan_spirv/texture_load_*_image_cs.h` by
`tools/build_texture_load_image_shaders.sh` (glslangValidator): the same
untiling, endian swaps and conversions as the 8, 16, 32 and 64 bpb copies,
24-bit float depth and `1_5_5_5` (the formats the resolved textures use),
written from the decompiled SPIR-V of the existing load shaders. Only 2D,
one layer, not resolution-scaled, no packed mip tail, no float fallback;
anything else, and hoisted loads, keep the copy. Each texture keeps a
storage view and descriptor per mip level (a pool owned by the texture
cache; destroyed textures hand theirs back for reuse, since the SDK's
function table has no `vkFreeDescriptorSets`). The image stays in
`GENERAL` (`Usage::kComputeWrite`) until binding it for sampling
transitions it. Needs `shaderStorageImageExtendedFormats`, now enabled.

At the heavy view about 28 loads per frame take this path. Trace
(`out/profile/ld_dir.trace` vs. `ld_cpy.trace`, per command buffer):
copy-to-image encoders 1.6 vs. 14.5, idle gaps between encoders 2.17 vs.
3.26 ms (dispatch-to-copy gaps 0.04 vs. 1.03 ms), copy time 0.19 vs. 0.44
ms. Sweep alternating the switch 12 times: shots within 1.6-2.0% (snow).
Uncapped on `fbo`, both orders (`fable_2_205`-`208`, GPU otherwise 9-16%
busy): **heavy view 20.0 / 19.5 vs. 20.9 / 20.7 ms per frame** (34.5
direct loads per frame), trail 16.6 / 16.7 vs. 16.7 / 16.9 ms.

### Resolves straight from the host render target

On the `fbo` path every resolve first dumps the render targets owning the
resolved EDRAM range into the EDRAM buffer (one compute dispatch each), and
the resolve copy then reads that buffer. `direct_host_resolve` (default on,
hot reload; the upstream switch was a stub that dumped anyway) now skips the
dump for the common case: the fast 32bpp copy (`resolve_fast_32bpp_1x2xmsaa`)
of a 1x color render target in `k_8_8_8_8`, `k_2_10_10_10`,
`k_2_10_10_10_AS_10_10_10_10` or `k_32_FLOAT` that owns the whole region (one
dump rectangle covering all rows, `GetDirectResolveSource`). The copy then
runs `resolve_direct_fast_32bpp.cs.glsl` (SDK tree, headers
`vulkan_spirv/resolve_direct_fast_32bpp_{float,uint}_cs.h` built by
`tools/build_texture_load_image_shaders.sh`): the decompiled fast resolve
with the EDRAM buffer read replaced by a `texelFetch` of the render target's
transfer view at the pixel the dump would have stored there, packed the way
the dump shaders pack it (`uint(c * 255 + 0.5)` per 8-bit channel, `* 1023`
and `* 3` for 2_10_10_10, raw bits through the `R32_UINT` view for 32-bit
float), then the same red/blue swap, endian swap and tiled destination
address. Anything else (depth, MSAA, 64bpp, the full copies, regions over
several render targets) dumps as before.

`vulkan_resolve_log` lists the resolves every 5 s by copy shader, size,
destination format and the render targets they read, tagged `direct` when
they take this path. At the heavy view 16 of the ~25 fast copies per frame do
(256x256, 280x184 and 640x360 `8_8_8_8`; 280x184 and 560x360 `2_10_10_10`;
280x184 `32_FLOAT`); the rest are depth, 4x MSAA, or other shaders.
`vulkan_debug_skip_resolve_dumps` and `vulkan_debug_skip_resolve_copies`
(wrong picture) measured what the two halves cost there before: dumps
~1.9 ms, copies ~3.7 ms per frame.

Sweep alternating the switch 6 times (`out/direct_resolve_check`): shots
within 1.6-1.8% of the first (snow). Uncapped on `fbo`, both orders
(`fable_2_216`-`219`, GPU otherwise 10-20% busy): **heavy view 18.9 / 18.9
vs. 19.4 / 19.3 ms per frame**, trail 16.6 / 16.6 vs. 16.6 / 16.5 ms.

**Depth** (`vulkan_direct_resolve_depth`, default on, hot reload): the same
for 1x depth render targets, built with `DEPTH 1` (the depth and the stencil
view of the render target, `direct_resolve_pipeline_layout_depth_`). Packed
as the depth dump packs it: `roundEven(depth * 0xFFFFFF)` for `kD24S8`,
`SpirvShaderTranslator::PreClampedDepthTo20e4` (remapped from [0, 0.5],
rounding per `depth_float24_round`) for `kD24FS8`, depth in bits 8:31 and
stencil in 0:7. The dump stores depth with the 40-sample halves of each tile
swapped and the fast resolve swaps them back when reading, so the render
target is read at the unswapped position. At the heavy view this covers the
two 1024x1024 shadow maps and the 1120x720 depth per frame; the 4x MSAA
depth (560x360, 640x360, 1280x720) still dumps. Sweep alternating it
(`out/direct_resolve_depth_check`): shots within 1.3-2.0%, shadows the same.
The frame time A/B stays within the run-to-run noise (`fable_2_221`-`228`,
heavy view 18.9-19.7 ms either way); the traces (`out/profile/rd_dep.trace`
vs. `rd_nod.trace`, heavy view, per command buffer, about 2.2 per frame)
show compute 1.50 vs. 1.63 ms and 42.5 vs. 43.9 dispatches, so about 0.3 ms
of GPU time per frame.
### Render pass areas and the EDRAM rows draws claim

**Per-pass GPU time.** `vulkan_pass_log` (hot reload) logs, every 5 s, the
host render passes of one frame per submission in recording order
(`[passes]`: framebuffer size, MSAA, formats, draws, end reason, the most
used pixel shaders) and the render areas they were begun with (`[pass
areas]`). It also begins each pass position with its own copy of the
`VkRenderPass`, named `<submission>.<pass> <target>`: MoltenVK labels every
Metal render encoder with the render pass's debug name, so a Metal System
Trace tells every pass apart. `tools/mac_pass_costs.py <trace> <log>`
prints the median vertex and fragment time per pass with the log's
description, and totals by kind of pass. A pass that begins with an
ownership transfer keeps the draws that follow (`transfer ... | N draws`).

**What it showed at the heavy view** (`out/profile/passes.trace`,
`fable_2_232`, two submissions of 80 and 69 passes per frame): the base pass
(1749 draws) 1.7 ms vertex and 1.8 ms fragment time, the depth prepass
(1356 draws) 1.0 ms, and **~67 passes per frame with at most one draw,
mostly render target ownership transfers, at 250-340 us of fragment time
each: ~5.4 ms per frame, half of all fragment time.** Two causes:

- Every pass began with the whole framebuffer as its render area, and host
  render targets are as tall as the EDRAM allows (1120x2352 for the 1120x720
  scene, 320x8192, 560x4688, 1040x2528 for the shadow maps). MoltenVK sizes
  the Metal pass from the render area (`renderTargetWidth/Height`), and a
  tiled GPU loads and stores the attachments over all of it: 1120x2352 depth
  and stencil is ~13 MB each way. The upstream code had a TODO for this.
- `execute_unclipped_draw_vs_on_cpu` was off in the SDK (on in Xenia).
  Draws with clipping disabled (screen-space quads and clears with an
  8192x8192 scissor, all over the post-processing chain) then count as using
  their render target down to the end of the EDRAM, so the next render
  target there gets an ownership transfer of all of it. Xenia's own flag
  text names exactly this ("spurious EDRAM range ownership transfer round
  trips").

**Changes:**

- `vulkan_tight_render_area` (default on, hot reload): when the deferred
  command buffer is replayed, each render pass begins with the union of the
  scissors of its draws and of its clear rectangles (scanned up to the end of
  the pass, clamped to the framebuffer) instead of the whole framebuffer.
  Ownership transfers set the scissor to the bounds of the rectangles they
  copy instead of the whole framebuffer, and draws clamp the bottom of their
  scissor to the rows the render target cache estimated for them
  (`RenderTargetCache::last_update_height_used`, the same estimate the EDRAM
  ownership uses, so nothing past it is ever read from the render target).
- `execute_unclipped_draw_vs_on_cpu` now defaults to true: the vertex shader
  of unclipped draws runs on the CPU (the existing shader interpreter) to
  find their real bottom.

**Results.** Sweep alternating both 8 times at the trail
(`out/tight_area_check`): shots within 1.4-2.0% (snow), post-processing
unchanged. Uncapped on `fbo`, both orders (`fable_2_239`-`244`): **heavy
view 16.6 / 16.5 ms per frame with both, 19.8 / 19.6 ms with neither**, 16.7
/ 16.6 ms with the estimate alone; the trail is unchanged at the 60 fps cap
(16.7 ms), and so is the heavy view now. GPU load at the heavy view (traces
`out/profile/area_old.trace` vs. `area_both.trace`, taken a few seconds
after the camera turn): **86% vs. 67% busy**, render encoder time per
command buffer 14.4 vs. 6.6 ms (7.2 ms with the estimate alone), at a higher
frame rate. Per pass: the base pass 1.04 ms vertex / 0.98 ms fragment (from
1.67 / 1.85), the prepass 0.58 ms vertex (from 0.95), single-draw transfer
passes 1.3-1.7 ms of fragment time per frame in total (from 5.4).

With the draw scissor clamp only the 1024-row shadow maps still begin
taller than 1024 rows (`draw_extent_log` lists the draws whose estimate is
1024 or more and what limited it; the `[passes]` lines show each pass's
largest estimate and its draws without one, the groups of 24 that use no
render target). What remains is shading: the base pass, the prepass, a
full-screen 2-draw pass (0.44 ms) and one 280x180 post-processing draw
(0.23 ms).

### Texture fetches: implicit LOD instead of explicit gradients

Every guest `tfetch` the translator emits samples with explicit gradients:
coarse derivatives of the coordinates scaled by 2^LOD (the fetch constant's
and the instruction's LOD bias), so the host computes the guest's LOD
(`spirv_translator_fetch.cpp`). It also samples the unsigned and the signed
view behind uniform branches and applies the per-component sign handling
(`texture_swizzled_signs`) with switches on system constants: a 13-tap blur
(`28DBE18F313350BF`) becomes ~2250 lines of Metal. Dumps for reading them:
`out/capture/rex/shader_<hash>_*.vk_fbo.bin.frag` (`spirv-cross --msl`).

`vulkan_tfetch_implicit_lod` (on by default on macOS, restart) samples 1D,
2D and cube fetches whose LOD is computed with an implicit LOD and the same
LOD as the bias (pixel shaders, no register gradients). Same LOD up to how
the host computes its derivatives. Traces at the heavy view, two each
(`out/profile/grad*.trace` vs. `imp*.trace`, `fable_2_254`-`257`): the
full-screen shadow mask pass (`1B4B5AE12EF270CA`, `0F99810F072BF0FC`)
445 / 448 -> 365 / 363 us of fragment time, the blur 237 / 231 -> 214 /
214 us, the base pass 1009 / 1009 -> 981 / 944 us; GPU 70% / 70% -> 68% /
65% busy (about 0.2-0.3 ms per frame). Sweeps in each mode
(`out/lod_check_{grad,imp}`): shots differ between the modes no more than
within one (1.4-1.9% of pixels; mean difference 1.8-3.3 vs. 1.6-3.1), and
distant roof tiles and cobbles are equally sharp.

### Texture fetches: signedness specialized per pipeline

The rest of the per-fetch overhead is the signedness: every fetch reads the
texture's swizzled signs from the system constants (`texture_swizzled_signs`,
2 bits per component), samples the unsigned and the signed host view behind
uniform branches and switches per component between unsigned, signed,
biased and gamma. With the signs as a constant instead (a debug build that
took every texture as unsigned, wrong for signed ones) the base pass took
593-601 instead of 937-959 us of fragment time, the shadow mask pass 250
instead of 365, the blur 95 instead of 211-215, and the GPU was 61% instead
of 65-70% busy at the heavy view (`out/profile/{dyn,const}*.trace`,
`fable_2_261`-`264`).

At the trail, 80% of the fetches read textures whose signs are all unsigned,
12% all signed (format 29, probably normal maps) and 3% gamma RGB with
unsigned alpha (sign 3330); the rest are rare. `vulkan_tfetch_specialize_signs`
(on by default on macOS, hot reload) builds pixel shaders on the host render
target path for those: the pixel shader modification's
`fsi_specialization_low` (15 bits the host render target path doesn't use)
holds 2 bits per texture binding (`Shader::TextureBinding::binding_index`,
the first 7): dynamic, all unsigned, all signed, gamma RGB + unsigned alpha
(`Modification::TextureSignsSpecialization`). `GetCurrentPixelShaderModification`
takes them from the fetch constants (`texture_util::SwizzleSigns`, as the
texture cache does); the translator then uses the signs as a constant in
place of the system constant word, and the host compiler drops the other
view's sample and the switches. Textures the cache can't bind are unsigned in
the system constants, so after `RequestTextures` the command processor
compares the specialization with `GetActiveTextureSwizzledSigns` and falls
back to the dynamic signs for those bindings (the bindings don't depend on
the modification). A shader typically always sees the same kind of texture
per binding, so this adds few pipelines. Same picture: two sweeps that
switch it on and off (`out/sign_check`, `out/sign_check2`) differ within
the snow noise (1.3-2.1% of pixels), and the glowing trail, which pulses,
follows the time of the shot, not the mode. Measured without
Metal traces (their temp files take ~2 GB each on the system disk): at a
2321-draw view of the trail, uncapped (`--vsync=false`), 80.6 -> 84.7 fps
(+5%, `fable_2_281`, `282`); at the 60 fps cap the GPU's "Device
Utilization" was 94 / 85 / 84% dynamic vs. 70 / 81 / 82% specialized
(`fable_2_273`-`278`; that figure follows the GPU clock, so it's coarse).

### GPU time per frame without traces

Metal System Traces show the GPU time per encoder, but each one leaves a ~2
GB `instruments*.ktrace` in `$(getconf DARWIN_USER_TEMP_DIR)` on the system
disk, and the GPU's "Device Utilization" in `ioreg` follows its clock
(~89% even at 5 fps). `vulkan_gpu_time` (on, restart) writes a timestamp
query at the top of each submission's command buffer and at its bottom,
reads both once the submission's fence has signaled (no waiting; up to 64
submissions in flight, more are counted as not measured), and after each
frame statistics line logs `GPU time: N ms/frame, M% of the time`. On
MoltenVK the timestamps are sampled at encoder boundaries, so this is the
span of the command buffer's encoders; submissions on the one queue run one
after another, so the sum is the GPU's busy time. At the game's default
30 fps (`fable_2_284`): logos (25 draws) 0.7 ms per frame, 2% of the time;
title screen (~290 draws) 4.7 ms, 14%; gameplay at the trail (~2300 draws)
10.6 ms, 32%. That agrees with the uncapped frame rate at the same view
(~83 fps, 12 ms per frame).

`vulkan_pass_gpu_time` (off, hot reload) also times the render passes, in
one frame out of ten: the deferred command buffer writes a timestamp before
each render pass begins and after it ends (into a 2048-query pool: with
8192 MoltenVK gave every query of the pool the same time, apparently
falling back to a CPU time for a pool larger than a Metal counter sample
buffer can be), and the passes are described by their target (as in
`vulkan_pass_log`) and their most used pixel shader. MoltenVK resolves each
timestamp written outside an encoder in a blit pass of its own that waits
for the encoders before it, so in a timed frame the passes run one after
another with those blit passes in between: a timed frame took 15.7 instead
of 10.6 ms at the trail, the average over all frames 10.95 ms. The pass
times are what each pass costs on its own; the time outside passes
(compute, copies, barriers) includes the timestamps' blit passes. At the
trail at 30 fps (`fable_2_291`, per timed frame): 63 transfers 3.23 ms, 35
passes with 0-2 draws 1.86 ms, 16 with 3-99 draws 0.94 ms, 3.5 with 100+
draws 2.39 ms (the 466-draw pass with `8D900846800943C8` 1.83 ms); the
largest transfers are 11 color transfers per frame into a 320x8192 target
(0.57 ms) and 5 depth transfers into it (0.29 ms). The title screen: 25
transfers 1.15 ms of 2.48 ms in passes.

### Transfers: clear quads cut out what they overwrite

When a draw uses EDRAM tiles that another render target owned (another
format, MSAA mode or pitch), the render target cache copies those tiles into
the new render target first (an ownership transfer). Resolve clears already
cut their rectangle out of the transfers. Fable II clears with draws
instead: a rectangle list quad (vs `1E6883FCCDE1F688`, 3 vertices, the
positions often in screen space with `vtx_xy_fmt`/`vtx_z_fmt`), often the
same EDRAM at 1x and then at 4x MSAA, so each clear first copied the old
contents it was about to overwrite.

`clear_quad_transfer_cutout` (on on macOS, hot reload) treats such a draw
like a resolve clear for the render targets it overwrites outright: no
culling, solid fill, the full sample mask, no alpha test, alpha to coverage
or kill; depth with test ALWAYS and write, and stencil (if enabled, both
faces) passing always with REPLACE and write mask 0xFF; color with the
pixel shader writing the target, mask F and blend ONE/ZERO. The rectangle
comes from running the vertex shader on the CPU
(`EstimateRectangleCoverage` in `draw_extent_estimator.cpp`): the three
vertices must form an axis-aligned rectangle; it is converted to pixels
with the viewport transform and the half-pixel offset, rounded by the
pixel-center rule at 1x (whole pixels at MSAA, conservatively), and clipped
to the scissor. The cache drops transfers that lie inside the rectangle
and cuts it out of the others' rectangles.

At the trail at 30 fps about 37 clear quads per frame qualify: transfers
93 -> 83 per frame, transfer passes 63 -> 57, and their time in
`vulkan_pass_gpu_time` frames 3.7 -> 2.9 ms. The frame GPU time stays at
11.5-12.0 ms with or without it (two runs each, and alternating within one
run): in timed frames each pass waits for the one before, so small passes
look costlier there than they are. The picture is the same (a sweep
switching it: on/off pairs as far apart as on/on, 1.3-2.0% of pixels, the
snow). `vulkan_transfer_log` (off, hot reload) logs one frame's transfers
every 5 s with the draw that caused each.

### Transfers: clear quads cleared in place

What the GPU time at the trail goes to (30 fps, ~10.5 ms per frame,
measured by switching each part off, so the picture is wrong in those
runs): resolve copies 3.2-3.5 ms, of which ~1 ms is the texture reloads
of resolved data (`debug_skip_resolved_texture_loads`); transfers ~2.3 ms
(`vulkan_debug_skip_transfers`); the depth prepass ~1.7 ms; resolve dumps
~0.3 ms. So emulating the EDRAM takes more than half of the GPU time. With
the transfers switched on and off in one run, the system's GPU power was
5.87 W against 4.92 W (`tools/mac_gpu_power.c`).

Most transfers come from how the game clears. It clears a 1x render target
in two quads: a strip at the edge at 1x (for example 16x256 at x 240 of a
256x256 target, 64x1024 at x 960 of the 1024x1024 shadow maps), then the
rest with a quad at 4x MSAA over the same EDRAM (120x128 pixels, i.e.
240x256 samples), and draws on at 1x. Each 4x clear moved the tiles into a
4x render target and the next 1x draw moved them all back: of 83
transfers per frame, 52 were caused by clear quads and 14 by the draw
right after one.

`clear_quad_in_place` (on on macOS, hot reload; needs
`clear_quad_transfer_cutout`) replaces such a clear quad by clears of the
render targets that already own its tiles in the same format: one value in
every sample is the same bytes in the EDRAM at any MSAA mode, so the
owner keeps the tiles and nothing is copied. In `RenderTargetCache::Update`
(`PrepareClearQuadInPlace`) a clear quad qualifies if:

- it overwrites all of its render targets (32bpp only), its rectangle is
  exact (at MSAA, edges on pixel edges), and no memexport or occlusion
  query depends on it being drawn;
- for each render target, the tile rows of the rectangle belong to one
  render target with the same base, pitch and format but another sample
  count (for depth, holding the host depth as well), and the rectangle is
  whole pixels of it;
- the values are uniform and the host would store exactly them: Z the same
  at the three corners, exact in the depth format (unorm24, or float24),
  no polygon offset, the stencil reference (the same for both faces); the
  color from running the pixel shader on the CPU
  (`DrawExtentEstimator::EvaluateUniformPixelShaderColor`: no texture
  fetches, kill or depth output, its interpolator inputs the same at all
  corners), no exponent bias, `k_8_8_8_8` or `k_2_10_10_10`, components
  not halfway between two steps.

The Vulkan cache then clears the owners with the resolve clear code
(`vkCmdClearAttachments`) and the command processor skips the draw. The
clears get a render pass of their own: on MoltenVK, a depth clear between
draws in one render pass lost the depth of the draws after it (the shadow
casters in the same pass, so the shadow maps were empty and the ground had
no shadows; ending the pass first fixed it, the render area was not the
cause). At the start of a pass the clear works, and the draws after it
continue in that pass.

At the trail (`fable_2_354`): 8 clear quads per frame cleared in place
(the scene's 256x256 targets, both shadow maps, the 320x176 and 560x352
depth clears), transfers 83 -> 62 per frame, tiles copied 13879 -> 9489.
In `vulkan_pass_gpu_time` frames (alternating, `fable_2_356`): 20.1-21.5
-> 16.1-16.7 ms per timed frame, render passes 14.5 -> 10.3 ms (transfers
4.4-5.4 -> 3.4-3.8 ms, passes with 0-2 draws 2.8-3.2 -> 1.1-1.5 ms). GPU
power, four alternating pairs of 26 s (`fable_2_357`): 7.57 -> 7.06 W
(the system's, with a browser using the GPU too); the untimed GPU time is
~1.3 ms lower but noisy, as the GPU clock follows the load. The picture is
the same at the trail, after walking and in the pause menu (on/off pairs
as close as off/off).

What is left of the clear pattern: the 1x strip clears still transfer the
rest of their tiles from other formats (cut out only where the strip is),
and resolves cost ~70-150 us each whatever their size (~35 per frame).

### Transfers: the strip clears (tried, dropped)

Two strip clears (draws with vs `1E6883FCCDE1F688` at x -0.5) were not
taken as clear quads: with clipping disabled their Z came out of the vertex
shader as -3.7e-9, and the rectangle estimate required Z in 0..1. Without
clipping the host doesn't clip Z (the pipeline uses depth clamping, and
isn't created without it), so the estimate now only requires Z to be a
number then, and reports it clamped to 0..1. At the trail: transfers 62 ->
60 per frame, tiles 9489 -> 9181 (`fable_2_366`), same picture.

Holding the strips' transfers back was tried and dropped. The idea: the
strip draw writes only its rectangle, the transfers only outside it, so
they can run after it; if the next draw clears the rest (the in-place 4x
clear, or a 4x quad after a 4x strip), the transfers need to copy even less.
Implemented (the transfers held until the next draw, resolve or end of
submission, the cutout grown by the next clear quad's rectangle when the two
make one rectangle), the picture stayed the same, but it gained nothing:
render pass time 15.3 ms per timed frame either way (`fable_2_365`,
alternating). What is left after the clears is still a sliver per transfer
(the columns between the cleared width and the pitch, e.g. 256-320 of a
4-tile target, or rows up to the next tile row), which has to be copied to
keep the EDRAM exact, so the number of passes stays the same; and a depth
transfer run right after the strip draw on the same target cost ~0.5 ms
instead of ~0.25 ms. Transfers cost about the same per pass whatever their
size (color into 320-wide targets ~0.11 ms, into 1120-wide ones ~0.11 ms,
depth ~0.25 ms, in timed frames; a render pass restart ~0.12 ms untimed,
`vulkan_debug_end_render_pass_every`). So fewer passes is what would pay:
for example transfers into all of a draw's render targets in one render
pass with the draw (now each destination gets a pass of its own, then the
draw another).

Measuring now: other apps using the GPU (another game port, RuneLite, the
window server) kept it 75% busy at 2.5 W with Fable II closed, so times and
power can only be compared within one run, alternating.

### Transfers: in the draw's render pass

Each render target taking over EDRAM tiles for a draw got the transfers in
a render pass of its own (one attachment, in the transfer format), and then
the draw opened another. A typical pair of clear-and-draw at the trail was
"transfer depth | transfer color | draw", three passes where one would do,
and on the M4 a pass costs about the same whatever it holds (see the strip
clears above).

`vulkan_transfers_in_draw_pass` (on by default on macOS, hot reload) does
the transfers in the draw's own render pass and framebuffer instead: the
transfer pipelines are made for the draw's render pass (the color transfer
shader writes the output location of its render target, the others' write
masks are 0, depth transfers don't write color), and the command processor
then sees the pass it wants already open and continues it. This was already
a TODO upstream; the pipeline code only needed the dynamic rendering formats
of all the attachments. Render targets get their draw usage before the
transfers, so the sources' barriers go in before the pass begins. It only
applies when:

- no transfer reads one of the draw's render targets (sampling an
  attachment); the host depth of the depth destination itself is fine, it
  is read from a copy in the EDRAM buffer made before;
- each color destination's transfer format is its draw format (not for
  k_16_16, k_16_16_16_16 and the 32-bit float formats, which transfer as
  integers to keep NaNs and -1 exact);
- stencil can be exported (no stencil clears and bit draws in the pass; the
  MoltenVK depth clear trap above);
- the destinations have the draw's pitch, so the framebuffer sizes match.

Otherwise the transfers get their own passes as before; `vulkan_transfer_log`
shows a `not in the draw's render pass` line with the reason. At the trail
only 2 per frame stay out (a k_32_FLOAT target).

At the trail: transfer passes 49 -> 13 per frame (left: the 11 in-place
clears, which need their own pass, and those 2), about 139 -> 114 passes in
all, timed frames ~20.0 -> ~18.5 ms (`fable_2_368`, three alternating
pairs), untimed GPU time ~11.1 -> ~9.6 ms per frame and GPU power 6.91 ->
6.77 W (`fable_2_371`, four alternating pairs; the power difference is
within the noise of the other apps). Same picture: on/on and on/off shot
differences are the same as off/off (animation).

About 9 passes per frame now hold only transfers: something ends the pass
between the transfers and the draw. Mostly (about 7 per frame) it is the
shared memory barrier a draw pushes after a resolve (`VulkanSharedMemory`
compute write -> read), pushed for any draw after a compute write, even if
the draw reads nothing the resolve wrote; the rest are texture loads.

`vulkan_skip_draw_barriers_after_writes` (on by default on macOS, hot
reload) skips that barrier when it isn't needed. `VulkanSharedMemory` now
keeps the ranges written by compute shaders and transfers (resolves,
uploads left in the command stream) since the last barrier that made all
writes visible to draws (a whole-buffer barrier to a draw usage); a draw
whose vertex fetch and index ranges are known and miss all of them, and
which writes nothing itself, needs no barrier, and the pass goes on. The
first draw reading one of the ranges gets the barrier as before (whole
buffer, from the writer's stage), and the next resolve's barrier also waits
for the draws in between (their stages are in its source mask, as for the
disjoint draw barriers). Until the first such barrier after creation, or
with over 256 ranges pending, every draw gets it. Draws reading unknown
ranges (vertex fetches in the pixel shader) always do.

At the trail: compute write -> read barriers 33 -> 25 per frame, passes
~118 -> ~110, timed frames ~17.8 -> ~16.4 ms (`fable_2_372`), GPU power
6.64 -> 6.46 W over four alternating pairs (`fable_2_375`). Same picture;
in the flicker test (16 shots 250 ms apart, standing still) the shots
differ from the first by 2.3-3.0% with it and 2.5-3.0% without it
(`fable_2_373`, `fable_2_374`), rising steadily with the animation, no
outliers.

### Resolves: written straight into the texture

What one resolve costs on the GPU, from the command letters `vulkan_pass_log`
now prints per logged submission (`[pass commands]`: P pass, B barrier,
D dispatch, C buffer copy, I buffer-to-image copy, X image clear): a typical
resolve is `P B D B D B P`. The draws' pass ends, the resolve dispatch writes
the shared memory, and the first draw sampling the result reloads the texture
from that memory with a second dispatch, barriers in between. On MoltenVK
1.4.1 with Metal argument buffers (the default here), every pipeline barrier
outside a render pass ends the current Metal encoder (`MVKCmdPipelineBarrier`
calls `endCurrentMetalEncoding`), so each of those dispatches gets a compute
encoder of its own.

- Turning argument buffers off (`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0`)
  was ~0.5 ms less GPU time per frame (8.6 -> 8.1 ms, `fable_2_378`-`381`,
  two runs each). GPU power was the same (~2.75 W), and the command
  processor took ~0.2 ms more CPU. Not adopted.
- The reloads of resolved textures cost 1.3-1.5 ms of GPU time and ~0.6 W
  (`debug_skip_resolved_texture_loads`; wrong picture). By format
  (`debug_skip_resolved_texture_loads_format`, the xenos::TextureFormat
  number):
  - k_24_8_FLOAT (23; the two shadow maps and the scene depth): 1.1 ms;
  - k_16_16_16_16_FLOAT (32): 0.6 ms;
  - k_1_5_5_5 (3): 0.4 ms. This one is a 256x256 texture with 4 mips,
    reloaded after each of its 4 per-mip resolves.
  - k_8 (2): 0.15 ms.

`vulkan_resolve_into_texture` (on by default on macOS, hot reload) lets a
direct resolve (`direct_host_resolve`) also write the texture at its
destination, with the texels reloading the memory would give. The resolve
shader for 32 bits per pixel then stores each texel twice: in the shared
memory as before, and in the texture's storage image. That image is the
r32ui view `vulkan_texture_load_to_image` gives textures on GPU-written
memory, and the texture's endian swap and 24-bit float conversion are done
on the way, as the load shader does them. The texture is then marked up to
date, so it is not reloaded. The texture cache keeps the storage-capable
textures by base page. A resolve picks the most recently used one at its
destination that:

- is 2D, tiled, one mip level and not scaled;
- loads with the 32bpb or depth float shader;
- has the resolve's row pitch;
- is either fully covered by the resolve or already up to date. The resolve
  only writes part of it, so the rest must already be right.

`vulkan_resolve_log` shows, per destination, whether the resolve wrote into
the texture or why not. The texture stats line counts the loads it saved.

At first this saved nothing: the textures the resolves wrote were still
reloaded once per frame. Watches on the shared memory fire per host page,
which is 16 KB on Apple Silicon. A resolve into the second shadow map
(0x12FE1000, 4 MB) therefore also invalidated two textures it does not
touch: the first shadow map, whose last 4 KB share a page with it, and the
640x360 texture starting at 0x133E1000. The mask resolve at 0x12B17000 did
the same to the first shadow map. With 4 KB pages these bases are
page-aligned, so this is a macOS problem.

`shared_memory_exact_gpu_write_watches` (on by default on macOS, hot reload)
fixes it for GPU writes, where the exact range is known: a resolve fires only
the watched ranges (textures) it overlaps byte by byte. CPU writes stay per
page, since memory protection only knows the page.

At the trail, standing still, three alternating triples (`fable_2_395`):

| | GPU time | CP CPU | GPU power |
|---|---|---|---|
| both off | 8.46 ms | 6.43 ms | 2.33 W |
| exact watches only | 8.12 ms | 6.30 ms | 2.30 W |
| both on | 7.40 ms | 6.21 ms | 2.13 W |

That is 17 texture loads per frame written by resolves instead; 14 reloads
per frame after a GPU write are left. The picture is the same: sweep shots
on vs. off differ like off vs. off (~2%, the snow). In the flicker test
(16 shots 250 ms apart, each setting), consecutive shots differ by
1.4-1.7% on and 1.0-1.9% off, brightness flat (`out/ab_paths/flicker_rit/`).

What these direct resolves left were the resolves from the EDRAM buffer
with format conversion (`resolve_full_8/16/32/64bpp`):

- the k_1_5_5_5 256x256 texture with 4 mips at 0x12704000, resolved level
  by level and reloaded after each (4 per frame);
- the 64-bit float textures (0x199E0000, 0x12D33000, 0x12DD5000);
- the k_8 mask at 0x12B17000;
- 0x135D1000, resolved from a 4x MSAA render target.

`vulkan_resolve_into_texture_full` (on by default, hot reload; it only
matters with `vulkan_resolve_into_texture`) gives those resolves a variant
that also writes the texture. Their source isn't in the SDK, only the SPIR-V.
`shaders/resolve_full_*_texture.cs.glsl` is therefore that SPIR-V decompiled
with spirv-cross, unchanged, plus the texture stores at the end, built by
`tools/build_texture_load_image_shaders.sh`. The stores give the texels
what the load into the image would give for the destination's texel size:

- 8 bpp: unchanged;
- 16 bpp: the 8-in-16 swap, and the R5G5B5A1 to B5G5R5A1 swizzle for
  k_1_5_5_5;
- 32 bpp: the swaps, and 24-bit float depth to 32-bit float;
- 64 bpp: the swaps, per 32-bit word.

A resolve can now also write a mip level. The texture cache keeps each
storage texture under the page of each level before the packed mip tail. A
resolve into level N writes that level's storage view, if the mips are
otherwise up to date, and then only the mips are marked up to date
(`MakePartUpToDateAndWatch`).

At the trail: GPU-write reloads 13.9 -> 3.0 per frame, written by resolves
17 -> 28 per frame. The reloads left:

- 0x12CFD000, which the game resolves in two formats (k_8_8_8_8 and
  k_2_10_10_10), so each resolve rightly outdates the other texture;
- the 1280x720 copy at 0x1ECFF000 (since fixed, below).

The 1280x720 copy is the frontbuffer: the presenter fetches it at every
swap (`RequestSwapTexture`). A texture gets a storage image only if its
memory is already GPU-written when it is created. This one was created
before gameplay, and a texture used every frame is never evicted, so it
never got a storage image and was reloaded after every resolve into it.

`vulkan_resolve_into_texture_recreate` (on, hot reload) fixes that. When a
resolve finds no storage texture at its destination, the textures at that
base page that would get a storage image are taken out of the cache (once
per page). They leave the map and the least-recently-used list
(`TextureCache::TakeTextureOutOfCache`), and the Vulkan cache keeps them
until the GPU has completed their last submission. The next request creates
the texture again, now with a storage image, and the resolves write it.

At the trail: reloads after a GPU write 3.0 -> 2.0 per frame, and 29
written by resolves. The GPU time couldn't be measured this time: remote
screen sharing (RustDesk, WindowServer) loaded the GPU, and the frame GPU
time varied 8-42 ms between runs and 10-21 ms within one. The work removed
is one 1280x720 32-bit reload per frame. The 11 reloads removed before saved
0.12 ms, so expect little. `debug_resolve_into_texture_exclude=<address>`
leaves one destination out, to compare within a run when the GPU is quiet.

Same picture:

- 8 alternating sweep shots with the frontbuffer excluded and included:
  the per-channel mean absolute differences are the same (5.96/5.12/4.00 vs
  5.57-5.98/4.87-5.19/3.78-4.09).
- The channel means agree within the noise between excluded shots (up to
  2.2/255).
- Flicker test: 1.4-1.7%, brightness flat (`out/ab_paths/flicker_frontbuffer/`).

The gain is small: GPU time 7.87 -> 7.76 ms per frame, lower in all four
alternating pairs, and GPU power 1.865 -> 1.849 W (`fable_2_402`). These
reloads cost less than the debug skip suggested, and the shader's own
texture writes take back part of what they saved. With both
`vulkan_resolve_into_texture` switches against off: 8.32 -> 7.42 ms and
1.93 -> 1.73 W (`fable_2_401`, three pairs).

Same picture:

- Sweep shots differ on vs off as off vs off (~2% of pixels). Per 40x40
  block, the mean absolute difference on vs off (6.90 per pixel) is no
  larger than off vs off (6.99), and the channel means agree within
  0.1/255, so no colour channel is swapped.
- Flicker test, 16 shots 250 ms apart: 1.4-1.7% between consecutive shots,
  brightness flat (`out/ab_paths/flicker_rit_full/`).
- One earlier attempt moved the hero: with `--merge_controllers`, input from
  a controller used for another app reaches the game. Rerun such a test.

### Resolves inside the render pass (not possible on stock MoltenVK)

The idea: do the direct resolve as a full-screen draw at the end of the
render pass that drew the source, reading the render target as an input
attachment, instead of ending the pass for a compute dispatch. On a
tile-based GPU that would read the tile and save the compute encoder and a
dependency per resolve.

MoltenVK 1.4.1 (the prebuilt the game uses; its source is in
`thirdparty/rexglue-sdk-src/thirdparty/moltenvk`) can't do this:

- It never enables SPIRV-Cross's `use_framebuffer_fetch_subpasses`. An input
  attachment read is a texture read of the image's memory, which is stale
  while the tile is still being rendered.
- On Apple GPUs `MVKCmdPipelineBarrier::encode` therefore ends the Metal
  render pass and begins a new one (`kMVKCommandUseRestartSubpass`) for any
  in-pass barrier to `GENERAL`, or with `BY_REGION` and
  `INPUT_ATTACHMENT_READ`. Metal memory barriers inside render encoders are
  off there (`memoryBarriers = false`).

So an in-pass resolve would store and reload the tiles as now, with a
render encoder in place of the compute one. A gain needs a patched MoltenVK
(framebuffer fetch for input attachments that are also color attachments,
no restart for those barriers), built from source and staged as
`build_sdk_mac.sh -mvk-private` does.

What resolves cost, measured with the GPU free (trail, 60 fps cap,
`debug_skip_resolve_dispatches`, four alternating rounds, `fable_2_503`):

- ~1.35 ms of the frame's 7.35 ms of GPU time;
- the copy dispatches ~1.05 ms (about 23 per frame, ~45 us each);
- the render target dumps for resolves from EDRAM ~0.25 ms;
- the pass ends and barriers only ~0.07 ms.

An earlier run that said ~1 ms for the pass ends had another app using a
quarter to half of the GPU.

### CPU: busy waits

At the default 30 fps the game used 2.3 cores (the whole system's CPU 10.5
W against 3.2 W for the GPU, from `powermetrics`), and almost half of that
was waiting. Two busy waits:

- **Waits for several objects** (`src/core/threading_posix.cpp`). Without a
  native wait for several condition variables, `PosixConditionBase::
  WaitMultiple` polls: lock each handle's mutex, check, sleep
  `min(remaining, 1 ms)`. The remaining time was truncated to whole
  milliseconds, and the alertable waits call it with 1 ms slices
  (`ComputeAlertableWaitTimeout`, itself truncating): after the first check
  less than 1 ms was left, the sleep was `sleep_for(0)`, and the wait spun
  for its whole length, allocating a vector each round. The audio worker
  waits for its client semaphores that way (alertable, 500 ms) and used 79%
  of a core, all in user space, even with audio muted. It now sleeps the
  real remaining time, and the alertable slices round up, so a timed
  alertable wait ends up to 1 ms late (a signal still ends it at once).
- **The game's main loop** (`ProcessGameFrame_82276C30`). It computes from
  the timebase whether a simulation step is due; if not, it calls
  `NtYieldExecution` and checks again. On the Xbox 360 the yield hands the
  hardware thread to others; here it is a `sched_yield` loop that kept
  GameThread 90% busy (27% in the kernel). The override sleeps
  `[perf] game_loop_sleep_us` (250) after the yield.

At the trail: 2.34 -> 1.55 -> 0.82 cores, the process's CPU energy 9.8 ->
5.9 -> 3.1 W, still 30 fps. The frame limiter wait was the first of these
(`[perf] frame_wait_sleep_us`).

Measuring: `tools/mac_thread_cpu.c` reads each thread's CPU time and the
process's energy, instruction and cycle counts from the kernel
(`proc_pidinfo`, `proc_pid_rusage`; no root), over an interval. `sample` at
its default 1 ms interval suspends the process so often that the game used
less than half its normal CPU while sampled, and threads waiting for a core
showed as running; with `sample <pid> 15 10` (10 ms) it agrees with the
kernel's accounting. The frame statistics also log the command processor
thread's CPU time per frame.

### CPU: the command processor, and what didn't help

At the trail, with frame reports off: 0.79 cores, 3.1 W of CPU. By thread:

- GPU Commands (the command processor) 20%, 6.5 ms per frame;
- 3D Engine 14%;
- GameThread 13%;
- two XThreads, 7% each.

**Measuring: turn the frame reports off.** `tools/mac_capture.sh` (and
so `tools/mac_autotest.sh`) asks for a frame report every 20 s. Each one
logs about 2300 draw lines and dumps four resolves, which makes a 200 ms
frame every 20 s and adds CPU time. Pass `--vulkan_frame_report_interval=0`
for CPU and pacing measurements. `mac_autotest.sh` then no longer waits for
a report before quitting.

**Where the command processor's time went** (`sample <pid> 10 5`, running
samples only):

| Share | What |
|---|---|
| 66% | issuing draws |
| 20% | `UpdateBindings`, of which `vkUpdateDescriptorSets` 14% (inside MoltenVK) |
| 13% | MoltenVK encoding at `vkQueueSubmit` |
| 12% | type-0 packets, plus 7% in `WriteRegister` |
| 11% | uploads to the shared memory, mostly `memmove` |
| ~5% | clock reads by the long-frame attribution |

The clock reads only happen with `vulkan_long_frame_log`, which the user's
`fable_2.toml` turns on. Three changes, each with a hot-reload switch, on
by default:

- `vulkan_reuse_texture_descriptor_sets`. Every draw wrote new texture and
  sampler descriptor sets for both stages, even with the same bindings (an
  upstream TODO). A set written earlier in the frame with the same contents
  (stage, counts, image views, layouts, samplers) is now bound again; the
  frame's sets stay allocated until the frame completes. If it is the set
  already bound, the rebind is skipped too. At the trail: 1458 sets written
  per frame -> 282 written and 1176 reused (the CPU stats line shows the
  counts). `vkUpdateDescriptorSets` fell from 14% to 4% of the thread; the
  rest is the constant buffers.
- `type0_register_ranges`. Type-0 packets wrote their registers one call at
  a time. Consecutive registers now go through `WriteRegisterRangeFromRing`,
  as upstream Xenia does. The Vulkan backend copies shader constant ranges
  in bulk there and invalidates their buffers once.
- `frame_timing_fast_clock` (macOS). The long-frame attribution reads the
  clock twice per scope, several times per draw, through `steady_clock`
  (`clock_gettime`, `mach_continuous_time`). It now reads
  `mach_absolute_time`, converted with the cached timebase.

The command processor's CPU time, alternating within one run: 6.92 -> 6.04
ms per frame (`fable_2_424`), and 6.47 -> 5.61 ms in a run that started with
all three off (`fable_2_431`): about -0.9 ms, -13%. One at a time, each
turned off with the others on: without the reuse 6.65 ms, without the
type-0 ranges 6.75 ms, without the fast clock 6.19 ms.

Separate runs vary as much as this change (5.6 to 6.5 ms for the same
settings), so between runs it showed only -0.2 ms (`fable_2_427`-`430`).
Same picture: sweep shots differ like off vs. off (~2%), and the flicker
test is clean (1.4-1.7%, brightness flat, `out/ab_paths/flicker_cpu/`).

Tried, no gain:

- **Thread QoS classes.** All runtime threads are user-interactive, so 99-100%
  of the instructions run on the performance cores. User initiated and
  default (`fable_2_411`, `_412`) measured the same as user interactive
  (0.83-0.86 cores, 3.0 W; `_407`, `_409`). Utility (`_408`) did not move the
  work to the efficiency cores either: 96-98% of the instructions stayed on
  the performance cores, but the GPU time doubled (16-20 ms), the
  command processor took 44% of a core and the process 1.32 cores, 5.4 W.
  Not kept.
- **`-mcpu=apple-m1` instead of `-march=armv8-a`** for the recompiled game
  code. That gives LSE atomics and Apple scheduling; a full rebuild takes
  15 minutes at ~2.4 cores. Three alternating pairs of separate runs
  (`fable_2_416`-`421`): 0.75-0.80 vs. 0.76 cores, 8.5 vs. 8.6 G
  instructions/s, 3.0 W either way, same per-thread shares. Not kept.

What is left:

- MoltenVK's encoding at submission (~14% of the command processor);
- the upload copies (copy-mode shared memory);
- register writes;
- the guest threads, which run game code with a flat profile:
  `ProcessTreeRecursive`, hash lookups, animation interpolation, no spinning.

### CPU: MoltenVK encoding on its own thread

A `sample` of the command processor thread (3245 draws per frame, 60 fps)
showed ~27% of its running time in `vkQueueSubmit`. That is MoltenVK
turning the Vulkan command buffer into Metal encoders, synchronously on the
thread that also parses the guest's command stream. MoltenVK can do that on
a dispatch queue of its own: `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0`. The
app now sets it on macOS (`setenv(..., 0)` in `fable_2_app.h`, so an
exported value wins; export 1 to compare).

At the trail, four alternating runs (`fable_2_507`-`510`):

| | sync (before) | async |
|---|---|---|
| command processor CPU | 5.5-6.6 ms/frame | 4.1-4.4 ms/frame |
| its "submit" time (`vulkan_submission_log`) | 1.5-2.2 ms | 0.2-0.3 ms |
| process | 1.21-1.24 cores, 4.6-4.7 W | 1.19-1.20 cores, 4.7 W |

The work moves rather than shrinks. What it buys is headroom on the thread
every frame waits for.

It changed one thing in the command stream. `vulkan_submit_on_primary_buffer_end`
submits whenever the command processor catches up with the guest's ring
buffer, and running faster it caught up at other points: 2 submissions
per frame instead of 3 (the one after the first 80 draws went). Textures
reloaded after an earlier use in the frame then fell in the same submission
as that use, so their loads couldn't be hoisted. That meant 13-16 inline loads
per frame instead of 4-6, four more render pass ends, and ~0.2 ms more GPU
time.

`texture_load_log` showed what these loads were (`fable_2_512`): ~30 per
frame after a CPU write, every one with the same guest data as at its last
load. These are the small textures sharing 16 KB pages with data the CPU
rewrites each frame ("Why textures reload every frame").
`texture_skip_unchanged_reloads` is now on by default on macOS. With async
submits, alternating within one run (`fable_2_515`):

| | skip off | skip on |
|---|---|---|
| GPU time | 7.53-7.71 ms/frame | 6.53-6.57 ms/frame |
| render pass (re)starts | 30-32 | 23 |
| pass ends for texture loads | 7-8 | 0 |
| texture loads | ~30/frame | 2/frame (resolved ones) |

When it was tried before, it saved nothing measurable: the skipped loads
were small and already hoisted. Now it also saves the inline ones and their
pass ends.

The picture is the same. In 16-shot bursts with the skip on, off and on
again (`fable_2_516`), on vs off differ by 0.35-0.40% of pixels and
consecutive shots of one setting by 0.29-0.36% (the snow). The channel
means agree within 0.1/255.

The thread also blocks on the global `recursive_mutex`. `SharedMemory::RequestRanges`
takes it for every draw, and the recompiled `mtmsrd r13` (the guest's
interrupt-disable around its spin locks, `REX_ENTER_GLOBAL_LOCK`) takes the
same one. That is waiting rather than CPU; `sample` slowed the game to 27
fps and makes it look worse than it is.

### CPU: the FPCR flush-mode switches

Xenon's VMX unit flushes denormals to zero (non-Java mode); its scalar FPU
doesn't. The recompiled code follows that by switching FPCR.FZ around every
change between VMX and scalar FP code (`ctx.fpscr.enable/disableFlushMode`).
A measurement build that counted them (`fable_2_517`, trail, 60 fps) saw
~2.5 M switches per second:

- two thirds where the codegen knew the mode changed within a function;
- one third after calls, where it didn't know.

An FPCR write that changes the mode serializes the pipeline. Keeping FZ on
instead (three alternating pairs) cut the 3D Engine thread's CPU by ~8%
and GameThread's by ~4%, with the same instructions in fewer cycles. That
is ~16 ns per switch, about twice a microbenchmark's 7.8 ns.

`fixed_flush_mode = true` (`[entrypoint]` in `fable_2_manifest.toml`, a codegen
option from the SDK patch) now does that, as RPCS3 does on arm64:

- The codegen emits no switches. The first FP operation after function
  entry or a call emits a conditional `enableFlushMode()`, which only
  writes the FPCR once per thread.
- Scalar FP code therefore also flushes denormals, unlike on the console.
- The hand-tuned overrides in `src/core/hotfunc` route their copies of the
  switch through `HOTFUNC_DISABLE_FLUSH_MODE`, a no-op when the generated
  `fable_2_pch.h` defines `REX_FIXED_FLUSH_MODE 1`.

Same picture: in a still scene at the trail, frames with FZ kept on differ
from normal ones by 0.33-0.39% of pixels, the same as consecutive frames
(0.30-0.36%). Four minutes in the square looked normal.

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

### Frame pacing: idle repaints and the display refresh

`--vulkan_present_pacing_log=10` logs, every 10 s, two things from
VK_GOOGLE_display_timing (MoltenVK fills it from Metal's
`presentedTime`):

- how many display refreshes each guest frame stayed on screen;
- how far apart presents are.

Only the first present of each guest frame counts as a frame: the presenter
tags it with a present ID when the guest output mailbox slot changes.

**Idle repaints.** `ReXApp::LaunchModule` creates the achievement toast
dialog for the whole session. A registered ImGui dialog makes the
`ImGuiDrawer` ask for a repaint after every paint, so the presenter repainted
the same guest frame on the UI thread at about 125 Hz. At 30 fps there were
1200 presents per 10 s for 300 guest frames.

The repaints cost GPU time, and at 60 fps they delayed new frames. 13-33
frames per 10 s stayed on screen twice as long, which shows as stutter.

`AchievementToastDialog` now unregisters itself from the drawer when its
queue is empty and registers again on `Push`. `ReXApp` forwards achievement
events to the UI thread for that. Presents now equal guest frames, and the
game's GPU share at 30 fps went from 24.3% to 18.5%.

**Display refresh.** The guest vblank is a free-running timer at
`video_mode_refresh_rate`. Whether that matters depends on the display:

- **Variable refresh** (the 144 Hz main display here): the display shows
  each frame when it arrives, and the timer is already even (30 fps: 300 of
  300 frames for 33.3 ms).
- **Fixed refresh** (60 Hz): the timer drifts against the refreshes. Per
  10 s, up to 17 frames are shown as a short/long pair (one refresh, then
  three).

`vsync_display_link` handles fixed-refresh displays. It is used when the
refresh rate of the window's display is a whole multiple of
`video_mode_refresh_rate` (or the reverse):

- the guest vblank follows a CVDisplayLink on that display, every Nth
  refresh or N times per refresh;
- each vblank fires `vsync_display_link_phase` (0.5) of a refresh after the
  callback;
- other rates keep the timer, so the frame rate never changes;
- the choice is made again when the window moves to another display.

The window's display comes from `rex::ui::GetPresentationDisplayId()`,
which the SDL window sets.

The phase matters. At 0, the game's flip lands on the display's deadline,
and 34-38 frames per 10 s alternated between one and three refreshes. At
0.5 (or 0.25), 0-2 did. At the 60 fps target on the 60 Hz display, 598-601
of 600 frames stay on screen for exactly one refresh.

### Window resize: MoltenVK leaves the layer at 1x1

After a resize (maximizing, dragging the window edge, moving the window to
another display), the window sometimes stayed black or a flat grey until the
next resize. On the main display this happened about 1 resize in 4; after a
move to the 60 Hz second display, every time. The UI thread was painting, and
it spent all its time in `-[CAMetalLayer nextDrawable]` (in MoltenVK's
drawable fetch, with or without `vulkan_fetch_drawable_before_submit`).

`MVKSwapchain::initCAMetalLayer` (MoltenVK 1.4.1) sets the layer's
`drawableSize` to the new extent. It then compares the new extent with the
surface's extent, which is that same `drawableSize` and so always equal. If
the old swapchain still has unpresented images, it sets `drawableSize` to
1x1, a workaround for presentations that never complete. Nothing sets the
size back, so the new swapchain presents 1x1 drawables, stretched over the
window. SDL's Metal view sometimes resizes the layer again on its own, which
is why only some resizes broke.

With `vulkan_fix_layer_drawable_size` (on), the presenter reads the layer's
`drawableSize` after creating a swapchain. If it differs from the swapchain
extent, the presenter logs `CAMetalLayer drawableSize 1x1 after creating the
WxH swapchain` and sets it to the extent. The line appeared after every
creation in the test, and since the fix 30 of 30 resizes on both displays
show the picture.

### Uploads of pages read earlier in the submission

Measured in the Bowerstone square at the 60 fps cap, in the crowd at
Murgo's wagon (about 4400 draws).

**What the pass timer showed.** `vulkan_pass_gpu_time` found 49
depth-only passes per frame into a 1040x2528 target, with about 6 draws
each. The trail has none of these.

**Why the pass was split.** The "Render pass ends per frame" line counted
40 shared memory uploads per frame, against 0 at the trail.
`vulkan_hoist_shared_memory_uploads` moves an upload to the start of the
submission only if nothing earlier in the submission used its pages.
Otherwise the upload would overtake those reads. In the square the CPU
writes vertex data into pages that earlier draws have already read. So
each of those uploads stayed in the command stream and ended the render
pass.

**What changed.** `vulkan_hoist_uploads_of_read_pages` (on by default on
macOS, hot reload) hoists such an upload when the reads it would overtake
cannot tell the difference:

- **What it records:** for each page, the span of bytes read in the
  submission. Reads come from `MarkBytesUsedInSubmission`. The first and
  last page of a read get a byte span; the pages in between are read
  whole, which a bitmap records.
- **What it keeps:** the staged data of the page's latest hoisted upload
  in the submission. That copy stays mapped until the submission
  completes.
- **The check:** a new upload of a page that was read is hoisted if the
  page was not GPU-written in the submission, it has an earlier hoisted
  upload to compare with, and the read bytes are equal in the two copies.
  The data is now staged before this decision.

The rule is safe because both hoisted copies run first and the later one
wins. So the earlier draws now see the new copy, which is identical in the
bytes they read. By induction this holds across any number of re-uploads
in one submission.

An upload that fails the check stays where it was. A page with an upload
in the command stream counts as GPU-written, so no later upload of it
overtakes that one.

Counts per frame are in the log line "Uploads of pages read earlier in
the submission":

- about 45 hoisted;
- about 4 kept because the page was GPU-written;
- about 2 kept because there was no earlier hoisted upload to compare
  with;
- 1.5-1.9 kept because the read bytes changed. Those would have shown
  wrong vertex data with `debug_hoist_used_page_uploads`, which skips the
  check.

**Results** (alternating within one run):

| Spot | GPU per frame, off -> on | Render pass restarts per frame |
| --- | --- | --- |
| 4388 draws | 12.7-12.9 -> 10.3-10.4 ms | 56 -> 27 |
| 4670 draws | 13.5-14.0 -> 11.1-11.9 ms | 63-69 -> 40-43 |

- In frames timed by the pass timer, the depth-only passes merge to about
  6 per frame, and the time outside render passes drops from 8.2 to 5.6 ms.
- Command processor CPU stays the same.
- The picture is the same, including a 16-shot burst.

**When the reads saw an earlier submission's upload.** About 1-10 uploads
per frame (it varies a lot) still stayed in the command stream, each
cutting the main scene pass. In these the page had been read earlier in
the submission, but its content came from an upload in an earlier
submission, so there was no staged copy to compare with.
`vulkan_hoist_uploads_of_read_pages_cache` (on, hot reload) keeps a copy
of such pages:

- A page that had nothing to compare with is marked. Its later uploads are
  copied into a 16 MB cache (1024 pages, reused round robin; the game's
  dynamic vertex pages are uploaded every frame).
- Each page has a content generation. Every upload and every GPU write to
  the page increments it. A cached copy is used only while its generation
  is the page's current one, so it is what the GPU buffer holds.
- The next upload of the page compares the read bytes with that copy, as
  above.

With it, no upload in the square stays in the command stream. The
"GPU-written page" cases those uploads caused go away too. Restarts drop
by 2-8 per frame, and GPU time is about 0.7 ms lower per frame (four
alternating pairs; the crowd was moving and other apps used the GPU).

**What is left on the GPU in the square:** about 39 resolves (3.1 ms in
timed frames), 47 passes with at most two draws (3.0 ms), and the main
scene pass (about 2 ms).

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
