// Synthetic standalone test: no SDL, game process, guest memory or audio device.
#include <rex/audio/clocked_audio_sink.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  using namespace std::chrono_literals;
  std::atomic<unsigned> consumed = 0;
  rex::audio::ClockedAudioSink sink([&] { ++consumed; });
  if (sink.Submit() || !sink.Start() || sink.Start()) return 1;
  std::this_thread::sleep_for(20ms);
  if (consumed != 0) return 2;  // No phantom permits while idle.
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 8; ++i) if (!sink.Submit()) return 3;
  while (consumed < 8 && std::chrono::steady_clock::now() - start < 2s)
    std::this_thread::sleep_for(1ms);
  auto elapsed = std::chrono::steady_clock::now() - start;
  if (consumed != 8 || elapsed < 35ms) return 4;
  sink.Stop();
  auto stopped = consumed.load();
  std::this_thread::sleep_for(20ms);
  if (consumed != stopped || sink.Submit()) return 5;
  if (!sink.Start() || !sink.Submit()) return 6;
  sink.Stop();  // Pending frame: joins, discards queue, no late callback.
  stopped = consumed.load();
  std::this_thread::sleep_for(20ms);
  if (consumed != stopped) return 7;
  std::atomic<unsigned> second_count = 0;
  rex::audio::ClockedAudioSink second([&] { ++second_count; });
  if (!sink.Start() || !second.Start()) return 8;
  if (!sink.Submit() || !second.Submit()) return 9;
  auto wait_start = std::chrono::steady_clock::now();
  while ((consumed == stopped || second_count == 0) &&
         std::chrono::steady_clock::now() - wait_start < 2s)
    std::this_thread::sleep_for(1ms);
  sink.Stop();
  second.Stop();
  if (consumed != stopped + 1 || second_count != 1) return 10;
  std::cout << "PASS: paced consumption, idle, shutdown, restart and independent clients\n";
}
