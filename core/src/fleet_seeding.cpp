#include <stellar/core/fleet_seeding.hpp>

#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stellar::core {
namespace {
const StellarSystem &home_system(std::span<const StellarSystem> systems,
                                 int home_system_id) {
  const auto found =
      std::find_if(systems.begin(), systems.end(), [=](const auto &system) {
        return system.id == home_system_id;
      });
  if (found == systems.end())
    throw std::invalid_argument("Sequence contains no matching element");
  return *found;
}

const Civilization &
civilization_by_id(std::span<const Civilization> civilizations,
                   int civilization_id) {
  const auto found = std::find_if(civilizations.begin(), civilizations.end(),
                                  [=](const auto &civilization) {
                                    return civilization.id == civilization_id;
                                  });
  if (found == civilizations.end())
    throw std::invalid_argument("Sequence contains no matching element");
  return *found;
}

int next_fleet_id(const std::vector<FleetState> &fleets) {
  if (fleets.empty())
    return 0;
  const auto maximum = std::max_element(
      fleets.begin(), fleets.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  // The C# addition is unchecked and wraps to Int32.MinValue. Imported native
  // state instead rejects identity exhaustion so signed overflow cannot occur.
  if (maximum->id == std::numeric_limits<int>::max())
    throw std::overflow_error("Fleet identity space is exhausted.");
  return maximum->id + 1;
}

const ShipDesignDefinition *first_colony_design() {
  const auto designs = ship_design_catalog();
  const auto found =
      std::find_if(designs.begin(), designs.end(), [](const auto &design) {
        return design.role == FleetRole::Colony;
      });
  return found == designs.end() ? nullptr : &*found;
}

bool known_species(const std::string &species_id) {
  const auto profiles = species_environment_profiles();
  return std::any_of(
      profiles.begin(), profiles.end(),
      [&](const auto &profile) { return profile.id == species_id; });
}

bool later_population_is_larger(double candidate, double current) {
  // Double.CompareTo orders NaN below every numeric value. OrderByDescending is
  // stable, so equal values and two NaNs retain their input order.
  if (std::isnan(current))
    return !std::isnan(candidate);
  return !std::isnan(candidate) && candidate > current;
}

Colony *largest_owned_colony(std::span<Colony> colonies, int civilization_id) {
  Colony *result = nullptr;
  for (auto &colony : colonies) {
    if (colony.civilization_id != civilization_id)
      continue;
    if (!result || later_population_is_larger(colony.population_millions,
                                              result->population_millions))
      result = &colony;
  }
  return result;
}

FleetState starter_fleet(int id, const Civilization &civilization,
                         const StellarSystem &home,
                         const ShipDesignDefinition &design, std::string name) {
  FleetState result;
  result.id = id;
  result.civilization_id = civilization.id;
  result.name = std::move(name);
  result.role = design.role;
  result.design_id = design.id;
  result.position = {home.position.x, home.position.y};
  result.current_system_id = home.id;
  result.strategic_speed = design.strategic_speed;
  result.maximum_leg_range_light_years = design.maximum_leg_range_light_years;
  result.fuel_capacity_light_years = design.fuel_endurance_light_years;
  result.fuel_remaining_light_years = design.fuel_endurance_light_years;
  result.sensor_range = design.sensor_range;
  result.is_active = true;
  return result;
}

void add_starter_fleets(std::vector<FleetState> &fleets,
                        std::span<const StellarSystem> systems,
                        const Civilization &civilization,
                        std::optional<std::span<Colony>> population_sources) {
  const auto &home = home_system(systems, civilization.home_system_id);
  auto next_id = next_fleet_id(fleets);
  const auto &scout_design = ship_design_for_role(FleetRole::Scout);
  fleets.push_back(starter_fleet(next_id, civilization, home, scout_design,
                                 civilization.is_player
                                     ? "Pathfinder One"
                                     : civilization.name + " Scout"));

  if (!civilization.expansion_allowed || !population_sources)
    return;

  const auto *colony_design = first_colony_design();
  if (!colony_design || colony_design->population_cost_millions <= 0)
    return;
  auto *source = largest_owned_colony(*population_sources, civilization.id);
  if (!source || source->population_millions <
                     colony_design->population_cost_millions + 500.0)
    return;
  if (!known_species(source->population_species_id))
    throw std::invalid_argument("Source colony " + std::to_string(source->id) +
                                " references unknown population species '" +
                                source->population_species_id + "'.");

  if (next_id == std::numeric_limits<int>::max())
    throw std::overflow_error("Fleet identity space is exhausted.");
  source->population_millions -= colony_design->population_cost_millions;
  auto colony_fleet = starter_fleet(
      next_id + 1, civilization, home, *colony_design,
      civilization.is_player ? "Pioneer One" : civilization.name + " Pioneer");
  colony_fleet.embarked_population_millions =
      colony_design->population_cost_millions;
  colony_fleet.embarked_population_species_id = source->population_species_id;
  fleets.push_back(std::move(colony_fleet));
}
} // namespace

std::vector<FleetState>
seed_fleets(std::span<const StellarSystem> systems,
            std::span<const Civilization> civilizations,
            std::optional<std::span<Colony>> population_sources) {
  std::vector<FleetState> result;
  for (const auto &civilization : civilizations)
    if (civilization.development_stage ==
        CivilizationDevelopmentStage::WarpCapable)
      add_starter_fleets(result, systems, civilization, population_sources);
  return result;
}

void ensure_starter_fleets(std::span<const StellarSystem> systems,
                           std::span<const Civilization> civilizations,
                           std::span<Colony> population_sources,
                           std::vector<FleetState> &fleets,
                           int civilization_id) {
  const auto &civilization = civilization_by_id(civilizations, civilization_id);
  if (std::any_of(fleets.begin(), fleets.end(), [=](const auto &fleet) {
        return fleet.is_active && fleet.civilization_id == civilization_id &&
               fleet.role == FleetRole::Scout;
      }))
    return;
  add_starter_fleets(fleets, systems, civilization, population_sources);
}
} // namespace stellar::core
