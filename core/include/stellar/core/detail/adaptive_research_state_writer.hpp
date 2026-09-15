#pragma once

#include <stellar/core/adaptive_research_state.hpp>

#include <span>
#include <string>

namespace stellar::core::detail {

std::int64_t checked_next_research_state_revision(std::int64_t current);

// Internal mutation seam for the future Adaptive Research runtime and codec.
// Presentation and ordinary query consumers use only the const state API.
class AdaptiveResearchStateWriter {
public:
  static AdaptiveResearchExpertiseState &
  expertise(AdaptiveResearchCivilizationState &state) noexcept;
  static void
  set_total_effective_research_labs(AdaptiveResearchCivilizationState &state,
                                    double value);
  static bool set_pressure(AdaptiveResearchCivilizationState &state,
                           std::string pressure_id, double value);
  static bool add_evidence(AdaptiveResearchCivilizationState &state,
                           ResearchEvidenceInstance evidence);
  static bool remove_evidence(AdaptiveResearchCivilizationState &state,
                              std::string_view evidence_instance_id);
  static bool add_civilization_trait(AdaptiveResearchCivilizationState &state,
                                     std::string trait_id);
  static bool
  remove_civilization_trait(AdaptiveResearchCivilizationState &state,
                            std::string_view trait_id);
  static bool
  set_applicability_context_traits(AdaptiveResearchCivilizationState &state,
                                   std::string context_id,
                                   std::span<const std::string> trait_ids);
  static bool add_applicability_trait(AdaptiveResearchCivilizationState &state,
                                      std::string context_id,
                                      std::string trait_id);
  static bool
  remove_applicability_trait(AdaptiveResearchCivilizationState &state,
                             std::string_view context_id,
                             std::string_view trait_id);
  static bool add_capability(AdaptiveResearchCivilizationState &state,
                             ResearchCapabilityKey capability);
  static bool remove_capability(AdaptiveResearchCivilizationState &state,
                                const ResearchCapabilityKey &capability);
  static bool add_facility_capability(AdaptiveResearchCivilizationState &state,
                                      std::string capability_id);
  static bool
  remove_facility_capability(AdaptiveResearchCivilizationState &state,
                             std::string_view capability_id);
  static void
  set_facility_capabilities(AdaptiveResearchCivilizationState &state,
                            std::span<const std::string> desired_capabilities);
  static bool
  add_enabled_deployment_event(AdaptiveResearchCivilizationState &state,
                               std::string event_id);
  static void
  set_directed_program_stage(AdaptiveResearchCivilizationState &state,
                             std::string stage_id);
  static void set_node_state(AdaptiveResearchCivilizationState &state,
                             ResearchNodeRuntimeState node);
  static bool remove_node_state(AdaptiveResearchCivilizationState &state,
                                std::string_view node_id);
  static void set_project(AdaptiveResearchCivilizationState &state,
                          ResearchProjectRuntimeState project);
  static bool remove_project(AdaptiveResearchCivilizationState &state,
                             std::string_view node_id);
  static void mark_view_dirty(AdaptiveResearchCivilizationState &state);
};

} // namespace stellar::core::detail
