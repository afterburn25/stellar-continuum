#pragma once

#include <stellar/core/adaptive_research_strategic_snapshot.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct ForeignTechnologyAssessmentSnapshot {
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
};

struct ForeignTechnologyPackageSnapshot {
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
};

struct AdaptiveResearchStateSnapshotV4 {
  int schema_version{};
  std::string catalog_id;
  AdaptiveResearchStateSnapshotV3 research;
  std::vector<ForeignTechnologyAssessmentSnapshot> foreign_assessments;
  std::vector<ForeignTechnologyPackageSnapshot> foreign_packages;
};

class AdaptiveResearchForeignTechnologySnapshotError final
    : public std::runtime_error {
public:
  explicit AdaptiveResearchForeignTechnologySnapshotError(std::string message);
};

// Borrows one stable strategic runtime and composes its schema-3 codec. The
// runtime must outlive this codec and remain unmoved. Captured DTOs, serialized
// strings, and restored states own their values. Move-assignment replaces the
// borrowed runtime association; a moved-from codec may only be destroyed or
// assigned a valid codec.
class AdaptiveResearchForeignTechnologySnapshotCodec final {
public:
  static constexpr int current_schema_version = 4;

  explicit AdaptiveResearchForeignTechnologySnapshotCodec(
      const AdaptiveResearchStrategicRuntime &runtime);
  AdaptiveResearchForeignTechnologySnapshotCodec(
      AdaptiveResearchStrategicRuntime &&) = delete;
  ~AdaptiveResearchForeignTechnologySnapshotCodec();
  AdaptiveResearchForeignTechnologySnapshotCodec(
      AdaptiveResearchForeignTechnologySnapshotCodec &&) noexcept;
  AdaptiveResearchForeignTechnologySnapshotCodec &
  operator=(AdaptiveResearchForeignTechnologySnapshotCodec &&) noexcept;
  AdaptiveResearchForeignTechnologySnapshotCodec(
      const AdaptiveResearchForeignTechnologySnapshotCodec &) = delete;
  AdaptiveResearchForeignTechnologySnapshotCodec &
  operator=(const AdaptiveResearchForeignTechnologySnapshotCodec &) = delete;

  [[nodiscard]] AdaptiveResearchStateSnapshotV4
  capture(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::string
  serialize(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  deserialize(std::string_view json) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  restore(const AdaptiveResearchStateSnapshotV4 &snapshot) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
