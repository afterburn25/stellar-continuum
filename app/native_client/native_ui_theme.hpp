#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace stellar::native_ui {

using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::FontFace;
using native_map::Line;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

namespace color {
inline constexpr Color canvas{5, 11, 18, 255};
inline constexpr Color surface{7, 19, 31, 248};
inline constexpr Color surface_opaque{7, 19, 31, 255};
inline constexpr Color surface_secondary{13, 29, 43, 248};
inline constexpr Color surface_raised{19, 40, 58, 252};
inline constexpr Color surface_hover{25, 55, 78, 252};
inline constexpr Color keyline{39, 67, 89, 255};
inline constexpr Color keyline_strong{73, 126, 164, 255};
inline constexpr Color text_primary{230, 240, 246, 255};
inline constexpr Color text_secondary{169, 187, 200, 255};
inline constexpr Color text_muted{111, 132, 148, 255};
inline constexpr Color selected{88, 207, 251, 255};
inline constexpr Color focus{147, 226, 255, 255};
inline constexpr Color success{100, 214, 165, 255};
inline constexpr Color caution{235, 203, 103, 255};
inline constexpr Color danger{255, 113, 108, 255};
inline constexpr Color unknown{139, 130, 162, 255};
inline constexpr Color disabled{82, 101, 116, 255};
inline constexpr Color science{175, 143, 255, 255};
inline constexpr Color economy{233, 182, 92, 255};
inline constexpr Color construction{241, 151, 91, 255};
inline constexpr Color diplomacy{95, 210, 192, 255};
inline constexpr Color military{255, 119, 110, 255};
inline constexpr Color shadow{0, 4, 9, 168};
}

enum class Tone { Neutral, Selected, Success, Caution, Danger, Science, Economy, Construction, Diplomacy, Military, Unknown };

[[nodiscard]] inline constexpr Color accent(Tone tone) noexcept {
  switch (tone) {
  case Tone::Selected: return color::selected;
  case Tone::Success: return color::success;
  case Tone::Caution: return color::caution;
  case Tone::Danger: return color::danger;
  case Tone::Science: return color::science;
  case Tone::Economy: return color::economy;
  case Tone::Construction: return color::construction;
  case Tone::Diplomacy: return color::diplomacy;
  case Tone::Military: return color::military;
  case Tone::Unknown: return color::unknown;
  case Tone::Neutral: return color::keyline_strong;
  }
  return color::keyline_strong;
}

inline void fill(DrawList &out, UiRect bounds, Color value) {
  out.overlay.emplace_back(FilledRectangle{bounds, value});
}

inline void stroke(DrawList &out, UiRect bounds, Color value = color::keyline) {
  out.overlay.emplace_back(StrokedRectangle{bounds, value});
}

inline void text(DrawList &out, Point at, std::string value,
                 Color value_color, int pixels, float wrap = 0.f,
                 TextAlign align = TextAlign::Left,
                 FontFace face = FontFace::Interface,
                 std::optional<UiRect> clip = std::nullopt) {
  out.overlay.emplace_back(Text{at, std::move(value), value_color, pixels, wrap,
                                clip, align, face});
}

inline void panel(DrawList &out, UiRect bounds, Tone tone = Tone::Neutral,
                  bool raised = false) {
  fill(out, {bounds.x + 4.f, bounds.y + 5.f, bounds.width, bounds.height},
       color::shadow);
  fill(out, bounds, raised ? color::surface_secondary : color::surface);
  stroke(out, bounds, color::keyline);
  fill(out, {bounds.x, bounds.y, 3.f, bounds.height}, accent(tone));
}

inline void section_header(DrawList &out, UiRect bounds, std::string title,
                           int pixels, Tone tone = Tone::Neutral,
                           std::string value = {}) {
  text(out, {bounds.x, bounds.y}, std::move(title), accent(tone), pixels,
       bounds.width, TextAlign::Left, FontFace::Heading);
  if (!value.empty())
    text(out, {bounds.x, bounds.y}, std::move(value), color::text_secondary,
         pixels, bounds.width, TextAlign::Right);
  fill(out, {bounds.x, bounds.y + static_cast<float>(pixels) + 5.f,
             bounds.width, 1.f}, color::keyline);
}

inline void button(DrawList &out, UiRect bounds, std::string caption,
                   Point pointer, int pixels, Tone tone = Tone::Neutral,
                   bool active = false, bool enabled = true) {
  const bool hovered = enabled && bounds.contains(pointer);
  fill(out, bounds, active ? color::surface_raised
                           : hovered ? color::surface_hover
                                     : color::surface_secondary);
  stroke(out, bounds, enabled ? (active || hovered ? accent(tone)
                                                    : color::keyline_strong)
                              : color::keyline);
  if (active) fill(out, {bounds.x, bounds.y, 3.f, bounds.height}, accent(tone));
  text(out, {bounds.x, bounds.y + (bounds.height - pixels) * .5f - 1.f},
       std::move(caption), enabled ? color::text_primary : color::disabled,
       pixels, bounds.width, TextAlign::Center, FontFace::Heading, bounds);
}

inline void progress(DrawList &out, UiRect bounds, double ratio,
                     Tone tone = Tone::Selected) {
  fill(out, bounds, color::canvas);
  const auto width = bounds.width * static_cast<float>(std::clamp(ratio, 0., 1.));
  if (width > 0.f) fill(out, {bounds.x, bounds.y, width, bounds.height}, accent(tone));
  stroke(out, bounds, color::keyline);
}

inline void tooltip(DrawList &out, Point anchor, std::string title,
                    std::string body, int viewport_width, int viewport_height,
                    float scale = 1.f, Tone tone = Tone::Neutral) {
  const float width = std::min(320.f * scale,
                               static_cast<float>(viewport_width) - 24.f);
  const float height = 76.f * scale;
  const float x = std::clamp(anchor.x, 12.f,
                             static_cast<float>(viewport_width) - width - 12.f);
  const float y = std::clamp(anchor.y, 12.f,
                             static_cast<float>(viewport_height) - height - 12.f);
  const UiRect bounds{x, y, width, height};
  panel(out, bounds, tone, true);
  text(out, {x + 12.f * scale, y + 9.f * scale}, std::move(title),
       accent(tone), static_cast<int>(13.f * scale), width - 24.f * scale,
       TextAlign::Left, FontFace::Heading);
  text(out, {x + 12.f * scale, y + 31.f * scale}, std::move(body),
       color::text_secondary, static_cast<int>(12.f * scale),
       width - 24.f * scale);
}

}
