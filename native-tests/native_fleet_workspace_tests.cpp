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
  // Layout uses fractional pixels at 1440p/4K; tolerate float roundoff only.
  constexpr float epsilon = .01f;
  return inner.x + epsilon >= outer.x && inner.y + epsilon >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width + epsilon &&
         inner.y + inner.height <= outer.y + outer.height + epsilon;
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
                contained(layout.panel, layout.confirm) &&
                contained(layout.details, layout.order_hold) &&
                contained(layout.details, layout.order_defend) &&
                contained(layout.details, layout.order_retreat) &&
                contained(layout.details, layout.civilian_locate) &&
                contained(layout.confirm, layout.locate) &&
                contained(layout.confirm, layout.military_locate) &&
                contained(layout.confirm, layout.engage) &&
                layout.order_hold.x + layout.order_hold.width < layout.order_defend.x &&
                layout.order_defend.x + layout.order_defend.width < layout.order_retreat.x,
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

  NativeFleetWorkspace reconnaissance;
  auto recon_view = player_view(true);
  recon_view.own_fleets.front().reconnaissance = NativeScoutReconnaissanceStatus{
      1., 2., false, false, false};
  reconnaissance.set_view(recon_view);
  DrawList recon_draw;
  reconnaissance.render(recon_draw, 1280, 720, markers);
  require(has_text(recon_draw, "RECONNAISSANCE") &&
              has_text(recon_draw, "Work 1.0 / 2.0 work-days") &&
              !has_text(recon_draw, "Right-click a system"),
          "Local scout work did not replace idle travel guidance.");
  const auto progress_bar = std::ranges::find_if(recon_draw.overlay, [&](const auto &command) {
    const auto *fill = std::get_if<FilledRectangle>(&command);
    return fill && fill->bounds.height == 4.f && contained(layout.route, fill->bounds);
  });
  require(progress_bar != recon_draw.overlay.end(),
          "Reconnaissance progress bar escaped the fixed 720p route area.");
  recon_view.own_fleets.front().reconnaissance->held = true;
  reconnaissance.set_view(recon_view);
  DrawList held_recon_draw;
  reconnaissance.render(held_recon_draw, 1280, 720, markers);
  require(has_text(held_recon_draw, "Held; work paused"),
          "Held scout work did not explain its paused state.");
  recon_view.own_fleets.front().reconnaissance = NativeScoutReconnaissanceStatus{
      2., 2., false, true, false};
  reconnaissance.set_view(recon_view);
  DrawList completed_recon_draw;
  reconnaissance.render(completed_recon_draw, 1280, 720, markers);
  require(has_text(completed_recon_draw, "Rapid reconnaissance complete") &&
              has_text(completed_recon_draw, "Send a science vessel for a full survey"),
          "Completed reconnaissance omitted its science-survey next step.");
  recon_view.own_fleets.front().reconnaissance->fully_surveyed = true;
  reconnaissance.set_view(recon_view);
  DrawList fully_surveyed_draw;
  reconnaissance.render(fully_surveyed_draw, 1280, 720, markers);
  require(has_text(fully_surveyed_draw, "System fully surveyed") &&
              !has_text(fully_surveyed_draw, "Send a science vessel"),
          "Fully surveyed system encouraged redundant science work.");

  NativeFleetWorkspace science_survey;
  auto science_view = player_view(true);
  science_view.own_fleets.front().role = stellar::core::FleetRole::Science;
  science_view.own_fleets.front().science_survey = NativeScienceSurveyStatus{
      .progress = .55, .held = false, .completed = false};
  science_survey.set_view(science_view);
  DrawList science_draw;
  science_survey.render(science_draw, 1280, 720, markers);
  require(has_text(science_draw, "SCIENCE SURVEY") &&
              has_text(science_draw, "Full survey 55.0%") &&
              !has_text(science_draw, "Right-click a system"),
          "Science survey progress did not replace idle travel guidance.");
  const auto science_bar = std::ranges::find_if(science_draw.overlay, [&](const auto &command) {
    const auto *fill = std::get_if<FilledRectangle>(&command);
    return fill && fill->bounds.height == 4.f && contained(layout.route, fill->bounds);
  });
  require(science_bar != science_draw.overlay.end(),
          "Science survey bar escaped the fixed 720p route area.");
  science_view.own_fleets.front().science_survey->held = true;
  science_survey.set_view(science_view);
  DrawList held_science_draw;
  science_survey.render(held_science_draw, 1280, 720, markers);
  require(has_text(held_science_draw, "Held; work paused"),
          "Held science survey did not explain its paused state.");
  science_view.own_fleets.front().science_survey = NativeScienceSurveyStatus{
      .progress = 1., .held = false, .completed = true};
  science_survey.set_view(science_view);
  DrawList complete_science_draw;
  science_survey.render(complete_science_draw, 1280, 720, markers);
  require(has_text(complete_science_draw, "System fully surveyed") &&
              has_text(complete_science_draw, "Select a planet to review findings."),
          "Completed science survey did not direct the player to observed findings.");

  NativeFleetWorkspace engagement;
  auto armed = player_view(true);
  auto &warship = armed.own_fleets.front();
  warship.role = stellar::core::FleetRole::Military;
  warship.current_system_id = 0;
  warship.combat_status = stellar::core::OwnCombatFleetStatus{};
  warship.combat_status->is_armed = true;
  const auto engage_click = [&] {
    return engagement.handle({InputEventType::LeftPressed, center(layout.engage)},
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

  {
    NativeFleetWorkspace strategic;
    auto tactical = armed;
    auto &fleet = tactical.own_fleets.front();
    fleet.military_order_quote = NativeMilitaryOrderQuote{
        .campaign_generation=4,.token=9,.observer_id=0,.fleet_id=10,
        .mission_order_revision=fleet.mission_order_revision,
        .role=stellar::core::FleetRole::Military,.current_system_id=0,
        .armed=true,.combat_effective=true};
    fleet.locate = NativeFleetLocateQuote{.campaign_generation=4,.observer_id=0,
                                          .fleet_id=10,.mission_order_revision=fleet.mission_order_revision};
    const auto order_quote=*fleet.military_order_quote;
    const auto locate_quote=*fleet.locate;
    strategic.set_view(tactical);
    DrawList tactical_draw;
    strategic.render(tactical_draw,1280,720,{});
    require(has_text(tactical_draw,"HOLD")&&has_text(tactical_draw,"DEFEND")&&
                has_text(tactical_draw,"RETREAT")&&has_text(tactical_draw,"LOCATE")&&
                has_text(tactical_draw,"Current tactical order Hold"),
            "Eligible armed fleet did not show strategic choices and Locate.");
    strategic.set_notice("Persistent command result.",true);
    (void)strategic.handle({InputEventType::PointerMove,center(layout.order_hold)},1280,720,{},std::nullopt);
    DrawList help_draw;
    strategic.render(help_draw,1280,720,{});
    require(has_text(help_draw,"Hold changes combat stance; it does not stop travel")&&
                !has_text(help_draw,"Persistent command result."),
            "Hovered tactical help did not override persistent feedback.");
    auto command=strategic.handle({InputEventType::LeftPressed,center(layout.order_defend)},1280,720,{},std::nullopt);
    require(command.captured&&command.kind==FleetWorkspaceCommandKind::None,
            "Strategic order was issued before its release.");
    command=strategic.handle({InputEventType::LeftReleased,center(layout.order_defend)},1280,720,{},std::nullopt);
    require(command.kind==FleetWorkspaceCommandKind::MilitaryOrder&&
                command.military_order==stellar::core::MilitaryOrderType::Defend&&
                command.military_order_quote==order_quote,
            "Strategic order did not retain the displayed quote.");
    (void)strategic.handle({InputEventType::LeftPressed,center(layout.order_retreat)},1280,720,{},std::nullopt);
    auto changed=tactical;
    ++changed.own_fleets.front().military_order_quote->token;
    strategic.set_view(changed);
    command=strategic.handle({InputEventType::LeftReleased,center(layout.order_retreat)},1280,720,{},std::nullopt);
    require(command.kind==FleetWorkspaceCommandKind::None,
            "Changed military quote retained a pressed action.");
    (void)strategic.handle({InputEventType::LeftPressed,center(layout.military_locate)},1280,720,{},std::nullopt);
    command=strategic.handle({InputEventType::LeftReleased,center(layout.military_locate)},1280,720,{},std::nullopt);
    require(command.kind==FleetWorkspaceCommandKind::Locate&&command.locate_quote==locate_quote,
            "Locate did not retain its displayed quote.");
    (void)strategic.handle({InputEventType::LeftPressed,center(layout.order_hold)},1280,720,{},std::nullopt);
    (void)strategic.handle({InputEventType::PointerCancelled},1280,720,{},std::nullopt);
    command=strategic.handle({InputEventType::LeftReleased,center(layout.order_hold)},1280,720,{},std::nullopt);
    require(command.kind==FleetWorkspaceCommandKind::None,
            "Pointer cancellation retained an armed strategic action.");
    (void)strategic.handle({InputEventType::LeftPressed,center(layout.order_hold)},1280,720,{},std::nullopt);
    strategic.cancel_recovery();
    command=strategic.handle({InputEventType::LeftReleased,center(layout.order_hold)},1280,720,{},std::nullopt);
    require(command.kind==FleetWorkspaceCommandKind::None,
            "Public navigation cancellation retained an armed strategic action.");
  }

  auto revised = player_view(true);
  {
    NativeFleetWorkspace recovery;
    auto view = player_view(true);
    view.own_fleets.front().recovery = NativeCivilianRecoveryQuote{
        .campaign_generation=4, .observer_id=0, .fleet_id=10,
        .role=stellar::core::FleetRole::Colony, .destination_body=4,
        .settlement_body=4, .settlement_days=3.5};
    view.own_fleets.front().locate = NativeFleetLocateQuote{
        .campaign_generation=4,.observer_id=0,.fleet_id=10,
        .mission_order_revision=view.own_fleets.front().mission_order_revision};
    const auto quote = *view.own_fleets.front().recovery;
    const auto locate_quote = *view.own_fleets.front().locate;
    recovery.set_view(view);
    const auto click = [&](UiRect bounds) {
      return recovery.handle({InputEventType::LeftPressed,center(bounds)},1280,720,{},std::nullopt);
    };
    const auto hold = click(layout.recovery_left);
    require(hold.kind == FleetWorkspaceCommandKind::Recovery && hold.recovery_quote == quote &&
            hold.recovery_action == NativeCivilianRecoveryAction::Hold && !hold.confirm_abandon,
            "Hold click lost the displayed mission identity.");
    DrawList recovery_draw;
    recovery.render(recovery_draw,1280,720,{});
    require(has_text(recovery_draw,"LOCATE")&&has_text(recovery_draw,"RETURN TO BASE"),
            "Civilian recovery did not retain both recovery controls and Locate.");
    (void)recovery.handle({InputEventType::LeftPressed,center(layout.civilian_locate)},1280,720,{},std::nullopt);
    const auto locate=recovery.handle({InputEventType::LeftReleased,center(layout.civilian_locate)},1280,720,{},std::nullopt);
    require(locate.kind==FleetWorkspaceCommandKind::Locate&&locate.locate_quote==locate_quote,
            "Civilian Locate did not bind its displayed quote.");
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
