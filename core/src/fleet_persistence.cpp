#include <stellar/core/fleet_persistence.hpp>

#include <stellar/core/combat_state.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
bool known_species(const std::string &id) {
  const auto profiles = species_environment_profiles();
  return std::any_of(profiles.begin(), profiles.end(),
                     [&](const auto &profile) { return profile.id == id; });
}
bool blank(const std::optional<std::string> &value) {
  return !value || value->find_first_not_of(" \t\r\n") == std::string::npos;
}
std::string require_population_species(const std::optional<std::string> &id,
                                       const std::string &owner) {
  if (blank(id) || !known_species(*id))
    throw std::runtime_error(owner +
                             " references unknown population species ID '" +
                             (id ? *id : std::string{}) + "'.");
  return *id;
}
std::string resolve_population_species(
    const std::optional<std::string> &saved, int civilization_id,
    std::span<const Civilization> civilizations, int version,
    const std::string &owner) {
  if (version >= 8)
    return require_population_species(saved, owner);
  const auto civilization = std::find_if(
      civilizations.begin(), civilizations.end(), [&](const auto &value) {
        return value.id == civilization_id;
      });
  if (civilization == civilizations.end())
    throw std::runtime_error("Population state references unknown civilization " +
                             std::to_string(civilization_id) + ".");
  return require_population_species(civilization->species_id,
                                    "civilization " +
                                        std::to_string(civilization_id));
}
double source_max_zero(double value) {
  return std::isnan(value) ? value : std::max(0.0, value);
}
FleetCombatState restore_combat(const FleetCombatSaveDto &value) {
  return {value.profile_id,
          value.shields,
          value.armor,
          value.hull,
          value.weapon_cooldown_remaining_days,
          value.order,
          value.target_fleet_id,
          value.defend_system_id,
          value.retreat_progress_days,
          value.retreat_started,
          value.is_disengaged,
          value.disengaged_system_id};
}
FleetCombatSaveDto capture_combat(const FleetCombatState &value) {
  return {value.profile_id,
          value.shields,
          value.armor,
          value.hull,
          value.weapon_cooldown_remaining_days,
          value.order,
          value.target_fleet_id,
          value.defend_system_id,
          value.retreat_progress_days,
          value.retreat_started,
          value.is_disengaged,
          value.disengaged_system_id};
}
} // namespace

