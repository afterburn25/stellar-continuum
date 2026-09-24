#include <stellar/engine/asset_registry.hpp>
#include "native_audio.hpp"
#include "native_audio_director.hpp"
#include "native_voice_filter.hpp"

#include <stellar/engine/foundation.hpp>
#include <stellar/engine/native_audio.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace stellar::native_audio {
namespace {
using Clip = std::shared_ptr<const stellar::engine::audio::AudioClip>;
constexpr std::size_t maximum_total_decoded_bytes = 104u * 1024u * 1024u;
constexpr std::size_t maximum_voice_clip_bytes = 8u * 1024u * 1024u;
constexpr std::size_t maximum_total_voice_bytes = 16u * 1024u * 1024u;
constexpr auto debounce_interval = std::chrono::milliseconds{60};
// Device-level failures (unplugged/default device errors) retry at this
// cadence instead of permanently disabling audio for the session.
constexpr auto recovery_interval = std::chrono::seconds{5};
constexpr std::array<std::string_view, 3> voice_text{
    "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.",
    "Our research team has a new report. Review the findings before selecting our next objective.",
    "System survey complete. The updated observations are ready for your review."};
constexpr std::array<double, 3> voice_seconds{6.5, 6.025, 5.25};

struct AssetPath { const char* relative; };
constexpr std::array sfx_paths{
    AssetPath{"assets/audio/sfx/ui-hover.wav"}, AssetPath{"assets/audio/sfx/ui-confirm.wav"},
    AssetPath{"assets/audio/sfx/discovery-reveal.wav"}, AssetPath{"assets/audio/sfx/construction-complete.wav"},
    AssetPath{"assets/audio/sfx/ship-launch.wav"}, AssetPath{"assets/audio/sfx/strategic-alert.wav"},
};
constexpr AssetPath music_path{"assets/audio/music/claimed-by-the-void-loop.mp3"};
constexpr std::array voice_paths{
    AssetPath{"assets/audio/voice/scientist-reconnaissance.wav"},
    AssetPath{"assets/audio/voice/scientist-research-report.wav"},
    AssetPath{"assets/audio/voice/scientist-survey-complete.wav"},
};
}

struct NativeAudioDirector::Clips final {
  Clip hover, confirm, discovery, construction, ship, alert;
  // Music pulls decoded PCM on demand — the only long program in the
  // set, so it never occupies a whole-file decode in memory.
  std::shared_ptr<stellar::engine::audio::AudioStreamDecoder> music_stream;
  Clip reconnaissance_required, research_report, survey_complete;
  std::array<Clip,3> filtered;
  std::array<int,3> filter_steps{-1,-1,-1};
};

struct NativeAudioDirector::LoadState final {
  std::mutex mutex;
  std::unique_ptr<Clips> clips;
  std::string failure;
  std::string voice_failure;
};

NativeAudioDirector::NativeAudioDirector(std::filesystem::path asset_root, bool enabled)
    : asset_root_(std::move(asset_root)) {
  stats_.enabled = enabled;
  if (!enabled) return;
  try {
    open_output();
    start_decode_job();
  } catch (const std::exception& error) {
    fail(std::string{"audio output initialization failed: "} + error.what(), true);
    output_.reset();
    jobs_.reset();
    load_state_.reset();
  }
}

void NativeAudioDirector::open_output() {
  output_ = std::make_unique<stellar::engine::audio::AudioOutput>();
  output_->set_volumes(master_volume_, music_volume_, effects_volume_);
  output_->set_voice_gain(voice_preferences_.enabled ? voice_preferences_.volume : 0.f);
}

