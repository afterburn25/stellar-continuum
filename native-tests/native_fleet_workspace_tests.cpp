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
              has_text(blocked_draw, "Own strength 7.2") &&
              has_text(blocked_draw, "Fuel 18.75 / 40.00 ly") &&
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

  NativeFleetWorkspace engagement;
  auto armed = player_view(true);
  auto &warship = armed.own_fleets.front();
  warship.role = stellar::core::FleetRole::Military;
  warship.current_system_id = 0;
  warship.combat_status = stellar::core::OwnCombatFleetStatus{};
  warship.combat_status->is_armed = true;
  const auto engage_click = [&] {
    return engagement.handle({InputEventType::LeftPressed, center(layout.confirm)},
                             1280, 720, markers, std::nullopt).kind;
  };
  engagement.set_view(armed);
  DrawList engage_draw;
  engagement.render(engage_draw, 1280, 720, markers);
  require(has_text(engage_draw, "ENGAGE HOSTILES") &&
              engage_click() == FleetWorkspaceCommandKind::Engage,
          "Stationed armed fleet did not expose the authoritative engagement command.");
  for (int unavailable = 0; unavailable < 4; ++unavailable) {
    auto view = armed;
    auto &fleet = view.own_fleets.front();
    if (unavailable == 0) fleet.role = stellar::core::FleetRole::Scout;
    if (unavailable == 1) fleet.current_system_id.reset();
    if (unavailable == 2) fleet.destination_system_id = 42;
    if (unavailable == 3) fleet.combat_status->is_armed = false;
    engagement.set_view(std::move(view));
    DrawList unavailable_draw;
    engagement.render(unavailable_draw, 1280, 720, markers);
    require(!has_text(unavailable_draw, "ENGAGE HOSTILES") &&
                engage_click() != FleetWorkspaceCommandKind::Engage,
            "Ineligible fleet exposed a tactical engagement action.");
  }
  engagement.set_view(armed);
  engagement.set_preview(blocked, "Unknown system");
  require(engage_click() != FleetWorkspaceCommandKind::Engage,
          "Blocked travel preview accidentally started combat.");
  auto ready = blocked;
  ready.route_supported = ready.route_authoritative = ready.command_available = true;
  engagement.set_preview(ready, "Unknown system");
  require(engage_click() == FleetWorkspaceCommandKind::Confirm,
          "Engagement replaced an explicit travel confirmation.");

  auto revised = player_view(true);
  {
    NativeFleetWorkspace recovery;
    auto view = player_view(true);
    view.own_fleets.front().recovery = NativeCivilianRecoveryQuote{
        .campaign_generation=4, .observer_id=0, .fleet_id=10,
        .role=stellar::core::FleetRole::Colony, .destination_body=4,
        .settlement_body=4, .settlement_days=3.5};
    const auto quote = *view.own_fleets.front().recovery;
    recovery.set_view(view);
    const auto click = [&](UiRect bounds) {
      return recovery.handle({InputEventType::LeftPressed,center(bounds)},1280,720,{},std::nullopt);
    };
    const auto hold = click(layout.recovery_left);
    require(hold.kind == FleetWorkspaceCommandKind::Recovery && hold.recovery_quote == quote &&
            hold.recovery_action == NativeCivilianRecoveryAction::Hold && !hold.confirm_abandon,
            "Hold click lost the displayed mission identity.");
    const auto initial = click(layout.recovery_right);
    require(initial.recovery_action == NativeCivilianRecoveryAction::ReturnToBase && !initial.confirm_abandon,
            "First return click authorized abandonment.");
    const NativeFleetOrderOutcome warning{false,
        "Returning this vessel will abandon its paid colony authorization with no refund. "
        "Current establishment progress: 3.5 days; all of it will be lost. "
        "Colonists remain aboard. Confirm return to continue.",0,true};
    recovery.set_recovery_result(quote,warning);
    DrawList warning_draw;
    recovery.render(warning_draw,1280,720,{});
    require(has_text(warning_draw,warning.message) && has_text(warning_draw,"CONFIRM RETURN") &&
            has_text(warning_draw,"CANCEL"), "Paid warning was truncated or cancellation unavailable.");
    const auto confirm_return = click(layout.recovery_left);
    require(confirm_return.confirm_abandon && confirm_return.recovery_quote == quote,
            "Confirmation lost its original quote.");
    require(click(layout.recovery_right).kind == FleetWorkspaceCommandKind::None &&
            !recovery.recovery_confirmation_open(), "Repeated return click confirmed instead of cancelling.");
    for(int change=0;change<5;++change) {
      recovery.set_view(view);
      recovery.set_recovery_result(quote,warning);
      auto replacement=view;
      if(change==0) replacement.selected_fleet_id=12;
      if(change==1) replacement.own_fleets.front().recovery->mission_order_revision++;
      if(change==2) replacement.own_fleets.front().recovery->settlement_days+=1.;
      if(change==3) replacement.own_fleets.front().recovery.reset();
      if(change==4) replacement.own_fleets.front().recovery->campaign_generation++;
      recovery.set_view(std::move(replacement));
      require(!recovery.recovery_confirmation_open(),"Changed mission retained confirmation.");
    }
    recovery.set_view(view);recovery.set_recovery_result(quote,warning);
    (void)recovery.handle({InputEventType::PointerCancelled},1280,720,{},std::nullopt);
    require(!recovery.recovery_confirmation_open(),"Focus loss retained destructive confirmation.");
    for(const auto [w,h]:std::array{std::pair{1280,720},std::pair{1920,1080},std::pair{3840,2160}}){
      const auto l=FleetWorkspaceLayout::for_viewport(w,h);
      require(contained(l.confirm,l.recovery_left)&&contained(l.confirm,l.recovery_right)&&
              l.recovery_left.x+l.recovery_left.width<l.recovery_right.x,
              "Recovery buttons overlap or escape the action row.");
    }
  }
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

  std::cout << "Native owned-fleet outliner, map hit, route preview, confirmation, "
               "empty-state and secrecy tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
