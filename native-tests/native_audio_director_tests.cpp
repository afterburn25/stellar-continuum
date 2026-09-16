#include "native_audio_director.hpp"
#include "native_voice_filter.hpp"
#include <cmath>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using stellar::native_audio::NativeAudioDirector;

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void wait_until_ready(NativeAudioDirector& director) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!director.assets_ready() && std::chrono::steady_clock::now() < deadline) {
    director.service();
    std::this_thread::sleep_for(2ms);
  }
  director.service();
  require(director.assets_ready(), "audio assets did not complete within the bounded wait");
}

template <class Predicate>
void wait_for_voice(Predicate&& predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 25s;
  while (!predicate() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(2ms);
  require(predicate(), message);
}

class AudioFixture final {
 public:
  explicit AudioFixture(const fs::path& root) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() / ("stellar-voice-fixture-" + std::to_string(nonce));
    fs::create_directories(path_ / "assets");
    fs::copy(root / "assets" / "audio", path_ / "assets" / "audio", fs::copy_options::recursive);
  }
  ~AudioFixture() { std::error_code ignored; fs::remove_all(path_, ignored); }
  [[nodiscard]] const fs::path& path() const noexcept { return path_; }
 private:
  fs::path path_;
};

void normal_lifecycle(const fs::path& root) {
  NativeAudioDirector director(root);
  require(director.stats().music_start_count == 0, "music started before menu admission");
  wait_until_ready(director);
  require(!director.stats().failed && director.stats().assets_loaded, "real audio assets failed to load");
  require(!director.stats().music_started && director.stats().music_start_count == 0,
          "music started before decoded assets were admitted to the menu");
  director.speak(stellar::native_audio::VoiceCue::ResearchReport);
  director.service();
  require(director.stats().voice_play_count == 0,
          "voice playback started before menu admission");
  director.menu_ready();
  director.service();
  require(director.stats().music_start_count == 1, "menu admission did not start music exactly once");
  director.speak(stellar::native_audio::VoiceCue::ResearchReport);
  require(director.stats().voice_event_count == 1,
          "a pre-menu speech rejection incorrectly cooled down the first player cue");
  director.menu_ready();
  director.service();
  require(director.stats().music_start_count == 1, "repeated menu admission restarted music");
  director.confirm();
  require(director.stats().confirm_count == 1, "confirm did not play an effect");
  for (int index = 0; index < 20; ++index) director.play_event(stellar::native_audio::Cue::Alert);
  require(director.stats().event_count == 20, "successful strategic effects were not counted");
  director.service();
  require(director.stats().queued_music_bytes > 0 && director.stats().queued_music_bytes <= 288000u,
          "music queue was not filled within its bounded 0.75-second cap");
  director.stop();
  const auto stopped = director.stats();
  director.menu_ready(); director.confirm(); director.service();
  require(director.stats().music_start_count == stopped.music_start_count && director.stats().stopped,
          "stopped director restarted audio");
}

void voice_queue_and_stop(const fs::path& root) {
  NativeAudioDirector director(root);
  wait_until_ready(director);
  require(director.stats().voice_available, "real scientist speech assets did not load");
  director.menu_ready();
  director.speak(stellar::native_audio::VoiceCue::ReconnaissanceRequired);
  director.speak(stellar::native_audio::VoiceCue::ResearchReport);
  director.speak(stellar::native_audio::VoiceCue::SurveyComplete);
  director.speak(stellar::native_audio::VoiceCue::ReconnaissanceRequired);
  require(director.stats().voice_event_count == 3, "voice cooldown did not coalesce a repeated cue");
  wait_for_voice([&] { director.service(); return director.stats().voice_play_count >= 1; },
                 "first queued voice cue did not start");
  wait_for_voice([&] { director.service(); return director.stats().voice_play_count >= 2; },
                 "second queued voice cue did not follow the first");
  wait_for_voice([&] { director.service(); return director.stats().voice_play_count >= 3; },
                 "third unique queued voice cue did not play");

  NativeAudioDirector stopping(root);
  wait_until_ready(stopping);
  stopping.menu_ready();
  stopping.speak(stellar::native_audio::VoiceCue::ReconnaissanceRequired);
  stopping.speak(stellar::native_audio::VoiceCue::ResearchReport);
  stopping.speak(stellar::native_audio::VoiceCue::SurveyComplete);
  wait_for_voice([&] { stopping.service(); return stopping.stats().voice_play_count == 1; },
                 "voice stop fixture did not begin playback");
  stopping.stop_voice();
  for (int index = 0; index < 20; ++index) { stopping.service(); std::this_thread::sleep_for(2ms); }
  require(!stopping.stats().voice_active && stopping.stats().queued_voice_bytes == 0 &&
          stopping.stats().voice_play_count == 1,
          "stop_voice did not clear queued speech before it replayed");
}

