#include "native_audio_settings.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_audio_settings;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

InputEvent press(InputEventType type, Point at) {
  InputEvent event{};
  event.type = type;
  event.position = at;
  return event;
}

void responsive_layout() {
  for (const auto [width, height] :
       {std::pair{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}}) {
    const auto layout = AudioSettingsLayout::for_viewport(width, height);
    const UiRect viewport{0.f, 0.f, static_cast<float>(width),
                          static_cast<float>(height)};
    const auto inside = [&](const UiRect &rect) {
      return rect.x >= viewport.x && rect.y >= viewport.y &&
             rect.x + rect.width <= viewport.x + viewport.width &&
             rect.y + rect.height <= viewport.y + viewport.height;
    };
    require(inside(layout.panel), "panel escaped");
    require(layout.labels.size() == 3 && layout.tracks.size() == 3 &&
                layout.values.size() == 3,
            "slider rows incomplete");
    for (int index = 0; index < 3; ++index)
      require(inside(layout.tracks[index]) && inside(layout.labels[index]),
              "slider row escaped");
    require(inside(layout.done) && inside(layout.defaults),
            "action buttons escaped");
  }
}

void slider_drag_applies() {
  constexpr int width = 1280, height = 720;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  NativeAudioSettingsView view;
  view.open({.5f, .5f, .5f});
  require(view.visible(), "view not visible after open");

  const auto &track = layout.tracks[0];
  const Point quarter{track.x + track.width * .25f,
                      track.y + track.height * .5f};
  auto result = view.handle(press(InputEventType::LeftPressed, quarter),
                            width, height);
  require(result.command == AudioSettingsCommand::Apply,
          "track press must apply");
  require(view.dragging() == 0, "track press did not start the drag");
  require(std::abs(result.values.master - .25f) < .02f,
          "track press did not move master to the click point");

  InputEvent move{};
  move.type = InputEventType::PointerMove;
  move.position = {track.x + track.width * .8f, track.y + 2.f};
  result = view.handle(move, width, height);
  require(result.command == AudioSettingsCommand::Apply,
          "drag move must apply");
  require(std::abs(result.values.master - .8f) < .02f,
          "drag did not track the pointer");

  result = view.handle(press(InputEventType::LeftReleased, move.position),
                       width, height);
  require(result.command == AudioSettingsCommand::None,
          "release must not re-apply");
  require(view.dragging() == -1, "drag not released");

  // A move without an active drag changes nothing.
  result = view.handle(move, width, height);
  require(result.command == AudioSettingsCommand::None,
          "idle move must not apply");
}

void clamp_and_defaults() {
  constexpr int width = 1280, height = 720;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  NativeAudioSettingsView view;
  view.open({.4f, .4f, .4f});

  const auto &track = layout.tracks[2];
  const Point beyond{track.x + track.width + 40.f,
                     track.y + track.height * .5f};
  auto result = view.handle(press(InputEventType::LeftPressed, beyond),
                            width, height);
  require(result.command == AudioSettingsCommand::None,
          "a point outside the track must not apply");

  InputEvent move{};
  move.type = InputEventType::PointerMove;
  move.position = beyond;
  result = view.handle(move, width, height);
  require(result.command == AudioSettingsCommand::None,
          "a stray move must not apply");

  // Defaults restore the reference mix and emit one apply.
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.defaults)),
                       width, height);
  require(result.command == AudioSettingsCommand::Apply,
          "defaults must apply");
  require(std::abs(result.values.master - .78f) < .001f &&
              std::abs(result.values.music - .64f) < .001f &&
              std::abs(result.values.sfx - .82f) < .001f,
          "defaults did not restore the reference mix");
}

void close_paths() {
  constexpr int width = 1280, height = 720;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  NativeAudioSettingsView view;
  view.open({.3f, .4f, .5f});

  auto result = view.handle(press(InputEventType::LeftPressed,
                                  center(layout.done)),
                            width, height);
  require(result.command == AudioSettingsCommand::Close,
          "Done must close");
  require(std::abs(result.values.master - .3f) < .001f,
          "Done must carry the current values for persistence");

  view.open({.3f, .4f, .5f});
  result = view.handle(press(InputEventType::EscapePressed, {}), width,
                       height);
  require(result.command == AudioSettingsCommand::Close,
          "Escape must close");

  // The view is modal: unrelated input stays captured.
  view.open({.3f, .4f, .5f});
  InputEvent wheel{};
  wheel.type = InputEventType::Wheel;
  wheel.position = {width * .5f, height * .5f};
  wheel.wheel_y = 1.f;
  result = view.handle(wheel, width, height);
  require(result.captured && result.command == AudioSettingsCommand::None,
          "modal view must capture stray input");
}

void render_output() {
  constexpr int width = 1280, height = 720;
  NativeAudioSettingsView view;
  view.open({.5f, .6f, .7f});
  DrawList draw;
  view.render(draw, width, height);
  require(!draw.overlay.empty(), "settings view emitted no overlay commands");
}

} // namespace

int main() {
  try {
    responsive_layout();
    slider_drag_applies();
    clamp_and_defaults();
    close_paths();
    render_output();
  } catch (const std::exception &error) {
    std::cerr << "native audio settings tests failed: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "native audio settings tests passed\n";
  return 0;
}
