#include <stellar/core/stellar_population_profiles.hpp>
#include "stellar_profiles_config.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace stellar::core {
namespace {
using Json = nlohmann::json;
constexpr std::array state_keys{"Starburst", "Active", "Mature", "Aging", "Quiescent"};
constexpr std::array morphology_keys{"Spiral", "BarredSpiral", "Lenticular", "Elliptical", "Irregular", "Ring"};
constexpr std::array region_keys{"Disk", "Arm", "Bulge", "StarForming", "Ring", "Interior",
  "InnerDisk", "InterArm", "OuterDisk", "Halo", "IrregularClump", "Bar"};
static_assert(region_keys.size() == stellar_region_count);

template<class E, std::size_t N>
const char* enum_key(E value, const std::array<const char*, N>& keys) {
  const auto index = static_cast<std::size_t>(value);
  if (index >= N) throw std::invalid_argument("Invalid stellar population profile enum");
  return keys[index];
}

void check_factor(double factor) {
  if (!std::isfinite(factor) || factor < 0)
    throw std::invalid_argument("Stellar population multiplier must be finite and nonnegative");
}

StellarSpawnWeights read_factors(const Json& json) {
  StellarSpawnWeights result{};
  const auto& definitions = stellar_object_definitions();
  if (!json.is_object() || json.size() != definitions.size())
    throw std::invalid_argument("Profile must specify exactly the canonical stellar type IDs");
  for (const auto& definition : definitions) {
    const auto index = static_cast<std::size_t>(definition.type);
    result[index] = json.at(definition.id).get<double>();
    check_factor(result[index]);
  }
  return result;
}

StellarSpawnWeights read_sparse_factors(const Json& json) {
  StellarSpawnWeights result{};
  if (!json.is_object()) throw std::invalid_argument("Rejuvenation floors must use canonical stellar type IDs");
  const auto& definitions = stellar_object_definitions();
  for (const auto& [id, factor] : json.items()) {
    const auto definition = std::find_if(definitions.begin(), definitions.end(), [&](const auto& item) { return item.id == id; });
    if (definition == definitions.end()) throw std::invalid_argument("Unknown stellar type in rejuvenation floor");
    const double value = factor.get<double>(); check_factor(value);
    result[static_cast<std::size_t>(definition->type)] = value;
  }
  return result;
}

const Json& configuration() {
  static const auto config = [] {
    auto value = Json::parse(stellar_profiles_config_json);
    for (const auto* key : morphology_keys) {
      const auto& profile = value.at("morphologies").at(key);
      read_factors(profile.at("presetModifiers"));
      for (const auto* field : {"referenceActivity", "defaultActivity"}) {
        const auto state = profile.at(field).get<std::string>();
        if (std::none_of(state_keys.begin(), state_keys.end(), [&](const auto* name) { return state == name; }))
          throw std::invalid_argument("Unknown reference/default stellar activity state");
      }
      if (profile.contains("rejuvenationMinimumBaselineMultipliers")) {
        for (const auto& [state, floors] : profile.at("rejuvenationMinimumBaselineMultipliers").items()) {
          if (std::none_of(state_keys.begin(), state_keys.end(), [&](const auto* name) { return state == name; }))
            throw std::invalid_argument("Unknown rejuvenation activity state");
          read_sparse_factors(floors);
        }
      }
    }
    for (const auto* key : state_keys) {
      const auto factors = read_factors(value.at("activities").at(key).at("stellarTypeMultipliers"));
      if (std::any_of(factors.begin(), factors.end(), [](double factor) { return factor <= 0; }))
        throw std::invalid_argument("Activity multipliers must be positive for reference-state ratios");
    }
    for (const auto* key : region_keys) read_factors(value.at("regions").at(key).at("stellarTypeMultipliers"));
    return value;
  }();
  return config;
}

const Json& activity(PopulationState state) {
  return configuration().at("activities").at(enum_key(state, state_keys));
}

StellarSpawnWeights normalized(StellarSpawnWeights weights) {
  long double total = 0;
  for (double weight : weights) { check_factor(weight); total += static_cast<long double>(weight); }
  if (!(total > 0) || !std::isfinite(total)) throw std::invalid_argument("Population has no eligible objects");
  for (auto& weight : weights) weight = static_cast<double>(static_cast<long double>(weight) / total);
  return weights;
}

struct Random {
  std::uint64_t state;
  double unit() {
    auto bits = (state += 0x9e3779b97f4a7c15ULL);
    bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
    bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
    bits ^= bits >> 31;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
  }
};

std::size_t type_index(StellarObjectType type) { return static_cast<std::size_t>(type); }
} // namespace

