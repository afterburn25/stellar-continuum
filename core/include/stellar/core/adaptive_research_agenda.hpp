#pragma once

#include <stellar/core/adaptive_research_authority.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

namespace detail {
class AdaptiveResearchAgendaStateWriter;
class AdaptiveResearchAgendaRuntimeTestAccess;
}

struct ResearchAgendaPriorityDefinition {
  std::string id;
  int rank{};
  double score{};
};

struct ResearchScientificCultureAxisDefinition {
  std::string id;
  std::string name;
};

struct ResearchAgendaRuntimePolicy {
  std::string default_priority_id;
  double default_basic_vs_applied_orientation{};
  double default_competence_preservation{};
  double default_portfolio_diversity{};
  double default_foreign_science_engagement{};
  double default_culture_axis{};
  double blocked_project_score_multiplier{};
  double already_covered_solution_value{};
  double novel_solution_value{};
  double time_to_effect_half_value_years{};
  double lab_cost_half_value_fraction{};
  double minimum_adequacy_confidence{};
  double low_relevant_pressure_maximum{};
  double high_adequacy_minimum{};
  double complacency_deprioritize_threshold{};
  double important_challenge_threshold{};
  double strategic_challenge_threshold{};
  double critical_challenge_threshold{};
};

class AdaptiveResearchAgendaCatalogError final : public std::runtime_error {
public:
  explicit AdaptiveResearchAgendaCatalogError(std::string message);
};

class AdaptiveResearchAgendaCatalog final {
public:
  // A moved-from catalog may only be destroyed or assigned a new value.
  ~AdaptiveResearchAgendaCatalog();
  AdaptiveResearchAgendaCatalog(AdaptiveResearchAgendaCatalog &&) noexcept;
  AdaptiveResearchAgendaCatalog &
  operator=(AdaptiveResearchAgendaCatalog &&) noexcept;
  AdaptiveResearchAgendaCatalog(const AdaptiveResearchAgendaCatalog &) = delete;
  AdaptiveResearchAgendaCatalog &
  operator=(const AdaptiveResearchAgendaCatalog &) = delete;

  [[nodiscard]] std::span<const ResearchAgendaPriorityDefinition>
  priorities() const noexcept;
  [[nodiscard]] std::span<const ResearchScientificCultureAxisDefinition>
  culture_axes() const noexcept;
  [[nodiscard]] std::span<const std::string>
  utility_component_ids() const noexcept;
  [[nodiscard]] int shortlist_bound() const noexcept;
  [[nodiscard]] const ResearchAgendaRuntimePolicy &runtime_policy() const noexcept;
  [[nodiscard]] const ResearchAgendaPriorityDefinition &
  get_priority(std::string_view id) const;

private:
  struct Storage;
  explicit AdaptiveResearchAgendaCatalog(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchAgendaCatalog load_adaptive_research_agenda_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &,
      const AdaptiveResearchExpertiseCatalog &);
};

[[nodiscard]] AdaptiveResearchAgendaCatalog
load_adaptive_research_agenda_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog);

struct ResearchAgendaOrientationState {
  double basic_vs_applied_orientation{};
  double competence_preservation_policy{};
  double portfolio_diversity_policy{};
  double foreign_science_engagement{};
  bool operator==(const ResearchAgendaOrientationState &) const = default;
};

struct ResearchAgendaPriorityEntry {
  std::string key;
  std::string priority_id;
};

struct ResearchScientificCultureAxisState {
  std::string axis_id;
  double value{};
};

class AdaptiveResearchAgendaState final {
public:
  // A moved-from state may only be destroyed or assigned a new value.
  ~AdaptiveResearchAgendaState();
  AdaptiveResearchAgendaState(AdaptiveResearchAgendaState &&) noexcept;
  AdaptiveResearchAgendaState &operator=(AdaptiveResearchAgendaState &&) noexcept;
  AdaptiveResearchAgendaState(const AdaptiveResearchAgendaState &) = delete;
  AdaptiveResearchAgendaState &operator=(const AdaptiveResearchAgendaState &) = delete;

  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] const ResearchAgendaOrientationState &orientations() const noexcept;
  [[nodiscard]] double last_major_review_year() const noexcept;
  [[nodiscard]] const std::string &policy_provenance() const noexcept;
  [[nodiscard]] std::span<const ResearchAgendaPriorityEntry>
  domain_priorities() const noexcept;
  [[nodiscard]] std::span<const ResearchAgendaPriorityEntry>
  field_priorities() const noexcept;
  [[nodiscard]] std::span<const ResearchAgendaPriorityEntry>
  problem_priorities() const noexcept;
  [[nodiscard]] std::span<const ResearchAgendaPriorityEntry>
  capability_priorities() const noexcept;
  [[nodiscard]] std::span<const ResearchScientificCultureAxisState>
  culture_axes() const noexcept;
  [[nodiscard]] std::string_view
  get_domain_priority(std::string_view domain_id,
                      std::string_view default_priority_id) const noexcept;
  [[nodiscard]] std::string_view
  get_field_priority(std::string_view field_id,
                     std::string_view default_priority_id) const noexcept;
  [[nodiscard]] std::string_view
  get_problem_priority(std::string_view pressure_id,
                       std::string_view default_priority_id) const noexcept;
  [[nodiscard]] std::string_view
  get_capability_priority(std::string_view capability_id,
                          std::string_view default_priority_id) const noexcept;
  [[nodiscard]] double get_culture_axis(std::string_view axis_id) const noexcept;

private:
  struct Storage;
  explicit AdaptiveResearchAgendaState(const AdaptiveResearchAgendaCatalog &);
  std::unique_ptr<Storage> storage_;
  friend class AdaptiveResearchAgendaRuntime;
  friend class detail::AdaptiveResearchAgendaStateWriter;
};

