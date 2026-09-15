#include <stellar/core/adaptive_research_eligibility.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>

namespace stellar::core {
namespace {

ResearchEligibilityResult failure(ResearchBlocker blocker) {
  ResearchEligibilityResult result;
  result.blockers.push_back(std::move(blocker));
  return result;
}

ResearchEligibilityResult finish(std::vector<ResearchBlocker> blockers) {
  return {.allowed = blockers.empty(), .blockers = std::move(blockers)};
}

bool has_required_capability(const AdaptiveResearchCatalog &catalog,
                             const AdaptiveResearchCivilizationState &state,
                             const std::string_view capability_id,
                             const std::optional<std::string_view> context_id) {
  const auto *definition = catalog.find_capability(capability_id);
  if (definition == nullptr) {
    return false;
  }
  switch (definition->scope) {
  case ResearchCapabilityScope::civilization:
    return state.has_capability(capability_id);
  case ResearchCapabilityScope::population_or_species:
  case ResearchCapabilityScope::colony_or_installation:
    return context_id.has_value() &&
           state.has_capability(capability_id, std::string(*context_id));
  }
  return false;
}

void add_facility_blockers(const AdaptiveResearchFacilityCatalog &facilities,
                           const AdaptiveResearchCivilizationState &state,
                           const std::string_view node_id,
                           const ResearchMaturity stage,
                           std::vector<ResearchBlocker> &blockers) {
  const auto *requirement = facilities.get_stage_requirement(node_id, stage);
  if (requirement == nullptr) {
    return;
  }

  for (const auto &capability_id : requirement->all_of) {
    if (!state.has_facility_capability(capability_id)) {
      blockers.push_back({
          .code = ResearchBlockerCode::missing_facility_capability,
          .subject_id = capability_id,
          .message = "A required specialist research-facility capability is "
                     "unavailable.",
      });
    }
  }

  if (!requirement->any_of.empty() &&
      std::ranges::none_of(requirement->any_of, [&](const auto &capability_id) {
        return state.has_facility_capability(capability_id);
      })) {
    blockers.push_back({
        .code = ResearchBlockerCode::missing_alternative_facility_capability,
        .message = "At least one acceptable specialist research-facility "
                   "capability is required.",
    });
  }
}

} // namespace

AdaptiveResearchEligibilityEvaluator::AdaptiveResearchEligibilityEvaluator(
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchApplicabilityCatalog &applicability,
    const AdaptiveResearchFacilityCatalog &facilities) noexcept
    : catalog_(&catalog), applicability_(&applicability),
      facilities_(&facilities) {}

ResearchEligibilityResult
AdaptiveResearchEligibilityEvaluator::evaluate_scientific_eligibility(
    const AdaptiveResearchCivilizationState &state,
    const std::string_view node_id,
    const std::optional<std::string_view> target_context_id) const {
  const auto *node = catalog_->find_node(node_id);
  if (node == nullptr) {
    return failure({.code = ResearchBlockerCode::unknown_node,
                    .message = "Unknown research possibility."});
  }
  if (!node->public_normal_research) {
    return failure({
        .code = ResearchBlockerCode::non_public_research,
        .message = "This possibility is not part of normal public research.",
    });
  }

  std::vector<ResearchBlocker> blockers;
  for (const auto &prerequisite_id : node->prerequisites.all_of) {
    if (!state.has_established_knowledge(prerequisite_id)) {
      blockers.push_back({
          .code = ResearchBlockerCode::missing_prerequisite,
          .subject_id = prerequisite_id,
          .message = "Additional prerequisite knowledge is required.",
      });
    }
  }
  if (!node->prerequisites.any_of.empty() &&
      std::ranges::none_of(node->prerequisites.any_of, [&](const auto &id) {
        return state.has_established_knowledge(id);
      })) {
    blockers.push_back({
        .code = ResearchBlockerCode::missing_alternative_prerequisite,
        .message =
            "At least one alternative prerequisite knowledge path is required.",
    });
  }

  for (const auto &trait_id : node->applicability.traits) {
    const auto &trait = applicability_->get_trait(trait_id);
    bool has_trait = false;
    switch (trait.scope) {
    case ResearchApplicabilityTraitScope::civilization:
      has_trait = state.has_civilization_trait(trait_id);
      break;
    case ResearchApplicabilityTraitScope::population_or_species:
      has_trait = target_context_id.has_value() &&
                  state.has_applicability_trait(*target_context_id, trait_id);
      break;
    }
    if (!has_trait) {
      if (trait.scope ==
              ResearchApplicabilityTraitScope::population_or_species &&
          !target_context_id.has_value()) {
        blockers.push_back({
            .code = ResearchBlockerCode::missing_applicability_context,
            .subject_id = trait_id,
            .message =
                "A target population/species research context is required.",
        });
      } else {
        blockers.push_back({
            .code = ResearchBlockerCode::missing_applicability_trait,
            .subject_id = trait_id,
            .message = "The target research context is not compatible with "
                       "this possibility.",
        });
      }
    }
  }

  std::unordered_set<std::string_view> seen_evidence;
  const auto check_evidence = [&](const std::string &evidence_id) {
    if (!seen_evidence.insert(evidence_id).second) {
      return;
    }
    const auto context = target_context_id.has_value()
                             ? std::optional(std::string(*target_context_id))
                             : std::nullopt;
    if (!state.has_evidence_type(evidence_id, context)) {
      blockers.push_back({
          .code = ResearchBlockerCode::missing_evidence,
          .subject_id = evidence_id,
          .message = "Required scientific evidence is not currently available.",
      });
    }
  };
  for (const auto &evidence_id : node->applicability.evidence_types) {
    check_evidence(evidence_id);
  }
  for (const auto &evidence_id : node->project_requirements.required_evidence) {
    check_evidence(evidence_id);
  }

  for (const auto &requirement : node->project_requirements.required_pressure) {
    const auto actual = state.get_pressure(requirement.id);
    if (actual + 0.000001 < requirement.value) {
      blockers.push_back({
          .code = ResearchBlockerCode::missing_pressure,
          .subject_id = requirement.id,
          .required_value = requirement.value,
          .actual_value = actual,
          .message = "The recognized research need/evidence pressure is below "
                     "the required level.",
      });
    }
  }
  if (!node->project_requirements.required_pressure_any.empty() &&
      std::ranges::none_of(node->project_requirements.required_pressure_any,
                           [&](const auto &requirement) {
                             return state.get_pressure(requirement.id) +
                                        0.000001 >=
                                    requirement.value;
                           })) {
    blockers.push_back({
        .code = ResearchBlockerCode::missing_alternative_pressure,
        .message = "None of the recognized conditions that would justify this "
                   "program are strong enough yet.",
    });
  }

  for (const auto &capability_id : node->capability_requirements.all_of) {
    if (!has_required_capability(*catalog_, state, capability_id,
                                 target_context_id)) {
      blockers.push_back({
          .code = ResearchBlockerCode::missing_capability,
          .subject_id = capability_id,
          .message = "A required functional capability is missing.",
      });
    }
  }
  if (!node->capability_requirements.any_of.empty() &&
      std::ranges::none_of(
          node->capability_requirements.any_of, [&](const auto &capability_id) {
            return has_required_capability(*catalog_, state, capability_id,
                                           target_context_id);
          })) {
    blockers.push_back({
        .code = ResearchBlockerCode::missing_alternative_capability,
        .message =
            "At least one alternative functional capability is required.",
    });
  }
  return finish(std::move(blockers));
}

ResearchEligibilityResult
AdaptiveResearchEligibilityEvaluator::evaluate_project_start(
    const AdaptiveResearchCivilizationState &state,
    const std::string_view node_id, const double requested_assigned_labs,
    const std::optional<std::string_view> target_context_id) const {
  const auto *node = catalog_->find_node(node_id);
  if (node == nullptr) {
    return failure({.code = ResearchBlockerCode::unknown_node,
                    .message = "Unknown research possibility."});
  }

  auto blockers =
      evaluate_scientific_eligibility(state, node_id, target_context_id)
          .blockers;
  if (const auto *node_state = state.try_get_node_state(node_id);
      node_state != nullptr) {
    if (node_state->maturity == ResearchMaturity::mature ||
        node_state->counts_as_established_knowledge()) {
      blockers.push_back({
          .code = ResearchBlockerCode::already_mature,
          .subject_id = std::string(node_id),
          .message = "This knowledge is already mature/established.",
      });
    } else if (node_state->maturity < ResearchMaturity::investigable) {
      blockers.push_back({
          .code = ResearchBlockerCode::node_not_investigable,
          .subject_id = std::string(node_id),
          .message = "The possibility is recognized but not yet Investigable.",
      });
    }
  } else {
    blockers.push_back({
        .code = ResearchBlockerCode::node_not_investigable,
        .message =
            "The possibility has not entered the visible research horizon.",
    });
  }

  if (std::ranges::any_of(state.active_projects(), [&](const auto &project) {
        return project.node_id == node_id;
      })) {
    blockers.push_back({
        .code = ResearchBlockerCode::already_active,
        .subject_id = std::string(node_id),
        .message = "This research project is already active or paused.",
    });
  }

  const auto &program_stage =
      catalog_->get_directed_program_stage(state.directed_program_stage_id());
  const auto active_directed_projects =
      std::ranges::count_if(state.active_projects(), [](const auto &project) {
        return !project.paused;
      });
  if (program_stage.directed_program_limit.has_value() &&
      active_directed_projects >= *program_stage.directed_program_limit) {
    blockers.push_back({
        .code = ResearchBlockerCode::directed_program_capacity,
        .subject_id = program_stage.id,
        .required_value = *program_stage.directed_program_limit,
        .actual_value = static_cast<double>(active_directed_projects),
        .message =
            "The civilization has no free directed research-program capacity.",
    });
  }

  if (requested_assigned_labs + 0.000001 <
      node->project_requirements.minimum_labs) {
    blockers.push_back({
        .code = ResearchBlockerCode::below_minimum_assigned_labs,
        .subject_id = std::string(node_id),
        .required_value = node->project_requirements.minimum_labs,
        .actual_value = requested_assigned_labs,
        .message = "The project needs more assigned Effective Research Labs "
                   "before it can begin.",
    });
  }
  if (state.free_effective_labs() + 0.000001 < requested_assigned_labs) {
    blockers.push_back({
        .code = ResearchBlockerCode::insufficient_free_labs,
        .subject_id = std::string(node_id),
        .required_value = requested_assigned_labs,
        .actual_value = state.free_effective_labs(),
        .message =
            "Not enough unassigned Effective Research Labs are available.",
    });
  }
  add_facility_blockers(*facilities_, state, node_id,
                        ResearchMaturity::experimental, blockers);
  return finish(std::move(blockers));
}

ResearchEligibilityResult
AdaptiveResearchEligibilityEvaluator::evaluate_stage_facility_eligibility(
    const AdaptiveResearchCivilizationState &state,
    const std::string_view node_id, const ResearchMaturity stage) const {
  std::vector<ResearchBlocker> blockers;
  add_facility_blockers(*facilities_, state, node_id, stage, blockers);
  return finish(std::move(blockers));
}

} // namespace stellar::core
