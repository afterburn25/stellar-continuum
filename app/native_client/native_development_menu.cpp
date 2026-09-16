#include "native_development_menu.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>

namespace stellar::native_development {

using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

namespace {

constexpr Color kPanel{10, 16, 28, 242}, kPanelEdge{59, 83, 118, 255};
constexpr Color kAccent{245, 197, 106, 255}, kText{233, 242, 252, 255};
constexpr Color kTextDim{148, 163, 184, 255}, kWarn{245, 197, 106, 255};
constexpr Color kButton{17, 27, 47, 240}, kButtonEdge{59, 83, 118, 255};
constexpr Color kButtonHover{24, 46, 70, 255}, kInput{7, 12, 22, 245};
constexpr Color kShade{0, 0, 0, 115};

void fill(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(FilledRectangle{rect, color});
}
void stroke(DrawList &out, UiRect rect, Color color) {
  out.overlay.emplace_back(StrokedRectangle{rect, color});
}
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          float wrap = 0.f, TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, std::nullopt, align});
}

[[nodiscard]] bool parse_seed(const std::string &text, std::int64_t &seed) {
  std::string trimmed;
  trimmed.reserve(text.size());
  for (const char c : text)
    if (c == ' ' || c == '_' || c == '-')
      continue;
    else
      trimmed.push_back(c);
  if (trimmed.empty()) return false;
  const char *first = trimmed.data();
  const char *last = first + trimmed.size();
  const auto result = std::from_chars(first, last, seed);
  return result.ec == std::errc{} && result.ptr == last;
}

}  // namespace

DevelopmentMenuLayout development_menu_layout_for(int width, int height) {
  DevelopmentMenuLayout layout;
  const float scale =
      std::clamp(std::min(width / 1280.f, height / 720.f), 0.6f, 2.0f);
  layout.scale = scale;
  layout.heading_font_pixels =
      std::max(11, static_cast<int>(std::lround(14.f * scale)));
  layout.body_font_pixels =
      std::max(10, static_cast<int>(std::lround(12.f * scale)));
  layout.small_font_pixels =
      std::max(9, static_cast<int>(std::lround(10.f * scale)));

  const float panel_w = std::clamp(340.f * scale, 280.f, width * 0.92f);
  const float pad = 14.f * scale;
  const float row_h = 26.f * scale;
  const float gap = 6.f * scale;
  float y = pad;
  const float inner_x = pad, inner_w = panel_w - 2.f * pad;

  UiRect header{inner_x, y, inner_w, 20.f * scale};
  y += header.height + gap;
  UiRect open_button{inner_x, y, inner_w, row_h};
  y += row_h + gap;
  UiRect seed_label{inner_x, y, inner_w, 16.f * scale};
  y += seed_label.height + 2.f * scale;
  UiRect seed_input{inner_x, y, inner_w, row_h};
  y += row_h + gap;
  UiRect new_button{inner_x, y, inner_w, row_h};
  y += row_h + gap;
  UiRect tools_button{inner_x, y, inner_w, row_h};
  y += row_h + gap;
  UiRect status{inner_x, y, inner_w, 30.f * scale};
  y += status.height + gap;
  UiRect back_button{inner_x, y, inner_w, row_h};
  y += row_h + pad;

  const float panel_h = y;
  const float x0 = (width - panel_w) * 0.5f;
  const float y0 = (height - panel_h) * 0.5f;
  layout.panel = {x0, y0, panel_w, panel_h};
  auto place = [&](UiRect &r) {
    r.x += x0;
    r.y += y0;
  };
  place(header);
  place(open_button);
  place(seed_label);
  place(seed_input);
  place(new_button);
  place(tools_button);
  place(status);
  place(back_button);
  layout.header = header;
  layout.open_button = open_button;
  layout.seed_label = seed_label;
  layout.seed_input = seed_input;
  layout.new_button = new_button;
  layout.tools_button = tools_button;
  layout.back_button = back_button;
  layout.status_text = status;
  return layout;
}

void NativeDevelopmentMenu::open() {
  visible_ = true;
  seed_focused_ = false;
  confirm_armed_ = false;
  invalid_seed_ = false;
  pressed_ = false;
}

void NativeDevelopmentMenu::close() noexcept {
  visible_ = false;
  seed_focused_ = false;
  confirm_armed_ = false;
  invalid_seed_ = false;
  pressed_ = false;
}

