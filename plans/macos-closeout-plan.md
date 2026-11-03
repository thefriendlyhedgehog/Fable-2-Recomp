# macOS closeout plan (2026-11-03)

**Playable today; not yet closed up.** The port runs the game at its 60 fps cap
on this Mac and the user has been playing it with a Steam Controller since
2026-10-23. There is no proven defect blocking play. What is left is (1)
*verification* of the play behaviours nobody has exercised yet, (2)
documentation and config truth, and (3) the Mac packaging work that turns
"clone + terminal + hand-written script" into "double-click and play".

Current state and history stay in `plans/macos-port-handoff.md` (canonical)
and `docs/macos_port.md` (background and reasoning). This file is the closeout
plan only; it does not re-open the parked performance work.

## 0. What "closed up and playable" means here

Two different bars, deliberately:

**Playable (already true, dev workflow).** With content in the repo root and
`./run_and_watch.sh`, the game boots, loads a save, holds 60 fps in normal
play and heavy views, and quits cleanly. The user is doing this.

**Closed up (the target).** All of:

1. A full playthrough on macOS at default settings, from current saves, with
   no crash outside the documented known issues.
2. The unverified user-facing behaviours are exercised: controller across
   sleep/wake, cursor at rest, the game's Xbox pop-ups, saves/loads, audio
   (if the user wants audio on).
3. Launch needs no terminal and no repo checkout knowledge: a double-clickable
   `Fable 2 Recompiled.app` (or, if the user prefers, one tracked command),
   with settings, saves and logs in a predictable place that survives quit,
   relaunch and rebuild.
4. A Mac owner with their own GOTY ISO has a documented, working setup path:
   extract `default.xex` + `data/`, verify the hash, point the game at it
   (no Windows-only launcher step).
5. The public repo states the Mac status: quickstart, known issues, required
   hardware/OS, a release tag built from `master`, and no stale "open" claims.
6. Game content is never redistributed; existing Windows workflow untouched.

## 1. Where things stand (verified 2026-11-03, read-only audit)

