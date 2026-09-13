#pragma once

#include <stellar/core/adaptive_research_foreign_technology_snapshot.hpp>
#include <stellar/core/adaptive_research_outcomes.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct AdaptiveResearchOutcomeSnapshot {
  std::vector<ResearchOutcomeNodeSummary> summaries;
  std::vector<ResearchOutcomeHistoryRecord> recent_records;
};

struct AdaptiveResearchStateSnapshotV5 {
  int schema_version{};
  std::string catalog_id;
  AdaptiveResearchStateSnapshotV4 research;
  AdaptiveResearchOutcomeSnapshot outcomes;
};

class AdaptiveResearchOutcomeSnapshotError final : public std::runtime_error {
public:
  explicit AdaptiveResearchOutcomeSnapshotError(std::string message);
};

// Borrows one stable strategic runtime and composes its schema-4 codec. The
// runtime must outlive this codec and remain unmoved. Captured DTOs, serialized
// strings, and restored states own their values. Move-assignment replaces the
// borrowed runtime association; a moved-from codec may only be destroyed or
// assigned a valid codec.
class AdaptiveResearchOutcomeSnapshotCodec final {
public:
  static constexpr int current_schema_version = 5;

  explicit AdaptiveResearchOutcomeSnapshotCodec(
      const AdaptiveResearchStrategicRuntime &runtime);
  AdaptiveResearchOutcomeSnapshotCodec(AdaptiveResearchStrategicRuntime &&) =
      delete;
  ~AdaptiveResearchOutcomeSnapshotCodec();
  AdaptiveResearchOutcomeSnapshotCodec(
      AdaptiveResearchOutcomeSnapshotCodec &&) noexcept;
  AdaptiveResearchOutcomeSnapshotCodec &operator=(
      AdaptiveResearchOutcomeSnapshotCodec &&) noexcept;
  AdaptiveResearchOutcomeSnapshotCodec(
      const AdaptiveResearchOutcomeSnapshotCodec &) = delete;
  AdaptiveResearchOutcomeSnapshotCodec &operator=(
      const AdaptiveResearchOutcomeSnapshotCodec &) = delete;

  [[nodiscard]] AdaptiveResearchStateSnapshotV5
  capture(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::string
  serialize(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  deserialize(std::string_view json) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  restore(const AdaptiveResearchStateSnapshotV5 &snapshot) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
