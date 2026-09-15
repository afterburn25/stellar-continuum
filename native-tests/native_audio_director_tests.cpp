#include "native_audio_director.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
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

void normal_lifecycle(const fs::path& root) {
  NativeAudioDirector director(root);
  require(director.stats().music_start_count == 0, "music started before menu admission");
  wait_until_ready(director);
  require(!director.stats().failed && director.stats().assets_loaded, "real audio assets failed to load");
  require(!director.stats().music_started && director.stats().music_start_count == 0,
          "music started before decoded assets were admitted to the menu");
  director.menu_ready();
  director.service();
  require(director.stats().music_start_count == 1, "menu admission did not start music exactly once");
  director.menu_ready();
  director.service();
  require(director.stats().music_start_count == 1, "repeated menu admission restarted music");
  director.confirm();
  require(director.stats().confirm_count == 1, "confirm did not play an effect");
  for (int index = 0; index < 20; ++index) director.play_event(stellar::native_audio::Cue::Alert);
  director.service();
  require(director.stats().queued_music_bytes > 0 && director.stats().queued_music_bytes <= 288000u,
          "music queue was not filled within its bounded 0.75-second cap");
  director.stop();
  const auto stopped = director.stats();
  director.menu_ready(); director.confirm(); director.service();
  require(director.stats().music_start_count == stopped.music_start_count && director.stats().stopped,
          "stopped director restarted audio");
}

void disabled_and_failure_paths(const fs::path& root) {
  NativeAudioDirector disabled(root / "missing", false);
  require(disabled.assets_ready() && !disabled.stats().failed && !disabled.stats().assets_loaded,
          "disabled audio attempted to load missing assets");
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
  disabled_and_failure_paths(root);
  stop_before_decode_completion(root);
  invalid_gain_is_rejected(root);
  owner_guard(root);
  std::cout << "Native audio director lifecycle tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "native audio director test failed: " << error.what() << '\n';
  return 1;
}