std::vector<FleetState> restore_fleet_dtos(
    std::span<const FleetSaveDto> dtos,
    std::span<const Civilization> civilizations, int save_format_version,
    bool restore_unserialized_shipbuilding_population) {
  const auto designs = ship_design_catalog();
  const auto colony = std::find_if(designs.begin(), designs.end(),
                                   [](const auto &value) {
                                     return value.role == FleetRole::Colony;
                                   });
  const double colony_population =
      colony == designs.end() ? 0.0 : colony->population_cost_millions;
  std::vector<FleetState> fleets;
  fleets.reserve(dtos.size());
  for (const auto &dto : dtos) {
    const auto phase = static_cast<int>(dto.transit_phase);
    if (phase < 0 || phase > 3 || !std::isfinite(dto.transit_progress) ||
        dto.transit_progress < 0 || dto.transit_progress > 1 ||
        !std::isfinite(dto.local_transit_start_x) ||
        !std::isfinite(dto.local_transit_start_y) ||
        !std::isfinite(dto.local_transit_position_x) ||
        !std::isfinite(dto.local_transit_position_y) ||
        !std::isfinite(dto.local_transit_target_x) ||
        !std::isfinite(dto.local_transit_target_y))
      throw std::runtime_error("Fleet " + std::to_string(dto.id) +
                               " contains invalid persisted transit state.");
    const double population = source_max_zero(
        dto.embarked_population_millions.value_or(
            restore_unserialized_shipbuilding_population &&
                    dto.role == FleetRole::Colony
                ? colony_population
                : 0.0));
    const auto species = population > 0
                             ? std::optional(resolve_population_species(
                                   dto.embarked_population_species_id,
                                   dto.civilization_id, civilizations,
                                   save_format_version,
                                   "fleet " + std::to_string(dto.id)))
                             : std::nullopt;
    if(dto.stellar_transit_path.size()>132)throw std::runtime_error("Stellar transit path exceeds route limit.");
    for(const auto& p:dto.stellar_transit_path)if(!std::isfinite(p[0])||!std::isfinite(p[1]))throw std::runtime_error("Invalid stellar transit waypoint.");
    FleetState fleet;
    fleet.stellar_transit_path=dto.stellar_transit_path;
    fleet.id = dto.id;
    fleet.civilization_id = dto.civilization_id;
    fleet.name = dto.name;
    fleet.role = dto.role;
    fleet.design_id = dto.design_id;
    fleet.position = {dto.x, dto.y};
    fleet.current_system_id = dto.current_system_id;
    fleet.destination_system_id = dto.destination_system_id;
    fleet.transit_phase = dto.transit_phase;
    fleet.transit_origin_system_id = dto.transit_origin_system_id;
    fleet.transit_target_system_id = dto.transit_target_system_id;
    fleet.transit_progress = dto.transit_progress;
    fleet.local_transit_start = {dto.local_transit_start_x,
                                 dto.local_transit_start_y};
    fleet.local_transit_position = {dto.local_transit_position_x,
                                    dto.local_transit_position_y};
    fleet.local_transit_target = {dto.local_transit_target_x,
                                  dto.local_transit_target_y};
    fleet.planned_route_system_ids =
        dto.planned_route_system_ids.value_or(std::vector<int>{});
    fleet.hold_requested = dto.hold_requested;
    fleet.return_to_base_requested = dto.return_to_base_requested;
    fleet.return_to_base_failure_reason = dto.return_to_base_failure_reason;
    fleet.mission_order_revision = dto.mission_order_revision;
    fleet.destination_planetary_body_id =
        save_format_version >= 8 ? dto.destination_planetary_body_id
                                 : std::nullopt;
    fleet.prevent_automatic_settlement = dto.prevent_automatic_settlement;
    fleet.settlement_body_id = dto.settlement_body_id;
    fleet.settlement_days_completed = dto.settlement_days_completed;
    fleet.reconnaissance_system_id = dto.reconnaissance_system_id;
    fleet.reconnaissance_days_completed = dto.reconnaissance_days_completed;
    fleet.freight_target_outpost_id = dto.freight_target_outpost_id;
    fleet.freight_home_colony_id = dto.freight_home_colony_id;
    fleet.cargo_material_capacity = dto.cargo_material_capacity;
    fleet.cargo_materials = dto.cargo_materials;
    fleet.strategic_speed = dto.strategic_speed;
    fleet.maximum_leg_range_light_years =
        dto.maximum_leg_range_light_years > 0
            ? dto.maximum_leg_range_light_years
            : 360.0;
    fleet.fuel_capacity_light_years =
        dto.fuel_capacity_light_years > 0 ? dto.fuel_capacity_light_years
                                          : 1000.0;
    fleet.fuel_remaining_light_years = dto.fuel_remaining_light_years.value_or(
        dto.fuel_capacity_light_years > 0 ? dto.fuel_capacity_light_years
                                          : 1000.0);
    fleet.sensor_range = dto.sensor_range;
    fleet.is_active = dto.is_active;
    fleet.embarked_population_millions = population;
    fleet.embarked_population_species_id = species;
    if (dto.combat)
      fleet.combat = restore_combat(*dto.combat);
    fleet.tactical_loadout = dto.tactical_loadout;
    fleet.tactical_vessel = dto.tactical_vessel;
    if (!fleet.current_system_id && fleet.destination_system_id &&
        fleet.transit_phase == FleetTransitPhase::None) {
      fleet.transit_phase = FleetTransitPhase::InterstellarWarp;
      fleet.transit_target_system_id =
          fleet.planned_route_system_ids.empty()
              ? fleet.destination_system_id
              : std::optional(fleet.planned_route_system_ids.front());
    }
    if (fleet.transit_phase == FleetTransitPhase::InterstellarWarp &&
        !fleet.transit_target_system_id)
      throw std::runtime_error("Fleet " + std::to_string(dto.id) +
                               " contains a warp phase without a target.");
    if ((fleet.transit_phase == FleetTransitPhase::LocalDeparture ||
         fleet.transit_phase == FleetTransitPhase::LocalArrival) &&
        !fleet.current_system_id)
      throw std::runtime_error("Fleet " + std::to_string(dto.id) +
                               " contains local transit without a current system.");
    ensure_fleet_combat_state(fleet);
    fleets.push_back(std::move(fleet));
  }
  return fleets;
}

