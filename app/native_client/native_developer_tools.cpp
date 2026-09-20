#include "native_developer_tools.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
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
constexpr Color tab_active_color = native_ui::color::selected;

// Panel chrome strings live in a localization table (req 25): the embedded
// English catalog is the default; load_locale_* can layer other locales on
// top. Command row titles/descriptions come from the parity-locked
// developer_command_catalog and stay untouched.
constexpr std::string_view kEnglishCatalog = R"json({
  "locale": "en",
  "fallback": "en",
  "strings": {
    "DEVTOOLS_TITLE": "DEVELOPER TOOLS",
    "DEVTOOLS_SUBTITLE": "Authorized test operations in a separate world",
    "DEVTOOLS_CLOSE": "X",
    "DEVTOOLS_MODE_USED": "DEVELOPER MODE  \u00b7  TOOLS USED \u00b7 DEVELOPER CAMPAIGN",
    "DEVTOOLS_MODE_UNUSED": "DEVELOPER MODE  \u00b7  TOOLS UNUSED",
    "DEVTOOLS_TAB_COMMANDS": "COMMANDS",
    "DEVTOOLS_TAB_DIAGNOSTICS": "DIAGNOSTICS",
    "DEVTOOLS_TAB_SAVES": "SAVES",
    "DEVTOOLS_EMPTY_DIAGNOSTICS": "No diagnostics recorded yet.",
    "DEVTOOLS_EMPTY_SAVES": "No save file has been written for this campaign yet."
  }
})json";

constexpr std::array<const char *, developer_tools_tab_count> tab_keys = {
    "DEVTOOLS_TAB_COMMANDS", "DEVTOOLS_TAB_DIAGNOSTICS", "DEVTOOLS_TAB_SAVES"};

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

Color save_status_color(int status) {
  switch (status) {
  case 0: return accent_color;
  case 1: return title_color;
  case 2: return muted_color;
  case 3: return error_color;
  default: return muted_color;
  }
}

}  // namespace

NativeDeveloperToolsPanel::NativeDeveloperToolsPanel() {
  engine::LocalizationTable table{"en", "en"};
  (void)table.load_json(kEnglishCatalog);
  localization_.add_table(std::move(table));
}

bool NativeDeveloperToolsPanel::load_locale_document(std::string_view json,
                                                     std::string *error) {
  engine::LocalizationTable table{localization_.locale(),
                                  localization_.fallback_locale()};
  if (!table.load_json(json, error)) return false;
  localization_.add_table(std::move(table));
  return true;
}

