#pragma once

#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_readiness.hpp>

#include <optional>
#include <string_view>

namespace stellar::core {

class AdaptiveResearchCivilizationState;

class AdaptiveResearchExpertiseService final {
public:
  // The two catalogs and calculator must outlive this service and remain at
  // the same addresses. Moving or move-assigning one invalidates the service.
  AdaptiveResearchExpertiseService(
      const AdaptiveResearchCatalog &catalog,
      const AdaptiveResearchExpertiseCatalog &expertise_catalog,
      const AdaptiveResearchReadinessCalculator &readiness) noexcept;

  AdaptiveResearchExpertiseService(
      AdaptiveResearchCatalog &&, const AdaptiveResearchExpertiseCatalog &,
      const AdaptiveResearchReadinessCalculator &) = delete;
  AdaptiveResearchExpertiseService(
      const AdaptiveResearchCatalog &, AdaptiveResearchExpertiseCatalog &&,
      const AdaptiveResearchReadinessCalculator &) = delete;
  AdaptiveResearchExpertiseService(
      const AdaptiveResearchCatalog &, const AdaptiveResearchExpertiseCatalog &,
      AdaptiveResearchReadinessCalculator &&) = delete;

  [[nodiscard]] ResearchReadinessBreakdown calculate_project_readiness(
      const AdaptiveResearchCivilizationState &state, std::string_view node_id,
      ResearchMaturity stage, double assigned_effective_labs,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;

  void seed_field_competence(AdaptiveResearchCivilizationState &state,
                             std::string_view field_id,
                             ResearchCompetenceVector value,
                             double activity_year = 0.0) const;
  void set_institution(
      AdaptiveResearchCivilizationState &state,
      std::string_view institution_instance_id,
      std::string_view institution_archetype_id, int total_count,
      int active_count,
      std::optional<std::string_view> context_id = std::nullopt) const;
  [[nodiscard]] bool
  remove_institution(AdaptiveResearchCivilizationState &state,
                     std::string_view institution_instance_id) const;
  void set_tacit_asset(
      AdaptiveResearchCivilizationState &state, std::string_view asset_id,
      std::string_view asset_type_id, ResearchTacitScopeKind scope_kind,
      std::string_view scope_ref,
      ResearchTacitAssimilationStage assimilation_stage, double depth,
      double availability, double translation_context_quality,
      double training_continuity, std::string_view provenance,
      std::optional<std::string_view> context_id = std::nullopt) const;
  [[nodiscard]] bool
  remove_tacit_asset(AdaptiveResearchCivilizationState &state,
                     std::string_view asset_id) const;
  void apply_completed_stage_practice(AdaptiveResearchCivilizationState &state,
                                      std::string_view node_id,
                                      ResearchMaturity completed_stage,
                                      double activity_year) const;
  void apply_competence_atrophy(AdaptiveResearchCivilizationState &state,
                                double current_year,
                                double elapsed_years) const;

private:
  const AdaptiveResearchCatalog *catalog_;
  const AdaptiveResearchExpertiseCatalog *expertise_catalog_;
  const AdaptiveResearchReadinessCalculator *readiness_;
};

} // namespace stellar::core
