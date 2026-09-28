#pragma once
#include <stellar/core/stellar_object.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stellar::core {
inline constexpr std::size_t stellar_region_count = 12;
using StellarSpawnWeights = std::array<double, stellar_object_type_count>;
using StellarRegionCounts = std::array<std::size_t, stellar_region_count>;
using StellarRegionalWeights = std::array<StellarSpawnWeights, stellar_region_count>;
using StellarRegionalObjectCounts = std::array<std::array<std::size_t, stellar_object_type_count>, stellar_region_count>;

struct StellarPopulationFactors {
  StellarSpawnWeights base_weights{}, morphology_modifiers{}, activity_modifiers{}, variation_modifiers{};
  StellarSpawnWeights preset_morphology_modifiers{}, rejuvenation_floor_modifiers{};
  StellarSpawnWeights target_weights{};
};

// Regional tables are calibrated against the actual map's region counts. This
// preserves the galaxy-wide target in expectation, without assigning quotas.
struct CalibratedStellarPopulation {
  std::string version;
  std::uint64_t seed{};
  StellarPopulationOptions options;
  StellarPopulationFactors factors;
  StellarSpawnWeights target_weights{};
  StellarRegionCounts region_counts{};
  StellarRegionalWeights regional_modifiers{}, region_weights{};
  StellarSpawnWeights calibration_modifiers{};
  double maximum_marginal_error{};
};

std::string_view stellar_population_profile_version();
PopulationState stellar_default_population_state(GalaxyMorphology);
std::string_view stellar_population_character(StellarPopulationOptions);
std::string_view stellar_star_formation_label(PopulationState);
std::string_view stellar_region_name(StellarRegion);
double stellar_star_forming_density_multiplier(PopulationState);
StellarPopulationFactors stellar_population_factors(StellarPopulationOptions,
    std::optional<std::uint64_t> variation_seed = std::nullopt);
StellarSpawnWeights stellar_profile_weights(StellarPopulationOptions,
    std::optional<std::uint64_t> variation_seed = std::nullopt);
StellarSpawnWeights stellar_region_modifiers(StellarRegion, PopulationState);
StellarSpawnWeights stellar_region_profile_weights(StellarPopulationOptions, StellarRegion,
    std::optional<std::uint64_t> variation_seed = std::nullopt);
CalibratedStellarPopulation calibrate_stellar_population_profile(std::uint64_t seed,
    StellarPopulationOptions, const StellarRegionCounts&, bool variation = true);
StellarObjectType sample_stellar_profile(std::uint64_t seed, const StellarSpawnWeights&);
// Developer diagnostics only. Never pass these undiscovered counts to player UI.
std::string stellar_profile_diagnostics(const CalibratedStellarPopulation&,
    const StellarRegionalObjectCounts&);
} // namespace stellar::core
