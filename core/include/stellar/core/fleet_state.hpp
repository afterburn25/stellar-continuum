#pragma once

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/combat_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {
enum class FleetTransitPhase {
  None,
  LocalDeparture,
  InterstellarWarp,
  LocalArrival
};
struct FleetState {
  int id{}, civilization_id{};
  std::string name;
  FleetRole role{};
  std::optional<std::string> design_id;
  Vec2 position{};

  std::optional<int> current_system_id, destination_system_id;
  FleetTransitPhase transit_phase{};
  std::optional<int> transit_origin_system_id, transit_target_system_id;
  double transit_progress{};
  Vec2 local_transit_start{}, local_transit_position{}, local_transit_target{};
  std::vector<int> planned_route_system_ids;

  bool hold_requested{}, return_to_base_requested{};
  std::optional<std::string> return_to_base_failure_reason;
  int mission_order_revision{};
  std::optional<int> destination_planetary_body_id;
  bool prevent_automatic_settlement{};
  std::optional<int> settlement_body_id;
  double settlement_days_completed{};
  std::optional<int> reconnaissance_system_id;
  double reconnaissance_days_completed{};
  std::optional<int> freight_target_outpost_id, freight_home_colony_id;

  double cargo_material_capacity{}, cargo_materials{};
  double strategic_speed{22}, maximum_leg_range_light_years{360};
  double fuel_capacity_light_years{1000}, fuel_remaining_light_years{1000};
  float sensor_range{135};
  bool is_active{true};

  double embarked_population_millions{};
  std::optional<std::string> embarked_population_species_id;
  std::optional<FleetCombatState> combat;
  std::optional<MassiveCombatLoadout> tactical_loadout;
  std::optional<MassiveVesselState> tactical_vessel;
};
std::vector<EconomyFleetState>
economic_fleet_projection(std::span<const FleetState> fleets);
} // namespace stellar::core