void NativeAudioDirector::start_decode_job() {
  if (decode_collected_) return;
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
          if (!stellar::engine::resource_exists(path))
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
        // Streamed, not decoded: bounds memory and skips the 16 MiB
        // source cap. The Media Foundation reader binds lazily on the
        // first read, which happens on the output owner thread.
        loaded->music_stream =
            stellar::engine::audio::open_audio_stream(root / music_path.relative);
        std::string voice_failure;
        try {
          std::size_t voice_total{};
          auto decode_voice = [&](const std::filesystem::path& relative) -> Clip {
            const auto path = root / relative;
            if (!stellar::engine::resource_exists(path))
              throw std::runtime_error("voice asset is missing: " + path.string());
            auto clip = stellar::engine::audio::decode_audio_clip(path);
            if (!clip) throw std::runtime_error("voice decoder returned no clip: " + path.string());
            if (clip->byte_size() > maximum_voice_clip_bytes)
              throw std::runtime_error("decoded voice clip exceeds 8 MiB limit: " + path.string());
            if (voice_total > maximum_total_voice_bytes - clip->byte_size())
              throw std::runtime_error("decoded voice set exceeds 16 MiB limit: " + path.string());
            voice_total += clip->byte_size();
            return clip;
          };
          loaded->reconnaissance_required = decode_voice(voice_paths[0].relative);
          loaded->research_report = decode_voice(voice_paths[1].relative);
          loaded->survey_complete = decode_voice(voice_paths[2].relative);
        } catch (const std::exception& error) {
          voice_failure = error.what();
        }
        std::lock_guard lock(state->mutex);
        state->voice_failure = std::move(voice_failure);
        state->clips = std::move(loaded);
      } catch (const std::exception& error) {
        std::lock_guard lock(state->mutex);
        state->failure = error.what();
      } catch (...) {
        std::lock_guard lock(state->mutex);
        state->failure = "unknown audio decoding failure";
      }
    });
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

