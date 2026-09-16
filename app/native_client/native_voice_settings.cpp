#include "native_voice_settings.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace stellar::native_voice_settings {
namespace {
using namespace stellar::native_map;

// Reference palette (VisualPalette / MainMenuLayer).
constexpr Color border = native_ui::color::keyline_strong;
constexpr Color track_color = native_ui::color::canvas;
constexpr Color text_primary = native_ui::color::text_primary;
constexpr Color text_muted = native_ui::color::text_secondary;
constexpr Color gold = native_ui::color::selected;
constexpr Color checked = native_ui::color::success;

constexpr std::array<std::string_view, 4> toggle_names = {
    "ENABLE VOICES", "SUBTITLES", "SPEAKER LABELS",
    "DO NOT INTERRUPT DIALOGUE"};
constexpr std::array<std::string_view, 3> slider_names = {
    "VOICE VOLUME", "SUBTITLE BACKGROUND", "COMMUNICATION FILTER"};
constexpr std::array<std::string_view, 2> choice_names = {
    "SUBTITLE SIZE", "ANNOUNCEMENT FREQUENCY"};
constexpr std::array<int, 5> subtitle_sizes = {14, 18, 22, 26, 32};
constexpr std::array<std::string_view, 3> frequency_names = {"MINIMAL",
                                                           "NORMAL",
                                                           "FREQUENT"};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  out.overlay.emplace_back(Text{at, std::move(value), color, pixels, 0.f,
                                std::nullopt, align, face});
}

[[nodiscard]] std::string size_label(const int size) {
  return std::to_string(size) + " PX";
}
[[nodiscard]] std::string_view frequency_label(const VoiceFrequency value) {
  const auto index = std::clamp(static_cast<int>(value), 0, 2);
  return frequency_names[static_cast<std::size_t>(index)];
}
} // namespace

VoiceSettingsLayout
VoiceSettingsLayout::for_viewport(const int width, const int height) {
  const auto scale = std::clamp(static_cast<float>(height) / 720.f, .75f, 2.6f);
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  VoiceSettingsLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(std::lround(26.f * scale));
  layout.body_font_pixels = static_cast<int>(std::lround(15.f * scale));
  layout.small_font_pixels = static_cast<int>(std::lround(12.f * scale));
  const auto panel_width = std::min(610.f * scale, w - 24.f * scale);
  const auto panel_height = std::min(448.f * scale, h - 24.f * scale);
  layout.panel = {(w - panel_width) * .5f, (h - panel_height) * .5f,
                  panel_width, panel_height};
  const auto inset = 26.f * scale;
  const auto inner = layout.panel.x + inset;
  const auto inner_width = panel_width - inset * 2.f;
  layout.title = {inner, layout.panel.y + 16.f * scale, inner_width,
                  30.f * scale};
  layout.hint = {inner, layout.title.y + layout.title.height, inner_width,
                 28.f * scale};
  auto row_y = layout.hint.y + layout.hint.height + 10.f * scale;
  const auto toggle_height = 30.f * scale;
  const auto box = 20.f * scale;
  for (int index = 0; index < 4; ++index) {
    layout.toggle_boxes.push_back(
        {inner, row_y + 4.f * scale, box, box});
    layout.toggle_labels.push_back({inner + box + 12.f * scale, row_y,
                                    inner_width - box - 12.f * scale,
                                    toggle_height});
    row_y += toggle_height;
  }
  row_y += 4.f * scale;
  const auto slider_height = 34.f * scale;
  const auto label_width = 190.f * scale;
  const auto value_width = 52.f * scale;
  for (int index = 0; index < 3; ++index) {
    layout.slider_labels.push_back(
        {inner, row_y + 5.f * scale, label_width, 26.f * scale});
    layout.slider_tracks.push_back(
        {inner + label_width + 10.f * scale, row_y,
         inner_width - label_width - value_width - 30.f * scale,
         28.f * scale});
    layout.slider_values.push_back(
        {inner + inner_width - value_width, row_y + 5.f * scale, value_width,
         26.f * scale});
    row_y += slider_height;
  }
  row_y += 2.f * scale;
  const auto choice_height = 32.f * scale;
  const auto choice_width = 150.f * scale;
  for (int index = 0; index < 2; ++index) {
    layout.choice_labels.push_back(
        {inner, row_y + 5.f * scale, label_width, 26.f * scale});
    layout.choice_buttons.push_back(
        {inner + inner_width - choice_width, row_y, choice_width,
         choice_height - 4.f * scale});
    row_y += choice_height;
  }
  const auto actions_y = layout.panel.y + panel_height - 56.f * scale;
  const auto button_height = 38.f * scale;
  layout.replay = {inner, actions_y, 226.f * scale, button_height};
  layout.stop = {layout.replay.x + layout.replay.width + 12.f * scale,
                 actions_y, 96.f * scale, button_height};
  layout.close = {inner + inner_width - 110.f * scale, actions_y,
                  110.f * scale, button_height};
  return layout;
}

