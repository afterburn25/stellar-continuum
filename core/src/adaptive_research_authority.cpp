#include <stellar/core/adaptive_research_authority.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {
constexpr double epsilon_years = 0.0000001;

double dotnet_max(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}

std::optional<std::string> own(std::optional<std::string_view> value) {
  return value ? std::optional<std::string>(*value) : std::nullopt;
}
std::optional<std::string_view> view(const std::optional<std::string> &value) {
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}
} // namespace

struct AdaptiveResearchAuthority::Storage {
  AdaptiveResearchRuntime kernel;
  AdaptiveResearchExpertiseCatalog expertise_catalog;
  AdaptiveResearchReadinessCalculator readiness;
  AdaptiveResearchExpertiseService expertise;
  AdaptiveResearchStartingProfileComposer starting_profiles;

  explicit Storage(const std::filesystem::path &root_path)
      : kernel(load_adaptive_research_runtime(root_path)),
        expertise_catalog(load_adaptive_research_expertise_catalog(
            root_path, kernel.catalog(), kernel.facilities())),
        readiness(kernel.catalog(), kernel.facilities(), expertise_catalog,
                  kernel.progress_policy()),
        expertise(kernel.catalog(), expertise_catalog, readiness),
        starting_profiles(kernel, root_path) {}

  void recalculate_project(AdaptiveResearchCivilizationState &state,
                           std::string_view node_id) const {
    const auto projects = state.active_projects();
    const auto project_pointer =
        std::ranges::find_if(projects, [&](const auto &project) {
          return project.node_id == node_id;
        });
    if (project_pointer == projects.end())
      return;
    const auto project = *project_pointer;
    const auto breakdown = expertise.calculate_project_readiness(
        state, project.node_id, project.stage, project.assigned_effective_labs,
        view(project.target_applicability_context_id));
    if (std::abs(project.readiness_efficiency - breakdown.rp_efficiency) <
        0.000001)
      return;
    auto updated = project;
    updated.readiness_efficiency = breakdown.rp_efficiency;
    detail::AdaptiveResearchStateWriter::set_project(state, std::move(updated));
  }

  void recalculate_all(AdaptiveResearchCivilizationState &state) const {
    std::vector<std::string> node_ids;
    node_ids.reserve(state.active_projects().size());
    for (const auto &project : state.active_projects())
      node_ids.push_back(project.node_id);
    for (const auto &node_id : node_ids)
      recalculate_project(state, node_id);
  }

  static void pause_for_capacity(AdaptiveResearchCivilizationState &state) {
    if (state.assigned_effective_labs() <=
        state.total_effective_research_labs() + 0.000001)
      return;
    const std::vector<ResearchProjectRuntimeState> projects(
        state.active_projects().begin(), state.active_projects().end());
    for (auto project : projects) {
      if (project.paused)
        continue;
      project.paused = true;
      project.pause_reason = "research_capacity_reallocation_required";
      detail::AdaptiveResearchStateWriter::set_project(state,
                                                       std::move(project));
    }
  }
};

