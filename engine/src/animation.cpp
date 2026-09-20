#include <stellar/engine/animation.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace stellar::engine {

float ease(float t, Easing mode) noexcept {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (mode) {
  case Easing::SmoothStep: return t * t * (3.0f - 2.0f * t);
  case Easing::EaseIn: return t * t;
  case Easing::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
  case Easing::EaseInOut:
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
  default: return t;
  }
}

float wrap_time(float time, float duration, LoopMode mode) noexcept {
  if (duration <= 0.0f)
    return 0.0f;
  switch (mode) {
  case LoopMode::Loop: {
    const auto wrapped = std::fmod(time, duration);
    return wrapped < 0.0f ? wrapped + duration : wrapped;
  }
  case LoopMode::PingPong: {
    const auto cycle = duration * 2.0f;
    auto wrapped = std::fmod(time, cycle);
    if (wrapped < 0.0f)
      wrapped += cycle;
    return wrapped > duration ? cycle - wrapped : wrapped;
  }
  default:
    return std::clamp(time, 0.0f, duration);
  }
}

void FloatCurve::add_key(float time, float value, Easing easing) {
  const Keyframe key{time, value, easing};
  const auto position =
      std::lower_bound(keys_.begin(), keys_.end(), key,
                       [](const Keyframe &a, const Keyframe &b) {
                         return a.time < b.time;
                       });
  keys_.insert(position, key);
}

float FloatCurve::evaluate(float time) const {
  if (keys_.empty())
    return 0.0f;
  if (time <= keys_.front().time)
    return keys_.front().value;
  if (time >= keys_.back().time)
    return keys_.back().value;
  const auto next =
      std::upper_bound(keys_.begin(), keys_.end(), time,
                       [](float t, const Keyframe &k) { return t < k.time; });
  const auto prev = std::prev(next);
  const auto span = next->time - prev->time;
  const auto t = span > 0.0f ? (time - prev->time) / span : 0.0f;
  const auto eased = ease(t, next->easing);
  return prev->value + (next->value - prev->value) * eased;
}

float FloatCurve::duration() const {
  return keys_.empty() ? 0.0f : keys_.back().time;
}

FloatCurve &Timeline::track(std::string_view name) {
  return tracks_[std::string(name)];
}
const FloatCurve *Timeline::track(std::string_view name) const {
  const auto found = tracks_.find(std::string(name));
  return found == tracks_.end() ? nullptr : &found->second;
}

void Timeline::add_event(float time, std::string name) {
  events_.push_back(TimelineEvent{time, std::move(name)});
  std::sort(events_.begin(), events_.end(),
            [](const auto &a, const auto &b) { return a.time < b.time; });
}

std::unordered_map<std::string, float> Timeline::evaluate(float time) const {
  std::unordered_map<std::string, float> result;
  for (const auto &[name, curve] : tracks_)
    result.emplace(name, curve.evaluate(time));
  return result;
}

std::vector<TimelineEvent>
Timeline::events_crossed(float previous, float current, LoopMode mode) const {
  std::vector<TimelineEvent> result;
  if (duration <= 0.0f || current < previous)
    return result;
  const float a = wrap_time(previous, duration, mode);
  const float b = wrap_time(current, duration, mode);
  for (const auto &event : events_) {
    bool crossed = false;
    if (mode == LoopMode::Once) {
      crossed = event.time > a && event.time <= b;
    } else {
      // Wrapping modes: report crossings in each traversal segment. A loop
      // boundary inside (previous, current] splits the check.
      const auto prev_cycle = static_cast<std::int64_t>(
          std::floor(previous / duration));
      const auto cur_cycle = static_cast<std::int64_t>(
          std::floor(current / duration));
      if (cur_cycle - prev_cycle > 1) {
        crossed = true; // full cycles elapsed: every event fired
      } else if (cur_cycle > prev_cycle) {
        // crossed a loop boundary: (a, duration] then [0, b]
        crossed = event.time > a || event.time <= b;
      } else {
        crossed = event.time > a && event.time <= b;
      }
      if (mode == LoopMode::PingPong) {
        // During the mirrored half the direction reverses; approximate by
        // using wrapped times only when inside the forward half.
        crossed = event.time > std::min(a, b) &&
                  event.time <= std::max(a, b);
      }
    }
    if (crossed)
      result.push_back(event);
  }
  return result;
}

} // namespace stellar::engine