void NativeAudioDirector::fail(std::string message, bool device_fault) {
  if (stats_.failed) return;
  stats_.failed = true;
  stats_.enabled = false;
  stats_.music_started = false;
  stats_.music_streaming = false;
  stats_.queued_music_bytes = 0;
  stats_.voice_active = false;
  stats_.queued_voice_bytes = 0;
  voice_queue_.clear();
  // Device-level faults (unplugged/default device errors, SDL stream failures)
  // are transient: arm the recovery cadence so service() retries the output.
  // Decode/asset failures stay permanent — reopening a device cannot fix them.
  failure_was_device_ = device_fault;
  device_recoverable_ = device_fault;
  next_recovery_attempt_ = std::chrono::steady_clock::now() + recovery_interval;
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

void NativeAudioDirector::disable_voice(std::string message) {
  stats_.voice_available = false;
  stats_.voice_active = false;
  stats_.queued_voice_bytes = 0;
  voice_queue_.clear();
  if (!voice_diagnostic_emitted_) {
    voice_diagnostic_emitted_ = true;
    std::cerr << "Scientist speech unavailable: " << message << '\n';
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
  const auto voice_failure = load_state_->voice_failure;
  clips_ = std::move(load_state_->clips);
  if (!clips_) { fail("audio decode job published no clips"); return; }
  stats_.assets_loaded = true;
  if (!voice_failure.empty()) disable_voice(voice_failure);
  else stats_.voice_available = true;
}

// Attempts to reopen the output device after a device-level failure. On
// success the normal pipeline resumes: collect_loaded_assets() picks up any
// pending decode job and menu_ready() restarts music (music_started was
// cleared by fail()). Bounded by recovery_interval; never throws.
void NativeAudioDirector::try_recover() {
  if (!device_recoverable_ || stats_.stopped) return;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_recovery_attempt_) return;
  next_recovery_attempt_ = now + recovery_interval;
  try {
    open_output();
    start_decode_job();
  } catch (const std::exception& error) {
    failure_message_ = std::string{"audio device recovery pending: "} + error.what();
    output_.reset();
    jobs_.reset();
    load_state_.reset();
    return;
  }
  device_recoverable_ = false;
  ++stats_.device_recoveries;
  if (failure_was_device_) {
    stats_.failed = false;
    stats_.enabled = true;
    failure_message_.clear();
  }
}

void NativeAudioDirector::service() {
  require_owner();
  if (stats_.stopped) return;
  if (!stats_.enabled) { try_recover(); return; }
  collect_loaded_assets();
  if (!stats_.enabled || !output_) return;
  if (menu_ready_ && !stats_.music_started) menu_ready();
  if (!stats_.enabled || !output_) return;
  try {
    output_->service();
    const auto diagnostics = output_->diagnostics();
    stats_.music_started = diagnostics.music_started;
    stats_.music_streaming = diagnostics.music_streaming;
    stats_.queued_music_bytes = diagnostics.queued_music_bytes;
    stats_.voice_active = diagnostics.voice_active;
    stats_.queued_voice_bytes = diagnostics.queued_voice_bytes;
    stats_.music_queue_limit_bytes = diagnostics.music_queue_limit_bytes;
    stats_.voice_queue_limit_bytes = diagnostics.voice_queue_limit_bytes;
    service_voice();
  } catch (const stellar::engine::audio::AudioStreamError& error) {
    fail(std::string{"audio stream failure: "} + error.what());
  } catch (const std::exception& error) {
    fail(std::string{"audio device failure: "} + error.what(), true);
  }
}

bool NativeAudioDirector::assets_ready() const {
  require_owner();
  return stats_.stopped || !stats_.enabled || decode_collected_ || stats_.failed;
}

bool NativeAudioDirector::may_play_effect() const {
  return stats_.enabled && !stats_.stopped && stats_.assets_loaded && output_ && clips_;
}

bool NativeAudioDirector::may_play_voice() const {
  return may_play_effect() && menu_ready_ && stats_.voice_available;
}

std::size_t NativeAudioDirector::voice_index(VoiceCue cue) {
  switch (cue) {
    case VoiceCue::ReconnaissanceRequired: return 0;
    case VoiceCue::ResearchReport: return 1;
    case VoiceCue::SurveyComplete: return 2;
  }
  throw std::invalid_argument("Unknown voice cue.");
}

void NativeAudioDirector::service_voice() {
  if (!may_play_voice() || !voice_preferences_.enabled || stats_.voice_active || voice_queue_.empty()) return;
  const auto cue = voice_queue_.front();
  voice_queue_.pop_front();
  Clip clip;
  switch (cue) {
    case VoiceCue::ReconnaissanceRequired: clip = clips_->reconnaissance_required; break;
    case VoiceCue::ResearchReport: clip = clips_->research_report; break;
    case VoiceCue::SurveyComplete: clip = clips_->survey_complete; break;
  }
  try {
    const auto index = voice_index(cue);
    const int step = static_cast<int>(std::lround(voice_preferences_.communication_filter*20.f));
    if (step>0) {
      if (clips_->filter_steps[index]!=step) {
        clips_->filtered[index].reset();
        clips_->filtered[index]=communication_clip(clip,step/20.f);
        clips_->filter_steps[index]=step;
      }
      clip=clips_->filtered[index];
    }
    output_->play_voice(std::move(clip));
    show_caption(cue);
    const auto diagnostics = output_->diagnostics();
    stats_.voice_active = diagnostics.voice_active;
    stats_.voice_play_count = diagnostics.voice_play_count;
    stats_.queued_voice_bytes = diagnostics.queued_voice_bytes;
    stats_.music_queue_limit_bytes = diagnostics.music_queue_limit_bytes;
    stats_.voice_queue_limit_bytes = diagnostics.voice_queue_limit_bytes;
  } catch (const std::exception& error) {
    fail(std::string{"audio voice playback failed: "} + error.what(), true);
  }
}

void NativeAudioDirector::menu_ready() {
  require_owner();
  menu_ready_ = true;
  if (stats_.stopped || !stats_.enabled) return;
  menu_ready_ = true;
  collect_loaded_assets();
  // Guard on music_started, not the start count: after a device recovery the
  // count is already nonzero but the queue is empty and must be refilled.
  if (!may_play_effect() || stats_.music_started) return;
  if (!clips_->music_stream) { fail("audio music stream was not loaded"); return; }
  try {
    output_->play_music(clips_->music_stream);
    ++stats_.music_start_count;
    const auto diagnostics = output_->diagnostics();
    stats_.music_started = diagnostics.music_started;
    stats_.music_streaming = diagnostics.music_streaming;
    stats_.queued_music_bytes = diagnostics.queued_music_bytes;
  } catch (const stellar::engine::audio::AudioStreamError& error) {
    // Corrupt/unreadable media is a permanent asset failure, not a
    // device fault — recovery retries cannot fix it.
    fail(std::string{"audio music stream failed: "} + error.what());
  } catch (const std::exception& error) {
    fail(std::string{"audio music playback failed: "} + error.what(), true);
  }
}

void NativeAudioDirector::confirm() {
  require_owner();
  const auto now = std::chrono::steady_clock::now();
  if (now - last_confirm_ < debounce_interval) return;
  last_confirm_ = now;
  if (!may_play_effect()) return;
  try { output_->play_effect(clips_->confirm); ++stats_.confirm_count; }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what(), true); }
}