- `HEAD` = `7a25a69` (2026-10-23), clean tree, branch
  `claude/elegant-ride-r7ako0`. `origin/master` also points at `7a25a69`
  (the Mac work is already on GitHub's default branch); only the *local*
  `master` is 5 commits behind (`cf1c118`) — nothing to merge.
- The binary in `out/build/mac-arm64-release/` was built from HEAD's code
  (no source file is newer; the last three commits' source edits landed
  before the 16:32 link). A HEAD-binary run exists: `out/hero-test/007`
  (85 s, save load, clean exit, `hero_dog_texture_readback=true`, adult
  hero/dog not black).
- Last long play run (`fable_2_631`, 2026-10-23, 7 m 41 s): sustained
  `60.0 fps, longest frame 18-19 ms, 0 frames over 40 ms`, clean window
  close. It ran the *pre-fix* binary (readback off), which is the run that
  reported the black hero/dog; the readback default was fixed right after.
- No crash reports (`*.ips`, `out/` dumps) since 2026-10-20. The last crash
  (`fable_2_622`, Start press) is user-confirmed fixed.
- Build is healthy: SDK source = `v0.10.0` + the Mac patch reverse-applies
  cleanly (98 files), prebuilt SDK `0.10.0`, codegen skipped, Xcode
  (beta, external volume), cmake/ninja present, 444 GB free.
- Machine state is not reproducible from git alone: SDK source/prebuilt,
  `out/`, generated TUs and the 6.5 GB content are all untracked. A fresh
  Mac must run the documented two-stage build (see the handoff, "Building
  and running"), and a fresh session must not assume the build dir exists.
- Performance is parked by decision (`fable2-stop-at-crumbs`): the cap is
  held, and the remaining candidates are ~0.1 ms / ~1% items. Do not
  re-open it for closeout.
- Packaging does not exist: no `.app`, `Info.plist`, `.icns`, dmg, codesign,
  notarization or install target anywhere; `launcher/` is Windows WPF only.
  Repo has no releases, no tags, and GitHub Issues are disabled.

## 2. Open items

### 2a. Must verify before calling it done (play-test items)

| Item | Current state | How to verify |
|---|---|---|
| Xbox pop-ups (message boxes, on-screen keyboard) | ImGui windows implemented; input hold added 2026-10-22; the game opens them from 7 places, none triggered in a test | Find the `xeXamIsUIActive` / message-box call sites, trigger each; window shows, mouse answers it, keyboard/mouse do not leak to the game |
| Controller after Mac sleep/wake | Reported, not investigated; no doc beyond one line | Sleep with controller connected, wake, check player 1; restart the game if needed and log the result |
| Cursor hides at rest (controller, `mouse_look = false`) | Implemented; documented in README but never confirmed in the handoff | Play with controller; pointer rests → cursor hides, moves → shows, never sticks |
| Saves/load stability | One 52 s load observed once; repeat loads ~19-20 s | Load the same save 3-5 times, note times and any hitch; back up saves first |
| Hero/dog textures on the user's real save | Checked only in a copy (`out/hero-test`) | Play the real build with `hero_dog_texture_readback = true`; face/hands/dog correct at normal distance |
| Golden quest trail flicker | New report 2026-10-23, not reproduced at that save's spot | Play the first quest trail area; if it reproduces, record the report and log |
| Audio | `audio_mute = true` is the user's intentional setting, never tested unmuted | Only if the user wants it: unmute, play, note stutter/desync; otherwise leave as is |
| One load crash with `vulkan_texture_load_to_image` on (`fable_2_197`) | Open/unverified, never repeated | Not reproducible on demand; A/B with the switch off only if it recurs |

### 2b. Known cosmetic / non-blocking

- Dog shows small flickering light rectangles on its back/side; present with
  the old readback method too, not caused by the morphing change.
- Loading hitches 300-800 ms; five still-locked sections "aren't busy".
- `fbo` vs `fsi` ground/shadow difference not compared against a console
  screenshot.
- In-pass resolves (needs a patched MoltenVK, ≤ ~1 ms GPU) — parked.
- Lucien-circle step climb with the tick-rate patches on — parked, covered
  live by F4 `Game: high_tick_rate` → off for that scene. Do not dig into
  this for closeout.

### 2c. Doc/config drift (fix during closeout)

These are real contradictions found by the audit, not new work:

1. `docs/macos_port.md` "Known gaps" still lists flickering dark shadow
   areas as open; the handoff says fixed 2026-10-14 and the enabling switch
   is on by default since then.
2. Upload hoisting: handoff "Fixed on this branch" says fixed + on by
   default, while the older paragraph in the same file and
   `docs/macos_port.md` say it is opt-in and causes flicker.
3. `docs/macos_port.md` "Performance (open)" heading is stale (performance
   is parked at the cap); its intro still says the `fsi` render path is
   what the game plays on (`fbo` has been the macOS default since
   2026-10-15).
4. The handoff header still says "state as of 2026-10-21, branch master";
   body and tree are 2026-10-23 on `claude/elegant-ride-r7ako0` (=
   `origin/master`).
5. `plans/hero-dog-realtime-texture-morphing.md` declares realtime morphing
   "implemented, verified" while its own macOS section says macOS defaults
   to the CPU readback; `hero-dog-texture-readback.md` calls itself a
   fallback but is now the active macOS path.
6. README feature list is Windows-first: "Vulkan support [ ]" is wrong for
   Mac (Vulkan is the only Mac backend), "Linux builds [ ]" is noise,
   "Custom Launcher / ISO extractor [x]" is Windows-only, and
   "Saves live in `<build dir>\saves\`" is Windows wording.
7. README has no Mac quickstart at all: no `out/build/mac-arm64-release`
   path, no `run_and_watch.sh`, no content-root explanation, no controller
   setup, no known-issues pointer. Mac notes sit only in the "Building"
   section.
8. The staged `out/build/mac-arm64-release/fable2_config.toml` is a
   pre-HEAD copy (still has the removed `fps_60` key, lacks
   `skip_intro_videos`, `disable_motion_blur`, `dog_fur_depth_bias`) because
   CMake stages it copy-if-missing. Its *values* are the tested play
   values; only the template keys are stale.
9. Handoff "Next, when this is picked up again" is all perf/RE and its
   first entry (busy square) is already measured — it predates the closeout
   phase and should point here instead.

### 2d. Explicitly out of scope for Mac closeout

- Windows build of the Mac changes (overlay input, pop-up capture, live tick
  switch, SDK patch) — parked; nobody has compiled it on MSVC.
- Zero-copy shared memory, upstream PRs #490/#501, MoltenVK 1.4.3, remaining
  Xenia patches, fast-math, Intel/universal build — all optional, measure or
  add only if the user asks.
- Porting the WPF launcher. The F4 overlay already covers player settings;
  only the first-run "find/extract content" flow needs a Mac front end.

## 3. Closeout plan

Ordered so the cheap, visible work lands first and the risky work (bundle,
data relocation) is validated by a play test before release.

### Phase 0 — Verification (S, one session; do this first)

0.1 Back up, then smoke-test the HEAD binary: copy
    `out/build/mac-arm64-release/{fable2_config.toml,fable_2.toml,saves/}`
    to a scratch dir, run `./run_and_watch.sh`, load a save, quit. Pass:
    `[native-build] configuration=Release linked_sdk=source-built`, XEX hash
    accepted, `hero_dog_texture_readback=true`, 60 fps in play, clean
    `Window closing` → `Execution complete` exit.
    *Work in a copy of the build folder for tests that may write saves (as
    was done for `out/hero-test`); never risk the user's saves.*
0.2 Exercise §2a: pop-ups (all 7 sites), controller sleep/wake, cursor at
    rest, 3-5 save loads, real-save hero/dog check, golden trail.
0.3 Write the results into `plans/macos-port-handoff.md` "Resume here" (one
    line each: verified / still open / not reproducible), and record anything
    new in `docs/macos_port.md`.
0.4 Decide whether the remaining cosmetic items block "done". Default: they
    do not; list them as known issues.

### Phase 1 — Docs, config truth, release hygiene (S)

1.1 README Mac quickstart: requirements (Apple Silicon, macOS 13.3+, full
    Xcode for the source SDK, cmake/ninja), content placement, build,
    launch, where saves/logs/config live, controller/`merge_controllers`,
    known-issues link. Fix §2c items 6-7.
1.2 Track the launch path: add `tools/run_mac.sh` (the same logic as the
    user's `run_and_watch.sh`) with a `.gitignore` exception, and point the
    README at it.
1.3 New `docs/MACOS_KNOWN_ISSUES.md`: Lucien-circle workaround, golden trail
    (if confirmed), dog light rectangles, loading hitches, Windows-untested
    note, required source-built plugin.
1.4 Reconcile the staged config template (§2c-8) **without changing the
    user's tested values**: keep `realtime_texture_morphing=false`,
    `hero_dog_texture_readback=true`, `game_msaa=1`, `merge_controllers=true`,
    `mouse_look=false`, `higher_hf_tick_rate=true`, `high_tick_rate=false`;
    add/remove only keys so it matches HEAD's schema. Back up first.
    Also decide whether CMake should refresh the template on build (it
    currently never does) or the game should merge in new keys.
1.5 Fix the stale claims in §2c items 1-5, and make the handoff "Next" list
    point here.
1.6 Repo/release: fast-forward local `master` (`git fetch origin` then
    `git checkout master && git merge --ff-only origin/master`), enable
    GitHub Issues (or state a contact path), add a release tag (e.g.
    `v0.1.0-macos`) from `master` with release notes built from the handoff
    "Resume here". No game content in the release; binaries/dmg only if the
    user wants them.
1.7 Cleanup candidates (audit first, do not delete yet):
    `../fable2-partial-stage` (a lone `default.xex`, not playable),
    `out/build/mac-arm64-release/logs` (4.9 GB / 213 files), the 10 GB of
    old traces already trashed on 2026-10-21. Propose sizes and let Kevin
    confirm.

### Phase 2 — Content setup on a Mac (S/M)

2.1 Document/add a macOS extraction path. Checked options: the sibling
    `../extract-xiso` builds and runs on this Mac as an arm64 binary and
    handles Xbox 360 discs (XGD1/XGD3); the project's own
    `tools/x360extract` is cross-platform .NET 8 but only has a Windows
    build script and no `dotnet` is installed here. Cheapest: a tracked
    `tools/extract_iso_mac.sh` that builds extract-xiso, extracts, then
    verifies `default.xex` against the supported-version hash
    (`685a0d3b…`, README) and prints the follow-up content-root step.
2.2 Verify the extraction once against the existing content tree
    (`default.xex` bytes + `data/` manifest), then document it as the
    supported Mac route. Do not redistribute content; the script takes the
    user's own ISO path.
2.3 First-run content UX: the missing-content path is a terminal-worded
    modal then `exit(1)`; add Mac/bundle wording and, if cheap, an SDL
    folder picker (`SDL_ShowOpenFolderDialog`) that persists
    `game_data_root`.

### Phase 3 — Double-clickable app (M, medium risk; do after Phase 0)

Implemented 2026-11-03 as a packaging step instead of a CMake bundle target:
`tools/package_mac_app.sh` builds `out/dist/Fable 2 Recompiled.app` from the
flat build, so the dev build and the user's `run_and_watch.sh` are untouched.
The app treats an executable under `*.app/Contents/MacOS` as bundled and keeps
content, saves, cache, config and logs next to the `.app`; the SDK still gets
its Vulkan runtime from `Contents/Resources/vulkan`.

Most plumbing already works: the exe's rpath is `@executable_path`, the
SDK stages its dylibs next to the exe, and MoltenVK lookup already knows
bundle layouts. The hard part is relocating mutable state out of the bundle
before signing.

3.1 `MACOSX_BUNDLE` for `fable_2` + `Info.plist` (`CFBundleExecutable
    fable_2`, `LSMinimumSystemVersion 13.3`, `NSHighResolutionCapable`,
    bundle id `com.thefriendlyhedgehog.fable2recomp`); `assets/app-icon.icns`
    generated from `assets/app-icon.png` (`sips`/`iconutil`, replacing the
    Windows `tools/create_app_icon.ps1` path for Mac).
3.2 Make staging bundle-aware: `fable2_config.toml` currently stages beside
    the executable's *build* dir, and `build_sdk_mac.sh`/`build.sh` assume a
    flat `out/build/mac-arm64-release`; teach them `Contents/MacOS` (and
    `Contents/Resources` for docs/icons).
3.3 Data relocation (the risky one, do it before any codesigning):
    - content root: add a "parent of the `.app`" fallback for
      `game_data_root` (keep `--game_data_root` and the `fable_2.toml` key
      winning), so `Fable 2 Recompiled.app` next to `default.xex` + `data/`
      just works;
    - saves/cache/logs/config: move outside the bundle (a portable folder
      beside the `.app`, or `~/Library/Application Support/Fable 2 Recomp`);
    - migrate the existing `saves/` by **copy**, never move; verify a save
      loads after the move;
    - verify launch after ad-hoc signing (`codesign --force --deep --sign -`,
      then `codesign --verify --deep --strict`): a bundle that rewrites
      itself on first run breaks its seal and can fail on arm64.
3.4 Terminal-free finish: verify Cmd+Q and window close under Finder launch,
    capture logs beside the app, and optionally add a Quit row to F4
    (README already lists "close the game" as missing). Gate the Windows
    README/`*.cmd` staging to WIN32 so the Mac app does not ship them.
3.5 Packaging (only if the user wants to hand it to someone): `hdiutil`
    dmg containing the `.app` + Mac README/known issues, **no game content**,
    Gatekeeper right-click-open note for an un-notarized build. Developer ID
    + notarization + hardened runtime is a separate decision (needs a paid
    cert; private-API MoltenVK is fine for notarization, not the App Store).

### Phase 4 — Acceptance and release (user play time)

Play folder, ready 2026-11-03: `/Volumes/MacMiniExternal/Games/fable2/Fable2-App/`
— `Fable 2 Recompiled.app` plus hardlinked content, the dev build's saves
copied in, and the tuned `fable2_config.toml`. Double-click the app.

4.1 Play from the current save (or a fresh one) at default settings, 60 fps
    target. Watch for the §2a items: Xbox pop-ups (mouse answers, keys don't
    leak), controller after sleep/wake, cursor at rest, repeated save loads,
    the golden trail, hero/dog textures at normal distance. Note every crash
    or soft-lock with the time and the log (`Fable2-App/logs/fable_2.log`,
    rotated) and the save folder (`Fable2-App/saves`). Quit and relaunch once
    to confirm saves/settings survive; confirm the app bundle is untouched and
    `codesign --verify --strict` still passes.
4.2 Fix what the pass turns up; update `docs/MACOS_KNOWN_ISSUES.md` with what
    was actually seen.
4.3 Tag the "closed up" release from `master` (e.g. `v1.0.0-macos`) with
    notes built from the handoff "Resume here", then close the memory/handoff
    state (what "done" meant, what stayed parked).

## 4. Start here (next session, in order)

**Progress log.** 2026-11-03: P0 smoke test passed on the HEAD binary in
`out/hero-test` (clean save load to gameplay to quit, readback default active,
no errors); gameplay measured 57.0-58.5 fps, GPU 17.2-17.4 ms/frame at 99%
busy, 0 frames over 40 ms over six 300-frame windows (`fable_2_008`). That is
~1.3 ms/frame above the pre-readback-default `fable_2_631`; even pacing and
below the crumbs threshold, so one quiet re-run instead of profiling. The
hands-on items (§2a) remain open; the user's play config and saves were
backed up first to `out/backup-20261103-play/`.

P1 (2026-11-03): README now has a macOS quickstart and corrected
Windows-only/stale feature lines; `tools/run_mac.sh` is tracked (with the
`.gitignore` exception); `docs/MACOS_KNOWN_ISSUES.md` added; the stale claims
in `docs/macos_port.md`, both hero-dog plans and the handoff's "Next" list
are corrected; the staged play config was rebuilt from the current template
with the tested values preserved (`fps_60` dropped, the `[perf]` and
`skip_intro_videos`/`disable_motion_blur`/`dog_fur_depth_bias` keys added).
Still to do in P1: push/tag/release and repo settings (need Kevin's call).
The reconciled config and `tools/run_mac.sh` were boot-checked: the game
loaded the new config with no unknown-key warnings, seeded the readback and
reached the title screen (`fable_2_632`).

P2 (2026-11-03): `tools/extract_iso_mac.sh` added and tracked — extracts
`default.xex` + `data/` with extract-xiso (building it under `out/tooling/`
on first use), defaults the destination to the repository root, refuses to
overwrite existing content without `--force`, and checks `default.xex`
against the supported list in `constants/game_versions.h`. Verified: the
argument/refusal/failure paths, and the fresh-user build path (clone + cmake
build of extract-xiso v2.7.1 arm64, then a clean failure on a non-Xbox
image). The real-disc path is still unverified — there is no Fable 2 ISO on
this Mac; run it once on Kevin's ISO before calling it supported.

P3 (2026-11-03): `tools/package_mac_app.sh` + `assets/macos/Info.plist` +
`assets/app-icon.icns` build an ad-hoc-signed `out/dist/Fable 2 Recompiled.app`
from the flat build (CMake and the dev workflow unchanged). The app detects an
executable under `*.app/Contents/MacOS` and keeps content, saves, cache,
config and logs next to the `.app`; the Vulkan runtime is under
`Contents/Resources/vulkan`. Verified: direct launch from `/` and a Finder
`open` both reach the title screen, state is created next to the app, the
bundle holds only binaries, and `codesign --verify --strict` still passes
after runs. Remaining for distribution: Developer ID signing + notarization,
a dmg, and the Phase 4 playthrough.

1. Phase 0.1 + 0.2: back up the play config/saves and run the verification
   list. This is the only work that can change the plan's conclusion.
2. Phase 1.1-1.3: README Mac quickstart, tracked `tools/run_mac.sh`,
   known-issues doc. Cheap, and they make everything after it reviewable.
3. Phase 3.1-3.2: bundle skeleton with the existing flat staging (no data
   relocation yet) to prove the app launches from Finder, then Phase 3.3.

## 5. Decisions for Kevin

1. **Closeout bar:** is double-clickable `.app` + dmg required, or is
   "tracked `tools/run_mac.sh` + README + release tag" enough for personal
   use? The audit's recommendation is the app bundle, because the rest of
   the setup is already one command.
2. **Historical attribution:** 176 commits in the repo carry
   `Co-Authored-By: Claude` trailers, against the standing no-AI-attribution
   rule. Rewriting published history on a public repo is destructive
   (force-push, breaks clones); recommendation is to leave history alone and
   enforce the rule for new commits/releases. Needs an explicit call.
3. **Cleanup:** confirm before deleting `../fable2-partial-stage` and the
   4.9 GB of logs.
4. **Audio:** `audio_mute = true` stays the user's choice; do not change it
   while testing unless asked.
5. **Notarization/Intel support:** not needed for this Mac; note as
   unsupported until asked.

## 6. Deliberate non-goals

- Re-opening performance or the tick-rate step-climb RE (parked by
  `fable2-stop-at-crumbs`; the F4 switch is the accepted workaround).
- Uncapped/stress runs and Metal traces (fans, disk).
- Moving or rewriting the user's saves.
- Shipping or linking game content in any release.
- Changing `audio_mute` or other user settings.
- Windows parity work.

P4 setup (2026-11-03): play folder `/Volumes/MacMiniExternal/Games/fable2/Fable2-App/`
created — app + hardlinked content + a copy of the dev build's saves and tuned
config; boot-checked to the title screen with a clean exit and state created
alongside the app (not in the bundle). The pass itself is Kevin's.
