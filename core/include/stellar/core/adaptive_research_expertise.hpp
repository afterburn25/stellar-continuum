#pragma once

#include <stellar/core/adaptive_research_catalog.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>

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

enum class ResearchCompetenceComponent {
  theoretical,
  experimental,
  engineering,
};

enum class ResearchTacitAssimilationStage {
  access,
  interpreted,
  codified,
  trained,
  native_practice,
};

enum class ResearchTacitScopeKind {
  knowledge_field,
  technology_node,
  solution_family,
  foreign_lineage,
  facility_or_process,
};

struct ResearchCompetenceVector {
  double theoretical{};
  double experimental{};
  double engineering{};

  [[nodiscard]] double get(ResearchCompetenceComponent component) const;
};

struct ResearchKnowledgeFieldDefinition {
  std::string id;
  std::string name;
  std::string family;
  std::vector<std::string> related_field_ids;
};

struct ResearchTacitAssetTypeDefinition {
  std::string id;
  std::vector<ResearchCompetenceComponent> supported_components;
};

struct ResearchInstitutionSpecializationDefinition {
  std::string institution_archetype_id;
  double effective_lab_units{};
  std::vector<std::string> specialized_field_ids;
  std::vector<std::string> facility_capability_ids;
};

struct ResearchReadinessComponentWeights {
  double field_competence{};
  double facility_readiness{};
  double evidence_readiness{};
  double tacit_expertise{};
};

struct ResearchStageCompetenceWeights {
  double theoretical{};
  double experimental{};
  double engineering{};
};

struct ResearchStageCompetenceWeightEntry {
  ResearchMaturity stage{};
  ResearchStageCompetenceWeights weights;
};

struct ResearchStagePracticeGain {
  ResearchMaturity stage{};
  ResearchCompetenceVector gain;
};

struct ResearchTacitAssimilationFactor {
  ResearchTacitAssimilationStage stage{};
  double factor{};
};

struct ResearchExpertiseRuntimePolicy {
  std::vector<ResearchStagePracticeGain> stage_practice_gain;
  double related_field_transfer_fraction{};
  double minimum_gain_factor{};
  ResearchCompetenceVector annual_atrophy_rates;
  double atrophy_grace_years{};
  double general_lab_matching_factor{};
  double specialized_matching_factor{};
  double nonmatching_specialist_factor{};
  std::vector<ResearchTacitAssimilationFactor> tacit_assimilation_factors;
  double tacit_best_weight{};
  double tacit_mean_weight{};
  double preservation_floor_fraction{};
  double established_knowledge_theoretical_floor_fraction{};
};

class AdaptiveResearchExpertiseCatalogError final : public std::runtime_error {
public:
  explicit AdaptiveResearchExpertiseCatalogError(std::string message);
};

class AdaptiveResearchExpertiseCatalog final {
public:
  AdaptiveResearchExpertiseCatalog(const AdaptiveResearchExpertiseCatalog &) =
      delete;
  AdaptiveResearchExpertiseCatalog &
  operator=(const AdaptiveResearchExpertiseCatalog &) = delete;
  AdaptiveResearchExpertiseCatalog(
      AdaptiveResearchExpertiseCatalog &&) noexcept;
  AdaptiveResearchExpertiseCatalog &
  operator=(AdaptiveResearchExpertiseCatalog &&) noexcept;
  ~AdaptiveResearchExpertiseCatalog();

  [[nodiscard]] std::span<const ResearchKnowledgeFieldDefinition>
  fields() const noexcept;
  [[nodiscard]] std::span<const ResearchStageCompetenceWeightEntry>
  stage_weights() const noexcept;
  [[nodiscard]] const ResearchReadinessComponentWeights &
  readiness_weights() const noexcept;
  [[nodiscard]] std::span<const ResearchTacitAssetTypeDefinition>
  tacit_asset_types() const noexcept;
  [[nodiscard]] std::span<const ResearchInstitutionSpecializationDefinition>
  institutions() const noexcept;
  [[nodiscard]] const ResearchExpertiseRuntimePolicy &
  runtime_policy() const noexcept;

  [[nodiscard]] const ResearchKnowledgeFieldDefinition &
  get_field(std::string_view field_id) const;
  [[nodiscard]] const ResearchInstitutionSpecializationDefinition &
  get_institution(std::string_view institution_archetype_id) const;

private:
  struct Storage;
  explicit AdaptiveResearchExpertiseCatalog(Storage storage);
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchExpertiseCatalog
  load_adaptive_research_expertise_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &,
      const AdaptiveResearchFacilityCatalog &);
};

[[nodiscard]] AdaptiveResearchExpertiseCatalog
load_adaptive_research_expertise_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchFacilityCatalog &facilities);

struct ResearchFieldCompetenceRuntimeState {
  std::string field_id;
  ResearchCompetenceVector current;
  ResearchCompetenceVector historical_peak;
  double last_theoretical_activity_year{};
  double last_experimental_activity_year{};
  double last_engineering_activity_year{};
  std::int64_t revision{};
};

struct ResearchInstitutionRuntimeState {
  std::string institution_instance_id;
  std::string institution_archetype_id;
  std::optional<std::string> context_id;
  int total_count{};
  int active_count{};
  std::int64_t revision{};

  [[nodiscard]] bool is_active() const noexcept { return active_count > 0; }
};

struct ResearchTacitAssetRuntimeState {
  std::string asset_id;
  std::string asset_type_id;
  ResearchTacitScopeKind scope_kind{};
  std::string scope_ref;
  ResearchTacitAssimilationStage assimilation_stage{};
  double depth{};
  double availability{};
  double translation_context_quality{};
  double training_continuity{};
  std::string provenance;
  std::optional<std::string> context_id;
  std::int64_t revision{};
};

namespace detail {
class AdaptiveResearchExpertiseStateWriter;
}

class AdaptiveResearchExpertiseState final {
public:
  AdaptiveResearchExpertiseState();
  ~AdaptiveResearchExpertiseState();
  AdaptiveResearchExpertiseState(const AdaptiveResearchExpertiseState &);
  AdaptiveResearchExpertiseState &
  operator=(const AdaptiveResearchExpertiseState &);
  AdaptiveResearchExpertiseState(AdaptiveResearchExpertiseState &&) noexcept;
  AdaptiveResearchExpertiseState &
  operator=(AdaptiveResearchExpertiseState &&) noexcept;

  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] std::span<const ResearchFieldCompetenceRuntimeState>
  field_competence() const noexcept;
  [[nodiscard]] std::span<const ResearchInstitutionRuntimeState>
  institutions() const noexcept;
  [[nodiscard]] std::span<const ResearchTacitAssetRuntimeState>
  tacit_assets() const noexcept;
  [[nodiscard]] ResearchFieldCompetenceRuntimeState
  get_field(std::string_view field_id) const;
  [[nodiscard]] double total_active_effective_lab_units(
      const AdaptiveResearchExpertiseCatalog &catalog) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::AdaptiveResearchExpertiseStateWriter;
};

} // namespace stellar::core
