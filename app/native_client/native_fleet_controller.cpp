#include "native_fleet_controller.hpp"
#include <stellar/core/campaign_observation.hpp>
#include <stellar/engine/localization.hpp>
#include <unordered_set>

#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/ship_designs.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::native_fleet {
namespace {
using namespace stellar::core;

struct PlayerContext {
  IntegratedAdaptiveCampaignRuntime &runtime;
  CampaignSimulationState &simulation;
  FreshCampaignState &world;
  int player_id{};
};

[[nodiscard]] PlayerContext context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &simulation = runtime.world();
  auto &world = simulation.campaign();
  if (std::ranges::count(world.civilizations, world.player_civilization_id,
                         &Civilization::id) != 1)
    throw std::runtime_error("The campaign has no unique player civilization.");
  const auto player = std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
  if (player == world.civilizations.end() || !player->is_player)
    throw std::runtime_error("The campaign has no valid player civilization.");
  return {runtime, simulation, world, world.player_civilization_id};
}

[[nodiscard]] FleetState *find_owned(PlayerContext &player, int fleet_id) {
  if (std::ranges::count(player.world.fleets, fleet_id, &FleetState::id) != 1)
    return nullptr;
  const auto found = std::ranges::find(player.world.fleets, fleet_id,
                                        &FleetState::id);
  return found != player.world.fleets.end() && found->is_active &&
                 found->civilization_id == player.player_id
             ? &*found
             : nullptr;
}

// Inspection is deliberately separate from find_owned, which remains the
// authority check for every command that changes a fleet.
[[nodiscard]] const FleetState *find_inspectable(PlayerContext &player, int id) {
  if (!developer_observation(player.world, player.player_id)) return find_owned(player, id);
  if (std::ranges::count(player.world.fleets, id, &FleetState::id) != 1) return nullptr;
  const auto found = std::ranges::find(player.world.fleets, id, &FleetState::id);
  return found != player.world.fleets.end() && found->is_active ? &*found : nullptr;
}

[[nodiscard]] InterstellarMissionKind mission_kind(FleetRole role) {
  switch (role) {
  case FleetRole::Scout: return InterstellarMissionKind::ScoutReconnaissance;
  case FleetRole::Science: return InterstellarMissionKind::ScienceSurvey;
  case FleetRole::Colony: return InterstellarMissionKind::Colony;
  case FleetRole::Military: return InterstellarMissionKind::MilitaryDeployment;
  case FleetRole::Logistics: return InterstellarMissionKind::Logistics;
  }
  return InterstellarMissionKind::MilitaryDeployment;
}

[[nodiscard]] bool tactical_for(const FreshCampaignState &world, const int fleet_id) {
  return world.active_combat_encounter && !world.active_combat_encounter->reconciled &&
      std::ranges::any_of(world.active_combat_encounter->vessels,
                          [fleet_id](const auto &binding) { return binding.fleet_id == fleet_id; });
}

[[nodiscard]] NativeMilitaryOrderQuote military_quote(
    const FleetState &fleet, const OwnCombatFleetStatus &status,
    const std::uint64_t generation, const int observer, const bool tactical) {
  return {generation, 0, observer, fleet.id, fleet.mission_order_revision, fleet.role,
          fleet.current_system_id, fleet.destination_system_id, status.defend_system_id,
          fleet.transit_phase, fleet.planned_route_system_ids,
          status.current_order, fleet.combat ? fleet.combat->target_fleet_id : std::nullopt,
          fleet.combat && fleet.combat->retreat_started,
          status.is_armed, status.is_combat_effective,
          status.is_disengaged, tactical};
}

[[nodiscard]] NativeCivilianRecoveryQuote recovery_quote(
    const FleetState &fleet, std::uint64_t generation, int observer) {
  return {generation, observer, fleet.id, fleet.mission_order_revision, fleet.role,
          fleet.hold_requested, fleet.return_to_base_requested,
          fleet.destination_system_id, fleet.destination_planetary_body_id,
          fleet.settlement_body_id, fleet.settlement_days_completed};
}

[[nodiscard]] std::optional<double> transit_days(
    const FreshCampaignState &world, const FleetState &fleet,
    double distance_light_years) {
  const auto funding =
      civilization_operating_funding(world.economies, fleet.civilization_id);
  const auto speed = fleet.strategic_speed * funding;
  if (!std::isfinite(speed) || speed <= 0.) return std::nullopt;
  return distance_light_years / speed;
}

