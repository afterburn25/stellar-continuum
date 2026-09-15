#include <stellar/core/adaptive_research_readiness.hpp>

#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>
#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace stellar::core {
namespace {

template <class Range, class Value>
bool contains(const Range &values, const Value &value) {
  return std::ranges::find(values, value) != values.end();
}

double dotnet_min(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}

double dotnet_max(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}

double dotnet_clamp(double value, double minimum, double maximum) noexcept {
  return dotnet_min(dotnet_max(value, minimum), maximum);
}

std::string maturity_name(ResearchMaturity stage) {
  switch (stage) {
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
  default:
    return std::to_string(static_cast<int>(stage));
  }
}

const ResearchStageCompetenceWeights &
stage_weights(const AdaptiveResearchExpertiseCatalog &catalog,
              ResearchMaturity stage) {
  const auto entries = catalog.stage_weights();
  const auto found = std::ranges::find(
      entries, stage, &ResearchStageCompetenceWeightEntry::stage);
  if (found == entries.end())
    throw std::invalid_argument(
        "Readiness is only defined for directed research stages, not '" +
        maturity_name(stage) + "'. (Parameter 'stage')");
  return found->weights;
}

const ResearchTacitAssetTypeDefinition &
tacit_type(const AdaptiveResearchExpertiseCatalog &catalog,
           std::string_view id) {
  const auto entries = catalog.tacit_asset_types();
  const auto found =
      std::ranges::find(entries, id, &ResearchTacitAssetTypeDefinition::id);
  if (found == entries.end())
    throw std::out_of_range("The given key '" + std::string(id) +
                            "' was not present in the dictionary.");
  return *found;
}

double assimilation_factor(const AdaptiveResearchExpertiseCatalog &catalog,
                           ResearchTacitAssimilationStage stage) {
  const auto &entries = catalog.runtime_policy().tacit_assimilation_factors;
  const auto found = std::ranges::find(entries, stage,
                                       &ResearchTacitAssimilationFactor::stage);
  if (found == entries.end())
    throw std::out_of_range("The given key '" +
                            std::to_string(static_cast<int>(stage)) +
                            "' was not present in the dictionary.");
  return found->factor;
}

double component_weight(const ResearchStageCompetenceWeights &weights,
                        ResearchCompetenceComponent component) noexcept {
  switch (component) {
  case ResearchCompetenceComponent::theoretical:
    return weights.theoretical;
  case ResearchCompetenceComponent::experimental:
    return weights.experimental;
  case ResearchCompetenceComponent::engineering:
    return weights.engineering;
  default:
    return 0.0;
  }
}

} // namespace

AdaptiveResearchReadinessCalculator::AdaptiveResearchReadinessCalculator(
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchFacilityCatalog &facilities,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog,
    const AdaptiveResearchProgressPolicy &progress_policy) noexcept
    : catalog_(&catalog), facilities_(&facilities),
      expertise_catalog_(&expertise_catalog),
      progress_policy_(&progress_policy) {}

