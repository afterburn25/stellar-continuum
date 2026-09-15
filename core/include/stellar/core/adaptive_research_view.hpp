#pragma once

#include <stellar/core/adaptive_research_eligibility.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct DirectedResearchCapacityView {
  std::string stage_id;
  std::optional<int> maximum_directed_programs;
  bool lab_capacity_only{};
  int active_program_count{};
  double free_effective_labs{};
};

struct AdaptiveResearchNodeView {
  std::string node_id;
  std::string display_name;
  std::string domain_id;
  std::string solution_family;
  ResearchMaturity state{};
  bool is_hypothesis{};
  std::vector<std::string> known_capabilities;
  std::vector<ResearchBlocker> blockers;
  std::optional<int> minimum_labs;
  std::optional<int> recommended_labs;
  std::optional<double> assigned_labs;
  std::optional<std::string> target_applicability_context_id;
};

struct AdaptiveResearchEdgeView {
  std::string from_visible_node_id;
  std::string to_visible_node_id;
  std::string relationship;
};

struct AdaptiveResearchProjectView {
  std::string node_id;
  ResearchMaturity stage{};
  double stage_progress{};
  double total_progress{};
  double assigned_effective_labs{};
  std::string readiness_band;
  bool paused{};
  std::optional<std::string> pause_reason;
  std::vector<ResearchBlocker> current_blockers;
  std::optional<std::string> target_applicability_context_id;
};

struct AdaptiveResearchPressureView {
  std::string pressure_id;
  double value{};
  std::vector<std::string> visible_hard_gate_target_node_ids;
};

struct AdaptiveResearchView {
  std::int64_t revision{};
  std::string civilization_id;
  DirectedResearchCapacityView directed_program_capacity;
  std::vector<AdaptiveResearchNodeView> visible_nodes;
  std::vector<AdaptiveResearchEdgeView> visible_edges;
  std::vector<AdaptiveResearchProjectView> active_projects;
  std::vector<AdaptiveResearchPressureView> recognized_pressures;
};

// The immutable catalog, evaluator and progress policy are borrowed and must
// outlive this builder without being moved. Each build returns a fully owned
// view. The input state is borrowed only for the duration of build().
class AdaptiveResearchViewBuilder final {
public:
  AdaptiveResearchViewBuilder(
      const AdaptiveResearchCatalog &catalog,
      const AdaptiveResearchEligibilityEvaluator &eligibility,
      const AdaptiveResearchProgressPolicy &progress_policy) noexcept;
  AdaptiveResearchViewBuilder(AdaptiveResearchCatalog &&,
                              const AdaptiveResearchEligibilityEvaluator &,
                              const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchViewBuilder(const AdaptiveResearchCatalog &,
                              AdaptiveResearchEligibilityEvaluator &&,
                              const AdaptiveResearchProgressPolicy &) = delete;
  AdaptiveResearchViewBuilder(const AdaptiveResearchCatalog &,
                              const AdaptiveResearchEligibilityEvaluator &,
                              AdaptiveResearchProgressPolicy &&) = delete;

  [[nodiscard]] AdaptiveResearchView build(
      const AdaptiveResearchCivilizationState &state,
      std::optional<std::string_view> default_target_applicability_context_id =
          std::nullopt) const;

private:
  const AdaptiveResearchCatalog *catalog_;
  const AdaptiveResearchEligibilityEvaluator *eligibility_;
  const AdaptiveResearchProgressPolicy *progress_policy_;
};

} // namespace stellar::core
