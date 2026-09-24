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
  const auto hud=CommandHudLayout::make(width,height);
  require(contains_rect(viewport,hud.context)&&
      contains_rect(hud.context,hud.switch_view),"HUD panels escaped or overlapped");
  require(contains_rect(hud.resource_strip,layout.pause)&&contains_rect(hud.resource_strip,layout.speed),"Time controls escaped resource strip");
  const std::array<std::pair<UiRect, UiAction>, 7> menu{{
      {layout.continue_button, UiAction::Continue},
      {layout.save_button, UiAction::Save},
      {layout.load_button, UiAction::Load},
      {layout.settings_button, UiAction::Settings},
      {layout.support_button, UiAction::Support},
      {layout.new_game_button, UiAction::NewGame},
      {layout.exit_button, UiAction::Exit},
  }};
  const std::array<std::pair<UiRect, UiAction>, 14> navigation{{
      {layout.map, UiAction::Map}, {layout.home, UiAction::Home},
      {layout.inspect, UiAction::Inspect}, {layout.zoom_in, UiAction::ZoomIn},
      {layout.zoom_out, UiAction::ZoomOut}, {layout.economy, UiAction::Economy},
      {layout.research, UiAction::Research}, {layout.construction, UiAction::Construction},
      {layout.shipyard, UiAction::Shipyard}, {layout.explore, UiAction::Explore},
      {layout.colonies, UiAction::Colonies}, {layout.supply, UiAction::Supply},
      {layout.diplomacy, UiAction::Diplomacy}, {layout.menu, UiAction::Menu},
  }};

  require(std::abs(layout.scale - expected_scale) < .001f,
          "Viewport produced the wrong UI scale.");
  require(contains_rect(viewport, layout.pause) &&
              contains_rect(viewport, layout.speed) &&
              contains_rect(viewport, layout.notifications) &&
              contains_rect(viewport, layout.day_text) &&
              contains_rect(viewport, layout.status_text) &&
              contains_rect(viewport, layout.zoom_text) &&
              contains_rect(viewport, layout.menu_panel) &&
              contains_rect(layout.menu_panel, layout.menu_heading),
          "A UI rectangle escaped the drawable viewport.");
  for (const auto [bounds, action] : navigation) {
    (void)action;
    require(contains_rect(viewport, bounds),
            "A navigation button escaped the drawable viewport.");
  }
  for (std::size_t left = 0; left < navigation.size(); ++left)
    for (std::size_t right = left + 1; right < navigation.size(); ++right)
      require(!overlaps(navigation[left].first, navigation[right].first),
              "Navigation buttons overlap.");
  require(!overlaps(layout.pause, layout.speed) &&
              !overlaps(layout.notifications, layout.pause) &&
              !overlaps(layout.notifications, layout.speed) &&
              !overlaps(layout.notifications, layout.status_text) &&
              !overlaps(layout.research, layout.shipyard) &&
              !overlaps(layout.shipyard, layout.construction) &&
              !overlaps(layout.construction, layout.diplomacy) &&
              !overlaps(layout.diplomacy, layout.supply) &&
              !overlaps(layout.supply, layout.economy) &&
              !overlaps(layout.economy, layout.colonies),
          "Top controls overlap each other.");
  const auto rail_right = layout.inspect.x + layout.inspect.width;
  require(std::abs(layout.inspect.x - 18.f * layout.scale) < .01f &&
              rail_right <= native_navigation_content_left * layout.scale &&
              layout.construction.x == layout.inspect.x &&
              layout.explore.x == layout.inspect.x,
          "Secondary navigation did not preserve the shared content gutter.");
  const std::array primary{layout.map,layout.home,layout.colonies,layout.economy,layout.research,layout.diplomacy,layout.supply,layout.shipyard};
  for(std::size_t i=0;i<primary.size();++i){
    require(contains_rect(layout.navigation_bar,primary[i])&&!overlaps(layout.brand,primary[i]),"Primary navigation overlaps branding or escapes its header.");
    for(std::size_t j=0;j<i;++j)require(!overlaps(primary[i],primary[j]),"Primary navigation tabs overlap.");
  }
  require(!overlaps(layout.pause, layout.day_text) &&
              !overlaps(layout.speed, layout.day_text) &&
              !overlaps(layout.pause, layout.status_text) &&
              !overlaps(layout.speed, layout.status_text) &&
              !overlaps(layout.day_text, layout.status_text) &&
              !overlaps(layout.zoom_text, layout.day_text) &&
              !overlaps(layout.zoom_text, layout.status_text),
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
      const auto visible_action=layout.hit(point,false);
      for(const auto& entry:menu)
        require(visible_action!=entry.second,"Closed menu accepted a hidden button.");
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
  for (const auto point : interior_points(layout.notifications)) {
    require(layout.hit(point, false) == UiAction::Notifications,
            "A point inside Events missed its action.");
    require(layout.hit(point, true) != UiAction::Notifications,
            "The pause menu admitted a hidden Events button.");
  }
  for (const auto [bounds, action] : navigation)
    for (const auto point : interior_points(bounds)) {
      require(layout.hit(point, false) == action,
              "A point inside a navigation button missed its action.");
      const auto modal_action=layout.hit(point,true);
      for(const auto& entry:navigation)
        require(modal_action!=entry.second,"Pause menu accepted a hidden navigation action.");
    }
  require(layout.hit({static_cast<float>(width - 1),
                      static_cast<float>(height - 1)}, true) == UiAction::None,
          "Outside point activated the menu.");

  // hud_actions() is the keyboard focus ring's source of truth — every item
  // must hit-test back to its own action and arrive in (y,x) order.
  const auto ring = layout.hud_actions();
  require(ring.size() == 17, "HUD focus ring changed size.");
  for (std::size_t index = 0; index < ring.size(); ++index) {
    const auto &[bounds, action] = ring[index];
    require(contains_rect(viewport, bounds),
            "A HUD focusable escaped the drawable viewport.");
    require(layout.hit({bounds.x + bounds.width * .5f,
                        bounds.y + bounds.height * .5f},
                       false) == action,
            "A HUD focusable did not hit-test to its action.");
    if (index)
      require(ring[index - 1].first.y < bounds.y ||
                  (ring[index - 1].first.y == bounds.y &&
                   ring[index - 1].first.x <= bounds.x),
              "HUD focusables are not in (y,x) order.");
  }
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

void verify_text_scale() {
  const auto baseline = NativeUiLayout::for_viewport(1920, 1080);
  NativeUiLayout::set_text_scale(1.5f);
  const auto scaled = NativeUiLayout::for_viewport(1920, 1080);
  require(scaled.scale == baseline.scale &&
              scaled.pause.x == baseline.pause.x &&
              scaled.menu_panel.width == baseline.menu_panel.width,
          "Text scale changed layout geometry.");
  require(scaled.control_font_pixels ==
                  std::lround(17.f * baseline.scale * 1.5f) &&
              scaled.metric_font_pixels ==
                  std::lround(15.f * baseline.scale * 1.5f) &&
              scaled.heading_font_pixels ==
                  std::lround(22.f * baseline.scale * 1.5f),
          "Text scale did not enlarge the shared font metrics.");
  NativeUiLayout::set_text_scale(0.5f);
  require(NativeUiLayout::text_scale() == .75f,
          "Text scale did not clamp below the accessibility range.");
  NativeUiLayout::set_text_scale(9.f);
  require(NativeUiLayout::text_scale() == 2.f,
          "Text scale did not clamp above the accessibility range.");
  NativeUiLayout::set_text_scale(1.f);
}

} // namespace

int main() try {
  verify_navigation_blockers();
  verify_text_scale();
  verify(640, 360, 324.f/430.f);
  verify(1280, 720, 1.f);
  verify(1920, 1080, 1.f);
  verify(1280, 1080, 1.f);
  verify(2560, 1440, 4.f/3.f);
  verify(3840, 2160, 2.f);
  std::cout << "Native UI 720p through 4K scaling, containment and full hit "
               "tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
