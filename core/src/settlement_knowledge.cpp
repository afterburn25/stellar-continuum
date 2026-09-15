#include <stellar/core/settlement_knowledge.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {

bool species_exists(std::string_view id) {
  const auto values = species_environment_profiles();
  return std::any_of(values.begin(), values.end(),
                     [&](const auto &value) { return value.id == id; });
}

void require_species(std::string_view id) {
  if (!species_exists(id))
    throw std::out_of_range("Unknown species ID '" + std::string(id) + "'.");
}

bool civilization_exists(std::span<const Civilization> values, int id) {
  return std::any_of(values.begin(), values.end(),
                     [&](const auto &value) { return value.id == id; });
}

const Civilization *first_civilization(std::span<const Civilization> values,
                                       int id) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto &value) { return value.id == id; });
  return found == values.end() ? nullptr : &*found;
}

KnownSpeciesPlanetarySuitability known_view(const PlanetaryBody &body,
                                            std::string_view species_id) {
  const auto assessment = species_colonization_assessment(species_id, body);
  const auto &environment = assessment.environment;
  return {body.id,
          body.system_id,
          std::string(species_id),
          environment.natural_habitability,
          environment.unprotected_operational_capacity,
          environment.limiting_factor,
          assessment.viability,
          environment.requires_gravity_mitigation,
          environment.requires_thermal_control,
          environment.requires_pressure_control,
          environment.requires_sealed_habitat,
          environment.requires_artificial_biosphere,
          environment.requires_radiation_shielding};
}

int compare_descending_double(double left, double right) {
  const auto left_nan = std::isnan(left);
  const auto right_nan = std::isnan(right);
  if (left_nan != right_nan)
    return left_nan ? 1 : -1;
  if (left_nan || left == right)
    return 0;
  return left > right ? -1 : 1;
}

std::optional<int> reservation_system_id(SettlementKnowledgeWorldView world,
                                         const FleetState &fleet) {
  if (fleet.destination_system_id)
    return fleet.destination_system_id;
  if (!fleet.current_system_id)
    return std::nullopt;
  const auto current = *fleet.current_system_id;
  if (std::any_of(
          world.colonies.begin(), world.colonies.end(),
          [&](const auto &colony) { return colony.system_id == current; }))
    return std::nullopt;
  if (!world.knowledge.is_system_fully_surveyed(fleet.civilization_id, current))
    return std::nullopt;
  if (!fleet.embarked_population_species_id ||
      fleet.embarked_population_species_id->empty() ||
      !species_exists(*fleet.embarked_population_species_id))
    return std::nullopt;
  const auto &species_id = *fleet.embarked_population_species_id;
  if (fleet.destination_planetary_body_id) {
    const auto body = std::find_if(
        world.bodies.begin(), world.bodies.end(), [&](const auto &value) {
          return value.id == *fleet.destination_planetary_body_id &&
                 value.system_id == current;
        });
    return body != world.bodies.end() &&
                   species_colonization_assessment(species_id, *body)
                       .can_found_current_colony
               ? std::optional<int>(current)
               : std::nullopt;
  }
  const auto viable = std::any_of(
      world.bodies.begin(), world.bodies.end(), [&](const auto &body) {
        return body.system_id == current &&
               species_colonization_assessment(species_id, body)
                   .can_found_current_colony;
      });
  return viable ? std::optional<int>(current) : std::nullopt;
}

} // namespace

std::vector<KnownSpeciesPlanetarySuitability>
build_known_suitability_for_species(SettlementKnowledgeWorldView world,
                                    int observer_civilization_id,
                                    std::string_view species_id) {
  require_species(species_id);
  if (!civilization_exists(world.civilizations, observer_civilization_id))
    throw std::runtime_error("Unknown observer civilization " +
                             std::to_string(observer_civilization_id) + ".");
  std::vector<const PlanetaryBody *> ordered;
  for (const auto &body : world.bodies)
    if (world.knowledge.system_survey_level(observer_civilization_id,
                                            body.system_id) ==
        SystemSurveyLevel::fully_surveyed)
      ordered.push_back(&body);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const auto *left, const auto *right) {
                     if (left->system_id != right->system_id)
                       return left->system_id < right->system_id;
                     return left->id < right->id;
                   });
  std::vector<KnownSpeciesPlanetarySuitability> result;
  result.reserve(ordered.size());
  for (const auto *body : ordered)
    result.push_back(known_view(*body, species_id));
  return result;
}