bool NativeDeveloperToolsPanel::load_locale_file(const std::string &path,
                                                 std::string *error) {
  return localization_.load_file(path, error);
}

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
  const auto panel_width = std::min(560.f * scale, sw - 48.f * scale);
  const auto panel_height = std::min(660.f * scale, sh - 48.f * scale);
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

  // Tab strip between the mode line and the content region.
  const auto tab_y =
      layout.mode_text.y + layout.mode_text.height + 8.f * scale;
  const auto tab_height = 26.f * scale;
  const auto tab_gap = 6.f * scale;
  const auto tab_width =
      (layout.panel.width - pad * 2.f -
       tab_gap * (developer_tools_tab_count - 1)) /
      developer_tools_tab_count;
  for (int i = 0; i < developer_tools_tab_count; ++i)
    layout.tabs[i] = {layout.panel.x + pad + i * (tab_width + tab_gap), tab_y,
                      tab_width, tab_height};

  layout.content = {layout.panel.x + pad, tab_y + tab_height + 8.f * scale,
                    layout.panel.width - pad * 2.f, 0.f};
  layout.result_text = {layout.panel.x + pad,
                        layout.panel.y + layout.panel.height - pad -
                            56.f * scale,
                        layout.panel.width - pad * 2.f, 56.f * scale};
  layout.content.height =
      layout.result_text.y - layout.content.y - 8.f * scale;

  const auto row_height = 58.f * scale;
  const auto row_gap = 8.f * scale;
  auto cursor = layout.content.y;
  for (int i = 0; i < 6; ++i) {
    layout.command_rows[i] = {layout.content.x, cursor,
                              layout.content.width, row_height};
    cursor += row_height + row_gap;
  }
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
    for (int i = 0; i < developer_tools_tab_count; ++i)
      if (layout.tabs[i].contains(event.position)) {
        active_tab_ = static_cast<DeveloperToolsTab>(i);
        command.captured = true;
        return command;
      }
  }
  if (event.type == InputEventType::LeftReleased &&
      active_tab_ == DeveloperToolsTab::Commands) {
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

  text(out, {layout.header.x, layout.header.y}, tr("DEVTOOLS_TITLE"),
       gold_color, layout.heading_font_pixels);
  text(out, {layout.header.x, layout.header.y + 26.f * scale},
       tr("DEVTOOLS_SUBTITLE"), muted_color, layout.small_font_pixels);
  fill(out, layout.close_button, button_color);
  text(out, {layout.close_button.x + 8.f * scale,
             layout.close_button.y + 4.f * scale},
       tr("DEVTOOLS_CLOSE"), muted_color, layout.body_font_pixels + 1);

  // Reference Refresh: "DEVELOPER MODE · TOOLS USED · DEVELOPER CAMPAIGN".
  text(out, {layout.mode_text.x, layout.mode_text.y},
       tr(view.tools_used ? "DEVTOOLS_MODE_USED" : "DEVTOOLS_MODE_UNUSED"),
       view.tools_used ? gold_color : accent_color, layout.body_font_pixels);

  // Tab strip.
  for (int i = 0; i < developer_tools_tab_count; ++i) {
    const auto &rect = layout.tabs[i];
    const bool active = static_cast<int>(active_tab_) == i;
    const bool hovered = pointer && rect.contains(*pointer);
    fill(out, rect, active ? hover_color : tile_color);
    stroke(out, rect, active ? tab_active_color : border_color);
    const auto label_pixels = layout.small_font_pixels;
    // Center the label by rough width estimate (TextAlign::Center not needed
    // for the strip — left pad keeps it consistent at any scale).
    text(out,
         {rect.x + 8.f * scale,
          rect.y + (rect.height - static_cast<float>(label_pixels)) * .5f},
         tr(tab_keys[i]),
         active ? title_color : (hovered ? title_color : muted_color),
         label_pixels);
  }

  if (active_tab_ == DeveloperToolsTab::Commands) {
    const auto commands = core::developer_command_catalog();
    for (std::size_t i = 0;
         i < layout.command_rows.size() && i < commands.size(); ++i) {
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
  } else if (active_tab_ == DeveloperToolsTab::Diagnostics) {
    const auto row_height = 20.f * scale;
    auto cursor = layout.content.y;
    for (const auto &row : view.diagnostics) {
      if (cursor + row_height >
          layout.content.y + layout.content.height)
        break;
      const bool section = row.starts_with("--");
      text(out, {layout.content.x, cursor}, row,
           section ? gold_color : muted_color,
           section ? layout.body_font_pixels : layout.small_font_pixels);
      cursor += section ? row_height + 4.f * scale : row_height;
    }
    if (view.diagnostics.empty())
      text(out, {layout.content.x, cursor}, tr("DEVTOOLS_EMPTY_DIAGNOSTICS"),
           muted_color, layout.small_font_pixels);
  } else {
    const auto row_height = 34.f * scale;
    auto cursor = layout.content.y;
    for (const auto &slot : view.save_slots) {
      if (cursor + row_height > layout.content.y + layout.content.height)
        break;
      const UiRect rect{layout.content.x, cursor, layout.content.width,
                        row_height - 4.f * scale};
      fill(out, rect, tile_color);
      stroke(out, rect, border_color);
      text(out, {rect.x + 8.f * scale, rect.y + 4.f * scale}, slot.label,
           save_status_color(slot.status), layout.small_font_pixels);
      text(out,
           {rect.x + 8.f * scale,
            rect.y + 4.f * scale + layout.small_font_pixels + 2.f},
           slot.detail, muted_color, layout.small_font_pixels,
           rect.width - 16.f * scale);
      cursor += row_height;
    }
    if (view.save_slots.empty())
      text(out, {layout.content.x, cursor}, tr("DEVTOOLS_EMPTY_SAVES"),
           muted_color, layout.small_font_pixels);
  }

  if (!view.result.empty())
    text(out, {layout.result_text.x, layout.result_text.y}, view.result,
         view.result_accepted ? accent_color : error_color,
         layout.small_font_pixels, layout.result_text.width);
}

}  // namespace stellar::native_developer