void NativeAudioDirector::hover() {
  require_owner();
  const auto now = std::chrono::steady_clock::now();
  if (now - last_hover_ < debounce_interval) return;
  last_hover_ = now;
  if (!may_play_effect()) return;
  try { output_->play_effect(clips_->hover); ++stats_.hover_count; }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what(), true); }
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
  try { output_->play_effect(std::move(clip)); ++stats_.event_count; }
  catch (const std::exception& error) { fail(std::string{"audio effect playback failed: "} + error.what(), true); }
}

void NativeAudioDirector::speak(VoiceCue cue) { require_owner(); admit_voice(cue,false); }
void NativeAudioDirector::show_caption(VoiceCue cue) {
  const auto index=voice_index(cue);
  caption_=VoiceCaption{"CHIEF SCIENTIST",std::string(voice_text[index]),
    std::chrono::steady_clock::now()+std::chrono::milliseconds{static_cast<int>((voice_seconds[index]+1.)*1000.)}};
}
void NativeAudioDirector::admit_voice(VoiceCue cue,bool replay) {
  const auto index=voice_index(cue);
  if (!menu_ready_ || stats_.stopped) return;
  if (!replay && voice_preferences_.frequency==VoiceFrequency::Minimal && cue!=VoiceCue::ReconnaissanceRequired) return;
  const auto now=std::chrono::steady_clock::now();
  const auto cooldown=std::chrono::seconds{voice_preferences_.frequency==VoiceFrequency::Frequent?3:voice_preferences_.frequency==VoiceFrequency::Minimal?20:8};
  if (!replay && now-last_voice_[index]<cooldown) {last_voice_[index]=now;return;}
  last_voice_[index]=now;last_voice_cue_=cue;++stats_.voice_event_count;
  if (!voice_preferences_.enabled || !may_play_voice()) {show_caption(cue);return;}
  if (replay || !voice_preferences_.no_interruptions) stop_voice();
  if (voice_queue_.size()>=3 || std::find(voice_queue_.begin(),voice_queue_.end(),cue)!=voice_queue_.end()) return;
  voice_queue_.push_back(cue);
}

