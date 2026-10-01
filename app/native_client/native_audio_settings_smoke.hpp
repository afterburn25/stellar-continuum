#pragma once

#include "native_audio_settings.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace stellar::native_audio {

// Opt-in runtime validation uses the same menu routing and drawable geometry
// as a player. It runs only against an explicitly isolated smoke save directory.
template <class Open, class Route, class Capture>
void check_audio_settings(NativeAudioSettings& settings,
                          const std::filesystem::path& settings_path,
                          int width, int height, std::string_view context,
                          Open open, Route route, Capture capture) {
  using namespace stellar::native_map;
  const auto require = [](bool ok, std::string_view message) {
    if (!ok) throw std::runtime_error(std::string(message));
  };
  const auto initial = settings.values();
  const AudioPreferences chosen{.25f, .5f, .75f, false};
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  const auto click = [&](UiRect bounds, float fraction = .5f) {
    const Point point{bounds.x + bounds.width * fraction, bounds.y + bounds.height * .5f};
    route(InputEvent{InputEventType::LeftPressed, point});
    route(InputEvent{InputEventType::LeftReleased, point});
  };
  // A slider click stores the gain at the pointer's x-fraction — the
  // x+width*frac roundtrip is not bit-exact at every viewport size, so the
  // preview asserts tolerance while the save/reopen legs keep exact
  // equality against whatever gain the preview actually stored.
  const auto near = [](float a, float b) { return std::fabs(a - b) < .01f; };
  const auto chosen_now = [&] {
    const auto v = settings.values();
    return AudioPreferences{v.master, v.music, v.effects, v.muted};
  };
  const auto verify_choice = [&] {
    const auto v = settings.values();
    return near(v.master, chosen.master) && near(v.music, chosen.music) &&
           near(v.effects, chosen.effects) && !v.muted;
  };
  const auto choose = [&] {
    click(layout.master_track, chosen.master);
    click(layout.music_track, chosen.music);
    click(layout.effects_track, chosen.effects);
    if (settings.values().muted) click(layout.mute);
    require(verify_choice(), "Audio sliders did not preview the chosen volumes.");
  };
  open();
  require(settings.visible(), "The menu did not open audio settings.");
  choose();
  const auto previewed = chosen_now();
  click(layout.mute);
  require(settings.values().muted, "Audio mute did not engage.");
  click(layout.mute);
  require(settings.values() == previewed, "Unmuting lost the chosen volumes.");
  capture();
  click(layout.cancel);
  require(!settings.visible() && settings.values() == initial,
          "Cancel did not restore the previous audio settings.");
  open();
  choose();
  click(layout.save);
  require(!settings.visible() && settings.saved_values() == previewed,
          "Audio settings could not be saved.");
  NativeAudioSettings disk(settings_path);
  require(disk.values() == settings.saved_values(),
          "Saved audio settings did not survive reconstruction.");
  open();
  require(settings.visible() && settings.values() == previewed,
          "Reopening audio settings lost the saved preferences.");
  route(InputEvent{InputEventType::EscapePressed});
  require(!settings.visible() && settings.values() == previewed,
          "Escape did not close only the audio settings overlay.");
  std::cout << "audio_settings_check={\"context\":\"" << context
            << "\",\"opened\":true,\"previewed\":true,\"muted\":true,"
               "\"cancel_restored\":true,\"saved\":true,\"reopened\":true,"
               "\"master\":0.25,\"music\":0.5,\"effects\":0.75,\"initial_master\":"
            << initial.master << ",\"initial_music\":" << initial.music
            << ",\"initial_effects\":" << initial.effects
            << ",\"initial_muted\":" << (initial.muted ? "true" : "false") << "}\n";
}

} // namespace stellar::native_audio
