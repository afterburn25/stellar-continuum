#include <stellar/core/adaptive_research_runtime.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

using Writer = detail::AdaptiveResearchStateWriter;

std::optional<std::string>
own(std::optional<std::string_view> value) {
  if (!value)
    return std::nullopt;
  return std::string(*value);
}

const ResearchProjectRuntimeState *
find_project(const AdaptiveResearchCivilizationState &state,
             std::string_view node_id) noexcept {
  const auto projects = state.active_projects();
  const auto it = std::find_if(projects.begin(), projects.end(),
                               [node_id](const auto &project) {
                                 return project.node_id == node_id;
                               });
  return it == projects.end() ? nullptr : std::addressof(*it);
}

std::string maturity_name(ResearchMaturity maturity) {
  switch (maturity) {
  case ResearchMaturity::rumored:
    return "Rumored";
  case ResearchMaturity::hypothesized:
    return "Hypothesized";
  case ResearchMaturity::investigable:
    return "Investigable";
  case ResearchMaturity::experimental:
    return "Experimental";
  case ResearchMaturity::demonstrated:
    return "Demonstrated";
  case ResearchMaturity::engineering:
    return "Engineering";
  case ResearchMaturity::mature:
    return "Mature";
  case ResearchMaturity::archived:
    return "Archived";
  }
  return std::to_string(static_cast<int>(maturity));
}

double dotnet_max(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}

double dotnet_min(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}

struct CapabilityVisitKey {
  std::string id;
  std::optional<std::string> context;

  bool operator==(const CapabilityVisitKey &) const = default;
};

struct CapabilityVisitHash {
  std::size_t operator()(const CapabilityVisitKey &key) const noexcept {
    auto hash = std::hash<std::string>{}(key.id);
    if (key.context)
      hash ^= std::hash<std::string>{}(*key.context) + 0x9e3779b9U +
              (hash << 6U) + (hash >> 2U);
    return hash;
  }
};

AdaptiveResearchRuntimeEvent event(
    AdaptiveResearchRuntimeEventType type,
    const AdaptiveResearchCivilizationState &state,
    std::optional<std::string> node_id, std::optional<std::string> subject_id,
    std::string message) {
  return {type, state.civilization_id(), std::move(node_id),
          std::move(subject_id), std::move(message)};
}

} // namespace

AdaptiveResearchCommandResult
AdaptiveResearchCommandResult::rejected(std::string message,
                                        std::vector<ResearchBlocker> blockers) {
  return {false, std::move(message), {}, std::move(blockers)};
}

struct AdaptiveResearchRuntimeContent::Storage {
  AdaptiveResearchCatalog catalog;
  AdaptiveResearchApplicabilityCatalog applicability;
  AdaptiveResearchFacilityCatalog facilities;
  AdaptiveResearchProgressPolicy progress_policy;

  Storage(AdaptiveResearchCatalog &&catalog_value,
          AdaptiveResearchApplicabilityCatalog &&applicability_value,
          AdaptiveResearchFacilityCatalog &&facilities_value,
          AdaptiveResearchProgressPolicy &&progress_policy_value)
      : catalog(std::move(catalog_value)),
        applicability(std::move(applicability_value)),
        facilities(std::move(facilities_value)),
        progress_policy(std::move(progress_policy_value)) {}
};

AdaptiveResearchRuntimeContent::AdaptiveResearchRuntimeContent(
    AdaptiveResearchCatalog &&catalog,
    AdaptiveResearchApplicabilityCatalog &&applicability,
    AdaptiveResearchFacilityCatalog &&facilities,
    AdaptiveResearchProgressPolicy &&progress_policy)
    : storage_(std::make_unique<Storage>(
          std::move(catalog), std::move(applicability), std::move(facilities),
          std::move(progress_policy))) {}

AdaptiveResearchRuntimeContent::~AdaptiveResearchRuntimeContent() = default;
AdaptiveResearchRuntimeContent::AdaptiveResearchRuntimeContent(
    AdaptiveResearchRuntimeContent &&) noexcept = default;
AdaptiveResearchRuntimeContent &AdaptiveResearchRuntimeContent::operator=(
    AdaptiveResearchRuntimeContent &&) noexcept = default;
const AdaptiveResearchCatalog &
AdaptiveResearchRuntimeContent::catalog() const noexcept {
  return storage_->catalog;
}
const AdaptiveResearchApplicabilityCatalog &
AdaptiveResearchRuntimeContent::applicability() const noexcept {
  return storage_->applicability;
}
const AdaptiveResearchFacilityCatalog &
AdaptiveResearchRuntimeContent::facilities() const noexcept {
  return storage_->facilities;
}
const AdaptiveResearchProgressPolicy &
AdaptiveResearchRuntimeContent::progress_policy() const noexcept {
  return storage_->progress_policy;
}

