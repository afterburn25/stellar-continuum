#pragma once

#include <stellar/core/adaptive_research_catalog.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

class AdaptiveResearchCivilizationState;
class AdaptiveResearchExpertiseCatalog;
class AdaptiveResearchExpertiseState;
class AdaptiveResearchFacilityCatalog;
class AdaptiveResearchProgressPolicy;

struct ResearchReadinessBreakdown {
  std::string node_id;
  ResearchMaturity stage{};
  std::optional<std::string> target_applicability_context_id;
  double field_competence_score{};
  double facility_readiness_score{};
  std::optional<double> evidence_readiness_score;
  std::optional<double> tacit_expertise_score;
  double overall_readiness_score{};
  double rp_efficiency{};
  std::vector<std::string> explanations;
};

class AdaptiveResearchReadinessCalculator final {
public:
  // All four catalogs must outlive this calculator and remain at the same
  // addresses. Moving or move-assigning any borrowed catalog invalidates it.
  AdaptiveResearchReadinessCalculator(
      const AdaptiveResearchCatalog &catalog,
      const AdaptiveResearchFacilityCatalog &facilities,
      const AdaptiveResearchExpertiseCatalog &expertise_catalog,
      const AdaptiveResearchProgressPolicy &progress_policy) noexcept;

  AdaptiveResearchReadinessCalculator(
      AdaptiveResearchCatalog &&, const AdaptiveResearchFacilityCatalog &,
      const AdaptiveResearchExpertiseCatalog &,
      const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchReadinessCalculator(
      const AdaptiveResearchCatalog &, AdaptiveResearchFacilityCatalog &&,
      const AdaptiveResearchExpertiseCatalog &,
      const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchReadinessCalculator(
      const AdaptiveResearchCatalog &, const AdaptiveResearchFacilityCatalog &,
      AdaptiveResearchExpertiseCatalog &&,
      const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchReadinessCalculator(
      const AdaptiveResearchCatalog &, const AdaptiveResearchFacilityCatalog &,
      const AdaptiveResearchExpertiseCatalog &,
      AdaptiveResearchProgressPolicy &&) = delete;

  [[nodiscard]] ResearchReadinessBreakdown
  calculate(const AdaptiveResearchCivilizationState &state,
            const AdaptiveResearchExpertiseState &expertise,
            std::string_view node_id, ResearchMaturity stage,
            double assigned_effective_labs,
            std::optional<std::string_view> target_applicability_context_id =
                std::nullopt) const;

private:
  const AdaptiveResearchCatalog *catalog_;
  const AdaptiveResearchFacilityCatalog *facilities_;
  const AdaptiveResearchExpertiseCatalog *expertise_catalog_;
  const AdaptiveResearchProgressPolicy *progress_policy_;
};

} // namespace stellar::core