[[nodiscard]] std::optional<NativeScoutReconnaissanceStatus>
scout_reconnaissance(const PlayerContext &player, const FleetState &fleet) {
  if (fleet.role != FleetRole::Scout || !fleet.current_system_id ||
      fleet.transit_phase != FleetTransitPhase::None ||
      fleet.destination_system_id)
    return std::nullopt;

  const auto system_id = *fleet.current_system_id;
  const auto level = player.world.knowledge.system_survey_level(
      fleet.civilization_id, system_id);
  const bool recorded_here = fleet.reconnaissance_system_id == system_id;
  if (level < SystemSurveyLevel::partially_surveyed) {
    return NativeScoutReconnaissanceStatus{
        recorded_here ? fleet.reconnaissance_days_completed : 0.,
        ExplorationSimulation::scout_reconnaissance_days, fleet.hold_requested,
        false, false};
  }
  // Knowledge alone does not establish that this scout completed it. Retain a
  // completion only when the fleet's canonical recorder identifies this system.
  if (!recorded_here) return std::nullopt;
  return NativeScoutReconnaissanceStatus{
      fleet.reconnaissance_days_completed,
      ExplorationSimulation::scout_reconnaissance_days, fleet.hold_requested,
      true, level >= SystemSurveyLevel::fully_surveyed};
}

[[nodiscard]] std::optional<NativeScienceSurveyStatus>
science_survey(const PlayerContext &player, const FleetState &fleet) {
  if (fleet.role != FleetRole::Science || !fleet.current_system_id ||
      fleet.transit_phase != FleetTransitPhase::None ||
      fleet.destination_system_id)
    return std::nullopt;
  const auto system_id = *fleet.current_system_id;
  const auto level = player.world.knowledge.system_survey_level(
      fleet.civilization_id, system_id);
  return NativeScienceSurveyStatus{
      std::clamp(player.world.knowledge.system_survey_progress(
                     fleet.civilization_id, system_id),
                 0., 1.),
      fleet.hold_requested, level >= SystemSurveyLevel::fully_surveyed};
}

} // namespace

void NativeFleetController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native fleet control must run on the simulation owner thread.");
}

