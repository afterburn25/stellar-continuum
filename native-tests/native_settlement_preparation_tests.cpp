#include "native_settlement_preparation.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/ship_designs.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar::core;
using namespace stellar::native_settlement_preparation;

namespace {
void require(const bool value, const std::string_view expression, const int line) {
  if (!value)
    throw std::runtime_error("Settlement preparation check failed at line " +
                             std::to_string(line) + ": " + std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

CampaignFrame frame(const fs::path &research, const fs::path &catalog) {
  auto world = seed_persistable_fresh_campaign(
      118500, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  return {IntegratedAdaptiveCampaignRuntime::create_fresh(
              load_adaptive_research_strategic_runtime(research), std::move(world)),
          StrategicClock{}, CampaignFramePolicy::Player};
}

const Civilization &player(const FreshCampaignState &world) {
  const auto found = std::ranges::find(world.civilizations,
                                       world.player_civilization_id,
                                       &Civilization::id);
  REQUIRE(found != world.civilizations.end());
  REQUIRE(found->is_player);
  return *found;
}

const PlanetaryBody &body_in_system(const FreshCampaignState &world, const int system_id) {
  const auto found = std::ranges::find_if(world.bodies, [=](const auto &body) {
    return body.system_id == system_id;
  });
  REQUIRE(found != world.bodies.end());
  return *found;
}

const StellarSystem &other_system_with_body(const FreshCampaignState &world,
                                            const int excluded_system_id) {
  const auto found = std::ranges::find_if(world.systems, [&](const auto &system) {
    return system.id != excluded_system_id &&
           std::ranges::any_of(world.bodies, [&](const auto &body) {
             return body.system_id == system.id;
           });
  });
  REQUIRE(found != world.systems.end());
  return *found;
}

const StellarSystem &hidden_system_with_body(const FreshCampaignState &world,
                                             const int observer_id,
                                             const int excluded_system_id) {
  const auto found = std::ranges::find_if(world.systems, [&](const auto &system) {
    return system.id != excluded_system_id &&
           world.knowledge.system_survey_level(observer_id, system.id) ==
               SystemSurveyLevel::unknown &&
           std::ranges::any_of(world.bodies, [&](const auto &body) {
             return body.system_id == system.id;
           });
  });
  REQUIRE(found != world.systems.end());
  return *found;
}

struct ReadOnlyState {
  std::size_t fleets{}, colonies{}, construction{}, shipyards{}, economies{};
  double credits{}, industry{};
  bool has_core{};
  std::vector<SystemSurveyKnowledgeView> surveys;
  std::vector<std::string> construction_projects;
};

ReadOnlyState capture_state(const FreshCampaignState &world, const int observer_id) {
  ReadOnlyState result{world.fleets.size(), world.colonies.size(), world.construction.size(),
                       world.shipyards.size(), world.economies.size(), 0., 0.,
                       world.core.has_value(), world.knowledge.system_survey_knowledge(observer_id)};
  for (const auto &economy : world.economies) {
    result.credits += economy.credits;
    result.industry += economy.industry;
  }
  for (const auto &state : world.construction)
    for (const auto &project : state.completed_project_ids)
      result.construction_projects.push_back(std::to_string(state.civilization_id) + ":" + project);
  return result;
}

bool same_state(const ReadOnlyState &left, const ReadOnlyState &right) {
  if (left.fleets != right.fleets || left.colonies != right.colonies ||
      left.construction != right.construction || left.shipyards != right.shipyards ||
      left.economies != right.economies || left.credits != right.credits ||
      left.industry != right.industry || left.has_core != right.has_core ||
      left.construction_projects != right.construction_projects ||
      left.surveys.size() != right.surveys.size())
    return false;
  for (std::size_t index = 0; index < left.surveys.size(); ++index) {
    const auto &a = left.surveys[index];
    const auto &b = right.surveys[index];
    if (a.system_id != b.system_id || a.level != b.level || a.progress != b.progress)
      return false;
  }
  return true;
}

void valid_full_view_uses_canonical_facts(const fs::path &research, const fs::path &catalog) {
  auto campaign = frame(research, catalog);
  auto &world = campaign.runtime().world().campaign();
  const auto &observer = player(world);
  const auto &body = body_in_system(world, observer.home_system_id);
  REQUIRE(world.knowledge.system_survey_level(observer.id, body.system_id) ==
          SystemSurveyLevel::fully_surveyed);
  const auto before = capture_state(world, observer.id);

  const auto view = build_settlement_preparation(campaign, 41, body.system_id, body.id);
  REQUIRE(view.has_value());
  REQUIRE(view->campaign_generation == 41);
  REQUIRE(view->player_civilization_id == observer.id);
  REQUIRE(view->system_id == body.system_id && view->body_id == body.id);
  REQUIRE(view->body_name == body.name && view->species_id == observer.species_id);
  REQUIRE(view->species_name == species_environment_profile(observer.species_id).display_name);
  const auto expected_currency = sovereign_currency_for_civilization(world.civilizations, observer.id);
  REQUIRE(view->currency.code == expected_currency.code);
  const auto economy = std::ranges::find(world.economies, observer.id,
                                         &CivilizationEconomy::civilization_id);
  REQUIRE(economy != world.economies.end());
  REQUIRE(view->treasury == economy->credits);
  REQUIRE(view->formatted_treasury == expected_currency.format(economy->credits));
  const auto expected_assessment = species_colonization_assessment(observer.species_id, body);
  REQUIRE(view->suitability.planetary_body_id == expected_assessment.planetary_body_id);
  REQUIRE(view->suitability.species_id == expected_assessment.species_id);
  REQUIRE(view->suitability.viability == expected_assessment.viability);
  REQUIRE(view->suitability.can_found_current_colony == expected_assessment.can_found_current_colony);
  REQUIRE(view->site_can_found_current_colony == expected_assessment.can_found_current_colony);
  REQUIRE(view->suitability.environment.natural_habitability ==
          expected_assessment.environment.natural_habitability);
  REQUIRE(view->suitability.environment.unprotected_operational_capacity ==
          expected_assessment.environment.unprotected_operational_capacity);

  const AdaptiveResearchShipbuildingCapabilityView capabilities(campaign.runtime().research());
  const ShipbuildingReadView read{world.civilizations, world.systems, world.construction,
                                  world.shipyards, world.colonies, world.economies,
                                  world.fleets, {}, {},
                                  [&](const int civilization_id, const std::string_view capability_id) {
                                    return capabilities.has_civilization_capability(civilization_id,
                                                                                     capability_id);
                                  }, {}};
  const auto check_option = [&](const Option &option, const std::string_view design_id,
                                const double expedition_cost, const double establishment_days) {
    const auto *design = find_ship_design(design_id);
    REQUIRE(design != nullptr);
    const auto assessment = assess_start_ship_build(read, observer.id, design_id);
    REQUIRE(option.design_id == design_id && option.design_name == design->name);
    REQUIRE(option.industry_cost == design->industry_cost);
    REQUIRE(option.minimum_build_days == design->industry_cost / shipbuilding_industry_per_day);
    REQUIRE(option.population_reservation_millions == design->population_cost_millions);
    REQUIRE(option.formatted_ship_cost == expected_currency.format(design->credit_cost));
    REQUIRE(option.expedition_cost == expedition_cost);
    REQUIRE(option.formatted_expedition_cost == expected_currency.format(expedition_cost));
    REQUIRE(option.establishment_days == establishment_days);
    REQUIRE(option.shipbuilding_blocker == assessment.blocker);
  };
  check_option(view->colony_ship, "colony_ship",
               ColonizationSimulation::colony_expedition_credit_cost,
               ColonizationSimulation::colony_establishment_days);
  check_option(view->resource_outpost, "resource_outpost_ship",
               ColonizationSimulation::resource_outpost_expedition_credit_cost,
               ColonizationSimulation::outpost_establishment_days);
  REQUIRE(same_state(before, capture_state(world, observer.id)));
}

void denies_hidden_partial_mismatched_and_invalid_observers(const fs::path &research,
                                                            const fs::path &catalog) {
  {
    auto campaign = frame(research, catalog);
    auto &world = campaign.runtime().world().campaign();
    const auto &observer = player(world);
    const auto &target = hidden_system_with_body(world, observer.id, observer.home_system_id);
    const auto &body = body_in_system(world, target.id);
    REQUIRE(world.knowledge.system_survey_level(observer.id, target.id) == SystemSurveyLevel::unknown);
    REQUIRE(!build_settlement_preparation(campaign, 1, target.id, body.id));
  }
  {
    auto campaign = frame(research, catalog);
    auto &world = campaign.runtime().world().campaign();
    const auto &observer = player(world);
    const auto &target = hidden_system_with_body(world, observer.id, observer.home_system_id);
    const auto &body = body_in_system(world, target.id);
    REQUIRE(world.knowledge.reveal_system(observer.id, target.id));
    REQUIRE(world.knowledge.record_reconnaissance(observer.id, target.id, .35));
    REQUIRE(world.knowledge.system_survey_level(observer.id, target.id) ==
            SystemSurveyLevel::partially_surveyed);
    REQUIRE(!build_settlement_preparation(campaign, 1, target.id, body.id));
  }
  {
    auto campaign = frame(research, catalog);
    auto &world = campaign.runtime().world().campaign();
    const auto &observer = player(world);
    const auto &home_body = body_in_system(world, observer.home_system_id);
    const auto &target = other_system_with_body(world, observer.home_system_id);
    REQUIRE(world.knowledge.mark_system_fully_surveyed(observer.id, target.id));
    REQUIRE(!build_settlement_preparation(campaign, 1, target.id, home_body.id));
  }
  {
    auto campaign = frame(research, catalog);
    auto &world = campaign.runtime().world().campaign();
    const auto &observer = player(world);
    const auto &home_body = body_in_system(world, observer.home_system_id);
    world.player_civilization_id = -1;
    REQUIRE(!build_settlement_preparation(campaign, 1, home_body.system_id, home_body.id));
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    REQUIRE(argc == 3);
    const auto research = fs::absolute(argv[1]);
    const auto catalog = fs::absolute(argv[2]);
    valid_full_view_uses_canonical_facts(research, catalog);
    denies_hidden_partial_mismatched_and_invalid_observers(research, catalog);
    std::cout << "native settlement preparation tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
