#pragma once

#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_snapshot.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct ResearchFieldCompetenceSnapshot {
  std::string field_id;
  ResearchCompetenceVector current;
  ResearchCompetenceVector historical_peak;
  double last_theoretical_activity_year{};
  double last_experimental_activity_year{};
  double last_engineering_activity_year{};
};

struct ResearchInstitutionSnapshot {
  std::string institution_instance_id;
  std::string institution_archetype_id;
  std::optional<std::string> context_id;
  int total_count{};
  int active_count{};
};

struct ResearchTacitAssetSnapshot {
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
};

struct AdaptiveResearchExpertiseSnapshot {
  std::vector<ResearchFieldCompetenceSnapshot> fields;
  std::vector<ResearchInstitutionSnapshot> institutions;
  std::vector<ResearchTacitAssetSnapshot> tacit_assets;
};

struct AdaptiveResearchStateSnapshotV2 {
  int schema_version{};
  std::string catalog_id;
  AdaptiveResearchStateSnapshot core;
  AdaptiveResearchExpertiseSnapshot expertise;
  // DTO decoder metadata for source nullable reference records. Existing
  // typed callers keep both present by default.
  bool core_present{true};
  bool expertise_present{true};
};

// Borrows one stable AdaptiveResearchAuthority. The authority must outlive the
// codec and remain unmoved. Captured DTOs, serialized strings, and restored
// states own their values. A moved-from codec may only be destroyed or assigned
// a valid codec. Move-assignment replaces the borrowed authority association;
// the previous authority no longer needs to outlive the assigned codec.
class AdaptiveResearchSnapshotV2Codec final {
public:
  static constexpr int current_schema_version = 2;

  explicit AdaptiveResearchSnapshotV2Codec(
      const AdaptiveResearchAuthority &authority);
  AdaptiveResearchSnapshotV2Codec(AdaptiveResearchAuthority &&) = delete;
  ~AdaptiveResearchSnapshotV2Codec();
  AdaptiveResearchSnapshotV2Codec(
      AdaptiveResearchSnapshotV2Codec &&) noexcept;
  AdaptiveResearchSnapshotV2Codec &operator=(
      AdaptiveResearchSnapshotV2Codec &&) noexcept;
  AdaptiveResearchSnapshotV2Codec(
      const AdaptiveResearchSnapshotV2Codec &) = delete;
  AdaptiveResearchSnapshotV2Codec &operator=(
      const AdaptiveResearchSnapshotV2Codec &) = delete;

  [[nodiscard]] AdaptiveResearchStateSnapshotV2
  capture(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::string
  serialize(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  deserialize(std::string_view json) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  restore(const AdaptiveResearchStateSnapshotV2 &snapshot) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
