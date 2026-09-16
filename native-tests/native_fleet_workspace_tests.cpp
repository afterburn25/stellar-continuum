#include "native_fleet_workspace.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;
using namespace stellar::native_map;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] bool contained(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

[[nodiscard]] Point center(UiRect bounds) noexcept {
  return {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f};
}

[[nodiscard]] bool has_text(const DrawList &draw, std::string_view value) {
  for (const auto &command : draw.overlay)
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.contains(value))
      return true;
  return false;
}

[[nodiscard]] const Text *find_text(const DrawList &draw,
                                    std::string_view value) {
  for (const auto &command : draw.overlay)
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.contains(value))
      return label;
  return nullptr;
}

[[nodiscard]] NativeFleetMapView player_view(bool selected = false) {
  NativeFleetMapView view;
  view.campaign_generation = 4;
  view.player_civilization_id = 0;
  NativeOwnFleet scout;
  scout.id = 10;
  scout.name = "ISS Wayfinder Long Range Expeditionary Vessel";
  scout.role = stellar::core::FleetRole::Scout;
  scout.position = {4, 7};
  scout.transit_phase = stellar::core::FleetTransitPhase::None;
  scout.maximum_leg_range_light_years = 24;
  scout.fuel_capacity_light_years = 40;
  scout.fuel_remaining_light_years = 18.75;
  scout.strategic_speed = 2.5;
  scout.combat_power = 7.25;
  NativeOwnFleet colony = scout;
  colony.id = 12;
  colony.name = "CSV Horizon";
  colony.role = stellar::core::FleetRole::Colony;
  colony.position = {4, 7};
  colony.combat_power = 3.5;
  view.own_fleets = {std::move(scout), std::move(colony)};
  view.foreign_contacts.push_back({99, 800, 12, "historic report"});
  if (selected) view.selected_fleet_id = 10;
  return view;
}
} // namespace

