#include <stellar/core/strategic_input_builder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace stellar::core {
namespace {

template <class T, class Identifier>
const T &first_by_id(std::span<const T> values, Identifier identifier,
                     int civilization_id, std::string message) {
  const auto found = std::find_if(values.begin(), values.end(),
                                  [=](const auto &value) {
                                    return identifier(value) == civilization_id;
                                  });
  if (found == values.end())
    throw std::invalid_argument(std::move(message));
  return *found;
}

double managed_max(double left, double right) noexcept {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return std::max(left, right);
}

bool contains(std::span<const std::string> values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

EconomyFleetRole economy_role(FleetRole role) {
  switch (role) {
  case FleetRole::Scout: return EconomyFleetRole::Scout;
  case FleetRole::Science: return EconomyFleetRole::Science;
  case FleetRole::Colony: return EconomyFleetRole::Colony;
  case FleetRole::Military: return EconomyFleetRole::Military;
  case FleetRole::Logistics: return EconomyFleetRole::Logistics;
  }
  return static_cast<EconomyFleetRole>(static_cast<int>(role));
}

} // namespace

CivilizationStrategicInputBuilder::CivilizationStrategicInputBuilder(
    StrategicLogisticsQuery logistics,
    StrategicShipbuildingCapabilityQuery shipbuilding_capabilities,
    StrategicExplorationPlanQuery exploration)
    : logistics_(std::move(logistics)),
      shipbuilding_capabilities_(std::move(shipbuilding_capabilities)),
      exploration_(std::move(exploration)) {
  if (!logistics_)
    logistics_ = [](const StrategicInputWorldView &world, int civilization_id) {
      std::vector<EconomyConstructionState> construction;
      construction.reserve(world.construction.size());
      for (const auto &state : world.construction)
        construction.push_back(
            {state.civilization_id, state.completed_project_ids});
      std::vector<EconomyFleetState> fleets;
      fleets.reserve(world.fleets.size());
      for (const auto &fleet : world.fleets)
        fleets.push_back(
            {fleet.civilization_id, economy_role(fleet.role), fleet.is_active});
      return economy_logistics(
          {world.civilizations, world.bodies, construction, fleets},
          world.colonies, world.economies, civilization_id);
    };
  if (!shipbuilding_capabilities_)
    shipbuilding_capabilities_ =
        [](const StrategicInputWorldView &world, int civilization_id,
           std::string_view capability_id) {
          return prototype_shipbuilding_has_capability(
              world.technologies, civilization_id, capability_id);
        };
  if (!exploration_) {
    exploration_ = [](ExplorationPlanningWorldView world, int fleet_id,
                      int maximum_candidates) {
      return ExplorationMissionPlanner{}.build_plan(world, fleet_id,
                                                     maximum_candidates);
    };
  }
}

CivilizationOwnState CivilizationStrategicInputBuilder::build(
    StrategicInputWorldView world, int civilization_id) const {
  const auto &civilization = first_by_id(
      world.civilizations, [](const auto &value) { return value.id; },
      civilization_id,
      "Unknown civilization " + std::to_string(civilization_id) + ".");
  const auto &economy = first_by_id(
      world.economies,
      [](const auto &value) { return value.civilization_id; }, civilization_id,
      "Civilization " + std::to_string(civilization_id) +
          " has no economy state.");
  const auto &technology = first_by_id(
      world.technologies,
      [](const auto &value) { return value.civilization_id; }, civilization_id,
      "Civilization " + std::to_string(civilization_id) +
          " has no technology state.");
  const auto &construction = first_by_id(
      world.construction,
      [](const auto &value) { return value.civilization_id; }, civilization_id,
      "Civilization " + std::to_string(civilization_id) +
          " has no construction state.");
  const auto logistics = logistics_(world, civilization_id);

  std::unordered_set<int> colonized_system_ids;
  for (const auto &colony : world.colonies)
    colonized_system_ids.insert(colony.system_id);

  std::vector<std::string_view> population_species_ids;
  const auto append_species = [&](std::string_view species_id) {
    if (std::find(population_species_ids.begin(), population_species_ids.end(),
                  species_id) == population_species_ids.end())
      population_species_ids.push_back(species_id);
  };
  for (const auto &colony : world.colonies)
    if (colony.civilization_id == civilization_id &&
        colony.population_millions > 0)
      append_species(colony.population_species_id);
  append_species(civilization.species_id);

  std::vector<const FleetState *> active_fleets;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id == civilization_id)
      active_fleets.push_back(&fleet);

