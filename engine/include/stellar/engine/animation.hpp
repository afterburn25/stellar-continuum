#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Data-driven animation substrate: keyframed scalar curves grouped into
// timelines, with loop modes and time-based events. Transform/skeletal
// blending build on these curves; celestial rotation stays on its own
// deterministic integrators (not coupled here).

enum class Easing {
  Linear,
  SmoothStep,
  EaseIn,
  EaseOut,
  EaseInOut,
};

enum class LoopMode {
  Once,
  Loop,
  PingPong,
};

struct Keyframe {
  float time{};
  float value{};
  Easing easing{Easing::Linear}; // easing INTO this key from the previous
};

// A single animated scalar channel.
class FloatCurve {
public:
  void add_key(float time, float value, Easing easing = Easing::Linear);
  // Evaluates at `time` with linear/eased interpolation between keys.
  // Clamps to first/last key outside the range.
  float evaluate(float time) const;
  float duration() const;
  std::size_t size() const noexcept { return keys_.size(); }

private:
  std::vector<Keyframe> keys_;
};

struct TimelineEvent {
  float time{};
  std::string name;
};

// A named set of curves plus timed events. evaluate() fills a map of
// track-name -> value; events_crossed(prev, now) reports events whose time
// boundary was crossed during the step (handles looping correctly).
class Timeline {
public:
  float duration{};

  FloatCurve &track(std::string_view name);
  const FloatCurve *track(std::string_view name) const;
  void add_event(float time, std::string name);

  std::unordered_map<std::string, float> evaluate(float time) const;
  // Reports events crossed while advancing `previous` -> `current` under
  // the given loop mode.
  std::vector<TimelineEvent> events_crossed(float previous, float current,
                                            LoopMode mode) const;

private:
  std::unordered_map<std::string, FloatCurve> tracks_;
  std::vector<TimelineEvent> events_; // kept sorted by time
};

// Maps external time onto timeline time under a loop mode.
float wrap_time(float time, float duration, LoopMode mode) noexcept;
float ease(float t, Easing mode) noexcept;

} // namespace stellar::engine
