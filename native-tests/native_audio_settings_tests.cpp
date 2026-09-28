#include "native_audio_settings.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar::native_audio;
using namespace stellar::native_map;

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Point center(UiRect rect) { return {rect.x + rect.width * .5f, rect.y + rect.height * .5f}; }
void click(NativeAudioSettings& settings, UiRect rect, int width = 1280, int height = 720) {
  require(settings.handle({InputEventType::LeftPressed, center(rect)}, width, height), "visible overlay did not capture click");
  (void)settings.handle({InputEventType::LeftReleased, center(rect)}, width, height);
}
void write_text(const fs::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write test settings");
  output << text;
}
std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void layout_and_render(const fs::path& path) {
  NativeAudioSettings settings(path);
  for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080}, std::pair{3840, 2160}}) {
    const auto layout = AudioSettingsLayout::for_viewport(width, height);
    require(layout.panel.width > 0 && layout.panel.height > 0 && layout.panel.x >= 0 && layout.panel.y >= 0,
            "responsive settings panel escaped viewport");
    settings.open();
    settings.set_device_status("No playback device available");
    DrawList draw; settings.render(draw, width, height);
    bool playback_label{};
    for (const auto& command : draw.overlay) if (const auto* text = std::get_if<Text>(&command)) {
      require(text->clip.has_value(), "settings text omitted a clipping rectangle");
      const auto clip = *text->clip;
      require(clip.x >= 0 && clip.y >= 0 && clip.width >= 0 && clip.height >= 0 &&
              clip.x + clip.width <= width && clip.y + clip.height <= height,
              "settings label clip escaped viewport");
      playback_label = playback_label || text->value.find("Playback:") != std::string::npos;
    }
    require(playback_label, "device diagnostic was not rendered");
    settings.cancel();
  }
}

void dragging_mute_and_cancel(const fs::path& path) {
  std::vector<AudioPreferences> previews;
  NativeAudioSettings settings(path, [&](const AudioPreferences& values) { previews.push_back(values); });
  settings.open();
  const auto layout = AudioSettingsLayout::for_viewport(1280, 720);
  const Point inside{layout.master_track.x + layout.master_track.width - .5f, layout.master_track.y + 2.f};
  const Point beyond{layout.master_track.x + layout.master_track.width + 100.f, layout.master_track.y + 2.f};
  (void)settings.handle({InputEventType::LeftPressed, inside}, 1280, 720);
  (void)settings.handle({InputEventType::PointerMove, beyond}, 1280, 720);
  require(settings.values().master > .99f, "slider press did not update preview");
  (void)settings.handle({InputEventType::PointerCancelled, beyond}, 1280, 720);
  const auto stopped = settings.values().master;
  (void)settings.handle({InputEventType::PointerMove, {layout.master_track.x, beyond.y}}, 1280, 720);
  require(settings.values().master == stopped, "pointer cancellation did not stop slider dragging");
  (void)settings.handle({InputEventType::LeftPressed, inside}, 1280, 720);
  const auto before_resize = settings.values().master;
  DrawList resized_draw; settings.render(resized_draw, 1920, 1080);
  (void)settings.handle({InputEventType::PointerMove, {layout.master_track.x, beyond.y}}, 1920, 1080);
  require(settings.values().master == before_resize, "rendering after resize did not preserve drag cancellation for the next input");
  click(settings, layout.mute);
  require(settings.values().muted && settings.values().master == before_resize && !previews.empty(),
          "mute did not retain levels and issue a live preview");
  settings.cancel();
  require(!settings.visible() && settings.values() == settings.saved_values(), "cancel did not restore saved settings");
}

