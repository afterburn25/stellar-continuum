#include "native_audio_settings.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace stellar::native_audio_settings {
namespace {
using namespace stellar::native_map;

// Reference palette (VisualPalette / MainMenuLayer).
constexpr Color text_primary = native_ui::color::text_primary;
constexpr Color text_muted = native_ui::color::text_secondary;
constexpr Color gold = native_ui::color::selected;

constexpr std::string_view slider_names[3] = {"MASTER", "MUSIC",
                                              "SOUND EFFECTS"};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, Point at, std::string value, Color color,
          int pixels, TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, 0.f, std::nullopt, align,
           face});
}
} // namespace

AudioSettingsLayout
AudioSettingsLayout::for_viewport(const int width, const int height) {
  const auto scale = std::clamp(static_cast<float>(height) / 720.f, .75f, 2.6f);
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  AudioSettingsLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(std::lround(26.f * scale));
  layout.body_font_pixels = static_cast<int>(std::lround(15.f * scale));
  layout.small_font_pixels = static_cast<int>(std::lround(12.f * scale));
  const auto panel_width = std::min(560.f * scale, w - 24.f * scale);
  const auto panel_height = 348.f * scale;
  layout.panel = {(w - panel_width) * .5f, (h - panel_height) * .5f,
                  panel_width, panel_height};
  const auto inset = 26.f * scale;
  const auto inner = layout.panel.x + inset;
  const auto inner_width = panel_width - inset * 2.f;
  layout.title = {inner, layout.panel.y + 18.f * scale, inner_width,
                  34.f * scale};
  layout.hint = {inner, layout.title.y + layout.title.height, inner_width,
                 20.f * scale};
  const auto row_height = 44.f * scale;
  const auto label_width = 132.f * scale;
  const auto value_width = 52.f * scale;
  for (int index = 0; index < 3; ++index) {
    const auto row_y =
        layout.hint.y + layout.hint.height + 16.f * scale +
        static_cast<float>(index) * row_height;
    layout.labels.push_back(
        {inner, row_y + 9.f * scale, label_width, 26.f * scale});
    layout.tracks.push_back({inner + label_width + 10.f * scale, row_y,
                             inner_width - label_width - value_width -
                                 30.f * scale,
                             34.f * scale});
    layout.values.push_back(
        {inner + inner_width - value_width, row_y + 9.f * scale, value_width,
         26.f * scale});
  }
  const auto actions_y = layout.tracks.back().y + row_height + 18.f * scale;
  const auto button_height = 38.f * scale;
  layout.done = {inner, actions_y, 150.f * scale, button_height};
  layout.defaults = {inner + inner_width - 196.f * scale, actions_y,
                     196.f * scale, button_height};
  return layout;
}

void NativeAudioSettingsView::open(const NativeAudioSettings current) noexcept {
  values_ = current;
  visible_ = true;
  dragging_ = -1;
}
void NativeAudioSettingsView::close() noexcept {
  visible_ = false;
  dragging_ = -1;
}

float *NativeAudioSettingsView::slider(const int index) noexcept {
  switch (index) {
  case 0: return &values_.master;
  case 1: return &values_.music;
  case 2: return &values_.sfx;
  default: return nullptr;
  }
}

AudioSettingsResult
NativeAudioSettingsView::handle(const InputEvent &event, const int width,
                                const int height) {
  AudioSettingsResult result{};
  if (!visible_) return result;
  result.captured = true;
  result.values = values_;
  pointer_ = event.position;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);

  if (event.type == InputEventType::EscapePressed) {
    result.command = AudioSettingsCommand::Close;
    return result;
  }
  if (event.type == InputEventType::LeftPressed) {
    if (layout.done.contains(event.position)) {
      result.command = AudioSettingsCommand::Close;
      return result;
    }
    if (layout.defaults.contains(event.position)) {
      values_ = NativeAudioSettings{};
      result.values = values_;
      result.command = AudioSettingsCommand::Apply;
      return result;
    }
    for (int index = 0; index < 3; ++index) {
      if (!layout.tracks[index].contains(event.position)) continue;
      const auto &track = layout.tracks[index];
      *slider(index) = std::clamp(
          (event.position.x - track.x) / std::max(1.f, track.width), 0.f, 1.f);
      dragging_ = index;
      result.values = values_;
      result.command = AudioSettingsCommand::Apply;
      return result;
    }
    return result;
  }
  if (event.type == InputEventType::PointerMove) {
    if (dragging_ < 0) return result;
    const auto &track = layout.tracks[dragging_];
    *slider(dragging_) = std::clamp(
        (event.position.x - track.x) / std::max(1.f, track.width), 0.f, 1.f);
    result.values = values_;
    result.command = AudioSettingsCommand::Apply;
    return result;
  }
  if (event.type == InputEventType::LeftReleased ||
      event.type == InputEventType::PointerCancelled) {
    dragging_ = -1;
    return result;
  }
  return result;
}

void NativeAudioSettingsView::render(DrawList &out, const int width,
                                     const int height) const {
  if (!visible_) return;
  const auto layout = AudioSettingsLayout::for_viewport(width, height);
  fill(out, {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
       {4, 9, 18, 160});
  native_ui::panel(out, layout.panel, native_ui::Tone::Selected);
  text(out, {layout.title.x, layout.title.y}, "AUDIO", text_primary,
       layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
  text(out, {layout.hint.x, layout.hint.y},
       "Balance the score and interface feedback for your play space.",
       text_muted, layout.small_font_pixels);
  const float *const levels[3] = {&values_.master, &values_.music,
                                  &values_.sfx};
  for (int index = 0; index < 3; ++index) {
    const auto &track = layout.tracks[index];
    const auto value = std::clamp(*levels[index], 0.f, 1.f);
    text(out, {layout.labels[index].x, layout.labels[index].y},
         std::string(slider_names[index]), gold, layout.small_font_pixels);
    const UiRect bar{track.x, track.y + 13.f * layout.scale, track.width,
                     8.f * layout.scale};
    native_ui::progress(out, bar, value, native_ui::Tone::Selected);
    stroke(out, track, index == dragging_ ? text_primary
                                         : native_ui::color::keyline);
    text(out,
         {layout.values[index].x + layout.values[index].width,
          layout.values[index].y},
         std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%",
         text_muted, layout.small_font_pixels, TextAlign::Right);
  }
  const auto draw_button = [&](UiRect bounds, std::string caption) {
    native_ui::button(out, bounds, std::move(caption), pointer_,
                      layout.body_font_pixels, native_ui::Tone::Selected);
  };
  draw_button(layout.done, "DONE");
  draw_button(layout.defaults, "RESTORE DEFAULTS");
}

} // namespace stellar::native_audio_settings
