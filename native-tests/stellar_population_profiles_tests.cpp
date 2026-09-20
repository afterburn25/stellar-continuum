#include <stellar/core/stellar_population_profiles.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace stellar::core;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void normalized(const StellarSpawnWeights& weights) {
  check(std::abs(std::accumulate(weights.begin(), weights.end(), 0.0) - 1) < 2e-14, "Weights normalize to 100 percent");
  for (auto value : weights) check(std::isfinite(value) && value >= 0, "Weights remain finite and nonnegative");
}
std::size_t index(StellarObjectType type) { return static_cast<std::size_t>(type); }
double young(const StellarSpawnWeights& weights) {
  double result = 0;
  for (const auto& definition : stellar_object_definitions()) if (definition.young) result += weights[index(definition.type)];
  return result;
}
void golden(GalaxyMorphology morphology, const StellarSpawnWeights& percentages) {
  const auto weights = stellar_profile_weights({morphology, stellar_default_population_state(morphology)});
  const double total = std::accumulate(percentages.begin(), percentages.end(), 0.0);
  for (std::size_t type = 0; type < weights.size(); ++type)
    check(std::abs(weights[type] - percentages[type] / total) < 2e-15, "Supplied preset matches exactly after rounding normalization");
}
}
int main(int argc, char** argv) try {
  std::cout << std::unitbuf;
  check(stellar_object_type_count == 23 && stellar_region_count == 12, "Canonical types and region enum reused");
  const StellarPopulationOptions spiral{GalaxyMorphology::Spiral, PopulationState::Mature};
  const auto baseline = stellar_profile_weights(spiral);
  for (const auto& definition : stellar_object_definitions())
    check(std::abs(baseline[index(definition.type)] - definition.weight_millionths / 1'000'000.0) < 1e-15, "Mature spiral preserves authoritative baseline");
  check(baseline == stellar_profile_weights({GalaxyMorphology::BarredSpiral, PopulationState::Mature}), "Central bar does not change global baseline");
  golden(GalaxyMorphology::Lenticular, {64.6412,11.0695,11.0115,5.2265,4.8245,1.3937,.8443,.6701,.1081,.0064,.040204,.103637,.0500,.0021,.006031,.000447,.000134,.000045,.000357,.000071,.000938,.000089,.000018});
  golden(GalaxyMorphology::Elliptical, {64.7114,11.1583,12.2167,4.5295,4.5731,.9512,.9451,.6899,.0479,.0010,.021777,.114981,.0348,.0005,.003266,.000044,.000004,0,.000087,0,.000314,.000003,0});
  golden(GalaxyMorphology::Irregular, {64.4418,10.4743,6.5949,6.8443,4.9878,3.2276,.6983,.5985,.9449,.5586,.144091,.075370,.1064,.0887,.034914,.044336,.027710,.0499,.011084,.026601,.006650,.008867,.004434});
  golden(GalaxyMorphology::Ring, {64.4224,10.4711,7.1867,6.7640,4.9863,3.0438,.6981,.6179,.8645,.4162,.130076,.079780,.0997,.0650,.027641,.032519,.021679,.0325,.008130,.017343,.004878,.006504,.003252});
  std::cout << "Exact supplied profiles passed.\n";

  const StellarRegionCounts regions{101,379,187,43,121,223,67,317,101,31,93,79};
  for (int morphology = 0; morphology < 6; ++morphology) for (int activity = 0; activity < 5; ++activity) {
    const StellarPopulationOptions options{static_cast<GalaxyMorphology>(morphology), static_cast<PopulationState>(activity)};
    normalized(stellar_profile_weights(options));
    check(stellar_profile_weights(options) == stellar_population_factors(options).target_weights, "Cached presets equal their authoritative factor calculation");
    auto plan = calibrate_stellar_population_profile(482100, options, regions);
    check(plan.region_weights == calibrate_stellar_population_profile(482100, options, regions).region_weights, "Calibrated seeded profiles are reproducible");
    const double total = static_cast<double>(std::accumulate(regions.begin(), regions.end(), std::size_t{}));
    StellarSpawnWeights aggregate{};
    for (std::size_t region = 0; region < stellar_region_count; ++region) {
      normalized(stellar_region_profile_weights(options, static_cast<StellarRegion>(region)));
      normalized(plan.region_weights[region]);
      for (std::size_t type = 0; type < stellar_object_type_count; ++type)
        aggregate[type] += static_cast<double>(regions[region]) / total * plan.region_weights[region][type];
    }
    for (std::size_t type = 0; type < stellar_object_type_count; ++type)
      check(std::abs(aggregate[type] - plan.target_weights[type]) < 1.01e-13, "Regional placement preserves global target in expectation");
  }
  StellarRegionCounts only_arm{}; only_arm[static_cast<std::size_t>(StellarRegion::Arm)] = 250;
  const auto single = calibrate_stellar_population_profile(91, spiral, only_arm, false);
  for (std::size_t type = 0; type < stellar_object_type_count; ++type)
    check(std::abs(single.region_weights[static_cast<std::size_t>(StellarRegion::Arm)][type] - baseline[type]) < 1.01e-13, "Single-region calibration remains unbiased");
  const auto empty = calibrate_stellar_population_profile(3, spiral, {});
  normalized(empty.region_weights[0]);
  std::cout << "All 360 regional combinations and global calibration passed.\n";

  const auto elliptical = stellar_profile_weights({GalaxyMorphology::Elliptical, PopulationState::Quiescent});
  for (auto type : {StellarObjectType::OHotBlueStar, StellarObjectType::WolfRayet, StellarObjectType::Hypergiant})
    check(elliptical[index(type)] == 0, "Default old ellipticals have no forced massive-star formation");
  const auto active_elliptical = stellar_population_factors({GalaxyMorphology::Elliptical, PopulationState::Active});
  const auto burst_elliptical = stellar_population_factors({GalaxyMorphology::Elliptical, PopulationState::Starburst});
  for (auto type : {StellarObjectType::OHotBlueStar, StellarObjectType::WolfRayet, StellarObjectType::Hypergiant}) {
    const auto object = index(type);
    check(active_elliptical.target_weights[object] > 0, "Explicitly Active elliptical may rejuvenate otherwise absent populations");
    check(burst_elliptical.target_weights[object] > active_elliptical.target_weights[object], "Starburst rejuvenation is stronger than Active");
    check(active_elliptical.preset_morphology_modifiers[object] == 0, "Rejuvenation does not rewrite the supplied default preset");
    check(active_elliptical.rejuvenation_floor_modifiers[object] > 0, "Rejuvenation is explicitly visible in diagnostics");
    check(std::abs(active_elliptical.morphology_modifiers[object] * active_elliptical.activity_modifiers[object]
      - active_elliptical.rejuvenation_floor_modifiers[object]) < 1e-15, "Configured rejuvenation floors preserve the multiplicative factor model");
    for (auto state : {PopulationState::Mature, PopulationState::Aging, PopulationState::Quiescent})
      check(stellar_profile_weights({GalaxyMorphology::Elliptical, state})[object] == 0, "Rejuvenation only applies to explicitly Active/Starburst settings");
  }
  const auto irregular = stellar_profile_weights({GalaxyMorphology::Irregular, PopulationState::Active});
  for (auto type : {StellarObjectType::OHotBlueStar, StellarObjectType::BBlueWhiteStar, StellarObjectType::WolfRayet, StellarObjectType::BlueSupergiant})
    check(irregular[index(type)] > baseline[index(type)] * 3, "Active irregular enhances massive young populations");
  const auto lenticular = stellar_profile_weights({GalaxyMorphology::Lenticular, PopulationState::Aging});
  check(lenticular[index(StellarObjectType::WhiteDwarf)] / young(lenticular) > baseline[index(StellarObjectType::WhiteDwarf)] / young(baseline) * 3, "Lenticular favors old remnants over young stars");
  auto ring_plan = calibrate_stellar_population_profile(72, {GalaxyMorphology::Ring, PopulationState::Mature}, regions, false);
  check(young(ring_plan.region_weights[4]) > young(ring_plan.region_weights[5]) * 10, "Ring is visibly younger than interior");
  auto irregular_plan = calibrate_stellar_population_profile(72, {GalaxyMorphology::Irregular, PopulationState::Active}, regions, false);
  check(young(irregular_plan.region_weights[10]) > young(irregular_plan.region_weights[5]) * 5, "Irregular young stars cluster rather than spreading uniformly");
  check(stellar_star_forming_density_multiplier(PopulationState::Starburst) > stellar_star_forming_density_multiplier(PopulationState::Active), "Starburst increases regional density");
  check(stellar_star_forming_density_multiplier(PopulationState::Active) > stellar_star_forming_density_multiplier(PopulationState::Mature), "Active increases regional density");
  check(young(stellar_profile_weights({GalaxyMorphology::Spiral,PopulationState::Starburst})) > young(baseline) * 3, "Starburst is a separate modifier");
  std::cout << "Morphology, activity and spatial contrasts passed.\n";

  constexpr std::size_t draws = 1'000'000;
  std::array<std::size_t, stellar_object_type_count> actual{};
  for (std::uint64_t seed = 0; seed < draws; ++seed)
    ++actual[index(sample_stellar_profile(seed, stellar_profile_weights(spiral)))];
  for (std::size_t type = 0; type < actual.size(); ++type) {
    const double expected = static_cast<double>(draws) * baseline[type];
    const double sigma = std::sqrt(expected * (1 - baseline[type]));
    check(std::abs(static_cast<double>(actual[type]) - expected) < 6 * sigma + 3, "Million-seed sample converges to baseline");
  }
  std::size_t absent_small_galaxies = 0;
  for (std::uint64_t seed = 0; seed < 1000; ++seed) {
    const auto a = stellar_population_factors(spiral, seed);
    const auto b = stellar_population_factors(spiral, seed);
    check(a.target_weights == b.target_weights, "Seeded variation exactly deterministic");
    check(a.target_weights != stellar_population_factors(spiral, seed + 1).target_weights, "Different seeds vary the profile");
    for (std::size_t type = 0; type < stellar_object_type_count; ++type) {
      check(a.variation_modifiers[type] >= .8 && a.variation_modifiers[type] <= 1.2, "Variation is bounded");
      check(a.target_weights[type] < baseline[type] * 1.3, "Rare frequencies cannot explode after normalization");
    }
    check(a.variation_modifiers[index(StellarObjectType::Hypergiant)] == 1, "Ultra-rare types rely on probabilistic counts, not noisy multipliers");
    bool hypergiant = false;
    for (std::uint64_t system = 0; system < 250; ++system) {
      const auto roll = seed * 250 + system;
      const auto first = sample_stellar_profile(roll, a.target_weights);
      check(first == sample_stellar_profile(roll, b.target_weights), "Same seed/settings reproduces placement");
      hypergiant = hypergiant || first == StellarObjectType::Hypergiant;
    }
    if (!hypergiant) ++absent_small_galaxies;
  }
  check(absent_small_galaxies > 950, "Small galaxies naturally lack ultra-rare types; no category filling");
  std::size_t absent_rejuvenated_galaxies = 0;
  for (std::uint64_t seed = 0; seed < 256; ++seed) {
    const auto weights = stellar_profile_weights({GalaxyMorphology::Elliptical, PopulationState::Starburst}, seed);
    bool hypergiant = false;
    for (std::uint64_t system = 0; system < 250; ++system)
      hypergiant = hypergiant || sample_stellar_profile(seed * 250 + system, weights) == StellarObjectType::Hypergiant;
    if (!hypergiant) ++absent_rejuvenated_galaxies;
  }
  check(absent_rejuvenated_galaxies > 240, "Rejuvenation is a probability floor, never a guaranteed rare-object count");
  for (const auto& definition : stellar_object_definitions())
    check(definition.id.find("supermassive") == std::string::npos, "SMBH excluded from normal pool");
  bool invalid = false;
  try { sample_stellar_profile(1, {}); } catch (const std::invalid_argument&) { invalid = true; }
  check(invalid, "Zero-eligibility sampling fails explicitly");
  std::cout << "Million-seed convergence, variation, placement determinism, and rarity passed.\n";

  StellarRegionalObjectCounts regional_counts{};
  for (std::size_t region = 0; region < stellar_region_count; ++region)
    for (std::size_t system = 0; system < regions[region]; ++system)
      ++regional_counts[region][index(sample_stellar_profile(1000 * region + system, ring_plan.region_weights[region]))];
  auto report = stellar_profile_diagnostics(ring_plan, regional_counts);
  check(report.find("inside ring=") != std::string::npos && report.find("RegionalModifier") != std::string::npos, "Developer report includes spatial and modifier diagnostics");
  check(report.find("RejuvenationBaselineFloor") != std::string::npos, "Developer report identifies activity-conditioned eligibility floors");
  if (argc > 1) { std::ofstream out(argv[1]); out << report; check(static_cast<bool>(out), "Diagnostic report written"); }
  std::cout << "Population profile tests passed.\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