std::string NativeFleetController::tr(std::string_view key,
                                      std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeFleetController::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale_->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

void NativeFleetController::bind_generation(
    const std::uint64_t campaign_generation) {
  if (generation_ && campaign_generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace the current fleet view.");
  if (generation_ && *generation_ != campaign_generation)
    selected_fleet_id_.reset(), military_order_quote_.reset();
  generation_ = campaign_generation;
}

NativeFleetMapView NativeFleetController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation) {
  require_owner();
  bind_generation(campaign_generation);
  auto player = context(frame);
  auto statuses = player.runtime.core().get_own_combat_fleet_status(
      &player.simulation, player.player_id);
  const bool developer = developer_observation(player.world, player.player_id);
  if (developer) for (const auto& civilization : player.world.civilizations) {
    if (civilization.id == player.player_id) continue;
    auto foreign = player.runtime.core().get_own_combat_fleet_status(&player.simulation, civilization.id);
    statuses.fleets.insert(statuses.fleets.end(), foreign.fleets.begin(), foreign.fleets.end());
  }
  std::unordered_map<int, OwnCombatFleetStatus> status_by_id;
  status_by_id.reserve(statuses.fleets.size());
  for (const auto &status : statuses.fleets)
    status_by_id.emplace(status.fleet_id, status);

  NativeFleetMapView result;
  result.campaign_generation = campaign_generation;
  result.player_civilization_id = player.player_id;
  result.developer_inspection = developer;
  std::unordered_map<int, unsigned> counts;
  for (const auto& fleet : player.world.fleets) ++counts[fleet.id];
  for (const auto &fleet : player.world.fleets) {
    if (!fleet.is_active || (!developer && fleet.civilization_id != player.player_id) || counts[fleet.id] != 1) continue;
    NativeOwnFleet item{
        .id = fleet.id,
        .name = fleet.name,
        .role = fleet.role,
        .design_id = fleet.design_id,
        .position = fleet.position,
        .current_system_id = fleet.current_system_id,
        .destination_system_id = fleet.destination_system_id,
        .transit_phase = fleet.transit_phase,
        .transit_progress = fleet.transit_progress,
        .planned_route_system_ids = fleet.planned_route_system_ids,
        .strategic_speed = fleet.strategic_speed,
        .maximum_leg_range_light_years = fleet.maximum_leg_range_light_years,
        .fuel_capacity_light_years = fleet.fuel_capacity_light_years,
        .fuel_remaining_light_years = fleet.fuel_remaining_light_years,
        .mission_order_revision = fleet.mission_order_revision,
        .combat_power = own_fleet_combat_power(fleet),
    };
    if (const auto status = status_by_id.find(fleet.id);
        status != status_by_id.end())
      item.combat_status = status->second;
    if (fleet.design_id)
      if (const auto *design = find_ship_design(*fleet.design_id))
        item.design_name = design->name;
    item.cargo_materials = fleet.cargo_materials;
    item.cargo_material_capacity = fleet.cargo_material_capacity;
    item.embarked_population_millions = fleet.embarked_population_millions;
    if (fleet.tactical_vessel) {
      item.has_vessel_state = true;
      item.hull_integrity = fleet.tactical_vessel->hull_fraction;
    }
    item.owner_civilization_id = fleet.civilization_id;
    item.foreign_inspection = fleet.civilization_id != player.player_id;
    const auto owner = std::ranges::find(player.world.civilizations, fleet.civilization_id, &Civilization::id);
    if (owner != player.world.civilizations.end()) item.owner_name = owner->name;
    if (item.foreign_inspection) item.recovery_message = trf("FLEET_MSG_DEV_INSPECTION", {item.owner_name}, "Developer inspection · {0} · Live fleet statistics");
    item.reconnaissance = scout_reconnaissance(player, fleet);
    item.science_survey = science_survey(player, fleet);
    result.own_fleets.push_back(std::move(item));
  }
  std::ranges::sort(result.own_fleets, {}, &NativeOwnFleet::id);

  std::set<int> own_fleet_ids;
  for (const auto &fleet : player.world.fleets)
    if (fleet.civilization_id == player.player_id)
      own_fleet_ids.insert(fleet.id);
  std::set<int> emitted_contacts;
  const auto observations = player.runtime.combat_intelligence();
  for (auto entry = observations.rbegin(); entry != observations.rend(); ++entry) {
    if (entry->observer_id != player.player_id ||
        own_fleet_ids.contains(entry->fleet_id) ||
        !emitted_contacts.insert(entry->fleet_id).second)
      continue;
    result.foreign_contacts.push_back({entry->fleet_id, entry->power,
                                       entry->observed_day, entry->evidence});
  }
  std::ranges::sort(result.foreign_contacts, {},
                    &NativeForeignFleetContact::fleet_id);

  if (selected_fleet_id_ && find_inspectable(player, *selected_fleet_id_))
    result.selected_fleet_id = selected_fleet_id_;
  else
    selected_fleet_id_.reset(), military_order_quote_.reset();
  if (result.selected_fleet_id) {
    auto selected = std::ranges::find(result.own_fleets, *result.selected_fleet_id,
                                     &NativeOwnFleet::id);
    if (selected != result.own_fleets.end() && !selected->foreign_inspection && is_civilian_role(selected->role)) {
      const auto *live = find_owned(player, selected->id);
      selected->recovery = recovery_quote(*live, campaign_generation, player.player_id);
      if (live->return_to_base_failure_reason)
        selected->recovery_message = *live->return_to_base_failure_reason;
      else if (live->return_to_base_requested)
        selected->recovery_message = tr("FLEET_MSG_RETURN_QUEUED", "Return to base queued. Routing uses actual fuel at the next system.");
      else
        // Route planning belongs to the explicit order, not a 10 Hz outliner
        // refresh. Core reports reachability and paid-work confirmation there.
        selected->recovery_message = tr("FLEET_MSG_RETURN_GUIDANCE", "Return to the nearest reachable owned refuelling settlement. Paid colony work requires confirmation.");
    }
    if (selected != result.own_fleets.end()) {
      const auto status = status_by_id.find(selected->id);
      const auto *live = find_owned(player, selected->id);
      const bool tactical_active = player.world.active_combat_encounter &&
                                   !player.world.active_combat_encounter->reconciled;
      if (live && status != status_by_id.end() && status->second.is_armed &&
          !tactical_active) {
        auto quote = military_quote(*live, status->second, campaign_generation,
                                    player.player_id, false);
        if (military_order_quote_) quote.token = military_order_quote_->token;
        if (!military_order_quote_ || *military_order_quote_ != quote) {
          if (next_military_quote_token_ == 0) throw std::overflow_error("Military quote token exhausted.");
          quote.token = next_military_quote_token_++;
          military_order_quote_ = quote;
        }
        selected->military_order_quote = military_order_quote_;
      } else military_order_quote_.reset();
      selected->locate = NativeFleetLocateQuote{campaign_generation, player.player_id,
                                                 selected->id, selected->mission_order_revision};
    }
  }
  return result;
}

NativeFleetSelectionOutcome NativeFleetController::select(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int fleet_id) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false, tr("FLEET_MSG_CAMPAIGN_SELECT", "The campaign changed; refresh fleets before selecting.")};
  auto player = context(frame);
  const auto *fleet = find_inspectable(player, fleet_id);
  if (!fleet)
    return {false, tr("FLEET_MSG_FLEET_GONE", "That owned fleet is no longer available.")};
  selected_fleet_id_ = fleet->id;
  military_order_quote_.reset();
  return {true, trf("FLEET_MSG_SELECTED", {fleet->name}, "{0} selected.")};
}