void NativeAudioDirector::play_dialogue_pcm(
    std::shared_ptr<const stellar::native_audio::PcmData> pcm) {
  require_owner();
  if (!output_ || !pcm ||
      pcm->sample_rate != stellar::engine::audio::audio_sample_rate ||
      pcm->channels != stellar::engine::audio::audio_channels ||
      pcm->frames.empty())
    return;
  try {
    output_->play_voice(stellar::engine::audio::AudioClip::create(
        std::vector<float>(pcm->frames)));
    const auto diagnostics = output_->diagnostics();
    stats_.voice_active = diagnostics.voice_active;
    stats_.voice_play_count = diagnostics.voice_play_count;
    stats_.queued_voice_bytes = diagnostics.queued_voice_bytes;
    stats_.music_queue_limit_bytes = diagnostics.music_queue_limit_bytes;
    stats_.voice_queue_limit_bytes = diagnostics.voice_queue_limit_bytes;
  } catch (const std::exception& error) {
    fail(std::string{"audio voice playback failed: "} + error.what(), true);
  }
}

void NativeAudioDirector::stop_voice() {
  require_owner();
  caption_.reset();
  voice_queue_.clear();
  stats_.voice_active = false;
  stats_.queued_voice_bytes = 0;
  if (!output_) return;
  try { output_->stop_voice(); }
  catch (const std::exception& error) { fail(std::string{"audio voice shutdown failed: "} + error.what(), true); }
}

void NativeAudioDirector::set_volumes(float master, float music, float effects) {
  require_owner();
  if (!std::isfinite(master) || !std::isfinite(music) || !std::isfinite(effects) ||
      master < 0.f || master > 1.f || music < 0.f || music > 1.f || effects < 0.f || effects > 1.f)
    throw std::invalid_argument("Audio volumes must be finite values from zero through one.");
  master_volume_ = master; music_volume_ = music; effects_volume_ = effects;
  if (!stats_.enabled || stats_.stopped || !output_) return;
  try { output_->set_volumes(master, music, effects); }
  catch (const std::exception& error) { fail(std::string{"audio volume update failed: "} + error.what(), true); }
}
void NativeAudioDirector::set_voice_preferences(const VoicePreferences& value) {
  require_owner();
  const auto gain=[](float v){return std::isfinite(v)&&v>=0.f&&v<=1.f;};
  if (!gain(value.volume)||!gain(value.communication_filter)||!gain(value.subtitle_background_opacity)||
      value.subtitle_size<14||value.subtitle_size>32||static_cast<int>(value.frequency)<0||static_cast<int>(value.frequency)>2)
    throw std::invalid_argument("Invalid voice preferences.");
  if (!value.enabled && voice_preferences_.enabled) stop_voice();
  voice_preferences_=value;
  if(output_)try{output_->set_voice_gain(value.enabled?value.volume:0.f);}
  catch(const std::exception& error){fail(std::string{"voice gain update failed: "}+error.what(), true);}
}
VoicePreferences NativeAudioDirector::voice_preferences() const { require_owner(); return voice_preferences_; }
std::optional<VoiceCaption> NativeAudioDirector::caption() const { require_owner(); if(!caption_ || !voice_preferences_.subtitles || std::chrono::steady_clock::now()>=caption_->expires_at) return {}; return caption_; }
bool NativeAudioDirector::replay_last_voice() {require_owner();if(!last_voice_cue_||stats_.stopped)return false;admit_voice(*last_voice_cue_,true);return true;}

void NativeAudioDirector::stop() {
  require_owner();
  if (stats_.stopped) return;
  stats_.stopped = true;
  caption_.reset();
  if (output_) {
    try { output_->stop_all(); }
    catch (const std::exception& error) { fail(std::string{"audio shutdown failed: "} + error.what(), true); }
    output_.reset();
  }
  voice_queue_.clear();
  stats_.music_started = false;
  stats_.music_streaming = false;
  stats_.queued_music_bytes = 0;
  stats_.voice_active = false;
  stats_.queued_voice_bytes = 0;
}

std::string NativeAudioDirector::failure_message() const { require_owner(); return failure_message_; }
NativeAudioStats NativeAudioDirector::stats() const { require_owner(); return stats_; }
void NativeAudioDirector::force_device_fault_for_test() { require_owner(); fail("injected device fault", true); }

} // namespace stellar::native_audio

