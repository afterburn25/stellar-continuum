#include "native_logistics_workspace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace stellar::native_logistics;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

bool contains(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

bool intersects(UiRect first, UiRect second) {
  return first.x < second.x + second.width &&
         first.x + first.width > second.x && first.y < second.y + second.height &&
         first.y + first.height > second.y;
}

TextExtent measured(const Text &text) {
  const int columns = text.wrap_width > 0.f
                          ? std::max(1, static_cast<int>(text.wrap_width / 7.f))
                          : std::max(1, static_cast<int>(text.value.size()));
  const int lines = std::max(1, (static_cast<int>(text.value.size()) + columns - 1) /
                                    columns);
  return {std::min(columns, static_cast<int>(text.value.size())) * 7,
          lines * (text.font_pixel_size + 4)};
}

View ready_view(int count = 2) {
  View view;
  view.state = LoadState::Ready;
  view.system_name = "Sol";
  view.message = "Available is exportable surplus; demand is required imports.";
  view.corridor_count = 3;
  view.supply_per_day = 9.25;
  view.demand_per_day = 6.50;
  view.delivered_per_day = 5.50;
  view.shortfall_per_day = 1.00;
  for (int index = 0; index < count; ++index) {
    view.nodes.push_back({index + 1,
        "施設名非常に長い補給拠点 " + std::to_string(index + 1) +
            " — long UTF-8 source-authored location name that must wrap",
        "Orbital logistics and resource processing facility", "Shortfall",
        1. + index, 2. + index, .5 + index});
  }
  return view;
}

void responsive_layout_fits() {
  for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080}}) {
    const auto layout = SupplyLayout::for_viewport(width, height);
    const UiRect viewport{0.f, 0.f, static_cast<float>(width), static_cast<float>(height)};
    require(contains(viewport, layout.panel) && contains(layout.panel, layout.body) &&
                contains(layout.panel, layout.close) && contains(layout.panel, layout.refresh),
            "supply layout escaped its viewport or panel");
  }
}

void every_node_is_reachable_and_scroll_clamps() {
  SupplyWorkspace workspace;
  workspace.set_text_measurer(measured);
  workspace.open();
  const auto view = ready_view(24);
  const auto layout = SupplyLayout::for_viewport(1280, 720);
  for (int i = 0; i < 300; ++i)
    (void)workspace.handle({InputEventType::Wheel, center(layout.body), {}, -10.f}, view, 1280, 720);
  require(workspace.scroll_offset() > 0.f, "long supply list did not scroll");
  DrawList draw;
  workspace.render(draw, view, 1280, 720);
  bool last_visible{};
  for (const auto &item : draw.overlay)
    if (const auto *text = std::get_if<Text>(&item);
        text && text->value == view.nodes.back().name)
      last_visible = text->clip && intersects(*text->clip, layout.body);
  require(last_visible, "last supply node was inaccessible at end scroll");
  for (int i = 0; i < 300; ++i)
    (void)workspace.handle({InputEventType::Wheel, center(layout.body), {}, 10.f}, view, 1280, 720);
  require(workspace.scroll_offset() == 0.f, "reverse supply scroll did not clamp at start");
}

void measured_rows_wrap_and_stay_clipped() {
  SupplyWorkspace workspace;
  workspace.set_text_measurer(measured);
  workspace.open();
  const auto view = ready_view(3);
  const auto layout = SupplyLayout::for_viewport(1280, 720);
  DrawList draw;
  workspace.render(draw, view, 1280, 720);
  bool wrapped{};
  for (const auto &item : draw.overlay) {
    const auto *text = std::get_if<Text>(&item);
    if (!text) continue;
    require(text->clip && contains(layout.panel, *text->clip),
            "supply text escaped its panel clip");
    for (const auto &node : view.nodes)
      if (text->value == node.name || text->value == node.kind_label ||
          text->value == node.status) {
        require(contains(layout.body, *text->clip),
                "supply row text escaped the scroll body");
        wrapped = wrapped || measured(*text).height > text->font_pixel_size + 4;
      }
  }
  require(wrapped, "injected measured UTF-8 name did not receive wrapped height");
}

