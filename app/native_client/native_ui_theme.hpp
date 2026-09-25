#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/accessibility.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace stellar::native_ui {

using native_map::Circle;
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::Image;
using native_map::FontFace;
using native_map::Line;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::TriangleMesh;
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

// Global high-contrast pass over a finished DrawList: snaps low-luminance
// text to the primary ink so every surface gains readability without
// per-screen palette plumbing. Colored accents above the threshold keep
// their semantic hue; dim labels/disabled text become legible.
inline void apply_high_contrast(DrawList &draw) {
  const auto boost = [](Color &c) {
    const float luminance = .299f * c.r + .587f * c.g + .114f * c.b;
    if (luminance < 160.f) c = color::text_primary;
  };
  for (auto &text : draw.text) boost(text.color);
  for (auto &command : draw.overlay)
    if (auto *text = std::get_if<Text>(&command)) boost(text->color);
  for (auto &command : draw.world)
    if (auto *text = std::get_if<Text>(&command)) boost(text->color);
}

// Every color-bearing field a draw-command variant can hold. Shared by the
// world and overlay variants; Scene3DView carries no CPU color to rewrite.
template <typename Command, typename Fn>
inline void for_each_command_color(Command &command, const Fn &fn) {
  std::visit([&](auto &value) {
    if constexpr (requires { value.color; }) fn(value.color);
    else if constexpr (requires { value.tint; }) fn(value.tint);
  }, command);
}

