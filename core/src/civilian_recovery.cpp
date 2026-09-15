#include <stellar/core/civilian_recovery.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {
bool civilian_role(FleetRole role) {
  return role == FleetRole::Scout || role == FleetRole::Science ||
         role == FleetRole::Colony;
}

FleetState *find_controlled(CivilianRecoveryWorldView world,
                            int civilization_id, int fleet_id) {
  const auto found = std::find_if(
      world.fleets.begin(), world.fleets.end(), [&](const auto &fleet) {
        return fleet.id == fleet_id && fleet.is_active &&
               fleet.civilization_id == civilization_id &&
               civilian_role(fleet.role);
      });
  return found == world.fleets.end() ? nullptr : &*found;
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 int id) {
  const auto found =
      std::find_if(systems.begin(), systems.end(),
                   [=](const auto &system) { return system.id == id; });
  return found == systems.end() ? nullptr : &*found;
}

bool has_paid_commitment(const FleetState &fleet) {
  return fleet.role == FleetRole::Colony &&
         (fleet.destination_planetary_body_id || fleet.settlement_body_id ||
          fleet.settlement_days_completed > 0.0);
}

CivilianFleetReturnOrderResult paid_confirmation(const FleetState &fleet) {
  return {false, true,
          "Returning " + fleet.name +
              " will abandon its paid colony authorization with no refund. "
              "Current establishment progress: " +
              detail::legacy_custom_fixed(fleet.settlement_days_completed, 0,
                                          1) +
              " days; all of it will be lost. Colonists remain aboard. "
              "Confirm return to continue."};
}

struct BaseChoice {
  int system_id{};
  MissionReachAssessment reach;
};

std::optional<BaseChoice> nearest_base(CivilianRecoveryWorldView world,
                                       const FleetState &fleet) {
  std::unordered_set<int> seen;
  std::vector<int> candidates;
  for (const auto &colony : world.colonies)
    if (colony.civilization_id == fleet.civilization_id &&
        seen.insert(colony.system_id).second)
      candidates.push_back(colony.system_id);

  std::vector<BaseChoice> supported;
  for (const int system_id : candidates) {
    auto reach = assess_operational_reach(
        {world.systems, world.colonies, world.lanes}, fleet.civilization_id,
        fleet, system_id,
        fleet.role == FleetRole::Colony
            ? InterstellarMissionKind::Colony
            : InterstellarMissionKind::ScoutReconnaissance);
    if (reach.is_supported)
      supported.push_back({system_id, std::move(reach)});
  }
  if (supported.empty())
    return std::nullopt;
  std::stable_sort(supported.begin(), supported.end(),
                   [](const auto &first, const auto &second) {
    if (first.reach.route_distance_light_years !=
        second.reach.route_distance_light_years)
      return first.reach.route_distance_light_years <
             second.reach.route_distance_light_years;
    return first.system_id < second.system_id;
  });
  return supported.front();
}

CivilianFleetReturnOrderResult activate(CivilianRecoveryWorldView world,
                                        FleetState &fleet,
                                        bool accepted_queued_return) {
  if (!fleet.current_system_id)
    return {false, false, fleet.name +
                              " must finish its current lane before return "
                              "routing can be rechecked."};
  const auto choice = nearest_base(world, fleet);
  if (!choice) {
    if (!accepted_queued_return)
      return {false, false,
              "No owned refuelling settlement is reachable with the fleet's "
              "current fuel."};
    fleet.return_to_base_requested = false;
    fleet.return_to_base_failure_reason =
        "No owned refuelling settlement is reachable with current fuel.";
    fleet.hold_requested = true;
    return {false, false, fleet.name + " is holding safely: " +
                              *fleet.return_to_base_failure_reason};
  }

  if (choice->system_id == *fleet.current_system_id) {
    if (fleet.role == FleetRole::Colony)
      abandon_colony_mission_for_transit(&fleet);
    clear_fleet_route(fleet);
    fleet.return_to_base_requested = false;
    fleet.return_to_base_failure_reason.reset();
    return {true, false,
            fleet.name +
                " is already at the nearest owned refuelling settlement."};
  }

  if (fleet.role == FleetRole::Colony)
    abandon_colony_mission_for_transit(&fleet);
  assign_fleet_route({world.systems, world.colonies, world.lanes}, fleet,
                     choice->system_id, choice->reach);
  fleet.return_to_base_requested = true;
  const auto *system = find_system(world.systems, choice->system_id);
  return {true, false,
          fleet.name + " is returning to " + system->name + ". " +
              choice->reach.reason};
}
} // namespace