  std::vector<const FleetState *> exploration_fleets;
  for (const auto *fleet : active_fleets)
    if (fleet->role == FleetRole::Scout || fleet->role == FleetRole::Science)
      exploration_fleets.push_back(fleet);
  std::stable_sort(exploration_fleets.begin(), exploration_fleets.end(),
                   [](const auto *left, const auto *right) {
                     return left->id < right->id;
                   });
  bool has_supported_exploration_work = false;
  for (const auto *fleet : exploration_fleets) {
    const auto plan = exploration_(world.exploration_view(), fleet->id,
                                   ExplorationMissionPlanner::hard_maximum_candidates);
    if (std::any_of(plan.candidates.begin(), plan.candidates.end(),
                    [](const auto &candidate) {
                      return candidate.reach.is_supported;
                    })) {
      has_supported_exploration_work = true;
      break;
    }
  }

  bool has_known_colonization_opportunity = false;
  for (const auto &body : world.bodies) {
    if (!world.knowledge.is_system_fully_surveyed(civilization_id,
                                                   body.system_id) ||
        colonized_system_ids.contains(body.system_id))
      continue;
    for (const auto species_id : population_species_ids) {
      if (species_colonization_assessment(species_id, body)
              .can_found_current_colony) {
        has_known_colonization_opportunity = true;
        break;
      }
    }
    if (has_known_colonization_opportunity)
      break;
  }

  const bool has_spacecraft_construction = shipbuilding_capabilities_(
      world, civilization_id, "spacecraft_construction");
  const bool has_experimental_transit = shipbuilding_capabilities_(
      world, civilization_id, "experimental_interstellar_transit");
  const bool has_orbital_shipyard =
      contains(construction.completed_project_ids, "orbital_shipyard");
  const bool can_build_interstellar_ships = has_spacecraft_construction &&
                                            has_experimental_transit &&
                                            has_orbital_shipyard;

  const auto military_fleet_count = std::count_if(
      active_fleets.begin(), active_fleets.end(), [](const auto *fleet) {
        return fleet->role == FleetRole::Military;
      });
  const auto readiness =
      combat_readiness(world.combat_readiness_view(), civilization_id);
  const auto colony_count =
      std::count_if(world.colonies.begin(), world.colonies.end(),
                    [=](const auto &colony) {
                      return colony.civilization_id == civilization_id;
                    });
  const auto desired_military_fleets =
      can_build_interstellar_ships
          ? std::max<std::ptrdiff_t>(
                1, static_cast<std::ptrdiff_t>(
                       std::ceil(static_cast<double>(colony_count) / 2.0)))
          : 0;
  const bool fleet_capacity_shortfall =
      military_fleet_count < desired_military_fleets;
  const bool available_research =
      !technology.active_research_id &&
      !available_legacy_technologies(technology, construction).empty();

  return {managed_max(1, readiness.combat_effective_armed_strength),
          logistics.effective_coverage_ratio,
          managed_max(0, economy.industry),
          managed_max(0, economy.last_science_per_second),
          available_research,
          has_supported_exploration_work,
          has_known_colonization_opportunity && has_experimental_transit,
          can_build_interstellar_ships,
          fleet_capacity_shortfall};
}

} // namespace stellar::core
