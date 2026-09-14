#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>

#include <nlohmann/json.hpp>

#include <cctype>

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
    const auto research = Json::parse(
        detail::encode_adaptive_research_snapshot_v5_dto(value.research));
    civilizations.push_back(
        {{"CivilizationId", value.civilization_id},
         {"SpeciesId", value.species_id},
         {"ReferenceProfileId", value.reference_profile_id},
         {"ApplicabilityContextId", value.applicability_context_id},
         {"Research", rename_initials(research, true)},
         {"ProjectFunding", funding}});
  }
  return {{"SchemaVersion", snapshot.schema_version},
          {"CatalogId", snapshot.catalog_id},
          {"Civilizations", civilizations}};
}

} // namespace stellar::core::player_json_detail
