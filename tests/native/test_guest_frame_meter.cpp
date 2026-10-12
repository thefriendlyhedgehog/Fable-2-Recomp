// Synthetic timestamp test: no game, GPU or timing-sensitive sleeps.
#include <rex/graphics/guest_frame_meter.h>
#include <iostream>
#include <cstdlib>

int main() {
  using Meter = rex::graphics::GuestFrameMeter;
  using namespace std::chrono;
  for (int rate : {30, 60, 120, 144, 165, 240}) {
    Meter meter;
    auto epoch = Meter::Clock::time_point{};
    if (meter.Record(epoch) != 0) return 1;
    int64_t mean = 0;
    for (int i = 1; i <= rate; ++i)
      mean = meter.Record(epoch + microseconds(int64_t(i) * 1000000 / rate));
    if (std::abs(mean - 1000000 / rate) > 1) return 2;
    std::cout << rate << " FPS: " << mean << " us averaged guest interval\n";
  }
  Meter stalled, independent;
  auto epoch = Meter::Clock::time_point{};
  stalled.Record(epoch);
  if (stalled.Record(epoch + seconds(2)) != 2000000) return 3;
  if (independent.Record(epoch + seconds(2)) != 0) return 4;
  std::cout << "Stall included; separate processors do not share timing.\n";
}