CivilianFleetHoldOrderResult hold_civilian_fleet(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id) {
  auto *fleet = find_controlled(world, civilization_id, fleet_id);
  if (!fleet)
    return {false,
            "No controllable active civilian mission ship with that identity "
            "is available."};
  if (fleet->hold_requested)
    return {false, fleet->name + " already has a hold order."};
  fleet->hold_requested = true;
  if (fleet->current_system_id) {
    const auto *current = find_system(world.systems, *fleet->current_system_id);
    return {true, fleet->name + " is holding at " +
                      (current ? current->name : "the current system") + "."};
  }
  const int next_id = !fleet->planned_route_system_ids.empty()
                          ? fleet->planned_route_system_ids.front()
                          : fleet->destination_system_id.value_or(-1);
  const auto *next = find_system(world.systems, next_id);
  return {true, fleet->name + " will hold after reaching " +
                    (next ? next->name : "the next system") + "."};
}

CivilianFleetHoldOrderResult resume_civilian_fleet(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id) {
  auto *fleet = find_controlled(world, civilization_id, fleet_id);
  if (!fleet)
    return {false,
            "No controllable active civilian mission ship with that identity "
            "is available."};
  if (!fleet->hold_requested)
    return {false, fleet->name +
                       " is already proceeding under its current orders."};
  fleet->hold_requested = false;
  fleet->return_to_base_failure_reason.reset();
  return {true, fleet->name + " resumed its existing orders."};
}

CivilianFleetReturnOrderResult preview_civilian_fleet_return(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id) {
  auto *fleet = find_controlled(world, civilization_id, fleet_id);
  if (!fleet)
    return {false, false,
            "No controllable active civilian mission ship with that identity "
            "is available."};
  if (has_paid_commitment(*fleet))
    return paid_confirmation(*fleet);
  if (!fleet->current_system_id)
    return {true, false,
            "Finish the current lane first; return routing will then be "
            "rechecked using actual fuel."};
  const auto choice = nearest_base(world, *fleet);
  if (!choice)
    return {false, false,
            "No owned refuelling settlement is reachable with the fleet's "
            "current fuel."};
  const auto *system = find_system(world.systems, choice->system_id);
  return {true, false, "Nearest reachable refuelling settlement: " +
                           system->name + ". " + choice->reach.reason};
}

CivilianFleetReturnOrderResult request_civilian_fleet_return(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id,
    bool confirm_abandon_colony_work) {
  auto *fleet = find_controlled(world, civilization_id, fleet_id);
  if (!fleet)
    return {false, false,
            "No controllable active civilian mission ship with that identity "
            "is available."};
  if (fleet->return_to_base_requested)
    return {false, false,
            fleet->name + " already has a return-to-base order."};
  if (has_paid_commitment(*fleet) && !confirm_abandon_colony_work)
    return paid_confirmation(*fleet);
  if (!fleet->current_system_id) {
    fleet->hold_requested = false;
    fleet->return_to_base_requested = true;
    fleet->return_to_base_failure_reason.reset();
    return {true, false,
            fleet->name +
                " will finish its current lane, then re-evaluate a safe "
                "return route."};
  }
  return activate(world, *fleet, false);
}

CivilianFleetReturnOrderResult activate_queued_civilian_return_at_system(
    CivilianRecoveryWorldView world, FleetState &fleet) {
  if (!fleet.return_to_base_requested)
    return {false, false, "No civilian return order is pending."};
  return activate(world, fleet, true);
}

void abandon_colony_mission_for_transit(FleetState *fleet) {
  if (!fleet)
    throw std::invalid_argument("Value cannot be null. (Parameter 'fleet')");
  fleet->destination_planetary_body_id.reset();
  fleet->settlement_body_id.reset();
  fleet->settlement_days_completed = 0;
  fleet->prevent_automatic_settlement = true;
}
} // namespace stellar::core
