#pragma once

#include "native_general_settings.hpp"
#include "native_ui_layout.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::native_general {

// Opt-in validation stays on the ordinary Settings -> General route. It
// exercises the TEXT SIZE row end-to-end — a draft change must not leak into
// the live layout, Save applies it through apply_ to
// NativeUiLayout::text_scale and persists the file, Cancel discards — then
// restores the caller's saved preference before returning. The check mutates
// only text_scale, so the persisted file must byte-match when one existed —
// a file first written by the check itself restores equivalently when it
// reconstructs the saved preferences.
template <class Open, class Route, class Capture>
void check_general_settings(NativeGeneralSettings& settings,
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
  const auto initial = settings.saved();
  const auto original_file = bytes_at(settings_path);
  const auto layout = GeneralSettingsLayout::for_viewport(width, height);
  const auto live_scale = [] { return NativeUiLayout::text_scale(); };
  const auto near = [](float a, float b) { return std::fabs(a - b) < .001f; };
  const auto text_scale = [](const GeneralPreferences& prefs) {
    return prefs.accessibility.text_scale;
  };
  // With a prior file, restore must byte-match; with none, a file written by
  // the save leg restores equivalently when it reconstructs the saved prefs.
  const auto file_restored = [&] {
    const auto bytes = bytes_at(settings_path);
    if (original_file) return bytes == original_file;
    if (!bytes) return true;
    NativeGeneralSettings disk(settings_path);
    return disk.saved() == initial;
  };
  const auto click = [&](UiRect bounds) {
    route(InputEvent{InputEventType::LeftPressed,
                     {bounds.x + bounds.width * .5f,
                      bounds.y + bounds.height * .5f}});
  };

  require(near(live_scale(), text_scale(initial)),
          "Live text scale did not match the saved preference at entry.");

  // Cancel leg: the draft moves while live layout and the file stay put.
  open_settings();
  require(settings.visible(), "The menu did not open general settings.");
  capture();
  click(layout.text_scale);
  const float changed = settings.draft().accessibility.text_scale;
  require(!near(changed, text_scale(initial)),
          "TEXT SIZE did not change the draft preference.");
  require(near(live_scale(), text_scale(initial)),
          "Draft text size leaked into the live layout before Save.");
  click(layout.cancel);
  require(!settings.visible() && near(text_scale(settings.saved()), text_scale(initial)) &&
              file_restored(),
          "Cancel did not restore the saved general settings.");

  // Save leg: apply_ must reach NativeUiLayout::text_scale and the file.
  open_settings();
  require(settings.visible(), "General settings did not reopen.");
  click(layout.text_scale);
  click(layout.save);
  require(!settings.visible(), "Save did not close general settings.");
  require(near(live_scale(), changed) && near(text_scale(settings.saved()), changed),
          "Saving TEXT SIZE did not apply the new scale.");
  {
    NativeGeneralSettings disk(settings_path);
    require(near(text_scale(disk.saved()), changed),
            "Saved TEXT SIZE did not survive reconstruction.");
  }

  // Restore leg: cycle back to the saved preset — an off-preset saved value
  // cannot be re-reached by the cycling control, so restore it through the
  // same save path then prove the file round-trips either way.
  open_settings();
  require(settings.visible(), "General settings did not reopen for restore.");
  for (int attempt = 0;
       attempt < 5 && !near(settings.draft().accessibility.text_scale, text_scale(initial));
       ++attempt)
    click(layout.text_scale);
  click(layout.save);
  if (!near(text_scale(settings.saved()), text_scale(initial)))
    require(settings.save(initial),
            "General settings could not restore the saved preference.");
  require(near(live_scale(), text_scale(initial)) && file_restored(),
          "General settings did not restore the original persisted preferences.");

  std::cout << "general_settings_check={\"location\":\"" << location
            << "\",\"opened\":true,\"capture\":true,\"text_scale\":true,"
               "\"cancel_restored\":true,\"saved\":true,\"restored\":true}\n";
}

} // namespace stellar::native_general
