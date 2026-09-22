#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_audio {

// Presentation-only music/UI/event audio, matching the reference AudioDirector:
// one looping music bed, up to eight polyphonic effect voices, persistent
// volume settings, and a voice-duck ramp. This mixer is pure CPU code — the
// SDL device glue lives in native_audio_device.* so headless tests can decode,
// mix and measure frames without an audio device.
inline constexpr int output_sample_rate = 48000;
inline constexpr int output_channels = 2;
inline constexpr std::size_t maximum_sfx_voices = 8;

struct PcmData {
  int sample_rate{}, channels{};
  std::vector<float> frames; // interleaved
};

struct NativeMixerSettings {
  float master{.78f}, music{.64f}, sfx{.82f};
};

enum class NativeSfx {
  ui_hover,
  ui_confirm,
  discovery_reveal,
  construction_complete,
  ship_launch,
  strategic_alert
};

[[nodiscard]] std::optional<PcmData> decode_wav(std::string_view bytes);
[[nodiscard]] std::optional<PcmData> decode_mp3(std::string_view bytes);
[[nodiscard]] std::optional<PcmData>
decode_audio_file(const std::filesystem::path &path);

class NativeAudioMixer final {
public:
  explicit NativeAudioMixer(std::filesystem::path settings_path = {});
  // Loads the required streams from <asset_root>/assets/audio/... Returns false
  // (without throwing) when any required stream is missing or undecodable; the
  // mixer then stays silent instead of failing the campaign.
  bool load_assets(const std::filesystem::path &asset_root);
  [[nodiscard]] bool has_required_audio() const noexcept;
  [[nodiscard]] bool music_playing() const noexcept;
  [[nodiscard]] std::size_t active_voices() const noexcept;
  [[nodiscard]] std::size_t music_frame_count() const noexcept;

  void complete_startup_loading();
  void set_menu_context(bool menu) noexcept;
  [[nodiscard]] bool is_menu_context() const noexcept { return menu_context_; }
  void set_voice_ducking(bool active) noexcept;
  // Live mix only; used while a settings slider drags.
  void apply_volumes(float master, float music, float sfx) noexcept;
  void set_volumes(float master, float music, float sfx);
  [[nodiscard]] NativeMixerSettings settings() const noexcept;

  void play(NativeSfx sound);
  void play_hover();
  void play_event(std::string_view category);
  // The single non-spatial dialogue voice (the reference's voice player).
  // A new call replaces the currently playing line.
  void play_dialogue(std::shared_ptr<const PcmData>);
  void stop_dialogue();
  [[nodiscard]] bool dialogue_playing() const noexcept;
  // Voice-layer gain multiplier (from VoiceSettings.volume); defaults to 1.
  void set_dialogue_volume(float) noexcept;
  // Fills interleaved 48 kHz stereo output; safe on the audio callback thread.
  void mix(float *output, std::size_t frame_count);

private:
  struct Voice {
    std::shared_ptr<const PcmData> source;
    std::uint64_t cursor{};
  };
  [[nodiscard]] static std::optional<NativeSfx> event_sound(std::string_view);
  [[nodiscard]] std::filesystem::path asset(std::string_view relative) const;
  void persist_settings() const noexcept;

  std::filesystem::path asset_root_, settings_path_;
  mutable std::mutex voices_mutex_;
  std::shared_ptr<const PcmData> music_;
  std::map<NativeSfx, std::shared_ptr<const PcmData>> sfx_streams_;
  std::vector<Voice> voices_;
  std::optional<Voice> dialogue_;
  std::uint64_t music_cursor_{};
  bool startup_ready_{}, menu_context_{true}, music_playing_{};
  float voice_duck_{1.f}, voice_duck_target_{1.f}, dialogue_volume_{1.f};
  NativeMixerSettings settings_;
  std::uint64_t last_hover_ms_{};
  bool has_last_hover_{};
};

} // namespace stellar::native_audio
