#include "native_development_menu.hpp"

#include <iostream>
#include <stdexcept>

using namespace stellar::native_development;
using namespace stellar::native_map;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] InputEvent tap(Point at) {
  return {InputEventType::LeftReleased, at};
}

[[nodiscard]] InputEvent press(Point at) {
  return {InputEventType::LeftPressed, at};
}

[[nodiscard]] Point center(UiRect rect) {
  return {rect.x + rect.width * .5f, rect.y + rect.height * .5f};
}

void require_contained(UiRect outer, UiRect inner, const char *message) {
  require(inner.x >= outer.x && inner.y >= outer.y &&
              inner.x + inner.width <= outer.x + outer.width &&
              inner.y + inner.height <= outer.y + outer.height,
          message);
}

void verify_geometry(int width, int height) {
  const auto layout = development_menu_layout_for(width, height);
  const UiRect viewport{0, 0, static_cast<float>(width),
                        static_cast<float>(height)};
  require_contained(viewport, layout.panel, "Submenu panel escaped viewport.");
  for (const auto row : {layout.header, layout.open_button,
                         layout.seed_label, layout.seed_input,
                         layout.new_button, layout.tools_button,
                         layout.status_text, layout.back_button})
    require_contained(layout.panel, row, "A submenu row escaped its panel.");
}

void verify_commands() {
  constexpr int width = 1280, height = 720;
  NativeDevelopmentMenu menu;
  require(!menu.visible(), "The submenu started visible.");
  menu.open();

  // The release of the click that opened the submenu must not fire a row.
  const auto layout = development_menu_layout_for(width, height);
  require(menu.handle(tap(center(layout.open_button)), width, height).kind ==
              DevelopmentMenuCommandKind::None,
          "A release without a panel press fired a submenu row.");

  // OPEN DEVELOPER.
  require(menu.handle(press(center(layout.open_button)), width, height)
                  .captured,
          "The open press escaped the submenu.");
  auto command = menu.handle(tap(center(layout.open_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::OpenDeveloper,
          "The open row did not emit OpenDeveloper.");

  // BACK returns to the campaign menu.
  (void)menu.handle(press(center(layout.back_button)), width, height);
  command = menu.handle(tap(center(layout.back_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::Back && !menu.visible(),
          "The back row did not close the submenu.");

  // Escape returns to the campaign menu.
  menu.open();
  command =
      menu.handle({InputEventType::EscapePressed, {}}, width, height);
  require(command.kind == DevelopmentMenuCommandKind::Back && !menu.visible(),
          "Escape did not close the submenu.");

  // NEW DEVELOPER CAMPAIGN arms then confirms, carrying the default
  // PlayableDemoScenario seed.
  menu.open();
  (void)menu.handle(press(center(layout.new_button)), width, height);
  command = menu.handle(tap(center(layout.new_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::None,
          "The first new-campaign click skipped confirmation.");
  (void)menu.handle(press(center(layout.new_button)), width, height);
  command = menu.handle(tap(center(layout.new_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::NewDeveloperCampaign &&
              command.seed == 20260908 && !menu.visible(),
          "The confirmed click did not emit the demo seed.");

  // Edited seeds parse; invalid seeds block the command.
  menu.open();
  (void)menu.handle(press(center(layout.seed_input)), width, height);
  (void)menu.handle(tap(center(layout.seed_input)), width, height);
  command =
      menu.handle({InputEventType::TextEntered, {}, {}, 0.f, "42"}, width,
                  height);
  require(command.captured, "Seed typing leaked out of the submenu.");
  require(menu.handle({InputEventType::BackspacePressed, {}}, width, height)
                  .captured,
          "Seed backspace leaked out of the submenu.");
  (void)menu.handle(press(center(layout.new_button)), width, height);
  (void)menu.handle(tap(center(layout.new_button)), width, height);
  (void)menu.handle(press(center(layout.new_button)), width, height);
  command = menu.handle(tap(center(layout.new_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::NewDeveloperCampaign &&
              command.seed == 202609084,
          "The edited seed did not reach the campaign command.");

  // A non-numeric seed never arms or emits the command.
  menu.open();
  (void)menu.handle(press(center(layout.seed_input)), width, height);
  (void)menu.handle(tap(center(layout.seed_input)), width, height);
  for (int i = 0; i < 8; ++i)
    (void)menu.handle({InputEventType::BackspacePressed, {}}, width, height);
  (void)menu.handle({InputEventType::TextEntered, {}, {}, 0.f, "sol"}, width,
                    height);
  (void)menu.handle(press(center(layout.new_button)), width, height);
  command = menu.handle(tap(center(layout.new_button)), width, height);
  require(command.kind == DevelopmentMenuCommandKind::None && menu.visible(),
          "An invalid seed fired the new-campaign command.");
}

}  // namespace

int main() try {
  verify_geometry(1280, 720);
  verify_geometry(2560, 1440);
  verify_geometry(640, 360);
  verify_commands();
  std::cout << "Native Development submenu geometry and command tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