AdaptiveResearchAuthority::AdaptiveResearchAuthority(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchAuthority::~AdaptiveResearchAuthority() = default;
AdaptiveResearchAuthority::AdaptiveResearchAuthority(
    AdaptiveResearchAuthority &&) noexcept = default;
AdaptiveResearchAuthority &AdaptiveResearchAuthority::operator=(
    AdaptiveResearchAuthority &&) noexcept = default;

const AdaptiveResearchRuntime &
AdaptiveResearchAuthority::kernel() const noexcept {
  return storage_->kernel;
}
const AdaptiveResearchExpertiseCatalog &
AdaptiveResearchAuthority::expertise_catalog() const noexcept {
  return storage_->expertise_catalog;
}
const AdaptiveResearchReadinessCalculator &
AdaptiveResearchAuthority::readiness() const noexcept {
  return storage_->readiness;
}
const AdaptiveResearchExpertiseService &
AdaptiveResearchAuthority::expertise_service() const noexcept {
  return storage_->expertise;
}
const AdaptiveResearchStartingProfileComposer &
AdaptiveResearchAuthority::starting_profiles() const noexcept {
  return storage_->starting_profiles;
}
const AdaptiveResearchCatalog &
AdaptiveResearchAuthority::catalog() const noexcept {
  return storage_->kernel.catalog();
}
const AdaptiveResearchApplicabilityCatalog &
AdaptiveResearchAuthority::applicability() const noexcept {
  return storage_->kernel.applicability();
}
const AdaptiveResearchFacilityCatalog &
AdaptiveResearchAuthority::facilities() const noexcept {
  return storage_->kernel.facilities();
}
const AdaptiveResearchProgressPolicy &
AdaptiveResearchAuthority::progress_policy() const noexcept {
  return storage_->kernel.progress_policy();
}

AdaptiveResearchCivilizationState
AdaptiveResearchAuthority::create_civilization_state(
    std::string civilization_id) const {
  return storage_->kernel.create_civilization_state(std::move(civilization_id));
}

AdaptiveResearchStartingCompositionResult
AdaptiveResearchAuthority::compose_reference_profile(
    std::string civilization_id, std::string reference_profile_id,
    std::string primary_context_id, double activity_year) const {
  const auto owned_profile_id = reference_profile_id;
  auto result = storage_->starting_profiles.compose_reference_profile(
      std::move(civilization_id), std::move(reference_profile_id),
      std::move(primary_context_id));
  for (const auto &[field_id, competence] : result.deferred.field_competence) {
    storage_->expertise.seed_field_competence(result.state, field_id,
                                              {competence.theoretical,
                                               competence.experimental,
                                               competence.engineering},
                                              activity_year);
  }
  int sequence = 0;
  for (const auto &seed : result.deferred.research_institutions) {
    storage_->expertise.set_institution(
        result.state,
        "start:" + owned_profile_id + ":" + seed.institution_archetype_id +
            ":" + std::to_string(sequence++),
        seed.institution_archetype_id, seed.count, seed.count);
  }
  storage_->recalculate_all(result.state);
  return result;
}

ResearchReadinessBreakdown AdaptiveResearchAuthority::get_project_readiness(
    const AdaptiveResearchCivilizationState &state, std::string_view node_id,
    ResearchMaturity stage, double assigned_effective_labs,
    std::optional<std::string_view> target_context_id) const {
  return storage_->expertise.calculate_project_readiness(
      state, node_id, stage, assigned_effective_labs, target_context_id);
}

AdaptiveResearchCommandResult
AdaptiveResearchAuthority::start_directed_research(
    AdaptiveResearchCivilizationState &state, std::string_view node_id_view,
    double requested_assigned_labs,
    std::optional<std::string_view> target_context_view) const {
  const std::string node_id(node_id_view);
  const auto target_context = own(target_context_view);
  const auto breakdown =
      get_project_readiness(state, node_id, ResearchMaturity::experimental,
                            requested_assigned_labs, view(target_context));
  return storage_->kernel.start_directed_research(
      state, node_id, requested_assigned_labs,
      breakdown.overall_readiness_score, view(target_context));
}

AdaptiveResearchCommandResult
AdaptiveResearchAuthority::pause_directed_research(
    AdaptiveResearchCivilizationState &state,
    std::string_view node_id_view) const {
  const std::string node_id(node_id_view);
  return storage_->kernel.pause_directed_research(state, node_id);
}

AdaptiveResearchCommandResult
AdaptiveResearchAuthority::resume_directed_research(
    AdaptiveResearchCivilizationState &state, std::string_view node_id_view,
    double requested_assigned_labs) const {
  const std::string node_id(node_id_view);
  const auto found =
      std::ranges::find_if(state.active_projects(), [&](const auto &project) {
        return project.node_id == node_id;
      });
  if (found == state.active_projects().end())
    return AdaptiveResearchCommandResult::rejected(
        "The project is not currently paused.");
  const auto project = *found;
  const auto breakdown = get_project_readiness(
      state, node_id, project.stage, requested_assigned_labs,
      view(project.target_applicability_context_id));
  return storage_->kernel.resume_directed_research(
      state, node_id, requested_assigned_labs,
      breakdown.overall_readiness_score);
}

AdaptiveResearchCommandResult
AdaptiveResearchAuthority::reallocate_research_labs(
    AdaptiveResearchCivilizationState &state, std::string_view node_id_view,
    double requested_assigned_labs) const {
  const std::string node_id(node_id_view);
  auto result = storage_->kernel.reallocate_research_labs(
      state, node_id, requested_assigned_labs);
  if (result.accepted)
    storage_->recalculate_project(state, node_id);
  return result;
}

AdaptiveResearchCommandResult AdaptiveResearchAuthority::resolve_hypothesis(
    AdaptiveResearchCivilizationState &state, std::string_view node_id_view,
    bool supported) const {
  const std::string node_id(node_id_view);
  auto result = storage_->kernel.resolve_hypothesis(state, node_id, supported);
  if (result.accepted && supported &&
      std::ranges::any_of(state.active_projects(), [&](const auto &project) {
        return project.node_id == node_id;
      }))
    storage_->recalculate_project(state, node_id);
  return result;
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::advance_projects(
    AdaptiveResearchCivilizationState &state, double elapsed_years,
    double current_year) const {
  if (elapsed_years < 0.0 || !std::isfinite(elapsed_years))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'elapsedYears')");
  if (elapsed_years <= 0.0)
    return {};

  std::vector<AdaptiveResearchRuntimeEvent> events;
  double remaining = elapsed_years;
  double cursor_year = current_year - elapsed_years;
  int guard = 0;
  while (remaining > epsilon_years && guard++ < 4096) {
    storage_->recalculate_all(state);
    std::vector<ResearchProjectRuntimeState> active;
    for (const auto &project : state.active_projects())
      if (!project.paused)
        active.push_back(project);
    if (active.empty())
      break;

    double step = remaining;
    for (const auto &project : active) {
      const auto &node = catalog().get_node(project.node_id);
      const double remaining_rp = dotnet_max(
          0.0, progress_policy().get_stage_work(node, project.stage) -
                   project.stage_research_points);
      const double scaled_labs = catalog().lab_scaling().scale_assigned_labs(
          project.assigned_effective_labs,
          node.project_requirements.recommended_labs);
      const double rp_per_year =
          scaled_labs *
          catalog().metadata().base_rp_per_effective_lab_per_year *
          project.readiness_efficiency;
      if (rp_per_year <= 0.0)
        continue;
      const double years_to_boundary = remaining_rp / rp_per_year;
      if (years_to_boundary < step)
        step = dotnet_max(years_to_boundary, epsilon_years);
    }

    std::vector<std::pair<std::string, ResearchMaturity>> pre_stages;
    pre_stages.reserve(state.active_projects().size());
    for (const auto &project : state.active_projects())
      pre_stages.emplace_back(project.node_id, project.stage);
    auto segment_events = storage_->kernel.advance_projects(state, step);
    events.insert(events.end(), segment_events.begin(), segment_events.end());
    cursor_year += step;
    remaining -= step;

    bool practice_changed = false;
    for (const auto &[node_id, previous_stage] : pre_stages) {
      const auto project =
          std::ranges::find_if(state.active_projects(), [&](const auto &value) {
            return value.node_id == node_id;
          });
      bool completed = false;
      if (project == state.active_projects().end()) {
        const auto *node_state = state.try_get_node_state(node_id);
        completed = node_state &&
                    node_state->maturity == ResearchMaturity::mature &&
                    previous_stage == ResearchMaturity::engineering;
      } else if (project->stage != previous_stage) {
        completed = true;
      } else if (project->paused &&
                 previous_stage == ResearchMaturity::experimental &&
                 project->pause_reason ==
                     std::optional<std::string>(
                         "hypothesis_resolution_required")) {
        completed = true;
      }
      if (!completed)
        continue;
      storage_->expertise.apply_completed_stage_practice(
          state, node_id, previous_stage, cursor_year);
      practice_changed = true;
    }
    if (practice_changed || !segment_events.empty())
      storage_->recalculate_all(state);
    if (step <= epsilon_years && segment_events.empty())
      break;
  }
  if (guard >= 4096)
    throw std::runtime_error(
        "Adaptive Research authority exceeded stage-boundary advancement "
        "guard.");
  return events;
}

void AdaptiveResearchAuthority::set_research_institution(
    AdaptiveResearchCivilizationState &state,
    std::string_view institution_instance_id_view,
    std::string_view institution_archetype_id_view, int total_count,
    int active_count, std::optional<std::string_view> context_id_view) const {
  const std::string institution_instance_id(institution_instance_id_view);
  const std::string institution_archetype_id(institution_archetype_id_view);
  const auto context_id = own(context_id_view);
  storage_->expertise.set_institution(state, institution_instance_id,
                                      institution_archetype_id, total_count,
                                      active_count, view(context_id));
  Storage::pause_for_capacity(state);
  storage_->recalculate_all(state);
}

void AdaptiveResearchAuthority::set_tacit_asset(
    AdaptiveResearchCivilizationState &state, std::string_view asset_id_view,
    std::string_view asset_type_id_view, ResearchTacitScopeKind scope_kind,
    std::string_view scope_ref_view,
    ResearchTacitAssimilationStage assimilation_stage, double depth,
    double availability, double translation_context_quality,
    double training_continuity, std::string_view provenance_view,
    std::optional<std::string_view> context_id_view) const {
  const std::string asset_id(asset_id_view);
  const std::string asset_type_id(asset_type_id_view);
  const std::string scope_ref(scope_ref_view);
  const std::string provenance(provenance_view);
  const auto context_id = own(context_id_view);
  storage_->expertise.set_tacit_asset(
      state, asset_id, asset_type_id, scope_kind, scope_ref, assimilation_stage,
      depth, availability, translation_context_quality, training_continuity,
      provenance, view(context_id));
  storage_->recalculate_all(state);
}

void AdaptiveResearchAuthority::apply_competence_atrophy(
    AdaptiveResearchCivilizationState &state, double current_year,
    double elapsed_years) const {
  storage_->expertise.apply_competence_atrophy(state, current_year,
                                               elapsed_years);
  storage_->recalculate_all(state);
}

AdaptiveResearchView AdaptiveResearchAuthority::build_view(
    const AdaptiveResearchCivilizationState &state) const {
  return storage_->kernel.build_view(state);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::set_pressure(
    AdaptiveResearchCivilizationState &state, std::string_view pressure_id_view,
    double value, std::optional<std::string_view> context_id_view) const {
  const std::string pressure_id(pressure_id_view);
  const auto context_id = own(context_id_view);
  return storage_->kernel.set_pressure(state, pressure_id, value,
                                       view(context_id));
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::add_evidence(
    AdaptiveResearchCivilizationState &state,
    std::string_view evidence_instance_id_view,
    std::string_view evidence_type_id_view, std::string_view provenance_view,
    double quality, double confidence,
    std::optional<std::string_view> context_id_view) const {
  const std::string evidence_instance_id(evidence_instance_id_view);
  const std::string evidence_type_id(evidence_type_id_view);
  const std::string provenance(provenance_view);
  const auto context_id = own(context_id_view);
  auto events = storage_->kernel.add_evidence(
      state, evidence_instance_id, evidence_type_id, provenance, quality,
      confidence, view(context_id));
  storage_->recalculate_all(state);
  return events;
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::add_civilization_trait(
    AdaptiveResearchCivilizationState &state,
    std::string_view trait_id_view) const {
  const std::string trait_id(trait_id_view);
  return storage_->kernel.add_civilization_trait(state, trait_id);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::set_applicability_context_traits(
    AdaptiveResearchCivilizationState &state, std::string_view context_id_view,
    std::span<const std::string> trait_ids_view) const {
  const std::string context_id(context_id_view);
  const std::vector<std::string> trait_ids(trait_ids_view.begin(),
                                           trait_ids_view.end());
  return storage_->kernel.set_applicability_context_traits(state, context_id,
                                                           trait_ids);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::add_capability(
    AdaptiveResearchCivilizationState &state,
    std::string_view capability_id_view,
    std::optional<std::string_view> context_id_view) const {
  const std::string capability_id(capability_id_view);
  const auto context_id = own(context_id_view);
  return storage_->kernel.add_capability(state, capability_id,
                                         view(context_id));
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchAuthority::review_basic_science_candidates(
    AdaptiveResearchCivilizationState &state,
    std::span<const std::string> candidate_ids_view,
    std::optional<std::string_view> context_id_view) const {
  const std::vector<std::string> candidate_ids(candidate_ids_view.begin(),
                                               candidate_ids_view.end());
  const auto context_id = own(context_id_view);
  return storage_->kernel.review_basic_science_candidates(state, candidate_ids,
                                                          view(context_id));
}

AdaptiveResearchAuthority
load_adaptive_research_authority(const std::filesystem::path &root_path) {
  return AdaptiveResearchAuthority(
      std::make_unique<AdaptiveResearchAuthority::Storage>(root_path));
}

} // namespace stellar::core
