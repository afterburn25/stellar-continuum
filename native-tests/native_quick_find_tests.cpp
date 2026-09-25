#include "native_quick_find.hpp"
#include "native_ui_theme.hpp"

#include <iostream>
#include <stdexcept>
#include <variant>
#include <vector>

using namespace stellar::native_quick_find;
using namespace stellar::native_map;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

std::vector<Entry> entries() {
  return {
      {EntryKind::System, 10, "Sol", {}},
      {EntryKind::System, 11, "Proxima Centauri", {}},
      {EntryKind::Colony, 20, "Landing", "Sol"},
      {EntryKind::Fleet, 30, "SCV Meridian", "Proxima Centauri"},
      {EntryKind::Contact, 40, "Kesh Exchange", "Peace"},
  };
}

InputEvent key(std::uint32_t code) {
  InputEvent event{InputEventType::KeyPressed};
  event.key = code;
  return event;
}

bool has_text(const DrawList &draw, std::string_view value) {
  for (const auto &command : draw.overlay)
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.find(value) != std::string::npos)
      return true;
  return false;
}
} // namespace

int main() {
  try {
    {
      // Closed palette captures nothing.
      QuickFind palette;
      require(!palette.visible() && !palette.wants_text_input(),
              "Quick find should start hidden.");
      require(!palette.handle(key(13), 1280, 720).captured,
              "Closed palette captured input.");
    }
    {
      QuickFind palette;
      palette.open(entries());
      require(palette.visible() && palette.wants_text_input() &&
                  palette.match_count() == 5,
              "Open did not surface every entry.");
      // The field owns focus and reports the Edit contract.
      require(palette.focus() == 0 &&
                  palette.focused_control(1280, 720) ==
                      stellar::engine::AnnouncementControl::Edit,
              "Quick find field did not report the Edit control kind.");
      // Typing filters case-insensitively across label + detail.
      const auto typed = palette.handle(
          {InputEventType::TextEntered, {}, {}, 0.f, "proxi"}, 1280, 720);
      require(typed.captured, "Palette did not capture typed text.");
      require(palette.match_count() == 2,
              "Substring filter did not match label and detail rows.");
      // Return activates the top match (nothing highlighted yet) and closes.
      const auto command = palette.handle(key(13), 1280, 720);
      require(command.captured && command.activated &&
                  command.activated->kind == EntryKind::System &&
                  command.activated->id == 11,
              "Return did not activate the filtered system entry.");
      require(!palette.visible(), "Activation did not close the palette.");
    }
    {
      QuickFind palette;
      palette.open(entries());
      (void)palette.handle({InputEventType::TextEntered, {}, {}, 0.f, "kesh"},
                           1280, 720);
      require(palette.match_count() == 1, "Contact filter did not match.");
      const auto command = palette.handle(key(13), 1280, 720);
      require(command.activated && command.activated->id == 40,
              "Return did not activate the contact entry.");
    }
    {
      // Arrow/Home/End navigation wraps the highlight.
      QuickFind palette;
      palette.open(entries());
      require(palette.focus() == 0, "Initial focus should sit on the field.");
      (void)palette.handle(key(0x40000051u), 1280, 720);  // Down
      require(palette.focus() == 1,
              "Down did not move the highlight to row 0.");
      require(palette.focused_control(1280, 720) ==
                  stellar::engine::AnnouncementControl::Custom,
              "A highlighted row should announce as a custom control.");
      require(palette.focused_label(1280, 720).find("Sol") !=
                  std::string::npos,
              "Focused label did not describe the highlighted row.");
      (void)palette.handle(key(0x4000004du), 1280, 720);  // End
      require(palette.focus() == 5, "End did not jump to the last row.");
      (void)palette.handle(key(0x40000052u), 1280, 720);  // Up
      require(palette.focus() == 4, "Up did not step back.");
      // Backspace truncates on a code-point boundary and refilters.
      (void)palette.handle({InputEventType::TextEntered, {}, {}, 0.f, "x"},
                           1280, 720);
      (void)palette.handle({InputEventType::BackspacePressed}, 1280, 720);
      require(palette.query().empty() && palette.match_count() == 5,
              "Backspace did not restore the unfiltered list.");
      // Escape closes without activation.
      const auto escaped = palette.handle({InputEventType::EscapePressed},
                                          1280, 720);
      require(escaped.captured && !escaped.activated && !palette.visible(),
              "Escape did not close the palette cleanly.");
    }
    {
      // Pointer: outside press closes; a row press+release activates.
      QuickFind palette;
      palette.open(entries());
      const auto layout = QuickFindLayout::make(1280, 720);
      const auto outside = palette.handle(
          {InputEventType::LeftPressed, {4.f, 4.f}}, 1280, 720);
      require(outside.captured && !palette.visible(),
              "Outside click did not close the palette.");
      palette.open(entries());
      const Point row_point{layout.list.x + 30.f, layout.list.y + 15.f};
      (void)palette.handle({InputEventType::LeftPressed, row_point}, 1280, 720);
      const auto clicked = palette.handle(
          {InputEventType::LeftReleased, row_point}, 1280, 720);
      require(clicked.activated && clicked.activated->id == 10,
              "Row click did not activate the first entry.");
    }
    {
      // Platform SetValue path applies to the field.
      QuickFind palette;
      palette.open(entries());
      require(palette.set_focused_text("landi", 1280, 720),
              "SetValue did not reach the palette field.");
      require(palette.match_count() == 1,
              "SetValue did not refilter the results.");
      palette.close();
      require(!palette.set_focused_text("ignored", 1280, 720),
              "SetValue reached a closed palette.");
    }
    {
      // Render: scrim + panel + field + badge labels stay inside the panel.
      QuickFind palette;
      palette.open(entries());
      (void)palette.handle({InputEventType::TextEntered, {}, {}, 0.f, "sol"},
                           1280, 720);
      DrawList draw;
      palette.render(draw, 1280, 720);
      require(!draw.overlay.empty(), "Palette rendered no overlay commands.");
      require(has_text(draw, "QUICK FIND") && has_text(draw, "SYSTEM") &&
                  has_text(draw, "COLONY"),
              "Palette did not render its title and kind badges.");
      const auto layout = QuickFindLayout::make(1280, 720);
      require(layout.field.x >= layout.panel.x &&
                  layout.field.x + layout.field.width <=
                      layout.panel.x + layout.panel.width &&
                  layout.hint.y + layout.hint.height <=
                      layout.panel.y + layout.panel.height,
              "Palette layout escaped its panel.");
    }
    {
      // Empty result set still renders the empty state and stays closable.
      QuickFind palette;
      palette.open(entries());
      (void)palette.handle({InputEventType::TextEntered, {}, {}, 0.f, "zzz"},
                           1280, 720);
      DrawList draw;
      palette.render(draw, 1280, 720);
      require(has_text(draw, "No matching destinations"),
              "Empty palette did not render its empty state.");
      const auto command = palette.handle(key(13), 1280, 720);
      require(command.captured && !command.activated,
              "Return on an empty result set must not activate.");
    }
  } catch (const std::exception &error) {
    std::cerr << "quick find test failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "quick find tests passed\n";
  return 0;
}
