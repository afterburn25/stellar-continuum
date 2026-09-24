#include "native_voice_settings.hpp"

#include <chrono>
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
void click(NativeVoiceSettings& settings, UiRect rect, int width = 1280, int height = 720) {
  require(settings.handle({InputEventType::LeftPressed, center(rect)}, width, height),
          "visible voice settings did not capture input");
  (void)settings.handle({InputEventType::LeftReleased, center(rect)}, width, height);
}
void write_text(const fs::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write voice settings test file");
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
}
std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
bool contained(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.width >= 0 && inner.height >= 0 &&
         inner.x + inner.width <= outer.x + outer.width + .01f &&
         inner.y + inner.height <= outer.y + outer.height + .01f;
}

void responsive_layout_and_render(const fs::path& path) {
  NativeVoiceSettings settings(path, {}, [] {}, [] {});
  for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080},
                                    std::pair{2560, 1440}, std::pair{3840, 2160}}) {
    const auto layout = VoiceSettingsLayout::for_viewport(width, height);
    require(contained({0, 0, static_cast<float>(width), static_cast<float>(height)}, layout.panel),
            "voice panel escaped the viewport");
    const UiRect controls[]{layout.title, layout.introduction, layout.enable_voices, layout.volume_track,
                           layout.subtitles, layout.subtitle_size, layout.background_track,
                           layout.speaker_labels, layout.filter_track, layout.frequency,
                           layout.no_interruptions, layout.interface_announcements, layout.replay,
                           layout.stop, layout.defaults, layout.cancel, layout.save, layout.status};
    for (const auto control : controls) require(contained(layout.panel, control), "voice control escaped the panel");
    require(layout.interface_announcements.y >= layout.no_interruptions.y + layout.no_interruptions.height,
            "voice announcement toggle overlaps the controls");
    require(layout.replay.y >= layout.interface_announcements.y + layout.interface_announcements.height,
            "voice action buttons overlap the controls");
    require(layout.defaults.y >= layout.replay.y + layout.replay.height,
            "voice footer buttons overlap replay controls");
    require(layout.status.y >= layout.save.y + layout.save.height, "voice status overlaps footer buttons");
    settings.open();
    DrawList draw;
    settings.render(draw, width, height);
    bool heading{}, replay{};
    for (const auto& command : draw.overlay) if (const auto* text = std::get_if<Text>(&command)) {
      require(text->clip.has_value(), "voice text omitted its clipping rectangle");
      require(contained({0, 0, static_cast<float>(width), static_cast<float>(height)}, *text->clip),
              "voice text clip escaped the viewport");
      heading = heading || text->value == "VOICE & SUBTITLES";
      replay = replay || text->value == "Replay last announcement";
    }
    require(heading && replay, "voice render omitted primary content");
    settings.cancel();
  }
}

