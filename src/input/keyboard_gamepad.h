// fable_2 - Keyboard -> guest gamepad input bridge
//
// ReXGlue's guest input path is:
//   game (PPC) -> __imp__XamInputGetState -> rex::input::InputSystem::GetState
//   -> each InputDriver::GetDeviceState -> MergeInto (buttons OR, triggers max)
//
// The SDK ships an SDL driver (real gamepads) plus a NOP stand-in. This file
// adds one more driver: a *synthetic* "keyboard gamepad". Synthetic devices
// are routed to guest user 0 by the default assignment and are OR-merged with
// the physical pad, so holding a mapped key injects the corresponding guest
// button on top of whatever the real pad reports (see state_merge.h /
// device_assignment.h). The physical pad keeps working; the keyboard just
// adds buttons.
//
// The keyboard mapping is data-driven via the `keyboard_gamepad_map` cvar
// (defined in main.cpp): "Key:Button,Key:Button,..." where Key is a host key
// name understood by rex::ui::ParseVirtualKey ("E", "Space", "LeftShift", ...)
// or a mouse-button name ("LMB"/"RMB"/"MMB", plus "XMB1"/"XMB2" for the side
// buttons; mouse names are also accepted case-insensitively, see
// input_detail::ParseKey). Mouse-button state is polled with
// GetAsyncKeyState, the same path as keyboard keys. Button is a guest gamepad
// button name (A/B/X/Y/LB/RB/LT/RT/Up/Down/Left/Right/Start/Back/L3/R3/
// StickUp/StickDown/StickLeft/StickRight).
// The cvar's default is seeded from fable2_config.toml [input] at startup
// (src/core/fable2_config.{h,cpp} + Fable2App::OnPostInitLogging), so remapping
// can be done in the config file without a rebuild or CLI arg.
//
// The mouse is mapped to the right stick (camera look) via the `mouse_look`
// and `mouse_look_scale` cvars (defaults seeded from fable2_config.toml
// [input] at startup, same as the keyboard map above). The cursor is recentered to the window center
// every poll and that frame's movement is consumed, so the camera rotates
// continuously and the cursor never wanders or hits the screen edge.
//
// Host input is read two ways. On Windows the driver polls Win32 directly
// (GetAsyncKeyState / cursor recentering). Elsewhere (macOS, Linux) it listens
// to the SDK window's key, mouse and focus events and uses SDL relative mouse
// mode for mouse look, falling back to recentering the cursor when the pointer
// cannot be locked; the mapping, cvars and guest-side behavior are the same.

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/keybinds.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>

#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "fable2_debug_sweep.h"
#endif

// Declared here, defined in main.cpp (REXCVAR_DEFINE_* must live in a .cpp).
REXCVAR_DECLARE(std::string, keyboard_gamepad_map);
REXCVAR_DECLARE(std::string, debug_command_file);
REXCVAR_DECLARE(bool, mouse_look);
REXCVAR_DECLARE(int32_t, mouse_look_scale);
REXCVAR_DECLARE(bool, merge_controllers);
REXCVAR_DECLARE(std::string, mouse_unlock_key);

namespace fable2 {

// Bring the SDK input types into scope so the driver overrides below read
// cleanly. (X_STATUS / X_RESULT live in rex::; the rest in rex::input::.)
using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::input::XINPUT_DEVTYPE_GAMEPAD;
using rex::input::X_INPUT_CAPS_FFB_SUPPORTED;

namespace input_detail {

// Full thumbstick deflection (XInput range is -32768..32767).
constexpr int32_t kStickMax = 32767;

inline int32_t ClampStick(int32_t v) {
  if (v > kStickMax) return kStickMax;
  if (v < -kStickMax) return -kStickMax;
  return v;
}

// One host-key -> guest-input binding. Exactly one of button/trigger/axis
// is set.
struct Mapping {
  uint16_t vk = 0;       // host virtual-key code (0 == invalid)
  uint16_t button = 0;   // guest X_INPUT_GAMEPAD_* button mask
  char trigger = 0;      // 0 = not a trigger, 'L' = left, 'R' = right
  int8_t axis = 0;       // 0 = not an axis, 1 = left-stick X, 2 = left-stick Y
  int8_t axis_sign = 0;  // +1 / -1 deflection direction for the axis
  bool valid() const { return button != 0 || trigger != 0 || axis != 0; }
};

// Case-insensitive guest-input name -> mapping fragment. Recognizes buttons,
// triggers, and left-stick axis names. Unknown names reset the fragment to
// invalid.
inline void FillGuestTarget(Mapping& m, std::string_view name) {
  auto upper = [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); };
  std::string_view n = name;
  while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.remove_prefix(1);
  while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.remove_suffix(1);
  std::string u;
  u.reserve(n.size());
  for (char c : n) u.push_back(upper(c));

