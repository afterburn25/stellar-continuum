#pragma once

#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_pressure.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

namespace detail {
class AdaptiveResearchOutcomeStateWriter;
class AdaptiveResearchOutcomeRuntimeTestAccess;
} // namespace detail

enum class ResearchUncertaintyProfile {
  established_extension,
  frontier_engineering,
  scientific_hypothesis,
  hazardous_foreign_or_anomalous,
};

enum class ResearchOutcomeKind {
  progress,
  setback,
  partial_success,
  hypothesis_supported,
  hypothesis_refined,
  hypothesis_disproven,
  anomalous_result,
  hazard_incident,
  side_discovery,
};

struct ResearchOutcomeCompetenceGain {
  double theoretical{};
  double experimental{};
  double engineering{};
};

struct ResearchOutcomeWeightEntry {
  ResearchOutcomeKind outcome{};
  double weight{};
};

struct ResearchOutcomeProfileWeights {
  ResearchUncertaintyProfile profile{};
  std::vector<ResearchOutcomeWeightEntry> weights;
};

struct ResearchOutcomeCompetenceGainEntry {
  ResearchOutcomeKind outcome{};
  ResearchOutcomeCompetenceGain gain;
};

struct ResearchSideDiscoveryCandidates {
  std::string node_id;
  std::vector<std::string> candidate_node_ids;
};

struct AdaptiveResearchOutcomeRuntimePolicy {
  double setback_stage_progress_loss_fraction{};
  double partial_success_stage_progress_credit_fraction{};
  double refined_hypothesis_stage_progress_preserved_fraction{};
  double high_readiness_risk_reduction_max_fraction{};
  int max_side_discovery_candidates{};
  int max_materialized_side_discoveries{};
  int minimum_shared_knowledge_fields{};
  int same_solution_family_depth_window{};
  int max_recent_outcome_records{};
  int max_per_node_recent_records{};
  std::vector<ResearchOutcomeProfileWeights> base_weights;
  std::vector<ResearchOutcomeCompetenceGainEntry> competence_gains;
  std::vector<std::string> frontier_engineering_node_ids;
  std::vector<std::string> hazardous_node_ids;
};

class AdaptiveResearchOutcomeCatalogError final : public std::runtime_error {
public:
  explicit AdaptiveResearchOutcomeCatalogError(std::string message);
};

enum class AdaptiveResearchOutcomeJsonErrorKind {
  reader,
  invalid_operation,
  format,
  missing_property,
};

// JSON parser wording is platform-specific; kind preserves the matching source
// exception boundary without exposing the implementation parser's exception.
class AdaptiveResearchOutcomeJsonError final : public std::runtime_error {
public:
  AdaptiveResearchOutcomeJsonError(AdaptiveResearchOutcomeJsonErrorKind kind,
                                   std::string message);
  [[nodiscard]] AdaptiveResearchOutcomeJsonErrorKind kind() const noexcept;

private:
  AdaptiveResearchOutcomeJsonErrorKind kind_;
};

class AdaptiveResearchOutcomeFileError final : public std::runtime_error {
public:
  explicit AdaptiveResearchOutcomeFileError(std::string message);
};

class AdaptiveResearchOutcomeHistoryRangeError final
    : public std::out_of_range {
public:
  explicit AdaptiveResearchOutcomeHistoryRangeError(std::string message);
};

class AdaptiveResearchOutcomeArgumentRangeError final
    : public std::out_of_range {
public:
  explicit AdaptiveResearchOutcomeArgumentRangeError(std::string message);
};

class AdaptiveResearchOutcomeIndexError final : public std::out_of_range {
public:
  explicit AdaptiveResearchOutcomeIndexError(std::string message);
};

// Owns policy definitions and load-time side-discovery indexes. The referenced
// research catalog must outlive this object and remain unmoved. References and
// spans returned by its getters expire when this catalog is destroyed or
// assigned. A moved-from catalog may only be destroyed or assigned.
class AdaptiveResearchOutcomeCatalog final {
public:
  ~AdaptiveResearchOutcomeCatalog();
  AdaptiveResearchOutcomeCatalog(AdaptiveResearchOutcomeCatalog &&) noexcept;
  AdaptiveResearchOutcomeCatalog &
  operator=(AdaptiveResearchOutcomeCatalog &&) noexcept;
  AdaptiveResearchOutcomeCatalog(const AdaptiveResearchOutcomeCatalog &) =
      delete;
  AdaptiveResearchOutcomeCatalog &
  operator=(const AdaptiveResearchOutcomeCatalog &) = delete;

