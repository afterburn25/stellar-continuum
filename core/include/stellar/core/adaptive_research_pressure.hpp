#pragma once

#include <stellar/core/adaptive_research_runtime.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stellar::core {

namespace detail {
class AdaptiveResearchPressureStateWriter;
}

struct ResearchPressureRuleDefinition {
  std::string id;
  std::vector<std::string> metric_signal_ids;
  std::vector<std::string> event_signal_ids;
  double decay_per_year{};
  double memory_floor{};
};

struct ResearchPressureRuntimePolicy {
  double strongest_signal_weight{};
  double mean_signal_weight{};
  double rise_toward_target_per_year{};
  double event_pulse_base_points{};
  double intended_review_interval_years{};
  double dormant_review_interval_years{};
};

// Owns immutable pressure rules and indexes. Returned references and spans
// borrow this catalog and remain valid until it is destroyed or move-assigned.
class AdaptiveResearchPressureCatalog final {
public:
  ~AdaptiveResearchPressureCatalog();
  AdaptiveResearchPressureCatalog(AdaptiveResearchPressureCatalog &&) noexcept;
  AdaptiveResearchPressureCatalog &
  operator=(AdaptiveResearchPressureCatalog &&) noexcept;
  AdaptiveResearchPressureCatalog(const AdaptiveResearchPressureCatalog &) =
      delete;
  AdaptiveResearchPressureCatalog &
  operator=(const AdaptiveResearchPressureCatalog &) = delete;

  [[nodiscard]] std::span<const ResearchPressureRuleDefinition>
  rules() const noexcept;
  [[nodiscard]] const ResearchPressureRuntimePolicy &
  runtime_policy() const noexcept;
  [[nodiscard]] bool
  is_known_metric_signal(std::string_view signal_id) const noexcept;
  [[nodiscard]] bool
  is_known_event_signal(std::string_view signal_id) const noexcept;
  [[nodiscard]] const ResearchPressureRuleDefinition &
  get_rule(std::string_view pressure_id) const;
  [[nodiscard]] std::span<const std::string>
  pressures_for_metric_signal(std::string_view signal_id) const noexcept;
  [[nodiscard]] std::span<const std::string>
  pressures_for_event_signal(std::string_view signal_id) const noexcept;

private:
  struct Storage;
  explicit AdaptiveResearchPressureCatalog(
      std::unique_ptr<Storage> storage) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchPressureCatalog
  load_adaptive_research_pressure_catalog(const std::filesystem::path &,
                                          const AdaptiveResearchCatalog &);
};

[[nodiscard]] AdaptiveResearchPressureCatalog
load_adaptive_research_pressure_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &research_catalog);
AdaptiveResearchPressureCatalog
load_adaptive_research_pressure_catalog(const std::filesystem::path &,
                                        AdaptiveResearchCatalog &&) = delete;

struct ResearchPressureMetricSignalEntry {
  std::string signal_id;
  double value{};
};

// Sparse support state owned by a pressure runtime's weak state table. Query
// spans preserve source Dictionary/HashSet runtime order and are invalidated by
// the next mutation. Copies are independent snapshots with their own storage.
class AdaptiveResearchPressureState final {
public:
  AdaptiveResearchPressureState();
  ~AdaptiveResearchPressureState();
  AdaptiveResearchPressureState(AdaptiveResearchPressureState &&) noexcept;
  AdaptiveResearchPressureState &
  operator=(AdaptiveResearchPressureState &&) noexcept;
  AdaptiveResearchPressureState(const AdaptiveResearchPressureState &);
  AdaptiveResearchPressureState &
  operator=(const AdaptiveResearchPressureState &);

  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] std::span<const ResearchPressureMetricSignalEntry>
  metric_signals() const noexcept;
  [[nodiscard]] std::span<const std::string>
  active_pressure_ids() const noexcept;
  [[nodiscard]] double
  get_metric_signal(std::string_view signal_id) const noexcept;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  class Writer;
  friend class AdaptiveResearchPressureRuntime;
  friend class detail::AdaptiveResearchPressureStateWriter;
};

// Borrows the kernel and pressure catalog; both must outlive this runtime and
// remain unmoved. The runtime owns a stable weak-key table. State support does
// not keep civilization state alive, is isolated per runtime and per state
// identity, and survives runtime moves. Returned support references borrow the
// moved-to runtime and require the matching state identity to remain alive.
class AdaptiveResearchPressureRuntime final {
public:
  AdaptiveResearchPressureRuntime(
      const AdaptiveResearchRuntime &kernel,
      const AdaptiveResearchPressureCatalog &catalog);
  AdaptiveResearchPressureRuntime(AdaptiveResearchRuntime &&,
                                  const AdaptiveResearchPressureCatalog &) =
      delete;
  AdaptiveResearchPressureRuntime(const AdaptiveResearchRuntime &,
                                  AdaptiveResearchPressureCatalog &&) = delete;
  ~AdaptiveResearchPressureRuntime();
  AdaptiveResearchPressureRuntime(AdaptiveResearchPressureRuntime &&) noexcept;
  AdaptiveResearchPressureRuntime &
  operator=(AdaptiveResearchPressureRuntime &&) noexcept;
  AdaptiveResearchPressureRuntime(const AdaptiveResearchPressureRuntime &) =
      delete;
  AdaptiveResearchPressureRuntime &
  operator=(const AdaptiveResearchPressureRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchPressureState &
  get_support_state(const AdaptiveResearchCivilizationState &state) const;
  void report_metric_signal(AdaptiveResearchCivilizationState &state,
                            std::string_view signal_id,
                            double normalized_value) const;
  void report_metric_signals(AdaptiveResearchCivilizationState &state,
                             std::span<const ResearchPressureMetricSignalEntry>
                                 normalized_signals) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> report_event_signal(
      AdaptiveResearchCivilizationState &state,
      std::string_view event_signal_id, double normalized_severity,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  advance(AdaptiveResearchCivilizationState &state, double elapsed_years,
          std::optional<std::string_view> target_applicability_context_id =
              std::nullopt) const;
  [[nodiscard]] double
  get_metric_target(const AdaptiveResearchCivilizationState &state,
                    std::string_view pressure_id) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