  using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
  if (u == "A") {
    m.button = B::X_INPUT_GAMEPAD_A;
  } else if (u == "B") {
    m.button = B::X_INPUT_GAMEPAD_B;
  } else if (u == "X") {
    m.button = B::X_INPUT_GAMEPAD_X;
  } else if (u == "Y") {
    m.button = B::X_INPUT_GAMEPAD_Y;
  } else if (u == "LB" || u == "LSHOULDER") {
    m.button = B::X_INPUT_GAMEPAD_LEFT_SHOULDER;
  } else if (u == "RB" || u == "RSHOULDER") {
    m.button = B::X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  } else if (u == "UP" || u == "DUP") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_UP;
  } else if (u == "DOWN" || u == "DDOWN") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_DOWN;
  } else if (u == "LEFT" || u == "DLEFT") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_LEFT;
  } else if (u == "RIGHT" || u == "DRIGHT") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_RIGHT;
  } else if (u == "START" || u == "PAUSE") {
    m.button = B::X_INPUT_GAMEPAD_START;
  } else if (u == "BACK" || u == "VIEW" || u == "SELECT") {
    m.button = B::X_INPUT_GAMEPAD_BACK;
  } else if (u == "L3" || u == "LTHUMB") {
    m.button = B::X_INPUT_GAMEPAD_LEFT_THUMB;
  } else if (u == "R3" || u == "RTHUMB") {
    m.button = B::X_INPUT_GAMEPAD_RIGHT_THUMB;
  } else if (u == "LT" || u == "LTRIGGER") {
    m.trigger = 'L';
  } else if (u == "RT" || u == "RTRIGGER") {
    m.trigger = 'R';
  // Left stick axes. lx negative = left (standard). Fable 2 reads ly positive
  // as forward/up (opposite of the standard XInput "ly negative = up"), so the
  // Y signs are flipped to match the game's expectation.
  } else if (u == "STICKUP" || u == "LU") {
    m.axis = 2; m.axis_sign = 1;
  } else if (u == "STICKDOWN" || u == "LD") {
    m.axis = 2; m.axis_sign = -1;
  } else if (u == "STICKLEFT" || u == "LL") {
    m.axis = 1; m.axis_sign = -1;
  } else if (u == "STICKRIGHT" || u == "LR") {
    m.axis = 1; m.axis_sign = 1;
  } else {
    m = Mapping{};  // unknown -> invalid
  }
}

// Case-insensitive upper helper (shared by the name parsers below).
inline std::string ToUpper(std::string_view s) {
  std::string u;
  u.reserve(s.size());
  for (char c : s) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  return u;
}

// Host key-name -> VirtualKey. The SDK table (rex::ui::ParseVirtualKey) is
// authoritative and already recognizes LMB/RMB/MMB; this fallback additionally
// accepts mouse-button names case-insensitively ("lmb") and covers the side
// buttons the SDK table does not spell out (VK_XBUTTON1/2). Returns kNone when
// the name is not a known key.
inline rex::ui::VirtualKey ParseKey(std::string_view name) {
  // Tolerate stray spaces/tabs around the name ("LMB", " LMB ").
  while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
  auto vk = rex::ui::ParseVirtualKey(name);
  if (vk != rex::ui::VirtualKey::kNone) return vk;
  using V = rex::ui::VirtualKey;
  std::string u = ToUpper(name);
  if (u == "LMB" || u == "LEFTMOUSE") return V::kLButton;   // VK_LBUTTON
  if (u == "RMB" || u == "RIGHTMOUSE") return V::kRButton;  // VK_RBUTTON
  if (u == "MMB" || u == "MIDDLEMOUSE") return V::kMButton;  // VK_MBUTTON
  if (u == "XMB1" || u == "XBUTTON1" || u == "SIDEBUTTON1")
    return V::kXButton1;  // mouse side button 1
  if (u == "XMB2" || u == "XBUTTON2" || u == "SIDEBUTTON2")
    return V::kXButton2;  // mouse side button 2
  return V::kNone;
}

// Parse "Key:Button,Key:Button,...". Entries whose key or target name does not
// parse are skipped. Returns an empty vector on empty input.
inline std::vector<Mapping> ParseMap(std::string_view text) {
  std::vector<Mapping> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    std::string_view entry =
        text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    if (!entry.empty()) {
      size_t colon = entry.find(':');
      if (colon != std::string_view::npos) {
        auto vk = ParseKey(entry.substr(0, colon));
        if (vk != rex::ui::VirtualKey::kNone) {
          Mapping m;
          m.vk = static_cast<uint16_t>(vk);
          FillGuestTarget(m, entry.substr(colon + 1));
          if (m.valid()) out.push_back(m);
        }
      }
    }
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return out;
}

}  // namespace input_detail

