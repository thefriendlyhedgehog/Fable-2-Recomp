// Synthetic standalone test; no game or GPU.
#include <rex/graphics/frame_limiter.h>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  using Clock = std::chrono::steady_clock;
  using namespace std::chrono_literals;
  rex::graphics::FrameLimiter limiter;
  for (int rate : {30, 60, 144}) {
    limiter.Pace(0);
    auto start = Clock::now();
    for (int i = 0; i < 7; ++i) limiter.Pace(rate);
    auto elapsed = Clock::now() - start;
    if (elapsed < std::chrono::duration<double>(5.9 / rate)) return 1;
    std::cout << rate << " FPS: "
              << std::chrono::duration<double>(elapsed).count() << " seconds for 6 intervals\n";
  }
  // Disabled/invalid modes and changing rates reset without an old deadline.
  for (int rate : {0, -1, 241, 30, 0, 60, 30, 60}) {
    auto start = Clock::now();
    limiter.Pace(rate);
    if (Clock::now() - start > 15ms) return 2;
  }
  std::this_thread::sleep_for(100ms);
  limiter.Pace(60);
  auto start = Clock::now();
  limiter.Pace(60);
  if (Clock::now() - start < 15ms) return 3;  // No late-frame catch-up burst.
  std::cout << "PASS: caps, disabled mode, rate changes and late-frame reset\n";
}
