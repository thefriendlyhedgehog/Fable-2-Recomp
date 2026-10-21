// fable_2 - ReXGlue Recompiled Project

#include "generated/default/fable_2_init.h"

// F5 -> run external Lua file. Must be included before fable_2_app.h (which
// pulls in keyboard_gamepad.h, which calls fable2::f5lua::poll_f5() per frame).
#include "fable2_f5_lua.h"

#include "fable_2_app.h"
#include "fps_meter.h"
#include "fable2_text_probe.h"
#include "fable2_glyph_probe.h"
#include "fable2_hotfuncs.h"
#include "fable2_heap_scan.h"
#include "fable2_text_append.h"
#include "fable2_state_probe.h"
#include "fable2_menu_probe.h"  // main-menu input path (mod-menu hook points)
#include "fable2_modmenu.h"  // 6th "MOD OPTION" selectable main-menu item
#include "fable2_av_probe.h"  // self-installs a guest-PC AV logger (VEH)
#include "fable2_stall_dump.h"  // self-installs a render-stall thread dumper
// #include "fable2_ui_render_probe.h"
#include "keyboard_gamepad.h"

#include <rex/cvar.h>

// Host keyboard -> guest gamepad map (see src/input/keyboard_gamepad.h).
// Format: "Key:Button,Key:Button,...".
// The default layout is NOT baked in here: it lives in the config layer
// (fable2::config::kDefaultKeyboardGamepadMap) and is editable in
// fable2_config.toml under [input]. Fable2App::OnPostInitLogging seeds this
// cvar from the config at startup (only when no higher-priority source set
// it). Override at runtime with --keyboard_gamepad_map "..." or in the
// console; empty string disables the keyboard gamepad.
REXCVAR_DEFINE_STRING(
    keyboard_gamepad_map,
    "",
    "Input",
    "Map host keyboard keys / mouse buttons to guest gamepad input "
    "(Key:Button,...; key names: keyboard keys plus LMB/RMB/MMB and "
    "XMB1/XMB2 mouse buttons; targets: A/B/X/Y, LB/RB, LT/RT, "
    "Up/Down/Left/Right, Pause, Select, L3/R3, StickUp/StickDown/"
    "StickLeft/StickRight). "
    "Default comes from fable2_config.toml [input] keyboard_gamepad_map.");

// Mouse -> right stick (camera look). See src/input/keyboard_gamepad.h.
// The defaults below are a fallback only: Fable2App::OnPostInitLogging
// seeds these cvars from fable2_config.toml [input] at startup (when no
// higher-priority source set them). Keep the values in sync with the
// defaults in fable2::config::Values (src/core/fable2_config.h).
REXCVAR_DEFINE_BOOL(mouse_look, true, "Input",
                    "Map mouse movement to the guest right stick (camera "
                    "look): sweep to look, stop to stop. Default comes from "
                    "fable2_config.toml [input] mouse_look.");
REXCVAR_DEFINE_INT32(mouse_look_scale, 256, "Input",
                     "Mouse-look sensitivity: right-stick units per pixel of "
                     "mouse movement (larger = more sensitive). Default comes "
                     "from fable2_config.toml [input] mouse_look_scale.")
    .range(1, 4096);
// Debug sweep (src/diagnostics/fable2_debug_sweep.h): the 0 key (fn+F10) on
// macOS steps through these settings and saves a screenshot of each.
REXCVAR_DEFINE_STRING(
    debug_sweep_steps,
    "baseline:vulkan_debug_force_mip=-1,vulkan_debug_fsi_whole_quads=false,"
    "vulkan_debug_skip_ps_group=0;"
    "mip0:vulkan_debug_force_mip=0;mip1:vulkan_debug_force_mip=1;"
    "mip2:vulkan_debug_force_mip=2;mip3:vulkan_debug_force_mip=3;"
    "mip4:vulkan_debug_force_mip=4;mip5:vulkan_debug_force_mip=5;"
    "whole_quads:vulkan_debug_force_mip=-1,vulkan_debug_fsi_whole_quads=true;"
    "no_shadow_shader:vulkan_debug_fsi_whole_quads=false,"
    "vulkan_debug_skip_ps=401A01FD4E5F8757,vulkan_debug_skip_ps_group=1;"
    "no_lighting:vulkan_debug_skip_ps=9C2144E42E95A76F+4755A2E4A05BA678+578342593A37DD35+"
    "04F23292B2F44077+AF1E16EE57E4BB06+45243B758F9272AB",
    "Debug",
    "Debug sweep steps (0 key / fn+F10 on macOS): label:cvar=value,cvar=value;... Each "
    "step applies on top of the previous one, then a screenshot is saved; afterwards "
    "the touched cvars get their old values back.");
REXCVAR_DEFINE_STRING(debug_sweep_dir, "debug_sweep", "Debug",
                      "Where the debug sweep saves its screenshots (NN_label.png).");
REXCVAR_DEFINE_INT32(debug_sweep_settle_ms, 1500, "Debug",
                     "Debug sweep: time between applying a step and its screenshot.")
    .range(100, 20000);
REXCVAR_DEFINE_STRING(debug_command_file, "", "Debug",
                      "macOS/Linux: a file the input poll checks every 250 ms; when it "
                      "exists it is read and deleted, one command per line: report (as 9 / "
                      "F9), sweep (as 0 / F10), dump [frames] (vulkan_debug_dump_frames, "
                      "default 16), quit (closes the window), press <button> [ms] "
                      "(gamepad button for player 1, names as in keyboard_gamepad_map, "
                      "default 150 ms), wait <ms>. Presses and waits run in order. Write it "
                      "to another name and rename it, so it is never read half-written.");
REXCVAR_DEFINE_BOOL(merge_controllers, false, "Input",
                    "Every connected controller drives player 1 (macOS/Linux), for a "
                    "controller that shows up as two devices, such as the Steam "
                    "Controller on macOS without Steam. A second player's controller "
                    "then also drives player 1. Read when the input system starts. "
                    "Default comes from fable2_config.toml [input] merge_controllers.");

// The allocator table monitor in Fable2App::OnPostSetup: a thread that samples
// the guest allocator table every 2 ms and logs when its head goes null (the
// table-zeroing / init-ordering bug). Off by default.
REXCVAR_DEFINE_BOOL(debug_alloc_watch, false, "Debug",
                    "Run the allocator table monitor: a thread sampling the guest "
                    "allocator table every 2 ms that logs when its head goes null "
                    "(the table-zeroing / init-ordering bug). Read at startup.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Skip the default.xex SHA-256 integrity check at startup (see
// Fable2App::OnLoadXexImage). When set, the XEX hash is still computed and
// written to the log, but a mismatch (or an unreadable file) no longer blocks
// the launch. Useful for running a modified or non-catalogue default.xex.
REXCVAR_DEFINE_BOOL(skipHash, false, "Launch",
                    "Skip the default.xex SHA-256 integrity check. The hash is "
                    "still logged, but a mismatch no longer blocks the launch "
                    "(useful for a modified or non-catalogue XEX). Pass "
                    "--skipHash on the command line.");

#ifdef _WIN32
// Hybrid-graphics laptops (Intel/AMD iGPU + NVIDIA/AMD dGPU): ask the drivers
// to run us on the discrete GPU. Without this Windows may hand the game the
// integrated GPU, which hits D3D12 DEVICE_HUNG (TDR) shortly after boot
// (issue #42). These must be exported from the .exe itself; a user's own
// per-app setting in Windows Graphics settings / NVIDIA Control Panel still
// takes priority.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

REX_DEFINE_APP(fable_2, Fable2App::Create)