std::string_view stellar_population_profile_version() {
  return configuration().at("version").get_ref<const std::string&>();
}

PopulationState stellar_default_population_state(GalaxyMorphology morphology) {
  const auto& name = configuration().at("morphologies").at(enum_key(morphology, morphology_keys)).at("defaultActivity");
  for (std::size_t index = 0; index < state_keys.size(); ++index)
    if (name == state_keys[index]) return static_cast<PopulationState>(index);
  throw std::invalid_argument("Unknown default activity state");
}

std::string_view stellar_population_character(StellarPopulationOptions options) {
  enum_key(options.state, state_keys);
  if (options.state != stellar_default_population_state(options.morphology))
    return activity(options.state).at("character").get_ref<const std::string&>();
  return configuration().at("morphologies").at(enum_key(options.morphology, morphology_keys)).at("character").get_ref<const std::string&>();
}

std::string_view stellar_star_formation_label(PopulationState state) {
  return activity(state).at("starFormationLabel").get_ref<const std::string&>();
}

std::string_view stellar_region_name(StellarRegion region) {
  return configuration().at("regions").at(enum_key(region, region_keys)).at("name").get_ref<const std::string&>();
}

double stellar_star_forming_density_multiplier(PopulationState state) {
  const double result = activity(state).at("starFormingDensityMultiplier").get<double>();
  check_factor(result);
  return result;
}

StellarPopulationFactors stellar_population_factors(StellarPopulationOptions options,
    std::optional<std::uint64_t> variation_seed) {
  const auto& config = configuration();
  const auto& morphology = config.at("morphologies").at(enum_key(options.morphology, morphology_keys));
  const auto current = read_factors(activity(options.state).at("stellarTypeMultipliers"));
  const auto reference = read_factors(config.at("activities").at(morphology.at("referenceActivity").get<std::string>()).at("stellarTypeMultipliers"));
  StellarPopulationFactors result;
  result.preset_morphology_modifiers = read_factors(morphology.at("presetModifiers"));
  result.morphology_modifiers = result.preset_morphology_modifiers;
  if (morphology.contains("rejuvenationMinimumBaselineMultipliers")) {
    const auto& floors = morphology.at("rejuvenationMinimumBaselineMultipliers");
    const auto* state = enum_key(options.state, state_keys);
    if (floors.contains(state)) result.rejuvenation_floor_modifiers = read_sparse_factors(floors.at(state));
  }
  const auto& variation = config.at("variation");
  const auto domain = std::stoull(variation.at("seedDomain").get<std::string>(), nullptr, 0);
  Random random{variation_seed.value_or(0) ^ domain};
  const double maximum = variation.at("maximumFraction").get<double>();
  if (!std::isfinite(maximum) || maximum < 0 || maximum > .25)
    throw std::invalid_argument("Population variation must remain bounded to 25 percent or less");
  for (const auto& definition : stellar_object_definitions()) {
    const auto index = type_index(definition.type);
    result.base_weights[index] = static_cast<double>(definition.weight_millionths) / 1'000'000.0;
    result.activity_modifiers[index] = current[index] / reference[index];
    // Activity-conditioned eligibility can rejuvenate an old morphology whose
    // default table intentionally has zeros. This is a weight floor, never a
    // count floor: a small galaxy may still roll no examples of the type.
    result.morphology_modifiers[index] = std::max(result.morphology_modifiers[index],
      result.rejuvenation_floor_modifiers[index] / result.activity_modifiers[index]);
    double amplitude = variation.at("commonFraction").get<double>();
    if (definition.young) amplitude = variation.at("youngFraction").get<double>() * activity(options.state).at("youngVariationScale").get<double>();
    if (definition.weight_millionths <= variation.at("ultraRareMaxBaselineMillionths").get<int>())
      amplitude = variation.at("ultraRareFraction").get<double>();
    check_factor(amplitude);
    amplitude = std::min(amplitude, maximum);
    const double noise = 2 * random.unit() - 1;
    result.variation_modifiers[index] = variation_seed ? 1 + amplitude * noise : 1;
    result.target_weights[index] = result.base_weights[index] * result.morphology_modifiers[index] *
      result.activity_modifiers[index] * result.variation_modifiers[index];
  }
  result.target_weights = normalized(result.target_weights);
  return result;
}

