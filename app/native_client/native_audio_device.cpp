#include "native_audio_device.hpp"

#include <SDL3/SDL.h>

#include <cstddef>

namespace stellar::native_audio {
namespace {
void SDLCALL feed_stream(void *userdata, SDL_AudioStream *stream,
                         int additional_amount, int /*total_amount*/) {
  auto &mixer = *static_cast<NativeAudioMixer *>(userdata);
  if (additional_amount <= 0) return;
  constexpr std::size_t frame_bytes =
      output_channels * static_cast<std::size_t>(sizeof(float));
  float buffer[output_channels * 480]{};
  int remaining = additional_amount;
  while (remaining > 0) {
    const auto want_bytes =
        std::min<int>(remaining, static_cast<int>(sizeof(buffer)));
    mixer.mix(buffer, static_cast<std::size_t>(want_bytes) / frame_bytes);
    SDL_PutAudioStreamData(stream, buffer, want_bytes);
    remaining -= want_bytes;
  }
}
} // namespace

NativeAudioDevice::~NativeAudioDevice() { close(); }

bool NativeAudioDevice::open(NativeAudioMixer &mixer) noexcept {
  close();
  if (!mixer.has_required_audio()) return false;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
  subsystem_initialized_ = true;
  const SDL_AudioSpec spec{SDL_AUDIO_F32, output_channels, output_sample_rate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      feed_stream, &mixer);
  if (!stream_) {
    close();
    return false;
  }
  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    close();
    return false;
  }
  return true;
}

void NativeAudioDevice::close() noexcept {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  if (subsystem_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    subsystem_initialized_ = false;
  }
}
} // namespace stellar::native_audio
