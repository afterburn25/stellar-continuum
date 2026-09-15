#include <stellar/core/adaptive_research_expertise_service.hpp>

#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {

using ExpertiseWriter = detail::AdaptiveResearchExpertiseStateWriter;
using StateWriter = detail::AdaptiveResearchStateWriter;

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

template <class Range, class Value>
bool contains(const Range &values, const Value &value) noexcept {
  return std::ranges::find(values, value) != values.end();
}

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

ResearchCompetenceVector divide(ResearchCompetenceVector value,
                                double divisor) noexcept {
  return {value.theoretical / divisor, value.experimental / divisor,
          value.engineering / divisor};
}

ResearchCompetenceVector multiply(ResearchCompetenceVector value,
                                  double factor) noexcept {
  return {value.theoretical * factor, value.experimental * factor,
          value.engineering * factor};
}

double diminishing_gain(double current, double raw_gain,
                        double minimum_factor) noexcept {
  if (raw_gain <= 0.0)
    return current;
  const double factor = dotnet_max(minimum_factor, 1.0 - current / 120.0);
  return dotnet_clamp(current + raw_gain * factor, 0.0, 100.0);
}

double atrophy_component(double current, double floor, double inactivity_years,
                         double grace_years, double annual_rate,
                         double elapsed_years) noexcept {
  if (inactivity_years <= grace_years || current <= floor)
    return current;
  return dotnet_max(floor, current - annual_rate * elapsed_years);
}

bool approximately_equal(const ResearchCompetenceVector &left,
                         const ResearchCompetenceVector &right) noexcept {
  return std::abs(left.theoretical - right.theoretical) < 0.000001 &&
         std::abs(left.experimental - right.experimental) < 0.000001 &&
         std::abs(left.engineering - right.engineering) < 0.000001;
}

const ResearchStagePracticeGain *
find_stage_gain(const AdaptiveResearchExpertiseCatalog &catalog,
                ResearchMaturity stage) noexcept {
  const auto &gains = catalog.runtime_policy().stage_practice_gain;
  const auto found =
      std::ranges::find(gains, stage, &ResearchStagePracticeGain::stage);
  return found == gains.end() ? nullptr : &*found;
}

const ResearchTacitAssetTypeDefinition &
tacit_type(const AdaptiveResearchExpertiseCatalog &catalog,
           std::string_view id) {
  const auto values = catalog.tacit_asset_types();
  const auto found =
      std::ranges::find(values, id, &ResearchTacitAssetTypeDefinition::id);
  if (found == values.end())
    throw std::out_of_range("The given key '" + std::string(id) +
                            "' was not present in the dictionary.");
  return *found;
}

void validate_field(const AdaptiveResearchExpertiseCatalog &catalog,
                    std::string_view field_id) {
  if (!std::ranges::any_of(catalog.fields(), [&](const auto &field) {
        return field.id == field_id;
      }))
    throw std::invalid_argument("Unknown research knowledge field '" +
                                std::string(field_id) +
                                "'. (Parameter 'fieldId')");
}

void validate_institution(const AdaptiveResearchExpertiseCatalog &catalog,
                          std::string_view institution_id) {
  if (!std::ranges::any_of(
          catalog.institutions(), [&](const auto &institution) {
            return institution.institution_archetype_id == institution_id;
          }))
    throw std::invalid_argument("Unknown research institution archetype '" +
                                std::string(institution_id) +
                                "'. (Parameter 'institutionArchetypeId')");
}

void validate_tacit_type(const AdaptiveResearchExpertiseCatalog &catalog,
                         std::string_view asset_type_id) {
  if (!std::ranges::any_of(catalog.tacit_asset_types(), [&](const auto &type) {
        return type.id == asset_type_id;
      }))
    throw std::invalid_argument("Unknown tacit knowledge asset type '" +
                                std::string(asset_type_id) +
                                "'. (Parameter 'assetTypeId')");
}

void synchronize_institution_capacity(
    AdaptiveResearchCivilizationState &state,
    const AdaptiveResearchExpertiseCatalog &catalog) {
  const auto &expertise = state.expertise();
  std::vector<const ResearchInstitutionRuntimeState *> active;
  for (const auto &institution : expertise.institutions())
    if (institution.is_active())
      active.push_back(&institution);

  double total_labs = 0.0;
  for (const auto *institution : active)
    total_labs += catalog.get_institution(institution->institution_archetype_id)
                      .effective_lab_units *
                  institution->active_count;
  StateWriter::set_total_effective_research_labs(state, total_labs);

  std::vector<std::string> capabilities;
  std::unordered_set<std::string> seen;
  for (const auto *institution : active)
    for (const auto &capability :
         catalog.get_institution(institution->institution_archetype_id)
             .facility_capability_ids)
      if (seen.insert(capability).second)
        capabilities.push_back(capability);
  StateWriter::set_facility_capabilities(state, capabilities);
}

