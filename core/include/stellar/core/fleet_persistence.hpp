#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/fleet_state.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {

struct FleetCombatSaveDto {
  std::string profile_id;
  double shields{}, armor{}, hull{}, weapon_cooldown_remaining_days{};
  MilitaryOrderType order{MilitaryOrderType::Hold};
  std::optional<int> target_fleet_id, defend_system_id;
  double retreat_progress_days{};
  bool retreat_started{}, is_disengaged{};
  std::optional<int> disengaged_system_id;
};

struct FleetSaveDto {
  int id{}, civilization_id{};
  std::string name;
  FleetRole role{};
  std::optional<std::string> design_id;
  float x{}, y{};
  std::optional<int> current_system_id, destination_system_id;
  FleetTransitPhase transit_phase{};
  std::optional<int> transit_origin_system_id, transit_target_system_id;
  double transit_progress{};
  float local_transit_start_x{}, local_transit_start_y{};
  float local_transit_position_x{}, local_transit_position_y{};
  float local_transit_target_x{}, local_transit_target_y{};
  std::optional<std::vector<int>> planned_route_system_ids;
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
  double cargo_material_capacity{}, cargo_materials{}, strategic_speed{};
  double maximum_leg_range_light_years{}, fuel_capacity_light_years{};
  std::optional<double> fuel_remaining_light_years;
  float sensor_range{};
  bool is_active{true};
  std::optional<double> embarked_population_millions;
  std::optional<std::string> embarked_population_species_id;
  std::optional<FleetCombatSaveDto> combat;
  std::optional<MassiveCombatLoadout> tactical_loadout;
  std::optional<MassiveVesselState> tactical_vessel;
};

std::vector<FleetState> restore_fleet_dtos(
    std::span<const FleetSaveDto> dtos,
    std::span<const Civilization> civilizations, int save_format_version,
    bool restore_unserialized_shipbuilding_population);

// Matches ToFleetDtos: ensuring combat state may mutate each live fleet before
// later population-species validation fails. Returned DTOs own nested values.
std::vector<FleetSaveDto> capture_fleet_dtos(std::span<FleetState> fleets);

} // namespace stellar::core