void input_preview_callbacks_and_rollback(const fs::path& path) {
  std::vector<VoicePreferences> previews;
  int replay_count{}, stop_count{};
  NativeVoiceSettings settings(path, [&](const VoicePreferences& value) { previews.push_back(value); },
                               [&] { ++replay_count; }, [&] { ++stop_count; });
  require(previews.size() == 1 && previews.back() == VoicePreferences{},
          "constructor did not apply default voice preferences");
  settings.open();
  const auto layout = VoiceSettingsLayout::for_viewport(1280, 720);
  click(settings, layout.enable_voices);
  click(settings, layout.subtitles);
  click(settings, layout.speaker_labels);
  click(settings, layout.no_interruptions);
  click(settings, layout.interface_announcements);
  click(settings, layout.subtitle_size);
  {stellar::native_ui::Dropdown menu;menu.open(0,{"14","18","22","26","32"},0);click(settings,menu.layout(layout.subtitle_size,1280,720).rows[2]);}
  click(settings, layout.frequency);
  {stellar::native_ui::Dropdown menu;menu.open(1,{"Minimal","Normal","Frequent"},0);click(settings,menu.layout(layout.frequency,1280,720).rows[2]);}
  require(!settings.values().enabled && !settings.values().subtitles && !settings.values().speaker_labels &&
          !settings.values().no_interruptions && settings.values().interface_announcements &&
          settings.values().subtitle_size == 22 &&
          settings.values().frequency == VoiceFrequency::Frequent,
          "toggle and choice controls did not update the draft");

  const Point volume_press{layout.volume_track.x + layout.volume_track.width * .2f, center(layout.volume_track).y};
  const Point volume_drag{layout.volume_track.x + layout.volume_track.width * .8f, center(layout.volume_track).y};
  (void)settings.handle({InputEventType::LeftPressed, volume_press}, 1280, 720);
  (void)settings.handle({InputEventType::PointerMove, volume_drag}, 1280, 720);
  require(settings.values().volume > .79f && settings.values().volume < .81f, "voice volume did not drag continuously");
  (void)settings.handle({InputEventType::PointerCancelled, volume_drag}, 1280, 720);
  const auto cancelled_volume = settings.values().volume;
  (void)settings.handle({InputEventType::PointerMove, center(layout.volume_track)}, 1280, 720);
  require(settings.values().volume == cancelled_volume, "focus loss retained voice slider dragging");

  const Point background{layout.background_track.x + layout.background_track.width * .25f,
                         center(layout.background_track).y};
  click(settings, {background.x, background.y, 1, 1});
  const Point filter{layout.filter_track.x + layout.filter_track.width * .6f, center(layout.filter_track).y};
  click(settings, {filter.x, filter.y, 1, 1});
  require(settings.values().subtitle_background_opacity > .24f &&
          settings.values().communication_filter > .59f, "secondary voice sliders did not update");
  click(settings, layout.replay);
  click(settings, layout.stop);
  require(replay_count == 1 && stop_count == 1, "replay or stop callback was not invoked exactly once");
  require(previews.size() >= 10, "live preview did not run for draft changes");

  (void)settings.handle({InputEventType::LeftPressed, center(layout.filter_track)}, 1280, 720);
  const auto before_resize = settings.values().communication_filter;
  (void)settings.handle({InputEventType::PointerMove, {layout.filter_track.x, layout.filter_track.y}}, 1920, 1080);
  require(settings.values().communication_filter == before_resize, "resize retained a stale slider drag");
  (void)settings.handle({InputEventType::EscapePressed}, 1920, 1080);
  require(!settings.visible() && settings.values() == settings.saved_values() && settings.values() == VoicePreferences{},
          "Escape did not roll the preview back to saved voice settings");
}

void persistence_and_failed_save(const fs::path& scratch) {
  const auto path = scratch / fs::path(L"voice-設定.json");
  NativeVoiceSettings settings(path);
  settings.open();
  const auto layout = VoiceSettingsLayout::for_viewport(1280, 720);
  click(settings, layout.enable_voices);
  click(settings, layout.subtitle_size);
  {stellar::native_ui::Dropdown menu;menu.open(0,{"14","18","22","26","32"},0);click(settings,menu.layout(layout.subtitle_size,1280,720).rows[2]);}
  click(settings, layout.frequency);
  {stellar::native_ui::Dropdown menu;menu.open(1,{"Minimal","Normal","Frequent"},0);click(settings,menu.layout(layout.frequency,1280,720).rows[2]);}
  click(settings, layout.no_interruptions);
  click(settings, layout.interface_announcements);
  (void)settings.handle({InputEventType::LeftPressed,
                         {layout.volume_track.x + layout.volume_track.width * .4f, center(layout.volume_track).y}},
                        1280, 720);
  (void)settings.handle({InputEventType::LeftReleased}, 1280, 720);
  const auto expected = settings.values();
  click(settings, layout.save);
  require(!settings.visible() && fs::is_regular_file(path), "voice settings were not atomically saved");
  NativeVoiceSettings reopened(path);
  require(reopened.values() == expected && reopened.saved_values() == expected,
          "valid persisted voice settings were not restored");
  require(reopened.values().interface_announcements,
          "persisted voice settings lost the interface-announcement flag");

  const auto legacy = scratch / "legacy.json";
  write_text(legacy,
             R"({"schemaVersion":1,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})");
  NativeVoiceSettings legacy_settings(legacy);
  require(legacy_settings.status().empty() && !legacy_settings.values().interface_announcements,
          "legacy voice settings did not load with announcements defaulting off");

  const auto directory_target = scratch / "directory-target";
  fs::create_directory(directory_target);
  NativeVoiceSettings unwritable(directory_target);
  unwritable.open();
  click(unwritable, VoiceSettingsLayout::for_viewport(1280, 720).enable_voices);
  const auto draft = unwritable.values();
  click(unwritable, VoiceSettingsLayout::for_viewport(1280, 720).save);
  require(unwritable.visible() && unwritable.values() == draft && unwritable.values() != unwritable.saved_values() &&
          unwritable.status().find("Could not save") != std::string::npos,
          "save failure did not preserve the draft and modal error");
}