DevelopmentMenuCommand
NativeDevelopmentMenu::handle(const native_map::InputEvent &event, int width,
                              int height) {
  using K = DevelopmentMenuCommandKind;
  if (!visible_) return {};
  const auto layout = development_menu_layout_for(width, height);

  if (event.type == native_map::InputEventType::EscapePressed) {
    close();
    return {K::Back, true};
  }
  if (event.type == native_map::InputEventType::BackspacePressed &&
      seed_focused_ && !seed_text_.empty()) {
    seed_text_.pop_back();
    confirm_armed_ = false;
    invalid_seed_ = false;
    return {K::None, true};
  }
  if (event.type == native_map::InputEventType::TextEntered &&
      seed_focused_) {
    if (seed_text_.size() + event.text.size() <= 40)
      seed_text_ += event.text;
    confirm_armed_ = false;
    invalid_seed_ = false;
    return {K::None, true};
  }
  if (event.type == native_map::InputEventType::PointerCancelled) {
    pressed_ = false;
    return {K::None, true};
  }
  if (event.type == native_map::InputEventType::LeftPressed) {
    pressed_ = layout.panel.contains(event.position);
    return {K::None, true};
  }
  if (event.type != native_map::InputEventType::LeftReleased)
    return {K::None, true};

  // The submenu replaces the menu body; clicks outside the panel are
  // swallowed rather than leaking to the hidden rows behind it. A release
  // only acts when its press landed inside the panel — the click that opened
  // the submenu must not fire a button on its release.
  const bool armed = pressed_;
  pressed_ = false;
  if (!armed || !layout.panel.contains(event.position))
    return {K::None, true};

  seed_focused_ = layout.seed_input.contains(event.position);
  if (layout.back_button.contains(event.position)) {
    close();
    return {K::Back, true};
  }
  if (layout.open_button.contains(event.position))
    return {K::OpenDeveloper, true};
  if (layout.tools_button.contains(event.position))
    return {K::OpenTools, true};
  if (layout.new_button.contains(event.position)) {
    std::int64_t seed{};
    if (!parse_seed(seed_text_, seed)) {
      invalid_seed_ = true;
      confirm_armed_ = false;
      return {K::None, true};
    }
    if (!confirm_armed_) {
      confirm_armed_ = true;
      invalid_seed_ = false;
      return {K::None, true};
    }
    close();
    return {K::NewDeveloperCampaign, true, seed};
  }
  return {K::None, true};
}

void NativeDevelopmentMenu::render(native_map::DrawList &out,
                                   const DevelopmentMenuView &view, int width,
                                   int height, const Point *pointer) const {
  if (!visible_) return;
  const auto layout = development_menu_layout_for(width, height);
  fill(out,
       {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)},
       kShade);
  fill(out, layout.panel, kPanel);
  stroke(out, layout.panel, kPanelEdge);

  const float text_x = layout.panel.x + 14.f * layout.scale;
  const auto button = [&](const UiRect &r, const std::string &label,
                          bool enabled, bool warn = false) {
    const bool hover = enabled && pointer && r.contains(*pointer);
    fill(out, r, hover ? kButtonHover : kButton);
    stroke(out, r, warn ? kWarn : kButtonEdge);
    const float tx = r.x + 12.f * layout.scale;
    const float ty = r.y + (r.height - layout.body_font_pixels) * 0.5f;
    text(out, {tx, ty}, label, enabled ? (warn ? kWarn : kText) : kTextDim,
         layout.body_font_pixels);
  };

  text(out, {text_x, layout.header.y + 2.f * layout.scale}, "DEVELOPMENT",
       kAccent, layout.heading_font_pixels);

  const std::string open_label =
      view.developer_mode ? "LOAD DEVELOPER CAMPAIGN"
                          : (view.has_developer_save ? "OPEN DEVELOPER SAVE"
                                                     : "NEW DEVELOPER MODE");
  button(layout.open_button, open_label, true);

  text(out, {text_x, layout.seed_label.y}, "WORLD SEED", kTextDim,
       layout.small_font_pixels);
  const bool input_hover = pointer && layout.seed_input.contains(*pointer);
  fill(out, layout.seed_input, kInput);
  stroke(out, layout.seed_input,
         seed_focused_ || input_hover ? kAccent : kButtonEdge);
  const float seed_ty =
      layout.seed_input.y +
      (layout.seed_input.height - layout.body_font_pixels) * 0.5f;
  text(out, {layout.seed_input.x + 10.f * layout.scale, seed_ty}, seed_text_,
       kText, layout.body_font_pixels);
  if (seed_focused_) {
    const float cursor_x =
        layout.seed_input.x + 10.f * layout.scale +
        static_cast<float>(seed_text_.size()) * layout.body_font_pixels *
            0.55f;
    fill(out,
         {cursor_x, seed_ty, 1.5f * layout.scale,
          static_cast<float>(layout.body_font_pixels)},
         kAccent);
  }

  const std::string new_label =
      confirm_armed_ ? "CONFIRM NEW CAMPAIGN" : "NEW DEVELOPER CAMPAIGN";
  button(layout.new_button, new_label, true, confirm_armed_);
  button(layout.tools_button, "DEVELOPER TOOLS", view.developer_mode);
  button(layout.back_button, "BACK", true);

  std::string status = view.status;
  if (invalid_seed_)
    status = "ENTER A NUMERIC WORLD SEED";
  else if (confirm_armed_)
    status = view.has_developer_save
                 ? "REPLACES THE DEVELOPER SAVE; THE PREVIOUS SAVE IS KEPT AS "
                   "ITS BACKUP"
                 : "CREATES A FRESH SEEDED DEVELOPER WORLD";
  if (!status.empty())
    text(out, {text_x, layout.status_text.y}, status,
         invalid_seed_ || confirm_armed_ ? kWarn : kTextDim,
         layout.small_font_pixels, layout.status_text.width);
}

}  // namespace stellar::native_development
