#include "native_audio.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_audio;

namespace {
int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
std::vector<float> mix_seconds(NativeAudioMixer &mixer, const double seconds) {
  const auto frames =
      static_cast<std::size_t>(seconds * output_sample_rate);
  std::vector<float> out(frames * output_channels);
  mixer.mix(out.data(), frames);
  return out;
}
bool any_nonzero(const std::vector<float> &samples) {
  for (const float v : samples)
    if (v != 0.f) return true;
  return false;
}
bool all_bounded(const std::vector<float> &samples) {
  for (const float v : samples)
    if (v < -1.f || v > 1.f) return false;
  return true;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: native_audio_tests <asset-root> [temp-dir]\n";
    return 2;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path temp =
      argc > 2 ? std::filesystem::path(argv[2])
               : std::filesystem::temp_directory_path() / "stellar-audio-tests";

  // A truncated RIFF header and garbage must reject without throwing.
  check(!decode_wav({}), "empty wav must reject");
  check(!decode_wav("RIFF\x04\x00\x00\x00WAVE"),
        "truncated wav must reject");
  check(!decode_mp3("not an mp3 stream at all"), "garbage mp3 must reject");
  check(!decode_audio_file(root / "does-not-exist.wav"),
        "missing file must reject");

  NativeAudioMixer mixer;
  check(!mixer.has_required_audio(), "unloaded mixer must report no audio");
  check(!mixer.music_playing(), "unloaded mixer must not play music");
  check(mixer.load_assets(root), "required audio assets must load");
  check(mixer.has_required_audio(), "loaded mixer must report required audio");
  check(mixer.music_frame_count() > output_sample_rate,
        "music loop must decode at least one second");

  // Missing assets must fail gracefully, not throw.
  NativeAudioMixer empty;
  check(!empty.load_assets(root / "missing-root"),
        "missing asset root must fail closed");

  // Mixing before startup completion is silent; afterwards the music bed
  // produces bounded output on every chunk.
  auto before = mix_seconds(mixer, .05);
  check(!any_nonzero(before), "mixer must stay silent before startup ready");
  mixer.complete_startup_loading();
  check(mixer.music_playing(), "startup completion must start the music bed");
  auto music = mix_seconds(mixer, .1);
  check(any_nonzero(music), "music bed must produce output");
  check(all_bounded(music), "mixed music must stay within [-1,1]");

  // SFX voices layer on the music bed and respect the eight-voice bound.
  mixer.set_voice_ducking(false);
  for (int i = 0; i < 16; ++i) mixer.play(NativeSfx::ui_confirm);
  check(mixer.active_voices() == maximum_sfx_voices,
        "sfx voices must cap at the reference polyphony");
  mixer.play_event("combat");
  mixer.play_event("unknown-category");
  check(mixer.active_voices() <= maximum_sfx_voices,
        "event sounds must respect the polyphony bound");
  auto layered = mix_seconds(mixer, .05);
  check(any_nonzero(layered), "sfx voices must produce output");
  check(all_bounded(layered), "layered output must stay bounded");
  check(mixer.active_voices() <= maximum_sfx_voices,
        "finished voices must retire");

  // Event categories route to the reference mappings.
  mixer.play_event("research");
  mixer.play_event("industry");
  mixer.play_event("ships");
  check(mixer.active_voices() <= maximum_sfx_voices,
        "event routing must remain bounded");

  // Hover debounce: a burst of hovers must not stack voices.
  (void)mix_seconds(mixer, 3.); // retire every earlier voice
  for (int i = 0; i < 8; ++i) mixer.play_hover();
  check(mixer.active_voices() <= 1,
        "hover debounce must limit repeated hover voices");

  // Volume changes persist to the settings path and reload.
  const auto settings_path = temp / "audio-settings.json";
  std::error_code error;
  std::filesystem::create_directories(temp, error);
  NativeAudioMixer persistent(settings_path);
  persistent.set_volumes(.5f, .25f, .9f);
  const auto written = persistent.settings();
  check(std::abs(written.master - .5f) < 1e-5f &&
            std::abs(written.music - .25f) < 1e-5f &&
            std::abs(written.sfx - .9f) < 1e-5f,
        "volume settings must round-trip through the mixer");
  check(std::filesystem::exists(settings_path),
        "volume changes must persist the settings file");
  {
    std::ifstream file(settings_path);
    const std::string text{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    check(text.find("\"master\"") != std::string::npos,
          "settings file must store the master volume");
  }
  NativeAudioMixer reloaded(settings_path);
  const auto restored = reloaded.settings();
  check(std::abs(restored.master - .5f) < 1e-5f &&
            std::abs(restored.sfx - .9f) < 1e-5f,
        "persisted volumes must reload into a new mixer");

  // Corrupt settings must fall back to defaults without throwing.
  std::filesystem::create_directories(temp / "broken", error);
  {
    std::ofstream file(temp / "broken" / "audio-settings.json");
    file << "{\"master\":";
  }
  NativeAudioMixer tolerant(temp / "broken" / "audio-settings.json");
  const auto defaults = tolerant.settings();
  check(defaults.master > 0.f && defaults.music > 0.f && defaults.sfx > 0.f,
        "corrupt settings must fall back to defaults");

  // Voice ducking attenuates the music bed over the ramp window.
  NativeAudioMixer ducking;
  if (ducking.load_assets(root)) {
    ducking.complete_startup_loading();
    ducking.set_voice_ducking(true);
    auto ducked = mix_seconds(ducking, .5);
    check(any_nonzero(ducked) && all_bounded(ducked),
          "voice ducking must keep output bounded");
  }

  if (failures) {
    std::cerr << failures << " audio check(s) failed\n";
    return 1;
  }
  std::cout << "native_audio: all checks passed\n";
  return 0;
}