void malformed_preferences(const fs::path& scratch) {
  const auto corrupt = scratch / "corrupt.json";
  constexpr std::string_view corrupt_source{"{not json"};
  write_text(corrupt, corrupt_source);
  NativeVoiceSettings corrupt_settings(corrupt);
  require(corrupt_settings.values() == VoicePreferences{} && !corrupt_settings.status().empty(),
          "corrupt voice settings were accepted");
  require(read_text(corrupt) == corrupt_source, "loading corrupt voice settings changed the source");

  const std::vector<std::pair<std::string, std::string>> invalid{
      {"oversized.json", std::string(4097, 'x')},
      {"schema.json", R"({"schemaVersion":2,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})"},
      {"range.json", R"({"schemaVersion":1,"enabled":true,"volume":1.1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})"},
      {"size.json", R"({"schemaVersion":1,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":17,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})"},
      {"frequency.json", R"({"schemaVersion":1,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":3,"noInterruptions":false})"},
      {"type.json", R"({"schemaVersion":1,"enabled":1,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})"},
      {"announcements.json", R"({"schemaVersion":1,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false,"interfaceAnnouncements":1})"},
      {"extra.json", R"({"schemaVersion":1,"enabled":true,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false,"extra":0})"},
      {"duplicate.json", R"({"schemaVersion":1,"enabled":true,"enabled":false,"volume":1,"subtitles":true,"subtitleSize":18,"backgroundOpacity":0.7,"speakerLabels":true,"communicationFilter":0,"frequency":1,"noInterruptions":false})"},
      {"nul.json", std::string{"{}\0{}", 5}}};
  for (const auto& [name, source] : invalid) {
    const auto path = scratch / name;
    write_text(path, source);
    NativeVoiceSettings settings(path);
    require(settings.values() == VoicePreferences{} && !settings.status().empty(),
            "invalid voice preferences were accepted");
  }

  std::vector<VoicePreferences> defaults;
  NativeVoiceSettings missing(scratch / "missing.json", [&](const VoicePreferences& value) { defaults.push_back(value); });
  require(missing.status().empty() && defaults.size() == 1 && defaults.front() == VoicePreferences{},
          "missing voice settings reported a false load error");
}

void owner_guard(const fs::path& path) {
  NativeVoiceSettings settings(path);
  bool rejected{};
  std::thread other([&] { try { (void)settings.visible(); } catch (const std::logic_error&) { rejected = true; } });
  other.join();
  require(rejected, "voice settings accepted a non-owner call");
}

