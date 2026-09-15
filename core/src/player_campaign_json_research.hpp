#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace stellar::core::player_json_detail {

using Json = nlohmann::ordered_json;

inline int maturity_number(std::string_view value) {
  constexpr std::string_view names[] = {{},
                                        "rumored",
                                        "hypothesized",
                                        "investigable",
                                        "experimental",
                                        "demonstrated",
                                        "engineering",
                                        "mature",
                                        "archived"};
  for (int index = 1; index < 9; ++index) {
    if (names[index] == value)
      return index;
  }
  throw nlohmann::json::type_error::create(
      302, "unknown ResearchMaturity value", nullptr);
}

inline Json rename_initials(const Json &value, bool uppercase,
                            bool dictionary_keys = false) {
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto &item : value)
      result.push_back(rename_initials(item, uppercase));
    return result;
  }
  if (!value.is_object())
    return value;
  Json result = Json::object();
  for (const auto &[key, item] : value.items()) {
    auto renamed = key;
    if (!dictionary_keys && !renamed.empty()) {
      const auto first = static_cast<unsigned char>(renamed.front());
      renamed.front() = static_cast<char>(uppercase ? std::toupper(first)
                                                    : std::tolower(first));
    }
    const bool dictionary =
        renamed == "Pressures" || renamed == "ApplicabilityContexts" ||
        renamed == "MetricSignals" || renamed == "DomainPriorities" ||
        renamed == "FieldPriorities" || renamed == "ProblemPriorities" ||
        renamed == "CapabilityPriorities" || renamed == "CultureAxes" ||
        renamed == "pressures" || renamed == "applicabilityContexts" ||
        renamed == "metricSignals" || renamed == "domainPriorities" ||
        renamed == "fieldPriorities" || renamed == "problemPriorities" ||
        renamed == "capabilityPriorities" || renamed == "cultureAxes";
    if (uppercase && (renamed == "Maturity" || renamed == "Stage") &&
        item.is_string())
      result[renamed] = maturity_number(item.get<std::string>());
    else
      result[renamed] = rename_initials(item, uppercase, dictionary);
  }
  return result;
}

inline AdaptiveResearchCampaignSnapshot decode_research(const Json &value) {
  AdaptiveResearchCampaignSnapshot result;
  result.schema_version = value.at("SchemaVersion").get<int>();
  result.catalog_id = value.at("CatalogId").get<std::string>();
  for (const auto &item : value.at("Civilizations")) {
    AdaptiveResearchCampaignCivilizationSnapshot civilization;
    civilization.civilization_id = item.at("CivilizationId").get<int>();
    civilization.species_id = item.at("SpeciesId").get<std::string>();
    civilization.reference_profile_id =
        item.at("ReferenceProfileId").get<std::string>();
    civilization.applicability_context_id =
        item.at("ApplicabilityContextId").get<std::string>();
    const auto lower = rename_initials(item.at("Research"), false);
    civilization.research =
        detail::decode_adaptive_research_snapshot_v5_dto(lower.dump());
    const auto funding_values = item.find("ProjectFunding");
    if (funding_values != item.end() && !funding_values->is_null()) {
      for (const auto &funding : *funding_values) {
        civilization.project_funding.push_back(
            {funding.at("NodeId").get<std::string>(),
             funding.at("ReservedMilestoneCredits").get<double>(),
             funding.at("ConsumedMilestoneCredits").get<double>(),
             funding.at("AuthorizationCredits").get<double>()});
      }
    }
    result.civilizations.push_back(std::move(civilization));
  }
  return result;
}

// Player17 persists these schema-defined enum fields as numbers, while the
// standalone Adaptive Research DTO deliberately uses readable enum names. Keep
// this conversion at the typed snapshot paths so arbitrary string fields and
// dictionary values retain their standalone representation.
inline void canonicalize_player_research_enums(
    Json &research, const AdaptiveResearchStateSnapshotV5 &snapshot) {
  auto &records = research.at("Outcomes").at("RecentRecords");
  if (records.size() != snapshot.outcomes.recent_records.size())
    throw std::logic_error(
        "Research outcome snapshot record count changed while encoding.");
  for (std::size_t index = 0; index < records.size(); ++index)
    records.at(index).at("Outcome") = static_cast<int>(
        snapshot.outcomes.recent_records.at(index).outcome);

  auto &v4 = research.at("Research");
  auto &assessments = v4.at("ForeignAssessments");
  if (assessments.size() != snapshot.research.foreign_assessments.size())
    throw std::logic_error(
        "Research foreign assessment count changed while encoding.");
  for (std::size_t index = 0; index < assessments.size(); ++index) {
    const auto &source = snapshot.research.foreign_assessments.at(index);
    auto &target = assessments.at(index);
    target.at("Understanding") = static_cast<int>(source.understanding);
    target.at("Operability") = static_cast<int>(source.operability);
    target.at("Reproduction") = static_cast<int>(source.reproduction);
    target.at("Adaptation") = static_cast<int>(source.adaptation);
  }

  auto &assets = v4.at("Research")
                     .at("Research")
                     .at("Expertise")
                     .at("TacitAssets");
  const auto &source_assets =
      snapshot.research.research.research.expertise.tacit_assets;
  if (assets.size() != source_assets.size())
    throw std::logic_error(
        "Research tacit asset count changed while encoding.");
  for (std::size_t index = 0; index < assets.size(); ++index) {
    const auto &source = source_assets.at(index);
    auto &target = assets.at(index);
    target.at("ScopeKind") = static_cast<int>(source.scope_kind);
    target.at("AssimilationStage") =
        static_cast<int>(source.assimilation_stage);
  }
}

inline Json encode_research(const AdaptiveResearchCampaignSnapshot &snapshot) {
  Json civilizations = Json::array();
  for (const auto &value : snapshot.civilizations) {
    Json funding = Json::array();
    for (const auto &entry : value.project_funding) {
      funding.push_back(
          {{"NodeId", entry.node_id},
           {"ReservedMilestoneCredits", entry.reserved_milestone_credits},
           {"ConsumedMilestoneCredits", entry.consumed_milestone_credits},
           {"AuthorizationCredits", entry.authorization_credits}});
    }
    const auto standalone = Json::parse(
        detail::encode_adaptive_research_snapshot_v5_dto(value.research));
    auto research = rename_initials(standalone, true);
    canonicalize_player_research_enums(research, value.research);
    civilizations.push_back(
        {{"CivilizationId", value.civilization_id},
         {"SpeciesId", value.species_id},
         {"ReferenceProfileId", value.reference_profile_id},
         {"ApplicabilityContextId", value.applicability_context_id},
         {"Research", std::move(research)},
         {"ProjectFunding", funding}});
  }
  return {{"SchemaVersion", snapshot.schema_version},
          {"CatalogId", snapshot.catalog_id},
          {"Civilizations", civilizations}};
}

} // namespace stellar::core::player_json_detail
