#include "native_voice_settings.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_voice_settings;
using namespace stellar::native_map;
using stellar::native_voice::VoiceFrequency;

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
    const auto layout = VoiceSettingsLayout::for_viewport(width, height);
    const UiRect viewport{0.f, 0.f, static_cast<float>(width),
                          static_cast<float>(height)};
    const auto inside = [&](const UiRect &rect) {
      return rect.x >= viewport.x && rect.y >= viewport.y &&
             rect.x + rect.width <= viewport.x + viewport.width &&
             rect.y + rect.height <= viewport.y + viewport.height;
    };
    require(inside(layout.panel), "panel escaped");
    require(layout.toggle_boxes.size() == 4 && layout.toggle_labels.size() == 4,
            "toggle rows incomplete");
    require(layout.slider_tracks.size() == 3 &&
                layout.slider_labels.size() == 3 &&
                layout.slider_values.size() == 3,
            "slider rows incomplete");
    require(layout.choice_labels.size() == 2 &&
                layout.choice_buttons.size() == 2,
            "choice rows incomplete");
    for (int index = 0; index < 4; ++index)
      require(inside(layout.toggle_boxes[index]), "toggle escaped");
    for (int index = 0; index < 3; ++index)
      require(inside(layout.slider_tracks[index]), "slider escaped");
    for (int index = 0; index < 2; ++index)
      require(inside(layout.choice_buttons[index]), "choice escaped");
    require(inside(layout.replay) && inside(layout.stop) && inside(layout.close),
            "action buttons escaped");
    // Rows stay inside the panel and never overlap each other.
    for (int index = 0; index < 4; ++index) {
      require(layout.panel.contains({layout.toggle_boxes[index].x + 1.f,
                                     layout.toggle_boxes[index].y + 1.f}),
              "toggle outside panel");
      if (index > 0)
        require(layout.toggle_boxes[index].y >
                    layout.toggle_boxes[index - 1].y,
                "toggle rows overlap");
    }
    require(layout.slider_tracks[0].y > layout.toggle_boxes.back().y,
            "sliders above toggles");
    for (int index = 1; index < 3; ++index)
      require(layout.slider_tracks[index].y > layout.slider_tracks[index - 1].y,
              "slider rows overlap");
    require(layout.choice_buttons[0].y > layout.slider_tracks.back().y,
            "choice above sliders");
    require(layout.replay.y > layout.choice_buttons.back().y,
            "actions above choices");
  }
}

void toggle_applies() {
  constexpr int width = 1280, height = 720;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  NativeVoiceSettingsView view;
  NativeVoiceSettings initial{};
  view.open(initial);
  require(view.visible(), "view not visible after open");
  require(view.values().enable_voices && view.values().subtitles &&
              view.values().speaker_labels && !view.values().no_interruptions,
          "defaults differ from NativeVoiceSettings");

  // Toggle 0 (enable voices) off — emits Apply with the flipped value.
  auto result =
      view.handle(press(InputEventType::LeftPressed,
                        center(layout.toggle_boxes[0])), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              !result.values.enable_voices,
          "voice toggle did not emit Apply with the new value");
  require(result.captured, "toggle click not captured");

  // Toggle 3 (no interruptions) on via its label rect.
  result =
      view.handle(press(InputEventType::LeftPressed,
                        center(layout.toggle_labels[3])), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              result.values.no_interruptions,
          "label click did not toggle no-interruptions");

  // Escape and Close both emit Close.
  result = view.handle(press(InputEventType::EscapePressed, {}), width, height);
  require(result.command == VoiceSettingsCommand::Close, "Escape did not close");
  result = view.handle(press(InputEventType::LeftPressed,
                             center(layout.close)), width, height);
  require(result.command == VoiceSettingsCommand::Close, "Close did not close");
  view.close();
  require(!view.visible(), "view stayed visible after close");
}