void localized_labels(const fs::path& path) {
  stellar::engine::LocalizationTable locale{"en", "en"};
  std::string error;
  require(locale.load_json(
              R"({"locale":"en","strings":{"SETTINGS_VOICE_TITLE":"VOIX ET SOUS-TITRES","SETTINGS_VOICE_REPLAY":"Rejouer"}})",
              &error),
          "the test locale table must parse");
  NativeVoiceSettings settings(path, {}, [] {}, [] {});
  settings.set_localization(&locale);
  settings.open();
  DrawList draw;
  settings.render(draw, 1280, 720);
  bool heading{}, replay{}, fallback{};
  for (const auto& command : draw.overlay)
    if (const auto* text = std::get_if<Text>(&command)) {
      heading = heading || text->value == "VOIX ET SOUS-TITRES";
      replay = replay || text->value == "Rejouer";
      fallback = fallback || text->value == "Enable voices";
    }
  require(heading && replay, "catalogued voice labels must render translated");
  require(fallback, "uncatalogued voice labels must keep their English text");
  settings.cancel();
}
} // namespace

void keyboard_focus_and_sliders(const fs::path& path) {
  int replays{};
  NativeVoiceSettings settings(path, {}, [&] { ++replays; }, {});
  settings.open();
  const auto press = [&](std::uint32_t k, bool shift = false) {
    InputEvent event{};
    event.type = InputEventType::KeyPressed;
    event.key = k;
    event.shift = shift;
    return settings.handle(event, 1280, 720);
  };
  constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
  constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u;
  constexpr std::uint32_t kEnd = 0x4000004du;
  require(settings.focused() < 0, "voice settings opened with stale focus");
  require(settings.focused_label().empty(), "unfocused panel reported a label");
  require(press(kTab) && settings.focused() == 0, "Tab did not focus ENABLE VOICES");
  require(settings.focused_label() == "Enable voices: On",
          "focused_label did not name ENABLE VOICES with its state");
  require(press(kSpace) && !settings.values().enabled && settings.focused() == 0,
          "Space did not toggle ENABLE VOICES in place");
  require(settings.focused_label() == "Enable voices: Off",
          "focused_label did not track the toggle");
  // The volume slider (index 1) adjusts on arrows, snaps on Home/End.
  require(press(kTab) && settings.focused() == 1, "Tab did not reach the volume slider");
  require(settings.focused_label() == "Voice volume: 100%",
          "focused_label did not announce the slider value");
  const float volume = settings.values().volume;
  require(press(kLeft) && settings.values().volume == volume - .05f,
          "Left arrow did not lower the focused volume slider");
  require(press(kRight) && settings.values().volume == volume,
          "Right arrow did not restore the volume slider");
  require(press(kEnd) && settings.values().volume == 1.f,
          "End did not maximize the focused slider");
  // Off a slider, arrows navigate the ring.
  require(press(kTab) && settings.focused() == 2 && press(kLeft) && settings.focused() == 1,
          "arrow keys did not move focus around sliders");
  // INTERFACE ANNOUNCEMENTS (index 9) reports its state, then REPLAY (index 10).
  for (int i = 0; i < 8; ++i) (void)press(kTab);
  require(settings.focused() == 9 &&
              settings.focused_label() == "Speak interface announcements: Off",
          "Tab chain did not reach the announcement toggle");
  require(press(kTab) && settings.focused() == 10, "Tab chain did not reach REPLAY");
  require(press(kReturn) && replays == 1, "Return on REPLAY did not invoke the callback");
  // SAVE (index 14) wraps from the last control.
  require(press(kEnd) && settings.focused() == 14 &&
              settings.focused_label() == "Save",
          "End did not reach SAVE");
  require(press(kTab) && settings.focused() == 0, "focus did not wrap to the first control");
}

int main(int argc, char** argv) try {
  if (argc != 2) throw std::invalid_argument("Usage: native_voice_settings_tests <scratch>");
  const auto scratch = fs::absolute(argv[1]) /
      ("voice-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(scratch);
  responsive_layout_and_render(scratch / "layout.json");
  input_preview_callbacks_and_rollback(scratch / "input.json");
  persistence_and_failed_save(scratch);
  malformed_preferences(scratch);
  keyboard_focus_and_sliders(scratch / "focus.json");
  owner_guard(scratch / "owner.json");
  localized_labels(scratch / "localized.json");
  std::cout << "Native voice settings tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "native voice settings test failed: " << error.what() << '\n';
  return 1;
}
