#pragma once

#include <stellar/core/adaptive_research_authority.hpp>

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

class AdaptiveResearchForeignCatalogDataError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class AdaptiveResearchForeignJsonOperationError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class AdaptiveResearchForeignMissingRecord : public std::out_of_range {
public:
  using std::out_of_range::out_of_range;
};

class AdaptiveResearchForeignArgumentOutOfRange : public std::out_of_range {
public:
  using std::out_of_range::out_of_range;
};

namespace detail {
class AdaptiveResearchForeignTechnologyStateWriter;
class AdaptiveResearchForeignTechnologySupportAccess;
}

enum class ForeignUnderstandingState {
  unknown,
  observed,
  characterized,
  principle_understood,
  engineering_understood,
};

enum class ForeignOperabilityState {
  unknown,
  unusable,
  origin_only,
  supported_operation,
  adapted_operation,
  native_operation,
};

enum class ForeignReproductionState {
  none,
  component_replication,
  subsystem_replication,
  foreign_process_replication,
  native_process_replication,
};

enum class ForeignAdaptationState {
  none,
  conceptual_inspiration,
  interface_adaptation,
  native_derivative,
  hybrid_lineage,
};

struct ForeignTechnologyPackageComponentDefinition {
  std::string id;
  std::vector<std::string> tacit_asset_type_ids;
  bool creates_or_references_evidence{};
};

struct ForeignTechnologyMinimumUnderstandingEntry {
  std::string component_id;
  ForeignUnderstandingState understanding{};
};

struct ForeignTechnologyRuntimePolicy {
  std::vector<ForeignTechnologyMinimumUnderstandingEntry>
      minimum_understanding_by_component;
  bool conceptual_inspiration_if_characterized{};
  double minimum_training_continuity_for_trained{};
  double minimum_translation_quality_beyond_access{};
  double new_observation_confidence{};
  double new_characterized_confidence{};
  double controlled_analysis_confidence_minimum{};
  double operational_fact_confidence_minimum{};
  double engineering_understood_confidence_minimum{};
};

// Owns immutable foreign-technology definitions and indexes. Returned spans
// and references borrow this catalog and are invalidated by destruction or
// move-assignment.
class AdaptiveResearchForeignTechnologyCatalog final {
public:
  ~AdaptiveResearchForeignTechnologyCatalog();
  AdaptiveResearchForeignTechnologyCatalog(
      AdaptiveResearchForeignTechnologyCatalog &&) noexcept;
  AdaptiveResearchForeignTechnologyCatalog &
  operator=(AdaptiveResearchForeignTechnologyCatalog &&) noexcept;
  AdaptiveResearchForeignTechnologyCatalog(
      const AdaptiveResearchForeignTechnologyCatalog &) = delete;
  AdaptiveResearchForeignTechnologyCatalog &
  operator=(const AdaptiveResearchForeignTechnologyCatalog &) = delete;

  [[nodiscard]] std::span<const std::string> constraint_ids() const noexcept;
  [[nodiscard]] std::span<const ForeignTechnologyPackageComponentDefinition>
  components() const noexcept;
  [[nodiscard]] std::span<const std::string> rights() const noexcept;
  [[nodiscard]] const ForeignTechnologyRuntimePolicy &
  runtime_policy() const noexcept;
  [[nodiscard]] bool has_constraint(std::string_view id) const noexcept;
  [[nodiscard]] bool has_component(std::string_view id) const noexcept;
  [[nodiscard]] bool has_right(std::string_view id) const noexcept;
  [[nodiscard]] const ForeignTechnologyPackageComponentDefinition &
  get_component(std::string_view id) const;
  [[nodiscard]] ForeignUnderstandingState
  minimum_understanding(std::string_view component_id) const;
  [[nodiscard]] static ForeignUnderstandingState
  parse_understanding(std::string_view id);

private:
  struct Storage;
  explicit AdaptiveResearchForeignTechnologyCatalog(
      std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchForeignTechnologyCatalog
  load_adaptive_research_foreign_technology_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &,
      const AdaptiveResearchExpertiseCatalog &);
};

