#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/stellar_coverage.hpp>
#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/galaxy_configuration.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {

FreshCampaignState seed_fresh_campaign(std::int64_t seed,
                                       std::span<const CatalogStar> catalog,
                                       int system_count, int pre_warp_count,
                                       int ancient_count,
                                       const std::string &player_species_id,
                                       std::optional<StellarPopulationOptions> population,bool developer_full_coverage,bool visual_footprint) {
  if (!supported_full_galaxy_system_count(system_count))
    throw std::out_of_range(
        "Full-galaxy system count must be one of: 250, 500, 1000, 2500, 5000, 10000, 25000, 50000. "
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
  if (population) apply_stellar_population(seed, physical, *population,visual_footprint);
  StellarCoverageResult coverage;
  if(developer_full_coverage){
    if(!population)throw std::invalid_argument("Developer coverage requires the current stellar population generator.");
    coverage=ensure_stellar_coverage(static_cast<std::uint64_t>(seed),physical);
  }
  auto founding = create_founding_catalog(seed, physical, pre_warp_count,
                                          ancient_count, player_species_id);

  FreshCampaignState result;
  if(developer_full_coverage){
    result.developer_provenance=CampaignDeveloperProvenance{};
    auto &p=*result.developer_provenance;p.tools_used=true;p.full_celestial_coverage=true;
    p.coverage_generation_version=stellar_coverage_version;p.coverage_forced_system_ids=std::move(coverage.forced_system_ids);
  }
  result.seed = seed;
  result.systems = std::move(founding.systems);
  result.bodies = std::move(founding.bodies);
  result.civilizations = std::move(founding.civilizations);
  result.used_constrained_home_fallback =
      founding.used_constrained_home_fallback;

  if (population) {
    const auto removed=apply_stellar_planetary_physics(result.systems,result.bodies);
    std::unordered_set<int> habitable_systems;
    for (const auto& body : result.bodies)
      if (body.legacy_colonization_candidate) habitable_systems.insert(body.system_id);
    for (auto& system:result.systems) {
      if(auto found=removed.find(system.id);found!=removed.end())system.engulfed_planets+=found->second;
      system.has_habitable_world=habitable_systems.contains(system.id);
    }
  }

  reconcile_frozen_planet_orbits(result.systems,result.bodies);
  initialize_small_body_fields(seed,result.systems,result.bodies,true);
  result.colonies = seed_colonies(result.civilizations, result.bodies);
  if(developer_full_coverage)ensure_developer_planet_coverage(result);
  reconcile_small_body_orbits(result.systems,result.bodies);
  if(population)initialize_stellar_orbits(seed,result.systems,result.bodies,true);
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
  if(visual_footprint&&population){const auto f=galaxy_footprint_frame(population->morphology,system_count,population->state);result.core->position={static_cast<float>(f.left+f.width*.5),static_cast<float>(f.top+f.height*.5)};}
  return result;
}

} // namespace stellar::core