/// Synthetic device that exposes held host keys as guest gamepad buttons.
class KeyboardGamepadDriver final : public rex::input::InputDriver
#ifndef _WIN32
    , public rex::ui::WindowInputListener,
      public rex::ui::WindowListener
#endif
{
 public:
  // InputDriver's constructor is protected, so expose a public one.
  KeyboardGamepadDriver(rex::ui::Window* window, size_t window_z_order)
      : rex::input::InputDriver(window, window_z_order) {
#ifndef _WIN32
    if (window) AttachToWindow(window);
#endif
  }

  ~KeyboardGamepadDriver() override {
#ifdef _WIN32
    if (cursor_hidden_) {
      if (rex::ui::Window* w = GameWindow())
        w->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      cursor_hidden_ = false;
    }
#else
    // Also releases the mouse and restores the cursor (on the UI thread).
    DetachFromWindow();
#endif
  }

#ifndef _WIN32
  void OnWindowAvailable(rex::ui::Window* window) override {
    if (window) AttachToWindow(window);
  }

  // WindowInputListener (called on the UI thread). Events are never marked
  // handled, so the SDK's own overlays/keybinds still see every key.
  void OnKeyDown(rex::ui::KeyEvent& e) override { SetKey(e.virtual_key(), true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { SetKey(e.virtual_key(), false); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    UiState& ui = *ui_;
    int32_t x = e.x();
    int32_t y = e.y();
    if (ui.captured) {
      std::lock_guard<std::mutex> lock(ui.mouse_mutex);
      if (ui.relative) {
        // The pointer is locked, so only the deltas move.
        ui.mouse_dx += e.dx();
        ui.mouse_dy += e.dy();
      } else {
        ui.mouse_dx += float(x - ui.prev_mouse_x);
        ui.mouse_dy += float(y - ui.prev_mouse_y);
      }
    }
    ui.prev_mouse_x = x;
    ui.prev_mouse_y = y;
    // Without the pointer lock the cursor has to be kept off the edges.
    if (ui.captured && !ui.relative) RecenterCursor(ui, x, y);
  }

  // WindowListener (called on the UI thread).
  void OnClosing(rex::ui::UIEvent&) override { DetachFromWindow(); }
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    has_focus_.store(true, std::memory_order_relaxed);
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    has_focus_.store(false, std::memory_order_relaxed);
    // Withdraw the request too, so the next poll that wants the mouse queues
    // a new capture once focus is back.
    ui_->capture_requested.store(false, std::memory_order_relaxed);
    ReleaseMouseCapture(*ui_);
  }
#endif

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    RefreshBindings();
    if (bindings_.empty() && !REXCVAR_GET(mouse_look)) return;  // nothing to do
    DeviceInfo info;
    info.id = kDeviceId;
    info.name = "Keyboard/Mouse (gamepad)";
    info.guid = "fable2-keyboard-gamepad";
    info.synthetic = true;  // routed to guest user 0 by the default assignment
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    RefreshBindings();

    uint16_t buttons = 0;
    uint8_t left_trigger = 0;
    uint8_t right_trigger = 0;
    int32_t lx = 0;
    int32_t ly = 0;
    int32_t rx = 0;
    int32_t ry = 0;
#ifdef _WIN32
    rex::ui::Window* win = GameWindow();
    HWND hwnd = win ? reinterpret_cast<HWND>(win->GetNativeWindowHandle()) : nullptr;
    // Focus: the game window is the foreground (top-level) window. Gates all
    // host input so nothing fires while alt-tabbed away.
    bool focused = hwnd != nullptr &&
                   (GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT));

    // F5 (host) -> run the external Lua file (fable2_f5_lua.h). Poll per frame;
    // the actual run happens on the guest main thread via the RunScript binding
    // probe, so this just latches a flag. Runs even while the console (F4) is
    // open (F5 is a host-level developer command, not gamepad input).
    fable2::f5lua::poll_f5();

    // Debug-console toggle key (default F4). Pressing it opens/closes the menu;
    // while the menu is open we release the mouse lock (free cursor for the
    // menu) and re-engage it when the menu closes. It's excluded from the
    // gamepad map so it doesn't also act as a button (F4 was the d-pad Right).
    uint16_t unlock_vk = static_cast<uint16_t>(
        rex::ui::ParseVirtualKey(REXCVAR_GET(mouse_unlock_key)));
    bool has_unlock_key = unlock_vk != 0;
    if (focused) {
      bool unlock_down = has_unlock_key && (GetAsyncKeyState(unlock_vk) & 0x8000);
      if (unlock_down && !unlock_down_prev_) console_open_ = !console_open_;
      unlock_down_prev_ = unlock_down;
    }

    if (focused) {
      for (const auto& m : bindings_) {
        if (has_unlock_key && m.vk == unlock_vk) continue;  // not gamepad input
        if ((GetAsyncKeyState(m.vk) & 0x8000) == 0) continue;  // not held
        if (m.button != 0) {
          buttons |= m.button;
        } else if (m.trigger == 'L') {
          left_trigger = 0xFF;
        } else if (m.trigger == 'R') {
          right_trigger = 0xFF;
        } else if (m.axis == 1) {
          lx += m.axis_sign * input_detail::kStickMax;
        } else if (m.axis == 2) {
          ly += m.axis_sign * input_detail::kStickMax;
        }
      }
    }
    // Clamp (opposing keys cancel; two same-direction keys can't exceed it).
    lx = input_detail::ClampStick(lx);
    ly = input_detail::ClampStick(ly);

    // Mouse -> right stick (camera look) with cursor recentering + hiding:
    // the cursor is pinned to the window center each poll (that frame's
    // movement is consumed, so the camera rotates continuously and never hits
    // the screen edge). We gate on the game window being focused (so alt-tabbing
    // away releases the lock) and on the debug menu being closed (F4) so the
    // cursor is free while the menu is open. Fable 2 inverts Y, so mouse-up
    // (dy<0) maps to +ry (look up).
    bool looking = focused && REXCVAR_GET(mouse_look) && !console_open_;

    // Hide/restore the cursor via the SDK Window. This is a thread-safe
    // "desired state" that the UI thread applies, so it actually takes effect
    // (raw ShowCursor from this guest thread was overwritten by the SDK's own
    // cursor management).
    if (looking && !cursor_hidden_) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      cursor_hidden_ = true;
    } else if (!looking && cursor_hidden_ && win) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      cursor_hidden_ = false;
    }

    if (looking) {
      RECT rc;
      POINT p;
      POINT c;
      if (GetClientRect(hwnd, &rc) && GetCursorPos(&p)) {
        c.x = rc.left + rc.right / 2;
        c.y = rc.top + rc.bottom / 2;
        if (ClientToScreen(hwnd, &c)) {
          if (mouse_centered_) {
            int32_t scale = REXCVAR_GET(mouse_look_scale);
            rx = input_detail::ClampStick((p.x - c.x) * scale);
            ry = input_detail::ClampStick(-(p.y - c.y) * scale);
          }
          SetCursorPos(c.x, c.y);  // recenter: consume this frame's movement
          mouse_centered_ = true;
        }
      }
    } else {
      mouse_centered_ = false;  // re-prime on (re)focus to avoid a jump
    }
