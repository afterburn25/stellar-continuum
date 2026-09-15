#pragma once

#include <stellar/core/adaptive_research_applicability_catalog.hpp>
#include <stellar/core/adaptive_research_catalog.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_state.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct AdaptiveResearchNodeSnapshot {
  std::string node_id;
  ResearchMaturity maturity{};
  std::optional<std::string> resolution;
  double stage_research_points{};
  double total_research_points{};
};
struct AdaptiveResearchPressureSnapshot {
  std::string pressure_id;
  double value{};
};
struct AdaptiveResearchEvidenceSnapshot {
  std::string evidence_instance_id;
  std::string evidence_type_id;
  std::string provenance;
  double quality{};
  double confidence{};
  std::optional<std::string> context_id;
};
struct AdaptiveResearchCapabilitySnapshot {
  std::string capability_id;
  std::optional<std::string> context_id;
};
struct AdaptiveResearchProjectSnapshot {
  std::string node_id;
  ResearchMaturity stage{};
  std::optional<std::string> target_applicability_context_id;
  double assigned_effective_labs{};
  double readiness_efficiency{};
  bool paused{};
  std::optional<std::string> pause_reason;
  double stage_research_points{};
  double total_research_points{};
};
struct AdaptiveResearchContextSnapshot {
  std::string context_id;
  std::vector<std::string> traits;
};

struct AdaptiveResearchStateSnapshot {
  int schema_version{};
  std::string catalog_id;
  std::string civilization_id;
  std::string directed_program_stage_id;
  double total_effective_research_labs{};
  std::vector<AdaptiveResearchNodeSnapshot> nodes;
  std::vector<AdaptiveResearchPressureSnapshot> pressures;
  std::vector<AdaptiveResearchEvidenceSnapshot> evidence;
  std::vector<std::string> civilization_traits;
  std::vector<AdaptiveResearchContextSnapshot> applicability_contexts;
  std::vector<AdaptiveResearchCapabilitySnapshot> capabilities;
  std::vector<std::string> facility_capabilities;
  std::vector<std::string> enabled_deployment_event_ids;
  std::vector<AdaptiveResearchProjectSnapshot> active_projects;
};

class AdaptiveResearchSnapshotError final : public std::runtime_error {
public:
  explicit AdaptiveResearchSnapshotError(std::string message);
};
class AdaptiveResearchSnapshotJsonError final : public std::runtime_error {
public:
  explicit AdaptiveResearchSnapshotJsonError(std::string message);
};

// The three catalogs are borrowed and must outlive this codec without being
// moved. Snapshots, serialized strings and restored civilization states own all
// content.
class AdaptiveResearchSnapshotCodec final {
public:
  static constexpr int current_schema_version = 1;
  AdaptiveResearchSnapshotCodec(
      const AdaptiveResearchCatalog &catalog,
      const AdaptiveResearchApplicabilityCatalog &applicability,
      const AdaptiveResearchFacilityCatalog &facilities) noexcept;
  AdaptiveResearchSnapshotCodec(
      AdaptiveResearchCatalog &&, const AdaptiveResearchApplicabilityCatalog &,
      const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchSnapshotCodec(
      const AdaptiveResearchCatalog &, AdaptiveResearchApplicabilityCatalog &&,
      const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchSnapshotCodec(const AdaptiveResearchCatalog &,
                                const AdaptiveResearchApplicabilityCatalog &,
                                AdaptiveResearchFacilityCatalog &&) = delete;
  AdaptiveResearchSnapshotCodec(const AdaptiveResearchSnapshotCodec &) = delete;
  AdaptiveResearchSnapshotCodec &
  operator=(const AdaptiveResearchSnapshotCodec &) = delete;
  AdaptiveResearchSnapshotCodec(AdaptiveResearchSnapshotCodec &&) noexcept =
      default;
  AdaptiveResearchSnapshotCodec &
  operator=(AdaptiveResearchSnapshotCodec &&) noexcept = default;
  [[nodiscard]] AdaptiveResearchStateSnapshot
  capture(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::string
  serialize(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  deserialize(std::string_view json) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  restore(const AdaptiveResearchStateSnapshot &snapshot) const;

private:
  const AdaptiveResearchCatalog *catalog_;
  const AdaptiveResearchApplicabilityCatalog *applicability_;
  const AdaptiveResearchFacilityCatalog *facilities_;
};

} // namespace stellar::core
