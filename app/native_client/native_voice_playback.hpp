#pragma once

#include "native_audio.hpp"
#include "native_voice.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace stellar::native_voice {

struct NativeVoiceResult {
  bool succeeded{};
  std::filesystem::path wave_path;
  std::string error;
  bool cache_hit{};
  std::string selected_voice;
};

// Synchronous offline speech backend contract (the reference IVoiceSpeechBackend
// equivalent). The SAPI adapter synthesizes PCM WAV files on its own STA worker.
class INativeSpeechBackend {
public:
  virtual ~INativeSpeechBackend() = default;
  [[nodiscard]] virtual bool available() const noexcept = 0;
  [[nodiscard]] virtual std::string backend_id() const = 0;
  [[nodiscard]] virtual std::string detail() const = 0;
  [[nodiscard]] virtual std::vector<std::string> voices() const = 0;
  [[nodiscard]] virtual std::string
  resolve_voice_id(const NativeVoiceProfile &profile,
                   std::string_view culture) const = 0;
  [[nodiscard]] virtual std::future<NativeVoiceResult>
  synthesize(const NativeVoiceProfile &profile, std::string normalized_text,
             std::filesystem::path wav_path) = 0;
  virtual void clear_pending() = 0;
};

// VoiceCache port: hashed file names under a caller-owned directory, validated
// PCM WAV entries and a bounded byte budget.
class NativeVoiceCache final {
public:
  explicit NativeVoiceCache(std::filesystem::path directory,
                            long long limit_bytes = 256ll * 1024 * 1024);
  [[nodiscard]] const std::filesystem::path &directory() const noexcept {
    return directory_;
  }
  [[nodiscard]] std::filesystem::path
  path_for(const NativeVoiceProfile &, std::string_view normalized) const;
  [[nodiscard]] bool try_get_valid(const std::filesystem::path &) const;
  void trim() const;
  [[nodiscard]] static bool is_valid_wave(const std::filesystem::path &);

private:
  std::filesystem::path directory_;
  long long limit_bytes_;
};

// VoicePlaybackController port without Godot: a single non-spatial dialogue
// voice, an 8-deep priority queue, a 15 s dedupe window, priority interrupts,
// and subtitle state for the caption surface. Speech output is delivered as a
// decoded PcmData the caller routes to its mixer; `speaking` drives the
// voice-duck ramp and caption visibility.
class NativeVoicePlayback final {
public:
  using Stream = std::shared_ptr<const native_audio::PcmData>;
  using Decode =
      std::function<Stream(const std::filesystem::path &)>;
  using Play = std::function<void(Stream, double seconds)>;
  using Stop = std::function<void()>;

  NativeVoicePlayback(NativeVoiceSettings settings,
                      const NativeVoiceProfileRegistry *profiles,
                      const NativeCharacterVoiceResolver *speakers,
                      NativeVoiceCache *cache);
  ~NativeVoicePlayback();

  void attach_backend(std::unique_ptr<INativeSpeechBackend>);
  // Binds the mixer-facing sinks once: decode turns a produced WAV into a
  // playable stream (nullptr falls back to the subtitle), play starts the
  // dialogue voice, stop silences it.
  void bind(Decode decode, Play play, Stop stop);

  // Speak() port — drops, dedupes, queues, interrupts exactly like the
  // reference VoicePlaybackController.
  void speak(NativeSpeechRequest request);
  // Drives the queue, backend completion and the caption lifetime.
  void update(double delta);
  void stop();
  // ReplayLast port: stops the active line, clears dedupe history and
  // re-presents the most recent request with a fresh dedupe key.
  void replay_last();
  void reset_campaign();

  [[nodiscard]] bool is_speaking() const noexcept;
  [[nodiscard]] bool ducking() const noexcept { return speaking_; }
  [[nodiscard]] bool has_active_subtitle() const noexcept;
  [[nodiscard]] std::string active_subtitle() const;
  [[nodiscard]] std::string active_speaker_name() const;
  [[nodiscard]] std::optional<std::string> active_speaker_portrait() const;
  [[nodiscard]] int pending_count() const noexcept;
  [[nodiscard]] int played_lines() const noexcept { return played_lines_; }
  [[nodiscard]] int subtitle_lines() const noexcept { return subtitle_lines_; }
  [[nodiscard]] std::string diagnostics() const;
  [[nodiscard]] std::string backend_status() const;
  [[nodiscard]] std::string last_source() const;
  [[nodiscard]] const NativeVoiceSettings &settings() const noexcept {
    return settings_;
  }
  void apply_settings(NativeVoiceSettings);

private:
  struct Queued {
    NativeSpeechRequest request;
    double added{};
  };
  void begin_line(NativeSpeechRequest);
  void present_result(NativeVoiceResult);
  void present_fallback(std::string_view reason);
  void finish_line();
  void stop_current();
  [[nodiscard]] static double read_seconds(const NativeSpeechRequest &);

  NativeVoiceSettings settings_;
  const NativeVoiceProfileRegistry *profiles_;
  const NativeCharacterVoiceResolver *speakers_;
  NativeVoiceCache *cache_;
  std::unique_ptr<INativeSpeechBackend> backend_;

  double time_{};
  std::deque<Queued> queue_;
  std::optional<NativeSpeechRequest> active_, last_;
  std::future<NativeVoiceResult> synthesis_;
  bool synthesis_pending_{};
  std::atomic<bool> cancel_line_{};
  double remaining_{};
  std::unordered_map<std::string, double> recent_;
  bool speaking_{}, alive_{true};
  int played_lines_{}, subtitle_lines_{};
  Decode decode_;
  Play play_;
  Stop stop_play_;
  std::string diagnostics_{"Idle"}, last_source_;
  std::string active_speaker_, active_text_;
  std::optional<std::string> active_portrait_;
};

// Builds the platform speech backend: SAPI 5 on Windows, unavailable stub
// elsewhere. Never throws for a missing backend — it reports unavailable.
[[nodiscard]] std::unique_ptr<INativeSpeechBackend>
create_offline_speech_backend();

} // namespace stellar::native_voice