StellarSpawnWeights stellar_profile_weights(StellarPopulationOptions options, std::optional<std::uint64_t> seed) {
  enum_key(options.morphology, morphology_keys); enum_key(options.state, state_keys);
  if (seed) return stellar_population_factors(options, seed).target_weights;
  // Config is immutable for the lifetime of this build. Sampling callers can
  // reuse all 30 exact presets without repeating JSON/key lookups per object.
  static const auto presets = [] {
    std::array<StellarSpawnWeights, morphology_keys.size() * state_keys.size()> result{};
    for (std::size_t morphology = 0; morphology < morphology_keys.size(); ++morphology)
      for (std::size_t state = 0; state < state_keys.size(); ++state)
        result[morphology * state_keys.size() + state] = stellar_population_factors(
          {static_cast<GalaxyMorphology>(morphology), static_cast<PopulationState>(state)}).target_weights;
    return result;
  }();
  return presets[static_cast<std::size_t>(options.morphology) * state_keys.size() + static_cast<std::size_t>(options.state)];
}

StellarSpawnWeights stellar_region_modifiers(StellarRegion region, PopulationState state) {
  const auto& profile = configuration().at("regions").at(enum_key(region, region_keys));
  auto weights = read_factors(profile.at("stellarTypeMultipliers"));
  const double relaxation = activity(state).at("oldRegionYoungRelaxation").get<double>();
  if (!std::isfinite(relaxation) || relaxation < 0 || relaxation > 1)
    throw std::invalid_argument("Old-region star-formation relaxation must be between zero and one");
  if (profile.at("oldPopulation").get<bool>()) {
    for (const auto& definition : stellar_object_definitions()) {
      const auto index = type_index(definition.type);
      if (definition.young && weights[index] < 1) weights[index] += relaxation * (1 - weights[index]);
    }
  }
  return weights;
}

StellarSpawnWeights stellar_region_profile_weights(StellarPopulationOptions options, StellarRegion region,
    std::optional<std::uint64_t> seed) {
  enum_key(options.morphology, morphology_keys); enum_key(options.state, state_keys); enum_key(region, region_keys);
  if (!seed) {
    static const auto presets = [] {
      std::array<StellarRegionalWeights, morphology_keys.size() * state_keys.size()> result{};
      for (std::size_t morphology = 0; morphology < morphology_keys.size(); ++morphology)
        for (std::size_t state = 0; state < state_keys.size(); ++state)
          for (std::size_t local_region = 0; local_region < stellar_region_count; ++local_region) {
            const StellarPopulationOptions preset_options{static_cast<GalaxyMorphology>(morphology), static_cast<PopulationState>(state)};
            auto weights = stellar_profile_weights(preset_options);
            const auto modifiers = stellar_region_modifiers(static_cast<StellarRegion>(local_region), preset_options.state);
            for (std::size_t type = 0; type < weights.size(); ++type) weights[type] *= modifiers[type];
            result[morphology * state_keys.size() + state][local_region] = normalized(weights);
          }
      return result;
    }();
    return presets[static_cast<std::size_t>(options.morphology) * state_keys.size() + static_cast<std::size_t>(options.state)][static_cast<std::size_t>(region)];
  }
  auto weights = stellar_profile_weights(options, seed);
  const auto modifiers = stellar_region_modifiers(region, options.state);
  for (std::size_t index = 0; index < weights.size(); ++index) weights[index] *= modifiers[index];
  return normalized(weights);
}

