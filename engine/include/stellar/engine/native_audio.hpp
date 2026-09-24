#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
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

// Incremental pull decoder: the same Media Foundation pipeline as
// decode_audio_clip, exposed as a bounded-memory stream for music and
// other long programs. Neither the 16 MiB source cap nor the 96 MiB
// decoded cap applies — the file is read and decoded on demand, so
// arbitrarily long tracks cost a fixed small footprint. The MF pipeline
// binds on the first read(); all reads must come from one consistent
// thread (the AudioOutput owner thread when used for music).
class AudioStreamDecoder final {
 public:
  ~AudioStreamDecoder();
  AudioStreamDecoder(const AudioStreamDecoder&) = delete;
  AudioStreamDecoder& operator=(const AudioStreamDecoder&) = delete;
  AudioStreamDecoder(AudioStreamDecoder&&) = delete;
  AudioStreamDecoder& operator=(AudioStreamDecoder&&) = delete;

  // Writes up to out.size() interleaved samples; returns the count
  // written (always whole stereo frames, 0 at end-of-stream).
  std::size_t read(std::span<float> out);
  // Returns to the start of the stream — looping music replays without
  // reopening the file or rebinding the decoder.
  void rewind();
  [[nodiscard]] bool finished() const noexcept;

 private:
  struct Storage;
  explicit AudioStreamDecoder(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend std::shared_ptr<AudioStreamDecoder> open_audio_stream(
      const std::filesystem::path&);
};

// Opens a pull decoder over a media file on disk. The object is cheap
// to create on any thread; the Media Foundation reader binds lazily on
// first read() so the decoding thread owns the COM pipeline.
[[nodiscard]] std::shared_ptr<AudioStreamDecoder>
open_audio_stream(const std::filesystem::path& path);

// Stream source/decode failures — corrupt or unreadable media rather
// than an output-device fault. Consumers can classify these as
// permanent asset failures instead of arming device recovery.
class AudioStreamError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct AudioDiagnostics final {
  std::size_t queued_music_bytes{};
  std::size_t music_queue_limit_bytes{};
  std::size_t active_effects{};
  std::uint64_t effect_play_count{};
  bool music_started{};
  bool music_streaming{};
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
  // Streamed variant: pulls decoded PCM from the source on demand under
  // the same bounded queue — long tracks cost constant memory instead
  // of a whole-file decode. Loops by rewinding the decoder at
  // end-of-stream; replaying the same decoder is idempotent.
  void play_music(std::shared_ptr<AudioStreamDecoder> decoder);
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
