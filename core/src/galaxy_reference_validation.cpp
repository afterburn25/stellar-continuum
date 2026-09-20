#include <stellar/core/galaxy_reference_validation.hpp>

#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/colony_operations.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/surface_construction.hpp>
#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  if (value.empty()) return false;
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty()) return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value)) return false;
  return true;
}

[[noreturn]] void data_error(std::string message) {
  throw GalaxyReferenceValidationDataError(std::move(message));
}

void validate_encounter(GalaxyReferenceValidationView world) {
  if (!world.active_encounter) return;
  try {
    validate_campaign_massive_encounter(
        *world.active_encounter, {world.systems, world.fleets});
  } catch (const MassiveCombatStateError &error) {
    throw GalaxyReferenceValidationOperationError(error.what());
  } catch (const CampaignMassiveEncounterDataError &error) {
    throw GalaxyReferenceValidationDataError(error.what());
  } catch (const CampaignMassiveEncounterArgumentError &error) {
    throw GalaxyReferenceValidationArgumentError(error.what());
  } catch (const std::overflow_error &error) {
    throw GalaxyReferenceValidationOverflowError(error.what());
  }
}

void validate_combat_intelligence(GalaxyReferenceValidationView world) {
  if (world.combat_intelligence.size() > 4096)
    data_error(
        "Campaign combat intelligence is invalid, duplicated, or unbounded.");
  std::unordered_set<std::uint64_t> identities;
  for (const auto &observation : world.combat_intelligence) {
    const auto observer = static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(observation.observer_id));
    const auto fleet = static_cast<std::uint64_t>(
        static_cast<std::uint32_t>(observation.fleet_id));
    if (!identities.insert((observer << 32) | fleet).second ||
        observation.observer_id < 0 ||
        std::ranges::none_of(world.fleets, [&](const FleetState &candidate) {
          return candidate.id == observation.fleet_id;
        }) ||
        !std::isfinite(observation.power) || observation.power < 0 ||
        !std::isfinite(observation.observed_day) ||
        observation.observed_day < 0 || blank(observation.evidence))
      data_error(
          "Campaign combat intelligence is invalid, duplicated, or unbounded.");
  }
}

void validate_surface_economies(GalaxyReferenceValidationView world) {
  const auto has_surface =
      std::ranges::any_of(world.colonies, [](const Colony &colony) {
        return !colony.surface_buildings.empty();
      });
  if (!has_surface) return;
  for (const auto &economy : world.economies)
    if (!std::isfinite(economy.credits) || economy.credits < 0 ||
        !std::isfinite(economy.industry) || economy.industry < 0 ||
        !std::isfinite(economy.science) || economy.science < 0)
      data_error("Civilization " + std::to_string(economy.civilization_id) +
                 " has invalid economy stock; resources must be finite and "
                 "nonnegative.");
  for (const auto &civilization : world.civilizations) {
    const auto count = std::ranges::count_if(
        world.economies, [&](const CivilizationEconomy &economy) {
          return economy.civilization_id == civilization.id;
        });
    if (count != 1)
      data_error(
          "Surface construction requires one authoritative economy for each "
          "civilization.");
  }
}

void validate_surface(const Colony &colony) {
  try {
    validate_surface_construction(colony);
  } catch (const std::runtime_error &error) {
    throw GalaxyReferenceValidationDataError(error.what());
  }
}

void validate_loadout(const MassiveCombatLoadout &loadout) {
  try {
    loadout.validate();
  } catch (const std::invalid_argument &error) {
    throw GalaxyReferenceValidationOperationError(error.what());
  } catch (const std::overflow_error &error) {
    throw GalaxyReferenceValidationOverflowError(error.what());
  }
}

void validate_vessel(const MassiveVesselState &vessel) {
  try {
    vessel.validate();
  } catch (const std::invalid_argument &error) {
    throw GalaxyReferenceValidationOperationError(error.what());
  }
}

} // namespace

