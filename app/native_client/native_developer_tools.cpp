#include "native_developer_tools.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace stellar::native_developer {

namespace {

using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::InputEventType;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

constexpr Color panel_color = native_ui::color::surface;
constexpr Color border_color = native_ui::color::keyline_strong;
constexpr Color tile_color = native_ui::color::surface_secondary;
constexpr Color title_color = native_ui::color::text_primary;
constexpr Color muted_color = native_ui::color::text_secondary;
constexpr Color accent_color = native_ui::color::success;
constexpr Color gold_color = native_ui::color::caution;
constexpr Color error_color = native_ui::color::danger;
constexpr Color button_color = native_ui::color::surface_secondary;
constexpr Color hover_color = native_ui::color::surface_hover;

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

}  // namespace

DeveloperToolsLayout developer_tools_layout_for(const int width,
                                                const int height) {
  const auto sw = static_cast<float>(width), sh = static_cast<float>(height);
  const auto scale = std::max(1.f, sh / 900.f);
  DeveloperToolsLayout layout;
  layout.scale = scale;
  layout.heading_font_pixels = std::max(11, static_cast<int>(21.f * scale));
  layout.body_font_pixels = std::max(9, static_cast<int>(13.f * scale));
  layout.small_font_pixels = std::max(8, static_cast<int>(11.f * scale));
  // Centered panel like the reference DeveloperToolsLayer CenterContainer.
  const auto panel_width = std::min(520.f * scale, sw - 48.f * scale);
  const auto panel_height = std::min(620.f * scale, sh - 48.f * scale);
  layout.panel = {sw * .5f - panel_width * .5f, sh * .5f - panel_height * .5f,
                  panel_width, panel_height};
  const auto pad = 14.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 44.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  layout.mode_text = {layout.panel.x + pad,
                      layout.header.y + layout.header.height + 6.f * scale,
                      layout.panel.width - pad * 2.f, 20.f * scale};
  const auto row_height = 62.f * scale;
  const auto row_gap = 8.f * scale;
  auto cursor = layout.mode_text.y + layout.mode_text.height + 10.f * scale;
  for (int i = 0; i < 6; ++i) {
    layout.command_rows[i] = {layout.panel.x + pad, cursor,
                              layout.panel.width - pad * 2.f, row_height};
    cursor += row_height + row_gap;
  }
  layout.result_text = {layout.panel.x + pad,
                        layout.panel.y + layout.panel.height - pad -
                            56.f * scale,
                        layout.panel.width - pad * 2.f, 56.f * scale};
  return layout;
}

DeveloperToolsCommand
NativeDeveloperToolsPanel::handle(const native_map::InputEvent &event,
                                  const int width, const int height) {
  DeveloperToolsCommand command;
  if (!visible_) return command;
  const auto layout = developer_tools_layout_for(width, height);
  if (event.type == InputEventType::LeftReleased &&
      layout.close_button.contains(event.position)) {
    close();
    command.kind = DeveloperToolsCommandKind::Close;
    command.captured = true;
    return command;
  }
  if (event.type == InputEventType::LeftReleased) {
    const auto commands = core::developer_command_catalog();
    for (std::size_t i = 0; i < layout.command_rows.size() && i < commands.size();
         ++i)
      if (layout.command_rows[i].contains(event.position)) {
        command.kind = DeveloperToolsCommandKind::Run;
        command.command_id = commands[i].id;
        command.captured = true;
        return command;
      }
  }
  if ((event.type == InputEventType::LeftPressed ||
       event.type == InputEventType::RightPressed ||
       event.type == InputEventType::LeftReleased ||
       event.type == InputEventType::RightReleased ||
       event.type == InputEventType::Wheel) &&
      layout.panel.contains(event.position))
    command.captured = true;
  return command;
}

void NativeDeveloperToolsPanel::render(DrawList &out,
                                       const NativeDeveloperToolsView &view,
                                       const int width, const int height,
                                       const Point *pointer) const {
  if (!visible_) return;
  const auto layout = developer_tools_layout_for(width, height);
  const auto scale = layout.scale;
  fill(out, layout.panel, panel_color);
  stroke(out, layout.panel, border_color);

  text(out, {layout.header.x, layout.header.y}, "DEVELOPER TOOLS", gold_color,
       layout.heading_font_pixels);
  text(out, {layout.header.x, layout.header.y + 26.f * scale},
       "Authorized test operations in a separate world", muted_color,
       layout.small_font_pixels);
  fill(out, layout.close_button, button_color);
  text(out, {layout.close_button.x + 8.f * scale,
             layout.close_button.y + 4.f * scale},
       "X", muted_color, layout.body_font_pixels + 1);

  // Reference Refresh: "DEVELOPER MODE · TOOLS USED · DEVELOPER CAMPAIGN".
  text(out, {layout.mode_text.x, layout.mode_text.y},
       view.tools_used ? "DEVELOPER MODE  ·  TOOLS USED · DEVELOPER CAMPAIGN"
                       : "DEVELOPER MODE  ·  TOOLS UNUSED",
       view.tools_used ? gold_color : accent_color, layout.body_font_pixels);

  const auto commands = core::developer_command_catalog();
  for (std::size_t i = 0; i < layout.command_rows.size() && i < commands.size();
       ++i) {
    const auto &rect = layout.command_rows[i];
    const bool hovered =
        pointer && rect.contains(*pointer) && view.developer;
    fill(out, rect, hovered ? hover_color : tile_color);
    stroke(out, rect, border_color);
    text(out, {rect.x + 10.f * scale, rect.y + 8.f * scale},
         std::string{commands[i].title}, title_color,
         layout.body_font_pixels);
    text(out, {rect.x + 10.f * scale, rect.y + 26.f * scale},
         std::string{commands[i].description}, muted_color,
         layout.small_font_pixels, rect.width - 20.f * scale);
  }

  if (!view.result.empty())
    text(out, {layout.result_text.x, layout.result_text.y}, view.result,
         view.result_accepted ? accent_color : error_color,
         layout.small_font_pixels, layout.result_text.width);
}

}  // namespace stellar::native_developer