void unavailable_and_failed_do_not_render_stale_totals() {
  SupplyWorkspace workspace;
  workspace.open();
  const auto ready = ready_view();
  const std::array<View, 2> states{
      View{LoadState::Unavailable, "SUPPLY NETWORK", "Economy missing.", {}, 0,
           999., 999., 999., 999., {}},
      View{LoadState::Failed, "SUPPLY NETWORK", "Retry after state is ready.",
           "Core failure", 0, 999., 999., 999., 999., ready.nodes}};
  for (const auto &view : states) {
    DrawList draw;
    workspace.render(draw, view, 1280, 720);
    for (const auto &item : draw.overlay)
      if (const auto *text = std::get_if<Text>(&item)) {
        require(text->value != "999.00 / day", "failed or unavailable view rendered stale numeric total");
        require(text->value != ready.nodes.front().name,
                "failed or unavailable view rendered stale node row");
      }
  }
}

void cache_reuses_measurements_and_invalidates_exactly_when_needed() {
  int first_calls{};
  SupplyWorkspace workspace;
  workspace.set_text_measurer([&first_calls](const Text &text) {
    ++first_calls;
    return measured(text);
  });
  workspace.open();
  auto view = ready_view(3);
  const auto layout = SupplyLayout::for_viewport(1280, 720);
  (void)workspace.handle({InputEventType::PointerMove, center(layout.body)}, view, 1280, 720);
  require(first_calls == 0, "pointer move measured supply rows without scrolling or rendering");
  DrawList first;
  workspace.render(first, view, 1280, 720);
  const auto stable_calls = first_calls;
  require(stable_calls > 0, "initial supply render did not measure rows");
  DrawList repeated;
  workspace.render(repeated, view, 1280, 720);
  require(first_calls == stable_calls, "unchanged supply render remeasured row geometry");
  view.nodes[1].name += " renamed";
  DrawList renamed;
  workspace.render(renamed, view, 1280, 720);
  require(first_calls > stable_calls, "changed node name did not invalidate row geometry");
  const auto renamed_calls = first_calls;
  DrawList resized;
  workspace.render(resized, view, 1920, 1080);
  require(first_calls > renamed_calls, "viewport change did not invalidate row geometry");
  int second_calls{};
  workspace.set_text_measurer([&second_calls](const Text &text) {
    ++second_calls;
    return measured(text);
  });
  DrawList remeasured;
  workspace.render(remeasured, view, 1920, 1080);
  require(second_calls > 0, "new text measurer did not invalidate cached geometry");
}

void tiny_positive_totals_are_not_rendered_as_zero() {
  SupplyWorkspace workspace;
  workspace.open();
  auto view = ready_view();
  view.shortfall_per_day = .005;
  DrawList draw;
  workspace.render(draw, view, 1280, 720);
  bool displayed{};
  for (const auto &item : draw.overlay)
    if (const auto *text = std::get_if<Text>(&item))
      displayed = displayed || text->value == "<0.01 / day";
  require(displayed, "tiny positive shortfall was displayed as zero");
}

void pointer_and_commands_route_without_leakage() {
  SupplyWorkspace workspace;
  workspace.open();
  const auto view = ready_view(20);
  const auto layout = SupplyLayout::for_viewport(1280, 720);
  const Point outside{0.f, 0.f};
  auto command = workspace.handle({InputEventType::LeftPressed, center(layout.body)}, view, 1280, 720);
  require(command.captured, "left press inside supply panel leaked");
  require(workspace.handle({InputEventType::PointerMove, outside}, view, 1280, 720).captured,
          "owned move outside supply panel leaked");
  require(workspace.handle({InputEventType::LeftReleased, outside}, view, 1280, 720).captured,
          "owned release outside supply panel leaked");
  require(workspace.handle({InputEventType::RightPressed, center(layout.body)}, view, 1280, 720).captured,
          "right press inside supply panel leaked");
  require(workspace.handle({InputEventType::RightReleased, outside}, view, 1280, 720).captured,
          "owned right release outside supply panel leaked");
  (void)workspace.handle({InputEventType::LeftPressed, center(layout.body)}, view, 1280, 720);
  require(workspace.handle({InputEventType::PointerCancelled}, view, 1280, 720).captured,
          "pointer cancellation was not captured");
  require(!workspace.handle({InputEventType::LeftReleased, outside}, view, 1280, 720).captured,
          "pointer cancellation retained stale ownership");
  require(!workspace.handle({InputEventType::KeyPressed}, view, 1280, 720).captured && workspace.visible(),
          "ordinary keyboard input did not pass through supply panel");
  command = workspace.handle({InputEventType::LeftPressed, center(layout.refresh)}, view, 1280, 720);
  require(command.captured && command.refresh, "refresh command was not emitted");
  (void)workspace.handle({InputEventType::LeftReleased, center(layout.refresh)}, view, 1280, 720);
  command = workspace.handle({InputEventType::LeftPressed, center(layout.close)}, view, 1280, 720);
  require(command.captured && !workspace.visible(), "close command did not dismiss supply panel");
  workspace.open();
  command = workspace.handle({InputEventType::EscapePressed}, view, 1280, 720);
  require(command.captured && !workspace.visible(), "Escape did not close and capture the supply workspace");
}