NativeFleetSelectionOutcome NativeFleetController::select_next_hit(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::span<const int> hit_fleet_ids) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false, tr("FLEET_MSG_CAMPAIGN_SELECT", "The campaign changed; refresh fleets before selecting.")};
  auto player = context(frame);
  std::vector<int> owned_hits;
  owned_hits.reserve(hit_fleet_ids.size());
  for (const auto id : hit_fleet_ids)
    if (find_inspectable(player, id)) owned_hits.push_back(id);
  std::ranges::sort(owned_hits);
  owned_hits.erase(std::unique(owned_hits.begin(), owned_hits.end()),
                   owned_hits.end());
  if (owned_hits.empty())
    return {false, tr("FLEET_MSG_NO_FLEET_POSITION", "No owned fleet is available at that position.")};

  auto current = selected_fleet_id_
                     ? std::ranges::find(owned_hits, *selected_fleet_id_)
                     : owned_hits.end();
  const auto next = current == owned_hits.end() || ++current == owned_hits.end()
                        ? owned_hits.begin()
                        : current;
  const auto *fleet = find_inspectable(player, *next);
  selected_fleet_id_ = *next;
  military_order_quote_.reset();
  return {true, trf("FLEET_MSG_SELECTED", {fleet->name}, "{0} selected.")};
}

void NativeFleetController::clear_selection() {
  require_owner();
  selected_fleet_id_.reset();
  military_order_quote_.reset();
}

NativeFleetRoutePreview NativeFleetController::preview_selected_route(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int target_system_id) {
  require_owner();
  NativeFleetRoutePreview result;
  result.campaign_generation = campaign_generation;
  result.target_system_id = target_system_id;
  if (!generation_ || *generation_ != campaign_generation) {
    result.message = tr("FLEET_MSG_CAMPAIGN_PREVIEW", "The campaign changed; refresh fleets before previewing a route.");
    return result;
  }
  auto player = context(frame);
  if (!selected_fleet_id_) {
    result.message = tr("FLEET_MSG_SELECT_FIRST", "Select an owned fleet before previewing a route.");
    return result;
  }
  auto *fleet = find_owned(player, *selected_fleet_id_);
  if (!fleet) {
    selected_fleet_id_.reset();
    result.message = tr("FLEET_MSG_SELECTED_GONE", "The selected owned fleet is no longer available.");
    return result;
  }
  result.fleet_id = fleet->id;
  result.expected_mission_order_revision = fleet->mission_order_revision;
  MissionReachAssessment reach;
  if (fleet->role == FleetRole::Scout || fleet->role == FleetRole::Science) {
    reach = ExplorationSimulation{}.assess_operational_reach(
        {player.world.systems, player.world.bodies, player.world.fleets,
         player.world.colonies, player.world.knowledge,
         player.simulation.lanes()},
        fleet->id, target_system_id);
  } else {
    reach = assess_operational_reach(
        {player.world.systems, player.world.colonies,
         player.simulation.lanes()},
        player.player_id, *fleet, target_system_id,
        mission_kind(fleet->role));
  }
  result.route_supported = reach.is_supported;
  result.route_authoritative = reach.is_authoritative;
  result.message = reach.reason;
  result.route_distance_light_years = reach.route_distance_light_years;
  if (reach.route_system_ids) result.route_system_ids = *reach.route_system_ids;
  if (!reach.is_supported) return result;
  result.estimated_transit_days =
      transit_days(player.world, *fleet, reach.route_distance_light_years);

  result.command_available = true;
  return result;
}

