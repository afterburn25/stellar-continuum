#include "native_new_campaign_setup.hpp"
#include <stellar/core/colony_biology.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace stellar::native_setup {
namespace {
constexpr std::string_view terran = "terran_baseline";
constexpr auto sizes=stellar::core::full_galaxy_system_counts;
constexpr std::array<std::string_view, 8> size_label_keys{
    "SETUP_SIZE_SMALL", "SETUP_SIZE_MEDIUM", "SETUP_SIZE_LARGE",
    "SETUP_SIZE_HUGE", "SETUP_SIZE_VAST", "SETUP_SIZE_IMMENSE",
    "SETUP_SIZE_EXPANSIVE", "SETUP_SIZE_GRAND"};
constexpr std::array<std::string_view, 8> size_labels{
    "Small - 250 systems", "Medium - 500 systems",
    "Large - 1,000 systems", "Huge - 2,500 systems",
    "Vast - 5,000 systems", "Immense - 10,000 systems", "Expansive - 25,000 systems", "Grand - 50,000 systems"};

std::string tr(const stellar::engine::LocalizationTable *locale,
               std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

bool whitespace(const char value) noexcept {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

std::string_view trim(std::string_view value) noexcept {
  while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
  return value;
}

bool present(const std::string_view value) noexcept { return !trim(value).empty(); }

std::string biochemistry_label(const stellar::core::BiochemicalBasis value,
                               const stellar::engine::LocalizationTable *locale) {
  using enum stellar::core::BiochemicalBasis;
  switch (value) {
  case CarbonWater: return tr(locale,"SETUP_BIOCHEM_CARBON_WATER","Carbon water");
  case CarbonAmmonia: return tr(locale,"SETUP_BIOCHEM_CARBON_AMMONIA","Carbon ammonia");
  case CarbonHydrocarbon: return tr(locale,"SETUP_BIOCHEM_CARBON_HYDROCARBON","Carbon hydrocarbon");
  case SiliconChemistry: return tr(locale,"SETUP_BIOCHEM_SILICON","Silicon chemistry");
  case Synthetic: return tr(locale,"SETUP_BIOCHEM_SYNTHETIC","Synthetic");
  }
  return tr(locale,"SETUP_UNCLASSIFIED","Unclassified");
}

std::string atmosphere_label(const stellar::core::SpeciesAtmosphere value,
                             const stellar::engine::LocalizationTable *locale) {
  using enum stellar::core::SpeciesAtmosphere;
  switch (value) {
  case OxygenNitrogen: return tr(locale,"SETUP_ATMO_OXYGEN_NITROGEN","Oxygen nitrogen");
  case OxygenRich: return tr(locale,"SETUP_ATMO_OXYGEN_RICH","Oxygen rich");
  case CarbonDioxideRich: return tr(locale,"SETUP_ATMO_CO2_RICH","Carbon dioxide rich");
  case Reducing: return tr(locale,"SETUP_ATMO_REDUCING","Reducing");
  case Inert: return tr(locale,"SETUP_ATMO_INERT","Inert");
  case Vacuum: return tr(locale,"SETUP_ATMO_VACUUM","Vacuum");
  case Other: return tr(locale,"SETUP_ATMO_OTHER","Other");
  }
  return tr(locale,"SETUP_UNCLASSIFIED","Unclassified");
}

std::string solvent_label(const stellar::core::SpeciesSolvent value,
                          const stellar::engine::LocalizationTable *locale) {
  using enum stellar::core::SpeciesSolvent;
  switch (value) {
  case Water: return tr(locale,"SETUP_SOLVENT_WATER","Water");
  case Ammonia: return tr(locale,"SETUP_SOLVENT_AMMONIA","Ammonia");
  case Hydrocarbon: return tr(locale,"SETUP_SOLVENT_HYDROCARBON","Hydrocarbon");
  case Other: return tr(locale,"SETUP_SOLVENT_OTHER","Other");
  case None: return tr(locale,"SETUP_SOLVENT_NONE","None");
  }
  return tr(locale,"SETUP_UNCLASSIFIED","Unclassified");
}

NativeSpeciesSetupOption copy_species(
    const stellar::core::SpeciesEnvironmentProfile &source,
    const stellar::engine::LocalizationTable *locale) {
  const auto& biology=stellar::core::species_biology_profile(source.id);
  return {source.id,
          source.display_name,
          source.biochemistry,
          source.gravity_g,
          source.temperature_kelvin,
          source.pressure_kpa,
          source.preferred_atmosphere,
          source.biological_solvent,
          source.requires_immersion,
          source.can_operate_in_vacuum_unprotected,
          source.radiation_tolerance,
          source.breathable_atmospheres,
          source.compatible_solvents,
          biochemistry_label(source.biochemistry,locale),
          atmosphere_label(source.preferred_atmosphere,locale),
          solvent_label(source.biological_solvent,locale),biology.typical_adult_mass_kg,biology.baseline_lifespan_years,biology.reproductive_maturity_years,biology.baseline_metabolic_demand};
}
} // namespace

NativeNewCampaignSetupView NativeNewCampaignSetupController::build(bool developer_mode) const {
  NativeNewCampaignSetupView result;
  result.developer_mode=developer_mode;
  for (const auto &profile : stellar::core::species_environment_profiles())
    result.species.push_back(copy_species(profile,locale_));
  for (std::size_t index = 0; index < sizes.size(); ++index)
    result.size_presets.push_back(
        {sizes[index], tr(locale_,size_label_keys[index],size_labels[index]), sizes[index] == 500});
  result.default_species_id = terran;
  result.default_system_count = 500;
  // These mirror the established Sandbox choices.  Counts include the player
  // in the pre-warp roster because that is the canonical seeder's contract.
  result.pre_warp_civilization_presets = {
      {1, tr(locale_,"SETUP_PREWARP_NONE","None"), false},
      {4, tr(locale_,"SETUP_PREWARP_SPARSE","Sparse · 3"), false},
      {6, tr(locale_,"SETUP_PREWARP_STANDARD","Standard · 5"), true},
      {9, tr(locale_,"SETUP_PREWARP_CROWDED","Crowded · 8"), false},
      {13, tr(locale_,"SETUP_PREWARP_PACKED","Packed · 12"), false}};
  result.ancient_civilization_presets = {
      {0, tr(locale_,"SETUP_ANCIENTS_NONE","None"), false},
      {1, tr(locale_,"SETUP_ANCIENTS_RARE","Rare"), true},
      {2, tr(locale_,"SETUP_ANCIENTS_STANDARD","Standard"), false}};
  result.default_pre_warp_civilization_count = 6;
  result.default_ancient_civilization_count = 1;
  return result;
}



NativeNewCampaignSetupAssessment NativeNewCampaignSetupController::prepare(
    const NativeNewCampaignSetupInput &input,bool developer_mode) const {
  if(!developer_mode&&(input.developer_research.complete_normal_research||input.developer_research.complete_special_research||input.developer_full_coverage||input.developer_full_exploration))
    return {false,tr(locale_,"SETUP_ERR_DEVELOPER","Overrides require a developer session."),{}};
  try{(void)stellar::core::stellar_population_weights(input.stellar_population);}catch(const std::exception&){return {false,tr(locale_,"SETUP_ERR_POPULATION","Choose a supported morphology and population state."),{}};}
  const auto seed = parse_campaign_seed(input.seed_text);
  if (!seed)
    return {false,
            tr(locale_,"SETUP_ERR_SEED","Enter a whole-number seed from -9223372036854775808 to "
            "9223372036854775807."),
            {}};
  if (std::ranges::find(sizes, input.system_count) == sizes.end())
    return {false, tr(locale_,"SETUP_ERR_SIZE","Choose a supported galaxy size."), {}};
  const auto profiles = stellar::core::species_environment_profiles();
  if (std::ranges::find(profiles, input.player_species_id,
                        &stellar::core::SpeciesEnvironmentProfile::id) ==
      profiles.end())
    return {false, tr(locale_,"SETUP_ERR_SPECIES","Choose an available species."), {}};
  constexpr std::array pre_warp_counts{1, 4, 6, 9, 13};
  constexpr std::array ancient_counts{0, 1, 2};
  if (std::ranges::find(pre_warp_counts, input.pre_warp_civilization_count) ==
          pre_warp_counts.end() ||
      std::ranges::find(ancient_counts, input.ancient_civilization_count) ==
          ancient_counts.end())
    return {false, tr(locale_,"SETUP_ERR_COUNTS","Choose supported rival and ancient civilization counts."), {}};
  if (!present(input.created_at_utc))
    return {false, tr(locale_,"SETUP_ERR_TIMESTAMP","Campaign creation time is unavailable."), {}};

  stellar::core::PersistableFreshCampaignOptions options{
      input.created_at_utc, input.system_count,
      input.pre_warp_civilization_count, input.ancient_civilization_count,
      input.player_species_id, input.stellar_population,input.developer_full_coverage};
  stellar::core::GalaxyGenerationConfig configuration;
  configuration.base_seed=*seed;configuration.morphology=input.stellar_population.morphology;
  configuration.requested_population=input.requested_population;configuration.system_count=input.system_count;
  configuration.pre_warp_count=input.pre_warp_civilization_count;configuration.ancient_count=input.ancient_civilization_count;
  configuration.player_species_id=input.player_species_id;configuration.developer_full_coverage=input.developer_full_coverage;
  options.configuration=stellar::core::resolve_galaxy_configuration(std::move(configuration));
  options.stellar_population=stellar::core::StellarPopulationOptions{options.configuration->morphology,options.configuration->resolved_population};
  return {true, tr(locale_,"SETUP_READY","Campaign setup is ready."),
          NativePreparedNewCampaign{*seed, std::move(options),developer_mode,input.developer_research,input.developer_full_exploration}};
}

stellar::core::IntegratedAdaptiveCampaignRuntime
NativeNewCampaignSetupController::create_runtime(
    const NativePreparedNewCampaign &prepared,
    stellar::core::AdaptiveResearchStrategicRuntime research,
    const std::span<const stellar::core::CatalogStar> catalog) const {
  auto world=stellar::core::seed_persistable_fresh_campaign(prepared.seed(),catalog,prepared.options());
  if(prepared.developer_mode()){
    if(!world.developer_provenance)world.developer_provenance=stellar::core::CampaignDeveloperProvenance{};
    if(prepared.developer_full_exploration())stellar::core::fully_explore_developer_galaxy(world);
  }
  auto runtime=stellar::core::IntegratedAdaptiveCampaignRuntime::create_fresh(std::move(research),std::move(world));
  if(prepared.developer_mode()){
    if(!runtime.world().campaign().developer_provenance)runtime.world().campaign().developer_provenance=stellar::core::CampaignDeveloperProvenance{};
    (void)stellar::core::initialize_developer_research(runtime,prepared.developer_research());
  }
  return runtime;
}

} // namespace stellar::native_setup