void NativeVoiceSettingsView::open(const NativeVoiceSettings current) noexcept {
  values_ = current.sanitized();
  visible_ = true;
  dragging_ = -1;
}
void NativeVoiceSettingsView::close() noexcept {
  visible_ = false;
  dragging_ = -1;
}

bool *NativeVoiceSettingsView::toggle(const int index) noexcept {
  switch (index) {
  case 0: return &values_.enable_voices;
  case 1: return &values_.subtitles;
  case 2: return &values_.speaker_labels;
  case 3: return &values_.no_interruptions;
  default: return nullptr;
  }
}
float *NativeVoiceSettingsView::slider(const int index) noexcept {
  switch (index) {
  case 0: return &values_.volume;
  case 1: return &values_.opacity;
  case 2: return &values_.comms_intensity;
  default: return nullptr;
  }
}
void NativeVoiceSettingsView::cycle_choice(const int index) noexcept {
  if (index == 0) {
    const auto it = std::ranges::find(subtitle_sizes, values_.subtitle_size);
    const auto next =
        it == subtitle_sizes.end() || it + 1 == subtitle_sizes.end()
            ? subtitle_sizes.front()
            : *(it + 1);
    values_.subtitle_size = next;
  } else if (index == 1) {
    values_.frequency = static_cast<VoiceFrequency>(
        (static_cast<int>(values_.frequency) + 1) % 3);
    // Reference: choosing a frequency restores the chatter level.
    values_.chatter_level = 1.f;
  }
}

VoiceSettingsResult
NativeVoiceSettingsView::handle(const InputEvent &event, const int width,
                                const int height) {
  VoiceSettingsResult result{};
  if (!visible_) return result;
  result.captured = true;
  result.values = values_;
  pointer_ = event.position;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);

  if (event.type == InputEventType::EscapePressed) {
    result.command = VoiceSettingsCommand::Close;
    return result;
  }
  if (event.type == InputEventType::LeftPressed) {
    if (layout.close.contains(event.position)) {
      result.command = VoiceSettingsCommand::Close;
      return result;
    }
    if (layout.replay.contains(event.position)) {
      result.command = VoiceSettingsCommand::Replay;
      return result;
    }
    if (layout.stop.contains(event.position)) {
      result.command = VoiceSettingsCommand::Stop;
      return result;
    }
    for (int index = 0; index < 4; ++index) {
      if (!layout.toggle_boxes[index].contains(event.position) &&
          !layout.toggle_labels[index].contains(event.position))
        continue;
      *toggle(index) = !*toggle(index);
      result.values = values_;
      result.command = VoiceSettingsCommand::Apply;
      return result;
    }
    for (int index = 0; index < 2; ++index) {
      if (!layout.choice_buttons[index].contains(event.position)) continue;
      cycle_choice(index);
      result.values = values_;
      result.command = VoiceSettingsCommand::Apply;
      return result;
    }
    for (int index = 0; index < 3; ++index) {
      if (!layout.slider_tracks[index].contains(event.position)) continue;
      const auto &track = layout.slider_tracks[index];
      *slider(index) = std::clamp(
          (event.position.x - track.x) / std::max(1.f, track.width), 0.f, 1.f);
      dragging_ = index;
      result.values = values_;
      result.command = VoiceSettingsCommand::Apply;
      return result;
    }
    return result;
  }
  if (event.type == InputEventType::PointerMove) {
    if (dragging_ < 0) return result;
    const auto &track = layout.slider_tracks[dragging_];
    *slider(dragging_) = std::clamp(
        (event.position.x - track.x) / std::max(1.f, track.width), 0.f, 1.f);
    result.values = values_;
    result.command = VoiceSettingsCommand::Apply;
    return result;
  }
  if (event.type == InputEventType::LeftReleased ||
      event.type == InputEventType::PointerCancelled) {
    dragging_ = -1;
    return result;
  }
  return result;
}

