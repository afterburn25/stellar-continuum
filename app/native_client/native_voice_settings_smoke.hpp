#pragma once

#include "native_voice_settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_audio {

// Opt-in validation stays on the ordinary Settings -> Voice route. It flips
// every toggle, drags all three sliders and re-selects both dropdowns during
// the preview, exercises Defaults/Replay/Stop, then restores the caller's
// saved values before returning. Slider clicks store the pointer's
// x-fraction — the roundtrip is not bit-exact at every viewport size, so
// float fields assert a .01 tolerance while toggles/choices stay exact.
template <class Open, class Route, class Capture>
void check_voice_settings(NativeVoiceSettings& settings,
                          const std::filesystem::path& settings_path,
                          int width, int height, std::string_view location,
                          Open open_settings, Route route, Capture capture) {
  using namespace stellar::native_map;
  const auto require = [](bool ok, std::string_view message) {
    if (!ok) throw std::runtime_error(std::string(message));
  };
  const auto bytes_at = [](const std::filesystem::path& path)
      -> std::optional<std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  };
  const auto initial = settings.saved_values();
  const auto original_file = bytes_at(settings_path);
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  const auto click = [&](UiRect bounds, float fraction = .5f) {
    const Point point{bounds.x + bounds.width * fraction, bounds.y + bounds.height * .5f};
    route(InputEvent{InputEventType::LeftPressed, point});
    route(InputEvent{InputEventType::LeftReleased, point});
  };
  const auto open = [&] {
    open_settings();
    require(settings.visible(), "The menu did not open voice settings.");
  };
  // Dropdown rows overlay their anchor — pick through a local layout mirror
  // of the same geometry the view computed when the row was clicked.
  const auto pick_dropdown = [&](UiRect anchor, int count, int target) {
    stellar::native_ui::Dropdown menu;
    menu.open(0, std::vector<std::string>(static_cast<std::size_t>(count), "item"), 0);
    click(menu.layout(anchor, width, height).rows[static_cast<std::size_t>(target)]);
  };
  constexpr std::array<int, 5> subtitle_sizes{14, 18, 22, 26, 32};
  const auto size_index = [&] {
    const auto found = std::ranges::find(subtitle_sizes, initial.subtitle_size);
    return found == subtitle_sizes.end() ? 1 : static_cast<int>(found - subtitle_sizes.begin());
  };
  const int size_target = size_index() == 2 ? 1 : 2;
  const int frequency_target =
      static_cast<int>(initial.frequency) == 1 ? 2 : 1;
  const auto near = [](float a, float b) { return std::fabs(a - b) < .01f; };
  // Expected state after choose(): every control moved off its saved value.
  const auto expected = [&] {
    VoicePreferences out = initial;
    out.enabled = !initial.enabled;
    out.volume = .4f;
    out.subtitles = !initial.subtitles;
    out.subtitle_size = subtitle_sizes[static_cast<std::size_t>(size_target)];
    out.subtitle_background_opacity = .3f;
    out.speaker_labels = !initial.speaker_labels;
    out.communication_filter = .6f;
    out.frequency = static_cast<VoiceFrequency>(frequency_target);
    out.no_interruptions = !initial.no_interruptions;
    out.interface_announcements = !initial.interface_announcements;
    return out;
  }();
  const auto matches = [&](const VoicePreferences& actual,
                           const VoicePreferences& wanted) {
    return actual.enabled == wanted.enabled && near(actual.volume, wanted.volume) &&
           actual.subtitles == wanted.subtitles &&
           actual.subtitle_size == wanted.subtitle_size &&
           near(actual.subtitle_background_opacity, wanted.subtitle_background_opacity) &&
           actual.speaker_labels == wanted.speaker_labels &&
           near(actual.communication_filter, wanted.communication_filter) &&
           actual.frequency == wanted.frequency &&
           actual.no_interruptions == wanted.no_interruptions &&
           actual.interface_announcements == wanted.interface_announcements;
  };
  const auto choose = [&] {
    click(layout.enable_voices);
    click(layout.volume_track, .4f);
    click(layout.subtitles);
    click(layout.subtitle_size);
    pick_dropdown(layout.subtitle_size, 5, size_target);
    click(layout.background_track, .3f);
    click(layout.speaker_labels);
    click(layout.filter_track, .6f);
    click(layout.frequency);
    pick_dropdown(layout.frequency, 3, frequency_target);
    click(layout.no_interruptions);
    click(layout.interface_announcements);
    require(matches(settings.values(), expected),
            "Voice settings did not preview the chosen values.");
  };
  // A fraction of exactly 0 or 1 lands on the track's half-open bounds and
  // misses — clamp inside the .01 float tolerance this helper asserts.
  const auto track_fraction = [](float value) {
    return std::clamp(value, .001f, .999f);
  };
  const auto restore = [&] {
    const auto current = settings.values();
    if (current.enabled != initial.enabled) click(layout.enable_voices);
    click(layout.volume_track, track_fraction(initial.volume));
    if (current.subtitles != initial.subtitles) click(layout.subtitles);
    if (current.subtitle_size != initial.subtitle_size) {
      click(layout.subtitle_size);
      pick_dropdown(layout.subtitle_size, 5, size_index());
    }
    click(layout.background_track, track_fraction(initial.subtitle_background_opacity));
    if (current.speaker_labels != initial.speaker_labels) click(layout.speaker_labels);
    click(layout.filter_track, track_fraction(initial.communication_filter));
    if (current.frequency != initial.frequency) {
      click(layout.frequency);
      pick_dropdown(layout.frequency, 3, static_cast<int>(initial.frequency));
    }
    if (current.no_interruptions != initial.no_interruptions) click(layout.no_interruptions);
    if (current.interface_announcements != initial.interface_announcements)
      click(layout.interface_announcements);
  };

  open();
  capture();
  choose();
  click(layout.replay);
  click(layout.stop);
  click(layout.defaults);
  require(settings.values() == VoicePreferences{},
          "Defaults did not reset the previewed voice settings.");
  click(layout.cancel);
  require(!settings.visible() && settings.values() == initial &&
              bytes_at(settings_path) == original_file,
          "Cancel did not restore the saved voice settings.");

  open();
  choose();
  click(layout.save);
  require(!settings.visible() && matches(settings.saved_values(), expected),
          "Voice settings could not be saved.");
  {
    NativeVoiceSettings disk(settings_path);
    require(disk.values() == settings.saved_values(),
            "Saved voice settings did not survive reconstruction.");
  }

  open();
  restore();
  click(layout.save);
  {
    NativeVoiceSettings disk(settings_path);
    require(!settings.visible() && matches(disk.values(), initial),
            "Voice settings did not restore the original persisted preferences.");
  }

  std::cout << "voice_settings_check={\"location\":\"" << location
            << "\",\"opened\":true,\"previewed\":true,\"replay\":true,"
               "\"stop\":true,\"defaults\":true,\"cancel_restored\":true,"
               "\"saved\":true,\"restored\":true}\n";
}

} // namespace stellar::native_audio
