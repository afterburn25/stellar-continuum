#pragma once

#include <stellar/core/fleet_reach.hpp>

#include <span>
#include <string>

namespace stellar::core {

struct CivilianFleetHoldOrderResult {
  bool accepted{};
  std::string message;
};

struct CivilianFleetReturnOrderResult {
  bool accepted{}, requires_confirmation{};
  std::string message;
};

struct CivilianRecoveryWorldView {
  std::span<const StellarSystem> systems;
  std::span<const Colony> colonies;
  std::span<FleetState> fleets;
  InterstellarLaneNetwork &lanes;
};

CivilianFleetHoldOrderResult hold_civilian_fleet(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id);
CivilianFleetHoldOrderResult resume_civilian_fleet(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id);
CivilianFleetReturnOrderResult preview_civilian_fleet_return(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id);
CivilianFleetReturnOrderResult request_civilian_fleet_return(
    CivilianRecoveryWorldView world, int civilization_id, int fleet_id,
    bool confirm_abandon_colony_work = false);
CivilianFleetReturnOrderResult activate_queued_civilian_return_at_system(
    CivilianRecoveryWorldView world, FleetState &fleet);

void abandon_colony_mission_for_transit(FleetState *fleet);

} // namespace stellar::core
