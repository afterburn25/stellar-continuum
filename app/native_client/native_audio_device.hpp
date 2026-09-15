#pragma once

#include "native_audio.hpp"

struct SDL_AudioStream;

namespace stellar::native_audio {

// SDL3 audio-device glue for NativeAudioMixer. The mixer stays headless; this
// wrapper owns the subsystem init, device stream, and callback thread. Audio
// failure must never fail the campaign — open() returns false instead of
// throwing.
class NativeAudioDevice final {
public:
  NativeAudioDevice() = default;
  NativeAudioDevice(const NativeAudioDevice &) = delete;
  NativeAudioDevice &operator=(const NativeAudioDevice &) = delete;
  ~NativeAudioDevice();

  // Initialises SDL_INIT_AUDIO, opens a 48 kHz float stream bound to the mixer,
  // and resumes playback. Returns false and leaves the object closed when the
  // host has no usable audio device.
  bool open(NativeAudioMixer &mixer) noexcept;
  void close() noexcept;
  [[nodiscard]] bool is_open() const noexcept { return stream_ != nullptr; }

private:
  SDL_AudioStream *stream_{};
  bool subsystem_initialized_{};
};

} // namespace stellar::native_audio
