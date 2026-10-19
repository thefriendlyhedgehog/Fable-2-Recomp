# Fable 2 Recompiled - notes for Claude Code

ReXGlue (Xenia-derived) static recompilation of Fable II. The app is in
`src/`, the recompiled guest code in `generated/` (from `rexglue codegen` on
`fable_2_manifest.toml`), the GPU emulation in the ReXGlue SDK.

## Start here

- **macOS port (active work):** `plans/macos-port-handoff.md` - current
  state, open problems with evidence, debug switches, how to build, capture
  and change the SDK on the Mac, and what to do next. Background for every
  SDK change: `docs/macos_port.md`.
- Building on Windows and general notes: `README.md` ("Building",
  "Vulkan renderer", "Function naming rule").
- Guest-image patches: `docs/patches.md`; hero/dog texture work:
  `plans/hero-dog-*.md`.

## macOS quick reference

- Repo on the Mac: `/Volumes/MacMiniExternal/Games/fable2/Fable-2-Recomp`;
  game content in the repo root (`--game_data_root="$PWD"`).
- Build: `tools/build_sdk_mac.sh && ./build.sh -release fable_2`.
- Capture and summary: `tools/mac_capture.sh --merge_controllers`
  (upload and texture load hoisting are on by default on macOS;
  `--vulkan_hoist_shared_memory_uploads=false`, `--vulkan_hoist_texture_loads=false` to compare)
  (summary in `out/capture/summary.txt`; analyzer `tools/analyze_mac_capture.py`).
- Render target path: `fbo` by default on macOS since 2026-10-15 (a third
  less GPU time than `fsi`); `--render_target_path_vulkan=fsi` to compare,
  `tools/mac_ab_paths.sh` for an fps comparison.
- SDK changes go into `thirdparty/sdk_mac_vulkan_fixes.patch`: edit
  `thirdparty/rexglue-sdk-src/{src,include,resources}`, then `tools/sdk_patch_refresh.sh`,
  then rebuild. Never edit the patch file by hand.
- Offline shader translation checks: `tools/sdk_offline/README.md`.

## Conventions

- Keep `config/fable2_config.toml` and the template in
  `src/core/fable2_config.cpp` identical.
- The game builds against the SDK's prebuilt headers: do not change the
  layout of SDK classes the app uses (append members only).
- `.gitignore` ignores `*.sh`, `*.py` and `*.png` by default; new tools need
  a `!tools/<name>` line.
- Commit messages: what changed and why; no model names or IDs.
- When asking the user to run something, show the exact command in its own
  code block, starting with `cd <repo> && ...`, output commands ending in
  `| tee /dev/tty | pbcopy`, and no `#` comments inside commands (zsh).
