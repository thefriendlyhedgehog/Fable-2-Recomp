// fable_2 - Debug sweep: step through GPU debug settings and save a
// screenshot of each, so one keypress while standing still captures an A/B
// comparison that can be judged afterwards instead of live.
//
// Started by the 0 key (or fn+F10) on macOS (src/input/keyboard_gamepad.h).
// The steps come from the debug_sweep_steps cvar:
//   label:cvar=value,cvar=value;label:cvar=value;...
// Each step applies its assignments on top of the previous step's, waits
// debug_sweep_settle_ms, then saves <debug_sweep_dir>/NN_label.png via the
// presenter's guest output readback (src/diagnostics/fable2_frame_capture.h,
// the same capture the debug-build remote control `screenshot` uses, without
// any network port). Afterwards every cvar the steps touched gets its value
// from before the sweep back. tools/mac_sweep_sheet.py tiles the PNGs into
// one numbered contact sheet.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

#include "fable2_frame_capture.h"

REXCVAR_DECLARE(std::string, debug_sweep_steps);
REXCVAR_DECLARE(std::string, debug_sweep_dir);
REXCVAR_DECLARE(int32_t, debug_sweep_settle_ms);

namespace fable2::debugsweep {

struct Step {
  std::string label;
  std::vector<std::pair<std::string, std::string>> assignments;
};

inline std::string Trim(std::string s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  size_t e = s.find_last_not_of(" \t\r\n");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// "label:a=1,b=2;label2:c=3" -> steps. A step without ':' is all label.
inline std::vector<Step> ParseSteps(const std::string& text) {
  std::vector<Step> steps;
  size_t begin = 0;
  while (begin <= text.size()) {
    size_t end = text.find(';', begin);
    if (end == std::string::npos) end = text.size();
    std::string item = Trim(text.substr(begin, end - begin));
    begin = end + 1;
    if (item.empty()) continue;
    Step step;
    size_t colon = item.find(':');
    step.label = Trim(item.substr(0, colon));
    if (colon != std::string::npos) {
      std::string list = item.substr(colon + 1);
      size_t a = 0;
      while (a <= list.size()) {
        size_t comma = list.find(',', a);
        if (comma == std::string::npos) comma = list.size();
        std::string assignment = Trim(list.substr(a, comma - a));
        a = comma + 1;
        size_t eq = assignment.find('=');
        if (eq == std::string::npos) continue;
        step.assignments.emplace_back(Trim(assignment.substr(0, eq)),
                                      Trim(assignment.substr(eq + 1)));
      }
    }
    steps.push_back(std::move(step));
  }
  return steps;
}

class Sweeper {
 public:
  using PresenterProvider = std::function<rex::ui::Presenter*()>;

  ~Sweeper() { Stop(); }

  void SetPresenterProvider(PresenterProvider provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    provider_ = std::move(provider);
  }

  // From any thread (the guest input poll). Ignored while a sweep runs.
  void Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) {
      REXSYS_INFO("[sweep] already running");
      return;
    }
    if (!provider_) {
      REXSYS_WARN("[sweep] no presenter provider; sweep unavailable");
      return;
    }
    if (thread_.joinable()) thread_.join();
    std::vector<Step> steps = ParseSteps(REXCVAR_GET(debug_sweep_steps));
    if (steps.empty()) {
      REXSYS_WARN("[sweep] debug_sweep_steps is empty");
      return;
    }
    std::filesystem::path dir = REXCVAR_GET(debug_sweep_dir);
    int32_t settle_ms = REXCVAR_GET(debug_sweep_settle_ms);
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this, steps = std::move(steps), dir, settle_ms, provider = provider_] {
      Run(steps, dir, settle_ms, provider);
      running_.store(false);
    });
  }

  // Before the window and the GPU go away (app shutdown).
  void Stop() {
    stop_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run(const std::vector<Step>& steps, const std::filesystem::path& dir, int32_t settle_ms,
           const PresenterProvider& provider) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // The values to restore afterwards, in first-touched order.
    std::vector<std::pair<std::string, std::string>> original;
    for (const Step& step : steps) {
      for (const auto& [name, value] : step.assignments) {
        bool known = false;
        for (const auto& o : original) known |= o.first == name;
        if (!known) original.emplace_back(name, rex::cvar::Query<std::string>(name));
      }
    }
    REXSYS_INFO("[sweep] {} steps, saving to {}", steps.size(), dir.string());
    for (size_t i = 0; i < steps.size() && !stop_.load(); ++i) {
      const Step& step = steps[i];
      for (const auto& [name, value] : step.assignments) {
        if (!rex::cvar::SetFlagByName(name, value)) {
          REXSYS_WARN("[sweep] step {} ({}): could not set {} = {}", i, step.label, name, value);
        }
      }
      for (int32_t waited = 0; waited < settle_ms && !stop_.load(); waited += 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (stop_.load()) break;
      char name[160];
      std::snprintf(name, sizeof(name), "%02zu_%s.png", i, step.label.c_str());
      std::filesystem::path path = dir / name;
      fable2::framecapture::Result r =
          fable2::framecapture::CaptureGuestOutputToPng(provider(), path.string());
      if (r.ok) {
        REXSYS_INFO("[sweep] {:02} {}: saved {}x{} (average brightness {})", i, step.label,
                    r.width, r.height, r.avg_luma);
      } else {
        REXSYS_WARN("[sweep] {:02} {}: screenshot failed: {}", i, step.label, r.error);
      }
    }
    for (const auto& [name, value] : original) {
      rex::cvar::SetFlagByName(name, value);
    }
    REXSYS_INFO("[sweep] done; settings restored");
  }

  std::mutex mutex_;
  PresenterProvider provider_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

inline Sweeper& Instance() {
  static Sweeper sweeper;
  return sweeper;
}

}  // namespace fable2::debugsweep
