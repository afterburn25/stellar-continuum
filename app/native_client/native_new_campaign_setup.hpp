#pragma once

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/developer_campaign.hpp>

#include <cstdint>
#include <charconv>
#include <string_view>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_setup {
[[nodiscard]] inline std::optional<std::int64_t> parse_campaign_seed(std::string_view value) noexcept {
  constexpr std::string_view space=" \t\r\n\f\v";
  const auto first=value.find_first_not_of(space);if(first==std::string_view::npos)return {};
  value=value.substr(first,value.find_last_not_of(space)-first+1);
  if(value.front()=='+'){value.remove_prefix(1);if(value.empty()||value.front()<'0'||value.front()>'9')return {};}
  std::int64_t result{};const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);
  if(error!=std::errc{}||end!=value.data()+value.size())return {};return result;
}

struct NativeSpeciesSetupOption {
  std::string id, display_name;
  stellar::core::BiochemicalBasis biochemistry{};
  stellar::core::ToleranceBand gravity_g, temperature_kelvin, pressure_kpa;
  stellar::core::SpeciesAtmosphere preferred_atmosphere{};
  stellar::core::SpeciesSolvent biological_solvent{};
  bool requires_immersion{}, can_operate_in_vacuum_unprotected{};
  double radiation_tolerance{};
  std::vector<stellar::core::SpeciesAtmosphere> breathable_atmospheres;
  std::vector<stellar::core::SpeciesSolvent> compatible_solvents;
  std::string biochemistry_label, preferred_atmosphere_label,
      biological_solvent_label;
  double adult_mass_kg{},lifespan_years{},maturity_years{},metabolic_demand{};
};

struct NativeGalaxySizeOption {
  int system_count{};
  std::string label;
  bool recommended{};
};
struct NativeCivilizationCountOption {
  int count{};
  std::string label;
  bool recommended{};
};

struct NativeNewCampaignSetupView {
  std::vector<NativeSpeciesSetupOption> species;
  std::vector<NativeGalaxySizeOption> size_presets;
  std::vector<NativeCivilizationCountOption> pre_warp_civilization_presets,
      ancient_civilization_presets;
  std::string default_species_id;
  int default_system_count{}, default_pre_warp_civilization_count{},
      default_ancient_civilization_count{};
  bool developer_mode{};
};

struct NativeNewCampaignSetupInput {
  std::string seed_text;
  int system_count{500};
  std::string player_species_id{"terran_baseline"};
  std::string created_at_utc;
  int pre_warp_civilization_count{6};
  int ancient_civilization_count{1};
  stellar::core::StellarPopulationOptions stellar_population;
  stellar::core::DeveloperResearchOptions developer_research;
  bool developer_full_coverage{};
  stellar::core::PopulationSelection requested_population{stellar::core::PopulationSelection::Random};
  bool developer_full_exploration{};
};

class NativePreparedNewCampaign final {
public:
  [[nodiscard]] std::int64_t seed() const noexcept { return seed_; }
  [[nodiscard]] const stellar::core::PersistableFreshCampaignOptions &
  options() const noexcept {
    return options_;
  }
  [[nodiscard]] bool developer_mode() const noexcept {return developer_mode_;}
  [[nodiscard]] stellar::core::DeveloperResearchOptions developer_research() const noexcept {return developer_research_;}
  [[nodiscard]] bool developer_full_exploration() const noexcept {return developer_full_exploration_;}

private:
  friend class NativeNewCampaignSetupController;
  NativePreparedNewCampaign(
      std::int64_t seed,
      stellar::core::PersistableFreshCampaignOptions options,bool developer_mode,
      stellar::core::DeveloperResearchOptions research,bool full_exploration)
      : seed_(seed), options_(std::move(options)),developer_mode_(developer_mode),developer_research_(research),developer_full_exploration_(full_exploration) {}
  std::int64_t seed_{};
  stellar::core::PersistableFreshCampaignOptions options_;
  bool developer_mode_{};
  stellar::core::DeveloperResearchOptions developer_research_;
  bool developer_full_exploration_{};
};

struct NativeNewCampaignSetupAssessment {
  bool accepted{};
  std::string message;
  std::optional<NativePreparedNewCampaign> prepared;
};

class NativeNewCampaignSetupController final {
public:
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept {locale_=table;}
  [[nodiscard]] NativeNewCampaignSetupView build(bool developer_mode=false) const;
  [[nodiscard]] NativeNewCampaignSetupAssessment
  prepare(const NativeNewCampaignSetupInput &,bool developer_mode=false) const;
  [[nodiscard]] stellar::core::IntegratedAdaptiveCampaignRuntime create_runtime(
      const NativePreparedNewCampaign &,
      stellar::core::AdaptiveResearchStrategicRuntime,
      std::span<const stellar::core::CatalogStar>) const;
private:
  const stellar::engine::LocalizationTable *locale_{nullptr};
};

} // namespace stellar::native_setup