std::vector<FleetSaveDto> capture_fleet_dtos(std::span<FleetState> fleets) {
  std::vector<FleetSaveDto> result;
  result.reserve(fleets.size());
  for (auto &fleet : fleets) {
    const double population =
        source_max_zero(fleet.embarked_population_millions);
    const auto &combat = ensure_fleet_combat_state(fleet);
    FleetSaveDto dto;
    dto.id = fleet.id; dto.civilization_id = fleet.civilization_id;
    dto.name = fleet.name; dto.role = fleet.role; dto.design_id = fleet.design_id;
    dto.x = fleet.position.x; dto.y = fleet.position.y;
    dto.current_system_id = fleet.current_system_id;
    dto.destination_system_id = fleet.destination_system_id;
    dto.transit_phase = fleet.transit_phase;
    dto.transit_origin_system_id = fleet.transit_origin_system_id;
    dto.transit_target_system_id = fleet.transit_target_system_id;
    dto.transit_progress = fleet.transit_progress;
    dto.local_transit_start_x = fleet.local_transit_start.x;
    dto.local_transit_start_y = fleet.local_transit_start.y;
    dto.local_transit_position_x = fleet.local_transit_position.x;
    dto.local_transit_position_y = fleet.local_transit_position.y;
    dto.stellar_transit_path=fleet.stellar_transit_path;
    dto.local_transit_target_x = fleet.local_transit_target.x;
    dto.local_transit_target_y = fleet.local_transit_target.y;
    dto.planned_route_system_ids = fleet.planned_route_system_ids;
    dto.hold_requested = fleet.hold_requested;
    dto.return_to_base_requested = fleet.return_to_base_requested;
    dto.return_to_base_failure_reason = fleet.return_to_base_failure_reason;
    dto.mission_order_revision = fleet.mission_order_revision;
    dto.destination_planetary_body_id = fleet.destination_planetary_body_id;
    dto.prevent_automatic_settlement = fleet.prevent_automatic_settlement;
    dto.settlement_body_id = fleet.settlement_body_id;
    dto.settlement_days_completed = fleet.settlement_days_completed;
    dto.reconnaissance_system_id = fleet.reconnaissance_system_id;
    dto.reconnaissance_days_completed = fleet.reconnaissance_days_completed;
    dto.freight_target_outpost_id = fleet.freight_target_outpost_id;
    dto.freight_home_colony_id = fleet.freight_home_colony_id;
    dto.cargo_material_capacity = fleet.cargo_material_capacity;
    dto.cargo_materials = fleet.cargo_materials;
    dto.strategic_speed = fleet.strategic_speed;
    dto.maximum_leg_range_light_years = fleet.maximum_leg_range_light_years;
    dto.fuel_capacity_light_years = fleet.fuel_capacity_light_years;
    dto.fuel_remaining_light_years = fleet.fuel_remaining_light_years;
    dto.sensor_range = fleet.sensor_range; dto.is_active = fleet.is_active;
    dto.embarked_population_millions = population;
    dto.embarked_population_species_id =
        population > 0 ? std::optional(require_population_species(
                             fleet.embarked_population_species_id,
                             "fleet " + std::to_string(fleet.id)))
                       : std::nullopt;
    dto.combat = capture_combat(combat);
    dto.tactical_loadout = fleet.tactical_loadout;
    dto.tactical_vessel = fleet.tactical_vessel;
    result.push_back(std::move(dto));
  }
  return result;
}

} // namespace stellar::core
