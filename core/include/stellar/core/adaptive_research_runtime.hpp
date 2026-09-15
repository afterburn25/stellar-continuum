#pragma once

#include <stellar/core/adaptive_research_applicability_catalog.hpp>
#include <stellar/core/adaptive_research_catalog.hpp>
#include <stellar/core/adaptive_research_eligibility.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>
#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/adaptive_research_view.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class AdaptiveResearchRuntimeEventType {
  node_became_investigable,
  project_started,
  project_paused,
  project_resumed,
  project_labs_changed,
  project_readiness_changed,
  stage_advanced,
  hypothesis_resolution_required,
  hypothesis_disproven,
  technology_matured,
  capability_granted,
  civilization_trait_granted,
  directed_program_stage_changed,
  deployment_event_unlocked,
};

struct AdaptiveResearchRuntimeEvent {
  AdaptiveResearchRuntimeEventType type{};
  std::string civilization_id;
  std::optional<std::string> node_id;
  std::optional<std::string> subject_id;
  std::string message;
};

struct AdaptiveResearchCommandResult {
  bool accepted{};
  std::string message;
  std::vector<AdaptiveResearchRuntimeEvent> events;
  std::vector<ResearchBlocker> blockers;

  [[nodiscard]] static AdaptiveResearchCommandResult
  rejected(std::string message, std::vector<ResearchBlocker> blockers = {});
};

// Owns one immutable version of every Adaptive Research content catalog.
// Returned references borrow this bundle and remain valid until it is moved or
// destroyed. A moved-from bundle may only be destroyed or move-assigned.
class AdaptiveResearchRuntimeContent final {
public:
  AdaptiveResearchRuntimeContent(
      AdaptiveResearchCatalog &&catalog,
      AdaptiveResearchApplicabilityCatalog &&applicability,
      AdaptiveResearchFacilityCatalog &&facilities,
      AdaptiveResearchProgressPolicy &&progress_policy);
  ~AdaptiveResearchRuntimeContent();
  AdaptiveResearchRuntimeContent(AdaptiveResearchRuntimeContent &&) noexcept;
  AdaptiveResearchRuntimeContent &
  operator=(AdaptiveResearchRuntimeContent &&) noexcept;
  AdaptiveResearchRuntimeContent(const AdaptiveResearchRuntimeContent &) = delete;
  AdaptiveResearchRuntimeContent &
  operator=(const AdaptiveResearchRuntimeContent &) = delete;

  [[nodiscard]] const AdaptiveResearchCatalog &catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchApplicabilityCatalog &
  applicability() const noexcept;
  [[nodiscard]] const AdaptiveResearchFacilityCatalog &facilities() const noexcept;
  [[nodiscard]] const AdaptiveResearchProgressPolicy &
  progress_policy() const noexcept;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

// Loads in source order: base catalog, applicability, facilities, then policy.
[[nodiscard]] AdaptiveResearchRuntimeContent
load_adaptive_research_runtime_content(const std::filesystem::path &root_path);

// Runtime moves preserve the stable addresses of owned content and the helper
// objects that borrow it. All getter results borrow this runtime and are
// invalidated by its destruction or move-assignment. A moved-from runtime may
// only be destroyed or move-assigned a valid runtime before it is queried.
class AdaptiveResearchRuntime final {
public:
  explicit AdaptiveResearchRuntime(
      std::shared_ptr<const AdaptiveResearchRuntimeContent> content);
  explicit AdaptiveResearchRuntime(AdaptiveResearchRuntimeContent &&content);
  AdaptiveResearchRuntime(AdaptiveResearchRuntimeContent &) = delete;
  ~AdaptiveResearchRuntime();
  AdaptiveResearchRuntime(AdaptiveResearchRuntime &&) noexcept;
  AdaptiveResearchRuntime &operator=(AdaptiveResearchRuntime &&) noexcept;
  AdaptiveResearchRuntime(const AdaptiveResearchRuntime &) = delete;
  AdaptiveResearchRuntime &operator=(const AdaptiveResearchRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchRuntimeContent &content() const noexcept;
  [[nodiscard]] std::shared_ptr<const AdaptiveResearchRuntimeContent>
  shared_content() const noexcept;
  [[nodiscard]] const AdaptiveResearchCatalog &catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchApplicabilityCatalog &
  applicability() const noexcept;
  [[nodiscard]] const AdaptiveResearchFacilityCatalog &facilities() const noexcept;
  [[nodiscard]] const AdaptiveResearchProgressPolicy &
  progress_policy() const noexcept;
  [[nodiscard]] const AdaptiveResearchEligibilityEvaluator &
  eligibility() const noexcept;
  [[nodiscard]] const AdaptiveResearchViewBuilder &view_builder() const noexcept;

  [[nodiscard]] AdaptiveResearchCivilizationState
  create_civilization_state(std::string civilization_id) const;
  [[nodiscard]] AdaptiveResearchView build_view(
      const AdaptiveResearchCivilizationState &state,
      std::optional<std::string_view> default_target_context_id =
          std::nullopt) const;

  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  set_total_effective_research_labs(AdaptiveResearchCivilizationState &state,
                                    double total_labs) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> set_pressure(
      AdaptiveResearchCivilizationState &state, std::string_view pressure_id,
      double value,
      std::optional<std::string_view> target_context_id = std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> add_evidence(
      AdaptiveResearchCivilizationState &state,
      std::string evidence_instance_id, std::string_view evidence_type_id,
      std::string provenance, double quality, double confidence,
      std::optional<std::string_view> context_id = std::nullopt) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  add_civilization_trait(AdaptiveResearchCivilizationState &state,
                         std::string_view trait_id) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  set_applicability_context_traits(
      AdaptiveResearchCivilizationState &state, std::string context_id,
      std::span<const std::string> trait_ids) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> add_capability(
      AdaptiveResearchCivilizationState &state, std::string_view capability_id,
      std::optional<std::string_view> context_id = std::nullopt) const;
  void add_facility_capability(AdaptiveResearchCivilizationState &state,
                               std::string_view capability_id) const;
  void remove_facility_capability(AdaptiveResearchCivilizationState &state,
                                  std::string_view capability_id) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent>
  review_basic_science_candidates(
      AdaptiveResearchCivilizationState &state,
      std::span<const std::string> bounded_candidate_node_ids,
      std::optional<std::string_view> target_context_id = std::nullopt) const;

  [[nodiscard]] AdaptiveResearchCommandResult start_directed_research(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double requested_assigned_labs, double readiness_score = 60.0,
      std::optional<std::string_view> target_context_id = std::nullopt) const;
  [[nodiscard]] AdaptiveResearchCommandResult pause_directed_research(
      AdaptiveResearchCivilizationState &state,
      std::string_view node_id) const;
  [[nodiscard]] AdaptiveResearchCommandResult resume_directed_research(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double requested_assigned_labs, double readiness_score) const;
  [[nodiscard]] AdaptiveResearchCommandResult reallocate_research_labs(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double requested_assigned_labs) const;
  [[nodiscard]] AdaptiveResearchCommandResult set_project_readiness(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      double readiness_score) const;
  [[nodiscard]] std::vector<AdaptiveResearchRuntimeEvent> advance_projects(
      AdaptiveResearchCivilizationState &state, double elapsed_years) const;
  [[nodiscard]] AdaptiveResearchCommandResult resolve_hypothesis(
      AdaptiveResearchCivilizationState &state, std::string_view node_id,
      bool supported) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

[[nodiscard]] AdaptiveResearchRuntime
load_adaptive_research_runtime(const std::filesystem::path &root_path);

} // namespace stellar::core
