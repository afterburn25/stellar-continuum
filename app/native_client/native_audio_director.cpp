#include "native_audio_director.hpp"

#include <stellar/engine/foundation.hpp>
#include <stellar/engine/native_audio.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace stellar::native_audio {
namespace {
using Clip = std::shared_ptr<const stellar::engine::audio::AudioClip>;
constexpr std::size_t maximum_total_decoded_bytes = 104u * 1024u * 1024u;
constexpr auto debounce_interval = std::chrono::milliseconds{60};

struct AssetPath { const char* relative; };
constexpr std::array sfx_paths{
    AssetPath{"assets/audio/sfx/ui-hover.wav"}, AssetPath{"assets/audio/sfx/ui-confirm.wav"},
    AssetPath{"assets/audio/sfx/discovery-reveal.wav"}, AssetPath{"assets/audio/sfx/construction-complete.wav"},
    AssetPath{"assets/audio/sfx/ship-launch.wav"}, AssetPath{"assets/audio/sfx/strategic-alert.wav"},
};
constexpr AssetPath music_path{"assets/audio/music/claimed-by-the-void-loop.mp3"};
}

struct NativeAudioDirector::Clips final {
  Clip hover, confirm, discovery, construction, ship, alert, music;
};

struct NativeAudioDirector::LoadState final {
  std::mutex mutex;
  std::unique_ptr<Clips> clips;
  std::string failure;
};

NativeAudioDirector::NativeAudioDirector(std::filesystem::path asset_root, bool enabled)
    : asset_root_(std::move(asset_root)) {
  stats_.enabled = enabled;
  if (!enabled) return;
  try {
    output_ = std::make_unique<stellar::engine::audio::AudioOutput>();
    output_->set_volumes(.78f, .64f, .82f);
    jobs_ = std::make_unique<stellar::engine::JobSystem>(1);
    load_state_ = std::make_shared<LoadState>();
    const auto state = load_state_;
    const auto root = asset_root_;
    decode_job_ = jobs_->submit([state, root] {
      try {
        auto loaded = std::make_unique<Clips>();
        std::size_t total{};
        auto decode = [&](const std::filesystem::path& relative) -> Clip {
          const auto path = root / relative;
          if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("audio asset is missing: " + path.string());
          auto clip = stellar::engine::audio::decode_audio_clip(path);
          if (!clip) throw std::runtime_error("audio decoder returned no clip: " + path.string());
          if (clip->byte_size() > stellar::engine::audio::maximum_decoded_audio_bytes)
            throw std::runtime_error("decoded audio clip exceeds limit: " + path.string());
          if (total > maximum_total_decoded_bytes - clip->byte_size())
            throw std::runtime_error("decoded audio set exceeds 104 MiB limit: " + path.string());
          total += clip->byte_size();
          return clip;
        };
        loaded->hover = decode(sfx_paths[0].relative);
        loaded->confirm = decode(sfx_paths[1].relative);
        loaded->discovery = decode(sfx_paths[2].relative);
        loaded->construction = decode(sfx_paths[3].relative);
        loaded->ship = decode(sfx_paths[4].relative);
        loaded->alert = decode(sfx_paths[5].relative);
        loaded->music = decode(music_path.relative);
        std::lock_guard lock(state->mutex);
        state->clips = std::move(loaded);
      } catch (const std::exception& error) {
        std::lock_guard lock(state->mutex);
        state->failure = error.what();
      } catch (...) {
        std::lock_guard lock(state->mutex);
        state->failure = "unknown audio decoding failure";
      }
    });
  } catch (const std::exception& error) {
    fail(std::string{"audio output initialization failed: "} + error.what());
    output_.reset();
    jobs_.reset();
    load_state_.reset();
  }
}

NativeAudioDirector::~NativeAudioDirector() noexcept {
  if (output_ && std::this_thread::get_id() == owner_) {
    try { output_->stop_all(); } catch (...) {}
  }
  if (decode_job_.valid()) {
    try { decode_job_.get(); } catch (...) {}
  }
  output_.reset();
  jobs_.reset();
}

void NativeAudioDirector::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native audio director must be used on its owner thread.");
}

void NativeAudioDirector::fail(std::string message) {
  if (stats_.failed) return;
  stats_.failed = true;
  stats_.enabled = false;
  stats_.music_started = false;
  stats_.queued_music_bytes = 0;
  failure_message_ = std::move(message);
  if (output_) {
    try { output_->stop_all(); } catch (...) {}
    output_.reset();
  }
  if (!diagnostic_emitted_) {
    diagnostic_emitted_ = true;
    std::cerr << "Audio disabled: " << failure_message_ << '\n';
  }
}