std::vector<KnownSpeciesPlanetarySuitability>
build_known_suitability_for_available_populations(
    SettlementKnowledgeWorldView world, int observer_civilization_id) {
  const auto *civilization =
      first_civilization(world.civilizations, observer_civilization_id);
  if (!civilization)
    throw std::runtime_error("Unknown observer civilization " +
                             std::to_string(observer_civilization_id) + ".");
  std::vector<std::string> species_ids;
  for (const auto &colony : world.colonies)
    if (colony.civilization_id == observer_civilization_id &&
        colony.population_millions > 0.0 &&
        std::find(species_ids.begin(), species_ids.end(),
                  colony.population_species_id) == species_ids.end())
      species_ids.push_back(colony.population_species_id);
  if (std::find(species_ids.begin(), species_ids.end(),
                civilization->species_id) == species_ids.end())
    species_ids.push_back(civilization->species_id);
  std::sort(species_ids.begin(), species_ids.end());

  std::vector<KnownSpeciesPlanetarySuitability> result;
  for (const auto &species_id : species_ids) {
    auto values = build_known_suitability_for_species(
        world, observer_civilization_id, species_id);
    result.insert(result.end(), std::make_move_iterator(values.begin()),
                  std::make_move_iterator(values.end()));
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const auto &left, const auto &right) {
                     if (left.system_id != right.system_id)
                       return left.system_id < right.system_id;
                     if (left.planetary_body_id != right.planetary_body_id)
                       return left.planetary_body_id < right.planetary_body_id;
                     return left.species_id < right.species_id;
                   });
  return result;
}

std::optional<int>
resolve_best_available_settlement_body(SettlementKnowledgeWorldView world,
                                       int civilization_id, int system_id,
                                       std::string_view species_id) {
  require_species(species_id);
  if (!civilization_exists(world.civilizations, civilization_id))
    throw std::runtime_error("Unknown civilization " +
                             std::to_string(civilization_id) + ".");
  if (!std::any_of(world.systems.begin(), world.systems.end(),
                   [&](const auto &system) { return system.id == system_id; }))
    return std::nullopt;
  if (!world.knowledge.is_system_fully_surveyed(civilization_id, system_id))
    return std::nullopt;
  if (std::any_of(
          world.colonies.begin(), world.colonies.end(),
          [&](const auto &colony) { return colony.system_id == system_id; }))
    return std::nullopt;

  struct Candidate {
    int id{};
    SpeciesPlanetaryColonizationAssessment assessment;
  };
  std::vector<Candidate> candidates;
  for (const auto &body : world.bodies) {
    if (body.system_id != system_id)
      continue;
    auto assessment = species_colonization_assessment(species_id, body);
    if (assessment.can_found_current_colony)
      candidates.push_back({body.id, std::move(assessment)});
  }
  std::stable_sort(
      candidates.begin(), candidates.end(),
      [](const auto &left, const auto &right) {
        if (left.assessment.viability != right.assessment.viability)
          return left.assessment.viability > right.assessment.viability;
        auto comparison = compare_descending_double(
            left.assessment.environment.natural_habitability,
            right.assessment.environment.natural_habitability);
        if (comparison != 0)
          return comparison < 0;
        comparison = compare_descending_double(
            left.assessment.environment.unprotected_operational_capacity,
            right.assessment.environment.unprotected_operational_capacity);
        if (comparison != 0)
          return comparison < 0;
        return left.id < right.id;
      });
  return candidates.empty() ? std::nullopt
                            : std::optional<int>(candidates.front().id);
}

std::vector<FriendlyColonyMissionReservation>
build_friendly_colony_mission_reservations(SettlementKnowledgeWorldView world,
                                           const FleetState &requesting_fleet) {
  struct Candidate {
    int fleet_id{};
    int system_id{};
  };
  std::vector<Candidate> candidates;
  for (const auto &fleet : world.fleets) {
    if (fleet.id == requesting_fleet.id || !fleet.is_active ||
        fleet.civilization_id != requesting_fleet.civilization_id ||
        fleet.role != FleetRole::Colony ||
        !(fleet.embarked_population_millions > 0.0))
      continue;
    if (const auto system_id = reservation_system_id(world, fleet))
      candidates.push_back({fleet.id, *system_id});
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto &left, const auto &right) {
                     return left.fleet_id < right.fleet_id;
                   });
  std::vector<FriendlyColonyMissionReservation> result;
  for (const auto &candidate : candidates)
    if (std::none_of(result.begin(), result.end(), [&](const auto &value) {
          return value.system_id == candidate.system_id;
        }))
      result.push_back({candidate.system_id, candidate.fleet_id});
  return result;
}

FriendlyColonyReservationLookup
try_get_friendly_reserving_fleet_id(SettlementKnowledgeWorldView world,
                                    const FleetState &requesting_fleet,
                                    int system_id) {
  const auto values =
      build_friendly_colony_mission_reservations(world, requesting_fleet);
  const auto found =
      std::find_if(values.begin(), values.end(), [&](const auto &value) {
        return value.system_id == system_id;
      });
  return found == values.end()
             ? FriendlyColonyReservationLookup{}
             : FriendlyColonyReservationLookup{true, found->fleet_id};
}

} // namespace stellar::core
