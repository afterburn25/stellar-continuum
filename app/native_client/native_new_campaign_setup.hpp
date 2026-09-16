#pragma once

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/species_environment.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace stellar::native_setup {

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
};

struct NativeNewCampaignSetupInput {
  std::string seed_text;
  int system_count{500};
  std::string player_species_id{"terran_baseline"};
  std::string created_at_utc;
  int pre_warp_civilization_count{6};
  int ancient_civilization_count{1};
};

class NativePreparedNewCampaign final {
public:
  [[nodiscard]] std::int64_t seed() const noexcept { return seed_; }
  [[nodiscard]] const stellar::core::PersistableFreshCampaignOptions &
  options() const noexcept {
    return options_;
  }

private:
  friend class NativeNewCampaignSetupController;
  NativePreparedNewCampaign(
      std::int64_t seed,
      stellar::core::PersistableFreshCampaignOptions options)
      : seed_(seed), options_(std::move(options)) {}
  std::int64_t seed_{};
  stellar::core::PersistableFreshCampaignOptions options_;
};

struct NativeNewCampaignSetupAssessment {
  bool accepted{};
  std::string message;
  std::optional<NativePreparedNewCampaign> prepared;
};

class NativeNewCampaignSetupController final {
public:
  [[nodiscard]] NativeNewCampaignSetupView build() const;
  [[nodiscard]] NativeNewCampaignSetupAssessment
  prepare(const NativeNewCampaignSetupInput &) const;
  [[nodiscard]] stellar::core::IntegratedAdaptiveCampaignRuntime create_runtime(
      const NativePreparedNewCampaign &,
      stellar::core::AdaptiveResearchStrategicRuntime,
      std::span<const stellar::core::CatalogStar>) const;
};

} // namespace stellar::native_setup