void validate_galaxy_references(GalaxyReferenceValidationView world) {
  validate_encounter(world);
  validate_combat_intelligence(world);
  validate_surface_economies(world);

  std::unordered_map<int, const PlanetaryBody *> bodies;
  bodies.reserve(world.bodies.size());
  for (const auto &body : world.bodies)
    if (!bodies.emplace(body.id, &body).second)
      throw GalaxyReferenceValidationArgumentError(
          "An item with the same key has already been added. Key: " +
          std::to_string(body.id));
  std::unordered_set<int> system_ids;
  system_ids.reserve(world.systems.size());
  for (const auto &system : world.systems) system_ids.insert(system.id);

  for (const auto &colony : world.colonies) {
    if (colony.kind != SettlementKind::Colony &&
        colony.kind != SettlementKind::ResourceOutpost)
      data_error("Settlement " + std::to_string(colony.id) +
                 " has an unknown settlement kind.");
    if (!std::isfinite(colony.stored_extracted_materials) ||
        colony.stored_extracted_materials < 0)
      data_error("Settlement " + std::to_string(colony.id) +
                 " has invalid extracted-material storage.");
    if (colony.remaining_extractable_materials &&
        (!std::isfinite(*colony.remaining_extractable_materials) ||
         *colony.remaining_extractable_materials < 0))
      data_error("Settlement " + std::to_string(colony.id) +
                 " has an invalid remaining resource deposit.");
    if (colony.surface_hub_level < 0 || colony.surface_hub_level > 3)
      data_error("Settlement " + std::to_string(colony.id) +
                 " has an invalid surface hub level.");
    if (!std::isfinite(colony.stored_food_population_days_millions) ||
        colony.stored_food_population_days_millions < 0 ||
        !std::isfinite(colony.stored_water_population_days_millions) ||
        colony.stored_water_population_days_millions < 0)
      data_error("Settlement " + std::to_string(colony.id) +
                 " has invalid food or potable-water reserves.");
    validate_surface(colony);
    if (colony.surface_buildings.size() >
        static_cast<std::size_t>(surface_building_capacity(colony)))
      data_error("Settlement " + std::to_string(colony.id) +
                 " exceeds its represented hub module capacity.");
    if (!colony.planetary_body_id) {
      if (!colony.surface_buildings.empty())
        data_error("Colony " + std::to_string(colony.id) +
                   " has surface buildings without an exact planetary body.");
      continue;
    }

    const auto body = bodies.find(*colony.planetary_body_id);
    if (body == bodies.end() || body->second->system_id != colony.system_id)
      data_error("Colony " + std::to_string(colony.id) +
                 " references planetary body " +
                 std::to_string(*colony.planetary_body_id) +
                 " outside system " + std::to_string(colony.system_id) + ".");
    if (!colony.surface_buildings.empty() &&
        !body->second->environment.has_solid_surface)
      data_error("Colony " + std::to_string(colony.id) +
                 " has buildings on a body without solid ground.");

    ResourceOutpostOperationsSnapshot operations;
    try {
      operations = resource_outpost_snapshot(world.bodies, world.economies,
                                             colony);
    } catch (const std::invalid_argument &error) {
      throw GalaxyReferenceValidationOperationError(error.what());
    } catch (const std::out_of_range &) {
      throw GalaxyReferenceValidationRangeError(
          "Operating funding fraction must be finite and between zero and "
          "one. (Parameter 'operatingFundingFraction')");
    }
    if (operations.is_resource_outpost &&
        colony.stored_extracted_materials >
            operations.storage_capacity + .000001)
      data_error("Settlement " + std::to_string(colony.id) +
                 " stores more extracted material than its represented "
                 "capacity.");
    if (operations.is_resource_outpost &&
        colony.remaining_extractable_materials &&
        *colony.remaining_extractable_materials +
                colony.stored_extracted_materials >
            operations.initial_deposit_materials + .000001)
      data_error("Settlement " + std::to_string(colony.id) +
                 " has more remaining and stored material than its "
                 "represented deposit.");
  }

  for (const auto &fleet : world.fleets) {
    if (fleet.tactical_loadout) validate_loadout(*fleet.tactical_loadout);
    if (fleet.tactical_vessel) validate_vessel(*fleet.tactical_vessel);
    if (fleet.tactical_vessel &&
        fleet.tactical_vessel->id != campaign_vessel_id_for_fleet(fleet.id))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has tactical state for a different vessel identity.");
    if (fleet.design_id) {
      const auto *design = find_ship_design(*fleet.design_id);
      if (!design || design->role != fleet.role)
        data_error("Fleet " + std::to_string(fleet.id) +
                   " references an unknown or role-incompatible ship design.");
    }
    if (!std::isfinite(fleet.maximum_leg_range_light_years) ||
        fleet.maximum_leg_range_light_years <= 0)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has an invalid maximum interstellar leg range.");
    if (!std::isfinite(fleet.fuel_capacity_light_years) ||
        fleet.fuel_capacity_light_years <= 0 ||
        !std::isfinite(fleet.fuel_remaining_light_years) ||
        fleet.fuel_remaining_light_years < 0 ||
        fleet.fuel_remaining_light_years >
            fleet.fuel_capacity_light_years + .000001)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has invalid interstellar fuel endurance.");
    if (!std::isfinite(fleet.cargo_material_capacity) ||
        fleet.cargo_material_capacity < 0 ||
        !std::isfinite(fleet.cargo_materials) || fleet.cargo_materials < 0 ||
        fleet.cargo_materials > fleet.cargo_material_capacity + .000001)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has invalid freight cargo state.");
    if ((fleet.freight_target_outpost_id || fleet.freight_home_colony_id ||
         fleet.cargo_materials > 0) &&
        fleet.role != FleetRole::Logistics)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " carries freight mission state without a logistics role.");
    if (fleet.freight_target_outpost_id &&
        std::ranges::none_of(world.colonies, [&](const Colony &candidate) {
          return candidate.id == *fleet.freight_target_outpost_id &&
                 candidate.civilization_id == fleet.civilization_id &&
                 candidate.kind == SettlementKind::ResourceOutpost;
        }))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " references an invalid freight outpost.");
    if (fleet.freight_home_colony_id &&
        std::ranges::none_of(world.colonies, [&](const Colony &candidate) {
          return candidate.id == *fleet.freight_home_colony_id &&
                 candidate.civilization_id == fleet.civilization_id &&
                 candidate.kind == SettlementKind::Colony;
        }))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " references an invalid freight home colony.");
    if (std::ranges::any_of(fleet.planned_route_system_ids, [&](int id) {
          return !system_ids.contains(id);
        }))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has a route waypoint outside the generated galaxy.");
    if (!fleet.destination_system_id &&
        !fleet.planned_route_system_ids.empty())
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has route waypoints without an active destination.");
    if (!fleet.planned_route_system_ids.empty() &&
        fleet.planned_route_system_ids.back() != fleet.destination_system_id)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " route does not end at its mission destination.");
    const auto civilian = fleet.role == FleetRole::Scout ||
                          fleet.role == FleetRole::Science ||
                          fleet.role == FleetRole::Colony;
    if (fleet.hold_requested && fleet.is_active && !civilian)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has an unsupported civilian hold order.");
    if ((fleet.return_to_base_requested ||
         fleet.return_to_base_failure_reason) &&
        fleet.is_active && !civilian)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has an unsupported civilian return order.");

    if (!std::isfinite(fleet.settlement_days_completed) ||
        fleet.settlement_days_completed < 0 ||
        fleet.settlement_days_completed >
            ColonizationSimulation::establishment_days(fleet) ||
        !std::isfinite(fleet.reconnaissance_days_completed) ||
        fleet.reconnaissance_days_completed < 0 ||
        fleet.reconnaissance_days_completed >
            ExplorationSimulation::scout_reconnaissance_days)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has invalid local-work progress.");
    if ((!fleet.settlement_body_id && fleet.settlement_days_completed > 0) ||
        (!fleet.reconnaissance_system_id &&
         fleet.reconnaissance_days_completed > 0) ||
        (fleet.prevent_automatic_settlement &&
         (fleet.role != FleetRole::Colony || fleet.settlement_body_id)))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has local work without a valid order.");
    if (fleet.settlement_body_id) {
      const auto site = bodies.find(*fleet.settlement_body_id);
      if (fleet.role != FleetRole::Colony || site == bodies.end() ||
          fleet.current_system_id != site->second->system_id ||
          fleet.destination_system_id)
        data_error("Fleet " + std::to_string(fleet.id) +
                   " has an invalid settlement work site.");
    }
    if (fleet.reconnaissance_system_id &&
        (fleet.role != FleetRole::Scout ||
         !system_ids.contains(*fleet.reconnaissance_system_id)))
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has an invalid reconnaissance work site.");
    if (!fleet.destination_planetary_body_id) continue;

    const auto target_system = fleet.destination_system_id
        ? fleet.destination_system_id
        : (fleet.settlement_body_id == fleet.destination_planetary_body_id
               ? fleet.current_system_id
               : std::nullopt);
    if (fleet.role != FleetRole::Colony || !target_system)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " has a planetary-body target without an active "
                 "colony-system destination.");
    const auto body = bodies.find(*fleet.destination_planetary_body_id);
    if (body == bodies.end() || body->second->system_id != *target_system)
      data_error("Fleet " + std::to_string(fleet.id) +
                 " targets planetary body " +
                 std::to_string(*fleet.destination_planetary_body_id) +
                 " outside destination system " +
                 std::to_string(*target_system) + ".");
  }
}

} // namespace stellar::core