#else
    // Focus gates all host input, as on Windows. Drop held keys on focus loss:
    // their key-up events go to whichever window has focus, not this one.
    // (The window itself is only touched on the UI thread: it can be closed
    // while the guest polls.)
    bool focused = attached_window_.load(std::memory_order_acquire) != nullptr &&
                   has_focus_.load(std::memory_order_relaxed);
    if (!focused) ClearKeys();

    // F5 (host) -> run the external Lua file (fable2_f5_lua.h).
    fable2::f5lua::poll_f5(focused && KeyDown(0x74 /* F5 */));

    // F9 (host) -> log every draw of the next frame (macOS SDK patch cvar
    // vulkan_frame_report; "[frame]" lines in the log).
    // The number keys 6-9 do the same as F6-F9: on a Mac keyboard the F keys
    // are media and system keys unless fn is held, and macOS never passes
    // those to the game.
    bool frame_report_down = focused && (KeyDown(0x78 /* F9 */) || KeyDown(0x39 /* 9 */));
    if (frame_report_down && !frame_report_down_prev_) {
      if (rex::cvar::SetFlagByName("vulkan_frame_report",
                                   std::to_string(++frame_report_requests_))) {
        REXSYS_INFO("[input] F9: logging the next frame's draws");
      }
    }
    frame_report_down_prev_ = frame_report_down;

    // F6 (host) -> flip a GPU debug switch of the macOS SDK patch, to compare
    // the picture with and without it while standing still: fragment shader
    // interlock shaders run for whole 2x2 quads (no early quad skip). F7
    // cycles the mip level every texture is sampled at (off, 0, 1, ... 6,
    // off), to see which levels are loaded wrong. F8 cycles through the
    // groups of pixel shaders in vulkan_debug_skip_ps (';'-separated),
    // skipping the draws of one group at a time, then none.
    static constexpr struct {
      uint16_t vk;
      uint16_t digit_vk;
      const char* key;
      const char* cvar;
    } kDebugToggles[] = {
        {0x75, 0x36, "F6", "vulkan_debug_fsi_whole_quads"},
    };
    for (size_t i = 0; i < std::size(kDebugToggles); ++i) {
      bool down =
          focused && (KeyDown(kDebugToggles[i].vk) || KeyDown(kDebugToggles[i].digit_vk));
      if (down && !debug_toggle_down_prev_[i]) {
        bool on = !rex::cvar::Query<bool>(kDebugToggles[i].cvar);
        if (rex::cvar::SetFlagByName(kDebugToggles[i].cvar, on ? "true" : "false")) {
          REXSYS_INFO("[input] {}: {} = {}", kDebugToggles[i].key, kDebugToggles[i].cvar, on);
        } else {
          REXSYS_WARN("[input] {}: {} is not available in this SDK build",
                      kDebugToggles[i].key, kDebugToggles[i].cvar);
        }
      }
      debug_toggle_down_prev_[i] = down;
    }
    bool force_mip_down = focused && (KeyDown(0x76 /* F7 */) || KeyDown(0x37 /* 7 */));
    if (force_mip_down && !debug_toggle_down_prev_[1]) {
      int32_t level = rex::cvar::Query<int32_t>("vulkan_debug_force_mip") + 1;
      if (level > 6) level = -1;
      if (rex::cvar::SetFlagByName("vulkan_debug_force_mip", std::to_string(level))) {
        if (level < 0) {
          REXSYS_INFO("[input] F7: textures use all mip levels again");
        } else {
          REXSYS_INFO("[input] F7: textures sampled only at mip level {}", level);
        }
      } else {
        REXSYS_WARN("[input] F7: vulkan_debug_force_mip is not available in this SDK build");
      }
    }
    debug_toggle_down_prev_[1] = force_mip_down;
    bool skip_group_down = focused && (KeyDown(0x77 /* F8 */) || KeyDown(0x38 /* 8 */));
    if (skip_group_down && !debug_toggle_down_prev_[2]) {
      std::string groups = rex::cvar::Query<std::string>("vulkan_debug_skip_ps");
      int32_t group_count =
          groups.empty() ? 0 : int32_t(std::count(groups.begin(), groups.end(), ';')) + 1;
      int32_t group =
          (rex::cvar::Query<int32_t>("vulkan_debug_skip_ps_group") + 1) % (group_count + 1);
      if (rex::cvar::SetFlagByName("vulkan_debug_skip_ps_group", std::to_string(group))) {
        std::string hashes;
        if (group) {
          size_t begin = 0;
          for (int32_t g = 1; g < group; ++g) {
            begin = groups.find(';', begin) + 1;
          }
          hashes = groups.substr(begin, groups.find(';', begin) - begin);
        }
        REXSYS_INFO("[input] F8: skipping pixel shader group {} of {}{}{}", group, group_count,
                    group ? ": " : " (none)", hashes);
      } else {
        REXSYS_WARN("[input] F8: vulkan_debug_skip_ps_group is not available in this SDK build");
      }
    }
    debug_toggle_down_prev_[2] = skip_group_down;

    // 0 / fn+F10 (host) -> debug sweep: step through debug_sweep_steps and
    // save a screenshot of each (src/diagnostics/fable2_debug_sweep.h).
    bool sweep_down = focused && (KeyDown(0x79 /* F10 */) || KeyDown(0x30 /* 0 */));
    if (sweep_down && !debug_toggle_down_prev_[3]) {
      REXSYS_INFO("[input] 0: debug sweep");
      fable2::debugsweep::Instance().Start();
    }
    debug_toggle_down_prev_[3] = sweep_down;

    // Commands from debug_command_file (a script driving a capture), also
    // without focus.
    PollCommandFile(&buttons);

    // Debug-console toggle key (default F4); see the Windows path above.
    uint16_t unlock_vk = static_cast<uint16_t>(
        rex::ui::ParseVirtualKey(REXCVAR_GET(mouse_unlock_key)));
    bool has_unlock_key = unlock_vk != 0;
    if (focused) {
      bool unlock_down = has_unlock_key && KeyDown(unlock_vk);
      if (unlock_down && !unlock_down_prev_) console_open_ = !console_open_;
      unlock_down_prev_ = unlock_down;
    }

    if (focused) {
      for (const auto& m : bindings_) {
        if (has_unlock_key && m.vk == unlock_vk) continue;  // not gamepad input
        if (!KeyDown(m.vk)) continue;  // not held
        if (m.button != 0) {
          buttons |= m.button;
        } else if (m.trigger == 'L') {
          left_trigger = 0xFF;
        } else if (m.trigger == 'R') {
          right_trigger = 0xFF;
        } else if (m.axis == 1) {
          lx += m.axis_sign * input_detail::kStickMax;
        } else if (m.axis == 2) {
          ly += m.axis_sign * input_detail::kStickMax;
        }
      }
    }
    lx = input_detail::ClampStick(lx);
    ly = input_detail::ClampStick(ly);

    // Mouse -> right stick. While looking, the UI thread hides the cursor
    // and locks the pointer with SDL relative mouse mode (or, if that is
    // refused, recenters the cursor), and motion events accumulate deltas
    // between polls (OnMouseMove).
    bool looking = focused && REXCVAR_GET(mouse_look) && !console_open_;
    RequestMouseCapture(looking);

    // Consume whole pixels and keep the fraction for the next poll: macOS
    // trackpads and high-DPI mice report sub-pixel deltas, which truncating
    // per poll would drop, losing slow camera movement.
    int32_t dx = 0;
    int32_t dy = 0;
    {
      UiState& ui = *ui_;
      std::lock_guard<std::mutex> lock(ui.mouse_mutex);
      dx = static_cast<int32_t>(ui.mouse_dx);
      dy = static_cast<int32_t>(ui.mouse_dy);
      ui.mouse_dx -= static_cast<float>(dx);
      ui.mouse_dy -= static_cast<float>(dy);
      if (!looking) ui.mouse_dx = ui.mouse_dy = 0.0f;
    }
    if (looking) {
      int32_t scale = REXCVAR_GET(mouse_look_scale);
      rx = input_detail::ClampStick(dx * scale);
      ry = input_detail::ClampStick(-dy * scale);
    }
