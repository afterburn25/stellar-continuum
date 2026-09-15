#include "native_new_campaign_setup.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace stellar::native_setup {
namespace {
constexpr std::string_view terran = "terran_baseline";
constexpr std::array<int, 4> sizes{250, 500, 1000, 2500};
constexpr std::array<std::string_view, 4> size_labels{
    "Small - 250 systems", "Medium - 500 systems",
    "Large - 1,000 systems", "Huge - 2,500 systems"};

bool whitespace(const char value) noexcept {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

std::string_view trim(std::string_view value) noexcept {
  while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
  return value;
}

std::optional<std::int64_t> numeric_seed(std::string_view value) noexcept {
  value = trim(value);
  if (value.empty()) return {};
  if (value.front() == '+') {
    value.remove_prefix(1);
    if (value.empty() || value.front() < '0' || value.front() > '9') return {};
  }
  std::int64_t result{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size()) return {};
  return result;
}

bool present(const std::string_view value) noexcept { return !trim(value).empty(); }

std::string biochemistry_label(const stellar::core::BiochemicalBasis value) {
  using enum stellar::core::BiochemicalBasis;
  switch (value) {
  case CarbonWater: return "Carbon water";
  case CarbonAmmonia: return "Carbon ammonia";
  case CarbonHydrocarbon: return "Carbon hydrocarbon";
  case SiliconChemistry: return "Silicon chemistry";
  case Synthetic: return "Synthetic";
  }
  return "Unclassified";
}

std::string atmosphere_label(const stellar::core::SpeciesAtmosphere value) {
  using enum stellar::core::SpeciesAtmosphere;
  switch (value) {
  case OxygenNitrogen: return "Oxygen nitrogen";
  case OxygenRich: return "Oxygen rich";
  case CarbonDioxideRich: return "Carbon dioxide rich";
  case Reducing: return "Reducing";
  case Inert: return "Inert";
  case Vacuum: return "Vacuum";
  case Other: return "Other";
  }
  return "Unclassified";
}

std::string solvent_label(const stellar::core::SpeciesSolvent value) {
  using enum stellar::core::SpeciesSolvent;
  switch (value) {
  case Water: return "Water";
  case Ammonia: return "Ammonia";
  case Hydrocarbon: return "Hydrocarbon";
  case Other: return "Other";
  case None: return "None";
  }
  return "Unclassified";
}

NativeSpeciesSetupOption copy_species(
    const stellar::core::SpeciesEnvironmentProfile &source) {
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
          biochemistry_label(source.biochemistry),
          atmosphere_label(source.preferred_atmosphere),
          solvent_label(source.biological_solvent)};
}
} // namespace

NativeNewCampaignSetupView NativeNewCampaignSetupController::build() const {
  NativeNewCampaignSetupView result;
  for (const auto &profile : stellar::core::species_environment_profiles())
    result.species.push_back(copy_species(profile));
  for (std::size_t index = 0; index < sizes.size(); ++index)
    result.size_presets.push_back(
        {sizes[index], std::string{size_labels[index]}, sizes[index] == 500});
  result.default_species_id = terran;
  result.default_system_count = 500;
  result.fixed_pre_warp_civilization_count = 6;
  result.fixed_ancient_civilization_count = 1;
  return result;
}

NativeNewCampaignSetupAssessment NativeNewCampaignSetupController::prepare(
    const NativeNewCampaignSetupInput &input) const {
  const auto seed = numeric_seed(input.seed_text);
  if (!seed)
    return {false,
            "Enter a whole-number seed from -9223372036854775808 to "
            "9223372036854775807.",
            {}};
  if (std::ranges::find(sizes, input.system_count) == sizes.end())
    return {false, "Choose a supported galaxy size.", {}};
  const auto profiles = stellar::core::species_environment_profiles();
  if (std::ranges::find(profiles, input.player_species_id,
                        &stellar::core::SpeciesEnvironmentProfile::id) ==
      profiles.end())
    return {false, "Choose an available species.", {}};
  if (!present(input.created_at_utc))
    return {false, "Campaign creation time is unavailable.", {}};

  stellar::core::PersistableFreshCampaignOptions options{
      input.created_at_utc, input.system_count, 6, 1,
      input.player_species_id};
  return {true, "Campaign setup is ready.",
          NativePreparedNewCampaign{*seed, std::move(options)}};
}

stellar::core::IntegratedAdaptiveCampaignRuntime
NativeNewCampaignSetupController::create_runtime(
    const NativePreparedNewCampaign &prepared,
    stellar::core::AdaptiveResearchStrategicRuntime research,
    const std::span<const stellar::core::CatalogStar> catalog) const {
  return stellar::core::IntegratedAdaptiveCampaignRuntime::create_fresh(
      std::move(research), stellar::core::seed_persistable_fresh_campaign(
                               prepared.seed(), catalog, prepared.options()));
}

} // namespace stellar::native_setup
