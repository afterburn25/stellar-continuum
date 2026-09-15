#pragma once

#include <stellar/core/adaptive_research_applicability_catalog.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_state.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class ResearchBlockerCode {
  unknown_node,
  non_public_research,
  missing_prerequisite,
  missing_alternative_prerequisite,
  missing_applicability_context,
  missing_applicability_trait,
  missing_evidence,
  missing_pressure,
  missing_alternative_pressure,
  missing_capability,
  missing_alternative_capability,
  node_not_investigable,
  already_mature,
  already_active,
  directed_program_capacity,
  insufficient_free_labs,
  below_minimum_assigned_labs,
  missing_facility_capability,
  missing_alternative_facility_capability,
};

struct ResearchBlocker {
  ResearchBlockerCode code{};
  std::optional<std::string> subject_id;
  std::optional<double> required_value;
  std::optional<double> actual_value;
  std::string message;
};

struct ResearchEligibilityResult {
  bool allowed{};
  std::vector<ResearchBlocker> blockers;
};

// The three immutable catalogs are borrowed and must outlive this evaluator.
// Results own every blocker string and remain independent of those catalogs.
class AdaptiveResearchEligibilityEvaluator final {
public:
  AdaptiveResearchEligibilityEvaluator(
      const AdaptiveResearchCatalog &catalog,
      const AdaptiveResearchApplicabilityCatalog &applicability,
      const AdaptiveResearchFacilityCatalog &facilities) noexcept;
  AdaptiveResearchEligibilityEvaluator(
      AdaptiveResearchCatalog &&, const AdaptiveResearchApplicabilityCatalog &,
      const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchEligibilityEvaluator(
      const AdaptiveResearchCatalog &, AdaptiveResearchApplicabilityCatalog &&,
      const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchEligibilityEvaluator(
      const AdaptiveResearchCatalog &,
      const AdaptiveResearchApplicabilityCatalog &,
      AdaptiveResearchFacilityCatalog &&) = delete;

  [[nodiscard]] ResearchEligibilityResult evaluate_scientific_eligibility(
      const AdaptiveResearchCivilizationState &state, std::string_view node_id,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;

  [[nodiscard]] ResearchEligibilityResult evaluate_project_start(
      const AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double requested_assigned_labs,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;

  [[nodiscard]] ResearchEligibilityResult evaluate_stage_facility_eligibility(
      const AdaptiveResearchCivilizationState &state, std::string_view node_id,
      ResearchMaturity stage) const;

private:
  const AdaptiveResearchCatalog *catalog_;
  const AdaptiveResearchApplicabilityCatalog *applicability_;
  const AdaptiveResearchFacilityCatalog *facilities_;
};

} // namespace stellar::core