void keyboard_focus() {
  constexpr std::uint32_t kTab = 9u, kReturn = 13u, kHome = 0x4000004au,
                          kEnd = 0x4000004du, kDigit5 = '5';
  SupplyWorkspace workspace;
  workspace.open();
  const auto view = ready_view(4);
  const auto key = [&](std::uint32_t value, bool shift = false) {
    InputEvent event{InputEventType::KeyPressed};
    event.key = value;
    event.shift = shift;
    return workspace.handle(event, view, 1280, 720);
  };
  require(workspace.focus() < 0, "supply panel opened with stale focus");
  require(key(kTab).captured && workspace.focus() == 0,
          "Tab did not focus the refresh control");
  require(key(kTab).captured && workspace.focus() == 1,
          "Tab did not focus the close control");
  require(key(kTab, true).captured && workspace.focus() == 0,
          "Shift+Tab did not retreat the ring");
  require(key(kEnd).captured && workspace.focus() == 1,
          "End did not select the last control");
  require(key(kHome).captured && workspace.focus() == 0,
          "Home did not select the first control");
  require(workspace.focused_label(view) == "Refresh",
          "focused refresh control reported the wrong label");
  (void)key(kEnd);
  require(workspace.focused_label(view) == "Close supply network",
          "focused close control reported the wrong label");
  (void)key(kHome);
  const auto refreshed = key(kReturn);
  require(refreshed.captured && refreshed.refresh,
          "Return on the focused refresh control did not emit the command");
  require(workspace.focus() == 0 && workspace.visible(),
          "refresh activation moved focus or closed the panel");
  require(!key(kDigit5).captured,
          "unrelated key was swallowed by the non-modal panel");
  (void)key(kEnd);
  const auto closed = key(kReturn);
  require(closed.captured && !workspace.visible(),
          "Return on the focused close control did not dismiss the panel");
  workspace.open();
  require(workspace.focus() < 0, "reopened panel kept a stale focus index");
}

void corridors_render_in_the_scroll_body() {
  SupplyWorkspace workspace;
  workspace.set_text_measurer(measured);
  workspace.open();
  auto view = ready_view(2);
  view.links.push_back({7, "Homeworld", "Depot", "Busy", 4., 3., .5, true, false});
  DrawList draw;
  workspace.render(draw, view, 1600, 900);
  const auto layout = SupplyLayout::for_viewport(1600, 900);
  bool header = false, route = false;
  for (const auto &item : draw.overlay)
    if (const auto *text = std::get_if<Text>(&item)) {
      header |= text->value == "FREIGHT CORRIDORS";
      route |= text->value == "Homeworld -> Depot";
    }
  require(header && route, "corridor section did not render its title or route");
  for (const auto &item : draw.overlay)
    if (const auto *text = std::get_if<Text>(&item))
      require(!text->clip || contains(layout.body, *text->clip) ||
                  contains(layout.panel, *text->clip),
              "corridor text escaped its clip region");
}
}  // namespace

int main() {
  try {
    responsive_layout_fits();
    every_node_is_reachable_and_scroll_clamps();
    measured_rows_wrap_and_stay_clipped();
    unavailable_and_failed_do_not_render_stale_totals();
    cache_reuses_measurements_and_invalidates_exactly_when_needed();
    tiny_positive_totals_are_not_rendered_as_zero();
    pointer_and_commands_route_without_leakage();
    keyboard_focus();
    corridors_render_in_the_scroll_body();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
