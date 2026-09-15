#pragma once

#include <stellar/core/adaptive_research_expertise_service.hpp>
#include <stellar/core/adaptive_research_runtime.hpp>
#include <stellar/core/adaptive_research_starting_profiles.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

// Owns the kernel, expertise content, readiness calculator, expertise service,
// and starting-profile composer in stable private storage. Getter results
// borrow that storage allocation. Move construction transfers the allocation,
// so an earlier getter remains valid while the moved-to authority owns it.
// Destruction or move-assignment of the current owner invalidates its getters.
// A moved-from authority may only be destroyed or assigned a valid authority.
class AdaptiveResearchAuthority final {
public:
  ~AdaptiveResearchAuthority();
  AdaptiveResearchAuthority(AdaptiveResearchAuthority &&) noexcept;
  AdaptiveResearchAuthority &operator=(AdaptiveResearchAuthority &&) noexcept;
  AdaptiveResearchAuthority(const AdaptiveResearchAuthority &) = delete;
  AdaptiveResearchAuthority &
  operator=(const AdaptiveResearchAuthority &) = delete;

  [[nodiscard]] const AdaptiveResearchRuntime &kernel() const noexcept;
  [[nodiscard]] const AdaptiveResearchExpertiseCatalog &
  expertise_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchReadinessCalculator &
  readiness() const noexcept;
  [[nodiscard]] const AdaptiveResearchExpertiseService &
  expertise_service() const noexcept;
  [[nodiscard]] const AdaptiveResearchStartingProfileComposer &
  starting_profiles() const noexcept;
  [[nodiscard]] const AdaptiveResearchCatalog &catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchApplicabilityCatalog &
  applicability() const noexcept;
  [[nodiscard]] const AdaptiveResearchFacilityCatalog &
  facilities() const noexcept;
  [[nodiscard]] const AdaptiveResearchProgressPolicy &
  progress_policy() const noexcept;

  [[nodiscard]] AdaptiveResearchCivilizationState
  create_civilization_state(std::string civilization_id) const;
  [[nodiscard]] AdaptiveResearchStartingCompositionResult
  compose_reference_profile(std::string civilization_id,
                            std::string reference_profile_id,
                            std::string primary_applicability_context_id,
                            double activity_year = 2050.0) const;

  [[nodiscard]] ResearchReadinessBreakdown get_project_readiness(
      const AdaptiveResearchCivilizationState &state, std::string_view node_id,
      ResearchMaturity stage, double assigned_effective_labs,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] AdaptiveResearchCommandResult start_directed_research(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double requested_assigned_labs,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] AdaptiveResearchCommandResult
  pause_directed_research(AdaptiveResearchCivilizationState &state,
                          std::string_view node_id) const;
  [[nodiscard]] AdaptiveResearchCommandResult
  resume_directed_research(AdaptiveResearchCivilizationState &state,
                           std::string_view node_id,
                           double requested_assigned_labs) const;
  [[nodiscard]] AdaptiveResearchCommandResult
  reallocate_research_labs(AdaptiveResearchCivilizationState &state,
                           std::string_view node_id,
                           double requested_assigned_labs) const;
  [[nodiscard]] AdaptiveResearchCommandResult
  resolve_hypothesis(AdaptiveResearchCivilizationState &state,
                     std::string_view node_id, bool supported) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  advance_projects(AdaptiveResearchCivilizationState &state,
                   double elapsed_years, double current_year) const;

  void set_research_institution(
      AdaptiveResearchCivilizationState &state,
      std::string_view institution_instance_id,
      std::string_view institution_archetype_id, int total_count,
      int active_count,
      std::optional<std::string_view> context_id = std::nullopt) const;
  void set_tacit_asset(
      AdaptiveResearchCivilizationState &state, std::string_view asset_id,
      std::string_view asset_type_id, ResearchTacitScopeKind scope_kind,
      std::string_view scope_ref,
      ResearchTacitAssimilationStage assimilation_stage, double depth,
      double availability, double translation_context_quality,
      double training_continuity, std::string_view provenance,
      std::optional<std::string_view> context_id = std::nullopt) const;
  void apply_competence_atrophy(AdaptiveResearchCivilizationState &state,
                                double current_year,
                                double elapsed_years) const;

  // AdaptiveResearchAuthorityInputs forwarding surface.
  [[nodiscard]] AdaptiveResearchView
  build_view(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  set_pressure(AdaptiveResearchCivilizationState &state,
               std::string_view pressure_id, double value,
               std::optional<std::string_view> target_applicability_context_id =
                   std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  add_evidence(AdaptiveResearchCivilizationState &state,
               std::string_view evidence_instance_id,
               std::string_view evidence_type_id, std::string_view provenance,
               double quality, double confidence,
               std::optional<std::string_view> context_id = std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  add_civilization_trait(AdaptiveResearchCivilizationState &state,
                         std::string_view trait_id) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  set_applicability_context_traits(
      AdaptiveResearchCivilizationState &state, std::string_view context_id,
      std::span<const std::string> trait_ids) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> add_capability(
      AdaptiveResearchCivilizationState &state, std::string_view capability_id,
      std::optional<std::string_view> context_id = std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  review_basic_science_candidates(
      AdaptiveResearchCivilizationState &state,
      std::span<const std::string> bounded_candidate_node_ids,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;

private:
  struct Storage;
  explicit AdaptiveResearchAuthority(std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchAuthority
  load_adaptive_research_authority(const std::filesystem::path &);
};

// Loads in source order: kernel content/helpers, expertise catalog, readiness,
// expertise service, then the starting-profile composer.
[[nodiscard]] AdaptiveResearchAuthority
load_adaptive_research_authority(const std::filesystem::path &root_path);

} // namespace stellar::core
