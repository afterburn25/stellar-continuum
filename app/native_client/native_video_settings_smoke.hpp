#pragma once

#include "native_video_controller.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::native_video_settings {

// Opt-in validation stays on the ordinary Settings → Video route. It changes
// every non-display choice row during the preview (display/resolution stay
// platform-owned), then restores the caller's saved values before returning.
template <class Open, class Route, class Capture>
void check_video_settings(NativeVideoController& controller,
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
  const auto initial = controller.active();
  const auto original_file = bytes_at(settings_path);
  const auto layout = VideoSettingsLayout::for_viewport(width, height);
  require(layout.choice_buttons.size() == 8,
          "Video settings did not expose display, resolution, V-Sync and frame-cap rows.");
  const auto click = [&](UiRect bounds) {
    const Point point{bounds.x + bounds.width * .5f,
                      bounds.y + bounds.height * .5f};
    route(InputEvent{InputEventType::LeftPressed, point});
    route(InputEvent{InputEventType::LeftReleased, point});
  };
  const auto open = [&] {
    open_settings();
    require(controller.visible(), "The menu did not open video settings.");
  };
  const auto cycle_to = [&](std::size_t row,int count,int,int target) {
    click(layout.choice_buttons[row]);
    stellar::native_ui::Dropdown menu;menu.open(static_cast<int>(row),std::vector<std::string>(count,"item"),0);
    click(menu.layout(layout.choice_buttons[row],width,height).rows[target]);
  };
  // Every non-display row has a live consumer: V-Sync/Frame Cap drive
  // pacing, EDGE SMOOTHING + SCENE RESOLUTION drive set_scene_quality's
  // scene-target reallocation, STARFIELD QUALITY drives the 3D
  // RenderQuality3D tier and the streamer budget, STARFIELD DENSITY the
  // starfield backdrop. Cycle each off its saved value.
  const auto samples_index = [](int samples) { return samples == 1 ? 0 : samples == 2 ? 1 : 2; };
  const auto samples_value = [](int index) { return index == 0 ? 1 : index == 1 ? 2 : 4; };
  const auto samples_target = samples_index(initial.scene_samples) == 1 ? 0 : 1;
  const auto resolution_index = [](int percent) { return (percent - 50) / 25; };
  const auto resolution_target = resolution_index(initial.scene_resolution_percent) == 1 ? 0 : 1;
  const auto quality_target = initial.starfield_quality == 0 ? 1 : 0;
  const auto density_target = initial.starfield_density == 0 ? 1 : 0;
  const auto choose_preview = [&](const NativeVideoSettings& from) {
    // No display or resolution click occurs here: platform coverage owns mode
    // changes, while this smoke check exercises the remaining six choice rows.
    cycle_to(2, 3, static_cast<int>(from.vsync),
             static_cast<int>(VideoVsync::Off));
    cycle_to(3, 5, static_cast<int>(from.frame_cap),
             static_cast<int>(VideoFrameCap::Fps60));
    cycle_to(4, 3, samples_index(from.scene_samples), samples_target);
    cycle_to(5, 3, resolution_index(from.scene_resolution_percent), resolution_target);
    cycle_to(6, 4, from.starfield_quality, quality_target);
    cycle_to(7, 3, from.starfield_density, density_target);
    click(layout.apply);
    require(controller.previewing() && controller.active().vsync == VideoVsync::Off &&
                controller.active().frame_cap == VideoFrameCap::Fps60 &&
                controller.active().scene_samples == samples_value(samples_target) &&
                controller.active().scene_resolution_percent == 50 + resolution_target * 25 &&
                controller.active().starfield_quality == quality_target &&
                controller.active().starfield_density == density_target,
            "Video preview did not apply the expected pacing/quality values.");
  };

  open();
  capture(false);
  choose_preview(initial);
  capture(true);
  route(InputEvent{InputEventType::EscapePressed});
  require(!controller.previewing() && controller.active() == initial &&
              bytes_at(settings_path) == original_file,
          "Escaping confirmation did not restore the active settings and original file.");

  if (!controller.visible()) open();
  choose_preview(initial);
  click(layout.keep);
  const NativeVideoSettings previewed{.display = initial.display,
                                      .width = initial.width,
                                      .height = initial.height,
                                      .refresh_hz = initial.refresh_hz,
                                      .vsync = VideoVsync::Off,
                                      .frame_cap = VideoFrameCap::Fps60,
                                      .scene_resolution_percent=50 + resolution_target * 25, .scene_samples=samples_value(samples_target),
                                      .starfield_quality=quality_target, .starfield_density=density_target};
  require(!controller.previewing() && !controller.visible() &&
              NativeVideoSettings::load(settings_path) == previewed,
          "Keeping video settings did not atomically persist the preview.");

  open();
  cycle_to(2, 3, static_cast<int>(VideoVsync::Off),
           static_cast<int>(initial.vsync));
  cycle_to(3, 5, static_cast<int>(VideoFrameCap::Fps60),
           static_cast<int>(initial.frame_cap));
  cycle_to(4, 3, samples_target, samples_index(initial.scene_samples));
  cycle_to(5, 3, resolution_target, resolution_index(initial.scene_resolution_percent));
  cycle_to(6, 4, quality_target, static_cast<int>(initial.starfield_quality));
  cycle_to(7, 3, density_target, static_cast<int>(initial.starfield_density));
  click(layout.apply);
  require(controller.previewing() && controller.active() == initial,
          "Video settings could not stage restoration of the original values.");
  click(layout.keep);
  require(!controller.previewing() && controller.active() == initial &&
              NativeVideoSettings::load(settings_path) == initial,
          "Video settings did not restore the original persisted preferences.");

  std::cout << "video_settings_check={\"location\":\"" << location
            << "\",\"opened\":true,\"choice_rows\":true,\"previewed\":true,"
               "\"normal_capture\":true,\"confirm_capture\":true,"
               "\"escape_reverted\":true,\"kept\":true,\"restored\":true}\n";
}

} // namespace stellar::native_video_settings
