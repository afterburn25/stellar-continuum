#include <stellar/core/adaptive_research_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point{};
    std::size_t width{};
    if (first <= 0x7f) {
      code_point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      code_point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      code_point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code_point = first & 0x07;
      width = 4;
    } else {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    if (value.size() < width) {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80) {
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      }
      code_point = (code_point << 6) | (byte & 0x3f);
    }
    if ((width == 3 && (code_point < 0x800 ||
                        (code_point >= 0xd800 && code_point <= 0xdfff))) ||
        (width == 4 && (code_point < 0x10000 || code_point > 0x10ffff))) {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}

bool ordinal_less(const std::string_view left, const std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}

std::vector<ResearchBlocker>
sanitize_blockers(std::vector<ResearchBlocker> blockers,
                  const std::unordered_set<std::string_view> &visible_ids) {
  for (auto &blocker : blockers) {
    if (blocker.code == ResearchBlockerCode::missing_prerequisite &&
        blocker.subject_id.has_value() &&
        !visible_ids.contains(*blocker.subject_id)) {
      blocker.subject_id.reset();
      blocker.message = "Additional prerequisite knowledge is required.";
    }
  }
  return blockers;
}

const ResearchProjectRuntimeState *find_project(
    const AdaptiveResearchCivilizationState &state,
    const std::string_view node_id) noexcept {
  const auto projects = state.active_projects();
  const auto found =
      std::ranges::find(projects, node_id, &ResearchProjectRuntimeState::node_id);
  return found == projects.end() ? nullptr : &*found;
}

std::vector<ResearchBlocker> current_project_blockers(
    const AdaptiveResearchEligibilityEvaluator &eligibility,
    const AdaptiveResearchCivilizationState &state,
    const ResearchProjectRuntimeState &project,
    const std::unordered_set<std::string_view> &visible_ids) {
  const auto context = project.target_applicability_context_id.has_value()
                           ? std::optional<std::string_view>(
                                 *project.target_applicability_context_id)
                           : std::nullopt;
  auto blockers = eligibility
                      .evaluate_scientific_eligibility(state, project.node_id,
                                                       context)
                      .blockers;
  auto facility = eligibility
                      .evaluate_stage_facility_eligibility(
                          state, project.node_id, project.stage)
                      .blockers;
  blockers.insert(blockers.end(), std::make_move_iterator(facility.begin()),
                  std::make_move_iterator(facility.end()));
  return sanitize_blockers(std::move(blockers), visible_ids);
}

std::string readiness_band(const double efficiency) {
  if (efficiency <= 0.35 + 0.000001) {
    return "poor";
  }
  if (efficiency <= 0.55 + 0.000001) {
    return "limited";
  }
  if (efficiency <= 0.75 + 0.000001) {
    return "adequate";
  }
  if (efficiency <= 1.00 + 0.000001) {
    return "strong";
  }
  return "exceptional";
}

double progress_fraction(const double value, const double total) {
  if (total <= 0.0) {
    return 0.0;
  }
  const auto fraction = value / total;
  if (fraction < 0.0) {
    return 0.0;
  }
  if (fraction > 1.0) {
    return 1.0;
  }
  return fraction;
}

} // namespace

AdaptiveResearchViewBuilder::AdaptiveResearchViewBuilder(
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchEligibilityEvaluator &eligibility,
    const AdaptiveResearchProgressPolicy &progress_policy) noexcept
    : catalog_(&catalog), eligibility_(&eligibility),
      progress_policy_(&progress_policy) {}