#endif

    out_state->packet_number.set(packet_number_++);
    out_state->gamepad.buttons.set(buttons);
    out_state->gamepad.left_trigger = left_trigger;
    out_state->gamepad.right_trigger = right_trigger;
    out_state->gamepad.thumb_lx.set(static_cast<int16_t>(lx));
    out_state->gamepad.thumb_ly.set(static_cast<int16_t>(ly));
    out_state->gamepad.thumb_rx.set(static_cast<int16_t>(rx));
    out_state->gamepad.thumb_ry.set(static_cast<int16_t>(ry));
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out_caps = X_INPUT_CAPABILITIES{};
    out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
    out_caps->sub_type = 0x05;  // XINPUT_SUBTYPE_GAMEPAD
    out_caps->flags.set(X_INPUT_CAPS_FFB_SUPPORTED);
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    (void)vibration;
    return id == kDeviceId ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override {
    (void)flags;
    (void)out_keystroke;
    return X_ERROR_EMPTY;  // never has queued keystrokes
  }

 private:
  // Re-parse the cvar only when its text changed (cheap for a short string),
  // so the mapping is hot-reloadable from the console without a rebuild.
  void RefreshBindings() {
    std::string text = REXCVAR_GET(keyboard_gamepad_map);
    if (text == cached_text_) return;
    cached_text_ = std::move(text);
    bindings_ = input_detail::ParseMap(cached_text_);
  }

  // Device id for this synthetic pad. MUST NOT collide with the SDL driver's
  // sequential ids (they start at 1): InputSystem::DriverForDevice() resolves
  // an id by first match, so sharing id 1 with the first physical pad
  // connected silently shadows this driver -- the guest only sees the real
  // pad and this driver is never polled (dead keyboard, no mouse lock).
  static constexpr DeviceId kDeviceId{1ull << 60};
  std::vector<input_detail::Mapping> bindings_;
  std::string cached_text_;
  uint32_t packet_number_ = 0;
  [[maybe_unused]] bool mouse_centered_ = false;  // Windows: set once the cursor is first pinned
  [[maybe_unused]] bool cursor_hidden_ = false;  // Windows: SDK window cursor hidden
  bool console_open_ = false;    // debug menu open -> mouse lock released
  bool unlock_down_prev_ = false;  // prev-poll state of the unlock key
  [[maybe_unused]] bool frame_report_down_prev_ = false;  // F9 state (non-Windows)
  [[maybe_unused]] int32_t frame_report_requests_ = 0;  // F9 presses (vulkan_frame_report)
  [[maybe_unused]] bool debug_toggle_down_prev_[4] = {};  // F6-F8, F10 state (non-Windows)
