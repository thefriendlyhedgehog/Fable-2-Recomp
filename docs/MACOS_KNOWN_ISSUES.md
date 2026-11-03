# macOS known issues (Apple Silicon)

Status as of 2026-11-03: the port is **playable and in testing**. The game
holds its 60 fps cap in normal play and heavy views, saves and quits cleanly,
and the Steam Controller works. The closeout work is verification of the play
behaviours below, documentation, and packaging (`plans/macos-closeout-plan.md`).
Evidence and history: `plans/macos-port-handoff.md`, `docs/macos_port.md`.

## Requirements

- Apple Silicon Mac, macOS 13.3 or newer. No Intel/universal build.
- The SDK must be built from source with this project's Mac patch
  (`tools/build_sdk_mac.sh`). The prebuilt SDK fallback lacks the Mac render
  fixes and the host resolve-readback path, so the hero/dog textures would be
  black and several debug switches would not exist.
- Full Xcode install to build (MoltenVK is built with `xcodebuild`).
- Your own Fable 2 GOTY (USA/Europe) content. Nothing is redistributed here.

## Known issues and workarounds

- **Prologue: Lord Lucien's glowing circle cannot be entered with the
  tick-rate patches on.** The hero cannot climb the raised step at doubled
  ticks. Workaround: open F4 and turn `Game: high_tick_rate` off for that
  scene (the switch takes effect live), then turn it back on afterwards. The
  cause is narrowed to the LF tick; the investigation is parked
  (`docs/patches.md`).
- **Golden quest trail can flicker** (report from 2026-10-23). Not reproduced
  at the save used for the hero/dog check; cosmetic, no workaround. Record the
  spot and log if it happens.
- **The dog shows small flickering light rectangles** on its back/side at 1x
  render scale. Present with both texture paths; cosmetic, not fixed.
- **Loading hitches of 300-800 ms** on area loads, and one 52 s save load was
  seen once (repeat loads are about 19-20 s). Not investigated; keep saves
  backed up.
- **Hero/dog black textures** are fixed on macOS by the CPU readback default
  (`[patches] hero_dog_texture_readback = true`). Realtime texture morphing
  builds the full-size texture correctly but leaves every smaller mip black on
  the Mac, so do not turn the readback off on macOS
  (`plans/hero-dog-realtime-texture-morphing.md`).
- **Xbox pop-ups** (message boxes, on-screen keyboard) are ImGui windows
  answered with the mouse; while one is open the keyboard and mouse go to it,
  not to the game. The game opens them from seven places, and not every one
  has been exercised in testing.
- **Controller after Mac sleep/wake** has not been verified: if the controller
  stops driving player 1 after wake, restart the game and report it.
- **Audio** is untested: the developer's config starts muted
  (`audio_mute = true` in `fable_2.toml`). Remove that line to unmute; report
  stutter or desync.
- **Function keys:** Mac keyboards need `fn` for F3/F4/F7 unless "Use F1,
  F2, etc. keys as standard function keys" is on. The debug keys are also on
  6-9 and 0, and the overlays are F3 (debug), F4 (settings), Backtick
  (console), F7 (achievements).
- **No macOS launcher or `.app` bundle yet:** launch with `tools/run_mac.sh`
  from the repository root and set player options in F4. The Windows WPF
  launcher and its ISO extractor are Windows-only; on macOS use any Xbox 360
  ISO extractor to get `default.xex` and `data/`.
- **Windows build of the Mac changes is untested:** the overlay input path,
  pop-up capture, live tick-rate switch and SDK patch have not been compiled
  on Windows. The Windows build keeps its own working code paths.

## Not issues

- The debug overlay (F3), settings overlay (F4), console and achievements
  overlay are developer/player tools; leaving them closed is the normal game.
- `audio_mute = true` in the developer's `fable_2.toml` is intentional.
- Performance work is parked on purpose: the 60 fps cap is held in normal
  play and heavy views, and the remaining candidates are small.
