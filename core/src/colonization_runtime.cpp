#include <stellar/core/colonization_runtime.hpp>

#include <stellar/core/colony_biology.hpp>
#include <stellar/core/colony_operations.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace stellar::core {
namespace {
constexpr double completion_epsilon = 1e-9;

template <class T>
T *first(std::span<T> values, const std::function<bool(const T &)> &predicate) {
  const auto it = std::find_if(values.begin(), values.end(), predicate);
  return it == values.end() ? nullptr : &*it;
}

const Civilization &require_civilization(ColonizationWorldView world, int id) {
  const auto it =
      std::find_if(world.civilizations.begin(), world.civilizations.end(),
                   [&](const auto &value) { return value.id == id; });
  if (it == world.civilizations.end())
    throw std::runtime_error("Sequence contains no matching element");
  return *it;
}

CivilizationEconomy &require_economy(ColonizationWorldView world, int id) {
  auto *value =
      first<CivilizationEconomy>(world.economies, [=](const auto &candidate) {
        return candidate.civilization_id == id;
      });
  if (!value)
    throw std::runtime_error("Sequence contains no matching element");
  return *value;
}

const PlanetaryBody *body_in_system(ColonizationWorldView world, int body_id,
                                    int system_id) {
  const auto it = std::find_if(
      world.bodies.begin(), world.bodies.end(), [&](const auto &body) {
        return body.id == body_id && body.system_id == system_id;
      });
  return it == world.bodies.end() ? nullptr : &*it;
}

bool valid_species(std::string_view id) {
  return std::any_of(species_environment_profiles().begin(),
                     species_environment_profiles().end(),
                     [&](const auto &species) { return species.id == id; });
}

std::string require_embarked_species(const FleetState &fleet) {
  if (fleet.embarked_population_millions <= 0)
    throw std::runtime_error("Fleet " + std::to_string(fleet.id) +
                             " has no embarked population to identify.");
  if (!fleet.embarked_population_species_id ||
      fleet.embarked_population_species_id->empty() ||
      !valid_species(*fleet.embarked_population_species_id)) {
    const auto id = fleet.embarked_population_species_id.value_or("");
    throw std::runtime_error(
        "Fleet " + std::to_string(fleet.id) +
        " carries population without a valid species identity '" + id + "'.");
  }
  return *fleet.embarked_population_species_id;
}

double source_min(double left, double right) {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return std::min(left, right);
}

bool advance_establishment(ColonizationWorldView world, FleetState &fleet,
                           int body_id, double days) {
  if (fleet.settlement_body_id != body_id) {
    fleet.settlement_body_id = body_id;
    fleet.settlement_days_completed = 0;
    return false;
  }
  const auto capacity =
      civilization_operating_funding(world.economies, fleet.civilization_id);
  fleet.settlement_days_completed =
      source_min(ColonizationSimulation::establishment_days(fleet),
                 fleet.settlement_days_completed + days * capacity);
  return fleet.settlement_days_completed + completion_epsilon >=
         ColonizationSimulation::establishment_days(fleet);
}

int next_colony_id(const std::vector<Colony> &colonies) {
  if (colonies.empty())
    return 0;
  const auto maximum = std::max_element(
      colonies.begin(), colonies.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  if (maximum->id == std::numeric_limits<int>::max())
    throw std::overflow_error("Colony ID exceeds the native integer range.");
  return maximum->id + 1;
}

void consume_settlement_vessel(FleetState &fleet) {
  fleet.embarked_population_millions = 0;
  fleet.embarked_population_species_id.reset();
  fleet.is_active = false;
  clear_fleet_route(fleet);
  fleet.destination_planetary_body_id.reset();
  fleet.settlement_body_id.reset();
  fleet.settlement_days_completed = 0;
}

const PlanetaryBody *select_compatibility_candidate(ColonizationWorldView world,
                                                    int system_id) {
  const PlanetaryBody *result = nullptr;
  for (const auto &body : world.bodies)
    if (body.system_id == system_id && body.legacy_colonization_candidate &&
        body.environment.has_solid_surface && (!result || body.id < result->id))
      result = &body;
  return result;
}

ColonyOrderResult validate_surveyed_system(ColonizationWorldView world,
                                           int civilization_id,
                                           int destination_system_id) {
  const auto system = std::find_if(
      world.systems.begin(), world.systems.end(),
      [&](const auto &value) { return value.id == destination_system_id; });
  if (system == world.systems.end())
    return {false, "Unknown destination."};
  if (!world.knowledge.is_system_known(civilization_id, destination_system_id))
    return {false, "That astronomical target has not been detected yet."};
  if (world.knowledge.system_survey_level(civilization_id,
                                          destination_system_id) !=
      SystemSurveyLevel::fully_surveyed)
    return {false,
            "A completed science survey is required before a colony mission "
            "can be prepared."};
  if (std::any_of(world.colonies.begin(), world.colonies.end(),
                  [&](const auto &colony) {
                    return colony.system_id == destination_system_id;
                  }))
    return {false,
            "That system already contains a founded colony in the current "
            "single-colony early-release model."};
  return {true, {}};
}

FleetState *available_colony_fleet(ColonizationWorldView world,
                                   int civilization_id) {
  FleetState *result = nullptr;
  for (auto &fleet : world.fleets) {
    if (!fleet.is_active || fleet.civilization_id != civilization_id ||
        fleet.role != FleetRole::Colony ||
        fleet.design_id == "resource_outpost_ship" ||
        !(fleet.embarked_population_millions > 0) ||
        fleet.destination_system_id || fleet.destination_planetary_body_id ||
        !fleet.current_system_id)
      continue;
    const bool friendly = std::any_of(
        world.colonies.begin(), world.colonies.end(), [&](const auto &colony) {
          return colony.civilization_id == civilization_id &&
                 colony.system_id == *fleet.current_system_id;
        });
    if (friendly && (!result || fleet.id < result->id))
      result = &fleet;
  }
  return result;
}

int double_compare(double left, double right) {
  const bool left_nan = std::isnan(left), right_nan = std::isnan(right);
  if (left_nan != right_nan)
    return left_nan ? -1 : 1;
  if (left_nan || left == right)
    return 0;
  return left < right ? -1 : 1;
}
} // namespace

ColonizationSimulation::ColonizationSimulation(SettlementReachAssessment reach)
    : reach_(std::move(reach)), opportunity_planner_(reach_),
      outpost_planner_(reach_) {}

double ColonizationSimulation::establishment_days(const FleetState &fleet) {
  return ResourceOutpostOpportunityPlanner::is_outpost_fleet(fleet)
             ? outpost_establishment_days
             : colony_establishment_days;
}

namespace {
SettlementExpeditionAuthorizationTerms authorization_terms(
    const FleetState &fleet, const double full_charge) noexcept {
  const bool is_new =
      fleet.prevent_automatic_settlement ||
      (!fleet.destination_system_id && !fleet.destination_planetary_body_id &&
       !fleet.settlement_body_id);
  return {is_new, is_new ? full_charge : 0.};
}
} // namespace

SettlementExpeditionAuthorizationTerms
ColonizationSimulation::colony_expedition_authorization(
    const FleetState &fleet) noexcept {
  return authorization_terms(fleet, colony_expedition_credit_cost);
}

SettlementExpeditionAuthorizationTerms
ColonizationSimulation::resource_outpost_expedition_authorization(
    const FleetState &fleet) noexcept {
  return authorization_terms(fleet, resource_outpost_expedition_credit_cost);
}

void ColonizationSimulation::abandon_mission_for_transit(FleetState &fleet) {
  fleet.destination_planetary_body_id.reset();
  fleet.settlement_body_id.reset();
  fleet.settlement_days_completed = 0;
  fleet.prevent_automatic_settlement = true;
}

std::vector<ColonizationEvent>
ColonizationSimulation::advance(ColonizationWorldView world,
                                double simulation_days) const {
  if (!std::isfinite(simulation_days) || simulation_days < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'simulationDays')");
  if (simulation_days == 0)
    return {};
  std::vector<ColonizationEvent> events;
  for (auto &fleet : world.fleets) {
    if (!fleet.is_active || fleet.role != FleetRole::Colony ||
        fleet.hold_requested ||
        civilization_operating_funding(world.economies,
                                       fleet.civilization_id) <= 1e-7 ||
        fleet.prevent_automatic_settlement)
      continue;
    const auto &civilization =
        require_civilization(world, fleet.civilization_id);
    if (ResourceOutpostOpportunityPlanner::is_outpost_fleet(fleet)) {
      if (fleet.transit_phase == FleetTransitPhase::None &&
          !fleet.destination_system_id && fleet.current_system_id &&
          fleet.destination_planetary_body_id) {
        const auto assessment = outpost_planner_.assess_order(
            world.planning(), fleet.id, *fleet.current_system_id,
            *fleet.destination_planetary_body_id);
        if (assessment.accepted &&
            advance_establishment(world, fleet,
                                  *fleet.destination_planetary_body_id,
                                  simulation_days)) {
          const auto *body =
              body_in_system(world, *fleet.destination_planetary_body_id,
                             *fleet.current_system_id);
          if (!body)
            throw std::runtime_error("Sequence contains no matching element");
          const auto personnel = fleet.embarked_population_millions;
          const auto species = require_embarked_species(fleet);
          const auto number =
              1 + std::count_if(world.colonies.begin(), world.colonies.end(),
                                [&](const auto &c) {
                                  return c.civilization_id == civilization.id &&
                                         c.kind ==
                                             SettlementKind::ResourceOutpost;
                                });
          Colony colony;
          colony.id = next_colony_id(world.colonies);
          colony.civilization_id = fleet.civilization_id;
          colony.system_id = *fleet.current_system_id;
          colony.planetary_body_id = body->id;
          colony.name =
              civilization.name + " Resource Outpost " + std::to_string(number);
          colony.kind = SettlementKind::ResourceOutpost;
          colony.population_species_id = species;
          colony.population_millions = personnel;
          colony.stored_food_population_days_millions =
              personnel * maximum_food_reserve_days;
          colony.stored_water_population_days_millions =
              personnel * maximum_water_reserve_days;
          colony.remaining_extractable_materials =
              initial_deposit_reserve(*body);
          colony.infrastructure = .15;
          colony.stability = .85;
          world.colonies.push_back(colony);
          consume_settlement_vessel(fleet);
          events.push_back({fleet.civilization_id, fleet.id, colony.system_id,
                            colony.id,
                            civilization.name +
                                " established a sealed staffed resource "
                                "outpost on " +
                                body->name + " with " +
                                detail::legacy_custom_fixed(personnel, 1, 1) +
                                " million specialist personnel."});
        }
      }
      continue;
    }
    if (fleet.transit_phase == FleetTransitPhase::None &&
        !fleet.destination_system_id && fleet.current_system_id &&
        fleet.embarked_population_millions > 0) {
      const auto species = require_embarked_species(fleet);
      const PlanetaryBody *body = nullptr;
      if (fleet.destination_planetary_body_id)
        body = body_in_system(world, *fleet.destination_planetary_body_id,
                              *fleet.current_system_id);
      else {
        const auto id = resolve_best_available_settlement_body(
            world.planning().knowledge_view(), fleet.civilization_id,
            *fleet.current_system_id, species);
        if (id)
          body = body_in_system(world, *id, *fleet.current_system_id);
      }
      const bool occupied =
          std::any_of(world.colonies.begin(), world.colonies.end(),
                      [&](const auto &colony) {
                        return colony.system_id == *fleet.current_system_id;
                      });
      if (body && !(body->stellar_exposure && body->stellar_exposure->baked) &&
          world.knowledge.system_survey_level(fleet.civilization_id,
                                              body->system_id) ==
              SystemSurveyLevel::fully_surveyed &&
          species_colonization_assessment(species, *body)
              .can_found_current_colony &&
          !occupied &&
          advance_establishment(world, fleet, body->id, simulation_days)) {
        const auto colonists = fleet.embarked_population_millions;
        const auto assessment = species_colonization_assessment(species, *body);
        Colony colony;
        colony.id = next_colony_id(world.colonies);
        colony.civilization_id = fleet.civilization_id;
        colony.system_id = *fleet.current_system_id;
        colony.planetary_body_id = body->id;
        colony.name =
            civilization.name + " Colony " +
            std::to_string(
                1 + std::count_if(world.colonies.begin(), world.colonies.end(),
                                  [&](const auto &c) {
                                    return c.civilization_id == civilization.id;
                                  }));
        colony.population_species_id = species;
        colony.population_millions = colonists;
        colony.stored_food_population_days_millions =
            colonists * maximum_food_reserve_days;
        colony.stored_water_population_days_millions =
            colonists * maximum_water_reserve_days;
        const bool natural = assessment.viability ==
                             SpeciesColonizationViability::NaturallyViable;
        colony.infrastructure = natural ? .35 : .42;
        colony.stability = natural ? .92 : .88;
        world.colonies.push_back(colony);
        consume_settlement_vessel(fleet);
        const auto &profile = species_environment_profile(species);
        events.push_back(
            {fleet.civilization_id, fleet.id, colony.system_id, colony.id,
             civilization.name + " established " + colony.name + " on " +
                 body->name + " with " +
                 detail::legacy_custom_fixed(colonists, 1, 1) + " million " +
                 profile.display_name + " colonists using " +
                 (natural ? "natural environmental viability."
                          : "prototype habitat support.")});
        continue;
      }
    }
    if (!fleet.destination_system_id && civilization_uses_ai(civilization,world.control) &&
        fleet.embarked_population_millions > 0) {
      std::unordered_map<int, const StellarSystem *> systems;
      for (const auto &system : world.systems)
        if (!systems.emplace(system.id, &system).second)
          throw std::invalid_argument(
              "An item with the same key has already been added. Key: " +
              std::to_string(system.id));
      std::unordered_map<int, const PlanetaryBody *> bodies;
      for (const auto &candidate : world.bodies)
        if (!bodies.emplace(candidate.id, &candidate).second)
          throw std::invalid_argument(
              "An item with the same key has already been added. Key: " +
              std::to_string(candidate.id));
      const auto plan = opportunity_planner_.build_plan(
          world.planning(), fleet.id,
          ColonizationOpportunityPlanner::hard_maximum_candidates);
      const ColonizationOpportunityCandidate *best = nullptr;
      double best_score{};
      for (const auto &candidate : plan.candidates) {
        if (!candidate.can_order || !systems.contains(candidate.system_id) ||
            !bodies.contains(candidate.planetary_body_id))
          continue;
        const auto &body = *bodies.at(candidate.planetary_body_id);
        const auto &system = *systems.at(candidate.system_id);
        double distance;
        if (!system.position.depth_light_years) {
          const float dx = fleet.position.x - system.position.x;
          const float dy = fleet.position.y - system.position.y;
          distance = static_cast<double>(dx * dx + dy * dy);
        } else {
          const auto physical =
              interstellar_distance_from_fleet(world.systems, fleet, system);
          distance = physical * physical;
        }
        const auto value =
            (candidate.colonization_viability ==
                     SpeciesColonizationViability::NaturallyViable
                 ? 14000.0
                 : 3500.0) +
            candidate.natural_habitability * 9000 +
            (body.has_rare_resource ? 14000 : 0) +
            civilization.traits.territoriality * 6000 +
            civilization.traits.greed * (body.has_rare_resource ? 9000 : 1500);
        const auto score = value - distance;
        const bool better =
            !best || double_compare(score, best_score) > 0 ||
            (double_compare(score, best_score) == 0 &&
             (candidate.system_id < best->system_id ||
              (candidate.system_id == best->system_id &&
               candidate.planetary_body_id < best->planetary_body_id)));
        if (better) {
          best = &candidate;
          best_score = score;
        }
      }
      if (best) {
        fleet.settlement_body_id.reset();
        fleet.settlement_days_completed = 0;
        assign_fleet_route(world.reach(), fleet, best->system_id, best->reach);
        fleet.destination_planetary_body_id = best->planetary_body_id;
      }
    }
  }
  return events;
}

ColonyOrderResult
ColonizationSimulation::issue_transit_order(ColonizationWorldView world,
                                            int civilization_id, int fleet_id,
                                            int destination_system_id) const {
  auto *fleet = first<FleetState>(world.fleets, [&](const auto &candidate) {
    return candidate.id == fleet_id && candidate.is_active &&
           candidate.civilization_id == civilization_id &&
           candidate.role == FleetRole::Colony;
  });
  if (!fleet)
    return {false, "Select an active colony or outpost ship you control."};
  const auto assessment =
      reach_ ? reach_(world.reach(), civilization_id, *fleet,
                      destination_system_id, InterstellarMissionKind::Colony)
             : stellar::core::assess_operational_reach(
                   world.reach(), civilization_id, *fleet,
                   destination_system_id, InterstellarMissionKind::Colony);
  if (!assessment.is_supported)
    return {false, assessment.reason};
  assign_fleet_route(world.reach(), *fleet, destination_system_id, assessment);
  abandon_mission_for_transit(*fleet);
  return {true, fleet->name +
                    ": course set. Colonists remain aboard until you "
                    "right-click a surveyed world to authorize settlement. " +
                    assessment.reason};
}

ColonizationOpportunityPlan ColonizationSimulation::get_opportunity_plan(
    ColonizationWorldView world, int fleet_id, int maximum_candidates) const {
  return opportunity_planner_.build_plan(world.planning(), fleet_id,
                                         maximum_candidates);
}

ResourceOutpostOpportunityPlan
ColonizationSimulation::get_resource_outpost_opportunity_plan(
    ColonizationWorldView world, int fleet_id, int maximum_candidates) const {
  return outpost_planner_.build_plan(world.planning(), fleet_id,
                                     maximum_candidates);
}

ResourceOutpostOrderAssessment
ColonizationSimulation::assess_resource_outpost_order(
    ColonizationWorldView world, int fleet_id, int destination_system_id,
    int planetary_body_id) const {
  return outpost_planner_.assess_order(world.planning(), fleet_id,
                                       destination_system_id,
                                       planetary_body_id);
}

ColonyOrderResult ColonizationSimulation::issue_resource_outpost_fleet_order(
    ColonizationWorldView world, int fleet_id, int destination_system_id,
    int planetary_body_id) const {
  const auto assessment = outpost_planner_.assess_order(
      world.planning(), fleet_id, destination_system_id, planetary_body_id);
  if (!assessment.accepted)
    return {false, assessment.message};
  auto *fleet = first<FleetState>(world.fleets, [&](const auto &candidate) {
    return candidate.id == fleet_id &&
           ResourceOutpostOpportunityPlanner::is_outpost_fleet(candidate);
  });
  if (!fleet)
    throw std::runtime_error("Sequence contains no matching element");
  const auto authorization =
      resource_outpost_expedition_authorization(*fleet);
  auto &economy = require_economy(world, fleet->civilization_id);
  const auto currency = sovereign_currency_for_civilization(
      world.civilizations, fleet->civilization_id);
  if (authorization.is_new_expedition &&
      economy.credits + .0001 < authorization.charge_budget_units)
    return {false, currency.format(resource_outpost_expedition_credit_cost) +
                       " is required to fund the resource-outpost expedition."};
  if (authorization.is_new_expedition)
    economy.credits -= authorization.charge_budget_units;
  assign_fleet_route(world.reach(), *fleet, destination_system_id,
                     assessment.candidate->reach);
  if (fleet->destination_planetary_body_id != planetary_body_id) {
    fleet->settlement_body_id.reset();
    fleet->settlement_days_completed = 0;
  }
  fleet->destination_planetary_body_id = planetary_body_id;
  fleet->prevent_automatic_settlement = false;
  return {true, assessment.message +
                    (authorization.is_new_expedition ? " Expedition funded for " +
                                  currency.format(
                                      resource_outpost_expedition_credit_cost) +
                                  "."
                            : " Destination updated; the original expedition "
                              "authorization remains in effect.")};
}

ColonizationOrderAssessment ColonizationSimulation::assess_colony_order(
    ColonizationWorldView world, int fleet_id, int destination_system_id,
    int planetary_body_id) const {
  return opportunity_planner_.assess_order(
      world.planning(), fleet_id, destination_system_id, planetary_body_id);
}

ColonyOrderResult ColonizationSimulation::issue_player_colony_order(
    ColonizationWorldView world, int civilization_id,
    int destination_system_id) const {
  const auto validation =
      validate_surveyed_system(world, civilization_id, destination_system_id);
  if (!validation.accepted)
    return validation;
  auto *fleet = available_colony_fleet(world, civilization_id);
  if (!fleet)
    return {false, "No colony ship carrying reserved colonists is available."};
  if (!fleet->embarked_population_species_id ||
      fleet->embarked_population_species_id->empty() ||
      !valid_species(*fleet->embarked_population_species_id))
    return {false, "The colony ship's passenger species identity is invalid."};
  const auto body = resolve_best_available_settlement_body(
      world.planning().knowledge_view(), civilization_id, destination_system_id,
      *fleet->embarked_population_species_id);
  if (!body)
    return {false,
            "No surveyed body in that system is currently viable for the "
            "colony ship's population. Additional environmental-support "
            "capability may make other worlds usable later."};
  return issue_player_colony_order(world, civilization_id,
                                   destination_system_id, *body);
}

ColonyOrderResult ColonizationSimulation::issue_player_colony_order(
    ColonizationWorldView world, int civilization_id, int destination_system_id,
    int planetary_body_id) const {
  const auto validation =
      validate_surveyed_system(world, civilization_id, destination_system_id);
  if (!validation.accepted)
    return validation;
  auto *fleet = available_colony_fleet(world, civilization_id);
  if (!fleet)
    return {false, "No colony ship carrying reserved colonists is available."};
  return issue_colony_fleet_order(world, fleet->id, destination_system_id,
                                  planetary_body_id);
}

ColonyOrderResult ColonizationSimulation::issue_colony_fleet_order(
    ColonizationWorldView world, int fleet_id, int destination_system_id,
    int planetary_body_id) const {
  const auto assessment = opportunity_planner_.assess_order(
      world.planning(), fleet_id, destination_system_id, planetary_body_id);
  if (!assessment.accepted)
    return {false, assessment.message};
  auto *fleet = first<FleetState>(world.fleets, [&](const auto &candidate) {
    return candidate.id == fleet_id && candidate.is_active &&
           candidate.role == FleetRole::Colony &&
           candidate.embarked_population_millions > 0;
  });
  if (!fleet)
    throw std::runtime_error("Sequence contains no matching element");
  const auto authorization = colony_expedition_authorization(*fleet);
  auto &economy = require_economy(world, fleet->civilization_id);
  const auto currency = sovereign_currency_for_civilization(
      world.civilizations, fleet->civilization_id);
  if (authorization.is_new_expedition &&
      economy.credits + .0001 < authorization.charge_budget_units)
    return {false, currency.format(colony_expedition_credit_cost) +
                       " is required to fund the colony expedition."};
  if (authorization.is_new_expedition)
    economy.credits -= authorization.charge_budget_units;
  assign_fleet_route(world.reach(), *fleet, destination_system_id,
                     assessment.candidate->reach);
  if (fleet->destination_planetary_body_id != planetary_body_id) {
    fleet->settlement_body_id.reset();
    fleet->settlement_days_completed = 0;
  }
  fleet->destination_planetary_body_id = planetary_body_id;
  fleet->prevent_automatic_settlement = false;
  return {true,
          assessment.message +
                    (authorization.is_new_expedition ? " Expedition funded for " +
                            currency.format(colony_expedition_credit_cost) + "."
                      : " Destination updated; the original expedition "
                        "authorization remains in effect.")};
}

MissionReachAssessment ColonizationSimulation::assess_operational_reach(
    ColonizationWorldView world, int fleet_id,
    int destination_system_id) const {
  return opportunity_planner_.assess_operational_reach(
      world.planning(), fleet_id, destination_system_id);
}

std::optional<PlanetaryBody>
ColonizationSimulation::resolve_compatibility_colony_world(
    ColonizationWorldView world, const Colony &colony) const {
  if (colony.planetary_body_id) {
    const auto *body =
        body_in_system(world, *colony.planetary_body_id, colony.system_id);
    return body ? std::optional<PlanetaryBody>(*body) : std::nullopt;
  }
  const auto *body = select_compatibility_candidate(world, colony.system_id);
  return body ? std::optional<PlanetaryBody>(*body) : std::nullopt;
}

} // namespace stellar::core