[[nodiscard]] AdaptiveResearchForeignTechnologyCatalog
load_adaptive_research_foreign_technology_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog);
AdaptiveResearchForeignTechnologyCatalog
load_adaptive_research_foreign_technology_catalog(
    const std::filesystem::path &, AdaptiveResearchCatalog &&,
    const AdaptiveResearchExpertiseCatalog &) = delete;
AdaptiveResearchForeignTechnologyCatalog
load_adaptive_research_foreign_technology_catalog(
    const std::filesystem::path &, const AdaptiveResearchCatalog &,
    AdaptiveResearchExpertiseCatalog &&) = delete;

struct ForeignTechnologyAssessmentRuntimeState {
  std::string foreign_technology_reference;
  std::string source_lineage_reference;
  ForeignUnderstandingState understanding{};
  ForeignOperabilityState operability{};
  ForeignReproductionState reproduction{};
  ForeignAdaptationState adaptation{};
  std::vector<std::string> known_constraint_ids;
  std::vector<std::string> evidence_refs;
  std::vector<std::string> tacit_asset_refs;
  double last_assessment_year{};
  double confidence{};
  std::int64_t revision{};
};

struct ForeignTechnologyPackageRuntimeState {
  std::string package_id;
  std::string foreign_technology_reference;
  std::string source_lineage_reference;
  std::vector<std::string> component_ids;
  std::vector<std::string> right_ids;
  std::vector<std::string> evidence_refs;
  std::vector<std::string> tacit_asset_refs;
  std::vector<std::string> knowledge_field_ids;
  std::string provenance;
  double integrity{};
  std::int64_t revision{};
};

// Sparse per-civilization support state. Spans preserve source Dictionary
// order and are invalidated by the next mutation. Copies own independent
// values; no support is stored in the civilization state or its snapshots.
class AdaptiveResearchForeignTechnologyState final {
public:
  AdaptiveResearchForeignTechnologyState();
  ~AdaptiveResearchForeignTechnologyState();
  AdaptiveResearchForeignTechnologyState(
      const AdaptiveResearchForeignTechnologyState &);
  AdaptiveResearchForeignTechnologyState &
  operator=(const AdaptiveResearchForeignTechnologyState &);
  AdaptiveResearchForeignTechnologyState(
      AdaptiveResearchForeignTechnologyState &&) noexcept;
  AdaptiveResearchForeignTechnologyState &
  operator=(AdaptiveResearchForeignTechnologyState &&) noexcept;

  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] std::span<const ForeignTechnologyAssessmentRuntimeState>
  assessments() const noexcept;
  [[nodiscard]] std::span<const ForeignTechnologyPackageRuntimeState>
  packages() const noexcept;
  [[nodiscard]] const ForeignTechnologyAssessmentRuntimeState *
  try_get_assessment(
      std::string_view foreign_technology_reference) const noexcept;
  [[nodiscard]] const ForeignTechnologyPackageRuntimeState *
  try_get_package(std::string_view package_id) const noexcept;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  get_or_unknown(std::string foreign_technology_reference,
                 std::string source_lineage_reference) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  class Writer;
  friend class AdaptiveResearchForeignTechnologyRuntime;
  friend class detail::AdaptiveResearchForeignTechnologyStateWriter;
};

struct ForeignTechnologyEvidenceTransfer {
  std::string evidence_instance_id;
  std::string evidence_type_id;
  std::string provenance;
  double quality{};
  double confidence{};
  std::optional<std::string> context_id;
};

struct ForeignTechnologyPackageInput {
  std::string package_id;
  std::string foreign_technology_reference;
  std::string source_lineage_reference;
  std::vector<std::string> component_ids;
  std::vector<std::string> right_ids;
  std::vector<std::string> knowledge_field_ids;
  std::vector<ForeignTechnologyEvidenceTransfer> evidence;
  std::vector<std::string> known_constraint_ids;
  std::string provenance;
  double integrity{};
  double translation_context_quality{};
  double training_continuity{};
  double acquired_year{};
  std::optional<std::string> target_applicability_context_id;
};

