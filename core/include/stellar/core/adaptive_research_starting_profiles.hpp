#pragma once

#include <stellar/core/adaptive_research_runtime.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace stellar::core {

struct StartingFieldCompetenceSeed {
  double theoretical{};
  double experimental{};
  double engineering{};
};

struct StartingResearchInstitutionSeed {
  std::string institution_archetype_id;
  int count{};
};

struct StartingTacitAssetSeed {
  std::string asset_type_id;
  std::string provenance;
  std::optional<std::string> scope_ref;
};

struct AdaptiveResearchDeferredStartingState {
  std::vector<std::pair<std::string, StartingFieldCompetenceSeed>>
      field_competence;
  std::vector<StartingResearchInstitutionSeed> research_institutions;
  std::vector<StartingTacitAssetSeed> tacit_assets;
  std::vector<std::string> selected_fragment_ids;
  std::string reference_profile_id;
  std::optional<std::string> historical_notes;
};

struct AdaptiveResearchStartingCompositionResult {
  AdaptiveResearchCivilizationState state;
  AdaptiveResearchDeferredStartingState deferred;
  std::vector<AdaptiveResearchRuntimeEvent> initial_horizon_events;
};

// The runtime is borrowed and must outlive this composer without being moved.
// Returned ID spans remain valid until this composer is moved or destroyed.
class AdaptiveResearchStartingProfileComposer final {
public:
  AdaptiveResearchStartingProfileComposer(
      const AdaptiveResearchRuntime &runtime,
      const std::filesystem::path &root_path);
  AdaptiveResearchStartingProfileComposer(
      AdaptiveResearchRuntime &&,
      const std::filesystem::path &) = delete;
  ~AdaptiveResearchStartingProfileComposer();
  AdaptiveResearchStartingProfileComposer(
      AdaptiveResearchStartingProfileComposer &&) noexcept;
  AdaptiveResearchStartingProfileComposer &operator=(
      AdaptiveResearchStartingProfileComposer &&) noexcept;
  AdaptiveResearchStartingProfileComposer(
      const AdaptiveResearchStartingProfileComposer &) = delete;
  AdaptiveResearchStartingProfileComposer &operator=(
      const AdaptiveResearchStartingProfileComposer &) = delete;

  [[nodiscard]] std::span<const std::string>
  reference_profile_ids() const noexcept;
  [[nodiscard]] std::span<const std::string> fragment_ids() const noexcept;

  [[nodiscard]] AdaptiveResearchStartingCompositionResult
  compose_reference_profile(std::string civilization_id,
                            std::string reference_profile_id,
                            std::string primary_applicability_context_id) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
