#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/fleet_transit.hpp>

#include <algorithm>
#include <stdexcept>

namespace stellar::core {

FreshCampaignState seed_fresh_campaign(std::int64_t seed,
                                       std::span<const CatalogStar> catalog,
                                       int system_count, int pre_warp_count,
                                       int ancient_count,
                                       const std::string &player_species_id,
                                       std::optional<StellarPopulationOptions> population) {
  if (system_count != 250 && system_count != 500 && system_count != 1000 &&
      system_count != 2500)
    throw std::out_of_range(
        "Full-galaxy system count must be one of: 250, 500, 1000, 2500. "
        "(Parameter 'systemCount')");
  const auto civilization_count =
      static_cast<std::int64_t>(pre_warp_count) + ancient_count;
  if (civilization_count < 1 || civilization_count > system_count)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'PreWarpCivilizationCount')");
  try {
    (void)species_environment_profile(player_species_id);
  } catch (const std::out_of_range &) {
    throw std::invalid_argument("Unknown player species '" + player_species_id +
                                "'. (Parameter 'playerSpeciesId')");
  }

  auto physical = generate_stellar_catalog(seed, system_count, catalog);
  if (population) apply_stellar_population(seed, physical, *population);
  auto founding = create_founding_catalog(seed, physical, pre_warp_count,
                                          ancient_count, player_species_id);

  FreshCampaignState result;
  result.seed = seed;
  result.systems = std::move(founding.systems);
  result.bodies = std::move(founding.bodies);
  result.civilizations = std::move(founding.civilizations);
  result.used_constrained_home_fallback =
      founding.used_constrained_home_fallback;

  if (population) {
    const auto removed=apply_stellar_planetary_physics(result.systems,result.bodies);
    for (auto& system:result.systems) {
      if(auto found=removed.find(system.id);found!=removed.end())system.engulfed_planets+=found->second;
      system.has_habitable_world=std::any_of(result.bodies.begin(),result.bodies.end(),[&](const auto& body){return body.system_id==system.id&&body.legacy_colonization_candidate;});
    }
  }

  result.colonies = seed_colonies(result.civilizations, result.bodies);
  result.fleets =
      seed_fleets(result.systems, result.civilizations, result.colonies);
  if(population)for(auto& fleet:result.fleets)if(fleet.current_system_id){
    const auto& p=result.systems.at(static_cast<std::size_t>(*fleet.current_system_id)).stellar_object;
    if(p)begin_fleet_local_transit(fleet,FleetTransitPhase::None,{}, {},&*p);
  }
  result.economies = seed_economies(result.civilizations);
  result.technologies = seed_legacy_technologies(result.civilizations);
  result.construction = seed_construction(result.civilizations);
  result.shipyards = seed_shipyards(result.civilizations);

  for (const auto &civilization : result.civilizations) {
    result.knowledge.mark_system_fully_surveyed(civilization.id,
                                                civilization.home_system_id);
    result.knowledge.reveal_within_sensor_range(
        civilization.id, civilization.home_system_id, result.systems,
        civilization.is_seeded_ancient ? 25.0F : 8.0F);
  }

  const auto player = std::find_if(
      result.civilizations.begin(), result.civilizations.end(),
      [](const auto &civilization) { return civilization.is_player; });
  if (player == result.civilizations.end())
    throw std::runtime_error("Sequence contains no matching element");
  result.player_civilization_id = player->id;
  result.core = full_galaxy_core(system_count);
  return result;
}

} // namespace stellar::core
