#pragma once

#include <stellar/core/adaptive_research_expertise_snapshot.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct AdaptiveResearchPressureSupportSnapshot {
  std::vector<ResearchPressureMetricSignalEntry> metric_signals;
  std::vector<std::string> active_pressure_ids;
};

struct AdaptiveResearchAgendaSnapshot {
  std::vector<ResearchAgendaPriorityEntry> domain_priorities;
  std::vector<ResearchAgendaPriorityEntry> field_priorities;
  std::vector<ResearchAgendaPriorityEntry> problem_priorities;
  std::vector<ResearchAgendaPriorityEntry> capability_priorities;
  ResearchAgendaOrientationState orientations;
  std::vector<ResearchScientificCultureAxisState> culture_axes;
  std::optional<double> last_major_review_year;
  std::string policy_provenance;
};

struct AdaptiveResearchStateSnapshotV3 {
  int schema_version{};
  std::string catalog_id;
  AdaptiveResearchStateSnapshotV2 research;
  AdaptiveResearchPressureSupportSnapshot pressure_support;
  AdaptiveResearchAgendaSnapshot agenda;
};

class AdaptiveResearchStrategicSnapshotError final : public std::runtime_error {
public:
  explicit AdaptiveResearchStrategicSnapshotError(std::string message);
};

// Borrows one stable strategic runtime and composes its existing schema-2
// codec. The runtime must outlive this codec and remain unmoved. Captured DTOs,
// serialized strings, and restored states own their values. A moved-from codec
// may only be destroyed or assigned; move-assignment replaces the borrowed
// runtime association.
class AdaptiveResearchStrategicSnapshotCodec final {
public:
  static constexpr int current_schema_version = 3;

  explicit AdaptiveResearchStrategicSnapshotCodec(
      const AdaptiveResearchStrategicRuntime &runtime);
  AdaptiveResearchStrategicSnapshotCodec(
      AdaptiveResearchStrategicRuntime &&) = delete;
  ~AdaptiveResearchStrategicSnapshotCodec();
  AdaptiveResearchStrategicSnapshotCodec(
      AdaptiveResearchStrategicSnapshotCodec &&) noexcept;
  AdaptiveResearchStrategicSnapshotCodec &operator=(
      AdaptiveResearchStrategicSnapshotCodec &&) noexcept;
  AdaptiveResearchStrategicSnapshotCodec(
      const AdaptiveResearchStrategicSnapshotCodec &) = delete;
  AdaptiveResearchStrategicSnapshotCodec &operator=(
      const AdaptiveResearchStrategicSnapshotCodec &) = delete;

  [[nodiscard]] AdaptiveResearchStateSnapshotV3
  capture(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] std::string
  serialize(const AdaptiveResearchCivilizationState &state) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  deserialize(std::string_view json) const;
  [[nodiscard]] AdaptiveResearchCivilizationState
  restore(const AdaptiveResearchStateSnapshotV3 &snapshot) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
