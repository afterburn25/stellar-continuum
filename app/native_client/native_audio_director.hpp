#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace stellar::engine {
class JobSystem;
namespace audio { class AudioOutput; }
}

namespace stellar::native_audio {

enum class Cue { Discovery, Construction, Ship, Alert };

struct NativeAudioStats final {
  bool assets_loaded{};
  std::uint64_t music_start_count{};
  std::uint64_t confirm_count{};
  bool music_started{};
  std::size_t queued_music_bytes{};
  bool failed{};
  bool enabled{};
  bool stopped{};
};

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
  void set_volumes(float master, float music, float effects);
  void stop();
  [[nodiscard]] std::string failure_message() const;
  [[nodiscard]] NativeAudioStats stats() const;

 private:
  struct LoadState;
  struct Clips;
  void require_owner() const;
  void collect_loaded_assets();
  void fail(std::string message);
  [[nodiscard]] bool may_play_effect() const;

  std::thread::id owner_{std::this_thread::get_id()};
  std::filesystem::path asset_root_;
  std::unique_ptr<stellar::engine::JobSystem> jobs_;
  std::unique_ptr<stellar::engine::audio::AudioOutput> output_;
  std::shared_ptr<LoadState> load_state_;
  std::unique_ptr<Clips> clips_;
  std::future<void> decode_job_;
  std::chrono::steady_clock::time_point last_hover_{};
  std::chrono::steady_clock::time_point last_confirm_{};
  NativeAudioStats stats_{};
  bool menu_ready_{};
  bool decode_collected_{};
  bool diagnostic_emitted_{};
  std::string failure_message_;
};

} // namespace stellar::native_audio