// Global color-blind pass over a finished DrawList: Machado et al. (2009)
// severity-1 simulation matrices measure the contrast the deficiency loses,
// then the standard error redistribution pushes it into the channels the
// mode still perceives (blue + luminance) — semantic accent hues stay
// distinguishable instead of merely being simulated away. Covers every
// CPU-side surface (text, primitives, image tints, mesh tints); GPU-rendered
// 3D scene content is out of scope until a post-process pass exists.
inline void apply_color_blind(DrawList &draw, engine::ColorBlindMode mode) {
  if (mode == engine::ColorBlindMode::None) return;
  const float *m;
  switch (mode) {
  case engine::ColorBlindMode::Protanopia: {
    static constexpr float matrix[]{.152286f,1.052583f,-.204868f,.114503f,.786281f,.099216f,-.003882f,-.048116f,1.051998f};
    m=matrix;break; }
  case engine::ColorBlindMode::Deuteranopia: {
    static constexpr float matrix[]{.367322f,.860646f,-.227968f,.280085f,.672501f,.047413f,-.011820f,.042940f,.968881f};
    m=matrix;break; }
  default: {
    static constexpr float matrix[]{1.255528f,-.076749f,-.178779f,-.078411f,.930809f,.147602f,.004733f,.691367f,.303900f};
    m=matrix;break; }
  }
  const auto daltonize = [&](Color &c) {
    const float r=static_cast<float>(c.r),g=static_cast<float>(c.g),b=static_cast<float>(c.b);
    const float eg=g-(m[3]*r+m[4]*g+m[5]*b),eb=b-(m[6]*r+m[7]*g+m[8]*b);
    const auto channel=[](float v){return static_cast<std::uint8_t>(std::clamp(std::lround(v),0l,255l));};
    c.r=channel(r+.7f*eg+.7f*eb);c.g=channel(g+eg+.7f*eb);c.b=channel(b+.7f*eg+eb);
  };
  for (auto &line : draw.lines) daltonize(line.color);
  for (auto &circle : draw.circles) daltonize(circle.color);
  for (auto &text : draw.text) daltonize(text.color);
  for (auto &command : draw.overlay) for_each_command_color(command,daltonize);
  for (auto &command : draw.world) for_each_command_color(command,daltonize);
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

// One keyboard/pad focus indicator — replaces the per-workspace hardcoded
// {160,210,255,255} stroked rects so the ring is identical on every surface.
inline void focus_ring(DrawList &out, UiRect bounds) {
  stroke(out, bounds, color::focus);
}

// Scannable statistic: muted micro-label over a larger tone-colored value.
// The primary Phase-3 hierarchy element for headline numbers.
inline void metric_tile(DrawList &out, UiRect bounds, std::string label,
                        std::string value, int label_pixels, int value_pixels,
                        Tone tone = Tone::Neutral, bool filled = true) {
  if (filled) {
    fill(out, bounds, color::surface_secondary);
    stroke(out, bounds, color::keyline);
  }
  const float pad = std::max(4.f, bounds.width * .06f);
  text(out, {bounds.x + pad, bounds.y + 5.f}, std::move(label),
       color::text_muted, label_pixels, bounds.width - 2.f * pad,
       TextAlign::Left, FontFace::Heading, bounds);
  text(out, {bounds.x + pad, bounds.y + bounds.height * .5f - 1.f},
       std::move(value),
       tone == Tone::Neutral ? color::text_primary : accent(tone),
       value_pixels, bounds.width - 2.f * pad, TextAlign::Left,
       FontFace::Heading, bounds);
}

// Compact status pill: tone-colored keyline + centered caps label.
inline void badge(DrawList &out, UiRect bounds, std::string label, int pixels,
                  Tone tone = Tone::Neutral) {
  fill(out, bounds, color::surface_secondary);
  stroke(out, bounds, accent(tone));
  text(out, {bounds.x, bounds.y + (bounds.height - pixels) * .5f - 1.f},
       std::move(label), accent(tone), pixels, bounds.width, TextAlign::Center,
       FontFace::Heading, bounds);
}

// Rectangle intersection shared by the clipped helpers below.
[[nodiscard]] inline std::optional<UiRect> clipped(UiRect a, UiRect b) {
  const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  const float r = std::min(a.x + a.width, b.x + b.width),
              bottom = std::min(a.y + a.height, b.y + b.height);
  if (r <= x || bottom <= y) return std::nullopt;
  return UiRect{x, y, r - x, bottom - y};
}

// Label left / value right row — the standard fact line.
inline void key_value(DrawList &out, UiRect bounds, std::string label,
                      std::string value, int pixels,
                      Tone tone = Tone::Neutral,
                      std::optional<UiRect> clip = std::nullopt) {
  const auto visible =
      clip ? clipped(bounds, *clip) : std::optional<UiRect>{bounds};
  if (!visible || visible->width <= 0.f || visible->height <= 0.f) return;
  text(out, {bounds.x, bounds.y}, std::move(label), color::text_secondary,
       pixels, bounds.width * .48f, TextAlign::Left, FontFace::Interface,
       visible);
  text(out, {bounds.x, bounds.y}, std::move(value),
       tone == Tone::Neutral ? color::text_primary : accent(tone), pixels,
       bounds.width * .52f, TextAlign::Right, FontFace::Interface, visible);
}

// Empty/error surface: centered muted message with an optional accent hint
// line underneath for the player's next action.
inline void empty_state(DrawList &out, UiRect bounds, std::string title,
                        std::string hint, int pixels) {
  text(out, {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .38f},
       std::move(title), color::text_secondary, pixels, bounds.width - 16.f,
       TextAlign::Center, FontFace::Heading, bounds);
  if (!hint.empty())
    text(out, {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .38f +
                                               pixels * 1.6f},
         std::move(hint), color::text_muted, std::max(9, pixels - 3),
         bounds.width - 16.f, TextAlign::Center, FontFace::Interface, bounds);
}

// Single tab inside a strip — caller owns the strip frame; active tabs get a
// tone underbar and primary text, inactive tabs stay muted.
inline void tab(DrawList &out, UiRect bounds, std::string caption,
                Point pointer, int pixels, bool active,
                bool enabled = true) {
  const bool hovered = enabled && bounds.contains(pointer);
  if (active || hovered)
    fill(out, bounds, active ? color::surface_raised : color::surface_hover);
  if (active)
    fill(out, {bounds.x, bounds.y + bounds.height - 2.f, bounds.width, 2.f},
         color::selected);
  text(out, {bounds.x + bounds.width * .5f,
             bounds.y + (bounds.height - pixels) * .5f - 1.f},
       std::move(caption),
       !enabled ? color::disabled
                : active ? color::text_primary
                         : hovered ? color::text_primary : color::text_secondary,
       pixels, bounds.width, TextAlign::Center, FontFace::Heading, bounds);
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
