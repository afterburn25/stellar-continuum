#pragma once

#include <cstddef>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include "native_voice_settings.hpp"

namespace stellar::engine {
class JobSystem;
namespace audio { class AudioOutput; class AudioStreamDecoder; }
}
namespace stellar::native_audio { struct PcmData; }

namespace stellar::native_audio {

enum class Cue { Discovery, Construction, Ship, Alert };
enum class VoiceCue { ReconnaissanceRequired, ResearchReport, SurveyComplete };

struct NativeAudioStats final {
  bool assets_loaded{};
  std::uint64_t music_start_count{};
  std::uint64_t confirm_count{};
  std::uint64_t hover_count{};
  std::uint64_t event_count{};
  bool music_started{};
  // Music is fed from the incremental pull decoder rather than a
  // whole-file decoded clip.
  bool music_streaming{};
  std::size_t queued_music_bytes{};
  bool failed{};
  bool enabled{};
  bool stopped{};
  bool voice_available{};
  bool voice_active{};
  std::uint64_t voice_play_count{};
  std::uint64_t voice_event_count{};
  std::size_t queued_voice_bytes{};
  // Successful output-device rebuilds after a device-level failure.
  std::uint64_t device_recoveries{};
};
struct VoiceCaption final { std::string speaker, text; std::chrono::steady_clock::time_point expires_at{}; };

// UI-owner-thread coordinator for asynchronous clip decoding and SDL output.
class NativeAudioDirector final {
 public:
  explicit NativeAudioDirector(std::filesystem::path asset_root, bool enabled = true);
  ~NativeAudioDirector() noexcept;
  NativeAudioDirector(const NativeAudioDirector&) = delete;
  NativeAudioDirector& operator=(const NativeAudioDirector&) = delete;
  NativeAudioDirector(NativeAudioDirector&&) = delete;
  NativeAudioDirector& operator=(NativeAudioDirector&&) = delete;

  void service();
  [[nodiscard]] bool assets_ready() const;
  void menu_ready();
  void confirm();
  void hover();
  void play_event(Cue cue);
  void speak(VoiceCue cue);
  void stop_voice();
  // Voice-pipeline sink: plays a decoded 48 kHz stereo PCM line on the voice
  // channel, replacing the currently playing line. Null/mismatched streams
  // are dropped.
  void play_dialogue_pcm(std::shared_ptr<const PcmData> pcm);
  void set_volumes(float master, float music, float effects);
  void set_voice_preferences(const VoicePreferences&);
  [[nodiscard]] VoicePreferences voice_preferences() const;
  [[nodiscard]] std::optional<VoiceCaption> caption() const;
  bool replay_last_voice();
  void stop();
  [[nodiscard]] std::string failure_message() const;
  [[nodiscard]] NativeAudioStats stats() const;
  // Test seam: routes a synthetic fault through the real device-failure path
  // so recovery can be verified without unplugging hardware.
  void force_device_fault_for_test();

 private:
  struct LoadState;
  struct Clips;
  void require_owner() const;
  void collect_loaded_assets();
  void open_output();
  void start_decode_job();
  void try_recover();
  void fail(std::string message, bool device_fault = false);
  [[nodiscard]] bool may_play_effect() const;
  [[nodiscard]] bool may_play_voice() const;
  [[nodiscard]] static std::size_t voice_index(VoiceCue cue);
  void disable_voice(std::string message);
  void service_voice();
  void show_caption(VoiceCue cue);
  void admit_voice(VoiceCue cue, bool replay);

  std::thread::id owner_{std::this_thread::get_id()};
  std::filesystem::path asset_root_;
  std::unique_ptr<stellar::engine::JobSystem> jobs_;
  std::unique_ptr<stellar::engine::audio::AudioOutput> output_;
  std::shared_ptr<LoadState> load_state_;
  std::unique_ptr<Clips> clips_;
  std::future<void> decode_job_;
  std::chrono::steady_clock::time_point last_hover_{};
  std::chrono::steady_clock::time_point last_confirm_{};
  std::array<std::chrono::steady_clock::time_point, 3> last_voice_{};
  std::deque<VoiceCue> voice_queue_;
  NativeAudioStats stats_{};
  float master_volume_{.78f}, music_volume_{.64f}, effects_volume_{.82f};
  bool device_recoverable_{};
  bool failure_was_device_{};
  std::chrono::steady_clock::time_point next_recovery_attempt_{};
  bool menu_ready_{};
  bool decode_collected_{};
  bool diagnostic_emitted_{};
  bool voice_diagnostic_emitted_{};
  std::string failure_message_;
  VoicePreferences voice_preferences_{};
  std::optional<VoiceCaption> caption_;
  std::optional<VoiceCue> last_voice_cue_;
};

} // namespace stellar::native_audio
