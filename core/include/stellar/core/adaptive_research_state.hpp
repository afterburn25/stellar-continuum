#pragma once

#include <stellar/core/adaptive_research_catalog.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

class AdaptiveResearchExpertiseState;

struct ResearchNodeRuntimeState {
  std::string node_id;
  ResearchMaturity maturity{};
  std::optional<std::string> resolution;
  double stage_research_points{};
  double total_research_points{};
  std::int64_t revision{};

  [[nodiscard]] bool counts_as_established_knowledge() const noexcept;
};

struct ResearchEvidenceInstance {
  std::string evidence_instance_id;
  std::string evidence_type_id;
  std::string provenance;
  double quality{};
  double confidence{};
  std::optional<std::string> context_id;
  std::int64_t revision{};
};

struct ResearchCapabilityKey {
  std::string capability_id;
  std::optional<std::string> context_id;

  bool operator==(const ResearchCapabilityKey &) const = default;
};

struct ResearchProjectRuntimeState {
  std::string node_id;
  ResearchMaturity stage{};
  std::optional<std::string> target_applicability_context_id;
  double assigned_effective_labs{};
  double readiness_efficiency{};
  bool paused{};
  std::optional<std::string> pause_reason;
  double stage_research_points{};
  double total_research_points{};
  std::int64_t revision{};
};

struct ResearchPressureEntry {
  std::string pressure_id;
  double value{};
};

struct ResearchApplicabilityContextSnapshot {
  std::string context_id;
  std::vector<std::string> sorted_traits;
};

namespace detail {
class AdaptiveResearchStateWriter;
class AdaptiveResearchStateIdentityAccess;
} // namespace detail

class AdaptiveResearchCivilizationState {
public:
  AdaptiveResearchCivilizationState(
      std::string civilization_id,
      std::string starting_directed_program_stage_id);
  ~AdaptiveResearchCivilizationState();
  AdaptiveResearchCivilizationState(
      AdaptiveResearchCivilizationState &&) noexcept;
  AdaptiveResearchCivilizationState &
  operator=(AdaptiveResearchCivilizationState &&) noexcept;
  AdaptiveResearchCivilizationState(const AdaptiveResearchCivilizationState &);
  AdaptiveResearchCivilizationState &
  operator=(const AdaptiveResearchCivilizationState &);

  // Copies own an independent deep copy of the attached expertise sidecar.

  [[nodiscard]] const std::string &civilization_id() const noexcept;
  [[nodiscard]] std::int64_t revision() const noexcept;
  [[nodiscard]] std::int64_t materialized_view_revision() const noexcept;
  [[nodiscard]] const AdaptiveResearchExpertiseState &
  expertise() const noexcept;
  [[nodiscard]] const std::string &directed_program_stage_id() const noexcept;
  [[nodiscard]] double total_effective_research_labs() const noexcept;
  [[nodiscard]] double assigned_effective_labs() const noexcept;
  [[nodiscard]] double free_effective_labs() const noexcept;

  // These spans preserve the source collection's observable runtime order.
  // They remain valid only until the next mutation of this state.
  [[nodiscard]] std::span<const ResearchNodeRuntimeState>
  node_states() const noexcept;
  [[nodiscard]] std::span<const ResearchPressureEntry>
  pressures() const noexcept;
  [[nodiscard]] std::span<const ResearchEvidenceInstance>
  evidence_instances() const noexcept;
  [[nodiscard]] std::span<const std::string>
  civilization_traits() const noexcept;
  [[nodiscard]] std::span<const ResearchCapabilityKey>
  capabilities() const noexcept;
  [[nodiscard]] std::span<const std::string>
  facility_capabilities() const noexcept;
  [[nodiscard]] std::span<const std::string>
  enabled_deployment_event_ids() const noexcept;
  [[nodiscard]] std::span<const ResearchProjectRuntimeState>
  active_projects() const noexcept;
  // Cancelled programs retain scientific work and target context, but consume
  // neither laboratories nor program capacity and never advance.
  [[nodiscard]] std::span<const ResearchProjectRuntimeState>
  cancelled_projects() const noexcept;
  [[nodiscard]] const ResearchProjectRuntimeState *
  cancelled_project(std::string_view node_id) const noexcept;

  // Mirrors the source ApplicabilityContexts property: context enumeration is
  // runtime order and each returned trait collection is ordinally sorted.
  [[nodiscard]] std::vector<ResearchApplicabilityContextSnapshot>
  applicability_contexts() const;

  [[nodiscard]] const ResearchNodeRuntimeState *
  try_get_node_state(std::string_view node_id) const noexcept;
  [[nodiscard]] bool
  has_established_knowledge(std::string_view node_id) const noexcept;
  [[nodiscard]] const double *
  try_get_pressure(std::string_view pressure_id) const noexcept;
  [[nodiscard]] double
  get_pressure(std::string_view pressure_id) const noexcept;
  [[nodiscard]] bool
  has_evidence_type(std::string_view evidence_type_id) const noexcept;
  [[nodiscard]] bool has_evidence_type(
      std::string_view evidence_type_id,
      const std::optional<std::string> &context_id) const noexcept;
  [[nodiscard]] bool
  has_civilization_trait(std::string_view trait_id) const noexcept;
  [[nodiscard]] bool has_trait(std::string_view trait_id) const noexcept;
  [[nodiscard]] bool
  has_applicability_trait(std::string_view context_id,
                          std::string_view trait_id) const noexcept;
  [[nodiscard]] std::span<const std::string>
  get_applicability_traits(std::string_view context_id) const noexcept;
  [[nodiscard]] bool
  has_facility_capability(std::string_view capability_id) const noexcept;
  [[nodiscard]] bool
  has_capability(std::string_view capability_id,
                 const std::optional<std::string> &context_id =
                     std::nullopt) const noexcept;
  [[nodiscard]] bool
  is_deployment_event_enabled(std::string_view event_id) const noexcept;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::AdaptiveResearchStateWriter;
  friend class detail::AdaptiveResearchStateIdentityAccess;
};

} // namespace stellar::core