  [[nodiscard]] const AdaptiveResearchCatalog &
  research_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchOutcomeRuntimePolicy &
  policy() const noexcept;
  [[nodiscard]] std::span<const ResearchSideDiscoveryCandidates>
  side_discovery_candidates() const noexcept;
  [[nodiscard]] std::span<const std::string>
  side_discovery_candidates_for(std::string_view node_id) const noexcept;
  [[nodiscard]] ResearchUncertaintyProfile
  get_profile(const AdaptiveResearchNodeDefinition &node) const noexcept;

  [[nodiscard]] static ResearchOutcomeKind parse_outcome(std::string_view id);
  [[nodiscard]] static std::string_view outcome_id(ResearchOutcomeKind value);

private:
  struct Storage;
  explicit AdaptiveResearchOutcomeCatalog(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchOutcomeCatalog
  load_adaptive_research_outcome_catalog(const std::filesystem::path &,
                                         const AdaptiveResearchCatalog &);
};

[[nodiscard]] AdaptiveResearchOutcomeCatalog
load_adaptive_research_outcome_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &research_catalog);
AdaptiveResearchOutcomeCatalog
load_adaptive_research_outcome_catalog(const std::filesystem::path &,
                                       AdaptiveResearchCatalog &&) = delete;

struct ResearchOutcomeNodeSummary {
  std::string node_id;
  int attempts{};
  int setbacks{};
  int partial_successes{};
  int refinements{};
  int disproofs{};
  int anomalies{};
  int hazards{};
  int side_discoveries{};
  std::optional<std::string> last_outcome_id;
  double last_outcome_year{};
};

struct ResearchOutcomeHistoryRecord {
  std::int64_t sequence{};
  std::string node_id;
  std::string checkpoint_id;
  int attempt_index{};
  ResearchOutcomeKind outcome{};
  std::optional<std::string> side_discovery_node_id;
  double year{};
  std::string explanation;
};

class AdaptiveResearchOutcomeState final {
public:
  AdaptiveResearchOutcomeState();
  ~AdaptiveResearchOutcomeState();
  AdaptiveResearchOutcomeState(AdaptiveResearchOutcomeState &&) noexcept;
  AdaptiveResearchOutcomeState &
  operator=(AdaptiveResearchOutcomeState &&) noexcept;
  AdaptiveResearchOutcomeState(const AdaptiveResearchOutcomeState &);
  AdaptiveResearchOutcomeState &operator=(const AdaptiveResearchOutcomeState &);

  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] std::int64_t next_sequence() const noexcept;
  [[nodiscard]] std::span<const ResearchOutcomeNodeSummary>
  summaries() const noexcept;
  [[nodiscard]] std::span<const ResearchOutcomeHistoryRecord>
  recent_records() const noexcept;
  [[nodiscard]] ResearchOutcomeNodeSummary
  get_summary(std::string_view node_id) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class AdaptiveResearchOutcomeRuntime;
  friend class detail::AdaptiveResearchOutcomeStateWriter;
};

enum class AdaptiveResearchOutcomeEventType {
  outcome_resolved,
  project_setback,
  partial_success,
  hypothesis_refined,
  hypothesis_disproven,
  anomalous_result,
  hazard_reported,
  side_discovery_materialized,
};

struct AdaptiveResearchOutcomeEvent {
  AdaptiveResearchOutcomeEventType type{};
  std::string civilization_id;
  std::string node_id;
  std::optional<std::string> subject_id;
  std::string message;
};

struct PlannedResearchOutcome {
  std::string node_id;
  std::string checkpoint_id;
  int attempt_index{};
  ResearchUncertaintyProfile profile{};
  ResearchOutcomeKind outcome{};
  double deterministic_roll{};
  std::optional<std::string> planned_side_discovery_node_id;
  std::string explanation;
};

struct ResearchOutcomeApplicationResult {
  bool accepted{};
  std::optional<PlannedResearchOutcome> resolution;
  std::vector<AdaptiveResearchRuntimeEvent> research_events;
  std::vector<AdaptiveResearchOutcomeEvent> outcome_events;
  std::string message;

  [[nodiscard]] static ResearchOutcomeApplicationResult
  rejected(std::string message);
};

