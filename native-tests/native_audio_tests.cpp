#include <stellar/engine/native_audio.hpp>

#include <cmath>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures{};
void check(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; ++failures; }
}

template <typename Function>
bool rejects(Function&& function) {
  try { function(); } catch (const std::exception&) { return true; }
  return false;
}

template <typename Predicate>
bool wait_until(Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  return predicate();
}

class FixtureDirectory final {
 public:
  FixtureDirectory() {
    const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
      path_ = std::filesystem::temp_directory_path() /
              (L"stellar-native-audio-测试-" + std::to_wstring(seed) + L"-" + std::to_wstring(attempt));
      if (std::filesystem::create_directory(path_)) return;
    }
    throw std::runtime_error("Could not create an isolated audio test directory.");
  }
  ~FixtureDirectory() {
    std::error_code ignored;
    std::filesystem::remove(path_ / L"truncated-测试.bin", ignored);
    std::filesystem::remove(path_ / L"oversized-测试.bin", ignored);
    std::filesystem::remove(path_ / L"resample-44100-测试.wav", ignored);
    std::filesystem::remove(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
 private:
  std::filesystem::path path_;
};

void write_u16(std::ofstream& output, std::uint16_t value) {
  const std::array<char, 2> bytes{static_cast<char>(value & 0xffu), static_cast<char>(value >> 8u)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ofstream& output, std::uint32_t value) {
  const std::array<char, 4> bytes{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
                                  static_cast<char>((value >> 16u) & 0xffu), static_cast<char>(value >> 24u)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_44100_mono_wave(const std::filesystem::path& path) {
  constexpr std::uint32_t frames = 441;
  constexpr std::uint32_t bytes = frames * sizeof(std::int16_t);
  std::ofstream output(path, std::ios::binary);
  output.write("RIFF", 4); write_u32(output, 36u + bytes); output.write("WAVEfmt ", 8); write_u32(output, 16);
  write_u16(output, 1); write_u16(output, 1); write_u32(output, 44100); write_u32(output, 88200);
  write_u16(output, 2); write_u16(output, 16); output.write("data", 4); write_u32(output, bytes);
  for (std::uint32_t frame = 0; frame < frames; ++frame) write_u16(output, static_cast<std::uint16_t>((frame % 32u) * 512u));
  if (!output) throw std::runtime_error("Could not write the 44.1 kHz resampling fixture.");
}

} // namespace

int main(int argc, char** argv) {
  using namespace stellar::engine::audio;
  try {
    if (argc != 3) throw std::invalid_argument("Usage: native_audio_tests <MP3> <WAV>");
    const std::filesystem::path mp3 = argv[1], wav = argv[2];
    FixtureDirectory fixtures;
    const auto decoded_mp3 = decode_audio_clip(mp3);
    const auto decoded_wav = decode_audio_clip(wav);
    for (const auto& clip : {decoded_mp3, decoded_wav}) {
      check(clip && clip->sample_frames() > 0, "decoded audio was empty");
      check(clip->samples().size() == clip->sample_frames() * audio_channels,
            "decoded audio was not interleaved stereo");
      check(clip->byte_size() == clip->samples().size() * sizeof(float) &&
                clip->byte_size() <= maximum_decoded_audio_bytes,
            "decoded audio did not respect PCM byte accounting");
      bool normalized = true;
      for (float sample : clip->samples()) normalized = normalized && std::isfinite(sample) && sample >= -1.f && sample <= 1.f;
      check(normalized, "decoded samples were not finite normalized float PCM");
    }
    const auto source_44100 = fixtures.path() / L"resample-44100-测试.wav";
    write_44100_mono_wave(source_44100);
    const auto resampled = decode_audio_clip(source_44100);
    check(resampled->sample_frames() > 0 && resampled->samples().size() == resampled->sample_frames() * audio_channels,
          "44.1 kHz mono WAV was not resampled to canonical stereo PCM");
    check(rejects([&] { (void)decode_audio_clip(mp3.parent_path() / L"missing-测试.mp3"); }),
          "missing audio was accepted");
    const auto truncated = fixtures.path() / L"truncated-测试.bin";
    { std::ofstream out(truncated, std::ios::binary); out << "not audio"; }
    check(rejects([&] { (void)decode_audio_clip(truncated); }), "truncated audio was accepted");
    const auto oversized = fixtures.path() / L"oversized-测试.bin";
    { std::ofstream out(oversized, std::ios::binary); out.seekp(static_cast<std::streamoff>(maximum_source_audio_bytes)); out.put('\0'); }
    check(rejects([&] { (void)decode_audio_clip(oversized); }), "oversized source audio was accepted");

    check(rejects([] { (void)AudioClip::create({}); }), "empty generated clip was accepted");
    check(rejects([] { (void)AudioClip::create({0.f}); }), "mono generated clip was accepted");
    check(rejects([] { (void)AudioClip::create({std::numeric_limits<float>::quiet_NaN(), 0.f}); }),
          "non-finite generated clip was accepted");
    check(rejects([] { (void)AudioClip::create({1.1f, 0.f}); }), "out-of-range generated clip was accepted");

    const auto short_loop = AudioClip::create({0.f, 0.f, 0.25f, -0.25f});
    AudioOutput output;
    const auto idle = output.diagnostics();
    check(!idle.music_started && idle.queued_music_bytes == 0, "audio output started music before play_music");
    output.play_music(short_loop);
    const auto started = output.diagnostics();
    check(started.music_started && started.queued_music_bytes > 0 &&
              started.queued_music_bytes <= started.music_queue_limit_bytes,
          "short music loop did not start with a bounded queue");
    output.play_music(short_loop);
    const auto idempotent = output.diagnostics();
    check(idempotent.music_started && idempotent.queued_music_bytes <= idempotent.music_queue_limit_bytes,
          "same music clip was not idempotent or exceeded queue cap");
    AudioDiagnostics drained = output.diagnostics();
    const auto music_drained = wait_until([&] {
      drained = output.diagnostics();
      return drained.queued_music_bytes < started.queued_music_bytes;
    });
    check(music_drained, "unpaused SDL device did not drain music before the 2-second deadline");
    output.service();
    const auto refilled = output.diagnostics();
    check(refilled.queued_music_bytes > drained.queued_music_bytes &&
              refilled.queued_music_bytes <= refilled.music_queue_limit_bytes,
          "music loop did not refill after drain within its queue cap");

    output.set_volumes(1.f, 0.5f, 0.25f);
    check(rejects([&] { output.set_volumes(-0.1f, 0.5f, 0.5f); }), "negative gain was accepted");
    check(rejects([&] { output.set_volumes(std::numeric_limits<float>::infinity(), 0.5f, 0.5f); }),
          "non-finite gain was accepted");
    for (int voice = 0; voice < 8; ++voice) output.play_effect(short_loop);
    const auto eight = output.diagnostics();
    check(eight.active_effects <= 8 && eight.effect_play_count == 8, "effect voices did not honor the eight-voice bound");
    output.play_effect(short_loop);
    const auto replaced = output.diagnostics();
    check(replaced.active_effects <= 8 && replaced.effect_play_count == 9, "full effect set did not replace its oldest voice");
    std::vector<float> oversized_effect(maximum_effect_audio_bytes / sizeof(float) + audio_channels, 0.f);
    const auto too_large_effect = AudioClip::create(std::move(oversized_effect));
    check(rejects([&] { output.play_effect(too_large_effect); }), "oversized effect was accepted");
    const auto preserved = output.diagnostics();
    check(preserved.active_effects == replaced.active_effects && preserved.effect_play_count == replaced.effect_play_count,
          "oversized effect cleared an existing voice before rejection");
    std::atomic_bool wrong_thread_rejected{};
    std::thread wrong_thread([&] {
      try { (void)output.diagnostics(); } catch (const std::logic_error&) { wrong_thread_rejected = true; }
    });
    wrong_thread.join();
    check(wrong_thread_rejected.load(), "AudioOutput allowed access from a different thread");
    const auto effects_retired = wait_until([&] {
      output.service();
      return output.diagnostics().active_effects == 0;
    });
    check(effects_retired, "finite flushed effects did not drain and retire before the 2-second deadline");
    output.stop_all();
    const auto stopped = output.diagnostics();
    check(!stopped.music_started && stopped.queued_music_bytes == 0 && stopped.active_effects == 0,
          "stop_all did not clear music and effects");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  if (failures == 0) std::cout << "Native Media Foundation decode and bounded SDL audio output passed\n";
  return failures == 0 ? 0 : 1;
}