AdaptiveResearchRuntimeContent load_adaptive_research_runtime_content(
    const std::filesystem::path &root_path) {
  auto catalog = load_adaptive_research_catalog(root_path);
  auto applicability =
      load_adaptive_research_applicability_catalog(root_path, catalog);
  auto facilities = load_adaptive_research_facility_catalog(root_path, catalog);
  auto policy = load_adaptive_research_progress_policy(root_path, catalog);
  return AdaptiveResearchRuntimeContent(std::move(catalog),
                                        std::move(applicability),
                                        std::move(facilities),
                                        std::move(policy));
}

struct AdaptiveResearchRuntime::Storage {
  std::shared_ptr<const AdaptiveResearchRuntimeContent> content;
  AdaptiveResearchEligibilityEvaluator eligibility;
  AdaptiveResearchViewBuilder view_builder;

  explicit Storage(
      std::shared_ptr<const AdaptiveResearchRuntimeContent> content_value)
      : content(std::move(content_value)),
        eligibility(content->catalog(), content->applicability(),
                    content->facilities()),
        view_builder(content->catalog(), eligibility,
                     content->progress_policy()) {
  }

  std::vector<AdaptiveResearchRuntimeEvent> materialize(
      AdaptiveResearchCivilizationState &state,
      std::span<const std::string> candidate_ids,
      const std::optional<std::string> &target_context_id) const {
    std::vector<AdaptiveResearchRuntimeEvent> events;
    std::unordered_set<std::string> seen;
    for (const auto &node_id : candidate_ids) {
      if (!seen.insert(node_id).second)
        continue;
      const auto *existing = state.try_get_node_state(node_id);
      if (existing && existing->maturity >= ResearchMaturity::investigable)
        continue;
      const auto eligible = eligibility.evaluate_scientific_eligibility(
          state, node_id,
          target_context_id ? std::optional<std::string_view>(*target_context_id)
                            : std::nullopt);
      if (!eligible.allowed)
        continue;
      const double total = existing ? existing->total_research_points : 0.0;
      Writer::set_node_state(
          state, {node_id, ResearchMaturity::investigable, std::nullopt, 0.0,
                  total, 0});
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::node_became_investigable, state,
          node_id, std::nullopt,
          "New research program is Investigable: " +
              content->catalog().get_node(node_id).name + "."));
    }
    return events;
  }

  void append_materialized(
      AdaptiveResearchCivilizationState &state,
      std::span<const std::string> candidate_ids,
      const std::optional<std::string> &target_context_id,
      std::vector<AdaptiveResearchRuntimeEvent> &events) const {
    auto appended = materialize(state, candidate_ids, target_context_id);
    std::move(appended.begin(), appended.end(), std::back_inserter(events));
  }

  void validate_capability_context(
      const ResearchCapabilityDefinition &definition,
      const std::optional<std::string> &context_id) const {
    if (definition.scope == ResearchCapabilityScope::civilization && context_id)
      throw std::invalid_argument(
          "Civilization capability '" + definition.id +
          "' cannot be granted to target context '" + *context_id + "'.");
    if (definition.scope != ResearchCapabilityScope::civilization &&
        !context_id)
      throw std::invalid_argument("Scoped capability '" + definition.id +
                                  "' requires a target population/installation "
                                  "context.");
  }

  std::optional<std::string> context_for_capability(
      std::string_view capability_id,
      const std::optional<std::string> &target_context_id) const {
    const auto *definition = content->catalog().find_capability(capability_id);
    if (definition->scope == ResearchCapabilityScope::civilization)
      return std::nullopt;
    return target_context_id;
  }

  void grant_capability(
      AdaptiveResearchCivilizationState &state, std::string capability_id,
      std::optional<std::string> context_id,
      const std::optional<std::string> &source_node_id,
      std::vector<AdaptiveResearchRuntimeEvent> &events) const {
    std::deque<CapabilityVisitKey> pending;
    std::unordered_set<CapabilityVisitKey, CapabilityVisitHash> visited;
    pending.push_back({std::move(capability_id), std::move(context_id)});
    while (!pending.empty()) {
      auto current = std::move(pending.front());
      pending.pop_front();
      if (!visited.insert(current).second)
        continue;
      const auto *definition = content->catalog().find_capability(current.id);
      // The public entry point validates the first ID. Catalog validation proves
      // implication targets and grant IDs, so all later lookups are present.
      if (!definition)
        throw std::invalid_argument("Unknown cross-lineage capability '" +
                                    current.id + "'.");
      validate_capability_context(*definition, current.context);
      if (Writer::add_capability(
              state, {current.id, current.context})) {
        events.push_back(event(
            AdaptiveResearchRuntimeEventType::capability_granted, state,
            source_node_id, current.id,
            "Functional capability gained: " + definition->name + "."));
        append_materialized(
            state,
            content->catalog().nodes_for_capability_requirement(current.id),
            current.context, events);
      }
      for (const auto &implication :
           content->catalog().capability_implications()) {
        if (implication.from_capability_id != current.id)
          continue;
        pending.push_back(
            {implication.to_capability_id,
             implication.preserve_target_context ? current.context
                                                 : std::nullopt});
      }
    }
  }

  void apply_stage_grant(
      AdaptiveResearchCivilizationState &state,
      const AdaptiveResearchNodeDefinition &node, ResearchMaturity stage,
      const std::optional<std::string> &target_context_id,
      std::vector<AdaptiveResearchRuntimeEvent> &events) const {
    std::span<const ResearchNodeGrant> grants;
    if (stage == ResearchMaturity::demonstrated)
      grants = content->catalog().demonstrated_grants();
    else if (stage == ResearchMaturity::mature)
      grants = content->catalog().mature_grants();
    else
      return;
    const auto it =
        std::find_if(grants.begin(), grants.end(), [&node](const auto &entry) {
          return entry.node_id == node.id;
        });
    if (it == grants.end())
      return;
    const auto &grant = it->grant;
    for (const auto &capability_id : grant.capability_ids)
      grant_capability(state, capability_id,
                       context_for_capability(capability_id, target_context_id),
                       node.id, events);

    for (const auto &trait_id : grant.civilization_trait_ids) {
      const auto &trait = content->applicability().get_trait(trait_id);
      bool changed = false;
      if (trait.scope == ResearchApplicabilityTraitScope::civilization)
        changed = Writer::add_civilization_trait(state, trait_id);
      else if (target_context_id)
        changed = Writer::add_applicability_trait(state, *target_context_id,
                                                  trait_id);
      if (!changed)
        continue;
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::civilization_trait_granted, state,
          node.id, trait_id,
          "Research established applicability trait/capability context '" +
              trait_id + "'."));
      append_materialized(state, content->catalog().nodes_for_trait(trait_id),
                          target_context_id, events);
    }

    if (grant.research_capacity_stage_id &&
        state.directed_program_stage_id() != *grant.research_capacity_stage_id) {
      Writer::set_directed_program_stage(state,
                                         *grant.research_capacity_stage_id);
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::directed_program_stage_changed,
          state, node.id, *grant.research_capacity_stage_id,
          "Directed research coordination advanced to '" +
              *grant.research_capacity_stage_id + "'."));
    }
    for (const auto &event_id : grant.enabled_deployment_event_ids)
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::deployment_event_unlocked, state,
          node.id, event_id,
          "Research now permits deployment event '" + event_id +
              "', subject to real deployment by the owning subsystem."));
  }

  void mature(AdaptiveResearchCivilizationState &state,
              const AdaptiveResearchNodeDefinition &node,
              const ResearchProjectRuntimeState &project,
              std::vector<AdaptiveResearchRuntimeEvent> &events) const {
    Writer::remove_project(state, node.id);
    Writer::set_node_state(
        state, {node.id, ResearchMaturity::mature, std::nullopt, 0.0,
                node.project_requirements.base_research_points, 0});
    for (const auto &capability_id : node.declared_capabilities) {
      if (!content->catalog().find_capability(capability_id))
        continue;
      grant_capability(
          state, capability_id,
          context_for_capability(capability_id,
                                 project.target_applicability_context_id),
          node.id, events);
    }
    apply_stage_grant(state, node, ResearchMaturity::mature,
                      project.target_applicability_context_id, events);
    events.push_back(event(
        AdaptiveResearchRuntimeEventType::technology_matured, state, node.id,
        std::nullopt,
        node.name + " reached Mature scientific/engineering knowledge."));
    append_materialized(state, content->catalog().children_for(node.id),
                        project.target_applicability_context_id, events);
  }

  void advance_one(AdaptiveResearchCivilizationState &state,
                   const AdaptiveResearchNodeDefinition &node,
                   ResearchProjectRuntimeState project, double available_rp,
                   std::vector<AdaptiveResearchRuntimeEvent> &events) const {
    while (available_rp > 0.000001 && find_project(state, node.id) &&
           !project.paused) {
      const double stage_work =
          content->progress_policy().get_stage_work(node, project.stage);
      const double remaining =
          dotnet_max(0.0, stage_work - project.stage_research_points);
      const double spend = dotnet_min(available_rp, remaining);
      project.stage_research_points += spend;
      project.total_research_points += spend;
      available_rp -= spend;
      Writer::set_project(state, project);
      Writer::set_node_state(
          state, {node.id, project.stage, std::nullopt,
                  project.stage_research_points, project.total_research_points,
                  0});
      if (project.stage_research_points + 0.000001 < stage_work)
        break;
      if (project.stage == ResearchMaturity::experimental &&
          node.is_hypothesis) {
        project.paused = true;
        project.pause_reason = "hypothesis_resolution_required";
        Writer::set_project(state, project);
        events.push_back(event(
            AdaptiveResearchRuntimeEventType::hypothesis_resolution_required,
            state, node.id, std::nullopt,
            node.name + " reached its experimental evidence boundary and "
                        "requires scientific resolution."));
        break;
      }
      if (project.stage == ResearchMaturity::engineering) {
        mature(state, node, project, events);
        break;
      }
      const auto next = project.stage == ResearchMaturity::experimental
                            ? ResearchMaturity::demonstrated
                            : ResearchMaturity::engineering;
      const auto facility = eligibility.evaluate_stage_facility_eligibility(
          state, node.id, next);
      if (!facility.allowed) {
        project.paused = true;
        project.pause_reason = facility.blockers.front().message;
        Writer::set_project(state, project);
        events.push_back(event(
            AdaptiveResearchRuntimeEventType::project_paused, state, node.id,
            facility.blockers.front().subject_id,
            "Research paused before " + maturity_name(next) + ": " +
                facility.blockers.front().message));
        break;
      }
      project.stage = next;
      project.stage_research_points = 0.0;
      Writer::set_project(state, project);
      Writer::set_node_state(
          state, {node.id, next, std::nullopt, 0.0,
                  project.total_research_points, 0});
      apply_stage_grant(state, node, next,
                        project.target_applicability_context_id, events);
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::stage_advanced, state, node.id,
          std::nullopt, node.name + " advanced to " + maturity_name(next) +
                            "."));
    }
  }
};