int main() try {
  for (const auto [width, height] :
       std::array{std::pair{640, 360}, std::pair{1280, 720},
                  std::pair{1920, 1080}, std::pair{2560, 1440},
                  std::pair{3840, 2160}}) {
    const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
    const UiRect viewport{0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
    require(contained(viewport, layout.panel) &&
                contained(layout.panel, layout.heading) &&
                contained(layout.panel, layout.list) &&
                contained(layout.panel, layout.details) &&
                contained(layout.panel, layout.route) &&
                contained(layout.panel, layout.feedback) &&
                contained(layout.panel, layout.confirm),
            "Fleet workspace escaped its viewport.");
  }

  NativeFleetWorkspace empty;
  NativeFleetMapView empty_view;
  empty_view.campaign_generation = 1;
  empty.set_view(std::move(empty_view));
  DrawList empty_draw;
  empty.render(empty_draw, 1280, 720, {});
  require(has_text(empty_draw, "No active player fleets") &&
              empty_draw.circles.empty(),
          "Fresh prewarp fleet state invented a vessel or hid its empty state.");

  NativeFleetWorkspace workspace;
  workspace.set_view(player_view());
  const auto layout = FleetWorkspaceLayout::for_viewport(1280, 720);
  const std::array markers{FleetScreenMarker{10, {300, 300}},
                           FleetScreenMarker{12, {310, 300}}};
  auto row_select = workspace.handle(
      {InputEventType::LeftPressed,
       {layout.list.x + 10.f, layout.list.y + 10.f}},
      1280, 720, markers, std::nullopt);
  require(row_select.captured &&
              row_select.kind == FleetWorkspaceCommandKind::Select &&
              row_select.fleet_id == 10,
          "Fleet outliner click did not emit an owned fleet selection.");
  (void)workspace.handle(
      {InputEventType::PointerMove,
       {layout.list.x + 10.f, layout.list.y + 10.f}},
      1280, 720, markers, std::nullopt);
  DrawList hover_draw;
  workspace.render(hover_draw, 1280, 720, markers);
  require(has_text(hover_draw, "Select for readiness"),
          "Fleet hover did not expose contextual guidance.");

  auto map_select = workspace.handle(
      {InputEventType::LeftPressed, {305, 300}}, 1280, 720, markers,
      std::nullopt);
  require(map_select.captured &&
              map_select.kind == FleetWorkspaceCommandKind::SelectHits &&
              map_select.hit_fleet_ids == std::vector<int>({10, 12}),
          "Overlapping owned map markers did not preserve cycle candidates.");

  workspace.set_view(player_view(true));
  auto preview_command = workspace.handle(
      {InputEventType::RightPressed, {500, 400}}, 1280, 720, markers, 42);
  require(preview_command.captured &&
              preview_command.kind == FleetWorkspaceCommandKind::Preview &&
              preview_command.target_system_id == 42,
          "Right-click did not request an adapter route preview.");

  NativeFleetRoutePreview blocked;
  blocked.campaign_generation = 4;
  blocked.fleet_id = 10;
  blocked.target_system_id = 42;
  blocked.route_distance_light_years = 31.25;
  blocked.message =
      "Insufficient operational range. Refuel at an owned support point before "
      "attempting this exceptionally long route to the selected destination.";
  workspace.set_preview(blocked, "Unknown system");
  DrawList blocked_draw;
  workspace.render(blocked_draw, 1280, 720, markers);
  require(has_text(blocked_draw, "ISS Wayfinder") &&
              has_text(blocked_draw, "COMBAT POWER") &&
              has_text(blocked_draw, "7.2") &&
              has_text(blocked_draw, "FUEL  18.75 / 40.00 ly") &&
              has_text(blocked_draw, "Maximum leg 24.00 ly") &&
              has_text(blocked_draw, "Destination Unknown system") &&
              has_text(blocked_draw, "Insufficient operational range") &&
              !has_text(blocked_draw, "CONFIRM TRAVEL") &&
              !has_text(blocked_draw, "historic report"),
          "Fleet details leaked foreign data or omitted adapter blockers.");

  auto supported = blocked;
  supported.route_supported = true;
  supported.route_authoritative = true;
  supported.command_available = true;
  supported.estimated_transit_days = 12.5;
  supported.message.clear();
  workspace.set_preview(
      std::move(supported),
      "Alpha Centauri Expeditionary Destination With A Long Name");
  DrawList supported_draw;
  workspace.render(supported_draw, 1280, 720, markers);
  require(has_text(supported_draw, "Distance 31.25 ly") &&
              has_text(supported_draw, "Estimated ETA 12.50 days") &&
              has_text(supported_draw, "Confirmed lane route") &&
              has_text(supported_draw, "CONFIRM TRAVEL") &&
              supported_draw.circles.size() == markers.size() * 2,
          "Supported adapter route or owned fleet markers were not rendered.");
  const auto *fleet_name = find_text(supported_draw, "ISS Wayfinder");
  const auto *route = find_text(supported_draw, "ROUTE PREVIEW");
  require(fleet_name && fleet_name->clip && route && route->clip &&
              fleet_name->clip->height <= 18.f &&
              contained(layout.list, *fleet_name->clip) &&
              contained(layout.route, *route->clip),
          "Long-name or route text was not clipped to its dedicated 720p row.");
  const auto confirm = workspace.handle(
      {InputEventType::LeftPressed, center(layout.confirm)}, 1280, 720,
      markers, std::nullopt);
  require(confirm.captured &&
              confirm.kind == FleetWorkspaceCommandKind::Confirm,
          "Explicit travel confirmation was not routed.");

  auto revised = player_view(true);
  revised.own_fleets.front().mission_order_revision = 1;
  workspace.set_view(std::move(revised));
  require(!workspace.preview(),
          "A mission revision change left stale confirmation visible.");

  workspace.set_preview(blocked, "Unknown system");

  auto replacement = player_view(true);
  replacement.campaign_generation = 5;
  replacement.selected_fleet_id.reset();
  workspace.set_notice("Old campaign order accepted.", true);
  workspace.set_view(std::move(replacement));
  DrawList replacement_draw;
  workspace.render(replacement_draw, 1280, 720, markers);
  require(!workspace.preview() && !workspace.selected_fleet_id() &&
              !has_text(replacement_draw, "Old campaign order accepted") &&
              !has_text(replacement_draw, "Alpha Centauri"),
          "Fleet preview, notice, or selection survived campaign replacement.");

  // Reference armed-fleet order row: Hold / Defend / Retreat / Locate /
  // Engage (UiIssueMilitaryOrder / UiFocusOwnedFleet / UiEngageHostiles).
  {
    auto armed_view = player_view();
    NativeOwnFleet warship;
    warship.id = 30;
    warship.name = "ISS Aegis";
    warship.role = stellar::core::FleetRole::Military;
    warship.position = {9, 4};
    warship.current_system_id = 7;
    stellar::core::OwnCombatFleetStatus status;
    status.is_armed = true;
    status.is_combat_effective = true;
    warship.combat_status = status;
    armed_view.own_fleets.push_back(std::move(warship));
    armed_view.selected_fleet_id = 30;
    workspace.set_view(std::move(armed_view));
    DrawList armed_draw;
    workspace.render(armed_draw, 1280, 720, markers);
    require(has_text(armed_draw, "HOLD") && has_text(armed_draw, "DEFEND") &&
                has_text(armed_draw, "RETREAT") &&
                has_text(armed_draw, "LOCATE") &&
                has_text(armed_draw, "ENGAGE"),
            "Armed fleet details did not render its reference order row.");
    const auto hold = workspace.handle(
        {InputEventType::LeftPressed, center(layout.order_hold)}, 1280, 720,
        markers, std::nullopt);
    require(hold.captured &&
                hold.kind == FleetWorkspaceCommandKind::MilitaryHold &&
                hold.fleet_id == 30,
            "Armed HOLD did not emit a military hold order.");
    const auto defend = workspace.handle(
        {InputEventType::LeftPressed, center(layout.order_defend)}, 1280, 720,
        markers, std::nullopt);
    require(defend.captured &&
                defend.kind == FleetWorkspaceCommandKind::MilitaryDefend &&
                defend.fleet_id == 30,
            "Armed DEFEND did not emit a military defend order.");
    const auto retreat = workspace.handle(
        {InputEventType::LeftPressed, center(layout.order_retreat)}, 1280,
        720, markers, std::nullopt);
    require(retreat.captured &&
                retreat.kind == FleetWorkspaceCommandKind::MilitaryRetreat &&
                retreat.fleet_id == 30,
            "Armed RETREAT did not emit a military retreat order.");
    const auto locate = workspace.handle(
        {InputEventType::LeftPressed, center(layout.locate)}, 1280, 720,
        markers, std::nullopt);
    require(locate.captured &&
                locate.kind == FleetWorkspaceCommandKind::Locate &&
                locate.fleet_id == 30,
            "Armed LOCATE did not emit a locate command.");
    const auto engage = workspace.handle(
        {InputEventType::LeftPressed, center(layout.engage)}, 1280, 720,
        markers, std::nullopt);
    require(engage.captured &&
                engage.kind == FleetWorkspaceCommandKind::Engage &&
                engage.fleet_id == 30,
            "Armed ENGAGE did not emit an engage command.");
  }

  // Unarmed selections keep LOCATE in the right-edge command slot.
  {
    workspace.set_view(player_view(true));
    DrawList unarmed_draw;
    workspace.render(unarmed_draw, 1280, 720, markers);
    require(has_text(unarmed_draw, "LOCATE") &&
                !has_text(unarmed_draw, "DEFEND"),
            "Unarmed fleet details omitted LOCATE or leaked order buttons.");
    const auto locate = workspace.handle(
        {InputEventType::LeftPressed, center(layout.engage)}, 1280, 720,
        markers, std::nullopt);
    require(locate.captured &&
                locate.kind == FleetWorkspaceCommandKind::Locate &&
                locate.fleet_id == 10,
            "Unarmed LOCATE did not emit a locate command.");
  }

  std::cout << "Native owned-fleet outliner, map hit, route preview, confirmation, "
               "empty-state and secrecy tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
