#pragma once
#include <stellar/engine/native_audio.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace stellar::native_audio {
// A dry, bounded radio-band blend. No delay line, echo, reverb, or duration change.
inline std::shared_ptr<const stellar::engine::audio::AudioClip> communication_clip(
    std::shared_ptr<const stellar::engine::audio::AudioClip> original, float amount) {
  if (!std::isfinite(amount) || amount < 0.f || amount > 1.f || !original)
    throw std::invalid_argument("Invalid communications filter input.");
  if (amount == 0.f) return original;
  if (original->byte_size() > stellar::engine::audio::maximum_voice_audio_bytes)
    throw std::invalid_argument("Communications clip exceeds the voice memory limit.");
  std::vector<float> result(original->samples().begin(), original->samples().end());
  std::array<float, 2> bass{}, treble{};
  const float high_alpha = 1.f - std::exp(-2.f * 3.14159265f * 240.f / 48000.f);
  const float low_alpha = 1.f - std::exp(-2.f * 3.14159265f * 4200.f / 48000.f);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto channel = i % 2;
    const float dry = result[i];
    bass[channel] += high_alpha * (dry - bass[channel]);
    treble[channel] += low_alpha * (dry - bass[channel] - treble[channel]);
    result[i] = std::clamp(dry * (1.f - amount) + treble[channel] * amount, -1.f, 1.f);
  }
  return stellar::engine::audio::AudioClip::create(std::move(result));
}
}
