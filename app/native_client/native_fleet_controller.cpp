#include "native_fleet_controller.hpp"

#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/industry_allocation.hpp>

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
  const auto player = std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
  if (player == world.civilizations.end() || !player->is_player)
    throw std::runtime_error("The campaign has no valid player civilization.");
  return {runtime, simulation, world, world.player_civilization_id};
}

[[nodiscard]] FleetState *find_owned(PlayerContext &player, int fleet_id) {
  const auto found = std::ranges::find(player.world.fleets, fleet_id,
                                        &FleetState::id);
  return found != player.world.fleets.end() && found->is_active &&
                 found->civilization_id == player.player_id
             ? &*found
             : nullptr;
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

[[nodiscard]] std::optional<double> transit_days(
    const FreshCampaignState &world, const FleetState &fleet,
    double distance_light_years) {
  const auto funding =
      civilization_operating_funding(world.economies, fleet.civilization_id);
  const auto speed = fleet.strategic_speed * funding;
  if (!std::isfinite(speed) || speed <= 0.) return std::nullopt;
  return distance_light_years / speed;
}

} // namespace

void NativeFleetController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native fleet control must run on the simulation owner thread.");
}

void NativeFleetController::bind_generation(
    const std::uint64_t campaign_generation) {
  if (generation_ && campaign_generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace the current fleet view.");
  if (generation_ && *generation_ != campaign_generation)
    selected_fleet_id_.reset();
  generation_ = campaign_generation;
}

NativeFleetMapView NativeFleetController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation) {
  require_owner();
  bind_generation(campaign_generation);
  auto player = context(frame);
  const auto statuses = player.runtime.core().get_own_combat_fleet_status(
      &player.simulation, player.player_id);
  std::unordered_map<int, OwnCombatFleetStatus> status_by_id;
  status_by_id.reserve(statuses.fleets.size());
  for (const auto &status : statuses.fleets)
    status_by_id.emplace(status.fleet_id, status);

  NativeFleetMapView result;
  result.campaign_generation = campaign_generation;
  result.player_civilization_id = player.player_id;
  for (const auto &fleet : player.world.fleets) {
    if (!fleet.is_active || fleet.civilization_id != player.player_id) continue;
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

  if (selected_fleet_id_ && find_owned(player, *selected_fleet_id_))
    result.selected_fleet_id = selected_fleet_id_;
  else
    selected_fleet_id_.reset();
  return result;
}

NativeFleetSelectionOutcome NativeFleetController::select(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int fleet_id) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false, "The campaign changed; refresh fleets before selecting."};
  auto player = context(frame);
  const auto *fleet = find_owned(player, fleet_id);
  if (!fleet)
    return {false, "That owned fleet is no longer available."};
  selected_fleet_id_ = fleet->id;
  return {true, fleet->name + " selected."};
}

NativeFleetSelectionOutcome NativeFleetController::select_next_hit(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::span<const int> hit_fleet_ids) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false, "The campaign changed; refresh fleets before selecting."};
  auto player = context(frame);
  std::vector<int> owned_hits;
  owned_hits.reserve(hit_fleet_ids.size());
  for (const auto id : hit_fleet_ids)
    if (find_owned(player, id)) owned_hits.push_back(id);
  std::ranges::sort(owned_hits);
  owned_hits.erase(std::unique(owned_hits.begin(), owned_hits.end()),
                   owned_hits.end());
  if (owned_hits.empty())
    return {false, "No owned fleet is available at that position."};

  auto current = selected_fleet_id_
                     ? std::ranges::find(owned_hits, *selected_fleet_id_)
                     : owned_hits.end();
  const auto next = current == owned_hits.end() || ++current == owned_hits.end()
                        ? owned_hits.begin()
                        : current;
  const auto *fleet = find_owned(player, *next);
  selected_fleet_id_ = *next;
  return {true, fleet->name + " selected."};
}

void NativeFleetController::clear_selection() {
  require_owner();
  selected_fleet_id_.reset();
}

NativeFleetRoutePreview NativeFleetController::preview_selected_route(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int target_system_id) {
  require_owner();
  NativeFleetRoutePreview result;
  result.campaign_generation = campaign_generation;
  result.target_system_id = target_system_id;
  if (!generation_ || *generation_ != campaign_generation) {
    result.message = "The campaign changed; refresh fleets before previewing a route.";
    return result;
  }
  auto player = context(frame);
  if (!selected_fleet_id_) {
    result.message = "Select an owned fleet before previewing a route.";
    return result;
  }
  auto *fleet = find_owned(player, *selected_fleet_id_);
  if (!fleet) {
    selected_fleet_id_.reset();
    result.message = "The selected owned fleet is no longer available.";
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
    return {false, "The campaign changed; refresh fleets before issuing an order.", 0};
  auto player = context(frame);
  if (!selected_fleet_id_ || *selected_fleet_id_ != preview.fleet_id)
    return {false, "Fleet selection changed; preview the route again.", 0};
  auto *fleet = find_owned(player, preview.fleet_id);
  if (!fleet)
    return {false, "That owned fleet is no longer available.", 0};
  if (fleet->mission_order_revision != preview.expected_mission_order_revision)
    return {false, "Fleet orders changed; preview the route again.",
            fleet->mission_order_revision};
  if (!preview.command_available || !preview.route_supported)
    return {false, preview.message.empty() ? "That route is unavailable."
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
    return {false, "Travel orders are unavailable for this vessel.",
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

} // namespace stellar::native_fleet
