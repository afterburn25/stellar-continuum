#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace stellar::engine::audio {

inline constexpr std::size_t maximum_source_audio_bytes = 16u * 1024u * 1024u;
inline constexpr std::size_t maximum_decoded_audio_bytes = 96u * 1024u * 1024u;
inline constexpr std::size_t maximum_effect_audio_bytes = 1u * 1024u * 1024u;
inline constexpr std::size_t maximum_voice_audio_bytes = 8u * 1024u * 1024u;
inline constexpr int audio_sample_rate = 48000;
inline constexpr int audio_channels = 2;

// Immutable, normalized interleaved F32 stereo PCM at 48 kHz.
class AudioClip final {
 public:
  [[nodiscard]] static std::shared_ptr<const AudioClip> create(std::vector<float> samples);
  [[nodiscard]] std::span<const float> samples() const noexcept;
  [[nodiscard]] std::size_t byte_size() const noexcept;
  [[nodiscard]] std::uint64_t sample_frames() const noexcept;

 private:
  explicit AudioClip(std::vector<float> samples) noexcept;
  std::vector<float> samples_;
};

// Decodes supported Windows Media Foundation audio formats (including WAV and MP3).
// The caller is expected to invoke this off the owner/UI thread.
[[nodiscard]] std::shared_ptr<const AudioClip> decode_audio_clip(const std::filesystem::path& path);
// In-memory variant for cooked packages and embedded content; `label` is
// only used for error/diagnostic context.
[[nodiscard]] std::shared_ptr<const AudioClip> decode_audio_clip(
    std::span<const std::uint8_t> encoded, const std::filesystem::path& label);

struct AudioDiagnostics final {
  std::size_t queued_music_bytes{};
  std::size_t music_queue_limit_bytes{};
  std::size_t active_effects{};
  std::uint64_t effect_play_count{};
  bool music_started{};
  bool voice_active{};
  std::size_t queued_voice_bytes{};
  std::size_t available_voice_bytes{};
  std::size_t voice_queue_limit_bytes{};
  std::uint64_t voice_play_count{};
  float applied_music_gain{};
  float applied_voice_gain{};
};

// Owner-thread-pinned SDL output. It owns only SDL's audio subsystem reference.
class AudioOutput final {
 public:
  AudioOutput();
  ~AudioOutput() noexcept;
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  AudioOutput(AudioOutput&&) = delete;
  AudioOutput& operator=(AudioOutput&&) = delete;

  // Replaying the currently selected clip is idempotent and preserves its queue.
  void play_music(std::shared_ptr<const AudioClip> clip);
  void stop_music();
  void play_effect(std::shared_ptr<const AudioClip> clip);
  void play_voice(std::shared_ptr<const AudioClip> clip);
  void stop_voice();
  void set_volumes(float master, float music, float effects);
  void set_voice_gain(float voice);
  // Feed bounded music queues and retire completed effects. Call once per frame.
  void service();
  void stop_all();
  [[nodiscard]] AudioDiagnostics diagnostics() const;

 private:
  struct Storage;
  void require_owner() const;
  void apply_gains();
  void cleanup_unchecked() noexcept;

  std::thread::id owner_;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::engine::audio
