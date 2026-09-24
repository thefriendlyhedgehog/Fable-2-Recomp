# macOS port

**Status:** builds and runs on an M4 Mac mini (macOS, Xcode beta toolchain,
SDK v0.10.0). With FSI render targets the boot logos, menus and new-game flow
work and gameplay starts (audio, cutscene, tutorial). Open problems: in-game
graphics corruption, low frame rate, and wonky stick input in menus.

## Prior work

- The only public Mac attempt is by AARosson48 (M4, Sept 2026), reported in
  [himdo/Fable-2-Recomp#9](https://github.com/himdo/Fable-2-Recomp/issues/9),
  [Fable2Recomp/Fable2Recomp#23](https://github.com/Fable2Recomp/Fable2Recomp/issues/23)
  and [rexglue/rexglue-sdk#446](https://github.com/rexglue/rexglue-sdk/issues/446).
  They got intro menus and loading working after reworking the keyboard
  module; terrain rendered see-through. Their code is not public.
- ReXGlue SDK **v0.10.0** is the first release with macOS support (Vulkan on
  MoltenVK) and ships prebuilt `mac-arm64` / `mac-amd64` SDKs. This project is
  pinned to v0.10.0 (the source pin `f5337cd` is the `v0.10.0` tag).

## Building on a Mac

Requirements: macOS 13.3+ (the prebuilt SDK's minimum), Xcode 15+ command
line tools, CMake >= 3.25, Ninja, and your own copy of the game content (see
the README).

```sh
xcode-select --install          # clang, if not already installed
brew install cmake ninja
./build.sh -release fable_2     # fetches the SDK, runs codegen, builds
tools/stage_content.sh . out/build/mac-arm64-release
out/build/mac-arm64-release/fable_2
```

- `tools/setup_sdk.sh` downloads the prebuilt SDK for the host
  (`mac-arm64`, `mac-amd64` or `linux-amd64`) into `thirdparty/rexglue-sdk/`.
  `REXGLUE_SDK_VERSION=<x.y.z>` selects a later release; versions before
  0.10.0 are refused.
- `build.sh` mirrors `build.cmd` (`-release`/`-r`, target argument). On a
  fresh checkout it runs `rexglue codegen` once to create
  `generated/rexglue.cmake`, which `CMakeLists.txt` includes. `CC`/`CXX`
  override the preset's compilers.
- The SDK's CMake helpers stage `librexruntime.dylib`, the GPU plugin, and
  MoltenVK + its ICD next to the executable, and the runtime points the Vulkan
  loader at them itself; no launcher script is needed.
- Vulkan is the only GPU backend on macOS; the default `xenos` plugin selects
  it automatically.

## What changed for non-Windows hosts

- **Keyboard/mouse input** (`src/input/keyboard_gamepad.h`): the driver
  polled Win32 (`GetAsyncKeyState`, cursor recentering), so off Windows the
  keyboard did nothing. It now also listens to the SDK window's key and mouse
  events and uses SDL relative mouse mode for mouse look. Same
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

## Open graphics issues

What the SDK's Vulkan backend already handles on MoltenVK (from the v0.10.0
source):

- No geometry shaders: quad lists are converted to triangle lists and
  point/rectangle lists are expanded in the vertex shader
  (`vulkan_require_geometry_shader` defaults to false on macOS).
- `Metal does not support disabling primitive restart` (rexglue-sdk#446):
  MoltenVK warns whenever a strip is drawn with restart disabled. Metal
  always restarts on index `0xFFFF`/`0xFFFFFFFF`, which only matters if a
  game uses that value as a real vertex index. Likely noise, not the cause.

Likely cause of the see-through ground: **depth format**. Apple GPUs have no
`D24_UNORM_S8`, so the game's 24-bit depth buffers are stored as
`D32_SFLOAT_S8` (`GetDepthVulkanFormat` in `render_target_cache.cpp`). The
values no longer match what the game expects when depth is reused or copied
between render targets, which shows up as surfaces failing the depth test.

### Results on an M4 Mac mini

- Default render target path (`fbo`): audio plays, the screen only flashes
  white. MoltenVK reports no `D24_UNORM_S8`, so the game's 24-bit depth is
  stored as `D32_SFLOAT_S8`.
- `render_target_path_vulkan=fsi`: logos and menus render, a new game starts.
  MoltenVK exposes fragment shader interlock on Apple Silicon. The app now
  defaults to this on macOS (`REX_RENDER_TARGET_PATH_VULKAN=fsi`, set in
  `Fable2App::OnPostInitLogging` unless already set); pass
  `--render_target_path_vulkan=fbo` to compare.
- MoltenVK's own primitive-restart warning goes to stderr on every strip draw,
  so the app also defaults `MVK_CONFIG_LOG_LEVEL=1` (errors only) on macOS.
- The guest arena is mapped at `0x7000000000` on macOS (not `0x100000000`),
  confirming the probes must not hardcode the Windows base.
- Device limits worth watching: no geometry shaders,
  `maxPerStageDescriptorSamplers: 16`, no sparse binding (512 MB shared
  memory buffer).

For remaining corruption, check `logs/` for pipeline creation failures and run
with `MVK_CONFIG_LOG_LEVEL=3` for MoltenVK's view.
