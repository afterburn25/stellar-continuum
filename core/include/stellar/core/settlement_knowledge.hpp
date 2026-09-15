#pragma once

#include <stellar/core/colony_biology.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/knowledge.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct KnownSpeciesPlanetarySuitability {
  int planetary_body_id{};
  int system_id{};
  std::string species_id;
  double natural_habitability{};
  double unprotected_operational_capacity{};
  EnvironmentalLimitingFactor limiting_factor{};
  SpeciesColonizationViability colonization_viability{};
  bool requires_gravity_mitigation{};
  bool requires_thermal_control{};
  bool requires_pressure_control{};
  bool requires_sealed_habitat{};
  bool requires_artificial_biosphere{};
  bool requires_radiation_shielding{};
};

struct FriendlyColonyMissionReservation {
  int system_id{};
  int fleet_id{};
};

struct FriendlyColonyReservationLookup {
  bool found{};
  int reserving_fleet_id{};
};

struct SettlementKnowledgeWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::span<const Colony> colonies;
  std::span<const FleetState> fleets;
  const CivilizationKnowledgeState &knowledge;
};

std::vector<KnownSpeciesPlanetarySuitability>
build_known_suitability_for_species(SettlementKnowledgeWorldView world,
                                    int observer_civilization_id,
                                    std::string_view species_id);

std::vector<KnownSpeciesPlanetarySuitability>
build_known_suitability_for_available_populations(
    SettlementKnowledgeWorldView world, int observer_civilization_id);

std::optional<int>
resolve_best_available_settlement_body(SettlementKnowledgeWorldView world,
                                       int civilization_id, int system_id,
                                       std::string_view species_id);

std::vector<FriendlyColonyMissionReservation>
build_friendly_colony_mission_reservations(SettlementKnowledgeWorldView world,
                                           const FleetState &requesting_fleet);

FriendlyColonyReservationLookup
try_get_friendly_reserving_fleet_id(SettlementKnowledgeWorldView world,
                                    const FleetState &requesting_fleet,
                                    int system_id);

} // namespace stellar::core