double assimilation_factor(const AdaptiveResearchExpertiseCatalog &catalog,
                           ResearchTacitAssimilationStage stage) {
  const auto &values = catalog.runtime_policy().tacit_assimilation_factors;
  const auto found =
      std::ranges::find(values, stage, &ResearchTacitAssimilationFactor::stage);
  if (found == values.end())
    throw std::out_of_range("The given key '" +
                            std::to_string(static_cast<int>(stage)) +
                            "' was not present in the dictionary.");
  return found->factor;
}

} // namespace

AdaptiveResearchExpertiseService::AdaptiveResearchExpertiseService(
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog,
    const AdaptiveResearchReadinessCalculator &readiness) noexcept
    : catalog_(&catalog), expertise_catalog_(&expertise_catalog),
      readiness_(&readiness) {}

ResearchReadinessBreakdown
AdaptiveResearchExpertiseService::calculate_project_readiness(
    const AdaptiveResearchCivilizationState &state, std::string_view node_id,
    ResearchMaturity stage, double assigned_effective_labs,
    std::optional<std::string_view> target_context) const {
  return readiness_->calculate(state, state.expertise(), node_id, stage,
                               assigned_effective_labs, target_context);
}

void AdaptiveResearchExpertiseService::seed_field_competence(
    AdaptiveResearchCivilizationState &state, std::string_view field_id_view,
    ResearchCompetenceVector value, double activity_year) const {
  const std::string field_id(field_id_view);
  validate_field(*expertise_catalog_, field_id);
  auto &expertise = StateWriter::expertise(state);
  const auto current = expertise.get_field(field_id);
  const ResearchCompetenceVector seeded{
      dotnet_max(current.current.theoretical, value.theoretical),
      dotnet_max(current.current.experimental, value.experimental),
      dotnet_max(current.current.engineering, value.engineering)};
  const ResearchCompetenceVector peak{
      dotnet_max(current.historical_peak.theoretical, seeded.theoretical),
      dotnet_max(current.historical_peak.experimental, seeded.experimental),
      dotnet_max(current.historical_peak.engineering, seeded.engineering)};
  ExpertiseWriter::set_field(
      expertise,
      {field_id, seeded, peak,
       dotnet_max(current.last_theoretical_activity_year, activity_year),
       dotnet_max(current.last_experimental_activity_year, activity_year),
       dotnet_max(current.last_engineering_activity_year, activity_year), 0});
  StateWriter::mark_view_dirty(state);
}

void AdaptiveResearchExpertiseService::set_institution(
    AdaptiveResearchCivilizationState &state,
    std::string_view institution_instance_id_view,
    std::string_view institution_archetype_id_view, int total_count,
    int active_count, std::optional<std::string_view> context_id_view) const {
  const std::string institution_instance_id(institution_instance_id_view);
  const std::string institution_archetype_id(institution_archetype_id_view);
  const std::optional<std::string> context_id =
      context_id_view ? std::optional<std::string>(*context_id_view)
                      : std::nullopt;
  validate_institution(*expertise_catalog_, institution_archetype_id);
  auto &expertise = StateWriter::expertise(state);
  ExpertiseWriter::set_institution(
      expertise, {institution_instance_id, institution_archetype_id, context_id,
                  total_count, active_count, 0});

  synchronize_institution_capacity(state, *expertise_catalog_);
  StateWriter::mark_view_dirty(state);
}

bool AdaptiveResearchExpertiseService::remove_institution(
    AdaptiveResearchCivilizationState &state,
    std::string_view institution_instance_id_view) const {
  const std::string institution_instance_id(institution_instance_id_view);
  auto &expertise = StateWriter::expertise(state);
  if (!ExpertiseWriter::remove_institution(expertise, institution_instance_id))
    return false;

  synchronize_institution_capacity(state, *expertise_catalog_);
  StateWriter::mark_view_dirty(state);
  return true;
}