void slider_drag_applies() {
  constexpr int width = 1280, height = 720;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  NativeVoiceSettingsView view;
  view.open(NativeVoiceSettings{});

  // Slider 0 (voice volume): press at 25% then drag to 50%.
  const auto &track = layout.slider_tracks[0];
  const Point quarter{track.x + track.width * .25f,
                      track.y + track.height * .5f};
  auto result =
      view.handle(press(InputEventType::LeftPressed, quarter), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              std::abs(result.values.volume - .25f) < .01f,
          "slider press did not apply");
  require(view.dragging() == 0, "slider drag not armed");
  const Point half{track.x + track.width * .5f, track.y + track.height * .5f};
  result =
      view.handle(press(InputEventType::PointerMove, half), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              std::abs(result.values.volume - .5f) < .01f,
          "slider drag did not update the value");
  result =
      view.handle(press(InputEventType::LeftReleased, half), width, height);
  require(view.dragging() < 0, "slider drag not released");

  // Slider 2 (communication filter) clamps out-of-range presses.
  const auto &filter = layout.slider_tracks[2];
  result = view.handle(
      press(InputEventType::LeftPressed,
            {filter.x + filter.width + 40.f, filter.y + filter.height * .5f}),
      width, height);
  require(result.command != VoiceSettingsCommand::Apply ||
              result.values.comms_intensity <= 1.f,
          "filter exceeded the 0-1 range");
}

void choice_cycles() {
  constexpr int width = 1280, height = 720;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  NativeVoiceSettingsView view;
  NativeVoiceSettings initial{};
  initial.chatter_level = .4f;
  view.open(initial);

  // Subtitle size cycles 18 → 22.
  auto result =
      view.handle(press(InputEventType::LeftPressed,
                        center(layout.choice_buttons[0])), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              result.values.subtitle_size == 22,
          "subtitle size did not cycle upward");
  // Wraps from 32 back to 14.
  for (int index = 0; index < 3; ++index)
    result = view.handle(press(InputEventType::LeftPressed,
                               center(layout.choice_buttons[0])),
                         width, height);
  require(result.values.subtitle_size == 14, "subtitle size did not wrap");

  // Frequency cycles Normal → Frequent and restores chatter level to 1
  // (reference: selecting a frequency sets ChatterLevel = 1).
  result =
      view.handle(press(InputEventType::LeftPressed,
                        center(layout.choice_buttons[1])), width, height);
  require(result.command == VoiceSettingsCommand::Apply &&
              result.values.frequency == VoiceFrequency::Frequent &&
              result.values.chatter_level == 1.f,
          "frequency cycle did not restore chatter level");
  // Frequent → Minimal wraps.
  result =
      view.handle(press(InputEventType::LeftPressed,
                        center(layout.choice_buttons[1])), width, height);
  require(result.values.frequency == VoiceFrequency::Minimal,
          "frequency did not wrap to Minimal");
}

void action_commands() {
  constexpr int width = 1280, height = 720;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  NativeVoiceSettingsView view;
  view.open(NativeVoiceSettings{});

  auto result = view.handle(
      press(InputEventType::LeftPressed, center(layout.replay)), width, height);
  require(result.command == VoiceSettingsCommand::Replay && result.captured,
          "Replay did not emit its command");
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.stop)), width, height);
  require(result.command == VoiceSettingsCommand::Stop && result.captured,
          "Stop did not emit its command");
  // An in-panel dead zone (between choices and actions) emits no command.
  const Point dead{layout.panel.x + layout.panel.width * .5f,
                   layout.choice_buttons.back().y +
                       layout.choice_buttons.back().height + 4.f};
  result = view.handle(press(InputEventType::LeftPressed, dead), width, height);
  require(result.command == VoiceSettingsCommand::None && result.captured,
          "in-panel dead zone emitted a command");
  // Closed views ignore input entirely.
  view.close();
  result = view.handle(
      press(InputEventType::LeftPressed, center(layout.replay)), width, height);
  require(result.command == VoiceSettingsCommand::None && !result.captured,
          "closed view captured input");
}

} // namespace

int main() {
  try {
    responsive_layout();
    toggle_applies();
    slider_drag_applies();
    choice_cycles();
    action_commands();
  } catch (const std::exception &error) {
    std::cerr << "native voice settings tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native voice settings tests passed\n";
  return 0;
}