#ifndef _WIN32
  // debug_command_file state (guest input poll thread only). A press with
  // mask 0 is a wait.
  struct CommandPress {
    uint16_t mask;
    int32_t ms;
  };
  std::deque<CommandPress> command_presses_;
  bool command_press_active_ = false;
  uint16_t command_press_mask_ = 0;
  std::chrono::steady_clock::time_point command_press_end_{};
  std::chrono::steady_clock::time_point next_command_poll_{};

  void PollCommandFile(uint16_t* buttons) {
    auto now = std::chrono::steady_clock::now();
    if (now >= next_command_poll_) {
      next_command_poll_ = now + std::chrono::milliseconds(250);
      const std::string& path = REXCVAR_GET(debug_command_file);
      std::error_code ec;
      if (!path.empty() && std::filesystem::exists(path, ec)) {
        std::vector<std::string> lines;
        {
          std::ifstream file(path);
          std::string line;
          while (std::getline(file, line)) lines.push_back(line);
        }
        std::filesystem::remove(path, ec);
        for (const std::string& line : lines) RunCommand(line);
      }
    }
    // One press (or wait) at a time, with a gap after a press so the game
    // sees the release.
    if (command_press_active_ && now >= command_press_end_) {
      command_press_active_ = false;
      if (command_press_mask_) {
        command_presses_.push_front({0, 100});
      }
    }
    if (!command_press_active_ && !command_presses_.empty()) {
      CommandPress press = command_presses_.front();
      command_presses_.pop_front();
      command_press_active_ = true;
      command_press_mask_ = press.mask;
      command_press_end_ = now + std::chrono::milliseconds(press.ms);
    }
    if (command_press_active_) *buttons |= command_press_mask_;
  }

  void RunCommand(const std::string& line) {
    std::istringstream words(line);
    std::string command;
    if (!(words >> command)) return;
    REXSYS_INFO("[input] command file: {}", line);
    if (command == "report") {
      rex::cvar::SetFlagByName("vulkan_frame_report", std::to_string(++frame_report_requests_));
    } else if (command == "sweep") {
      fable2::debugsweep::Instance().Start();
    } else if (command == "dump") {
      int32_t frames = 16;
      words >> frames;
      rex::cvar::SetFlagByName("vulkan_debug_dump_frames", std::to_string(std::max(frames, 1)));
    } else if (command == "quit") {
      rex::ui::WindowedAppContext* ctx = ui_context_.load(std::memory_order_acquire);
      std::shared_ptr<UiState> ui = ui_;
      if (ctx) {
        ctx->CallInUIThreadDeferred([ui] {
          if (ui->ui_window) ui->ui_window->RequestClose();
        });
      }
    } else if (command == "press") {
      std::string name;
      int32_t ms = 150;
      words >> name >> ms;
      input_detail::Mapping m;
      input_detail::FillGuestTarget(m, name);
      if (!m.button) {
        REXSYS_WARN("[input] command file: unknown button '{}'", name);
        return;
      }
      command_presses_.push_back({m.button, std::max(ms, 16)});
    } else if (command == "set") {
      std::string name, value;
      words >> name >> value;
      if (name.empty() || !rex::cvar::SetFlagByName(name, value)) {
        REXSYS_WARN("[input] command file: could not set '{}' to '{}'", name, value);
      }
    } else if (command == "wait") {
      int32_t ms = 0;
      words >> ms;
      command_presses_.push_back({0, std::max(ms, 0)});
    } else {
      REXSYS_WARN("[input] command file: unknown command '{}'", command);
    }
  }
  // Held-key table indexed by rex::ui::VirtualKey (Win32 VK numbering, so the
  // cvar mapping means the same thing on every platform). Written on the UI
  // thread by the listener callbacks, read by the guest input poll.
  std::array<std::atomic<bool>, 256> keys_{};
  // The window the listeners are registered with. Read on any thread, only
  // dereferenced on the UI thread (or while it is known to be alive).
  std::atomic<rex::ui::Window*> attached_window_{nullptr};
  // The window's app context, which outlives the window, for queueing UI
  // thread work from the guest thread without touching the window.
  std::atomic<rex::ui::WindowedAppContext*> ui_context_{nullptr};
  std::atomic<bool> has_focus_{false};  // from the window's focus events

  // Mouse capture state. Shared with functions queued to the UI thread, which
  // can run after the driver is gone, so they hold this rather than `this`.
  // ui_window is set and cleared on the UI thread (cleared when the window
  // closes), so a queued function never reaches a destroyed window.
  struct UiState {
    // UI thread only.
    rex::ui::Window* ui_window = nullptr;
    bool captured = false;  // cursor hidden, pointer locked or recentered
    bool relative = false;  // pointer locked (SDL relative mouse mode)
    bool relative_failure_logged = false;
    rex::ui::Window::CursorVisibility precapture_cursor_visibility =
        rex::ui::Window::CursorVisibility::kVisible;
    int32_t prev_mouse_x = 0;
    int32_t prev_mouse_y = 0;
    // Any thread.
    std::atomic<bool> capture_requested{false};
    std::atomic<bool> capture_update_queued{false};
    std::mutex mouse_mutex;  // guards the deltas
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;
  };
  std::shared_ptr<UiState> ui_ = std::make_shared<UiState>();

  bool KeyDown(uint16_t vk) const {
    return vk < keys_.size() && keys_[vk].load(std::memory_order_relaxed);
  }
  void SetKey(rex::ui::VirtualKey vk, bool down) {
    auto i = static_cast<uint16_t>(vk);
    if (i < keys_.size()) keys_[i].store(down, std::memory_order_relaxed);
  }
  void ClearKeys() {
    for (auto& k : keys_) k.store(false, std::memory_order_relaxed);
  }

  // Listener registration touches the window's listener lists, which the UI
  // thread iterates, so it is done on the UI thread. The highest z-order puts
  // this listener first, ahead of any overlay that marks keys handled. The
  // queued function may use `this`: the destructor detaches synchronously on
  // the UI thread, after it.
  void AttachToWindow(rex::ui::Window* window) {
    rex::ui::Window* expected = nullptr;
    if (!attached_window_.compare_exchange_strong(expected, window)) return;
    rex::ui::WindowedAppContext& ctx = window->app_context();
    ui_context_.store(&ctx, std::memory_order_release);
    ctx.CallInUIThread([this, window] {
      if (attached_window_.load(std::memory_order_acquire) != window) return;
      ui_->ui_window = window;
      has_focus_.store(window->HasFocus(), std::memory_order_relaxed);
      window->AddInputListener(this, std::numeric_limits<size_t>::max());
      window->AddListener(this);
    });
  }
  // From the destructor (any thread) or OnClosing (UI thread), while the
  // window is still alive.
  void DetachFromWindow() {
    rex::ui::Window* window = attached_window_.exchange(nullptr);
    if (!window) return;
    has_focus_.store(false, std::memory_order_relaxed);
    auto detach = [this, window] {
      UiState& ui = *ui_;
      if (ui.ui_window == window) {
        ReleaseMouseCapture(ui);
        ui.ui_window = nullptr;
      }
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    };
    rex::ui::WindowedAppContext& ctx = window->app_context();
    if (ctx.IsInUIThread()) {
      detach();
    } else {
      ctx.CallInUIThreadSynchronous(detach);
    }
  }

  // Cursor visibility and the pointer lock are window (Cocoa/X11/SDL) calls,
  // so they run on the UI thread; the guest thread only forwards changes of
  // the request, coalesced into one queued update.
  void RequestMouseCapture(bool capture) {
    if (ui_->capture_requested.exchange(capture, std::memory_order_relaxed) == capture) return;
    rex::ui::WindowedAppContext* ctx = ui_context_.load(std::memory_order_acquire);
    if (!ctx) return;
    bool already_queued = false;
    if (!ui_->capture_update_queued.compare_exchange_strong(already_queued, true)) return;
    std::shared_ptr<UiState> ui = ui_;
    if (!ctx->CallInUIThreadDeferred([ui] {
          ui->capture_update_queued.store(false, std::memory_order_relaxed);
          ApplyMouseCapture(*ui);
        })) {
      ui_->capture_update_queued.store(false, std::memory_order_relaxed);
    }
  }
  static void ApplyMouseCapture(UiState& ui) {
    rex::ui::Window* window = ui.ui_window;
    if (!window) return;
    bool capture = ui.capture_requested.load(std::memory_order_relaxed);
    if (capture == ui.captured) return;
    if (!capture) {
      ReleaseMouseCapture(ui);
      return;
    }
    ui.captured = true;
    ui.precapture_cursor_visibility = window->GetCursorVisibility();
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    // Tried again on every capture: it can fail while the window is not key
    // yet, for example.
    ui.relative = window->SetRelativeMouseMode(true);
    if (!ui.relative && !ui.relative_failure_logged) {
      ui.relative_failure_logged = true;
      REXSYS_WARN("[input] pointer lock unavailable; mouse look recenters the cursor instead");
    }
    // No jump from movement before the capture.
    std::lock_guard<std::mutex> lock(ui.mouse_mutex);
    ui.mouse_dx = ui.mouse_dy = 0.0f;
  }
  static void ReleaseMouseCapture(UiState& ui) {
    rex::ui::Window* window = ui.ui_window;
    if (!window || !ui.captured) return;
    ui.captured = false;
    if (ui.relative) {
      window->SetRelativeMouseMode(false);
      ui.relative = false;
    }
    window->SetCursorVisibility(ui.precapture_cursor_visibility);
  }
  // Recentering fallback: warp only once the pointer has drifted well off the
  // middle, to keep warps rare.
  static void RecenterCursor(UiState& ui, int32_t x, int32_t y) {
    rex::ui::Window* window = ui.ui_window;
    if (!window) return;
    int32_t width = int32_t(window->GetActualPhysicalWidth());
    int32_t height = int32_t(window->GetActualPhysicalHeight());
    if (width <= 0 || height <= 0) return;
    if (std::abs(x - width / 2) < width / 4 && std::abs(y - height / 2) < height / 4) return;
    int32_t center_x = 0;
    int32_t center_y = 0;
    if (window->WarpMouseToCenter(center_x, center_y)) {
      ui.prev_mouse_x = center_x;
      ui.prev_mouse_y = center_y;
    }
  }
#endif
  // The game window (from the runtime's display window), used for recentering
  // and cursor visibility.
  rex::ui::Window* GameWindow() {
    auto* rt = rex::Runtime::instance();
    if (!rt) return nullptr;
    return rt->display_window();
  }
};

}  // namespace fable2
