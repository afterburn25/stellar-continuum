#pragma once

#include <stellar/core/adaptive_research_foreign_technology.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class ForeignDiscoveryTriggerAxis {
  understanding,
  adaptation,
};

struct ForeignResearchMethodCandidateRule {
  ForeignDiscoveryTriggerAxis trigger_axis{};
  int minimum_axis_rank{};
  std::vector<std::string> node_ids;
  ResearchMaturity awareness_state{};
};

struct ForeignResearchAwarenessEvent {
  std::string foreign_technology_reference;
  std::string node_id;
  std::optional<ResearchMaturity> previous_state;
  ResearchMaturity new_state{};
  std::string reason;
};

struct ForeignResearchDiscoveryResult {
  ForeignTechnologyAssessmentRuntimeState assessment;
  std::vector<ForeignResearchAwarenessEvent> awareness_events;
};

// Owns the validated foreign-awareness policy and its ordered method rules.
// Returned spans remain valid until destruction or move-assignment.
class AdaptiveResearchForeignDiscoveryCatalog final {
public:
  ~AdaptiveResearchForeignDiscoveryCatalog();
  AdaptiveResearchForeignDiscoveryCatalog(
      AdaptiveResearchForeignDiscoveryCatalog &&) noexcept;
  AdaptiveResearchForeignDiscoveryCatalog &
  operator=(AdaptiveResearchForeignDiscoveryCatalog &&) noexcept;
  AdaptiveResearchForeignDiscoveryCatalog(
      const AdaptiveResearchForeignDiscoveryCatalog &) = delete;
  AdaptiveResearchForeignDiscoveryCatalog &
  operator=(const AdaptiveResearchForeignDiscoveryCatalog &) = delete;

  [[nodiscard]] ResearchMaturity observed_evidence_state() const noexcept;
  [[nodiscard]] ResearchMaturity characterized_evidence_state() const noexcept;
  [[nodiscard]] std::span<const ForeignResearchMethodCandidateRule>
  method_rules() const noexcept;
  [[nodiscard]] static ForeignAdaptationState
  parse_adaptation(std::string_view id);

private:
  struct Storage;
  explicit AdaptiveResearchForeignDiscoveryCatalog(
      std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchForeignDiscoveryCatalog
  load_adaptive_research_foreign_discovery_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &);
};

[[nodiscard]] AdaptiveResearchForeignDiscoveryCatalog
load_adaptive_research_foreign_discovery_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog);
AdaptiveResearchForeignDiscoveryCatalog
load_adaptive_research_foreign_discovery_catalog(
    const std::filesystem::path &, AdaptiveResearchCatalog &&) = delete;

// Borrows three stable dependencies. They must outlive this coordinator and
// remain unmoved. Every caller-owned view is copied before factual mutation.
class AdaptiveResearchForeignDiscoveryRuntime final {
public:
  AdaptiveResearchForeignDiscoveryRuntime(
      const AdaptiveResearchAuthority &authority,
      const AdaptiveResearchForeignTechnologyRuntime &foreign_technology,
      const AdaptiveResearchForeignDiscoveryCatalog &catalog) noexcept;
  AdaptiveResearchForeignDiscoveryRuntime(
      AdaptiveResearchAuthority &&,
      const AdaptiveResearchForeignTechnologyRuntime &,
      const AdaptiveResearchForeignDiscoveryCatalog &) = delete;
  AdaptiveResearchForeignDiscoveryRuntime(
      const AdaptiveResearchAuthority &,
      AdaptiveResearchForeignTechnologyRuntime &&,
      const AdaptiveResearchForeignDiscoveryCatalog &) = delete;
  AdaptiveResearchForeignDiscoveryRuntime(
      const AdaptiveResearchAuthority &,
      const AdaptiveResearchForeignTechnologyRuntime &,
      AdaptiveResearchForeignDiscoveryCatalog &&) = delete;

  [[nodiscard]] ForeignResearchDiscoveryResult
  observe(AdaptiveResearchCivilizationState &state,
          std::string_view foreign_technology_reference,
          std::string_view source_lineage_reference, double confidence,
          double year,
          std::span<const std::string> known_constraint_ids = {},
          std::optional<std::string_view> target_applicability_context_id =
              std::nullopt) const;
  [[nodiscard]] ForeignResearchDiscoveryResult
  acquire_package(AdaptiveResearchCivilizationState &state,
                  const ForeignTechnologyPackageInput &input) const;
  [[nodiscard]] ForeignResearchDiscoveryResult record_analysis_result(
      AdaptiveResearchCivilizationState &state,
      std::string_view foreign_technology_reference,
      ForeignUnderstandingState target_understanding, double confidence,
      double year,
      std::span<const std::string> newly_known_constraint_ids = {},
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] ForeignResearchDiscoveryResult record_adaptation_result(
      AdaptiveResearchCivilizationState &state,
      std::string_view foreign_technology_reference,
      ForeignAdaptationState adaptation, double confidence, double year,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] std::vector<ForeignResearchAwarenessEvent>
  reevaluate(AdaptiveResearchCivilizationState &state,
             std::string_view foreign_technology_reference,
             std::optional<std::string_view>
                 target_applicability_context_id = std::nullopt) const;

private:
  const AdaptiveResearchAuthority *authority_{};
  const AdaptiveResearchForeignTechnologyRuntime *foreign_technology_{};
  const AdaptiveResearchForeignDiscoveryCatalog *catalog_{};
};

} // namespace stellar::core