AdaptiveResearchView AdaptiveResearchViewBuilder::build(
    const AdaptiveResearchCivilizationState &state,
    const std::optional<std::string_view> default_context) const {
  std::unordered_set<std::string_view> visible_ids;
  for (const auto &node : state.node_states()) {
    visible_ids.insert(node.node_id);
  }

  struct VisibleState {
    const ResearchNodeRuntimeState *state;
    const AdaptiveResearchNodeDefinition *definition;
  };
  std::vector<VisibleState> sorted_states;
  for (const auto &node : state.node_states()) {
    sorted_states.push_back({&node, &catalog_->get_node(node.node_id)});
  }
  std::ranges::sort(sorted_states, [&](const auto &left, const auto &right) {
    const auto &left_definition = *left.definition;
    const auto &right_definition = *right.definition;
    if (left_definition.graph_depth != right_definition.graph_depth) {
      return left_definition.graph_depth < right_definition.graph_depth;
    }
    if (left_definition.domain_id != right_definition.domain_id) {
      return ordinal_less(left_definition.domain_id, right_definition.domain_id);
    }
    return ordinal_less(left.state->node_id, right.state->node_id);
  });

  std::vector<AdaptiveResearchNodeView> nodes;
  nodes.reserve(sorted_states.size());
  for (const auto &visible_state : sorted_states) {
    const auto *node_state = visible_state.state;
    const auto &definition = *visible_state.definition;
    const auto *active_project = find_project(state, node_state->node_id);
    const auto target_context =
        active_project != nullptr &&
                active_project->target_applicability_context_id.has_value()
            ? std::optional<std::string_view>(
                  *active_project->target_applicability_context_id)
            : default_context;
    std::vector<ResearchBlocker> blockers;
    if (active_project != nullptr) {
      blockers = current_project_blockers(*eligibility_, state, *active_project,
                                          visible_ids);
    } else if (node_state->maturity >= ResearchMaturity::investigable &&
               node_state->maturity < ResearchMaturity::mature) {
      blockers = sanitize_blockers(
          eligibility_
              ->evaluate_project_start(state, node_state->node_id,
                                       definition.project_requirements.minimum_labs,
                                       target_context)
              .blockers,
          visible_ids);
    } else if (node_state->maturity < ResearchMaturity::investigable) {
      blockers = sanitize_blockers(
          eligibility_
              ->evaluate_scientific_eligibility(state, node_state->node_id,
                                                target_context)
              .blockers,
          visible_ids);
    }
    nodes.push_back({
        .node_id = definition.id,
        .display_name = definition.name,
        .domain_id = definition.domain_id,
        .solution_family = definition.solution_family,
        .state = node_state->maturity,
        .is_hypothesis = definition.is_hypothesis,
        .known_capabilities =
            node_state->maturity >= ResearchMaturity::demonstrated
                ? definition.declared_capabilities
                : std::vector<std::string>{},
        .blockers = std::move(blockers),
        .minimum_labs =
            node_state->maturity >= ResearchMaturity::investigable
                ? std::optional(definition.project_requirements.minimum_labs)
                : std::nullopt,
        .recommended_labs =
            node_state->maturity >= ResearchMaturity::investigable
                ? std::optional(definition.project_requirements.recommended_labs)
                : std::nullopt,
        .assigned_labs = active_project == nullptr
                             ? std::nullopt
                             : std::optional(active_project->assigned_effective_labs),
        .target_applicability_context_id =
            target_context.has_value()
                ? std::optional(std::string(*target_context))
                : std::nullopt,
    });
  }

  std::vector<AdaptiveResearchEdgeView> edges;
  for (const auto target_id : visible_ids) {
    const auto &definition = catalog_->get_node(target_id);
    for (const auto &prerequisite : definition.prerequisites.all_of) {
      if (visible_ids.contains(prerequisite)) {
        edges.push_back({prerequisite, std::string(target_id),
                         "known_prerequisite"});
      }
    }
    for (const auto &prerequisite : definition.prerequisites.any_of) {
      if (visible_ids.contains(prerequisite)) {
        edges.push_back(
            {prerequisite, std::string(target_id), "known_alternative"});
      }
    }
  }
  std::ranges::stable_sort(edges, [&](const auto &left, const auto &right) {
    if (left.from_visible_node_id != right.from_visible_node_id) {
      return ordinal_less(left.from_visible_node_id,
                          right.from_visible_node_id);
    }
    return ordinal_less(left.to_visible_node_id, right.to_visible_node_id);
  });

  std::vector<const ResearchProjectRuntimeState *> sorted_projects;
  for (const auto &project : state.active_projects()) {
    sorted_projects.push_back(&project);
  }
  std::ranges::sort(sorted_projects, [&](const auto *left, const auto *right) {
    return ordinal_less(left->node_id, right->node_id);
  });
  std::vector<AdaptiveResearchProjectView> projects;
  projects.reserve(sorted_projects.size());
  for (const auto *project : sorted_projects) {
    const auto &definition = catalog_->get_node(project->node_id);
    const auto stage_work =
        progress_policy_->get_stage_work(definition, project->stage);
    projects.push_back({
        .node_id = project->node_id,
        .stage = project->stage,
        .stage_progress =
            progress_fraction(project->stage_research_points, stage_work),
        .total_progress = progress_fraction(
            project->total_research_points,
            definition.project_requirements.base_research_points),
        .assigned_effective_labs = project->assigned_effective_labs,
        .readiness_band = readiness_band(project->readiness_efficiency),
        .paused = project->paused,
        .pause_reason = project->pause_reason,
        .current_blockers = current_project_blockers(
            *eligibility_, state, *project, visible_ids),
        .target_applicability_context_id =
            project->target_applicability_context_id,
    });
  }

  std::unordered_set<std::string_view> relevant_pressures;
  std::unordered_map<std::string_view, std::unordered_set<std::string>>
      hard_targets;
  for (const auto node_id : visible_ids) {
    const auto &definition = catalog_->get_node(node_id);
    for (const auto &pressure_id : definition.pressure_affinities) {
      relevant_pressures.insert(pressure_id);
    }
    const auto add_hard_target = [&](const ResearchNumberRequirement &requirement) {
      relevant_pressures.insert(requirement.id);
      hard_targets[requirement.id].insert(std::string(node_id));
    };
    for (const auto &requirement :
         definition.project_requirements.required_pressure) {
      add_hard_target(requirement);
    }
    for (const auto &requirement :
         definition.project_requirements.required_pressure_any) {
      add_hard_target(requirement);
    }
  }
  std::vector<AdaptiveResearchPressureView> pressures;
  for (const auto pressure_id : relevant_pressures) {
    const auto *value = state.try_get_pressure(pressure_id);
    if (value == nullptr) {
      continue;
    }
    const auto &target_set = hard_targets[pressure_id];
    std::vector<std::string> targets(target_set.begin(), target_set.end());
    std::ranges::sort(targets, ordinal_less);
    pressures.push_back({std::string(pressure_id), *value, std::move(targets)});
  }
  std::ranges::sort(pressures, [&](const auto &left, const auto &right) {
    return ordinal_less(left.pressure_id, right.pressure_id);
  });

  const auto &directed =
      catalog_->get_directed_program_stage(state.directed_program_stage_id());
  const auto active_count = std::ranges::count_if(
      state.active_projects(), [](const auto &project) { return !project.paused; });
  DirectedResearchCapacityView capacity{
      .stage_id = directed.id,
      .maximum_directed_programs = directed.directed_program_limit,
      .lab_capacity_only = directed.lab_capacity_only,
      .active_program_count = static_cast<int>(active_count),
      .free_effective_labs = state.free_effective_labs(),
  };
  return {
      .revision = state.materialized_view_revision(),
      .civilization_id = state.civilization_id(),
      .directed_program_capacity = std::move(capacity),
      .visible_nodes = std::move(nodes),
      .visible_edges = std::move(edges),
      .active_projects = std::move(projects),
      .recognized_pressures = std::move(pressures),
  };
}

} // namespace stellar::core