void NativeAudioDirector::collect_loaded_assets() {
  if (decode_collected_ || !decode_job_.valid() ||
      decode_job_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return;
  decode_collected_ = true;
  try { decode_job_.get(); }
  catch (const std::exception& error) { fail(std::string{"audio decode job failed: "} + error.what()); return; }
  catch (...) { fail("audio decode job failed unexpectedly"); return; }
  std::lock_guard lock(load_state_->mutex);
  if (!load_state_->failure.empty()) { fail(load_state_->failure); return; }
  clips_ = std::move(load_state_->clips);
  if (!clips_) { fail("audio decode job published no clips"); return; }
  stats_.assets_loaded = true;
}

void NativeAudioDirector::service() {
  require_owner();
  if (stats_.stopped || !stats_.enabled) return;
  collect_loaded_assets();
  if (!stats_.enabled || !output_) return;
  if (menu_ready_ && stats_.music_start_count == 0) menu_ready();
  if (!stats_.enabled || !output_) return;
  try {
    output_->service();
    const auto diagnostics = output_->diagnostics();
    stats_.music_started = diagnostics.music_started;
    stats_.queued_music_bytes = diagnostics.queued_music_bytes;
  } catch (const std::exception& error) {
    fail(std::string{"audio device failure: "} + error.what());
  }
}

bool NativeAudioDirector::assets_ready() const {
  require_owner();
  return stats_.stopped || !stats_.enabled || decode_collected_ || stats_.failed;
}

bool NativeAudioDirector::may_play_effect() const {
  return stats_.enabled && !stats_.stopped && stats_.assets_loaded && output_ && clips_;
}

void NativeAudioDirector::menu_ready() {
  require_owner();
  if (stats_.stopped || !stats_.enabled) return;
  menu_ready_ = true;
  collect_loaded_assets();
  if (!may_play_effect() || stats_.music_start_count != 0) return;
  try {
    output_->play_music(clips_->music);
    ++stats_.music_start_count;
    const auto diagnostics = output_->diagnostics();
    stats_.music_started = diagnostics.music_started;
    stats_.queued_music_bytes = diagnostics.queued_music_bytes;
  } catch (const std::exception& error) {
    fail(std::string{"audio music playback failed: "} + error.what());
  }
}

void NativeAudioDirector::confirm() {
  require_owner();
  const auto now = std::chrono::steady_clock::now();
  if (now - last_confirm_ < debounce_interval) return;
  last_confirm_ = now;
  if (!may_play_effect()) return;
  try { output_->play_effect(clips_->confirm); ++stats_.confirm_count; }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what()); }
}

void NativeAudioDirector::hover() {
  require_owner();
  const auto now = std::chrono::steady_clock::now();
  if (now - last_hover_ < debounce_interval) return;
  last_hover_ = now;
  if (!may_play_effect()) return;
  try { output_->play_effect(clips_->hover); }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what()); }
}

void NativeAudioDirector::play_event(Cue cue) {
  require_owner();
  if (!may_play_effect()) return;
  Clip clip;
  switch (cue) {
    case Cue::Discovery: clip = clips_->discovery; break;
    case Cue::Construction: clip = clips_->construction; break;
    case Cue::Ship: clip = clips_->ship; break;
    case Cue::Alert: clip = clips_->alert; break;
  }
  try { output_->play_effect(std::move(clip)); }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what()); }
}

void NativeAudioDirector::set_volumes(float master, float music, float effects) {
  require_owner();
  if (!std::isfinite(master) || !std::isfinite(music) || !std::isfinite(effects) ||
      master < 0.f || master > 1.f || music < 0.f || music > 1.f || effects < 0.f || effects > 1.f)
    throw std::invalid_argument("Audio volumes must be finite values from zero through one.");
  if (!stats_.enabled || stats_.stopped || !output_) return;
  try { output_->set_volumes(master, music, effects); }
  catch (const std::exception& error) { fail(std::string{"audio volume update failed: "} + error.what()); }
}

void NativeAudioDirector::stop() {
  require_owner();
  if (stats_.stopped) return;
  stats_.stopped = true;
  if (output_) {
    try { output_->stop_all(); }
    catch (const std::exception& error) { fail(std::string{"audio shutdown failed: "} + error.what()); }
    output_.reset();
  }
  stats_.music_started = false;
  stats_.queued_music_bytes = 0;
}

std::string NativeAudioDirector::failure_message() const { require_owner(); return failure_message_; }
NativeAudioStats NativeAudioDirector::stats() const { require_owner(); return stats_; }

} // namespace stellar::native_audio