void missing_voice_preserves_required_audio(const fs::path& root) {
  AudioFixture fixture(root);
  fs::remove(fixture.path() / "assets" / "audio" / "voice" / "scientist-research-report.wav");
  std::ostringstream diagnostics;
  struct RestoreCerr { std::streambuf* previous; ~RestoreCerr(){ std::cerr.rdbuf(previous); } };
  const RestoreCerr restore{std::cerr.rdbuf(diagnostics.rdbuf())};
  {
    NativeAudioDirector director(fixture.path());
    wait_until_ready(director);
    director.menu_ready(); director.service();
    director.play_event(stellar::native_audio::Cue::Alert); director.service();
    require(!director.stats().failed && director.stats().assets_loaded && !director.stats().voice_available &&
            director.stats().music_start_count == 1 && director.stats().event_count == 1,
            "missing speech asset disabled required music or effects");
    director.speak(stellar::native_audio::VoiceCue::ResearchReport); director.service();
    require(director.stats().voice_play_count == 0, "missing speech asset began voice playback");
  }
  const auto text = diagnostics.str();
  const auto marker = std::string{"Scientist speech unavailable:"};
  require(text.find(marker) != std::string::npos && text.find(marker, text.find(marker) + 1) == std::string::npos,
          "missing speech asset did not emit exactly one actionable diagnostic");
}

void disabled_and_failure_paths(const fs::path& root) {
  NativeAudioDirector disabled(root / "missing", false);
  require(disabled.assets_ready() && !disabled.stats().failed && !disabled.stats().assets_loaded,
          "disabled audio attempted to load missing assets");
  disabled.speak(stellar::native_audio::VoiceCue::SurveyComplete);
  disabled.stop_voice();
  require(!disabled.stats().voice_available && !disabled.stats().voice_active &&
          disabled.stats().voice_play_count == 0,
          "disabled audio admitted scientist speech");
  NativeAudioDirector missing(root / "missing");
  wait_until_ready(missing);
  require(missing.stats().failed && missing.failure_message().find((root / "missing").string()) != std::string::npos,
          "missing asset failure did not preserve the requested asset root");
}

void stop_before_decode_completion(const fs::path& root) {
  NativeAudioDirector director(root);
  director.stop();
  require(director.assets_ready() && director.stats().stopped,
          "stopping before decoding completed left startup readiness pending");
  director.menu_ready(); director.service();
  require(director.stats().music_start_count == 0, "pre-decode stop allowed music to restart");
}

void invalid_gain_is_rejected(const fs::path& root) {
  NativeAudioDirector director(root, false);
  bool rejected{};
  try { director.set_volumes(std::numeric_limits<float>::quiet_NaN(), .64f, .82f); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "non-finite audio gain was accepted");
}

void voice_preferences_and_filter(const fs::path& root) {
  using namespace stellar::native_audio;
  using stellar::engine::audio::AudioClip;
  std::vector<float> pcm(9600);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=std::sin(static_cast<float>(i/2)*.12f)*.5f;
  const auto dry=AudioClip::create(pcm),wet=communication_clip(dry,1.f);
  require(communication_clip(dry,0.f)==dry,"Dry voice allocated or altered PCM.");
  require(wet->sample_frames()==dry->sample_frames()&&wet->samples()[100]!=dry->samples()[100],"Radio blend was inert or changed clip duration.");
  for(float sample:wet->samples())require(std::isfinite(sample)&&std::abs(sample)<=1.f,"Radio filter produced invalid PCM.");
  NativeAudioDirector director(root);wait_until_ready(director);director.menu_ready();
  auto prefs=director.voice_preferences();prefs.communication_filter=.75f;director.set_voice_preferences(prefs);
  director.speak(VoiceCue::ReconnaissanceRequired);director.service();
  require(director.caption()&&director.caption()->text.find("Long-range telemetry is incomplete.")==0,"Subtitle does not match the recorded scientist line.");
  const auto first=director.stats().voice_play_count;require(director.replay_last_voice(),"Replay rejected previous line.");director.service();
  require(director.stats().voice_play_count==first+1,"Replay was incorrectly blocked by automatic cooldown.");
  prefs.enabled=false;director.set_voice_preferences(prefs);require(!director.stats().voice_active,"Disable voices left speech active.");
  director.speak(VoiceCue::SurveyComplete);require(director.caption().has_value(),"Disabling speech also disabled subtitles.");
  prefs.subtitles=false;director.set_voice_preferences(prefs);require(!director.caption(),"Subtitle toggle was ignored.");
  prefs.volume=std::numeric_limits<float>::quiet_NaN();bool rejected{};try{director.set_voice_preferences(prefs);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Non-finite voice gain was accepted.");
  director.stop();require(!director.caption(),"Stopped director retained a caption.");
}

void owner_guard(const fs::path& root) {
  NativeAudioDirector director(root, false);
  bool rejected{};
  std::thread other([&] { try { (void)director.stats(); } catch (const std::logic_error&) { rejected = true; } });
  other.join();
  require(rejected, "audio director accepted a non-owner call");
}
}

int main(int argc, char** argv) try {
  if (argc != 2) throw std::invalid_argument("Usage: native_audio_director_tests <asset-root>");
  const auto root = fs::absolute(argv[1]);
  normal_lifecycle(root);
  voice_queue_and_stop(root);
  missing_voice_preserves_required_audio(root);
  disabled_and_failure_paths(root);
  stop_before_decode_completion(root);
  invalid_gain_is_rejected(root);
  owner_guard(root);
  voice_preferences_and_filter(root);
  std::cout << "Native audio director lifecycle tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "native audio director test failed: " << error.what() << '\n';
  return 1;
}