// Owns only weak-key support state. Authority, outcome catalog, and optional
// pressure runtime must outlive this object and remain unmoved. A move
// transfers the stable support-state storage; the moved-from runtime may only
// be destroyed or assigned. References returned by state() expire when their
// civilization state dies or when this runtime is destroyed or assigned.
class AdaptiveResearchOutcomeRuntime final {
public:
  AdaptiveResearchOutcomeRuntime(const AdaptiveResearchAuthority &authority,
                                 const AdaptiveResearchOutcomeCatalog &catalog);
  AdaptiveResearchOutcomeRuntime(
      const AdaptiveResearchAuthority &authority,
      const AdaptiveResearchOutcomeCatalog &catalog,
      const AdaptiveResearchPressureRuntime &pressure);
  AdaptiveResearchOutcomeRuntime(AdaptiveResearchAuthority &&,
                                 const AdaptiveResearchOutcomeCatalog &) =
      delete;
  AdaptiveResearchOutcomeRuntime(const AdaptiveResearchAuthority &,
                                 AdaptiveResearchOutcomeCatalog &&) = delete;
  AdaptiveResearchOutcomeRuntime(const AdaptiveResearchAuthority &,
                                 const AdaptiveResearchOutcomeCatalog &,
                                 AdaptiveResearchPressureRuntime &&) = delete;
  AdaptiveResearchOutcomeRuntime(
      AdaptiveResearchAuthority &&, const AdaptiveResearchOutcomeCatalog &,
      const AdaptiveResearchPressureRuntime &) = delete;
  AdaptiveResearchOutcomeRuntime(
      const AdaptiveResearchAuthority &, AdaptiveResearchOutcomeCatalog &&,
      const AdaptiveResearchPressureRuntime &) = delete;
  ~AdaptiveResearchOutcomeRuntime();
  AdaptiveResearchOutcomeRuntime(AdaptiveResearchOutcomeRuntime &&) noexcept;
  AdaptiveResearchOutcomeRuntime &
  operator=(AdaptiveResearchOutcomeRuntime &&) noexcept;
  AdaptiveResearchOutcomeRuntime(const AdaptiveResearchOutcomeRuntime &) =
      delete;
  AdaptiveResearchOutcomeRuntime &
  operator=(const AdaptiveResearchOutcomeRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchOutcomeCatalog &catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchOutcomeState &
  state(const AdaptiveResearchCivilizationState &civilization) const;
  [[nodiscard]] PlannedResearchOutcome
  plan_outcome(const AdaptiveResearchCivilizationState &civilization,
               std::string_view node_id, std::string_view campaign_seed,
               std::string_view checkpoint_id,
               std::optional<std::string_view> target_applicability_context_id =
                   std::nullopt) const;
  [[nodiscard]] ResearchOutcomeApplicationResult resolve_pending_hypothesis(
      AdaptiveResearchCivilizationState &civilization, std::string_view node_id,
      std::string_view campaign_seed, double current_year,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] ResearchOutcomeApplicationResult resolve_active_checkpoint(
      AdaptiveResearchCivilizationState &civilization, std::string_view node_id,
      std::string_view campaign_seed, std::string_view checkpoint_id,
      double current_year) const;

private:
  [[nodiscard]] ResearchOutcomeApplicationResult
  apply_outcome(AdaptiveResearchCivilizationState &civilization,
                ResearchProjectRuntimeState project,
                PlannedResearchOutcome resolution, double current_year) const;
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::AdaptiveResearchOutcomeRuntimeTestAccess;
};

namespace detail {

class AdaptiveResearchOutcomeStateWriter final {
public:
  static void record(AdaptiveResearchOutcomeState &state, std::string node_id,
                     std::string checkpoint_id, int attempt_index,
                     ResearchOutcomeKind outcome,
                     std::optional<std::string> side_discovery_node_id,
                     double year, std::string explanation, int max_recent,
                     int max_per_node_recent);
  static void restore_summary(AdaptiveResearchOutcomeState &state,
                              ResearchOutcomeNodeSummary summary);
  static void restore_record(AdaptiveResearchOutcomeState &state,
                             ResearchOutcomeHistoryRecord record);
};

class AdaptiveResearchOutcomeRuntimeTestAccess final {
public:
  static void maintain(AdaptiveResearchOutcomeRuntime &runtime,
                       std::size_t limit = 8);
  [[nodiscard]] static std::size_t
  state_count(const AdaptiveResearchOutcomeRuntime &runtime) noexcept;
  [[nodiscard]] static ResearchOutcomeApplicationResult
  apply(const AdaptiveResearchOutcomeRuntime &runtime,
        AdaptiveResearchCivilizationState &civilization,
        ResearchProjectRuntimeState project, PlannedResearchOutcome resolution,
        double current_year);
};

} // namespace detail

} // namespace stellar::core
