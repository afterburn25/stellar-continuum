#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace stellar::engine {
// Bounded, allocation-free measurements owned by the calling thread. These are
// observations only: never serialize them into simulation state or use them
// as inputs to scheduling, RNG, AI, or other gameplay decisions.
struct PerformanceCounter {
  std::uint64_t samples{},total_nanoseconds{},maximum_nanoseconds{};
  void record(std::uint64_t elapsed) noexcept {
    ++samples;total_nanoseconds+=elapsed;maximum_nanoseconds=std::max(maximum_nanoseconds,elapsed);
  }
};
class PhaseTimer {
public:
  explicit PhaseTimer(bool enabled) noexcept :enabled_(enabled),since_(enabled?Clock::now():Clock::time_point{}){}
  void finish(PerformanceCounter &counter) noexcept {
    if(!enabled_)return;
    const auto now=Clock::now();
    counter.record(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now-since_).count()));
    since_=now;
  }
private:
  using Clock=std::chrono::steady_clock;
  bool enabled_{};
  Clock::time_point since_;
};
}