NativeFleetOrderOutcome NativeFleetController::issue_selected_route(
    CampaignFrame &frame, const NativeFleetRoutePreview &preview) {
  require_owner();
  if (!generation_ || *generation_ != preview.campaign_generation)
    return {false, tr("FLEET_MSG_CAMPAIGN_ORDER", "The campaign changed; refresh fleets before issuing an order."), 0};
  auto player = context(frame);
  if (!selected_fleet_id_ || *selected_fleet_id_ != preview.fleet_id)
    return {false, tr("FLEET_MSG_SELECTION_PREVIEW", "Fleet selection changed; preview the route again."), 0};
  auto *fleet = find_owned(player, preview.fleet_id);
  if (!fleet)
    return {false, tr("FLEET_MSG_FLEET_GONE", "That owned fleet is no longer available."), 0};
  if (fleet->mission_order_revision != preview.expected_mission_order_revision)
    return {false, tr("FLEET_MSG_ORDERS_PREVIEW", "Fleet orders changed; preview the route again."),
            fleet->mission_order_revision};
  if (!preview.command_available || !preview.route_supported)
    return {false, preview.message.empty() ? tr("FLEET_MSG_ROUTE_UNAVAILABLE", "That route is unavailable.")
                                           : preview.message,
            fleet->mission_order_revision};

  bool accepted{};
  std::string message;
  if (fleet->role == FleetRole::Scout || fleet->role == FleetRole::Science) {
    auto order = ExplorationSimulation{}.issue_travel_order(
        {player.world.systems, player.world.bodies, player.world.fleets,
         player.world.colonies, player.world.knowledge,
         player.simulation.lanes()},
        fleet->id, preview.target_system_id);
    accepted = order.accepted;
    message = std::move(order.message);
  } else if (fleet->role == FleetRole::Military) {
    auto order = player.runtime.core().issue_military_deployment_order(
        &player.simulation, player.player_id, fleet->id,
        preview.target_system_id);
    accepted = order.accepted;
    message = std::move(order.message);
  } else if (fleet->role == FleetRole::Logistics) {
    auto order = player.runtime.core().issue_freight_transit_order(
        &player.simulation, player.player_id, fleet->id,
        preview.target_system_id);
    accepted = order.accepted;
    message = std::move(order.message);
  } else if (fleet->role == FleetRole::Colony) {
    auto order = ColonizationSimulation{}.issue_transit_order(
        {player.world.systems, player.world.bodies, player.world.civilizations,
         player.world.colonies, player.world.fleets, player.world.economies,
         player.world.knowledge, player.simulation.lanes()},
        player.player_id, fleet->id, preview.target_system_id);
    accepted = order.accepted;
    message = std::move(order.message);
  } else {
    return {false, tr("FLEET_MSG_VESSEL_NO_TRAVEL", "Travel orders are unavailable for this vessel."),
            fleet->mission_order_revision};
  }
  const auto current = find_owned(player, preview.fleet_id);
  return {accepted, std::move(message),
          current ? current->mission_order_revision
                  : preview.expected_mission_order_revision};
}

std::optional<int> NativeFleetController::selection() const {
  require_owner();
  return selected_fleet_id_;
}

NativeFleetOrderOutcome NativeFleetController::issue_civilian_recovery(
    CampaignFrame &frame, const NativeCivilianRecoveryQuote &quote,
    NativeCivilianRecoveryAction action, bool confirm_abandon) {
  require_owner();
  if (!generation_ || *generation_ != quote.campaign_generation)
    return {false, tr("FLEET_MSG_CAMPAIGN_RECOVERY", "The campaign changed; review this recovery order again.")};
  auto player = context(frame);
  if (player.player_id != quote.observer_id || selected_fleet_id_ != quote.fleet_id)
    return {false, tr("FLEET_MSG_SELECTION_RECOVERY", "Fleet selection changed; review this recovery order again.")};
  auto *fleet = find_owned(player, quote.fleet_id);
  if (!fleet || !is_civilian_role(fleet->role))
    return {false, tr("FLEET_MSG_SELECT_CIVILIAN", "Select an active owned civilian mission ship.")};
  if (recovery_quote(*fleet, *generation_, player.player_id) != quote)
    return {false, tr("FLEET_MSG_MISSION_CHANGED", "The mission changed; pause and review the recovery order again."),
            fleet->mission_order_revision};
  const auto id = fleet->id;
  bool accepted{}, confirmation{};
  std::string message;
  if (action == NativeCivilianRecoveryAction::ReturnToBase) {
    const auto outcome = player.runtime.core().issue_civilian_return_to_base_order(
        &player.simulation, player.player_id, id, confirm_abandon);
    accepted = outcome.accepted;
    confirmation = outcome.requires_confirmation;
    message = outcome.message;
  } else if (action == NativeCivilianRecoveryAction::Hold ||
             action == NativeCivilianRecoveryAction::Resume) {
    const auto outcome = action == NativeCivilianRecoveryAction::Hold
        ? player.runtime.core().issue_civilian_hold_order(&player.simulation, player.player_id, id)
        : player.runtime.core().issue_civilian_resume_order(&player.simulation, player.player_id, id);
    accepted = outcome.accepted;
    message = outcome.message;
  } else return {false, tr("FLEET_MSG_UNKNOWN_RECOVERY", "Unknown civilian recovery action.")};
  const auto *current = find_owned(player, id);
  return {accepted, std::move(message), current ? current->mission_order_revision : 0,
          confirmation};
}