struct ForeignTechnologyRecipientValueContext {
  double capability_novelty{};
  double strategic_need{};
  double expected_native_work_saved{};
  double recipient_readiness{};
  double recipient_operability_fit{};
  double dependency_safety{};
  double known_third_party_demand{};
  double package_transferability{};
  double scarcity_or_exclusivity{};
  double dependency_risk{};
  double hazard_risk{};
};

struct ForeignTechnologyRecipientValueAssessment {
  double research_utility{};
  double operational_utility{};
  double resale_or_brokerage_utility{};
  double dependency_risk{};
  double hazard_risk{};
  bool holder_incompatibility_can_still_broker{};
  std::string explanation;
};

// Borrows one stable authority and foreign-technology catalog. Both must
// outlive this runtime and remain unmoved. Runtime moves preserve its weak
// state table and support addresses. Inputs are copied before any mutation.
class AdaptiveResearchForeignTechnologyRuntime final {
public:
  AdaptiveResearchForeignTechnologyRuntime(
      const AdaptiveResearchAuthority &authority,
      const AdaptiveResearchForeignTechnologyCatalog &catalog);
  AdaptiveResearchForeignTechnologyRuntime(
      AdaptiveResearchAuthority &&,
      const AdaptiveResearchForeignTechnologyCatalog &) = delete;
  AdaptiveResearchForeignTechnologyRuntime(
      const AdaptiveResearchAuthority &,
      AdaptiveResearchForeignTechnologyCatalog &&) = delete;
  ~AdaptiveResearchForeignTechnologyRuntime();
  AdaptiveResearchForeignTechnologyRuntime(
      AdaptiveResearchForeignTechnologyRuntime &&) noexcept;
  AdaptiveResearchForeignTechnologyRuntime &
  operator=(AdaptiveResearchForeignTechnologyRuntime &&) noexcept;
  AdaptiveResearchForeignTechnologyRuntime(
      const AdaptiveResearchForeignTechnologyRuntime &) = delete;
  AdaptiveResearchForeignTechnologyRuntime &
  operator=(const AdaptiveResearchForeignTechnologyRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchForeignTechnologyState &
  state(const AdaptiveResearchCivilizationState &civilization) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  observe(AdaptiveResearchCivilizationState &state,
          std::string_view foreign_technology_reference,
          std::string_view source_lineage_reference, double confidence,
          double year,
          std::span<const std::string> known_constraint_ids = {}) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  acquire_package(AdaptiveResearchCivilizationState &state,
                  const ForeignTechnologyPackageInput &input) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState record_analysis_result(
      AdaptiveResearchCivilizationState &state,
      std::string_view foreign_technology_reference,
      ForeignUnderstandingState target_understanding, double confidence,
      double year,
      std::span<const std::string> newly_known_constraint_ids = {}) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  record_operability_fact(AdaptiveResearchCivilizationState &state,
                          std::string_view foreign_technology_reference,
                          ForeignOperabilityState operability,
                          double confidence, double year) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  record_reproduction_fact(AdaptiveResearchCivilizationState &state,
                           std::string_view foreign_technology_reference,
                           ForeignReproductionState reproduction,
                           double confidence, double year) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  record_adaptation_result(AdaptiveResearchCivilizationState &state,
                           std::string_view foreign_technology_reference,
                           ForeignAdaptationState adaptation, double confidence,
                           double year) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  confirm_constraint(AdaptiveResearchCivilizationState &state,
                     std::string_view foreign_technology_reference,
                     std::string_view constraint_id, double year) const;
  [[nodiscard]] ForeignTechnologyAssessmentRuntimeState
  resolve_constraint(AdaptiveResearchCivilizationState &state,
                     std::string_view foreign_technology_reference,
                     std::string_view constraint_id, double year) const;
  void advance_package_tacit_assimilation(
      AdaptiveResearchCivilizationState &state, std::string_view package_id,
      ResearchTacitAssimilationStage target_stage) const;
  [[nodiscard]] ForeignTechnologyRecipientValueAssessment
  evaluate_recipient_value(
      const ForeignTechnologyPackageRuntimeState &package,
      const ForeignTechnologyRecipientValueContext &context,
      bool holder_currently_unusable) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::AdaptiveResearchForeignTechnologySupportAccess;
};

} // namespace stellar::core