CalibratedStellarPopulation calibrate_stellar_population_profile(std::uint64_t seed,
    StellarPopulationOptions options, const StellarRegionCounts& counts, bool variation) {
  CalibratedStellarPopulation result;
  result.version = stellar_population_profile_version(); result.seed = seed; result.options = options;
  result.factors = stellar_population_factors(options, variation ? std::optional{seed} : std::nullopt);
  result.target_weights = result.factors.target_weights; result.region_counts = counts;
  result.calibration_modifiers.fill(1);
  long double total = 0;
  for (const auto count : counts) total += static_cast<long double>(count);
  for (std::size_t region = 0; region < stellar_region_count; ++region)
    result.regional_modifiers[region] = stellar_region_modifiers(static_cast<StellarRegion>(region), options.state);
  const auto& config = configuration().at("calibration");
  const auto iterations = config.at("maximumIterations").get<int>();
  const auto tolerance = config.at("absoluteTolerance").get<double>();
  if (iterations < 1 || iterations > 100'000 || !(tolerance > 0) || !std::isfinite(tolerance))
    throw std::invalid_argument("Invalid stellar regional calibration limits");
  for (int iteration = 0; iteration < iterations; ++iteration) {
    std::array<long double, stellar_object_type_count> marginal{};
    for (std::size_t region = 0; region < stellar_region_count; ++region) {
      auto& weights = result.region_weights[region];
      for (std::size_t type = 0; type < stellar_object_type_count; ++type)
        weights[type] = result.target_weights[type] * result.regional_modifiers[region][type] * result.calibration_modifiers[type];
      weights = normalized(weights);
      if (total > 0) for (std::size_t type = 0; type < stellar_object_type_count; ++type)
        marginal[type] += static_cast<long double>(counts[region]) / total * weights[type];
    }
    if (total == 0) return result;
    result.maximum_marginal_error = 0;
    for (std::size_t type = 0; type < stellar_object_type_count; ++type)
      result.maximum_marginal_error = std::max(result.maximum_marginal_error,
        std::abs(static_cast<double>(marginal[type]) - result.target_weights[type]));
    if (result.maximum_marginal_error <= tolerance) return result;
    for (std::size_t type = 0; type < stellar_object_type_count; ++type) {
      if (result.target_weights[type] == 0) continue;
      if (!(marginal[type] > 0)) throw std::invalid_argument("Regional profiles exclude an eligible stellar type from every populated region");
      result.calibration_modifiers[type] *= result.target_weights[type] / static_cast<double>(marginal[type]);
      check_factor(result.calibration_modifiers[type]);
    }
  }
  throw std::runtime_error("Stellar regional calibration did not converge");
}

StellarObjectType sample_stellar_profile(std::uint64_t seed, const StellarSpawnWeights& weights) {
  long double total = 0;
  for (const auto weight : weights) { check_factor(weight); total += weight; }
  if (!(total > 0) || !std::isfinite(total)) throw std::invalid_argument("Cannot sample an empty stellar population");
  Random random{seed ^ 0x5354454c524f4c4cULL};
  const long double roll = static_cast<long double>(random.unit()) * total;
  long double cumulative = 0;
  std::size_t last_eligible = 0;
  for (std::size_t type = 0; type < weights.size(); ++type) {
    if (weights[type] <= 0) continue;
    last_eligible = type; cumulative += weights[type];
    if (roll < cumulative) return static_cast<StellarObjectType>(type);
  }
  return static_cast<StellarObjectType>(last_eligible);
}

std::string stellar_profile_diagnostics(const CalibratedStellarPopulation& profile,
    const StellarRegionalObjectCounts& counts) {
  std::ostringstream output;
  output << std::setprecision(12) << "DEVELOPER-ONLY STELLAR POPULATION REPORT\nProfile: " << profile.version
    << "\nMorphology: " << morphology_name(profile.options.morphology) << "\nActivity: " << population_state_name(profile.options.state)
    << "\nSeed: " << profile.seed << "\nSystems (procedural pool): ";
  long double total = 0; for (const auto count : profile.region_counts) total += static_cast<long double>(count);
  output << total << "\nMaximum regional marginal error: " << profile.maximum_marginal_error
    << "\nType,BasePercent,PresetMorphologyModifier,EffectiveMorphologyModifier,RejuvenationBaselineFloor,ActivityModifier,SeedVariation,TargetPercent,ExpectedCount,ActualCount,ActualPercent\n";
  std::array<std::size_t, stellar_object_type_count> actual{};
  for (const auto& region : counts) for (std::size_t type = 0; type < region.size(); ++type) actual[type] += region[type];
  for (const auto& definition : stellar_object_definitions()) {
    const auto type = type_index(definition.type);
    output << definition.id << ',' << profile.factors.base_weights[type] * 100 << ',' << profile.factors.preset_morphology_modifiers[type]
      << ',' << profile.factors.morphology_modifiers[type] << ',' << profile.factors.rejuvenation_floor_modifiers[type]
      << ',' << profile.factors.activity_modifiers[type] << ',' << profile.factors.variation_modifiers[type]
      << ',' << profile.target_weights[type] * 100 << ',' << total * profile.target_weights[type] << ',' << actual[type]
      << ',' << (total > 0 ? static_cast<long double>(actual[type]) * 100 / total : 0) << '\n';
  }
  output << "\nRegion,Type,RegionalModifier,CalibrationModifier,EffectivePercent,ExpectedCount,ActualCount\n";
  for (std::size_t region = 0; region < stellar_region_count; ++region) {
    if (profile.region_counts[region] == 0) continue;
    for (const auto& definition : stellar_object_definitions()) {
      const auto type = type_index(definition.type);
      output << stellar_region_name(static_cast<StellarRegion>(region)) << ',' << definition.id << ','
        << profile.regional_modifiers[region][type] << ',' << profile.calibration_modifiers[type] << ','
        << profile.region_weights[region][type] * 100 << ','
        << static_cast<long double>(profile.region_counts[region]) * profile.region_weights[region][type] << ',' << counts[region][type] << '\n';
    }
  }
  const auto count_of = [&](StellarObjectType type) { return actual[type_index(type)]; };
  output << "\nRare summary: O=" << count_of(StellarObjectType::OHotBlueStar)
    << ", Wolf-Rayet=" << count_of(StellarObjectType::WolfRayet) << ", hypergiants=" << count_of(StellarObjectType::Hypergiant)
    << ", magnetars=" << count_of(StellarObjectType::Magnetar) << ", quiet neutron stars=" << count_of(StellarObjectType::QuietNeutronStar)
    << ", pulsars=" << count_of(StellarObjectType::Pulsar) << ", stellar black holes="
    << count_of(StellarObjectType::QuiescentBlackHole) + count_of(StellarObjectType::AccretingBlackHole) + count_of(StellarObjectType::JetBlackHole) << '\n';
  const auto young_in = [&](StellarRegion region) {
    std::size_t total_young = 0;
    for (const auto& definition : stellar_object_definitions())
      if (definition.young) total_young += counts[static_cast<std::size_t>(region)][type_index(definition.type)];
    return total_young;
  };
  std::size_t young_total = 0;
  for (const auto& definition : stellar_object_definitions()) if (definition.young) young_total += actual[type_index(definition.type)];
  if (profile.options.morphology == GalaxyMorphology::Ring)
    output << "Young objects inside ring=" << young_in(StellarRegion::Ring) << ", outside ring=" << young_total - young_in(StellarRegion::Ring) << '\n';
  if (profile.options.morphology == GalaxyMorphology::Spiral || profile.options.morphology == GalaxyMorphology::BarredSpiral)
    output << "Young objects in arms=" << young_in(StellarRegion::Arm) << ", inter-arm=" << young_in(StellarRegion::InterArm) << '\n';
  if (profile.options.morphology == GalaxyMorphology::Irregular)
    output << "Young objects in irregular clumps=" << young_in(StellarRegion::IrregularClump) << ", between clumps=" << young_total - young_in(StellarRegion::IrregularClump) << '\n';
  output << "Central supermassive black hole is excluded from this pool.\n";
  return output.str();
}
} // namespace stellar::core