void save_reopen_and_invalid_files(const fs::path& scratch) {
  const auto unicode = scratch / fs::path(L"audio-設定.json");
  NativeAudioSettings settings(unicode);
  settings.open();
  const auto layout = AudioSettingsLayout::for_viewport(1280, 720);
  click(settings, layout.mute);
  click(settings, layout.save);
  require(!settings.visible() && fs::is_regular_file(unicode), "save did not atomically create the Unicode settings path");
  NativeAudioSettings reopened(unicode);
  require(reopened.saved_values().muted, "saved settings did not reopen from the Unicode path");

  const auto corrupt = scratch / "corrupt.json";
  constexpr std::string_view corrupt_text{"{ definitely not JSON"};
  write_text(corrupt, corrupt_text);
  NativeAudioSettings invalid(corrupt);
  require(!invalid.status().empty() && invalid.values() == AudioPreferences{}, "corrupt settings did not retain a visible defaults notice");
  require(read_text(corrupt) == corrupt_text, "loading corrupt settings changed the source file before Save");
  invalid.open(); click(invalid, AudioSettingsLayout::for_viewport(1280, 720).save);
  require(fs::is_regular_file(corrupt), "explicit save did not permit replacing corrupt settings");

  const auto oversized = scratch / "oversized.json";
  write_text(oversized, std::string(static_cast<std::size_t>(4097), 'x'));
  NativeAudioSettings too_large(oversized);
  require(too_large.values() == AudioPreferences{} && !too_large.status().empty(),
          "oversized settings were accepted");
  const auto malformed_schema = scratch / "schema.json";
  write_text(malformed_schema, R"({"schemaVersion":2,"master":0.5,"music":0.5,"effects":0.5,"muted":false})");
  NativeAudioSettings unsupported(malformed_schema);
  require(unsupported.values() == AudioPreferences{} && !unsupported.status().empty(),
          "unsupported settings schema was accepted");
  const auto invalid_gain = scratch / "gain.json";
  write_text(invalid_gain, R"({"schemaVersion":1,"master":1.1,"music":0.5,"effects":0.5,"muted":false})");
  NativeAudioSettings bad_gain(invalid_gain);
  require(bad_gain.values() == AudioPreferences{} && !bad_gain.status().empty(),
          "out-of-range persisted gain was accepted");
  const auto bad_bool = scratch / "bool.json";
  write_text(bad_bool, R"({"schemaVersion":1,"master":0.5,"music":0.5,"effects":0.5,"muted":0})");
  NativeAudioSettings invalid_bool(bad_bool);
  require(invalid_bool.values() == AudioPreferences{} && !invalid_bool.status().empty(),
          "non-boolean muted setting was accepted");
  const auto huge_version = scratch / "huge-version.json";
  write_text(huge_version, R"({"schemaVersion":18446744073709551615,"master":0.5,"music":0.5,"effects":0.5,"muted":false})");
  NativeAudioSettings huge(huge_version);
  require(huge.values() == AudioPreferences{} && !huge.status().empty(), "huge schema version was accepted");
  const auto duplicate = scratch / "duplicate.json";
  write_text(duplicate, R"({"schemaVersion":1,"master":0.5,"master":0.4,"music":0.5,"effects":0.5,"muted":false})");
  NativeAudioSettings repeated(duplicate);
  require(repeated.values() == AudioPreferences{} && !repeated.status().empty(), "duplicate settings member was accepted");

  const auto directory_target = scratch / "not-a-file";
  fs::create_directory(directory_target);
  NativeAudioSettings unwritable(directory_target);
  unwritable.open(); click(unwritable, AudioSettingsLayout::for_viewport(1280, 720).save);
  require(unwritable.visible() && unwritable.status().find("Could not save") != std::string::npos,
          "write failure did not keep the settings overlay open with an error");
}

void constructor_applies_loaded_preferences(const fs::path& scratch) {
  const auto loaded = scratch / "loaded.json";
  write_text(loaded, R"({"schemaVersion":1,"master":0.25,"music":0.5,"effects":0.75,"muted":true})");
  std::vector<AudioPreferences> previews;
  NativeAudioSettings settings(loaded, [&](const AudioPreferences& values) { previews.push_back(values); });
  require(previews.size() == 1 && previews.front() == AudioPreferences{.25f, .5f, .75f, true},
          "constructor did not apply loaded preferences before opening the overlay");
  std::vector<AudioPreferences> defaults;
  NativeAudioSettings missing(scratch / "missing.json", [&](const AudioPreferences& values) { defaults.push_back(values); });
  require(defaults.size() == 1 && defaults.front() == AudioPreferences{},
          "constructor did not apply default preferences for a missing file");
}

void owner_guard(const fs::path& path) {
  NativeAudioSettings settings(path);
  bool rejected{};
  std::thread other([&] { try { (void)settings.visible(); } catch (const std::logic_error&) { rejected = true; } });
  other.join();
  require(rejected, "settings overlay accepted a non-owner call");
}
}

int main(int argc, char** argv) try {
  if (argc != 2) throw std::invalid_argument("Usage: native_audio_settings_tests <scratch>");
  const auto scratch = fs::absolute(argv[1]) /
      ("audio-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(scratch);
  layout_and_render(scratch / "layout.json");
  dragging_mute_and_cancel(scratch / "drag.json");
  save_reopen_and_invalid_files(scratch);
  constructor_applies_loaded_preferences(scratch);
  owner_guard(scratch / "owner.json");
  std::cout << "Native audio settings overlay tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "native audio settings test failed: " << error.what() << '\n';
  return 1;
}
