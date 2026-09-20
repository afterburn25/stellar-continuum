#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/engine/foundation.hpp>

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace stellar::core {
namespace {

std::string invariant_seed(std::int64_t seed) {
  char text[32]{};
  const auto [end, error] = std::to_chars(text, text + sizeof(text), seed);
  if (error != std::errc{})
    throw std::runtime_error("Cannot format campaign seed.");
  return {text, end};
}

GalacticCoreMetadata named_core(const GalacticCore &core) {
  return {GalacticCoreMetadata::stable_landmark_key, core.position.x,
          core.position.y, core.exclusion_radius};
}

} // namespace

FreshCampaignState seed_persistable_fresh_campaign(
    std::int64_t seed, std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options) {
  const auto generation_seed=options.configuration?static_cast<std::int64_t>(galaxy_generation_stream(*options.configuration)):seed;
  if(options.configuration&&(options.configuration->base_seed!=seed||options.configuration->system_count!=options.system_count||options.configuration->pre_warp_count!=options.pre_warp_civilization_count||options.configuration->ancient_count!=options.ancient_civilization_count||options.configuration->player_species_id!=options.player_species_id||options.configuration->developer_full_coverage!=options.developer_full_coverage||!options.stellar_population||options.configuration->morphology!=options.stellar_population->morphology||options.configuration->resolved_population!=options.stellar_population->state))throw std::invalid_argument("Generation options disagree with canonical configuration");
  auto state = seed_fresh_campaign(
      generation_seed, catalog, options.system_count,
      options.pre_warp_civilization_count,
      options.ancient_civilization_count, options.player_species_id, options.stellar_population,options.developer_full_coverage,options.configuration.has_value());
  state.seed=seed;

  if (!state.core)
    throw std::logic_error(
        "Full-galaxy campaign generation did not produce its galactic core.");

  auto core = named_core(*state.core);
  GalaxyGenerationMetadata metadata{
      invariant_seed(seed),
      seed,
      "full-galaxy-compact-v1",
      options.created_at_utc,
      options.system_count,
      "Full galaxy",
      "Dwarf-heavy",
      "Common",
      "Uncommon",
      2,
      std::max(0, options.pre_warp_civilization_count - 1),
      options.ancient_civilization_count == 0
          ? "None"
          : options.ancient_civilization_count == 1 ? "Rare" : "Standard",
      "Standard",
      "Early Space Age",
      "Standard",
      "milky-way-full-500-v1",
      options.player_species_id,
      "Standard",
      core,
  };

  if (options.stellar_population) {
    core.black_hole=generate_central_black_hole(static_cast<std::uint64_t>(seed));
    metadata.galactic_core=core;
    metadata.stellar_population=options.stellar_population;
    metadata.stellar_profile_version=std::string(stellar_population_profile_version());
    metadata.generator_version="stellar-population-v1";
    metadata.galaxy_shape=std::string(morphology_name(options.stellar_population->morphology));
    metadata.stellar_variety=std::string(population_state_name(options.stellar_population->state));
  }
  state.galactic_core = core;
  if(options.configuration){
    metadata.configuration=options.configuration;
    metadata.generator_version=options.configuration->generator_version;
    metadata.art_profile_version=options.configuration->asset_set_version;
    if(options.configuration->generator_version!="galaxy-configuration-v2")
      metadata.phenomena=generate_galaxy_phenomena(*options.configuration,state.systems);
    std::unordered_map<int, PlanetaryBody*> first_body;
    if(metadata.phenomena)for(auto& body:state.bodies)first_body.try_emplace(body.system_id,&body);
    if(metadata.phenomena)for(auto& system:state.systems){
      if(system.id==sol_system_id)continue;
      const auto context=phenomenon_context(&*metadata.phenomena,system.position.x,system.position.y,system.id);
      stellar::engine::DeterministicRandom discoveries(context.local_seed^0x4f50504f5254554eULL);
      const bool anomaly=discoveries.unit_double()<context.effects.anomaly_bias;
      const bool resource=discoveries.unit_double()<context.effects.resource_bias;
      const auto found=first_body.find(system.id);
      if(found!=first_body.end()){
        auto* body=found->second;
        system.has_anomaly|=anomaly;system.has_rare_resource|=resource;
        body->has_anomaly|=anomaly;body->has_rare_resource|=resource;
      }
    }
    if(galaxy_has_central_black_hole(*options.configuration)){
      core.black_hole=generate_central_black_hole(static_cast<std::uint64_t>(generation_seed));
      metadata.galactic_core=core;state.galactic_core=core;
    }else{
      metadata.galactic_core.reset();state.galactic_core.reset();state.core.reset();
    }
  }
  state.generation_metadata = std::move(metadata);
  validate_galactic_core_agreement(state.generation_metadata->galactic_core,
                                   state.galactic_core);
  return state;
}

} // namespace stellar::core
