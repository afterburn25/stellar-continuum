#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/stellar_population_profiles.hpp>

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <system_error>

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
  auto state = seed_fresh_campaign(
      seed, catalog, options.system_count,
      options.pre_warp_civilization_count,
      options.ancient_civilization_count, options.player_species_id, options.stellar_population);

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
  state.generation_metadata = std::move(metadata);
  validate_galactic_core_agreement(state.generation_metadata->galactic_core,
                                   state.galactic_core);
  return state;
}

} // namespace stellar::core