void NativeVoiceSettingsView::render(DrawList &out, const int width,
                                     const int height) const {
  if (!visible_) return;
  const auto layout = VoiceSettingsLayout::for_viewport(width, height);
  fill(out, {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
       {4, 9, 18, 160});
  native_ui::panel(out, layout.panel, native_ui::Tone::Selected);
  text(out, {layout.title.x, layout.title.y}, "VOICE & SUBTITLES",
       text_primary, layout.title_font_pixels, TextAlign::Left,
       FontFace::Heading);
  text(out, {layout.hint.x, layout.hint.y},
       "Speech is generated locally. Subtitles remain available when speech "
       "is disabled.",
       text_muted, layout.small_font_pixels);
  const bool toggled[4] = {values_.enable_voices, values_.subtitles,
                           values_.speaker_labels, values_.no_interruptions};
  for (int index = 0; index < 4; ++index) {
    const auto &box = layout.toggle_boxes[index];
    fill(out, box, toggled[index] ? checked : track_color);
    stroke(out, box, border);
    if (toggled[index])
      text(out, {box.x + box.width * .5f, box.y + 2.f * layout.scale}, "X",
           {9, 20, 37, 255}, layout.small_font_pixels, TextAlign::Center);
    text(out, {layout.toggle_labels[index].x,
               layout.toggle_labels[index].y + 5.f * layout.scale},
         std::string(toggle_names[static_cast<std::size_t>(index)]), gold,
         layout.small_font_pixels);
  }
  const float levels[3] = {values_.volume, values_.opacity,
                           values_.comms_intensity};
  for (int index = 0; index < 3; ++index) {
    const auto &track = layout.slider_tracks[index];
    const auto value = std::clamp(levels[index], 0.f, 1.f);
    text(out,
         {layout.slider_labels[index].x, layout.slider_labels[index].y},
         std::string(slider_names[static_cast<std::size_t>(index)]), gold,
         layout.small_font_pixels);
    const UiRect bar{track.x, track.y + 10.f * layout.scale, track.width,
                     8.f * layout.scale};
    native_ui::progress(out, bar, value, native_ui::Tone::Selected);
    stroke(out, track, index == dragging_ ? text_primary
                                         : native_ui::color::keyline);
    text(out,
         {layout.slider_values[index].x + layout.slider_values[index].width,
          layout.slider_values[index].y},
         std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%",
         text_muted, layout.small_font_pixels, TextAlign::Right);
  }
  const auto draw_button = [&](UiRect bounds, std::string caption,
                               native_ui::Tone tone =
                                   native_ui::Tone::Selected) {
    native_ui::button(out, bounds, std::move(caption), pointer_,
                      layout.body_font_pixels, tone);
  };
  for (int index = 0; index < 2; ++index) {
    text(out,
         {layout.choice_labels[index].x, layout.choice_labels[index].y},
         std::string(choice_names[static_cast<std::size_t>(index)]), gold,
         layout.small_font_pixels);
    draw_button(layout.choice_buttons[index],
                index == 0
                    ? size_label(values_.subtitle_size)
                    : std::string(frequency_label(values_.frequency)));
  }
  draw_button(layout.replay, "REPLAY LAST ANNOUNCEMENT");
  draw_button(layout.stop, "STOP", native_ui::Tone::Caution);
  draw_button(layout.close, "CLOSE");
}

} // namespace stellar::native_voice_settings