struct ResearchPerceivedAdequacyAssessment {
  std::string domain_id;
  double adequacy{};
  double credible_challenge{};
  double confidence{};
};

struct ResearchAgendaReviewRecommendation {
  std::string domain_id;
  std::string recommended_priority_id;
  double relevant_pressure{};
  double complacency_index{};
  double challenge_index{};
  std::string explanation;
};

struct ResearchProjectUtilityComponent {
  std::string id;
  double score{};
  std::string explanation;
};

struct ResearchVisibleProjectCandidate {
  std::string node_id;
  double utility_score{};
  bool can_start{};
  double requested_effective_labs{};
  double estimated_years_to_mature{};
  std::vector<ResearchProjectUtilityComponent> components;
  std::vector<ResearchBlocker> blockers;
  std::string explanation;
};

// Owns the state table and candidate-cache table. It borrows one stable
// authority and one stable immutable agenda catalog. Both must outlive the
// runtime and remain unmoved.
// Runtime moves preserve owned state/cache addresses. Returned candidates own
// their values. State references borrow the runtime and matching civilization
// identity and are invalidated when either is destroyed or reassigned.
// A moved-from runtime may only be destroyed or assigned a new value.
class AdaptiveResearchAgendaRuntime final {
public:
  AdaptiveResearchAgendaRuntime(const AdaptiveResearchAuthority &authority,
                                const AdaptiveResearchAgendaCatalog &catalog);
  AdaptiveResearchAgendaRuntime(AdaptiveResearchAuthority &&,
                                const AdaptiveResearchAgendaCatalog &) = delete;
  AdaptiveResearchAgendaRuntime(const AdaptiveResearchAuthority &,
                                AdaptiveResearchAgendaCatalog &&) = delete;
  ~AdaptiveResearchAgendaRuntime();
  AdaptiveResearchAgendaRuntime(AdaptiveResearchAgendaRuntime &&) noexcept;
  AdaptiveResearchAgendaRuntime &
  operator=(AdaptiveResearchAgendaRuntime &&) noexcept;
  AdaptiveResearchAgendaRuntime(const AdaptiveResearchAgendaRuntime &) = delete;
  AdaptiveResearchAgendaRuntime &
  operator=(const AdaptiveResearchAgendaRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchAgendaCatalog &catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchAgendaState &
  state(const AdaptiveResearchCivilizationState &civilization) const;

  void set_domain_priority(const AdaptiveResearchCivilizationState &civilization,
                           std::string_view domain_id,
                           std::string_view priority_id) const;
  void set_field_priority(const AdaptiveResearchCivilizationState &civilization,
                          std::string_view field_id,
                          std::string_view priority_id) const;
  void set_problem_priority(const AdaptiveResearchCivilizationState &civilization,
                            std::string_view pressure_id,
                            std::string_view priority_id) const;
  void set_capability_priority(
      const AdaptiveResearchCivilizationState &civilization,
      std::string_view capability_id, std::string_view priority_id) const;
  void set_scientific_culture_axis(
      const AdaptiveResearchCivilizationState &civilization,
      std::string_view axis_id, double value) const;
  void set_orientations(const AdaptiveResearchCivilizationState &civilization,
                        ResearchAgendaOrientationState orientations) const;
  [[nodiscard]] ResearchAgendaReviewRecommendation evaluate_perceived_adequacy(
      const AdaptiveResearchCivilizationState &civilization,
      const ResearchPerceivedAdequacyAssessment &assessment) const;
  void apply_recommendation(
      const AdaptiveResearchCivilizationState &civilization,
      const ResearchAgendaReviewRecommendation &recommendation,
      double current_year, std::string_view provenance) const;
  [[nodiscard]] std::vector<ResearchVisibleProjectCandidate>
  build_visible_shortlist(
      const AdaptiveResearchCivilizationState &civilization) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::AdaptiveResearchAgendaRuntimeTestAccess;
};

namespace detail {

class AdaptiveResearchAgendaStateWriter final {
public:
  static bool set_domain_priority(AdaptiveResearchAgendaState &state,
                                  std::string key, std::string priority_id,
                                  std::string_view default_priority_id);
  static bool set_field_priority(AdaptiveResearchAgendaState &state,
                                 std::string key, std::string priority_id,
                                 std::string_view default_priority_id);
  static bool set_problem_priority(AdaptiveResearchAgendaState &state,
                                   std::string key, std::string priority_id,
                                   std::string_view default_priority_id);
  static bool set_capability_priority(AdaptiveResearchAgendaState &state,
                                      std::string key, std::string priority_id,
                                      std::string_view default_priority_id);
  static bool set_culture_axis(AdaptiveResearchAgendaState &state,
                               std::string axis_id, double value);
  static void set_orientations(AdaptiveResearchAgendaState &state,
                               ResearchAgendaOrientationState value);
  static void mark_reviewed(AdaptiveResearchAgendaState &state,
                            double current_year, std::string provenance);
};

class AdaptiveResearchAgendaRuntimeTestAccess final {
public:
  [[nodiscard]] static std::uint64_t shortlist_rebuild_count(
      const AdaptiveResearchAgendaRuntime &runtime,
      const AdaptiveResearchCivilizationState &civilization) noexcept;
  static void maintain(AdaptiveResearchAgendaRuntime &runtime,
                       std::size_t limit = 8);
  [[nodiscard]] static std::size_t
  state_count(const AdaptiveResearchAgendaRuntime &runtime) noexcept;
  [[nodiscard]] static std::size_t
  cache_count(const AdaptiveResearchAgendaRuntime &runtime) noexcept;
};

} // namespace detail

} // namespace stellar::core