ResearchReadinessBreakdown AdaptiveResearchReadinessCalculator::calculate(
    const AdaptiveResearchCivilizationState &state,
    const AdaptiveResearchExpertiseState &expertise, std::string_view node_id,
    ResearchMaturity stage, double assigned_effective_labs,
    std::optional<std::string_view> target_context) const {
  if (assigned_effective_labs <= 0.0 || !std::isfinite(assigned_effective_labs))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'assignedEffectiveLabs')");

  const auto &weights = stage_weights(*expertise_catalog_, stage);
  const auto &node = catalog_->get_node(node_id);

  double field_score = 0.0;
  if (!node.knowledge_fields.empty()) {
    std::vector<double> scores;
    scores.reserve(node.knowledge_fields.size());
    for (const auto &field_id : node.knowledge_fields) {
      const auto field = expertise.get_field(field_id).current;
      scores.push_back(
          dotnet_clamp(field.theoretical * weights.theoretical +
                           field.experimental * weights.experimental +
                           field.engineering * weights.engineering,
                       0.0, 100.0));
    }
    const double weakest = *std::ranges::min_element(scores);
    const double mean =
        std::accumulate(scores.begin(), scores.end(), 0.0) / scores.size();
    field_score = dotnet_clamp(0.60 * weakest + 0.40 * mean, 0.0, 100.0);
  }

  double matching_units = 0.0;
  const auto *stage_requirement =
      facilities_->get_stage_requirement(node.id, stage);
  const auto &policy = expertise_catalog_->runtime_policy();
  for (const auto &institution : expertise.institutions()) {
    if (!institution.is_active())
      continue;
    if (target_context && institution.context_id &&
        *institution.context_id != *target_context)
      continue;
    const auto &definition = expertise_catalog_->get_institution(
        institution.institution_archetype_id);
    const double units =
        definition.effective_lab_units * institution.active_count;
    double factor = policy.general_lab_matching_factor;
    if (!definition.specialized_field_ids.empty()) {
      factor =
          std::ranges::any_of(definition.specialized_field_ids,
                              [&](const auto &field) {
                                return contains(node.knowledge_fields, field);
                              })
              ? policy.specialized_matching_factor
              : policy.nonmatching_specialist_factor;
    }
    if (stage_requirement &&
        std::ranges::any_of(
            definition.facility_capability_ids, [&](const auto &capability) {
              return contains(stage_requirement->all_of, capability) ||
                     contains(stage_requirement->any_of, capability);
            }))
      factor = dotnet_max(factor, policy.specialized_matching_factor);
    matching_units += units * factor;
  }
  const double facility_score =
      100.0 * dotnet_min(1.0, matching_units /
                                  dotnet_max(assigned_effective_labs, 1.0));

  std::vector<std::string_view> required_evidence;
  for (const auto &id : node.applicability.evidence_types)
    if (!contains(std::span<const std::string_view>(required_evidence), id))
      required_evidence.push_back(id);
  for (const auto &id : node.project_requirements.required_evidence)
    if (!contains(std::span<const std::string_view>(required_evidence), id))
      required_evidence.push_back(id);
  std::optional<double> evidence_score;
  if (!required_evidence.empty()) {
    double total = 0.0;
    std::size_t count = 0;
    for (const auto &evidence : state.evidence_instances()) {
      if (!contains(std::span<const std::string_view>(required_evidence),
                    evidence.evidence_type_id))
        continue;
      if (target_context && evidence.context_id &&
          *evidence.context_id != *target_context)
        continue;
      total += 100.0 * evidence.quality * evidence.confidence;
      ++count;
    }
    evidence_score = count == 0 ? 0.0 : dotnet_clamp(total / count, 0.0, 100.0);
  }

  std::vector<double> relevant_tacit;
  for (const auto &asset : expertise.tacit_assets()) {
    if (target_context && asset.context_id &&
        *asset.context_id != *target_context)
      continue;
    bool relevant = false;
    switch (asset.scope_kind) {
    case ResearchTacitScopeKind::knowledge_field:
      relevant = contains(node.knowledge_fields, asset.scope_ref);
      break;
    case ResearchTacitScopeKind::technology_node:
      relevant = asset.scope_ref == node.id;
      break;
    case ResearchTacitScopeKind::solution_family:
      relevant = asset.scope_ref == node.solution_family;
      break;
    case ResearchTacitScopeKind::foreign_lineage:
      relevant = contains(node.knowledge_fields, "xenoscience");
      break;
    case ResearchTacitScopeKind::facility_or_process:
      relevant = asset.scope_ref == node.id ||
                 (stage_requirement &&
                  (contains(stage_requirement->all_of, asset.scope_ref) ||
                   contains(stage_requirement->any_of, asset.scope_ref)));
      break;
    default:
      break;
    }
    if (!relevant)
      continue;
    const auto &type = tacit_type(*expertise_catalog_, asset.asset_type_id);
    if (!std::ranges::any_of(type.supported_components, [&](auto component) {
          return component_weight(weights, component) > 0.0;
        }))
      continue;
    const double score =
        asset.depth *
        assimilation_factor(*expertise_catalog_, asset.assimilation_stage) *
        asset.availability * asset.translation_context_quality *
        asset.training_continuity;
    relevant_tacit.push_back(dotnet_clamp(score, 0.0, 100.0));
  }

  std::optional<double> tacit_score;
  if (contains(node.knowledge_fields, "xenoscience") ||
      !relevant_tacit.empty()) {
    if (relevant_tacit.empty()) {
      tacit_score = 0.0;
    } else {
      const double best = *std::ranges::max_element(relevant_tacit);
      const double mean =
          std::accumulate(relevant_tacit.begin(), relevant_tacit.end(), 0.0) /
          relevant_tacit.size();
      tacit_score = dotnet_clamp(policy.tacit_best_weight * best +
                                     policy.tacit_mean_weight * mean,
                                 0.0, 100.0);
    }
  }

  const auto &readiness_weights = expertise_catalog_->readiness_weights();
  double weighted_total = field_score * readiness_weights.field_competence +
                          facility_score * readiness_weights.facility_readiness;
  double applied_weight =
      readiness_weights.field_competence + readiness_weights.facility_readiness;
  if (evidence_score) {
    weighted_total += *evidence_score * readiness_weights.evidence_readiness;
    applied_weight += readiness_weights.evidence_readiness;
  }
  if (tacit_score) {
    weighted_total += *tacit_score * readiness_weights.tacit_expertise;
    applied_weight += readiness_weights.tacit_expertise;
  }
  const double readiness =
      applied_weight <= 0.0
          ? 0.0
          : dotnet_clamp(weighted_total / applied_weight, 0.0, 100.0);
  const double efficiency =
      progress_policy_->get_readiness_efficiency(readiness);

  std::vector<std::string> explanations{
      "Field competence readiness " +
          detail::legacy_custom_fixed(field_score, 0, 1) + "/100.",
      "Facility readiness " +
          detail::legacy_custom_fixed(facility_score, 0, 1) + "/100 for " +
          detail::legacy_custom_fixed(assigned_effective_labs, 0, 2) +
          " assigned Effective Research Labs."};
  if (evidence_score)
    explanations.push_back("Required evidence readiness " +
                           detail::legacy_custom_fixed(*evidence_score, 0, 1) +
                           "/100.");
  if (tacit_score)
    explanations.push_back("Relevant tacit expertise readiness " +
                           detail::legacy_custom_fixed(*tacit_score, 0, 1) +
                           "/100.");
  explanations.push_back("Overall Project Readiness " +
                         detail::legacy_custom_fixed(readiness, 0, 1) +
                         "/100 -> RP efficiency " +
                         detail::legacy_custom_fixed(efficiency, 0, 2) + "x.");

  return {std::string(node_id),
          stage,
          target_context ? std::optional<std::string>(*target_context)
                         : std::nullopt,
          field_score,
          facility_score,
          evidence_score,
          tacit_score,
          readiness,
          efficiency,
          std::move(explanations)};
}

} // namespace stellar::core