AdaptiveResearchRuntime::AdaptiveResearchRuntime(
    std::shared_ptr<const AdaptiveResearchRuntimeContent> content)
    : storage_(content ? std::make_unique<Storage>(std::move(content))
                       : throw std::invalid_argument(
                             "Adaptive Research runtime content cannot be "
                             "null.")) {}
AdaptiveResearchRuntime::AdaptiveResearchRuntime(
    AdaptiveResearchRuntimeContent &&content)
    : AdaptiveResearchRuntime(
          std::make_shared<const AdaptiveResearchRuntimeContent>(
              std::move(content))) {}
AdaptiveResearchRuntime::~AdaptiveResearchRuntime() = default;
AdaptiveResearchRuntime::AdaptiveResearchRuntime(
    AdaptiveResearchRuntime &&) noexcept = default;
AdaptiveResearchRuntime &
AdaptiveResearchRuntime::operator=(AdaptiveResearchRuntime &&) noexcept =
    default;

const AdaptiveResearchRuntimeContent &
AdaptiveResearchRuntime::content() const noexcept {
  return *storage_->content;
}
std::shared_ptr<const AdaptiveResearchRuntimeContent>
AdaptiveResearchRuntime::shared_content() const noexcept {
  return storage_->content;
}
const AdaptiveResearchCatalog &AdaptiveResearchRuntime::catalog() const noexcept {
  return content().catalog();
}
const AdaptiveResearchApplicabilityCatalog &
AdaptiveResearchRuntime::applicability() const noexcept {
  return content().applicability();
}
const AdaptiveResearchFacilityCatalog &
AdaptiveResearchRuntime::facilities() const noexcept {
  return content().facilities();
}
const AdaptiveResearchProgressPolicy &
AdaptiveResearchRuntime::progress_policy() const noexcept {
  return content().progress_policy();
}
const AdaptiveResearchEligibilityEvaluator &
AdaptiveResearchRuntime::eligibility() const noexcept {
  return storage_->eligibility;
}
const AdaptiveResearchViewBuilder &
AdaptiveResearchRuntime::view_builder() const noexcept {
  return storage_->view_builder;
}