void AdaptiveResearchExpertiseService::set_tacit_asset(
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
  const std::optional<std::string> context_id =
      context_id_view ? std::optional<std::string>(*context_id_view)
                      : std::nullopt;

  validate_tacit_type(*expertise_catalog_, asset_type_id);
  if (blank(scope_ref))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'scopeRef')");
  switch (scope_kind) {
  case ResearchTacitScopeKind::knowledge_field:
    validate_field(*expertise_catalog_, scope_ref);
    break;
  case ResearchTacitScopeKind::technology_node:
    if (!catalog_->find_node(scope_ref))
      throw std::invalid_argument("Unknown research node '" + scope_ref +
                                  "' for tacit asset scope. (Parameter "
                                  "'scopeRef')");
    break;
  case ResearchTacitScopeKind::solution_family:
    if (!std::ranges::any_of(catalog_->nodes(), [&](const auto &node) {
          return node.solution_family == scope_ref;
        }))
      throw std::invalid_argument(
          "Unknown research solution family '" + scope_ref +
          "' for tacit asset scope. (Parameter 'scopeRef')");
    break;
  case ResearchTacitScopeKind::facility_or_process:
    if (!std::ranges::any_of(
            expertise_catalog_->institutions(), [&](const auto &institution) {
              return institution.institution_archetype_id == scope_ref ||
                     contains(institution.facility_capability_ids, scope_ref);
            }))
      throw std::invalid_argument(
          "Unknown research facility/process '" + scope_ref +
          "' for tacit asset scope. (Parameter 'scopeRef')");
    break;
  default:
    break;
  }

  ExpertiseWriter::set_tacit_asset(
      StateWriter::expertise(state),
      {asset_id, asset_type_id, scope_kind, scope_ref, assimilation_stage,
       depth, availability, translation_context_quality, training_continuity,
       provenance, context_id, 0});
  StateWriter::mark_view_dirty(state);
}

bool AdaptiveResearchExpertiseService::remove_tacit_asset(
    AdaptiveResearchCivilizationState &state,
    std::string_view asset_id_view) const {
  const std::string asset_id(asset_id_view);
  const bool changed = ExpertiseWriter::remove_tacit_asset(
      StateWriter::expertise(state), asset_id);
  if (changed)
    StateWriter::mark_view_dirty(state);
  return changed;
}

void AdaptiveResearchExpertiseService::apply_completed_stage_practice(
    AdaptiveResearchCivilizationState &state, std::string_view node_id_view,
    ResearchMaturity completed_stage, double activity_year) const {
  const std::string node_id(node_id_view);
  const auto *gain = find_stage_gain(*expertise_catalog_, completed_stage);
  if (!gain)
    return;
  const auto &node = catalog_->get_node(node_id);
  if (node.knowledge_fields.empty())
    return;

  auto &expertise = StateWriter::expertise(state);
  const double field_count = static_cast<double>(node.knowledge_fields.size());
  for (const auto &field_id : node.knowledge_fields) {
    const auto apply_gain = [&](const std::string &target_field,
                                ResearchCompetenceVector raw_gain) {
      validate_field(*expertise_catalog_, target_field);
      const auto existing = expertise.get_field(target_field);
      const auto &policy = expertise_catalog_->runtime_policy();
      const ResearchCompetenceVector current{
          diminishing_gain(existing.current.theoretical, raw_gain.theoretical,
                           policy.minimum_gain_factor),
          diminishing_gain(existing.current.experimental, raw_gain.experimental,
                           policy.minimum_gain_factor),
          diminishing_gain(existing.current.engineering, raw_gain.engineering,
                           policy.minimum_gain_factor)};
      ExpertiseWriter::set_field(
          expertise,
          {target_field,
           current,
           {dotnet_max(existing.historical_peak.theoretical,
                       current.theoretical),
            dotnet_max(existing.historical_peak.experimental,
                       current.experimental),
            dotnet_max(existing.historical_peak.engineering,
                       current.engineering)},
           raw_gain.theoretical > 0.0 ? activity_year
                                      : existing.last_theoretical_activity_year,
           raw_gain.experimental > 0.0
               ? activity_year
               : existing.last_experimental_activity_year,
           raw_gain.engineering > 0.0 ? activity_year
                                      : existing.last_engineering_activity_year,
           0});
    };

    const auto direct_gain = divide(gain->gain, field_count);
    apply_gain(field_id, direct_gain);
    const auto related_gain = multiply(
        direct_gain,
        expertise_catalog_->runtime_policy().related_field_transfer_fraction);
    for (const auto &related_id :
         expertise_catalog_->get_field(field_id).related_field_ids)
      apply_gain(related_id, related_gain);
  }
  StateWriter::mark_view_dirty(state);
}