NativeFleetOrderOutcome NativeFleetController::issue_selected_military_order(
    CampaignFrame &frame, const NativeMilitaryOrderQuote &quote,
    const MilitaryOrderType type) {
  require_owner();
  if (type != MilitaryOrderType::Hold && type != MilitaryOrderType::Defend &&
      type != MilitaryOrderType::Retreat)
    return {false, tr("FLEET_MSG_UNKNOWN_MILITARY", "Unknown military order.")};
  if (!generation_ || *generation_ != quote.campaign_generation ||
      !military_order_quote_ || *military_order_quote_ != quote ||
      selected_fleet_id_ != quote.fleet_id)
    return {false, tr("FLEET_MSG_MILITARY_CHANGED", "The military order changed; refresh fleet details first.")};
  auto player = context(frame);
  if (player.player_id != quote.observer_id ||
      (player.world.active_combat_encounter && !player.world.active_combat_encounter->reconciled))
    return {false, tr("FLEET_MSG_TACTICAL_BLOCKED", "Strategic military orders are unavailable during tactical combat.")};
  auto *fleet = find_owned(player, quote.fleet_id);
  if (!fleet)
    return {false, tr("FLEET_MSG_SELECT_ARMED", "Select an active owned armed military fleet.")};
  auto statuses = player.runtime.core().get_own_combat_fleet_status(
      &player.simulation, player.player_id);
  const auto status = std::ranges::find(statuses.fleets, fleet->id,
                                        &OwnCombatFleetStatus::fleet_id);
  if (status == statuses.fleets.end() || !status->is_armed)
    return {false, tr("FLEET_MSG_SELECT_ARMED", "Select an active owned armed military fleet."), fleet->mission_order_revision};
  auto current = military_quote(*fleet, *status, *generation_, player.player_id,
                                tactical_for(player.world, fleet->id));
  current.token = quote.token;
  if (current != quote)
    return {false, tr("FLEET_MSG_ORDERS_DETAILS", "Fleet orders changed; refresh fleet details first."), fleet->mission_order_revision};
  const auto outcome = player.runtime.core().issue_military_order(
      &player.simulation, player.player_id, fleet->id,
      MilitaryOrder{type, {}, type == MilitaryOrderType::Defend ? fleet->current_system_id : std::nullopt});
  military_order_quote_.reset();
  const auto *after = find_owned(player, quote.fleet_id);
  return {outcome.accepted, outcome.message,
          after ? after->mission_order_revision : quote.mission_order_revision};
}

NativeFleetLocateOutcome NativeFleetController::locate_selected(
    CampaignFrame &frame, const NativeFleetLocateQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != quote.campaign_generation ||
      selected_fleet_id_ != quote.fleet_id)
    return {false, tr("FLEET_MSG_SELECTION_DETAILS", "The fleet selection changed; refresh fleet details first.")};
  auto player = context(frame);
  if (player.player_id != quote.observer_id)
    return {false, tr("FLEET_MSG_OBSERVER_DETAILS", "The fleet observer changed; refresh fleet details first.")};
  const auto *fleet = find_inspectable(player, quote.fleet_id);
  if (!fleet || fleet->mission_order_revision != quote.mission_order_revision)
    return {false, tr("FLEET_MSG_FLEET_MOVED", "That owned fleet is no longer at its displayed position.")};
  return {true, trf("FLEET_MSG_LOCATED", {fleet->name}, "{0} located."), fleet->id, fleet->current_system_id,
          fleet->position};
}

} // namespace stellar::native_fleet
