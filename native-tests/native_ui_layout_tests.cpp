#include "native_ui_layout.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace stellar::native_map;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] bool contains_rect(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

[[nodiscard]] bool overlaps(UiRect left, UiRect right) noexcept {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

[[nodiscard]] std::array<Point, 5> interior_points(UiRect value) noexcept {
  constexpr float edge_inset = .25f;
  return {{{value.x + value.width * .5f, value.y + value.height * .5f},
           {value.x + edge_inset, value.y + edge_inset},
           {value.x + value.width - edge_inset, value.y + edge_inset},
           {value.x + edge_inset, value.y + value.height - edge_inset},
           {value.x + value.width - edge_inset,
            value.y + value.height - edge_inset}}};
}

void verify(int width, int height, float expected_scale) {
  const auto layout = NativeUiLayout::for_viewport(width, height);
  const UiRect viewport{0, 0, static_cast<float>(width),
                       static_cast<float>(height)};
  const std::array<std::pair<UiRect, UiAction>, 6> menu{{
      {layout.continue_button, UiAction::Continue},
      {layout.save_button, UiAction::Save},
      {layout.load_button, UiAction::Load},
      {layout.settings_button, UiAction::Settings},
      {layout.support_button, UiAction::Support},
      {layout.exit_button, UiAction::Exit},
  }};

  require(std::abs(layout.scale - expected_scale) < .001f,
          "Viewport produced the wrong UI scale.");
  require(contains_rect(viewport, layout.pause) &&
              contains_rect(viewport, layout.speed) &&
              contains_rect(viewport, layout.notifications) &&
              contains_rect(viewport, layout.research) &&
              contains_rect(viewport, layout.shipyard) &&
              contains_rect(viewport, layout.construction) &&
              contains_rect(viewport, layout.day_text) &&
              contains_rect(viewport, layout.status_text) &&
              contains_rect(viewport, layout.menu_panel) &&
              contains_rect(layout.menu_panel, layout.menu_heading),
          "A UI rectangle escaped the drawable viewport.");
  require(!overlaps(layout.pause, layout.speed) &&
              !overlaps(layout.notifications, layout.pause) &&
              !overlaps(layout.notifications, layout.speed) &&
              !overlaps(layout.notifications, layout.status_text) &&
              !overlaps(layout.research, layout.shipyard) &&
              !overlaps(layout.shipyard, layout.construction) &&
              !overlaps(layout.construction, layout.diplomacy),
          "Top controls overlap each other.");
  const auto rail_right = layout.research.x + layout.research.width;
  require(std::abs(layout.research.x - 18.f * layout.scale) < .01f &&
              rail_right <= native_navigation_content_left * layout.scale &&
              layout.shipyard.x == layout.research.x &&
              layout.construction.x == layout.research.x &&
              layout.diplomacy.x == layout.research.x,
          "Navigation rail did not preserve the shared content gutter.");
  require(!overlaps(layout.pause, layout.day_text) &&
              !overlaps(layout.speed, layout.day_text) &&
              !overlaps(layout.pause, layout.status_text) &&
              !overlaps(layout.speed, layout.status_text) &&
              !overlaps(layout.day_text, layout.status_text),
          "Top controls or metrics overlap.");
  require(layout.control_font_pixels >= layout.metric_font_pixels &&
              layout.heading_font_pixels > layout.control_font_pixels,
          "Scaled font hierarchy is invalid.");

  for (std::size_t index = 0; index < menu.size(); ++index) {
    const auto [bounds, action] = menu[index];
    require(contains_rect(layout.menu_panel, bounds),
            "A whole menu button escaped the panel.");
    if (index > 0) {
      require(!overlaps(menu[index - 1].first, bounds),
              "Adjacent menu buttons overlap.");
    }
    for (const auto point : interior_points(bounds)) {
      require(layout.hit(point, true) == action,
              "A point inside a menu button missed its action.");
      require(layout.hit(point, false) == UiAction::None,
              "Closed menu accepted a hidden button.");
    }
  }
  require(!overlaps(layout.menu_heading, layout.continue_button),
          "Menu heading overlaps the first button.");

  for (const auto point : interior_points(layout.pause)) {
    require(layout.hit(point, false) == UiAction::Pause,
            "A point inside Pause missed its action.");
  }
  for (const auto point : interior_points(layout.speed)) {
    require(layout.hit(point, false) == UiAction::Speed,
            "A point inside Speed missed its action.");
  }
  for (const auto point : interior_points(layout.research)) {
    require(layout.hit(point, false) == UiAction::Research,
            "A point inside Research missed its action.");
  }
  for (const auto point : interior_points(layout.shipyard)) {
    require(layout.hit(point, false) == UiAction::Shipyard,
            "A point inside Shipyard missed its action.");
  }
  for (const auto point : interior_points(layout.construction)) {
    require(layout.hit(point, false) == UiAction::Construction,
            "A point inside Construction missed its action.");
  }
  for (const auto point : interior_points(layout.notifications)) {
    require(layout.hit(point, false) == UiAction::Notifications,
            "A point inside Events missed its action.");
    require(layout.hit(point, true) == UiAction::None,
            "The pause menu admitted a hidden Events button.");
  }
  for (const auto point : interior_points(layout.diplomacy)) {
    require(layout.hit(point, false) == UiAction::Diplomacy,
            "A point inside Relations missed its action.");
  }
  for (const auto bounds : {layout.research, layout.shipyard,
                            layout.construction, layout.diplomacy})
    for (const auto point : interior_points(bounds))
      require(layout.hit(point, true) == UiAction::None,
              "Pause menu accepted a hidden navigation-rail action.");
  require(layout.hit({static_cast<float>(width - 1),
                      static_cast<float>(height - 1)}, true) == UiAction::None,
          "Outside point activated the menu.");
}

void verify_navigation_blockers() {
  require(native_navigation_available(false, false, false, false, false),
          "Unobstructed navigation was disabled.");
  for (const auto blocked : {
           native_navigation_available(true, false, false, false, false),
           native_navigation_available(false, true, false, false, false),
           native_navigation_available(false, false, true, false, false),
           native_navigation_available(false, false, false, true, false),
           native_navigation_available(false, false, false, false, true)})
    require(!blocked, "A menu or modal exposed navigation underneath it.");
}

} // namespace

int main() try {
  verify_navigation_blockers();
  verify(640, 360, 324.f/380.f);
  verify(1280, 720, 1.f);
  verify(1920, 1080, 1.2f);
  verify(1280, 1080, 1.2f);
  verify(2560, 1440, 1.6f);
  verify(3840, 2160, 2.4f);
  std::cout << "Native UI 720p through 4K scaling, containment and full hit "
               "tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