void AdaptiveResearchExpertiseService::apply_competence_atrophy(
    AdaptiveResearchCivilizationState &state, double current_year,
    double elapsed_years) const {
  if (elapsed_years <= 0.0 || !std::isfinite(elapsed_years))
    return;

  auto &expertise = StateWriter::expertise(state);
  const std::vector<ResearchFieldCompetenceRuntimeState> fields(
      expertise.field_competence().begin(), expertise.field_competence().end());
  const auto &policy = expertise_catalog_->runtime_policy();
  for (const auto &field : fields) {
    ResearchCompetenceVector floor{};
    if (std::ranges::any_of(state.node_states(), [&](const auto &node_state) {
          return node_state.counts_as_established_knowledge() &&
                 contains(
                     catalog_->get_node(node_state.node_id).knowledge_fields,
                     field.field_id);
        }))
      floor.theoretical =
          field.historical_peak.theoretical *
          policy.established_knowledge_theoretical_floor_fraction;

    for (const auto &asset : expertise.tacit_assets()) {
      const auto &type = tacit_type(*expertise_catalog_, asset.asset_type_id);
      bool supports = false;
      switch (asset.scope_kind) {
      case ResearchTacitScopeKind::knowledge_field:
        supports = asset.scope_ref == field.field_id;
        break;
      case ResearchTacitScopeKind::technology_node: {
        const auto *node = catalog_->find_node(asset.scope_ref);
        supports = node && contains(node->knowledge_fields, field.field_id);
        break;
      }
      case ResearchTacitScopeKind::solution_family:
        supports =
            std::ranges::any_of(catalog_->nodes(), [&](const auto &node) {
              return node.solution_family == asset.scope_ref &&
                     contains(node.knowledge_fields, field.field_id);
            });
        break;
      case ResearchTacitScopeKind::foreign_lineage:
        supports = field.field_id == "xenoscience";
        break;
      case ResearchTacitScopeKind::facility_or_process:
        supports = std::ranges::any_of(
            expertise_catalog_->institutions(), [&](const auto &institution) {
              return (institution.institution_archetype_id == asset.scope_ref ||
                      contains(institution.facility_capability_ids,
                               asset.scope_ref)) &&
                     contains(institution.specialized_field_ids,
                              field.field_id);
            });
        break;
      default:
        break;
      }
      if (!supports)
        continue;
      const double score =
          asset.depth *
          assimilation_factor(*expertise_catalog_, asset.assimilation_stage) *
          asset.availability * asset.translation_context_quality *
          asset.training_continuity * policy.preservation_floor_fraction;
      if (contains(type.supported_components,
                   ResearchCompetenceComponent::theoretical))
        floor.theoretical = dotnet_max(floor.theoretical, score);
      if (contains(type.supported_components,
                   ResearchCompetenceComponent::experimental))
        floor.experimental = dotnet_max(floor.experimental, score);
      if (contains(type.supported_components,
                   ResearchCompetenceComponent::engineering))
        floor.engineering = dotnet_max(floor.engineering, score);
    }
    floor.theoretical =
        dotnet_min(floor.theoretical, field.historical_peak.theoretical);
    floor.experimental =
        dotnet_min(floor.experimental, field.historical_peak.experimental);
    floor.engineering =
        dotnet_min(floor.engineering, field.historical_peak.engineering);

    const ResearchCompetenceVector updated{
        atrophy_component(field.current.theoretical, floor.theoretical,
                          current_year - field.last_theoretical_activity_year,
                          policy.atrophy_grace_years,
                          policy.annual_atrophy_rates.theoretical,
                          elapsed_years),
        atrophy_component(field.current.experimental, floor.experimental,
                          current_year - field.last_experimental_activity_year,
                          policy.atrophy_grace_years,
                          policy.annual_atrophy_rates.experimental,
                          elapsed_years),
        atrophy_component(field.current.engineering, floor.engineering,
                          current_year - field.last_engineering_activity_year,
                          policy.atrophy_grace_years,
                          policy.annual_atrophy_rates.engineering,
                          elapsed_years)};
    if (!approximately_equal(updated, field.current)) {
      auto replacement = field;
      replacement.current = updated;
      ExpertiseWriter::set_field(expertise, std::move(replacement));
    }
  }
  StateWriter::mark_view_dirty(state);
}

} // namespace stellar::core
