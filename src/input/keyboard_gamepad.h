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
// and Button is a guest gamepad button name (A/B/X/Y/LB/RB/LT/RT/Up/Down/
// Left/Right/Start/Back/L3/R3/StickUp/StickDown/StickLeft/StickRight).
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
// to the SDK window's key and mouse events and uses SDL relative mouse mode
// for mouse look; the mapping, cvars and guest-side behavior are the same.

#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/keybinds.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <array>
#include <atomic>
#include <limits>
#include <mutex>

#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#endif

// Declared here, defined in main.cpp (REXCVAR_DEFINE_* must live in a .cpp).
REXCVAR_DECLARE(std::string, keyboard_gamepad_map);
REXCVAR_DECLARE(bool, mouse_look);
REXCVAR_DECLARE(int32_t, mouse_look_scale);
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
        auto vk = rex::ui::ParseVirtualKey(entry.substr(0, colon));
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
    , public rex::ui::WindowInputListener
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
#ifndef _WIN32
    DetachFromWindow();
#endif
    if (cursor_hidden_) {
      if (rex::ui::Window* w = GameWindow())
        w->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      cursor_hidden_ = false;
    }
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
    if (!relative_mouse_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mouse_mutex_);
    mouse_dx_ += e.dx();
    mouse_dy_ += e.dy();
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
    rex::ui::Window* win = attached_window_.load(std::memory_order_acquire);
    // Focus gates all host input, as on Windows. Drop held keys on focus loss:
    // their key-up events go to whichever window has focus, not this one.
    bool focused = win != nullptr && win->HasFocus();
    if (!focused) ClearKeys();

    // F5 (host) -> run the external Lua file (fable2_f5_lua.h).
    fable2::f5lua::poll_f5(focused && KeyDown(0x74 /* F5 */));

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

    // Mouse -> right stick. Instead of recentering the cursor every poll, use
    // SDL relative mouse mode: the pointer is locked and hidden, and motion
    // events carry deltas that accumulate between polls (OnMouseMove).
    bool looking = focused && REXCVAR_GET(mouse_look) && !console_open_;
    SetRelativeMouse(win, looking);
    if (looking && !cursor_hidden_) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      cursor_hidden_ = true;
    } else if (!looking && cursor_hidden_ && win) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      cursor_hidden_ = false;
    }

    // Consume whole pixels and keep the fraction for the next poll: macOS
    // trackpads and high-DPI mice report sub-pixel deltas, which truncating
    // per poll would drop, losing slow camera movement.
    int32_t dx = 0;
    int32_t dy = 0;
    {
      std::lock_guard<std::mutex> lock(mouse_mutex_);
      dx = static_cast<int32_t>(mouse_dx_);
      dy = static_cast<int32_t>(mouse_dy_);
      mouse_dx_ -= static_cast<float>(dx);
      mouse_dy_ -= static_cast<float>(dy);
      if (!looking) mouse_dx_ = mouse_dy_ = 0.0f;
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
  bool cursor_hidden_ = false;   // set while the SDK window cursor is hidden
  bool console_open_ = false;    // debug menu open -> mouse lock released
  bool unlock_down_prev_ = false;  // prev-poll state of the unlock key
#ifndef _WIN32
  // Held-key table indexed by rex::ui::VirtualKey (Win32 VK numbering, so the
  // cvar mapping means the same thing on every platform). Written on the UI
  // thread by the listener callbacks, read by the guest input poll.
  std::array<std::atomic<bool>, 256> keys_{};
  std::atomic<rex::ui::Window*> attached_window_{nullptr};
  std::atomic<bool> relative_mouse_{false};
  std::atomic<bool> relative_mouse_failed_{false};  // SetRelativeMouseMode refused
  std::mutex mouse_mutex_;
  float mouse_dx_ = 0.0f;
  float mouse_dy_ = 0.0f;

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

  // Listener registration touches the window's listener list, which the UI
  // thread iterates, so it is done on the UI thread. The highest z-order puts
  // this listener first, ahead of any overlay that marks keys handled.
  void AttachToWindow(rex::ui::Window* window) {
    rex::ui::Window* expected = nullptr;
    if (!attached_window_.compare_exchange_strong(expected, window)) return;
    window->app_context().CallInUIThread([this, window] {
      window->AddInputListener(this, std::numeric_limits<size_t>::max());
    });
  }
  void DetachFromWindow() {
    rex::ui::Window* window = attached_window_.exchange(nullptr);
    if (!window) return;
    auto detach = [this, window] {
      if (relative_mouse_.exchange(false)) window->SetRelativeMouseMode(false);
      window->RemoveInputListener(this);
    };
    auto& ctx = window->app_context();
    if (ctx.IsInUIThread()) {
      detach();
    } else {
      ctx.CallInUIThreadSynchronous(detach);
    }
  }

  // Relative mouse mode is a window (Cocoa/X11) call, so it runs on the UI
  // thread; only state changes are forwarded.
  void SetRelativeMouse(rex::ui::Window* window, bool enable) {
    if (!window || relative_mouse_failed_) return;
    if (relative_mouse_.exchange(enable) == enable) return;
    window->app_context().CallInUIThread([this, window, enable] {
      if (!window->SetRelativeMouseMode(enable) && enable) {
        relative_mouse_.store(false);
        relative_mouse_failed_ = true;
        REXSYS_WARN("[input] relative mouse mode unavailable; mouse look disabled");
      }
    });
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
