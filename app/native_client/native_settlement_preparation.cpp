#include "native_settlement_preparation.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <ranges>
#include <string_view>

namespace stellar::native_settlement_preparation {
namespace {
using namespace stellar::core;

template <class Range, class Member>
auto *find_by_id(Range &values, const int id, Member member) {
  const auto found = std::ranges::find(values, id, member);
  return found == values.end() ? nullptr : &*found;
}

Option build_option(const ShipbuildingReadView &read,
                    const int civilization_id,
                    const SovereignCurrencyDefinition &currency,
                    const std::string_view design_id,
                    const double expedition_cost,
                    const double establishment_days) {
  const auto *design = find_ship_design(design_id);
  const auto assessment = assess_start_ship_build(read, civilization_id, design_id);
  Option option;
  option.design_id = std::string(design_id);
  option.design_name = design ? design->name : "Unavailable";
  if (design) {
    option.industry_cost = design->industry_cost;
    option.minimum_build_days = design->industry_cost / shipbuilding_industry_per_day;
    option.population_reservation_millions = design->population_cost_millions;
    option.formatted_ship_cost = currency.format(design->credit_cost);
  }
  option.expedition_cost = expedition_cost;
  option.formatted_expedition_cost = currency.format(expedition_cost);
  option.establishment_days = establishment_days;
  option.shipbuilding_blocker = assessment.blocker;
  return option;
}
}  // namespace

std::optional<View> build_settlement_preparation(CampaignFrame &frame,
                                                  const std::uint64_t generation,
                                                  const int system_id,
                                                  const int body_id) {
  auto &runtime = frame.runtime();
  const auto &world = runtime.world().campaign();
  const auto *player = find_by_id(world.civilizations, world.player_civilization_id,
                                  &Civilization::id);
  const auto *system = find_by_id(world.systems, system_id, &StellarSystem::id);
  const auto *body = find_by_id(world.bodies, body_id, &PlanetaryBody::id);
  if (!player || !player->is_player || !system || !body ||
      body->system_id != system->id ||
      world.knowledge.system_survey_level(player->id, system->id) !=
          SystemSurveyLevel::fully_surveyed)
    return std::nullopt;

  const auto profiles = species_environment_profiles();
  const auto profile = std::ranges::find(profiles, player->species_id,
                                         &SpeciesEnvironmentProfile::id);
  const auto *economy = find_by_id(world.economies, player->id,
                                   &CivilizationEconomy::civilization_id);
  const auto *construction = find_by_id(world.construction, player->id,
                                        &ConstructionState::civilization_id);
  if (profile == profiles.end() || !economy || !construction)
    return std::nullopt;

  const AdaptiveResearchShipbuildingCapabilityView capabilities(runtime.research());
  const auto capability_query = [&](const int civilization_id,
                                    const std::string_view capability_id) {
    return capabilities.has_civilization_capability(civilization_id, capability_id);
  };
  const ShipbuildingReadView read{world.civilizations, world.systems,
                                  world.construction, world.shipyards,
                                  world.colonies, world.economies, world.fleets,
                                  {}, {}, capability_query, {}};

  View result;
  result.campaign_generation = generation;
  result.player_civilization_id = player->id;
  result.system_id = system->id;
  result.body_id = body->id;
  result.body_name = body->name;
  result.species_id = player->species_id;
  result.species_name = profile->display_name;
  result.currency = sovereign_currency_for_civilization(world.civilizations, player->id);
  result.treasury = economy->credits;
  result.formatted_treasury = result.currency.format(result.treasury);
  result.solid_surface = body->environment.has_solid_surface;
  result.native_pre_warp_life = body->has_pre_warp_civilization;
  result.rare_resource = body->has_rare_resource;
  result.suitability = species_colonization_assessment(player->species_id, *body);
  result.site_can_found_current_colony = result.suitability.can_found_current_colony;
  result.colony_ship = build_option(
      read, player->id, result.currency, "colony_ship",
      ColonizationSimulation::colony_expedition_credit_cost,
      ColonizationSimulation::colony_establishment_days);
  result.resource_outpost = build_option(
      read, player->id, result.currency, "resource_outpost_ship",
      ColonizationSimulation::resource_outpost_expedition_credit_cost,
      ColonizationSimulation::outpost_establishment_days);
  return result;
}
}  // namespace stellar::native_settlement_preparation