AdaptiveResearchCivilizationState AdaptiveResearchRuntime::create_civilization_state(
    std::string civilization_id) const {
  return {std::move(civilization_id),
          catalog().metadata().starting_directed_program_stage_id};
}
AdaptiveResearchView AdaptiveResearchRuntime::build_view(
    const AdaptiveResearchCivilizationState &state,
    std::optional<std::string_view> default_target_context_id) const {
  return view_builder().build(state, default_target_context_id);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchRuntime::set_total_effective_research_labs(
    AdaptiveResearchCivilizationState &state, double total_labs) const {
  Writer::set_total_effective_research_labs(state, total_labs);
  return {};
}

std::vector<AdaptiveResearchRuntimeEvent> AdaptiveResearchRuntime::set_pressure(
    AdaptiveResearchCivilizationState &state, std::string_view pressure_id,
    double value, std::optional<std::string_view> target_context_id) const {
  const std::string owned_pressure_id(pressure_id);
  const auto owned_context_id = own(target_context_id);
  if (!catalog().has_pressure(owned_pressure_id))
    throw std::invalid_argument("Unknown Research Pressure '" +
                                owned_pressure_id +
                                "'. (Parameter 'pressureId')");
  if (!Writer::set_pressure(state, owned_pressure_id, value))
    return {};
  return storage_->materialize(
      state, catalog().nodes_for_pressure(owned_pressure_id), owned_context_id);
}

std::vector<AdaptiveResearchRuntimeEvent> AdaptiveResearchRuntime::add_evidence(
    AdaptiveResearchCivilizationState &state,
    std::string evidence_instance_id, std::string_view evidence_type_id,
    std::string provenance, double quality, double confidence,
    std::optional<std::string_view> context_id) const {
  const std::string owned_evidence_type_id(evidence_type_id);
  auto context = own(context_id);
  if (!catalog().has_evidence_type(owned_evidence_type_id))
    throw std::invalid_argument("Unknown evidence type '" +
                                owned_evidence_type_id +
                                "'. (Parameter 'evidenceTypeId')");
  if (quality < 0.0 || quality > 1.0 || std::isnan(quality))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'quality')");
  if (confidence < 0.0 || confidence > 1.0 || std::isnan(confidence))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'confidence')");
  ResearchEvidenceInstance evidence{std::move(evidence_instance_id),
                                    owned_evidence_type_id,
                                    std::move(provenance), quality, confidence,
                                    context,
                                    detail::checked_next_research_state_revision(
                                        state.revision())};
  if (!Writer::add_evidence(state, std::move(evidence)))
    return {};
  return storage_->materialize(
      state, catalog().nodes_for_evidence(owned_evidence_type_id), context);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchRuntime::add_civilization_trait(
    AdaptiveResearchCivilizationState &state, std::string_view trait_id) const {
  const std::string owned_trait_id(trait_id);
  const auto &definition = applicability().get_trait(owned_trait_id);
  if (definition.scope != ResearchApplicabilityTraitScope::civilization)
    throw std::invalid_argument(
        "Trait '" + owned_trait_id +
        "' must be attached to a population/species applicability context. "
        "(Parameter 'traitId')");
  if (!Writer::add_civilization_trait(state, owned_trait_id))
    return {};
  return storage_->materialize(state, catalog().nodes_for_trait(owned_trait_id),
                               std::nullopt);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchRuntime::set_applicability_context_traits(
    AdaptiveResearchCivilizationState &state, std::string context_id,
    std::span<const std::string> trait_ids) const {
  std::vector<std::string> traits;
  std::unordered_set<std::string> seen;
  for (const auto &trait_id : trait_ids)
    if (seen.insert(trait_id).second)
      traits.push_back(trait_id);
  for (const auto &trait_id : traits) {
    const auto &definition = applicability().get_trait(trait_id);
    if (definition.scope !=
        ResearchApplicabilityTraitScope::population_or_species)
      throw std::invalid_argument(
          "Civilization-scoped trait '" + trait_id +
          "' cannot be placed on applicability context '" + context_id +
          "'. (Parameter 'traitIds')");
  }
  if (!Writer::set_applicability_context_traits(state, context_id, traits))
    return {};
  std::vector<std::string> candidates;
  std::unordered_set<std::string> candidate_seen;
  for (const auto &trait_id : traits)
    for (const auto &node_id : catalog().nodes_for_trait(trait_id))
      if (candidate_seen.insert(node_id).second)
        candidates.push_back(node_id);
  return storage_->materialize(state, candidates, context_id);
}

std::vector<AdaptiveResearchRuntimeEvent> AdaptiveResearchRuntime::add_capability(
    AdaptiveResearchCivilizationState &state, std::string_view capability_id,
    std::optional<std::string_view> context_id) const {
  const std::string owned_capability_id(capability_id);
  auto context = own(context_id);
  const auto *definition = catalog().find_capability(owned_capability_id);
  if (!definition)
    throw std::invalid_argument("Unknown cross-lineage capability '" +
                                owned_capability_id +
                                "'. (Parameter 'capabilityId')");
  storage_->validate_capability_context(*definition, context);
  std::vector<AdaptiveResearchRuntimeEvent> events;
  storage_->grant_capability(state, owned_capability_id,
                             std::move(context), std::nullopt, events);
  return events;
}

void AdaptiveResearchRuntime::add_facility_capability(
    AdaptiveResearchCivilizationState &state,
    std::string_view capability_id) const {
  const std::string owned_capability_id(capability_id);
  const auto ids = facilities().facility_capability_ids();
  if (std::find(ids.begin(), ids.end(), owned_capability_id) == ids.end())
    throw std::invalid_argument("Unknown research facility capability '" +
                                owned_capability_id +
                                "'. (Parameter 'facilityCapabilityId')");
  Writer::add_facility_capability(state, owned_capability_id);
}
void AdaptiveResearchRuntime::remove_facility_capability(
    AdaptiveResearchCivilizationState &state,
    std::string_view capability_id) const {
  const std::string owned_capability_id(capability_id);
  Writer::remove_facility_capability(state, owned_capability_id);
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchRuntime::review_basic_science_candidates(
    AdaptiveResearchCivilizationState &state,
    std::span<const std::string> bounded_candidate_node_ids,
    std::optional<std::string_view> target_context_id) const {
  std::vector<std::string> copied(bounded_candidate_node_ids.begin(),
                                  bounded_candidate_node_ids.end());
  return storage_->materialize(state, copied, own(target_context_id));
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::start_directed_research(
    AdaptiveResearchCivilizationState &state, std::string_view node_id,
    double requested_assigned_labs, double readiness_score,
    std::optional<std::string_view> target_context_id) const {
  const std::string owned_node_id(node_id);
  const auto owned_context_id = own(target_context_id);
  const auto eligibility_result = eligibility().evaluate_project_start(
      state, owned_node_id, requested_assigned_labs,
      owned_context_id
          ? std::optional<std::string_view>(*owned_context_id)
          : std::nullopt);
  if (!eligibility_result.allowed)
    return AdaptiveResearchCommandResult::rejected(
        "Research project cannot start under current conditions.",
        eligibility_result.blockers);
  const auto readiness =
      progress_policy().get_readiness_efficiency(readiness_score);
  const auto node = *state.try_get_node_state(owned_node_id);
  ResearchProjectRuntimeState project{
      owned_node_id, ResearchMaturity::experimental, owned_context_id,
      requested_assigned_labs, readiness, false, std::nullopt, 0.0,
      node.total_research_points, 0};
  Writer::set_project(state, project);
  auto updated_node = node;
  updated_node.maturity = ResearchMaturity::experimental;
  updated_node.stage_research_points = 0.0;
  updated_node.revision = 0;
  Writer::set_node_state(state, std::move(updated_node));
  auto started = event(
      AdaptiveResearchRuntimeEventType::project_started, state,
      owned_node_id, std::nullopt,
      "Directed research started: " +
          catalog().get_node(owned_node_id).name + ".");
  return {true, started.message, {started}, {}};
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::pause_directed_research(
    AdaptiveResearchCivilizationState &state, std::string_view node_id) const {
  const std::string owned_node_id(node_id);
  const auto *found = find_project(state, owned_node_id);
  if (!found)
    return AdaptiveResearchCommandResult::rejected(
        "No active research project exists for that node.");
  auto project = *found;
  if (project.paused)
    return {true, "Research project is already paused.", {}, {}};
  project.paused = true;
  project.pause_reason = "paused_by_order";
  Writer::set_project(state, project);
  auto paused = event(
      AdaptiveResearchRuntimeEventType::project_paused, state,
      owned_node_id, std::nullopt,
      "Research project paused; accumulated scientific work is preserved.");
  return {true, paused.message, {paused}, {}};
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::resume_directed_research(
    AdaptiveResearchCivilizationState &state, std::string_view node_id,
    double requested_assigned_labs, double readiness_score) const {
  const std::string owned_node_id(node_id);
  const auto *found = find_project(state, owned_node_id);
  if (!found || !found->paused)
    return AdaptiveResearchCommandResult::rejected(
        "The project is not currently paused.");
  auto project = *found;
  std::vector<ResearchBlocker> blockers;
  auto scientific = eligibility().evaluate_scientific_eligibility(
      state, owned_node_id,
      project.target_applicability_context_id
          ? std::optional<std::string_view>(
                *project.target_applicability_context_id)
          : std::nullopt);
  auto facility = eligibility().evaluate_stage_facility_eligibility(
      state, owned_node_id, project.stage);
  blockers.insert(blockers.end(), scientific.blockers.begin(),
                  scientific.blockers.end());
  blockers.insert(blockers.end(), facility.blockers.begin(),
                  facility.blockers.end());
  const auto &definition = catalog().get_node(owned_node_id);
  if (requested_assigned_labs + 0.000001 <
      definition.project_requirements.minimum_labs)
    blockers.push_back(
        {ResearchBlockerCode::below_minimum_assigned_labs,
         owned_node_id,
         static_cast<double>(definition.project_requirements.minimum_labs),
         requested_assigned_labs,
         "The project needs more assigned Effective Research Labs."});
  if (state.free_effective_labs() + 0.000001 < requested_assigned_labs)
    blockers.push_back({ResearchBlockerCode::insufficient_free_labs,
                        owned_node_id, requested_assigned_labs,
                        state.free_effective_labs(),
                        "Not enough free Effective Research Labs are "
                        "available."});
  const auto &stage = catalog().get_directed_program_stage(
      state.directed_program_stage_id());
  const auto active_count = static_cast<int>(std::count_if(
      state.active_projects().begin(), state.active_projects().end(),
      [](const auto &active) { return !active.paused; }));
  if (stage.directed_program_limit &&
      active_count >= *stage.directed_program_limit)
    blockers.push_back({ResearchBlockerCode::directed_program_capacity,
                        stage.id,
                        static_cast<double>(*stage.directed_program_limit),
                        static_cast<double>(active_count),
                        "No directed research-program capacity is free."});
  if (!blockers.empty())
    return AdaptiveResearchCommandResult::rejected(
        "Research project cannot resume under current conditions.",
        std::move(blockers));
  project.paused = false;
  project.pause_reason = std::nullopt;
  project.assigned_effective_labs = requested_assigned_labs;
  project.readiness_efficiency =
      progress_policy().get_readiness_efficiency(readiness_score);
  Writer::set_project(state, project);
  auto resumed = event(AdaptiveResearchRuntimeEventType::project_resumed, state,
                       owned_node_id, std::nullopt,
                       "Research project resumed.");
  return {true, resumed.message, {resumed}, {}};
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::reallocate_research_labs(
    AdaptiveResearchCivilizationState &state, std::string_view node_id,
    double requested_assigned_labs) const {
  const std::string owned_node_id(node_id);
  const auto *found = find_project(state, owned_node_id);
  if (!found)
    return AdaptiveResearchCommandResult::rejected(
        "No research project exists for that node.");
  auto project = *found;
  const auto &definition = catalog().get_node(owned_node_id);
  if (requested_assigned_labs + 0.000001 <
      definition.project_requirements.minimum_labs)
    return AdaptiveResearchCommandResult::rejected(
        "Allocation is below the project's minimum lab requirement.");
  const double available =
      state.free_effective_labs() +
      (project.paused ? 0.0 : project.assigned_effective_labs);
  if (available + 0.000001 < requested_assigned_labs)
    return AdaptiveResearchCommandResult::rejected(
        "Not enough Effective Research Labs are available for that "
        "allocation.");
  project.assigned_effective_labs = requested_assigned_labs;
  Writer::set_project(state, project);
  auto changed = event(
      AdaptiveResearchRuntimeEventType::project_labs_changed, state,
      owned_node_id, std::nullopt,
      "Research lab allocation changed to " +
          detail::legacy_custom_fixed(requested_assigned_labs, 0, 2) +
          " effective labs.");
  return {true, changed.message, {changed}, {}};
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::set_project_readiness(
    AdaptiveResearchCivilizationState &state, std::string_view node_id,
    double readiness_score) const {
  const std::string owned_node_id(node_id);
  const auto *found = find_project(state, owned_node_id);
  if (!found)
    return AdaptiveResearchCommandResult::rejected(
        "No research project exists for that node.");
  auto project = *found;
  project.readiness_efficiency =
      progress_policy().get_readiness_efficiency(readiness_score);
  Writer::set_project(state, project);
  auto changed = event(
      AdaptiveResearchRuntimeEventType::project_readiness_changed, state,
      owned_node_id, std::nullopt,
      "Research readiness was recalculated from current "
      "competence/facility/evidence/tacit inputs.");
  return {true, changed.message, {changed}, {}};
}

std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchRuntime::advance_projects(
    AdaptiveResearchCivilizationState &state, double elapsed_years) const {
  if (elapsed_years < 0.0 || !std::isfinite(elapsed_years))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'elapsedYears')");
  if (elapsed_years <= 0.0)
    return {};
  std::vector<std::string> initial_ids;
  for (const auto &project : state.active_projects())
    initial_ids.push_back(project.node_id);
  std::vector<AdaptiveResearchRuntimeEvent> events;
  for (const auto &node_id : initial_ids) {
    const auto *found = find_project(state, node_id);
    if (!found || found->paused)
      continue;
    auto project = *found;
    const auto scientific = eligibility().evaluate_scientific_eligibility(
        state, node_id,
        project.target_applicability_context_id
            ? std::optional<std::string_view>(
                  *project.target_applicability_context_id)
            : std::nullopt);
    const auto facility = eligibility().evaluate_stage_facility_eligibility(
        state, node_id, project.stage);
    if (!scientific.allowed || !facility.allowed) {
      const auto &first = !scientific.blockers.empty()
                              ? scientific.blockers.front().message
                              : (!facility.blockers.empty()
                                     ? facility.blockers.front().message
                                     : std::string("research requirements "
                                                   "changed"));
      project.paused = true;
      project.pause_reason = first;
      Writer::set_project(state, project);
      events.push_back(event(
          AdaptiveResearchRuntimeEventType::project_paused, state, node_id,
          std::nullopt, "Research paused: " + first));
      continue;
    }
    const auto &node = catalog().get_node(node_id);
    const double scaled = catalog().lab_scaling().scale_assigned_labs(
        project.assigned_effective_labs,
        static_cast<double>(node.project_requirements.recommended_labs));
    const double available =
        scaled * catalog().metadata().base_rp_per_effective_lab_per_year *
        project.readiness_efficiency * elapsed_years;
    storage_->advance_one(state, node, project, available, events);
  }
  return events;
}

AdaptiveResearchCommandResult AdaptiveResearchRuntime::resolve_hypothesis(
    AdaptiveResearchCivilizationState &state, std::string_view node_id,
    bool supported) const {
  const std::string owned_node_id(node_id);
  const auto *found = find_project(state, owned_node_id);
  if (!found || !found->paused ||
      found->pause_reason !=
          std::optional<std::string>("hypothesis_resolution_required"))
    return AdaptiveResearchCommandResult::rejected(
        "No hypothesis is awaiting scientific resolution for that node.");
  auto project = *found;
  const auto &node = catalog().get_node(owned_node_id);
  if (!node.is_hypothesis)
    return AdaptiveResearchCommandResult::rejected(
        "That node is not a scientific hypothesis.");
  std::vector<AdaptiveResearchRuntimeEvent> events;
  if (!supported) {
    Writer::remove_project(state, owned_node_id);
    Writer::set_node_state(
        state, {owned_node_id, ResearchMaturity::archived, "disproven",
                0.0, project.total_research_points, 0});
    events.push_back(event(
        AdaptiveResearchRuntimeEventType::hypothesis_disproven, state,
        owned_node_id, std::nullopt,
        node.name +
            " was disproven; accumulated negative knowledge is preserved."));
  } else {
    project.stage = ResearchMaturity::demonstrated;
    project.stage_research_points = 0.0;
    project.paused = false;
    project.pause_reason = std::nullopt;
    Writer::set_project(state, project);
    Writer::set_node_state(
        state, {owned_node_id, ResearchMaturity::demonstrated,
                std::nullopt, 0.0, project.total_research_points, 0});
    storage_->apply_stage_grant(state, node, ResearchMaturity::demonstrated,
                                project.target_applicability_context_id,
                                events);
    events.push_back(event(
        AdaptiveResearchRuntimeEventType::stage_advanced, state,
        owned_node_id, std::nullopt,
        "Evidence supports " + node.name +
            "; the principle is Demonstrated."));
  }
  return {true, events.back().message, events, {}};
}

AdaptiveResearchRuntime load_adaptive_research_runtime(
    const std::filesystem::path &root_path) {
  auto content = load_adaptive_research_runtime_content(root_path);
  auto shared = std::make_shared<const AdaptiveResearchRuntimeContent>(
      std::move(content));
  return AdaptiveResearchRuntime(std::move(shared));
}

} // namespace stellar::core
