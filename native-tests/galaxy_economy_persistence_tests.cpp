#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/galaxy_economy_persistence.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

namespace {
using Json = nlohmann::ordered_json;
using namespace stellar::core;

void check(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string sha256(const std::string &bytes) {
  const auto digest = detail::adaptive_research_sha256(std::span(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  for (const auto byte : digest) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

double number(const Json &value) {
  if (value.is_number()) return value.get<double>();
  const auto name = value.get<std::string>();
  if (name == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (name == "Infinity") return std::numeric_limits<double>::infinity();
  if (name == "-Infinity") return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("Unknown named floating-point value '" + name + "'.");
}

void equal_number(double actual, double expected, const std::string &field) {
  check((std::isnan(actual) && std::isnan(expected)) || actual == expected,
        field);
  if (actual == 0 && expected == 0)
    check(std::signbit(actual) == std::signbit(expected), field + " sign");
}

template <class T>
std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>(value.get<T>());
}

std::optional<double> optional_number(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<double>(number(value));
}

SurfaceBuilding surface_building(const Json &value) {
  SurfaceBuilding result;
  result.id = value.at("Id");
  result.type_id = value.at("TypeId");
  result.x = static_cast<float>(number(value.at("X")));
  result.z = static_cast<float>(number(value.at("Z")));
  result.rotation_degrees =
      static_cast<float>(number(value.at("RotationDegrees")));
  result.industry_progress = number(value.at("IndustryProgress"));
  result.is_complete = value.at("IsComplete");
  result.is_enabled = value.at("IsEnabled");
  result.pending_upgrade_type_id =
      optional_value<std::string>(value.at("PendingUpgradeTypeId"));
  result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
  result.operating_priority = value.at("OperatingPriority");
  result.condition = number(value.at("Condition"));
  result.stored_power_days = number(value.at("StoredPowerDays"));
  return result;
}

void check_surface(const SurfaceBuilding &actual, const Json &expected,
                   const std::string &label) {
  check(actual.id == expected.at("Id").get<int>(), label + ".Id");
  check(actual.type_id == expected.at("TypeId").get<std::string>(),
        label + ".TypeId");
  equal_number(actual.x, number(expected.at("X")), label + ".X");
  equal_number(actual.z, number(expected.at("Z")), label + ".Z");
  equal_number(actual.rotation_degrees,
               number(expected.at("RotationDegrees")),
               label + ".RotationDegrees");
  equal_number(actual.industry_progress,
               number(expected.at("IndustryProgress")),
               label + ".IndustryProgress");
  check(actual.is_complete == expected.at("IsComplete").get<bool>(),
        label + ".IsComplete");
  check(actual.is_enabled == expected.at("IsEnabled").get<bool>(),
        label + ".IsEnabled");
  check(actual.pending_upgrade_type_id ==
            optional_value<std::string>(expected.at("PendingUpgradeTypeId")),
        label + ".PendingUpgradeTypeId");
  equal_number(actual.upgrade_days_remaining,
               number(expected.at("UpgradeDaysRemaining")),
               label + ".UpgradeDaysRemaining");
  check(actual.operating_priority ==
            expected.at("OperatingPriority").get<int>(),
        label + ".OperatingPriority");
  equal_number(actual.condition, number(expected.at("Condition")),
               label + ".Condition");
  equal_number(actual.stored_power_days,
               number(expected.at("StoredPowerDays")),
               label + ".StoredPowerDays");
}

ColonySaveDto colony_dto(const Json &value) {
  ColonySaveDto result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.system_id = value.at("SystemId");
  result.planetary_body_id = optional_value<int>(value.at("PlanetaryBodyId"));
  result.name = value.at("Name");
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id =
      optional_value<std::string>(value.at("PopulationSpeciesId"));
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials =
      number(value.at("StoredExtractedMaterials"));
  result.remaining_extractable_materials =
      optional_number(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = optional_value<int>(value.at("SurfaceHubLevel"));
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  if (!value.at("SurfaceBuildings").is_null()) {
    result.surface_buildings.emplace();
    for (const auto &building : value.at("SurfaceBuildings"))
      result.surface_buildings->push_back(surface_building(building));
  }
  return result;
}

Colony colony(const Json &value) {
  Colony result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.system_id = value.at("SystemId");
  result.planetary_body_id = optional_value<int>(value.at("PlanetaryBodyId"));
  result.name = value.at("Name");
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id = value.at("PopulationSpeciesId");
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials =
      number(value.at("StoredExtractedMaterials"));
  result.remaining_extractable_materials =
      optional_number(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = value.at("SurfaceHubLevel");
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &building : value.at("SurfaceBuildings"))
    result.surface_buildings.push_back(surface_building(building));
  return result;
}

void check_colony(const Colony &actual, const Json &expected,
                  const std::string &label) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.civilization_id == expected.at("CivilizationId").get<int>() &&
            actual.system_id == expected.at("SystemId").get<int>(),
        label + " identity");
  check(actual.planetary_body_id ==
            optional_value<int>(expected.at("PlanetaryBodyId")),
        label + ".PlanetaryBodyId");
  check(actual.name == expected.at("Name").get<std::string>() &&
            static_cast<int>(actual.kind) == expected.at("Kind").get<int>() &&
            actual.population_species_id ==
                expected.at("PopulationSpeciesId").get<std::string>(),
        label + " description");
#define CHECK_NUMBER(member, field)                                             \
  equal_number(actual.member, number(expected.at(field)), label + "." field)
  CHECK_NUMBER(population_millions, "PopulationMillions");
  CHECK_NUMBER(infrastructure, "Infrastructure");
  CHECK_NUMBER(stability, "Stability");
  CHECK_NUMBER(stored_food_population_days_millions,
               "StoredFoodPopulationDaysMillions");
  CHECK_NUMBER(stored_water_population_days_millions,
               "StoredWaterPopulationDaysMillions");
  CHECK_NUMBER(stored_extracted_materials, "StoredExtractedMaterials");
  CHECK_NUMBER(surface_hub_upgrade_days_remaining,
               "SurfaceHubUpgradeDaysRemaining");
#undef CHECK_NUMBER
  const auto remaining = optional_number(expected.at("RemainingExtractableMaterials"));
  check(actual.remaining_extractable_materials.has_value() ==
            remaining.has_value(),
        label + ".RemainingExtractableMaterials presence");
  if (remaining)
    equal_number(*actual.remaining_extractable_materials, *remaining,
                 label + ".RemainingExtractableMaterials");
  check(actual.surface_hub_level == expected.at("SurfaceHubLevel").get<int>(),
        label + ".SurfaceHubLevel");
  check(actual.surface_buildings.size() ==
            expected.at("SurfaceBuildings").size(),
        label + ".SurfaceBuildings size");
  for (std::size_t index = 0; index < actual.surface_buildings.size(); ++index)
    check_surface(actual.surface_buildings[index],
                  expected.at("SurfaceBuildings")[index],
                  label + ".SurfaceBuildings");
}

void check_colony_dto(const ColonySaveDto &actual, const Json &expected,
                      const std::string &label) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.civilization_id == expected.at("CivilizationId").get<int>() &&
            actual.system_id == expected.at("SystemId").get<int>(),
        label + " identity");
  check(actual.planetary_body_id ==
            optional_value<int>(expected.at("PlanetaryBodyId")) &&
            actual.name == expected.at("Name").get<std::string>() &&
            static_cast<int>(actual.kind) == expected.at("Kind").get<int>() &&
            actual.population_species_id ==
                optional_value<std::string>(expected.at("PopulationSpeciesId")),
        label + " description");
#define CHECK_NUMBER(member, field)                                             \
  equal_number(actual.member, number(expected.at(field)), label + "." field)
  CHECK_NUMBER(population_millions, "PopulationMillions");
  CHECK_NUMBER(infrastructure, "Infrastructure");
  CHECK_NUMBER(stability, "Stability");
  CHECK_NUMBER(stored_food_population_days_millions,
               "StoredFoodPopulationDaysMillions");
  CHECK_NUMBER(stored_water_population_days_millions,
               "StoredWaterPopulationDaysMillions");
  CHECK_NUMBER(stored_extracted_materials, "StoredExtractedMaterials");
  CHECK_NUMBER(surface_hub_upgrade_days_remaining,
               "SurfaceHubUpgradeDaysRemaining");
#undef CHECK_NUMBER
  const auto remaining = optional_number(expected.at("RemainingExtractableMaterials"));
  check(actual.remaining_extractable_materials.has_value() == remaining.has_value(),
        label + ".RemainingExtractableMaterials presence");
  if (remaining)
    equal_number(*actual.remaining_extractable_materials, *remaining,
                 label + ".RemainingExtractableMaterials");
  check(actual.surface_hub_level ==
            optional_value<int>(expected.at("SurfaceHubLevel")),
        label + ".SurfaceHubLevel");
  if (expected.at("SurfaceBuildings").is_null()) {
    check(!actual.surface_buildings, label + ".SurfaceBuildings null");
  } else {
    check(actual.surface_buildings &&
              actual.surface_buildings->size() ==
                  expected.at("SurfaceBuildings").size(),
          label + ".SurfaceBuildings size");
    for (std::size_t index = 0; index < actual.surface_buildings->size(); ++index)
      check_surface((*actual.surface_buildings)[index],
                    expected.at("SurfaceBuildings")[index],
                    label + ".SurfaceBuildings");
  }
}

EconomySaveDto economy_dto(const Json &value) {
  EconomySaveDto result;
  result.civilization_id = value.at("CivilizationId");
  result.credits = number(value.at("Credits"));
  result.industry = number(value.at("Industry"));
  result.science = number(value.at("Science"));
  result.last_credits_per_second = number(value.at("LastCreditsPerSecond"));
  result.last_industry_per_second = number(value.at("LastIndustryPerSecond"));
  result.last_science_per_second = number(value.at("LastSciencePerSecond"));
  result.last_research_spending_per_day =
      number(value.at("LastResearchSpendingPerDay"));
  result.last_research_funding_fraction =
      number(value.at("LastResearchFundingFraction"));
  result.operating_arrears = number(value.at("OperatingArrears"));
  result.last_base_operations_funding_fraction =
      number(value.at("LastBaseOperationsFundingFraction"));
  if (!value.at("IndustryPriority").is_null())
    result.industry_priority = static_cast<IndustryPriority>(
        value.at("IndustryPriority").get<int>());
  return result;
}

CivilizationEconomy economy(const Json &value) {
  const auto dto = economy_dto(value);
  return {dto.civilization_id,
          dto.credits,
          dto.industry,
          dto.science,
          dto.last_credits_per_second,
          dto.last_industry_per_second,
          dto.last_science_per_second,
          dto.last_research_spending_per_day,
          dto.last_research_funding_fraction,
          dto.operating_arrears,
          dto.last_base_operations_funding_fraction,
          dto.industry_priority};
}

void check_economy_values(const CivilizationEconomy &actual,
                          const Json &expected, const std::string &label) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        label + ".CivilizationId");
#define CHECK_NUMBER(member, field)                                             \
  equal_number(actual.member, number(expected.at(field)), label + "." field)
  CHECK_NUMBER(credits, "Credits");
  CHECK_NUMBER(industry, "Industry");
  CHECK_NUMBER(science, "Science");
  CHECK_NUMBER(last_credits_per_second, "LastCreditsPerSecond");
  CHECK_NUMBER(last_industry_per_second, "LastIndustryPerSecond");
  CHECK_NUMBER(last_science_per_second, "LastSciencePerSecond");
  CHECK_NUMBER(last_research_spending_per_day, "LastResearchSpendingPerDay");
  CHECK_NUMBER(last_research_funding_fraction, "LastResearchFundingFraction");
  CHECK_NUMBER(operating_arrears, "OperatingArrears");
  CHECK_NUMBER(last_base_operations_funding_fraction,
               "LastBaseOperationsFundingFraction");
#undef CHECK_NUMBER
  const auto expected_priority = expected.at("IndustryPriority").is_null()
      ? std::optional<IndustryPriority>{}
      : std::optional(static_cast<IndustryPriority>(
            expected.at("IndustryPriority").get<int>()));
  check(actual.industry_priority == expected_priority,
        label + ".IndustryPriority");
}

void check_economy_dto(const EconomySaveDto &actual, const Json &expected,
                       const std::string &label) {
  const CivilizationEconomy projected{
      actual.civilization_id,
      actual.credits,
      actual.industry,
      actual.science,
      actual.last_credits_per_second,
      actual.last_industry_per_second,
      actual.last_science_per_second,
      actual.last_research_spending_per_day,
      actual.last_research_funding_fraction,
      actual.operating_arrears,
      actual.last_base_operations_funding_fraction,
      actual.industry_priority};
  check_economy_values(projected, expected, label);
}

TechnologySaveDto technology_dto(const Json &value) {
  TechnologySaveDto result;
  result.civilization_id = value.at("CivilizationId");
  if (!value.at("CompletedTechnologyIds").is_null())
    result.completed_technology_ids =
        value.at("CompletedTechnologyIds").get<std::vector<std::string>>();
  result.active_research_id =
      optional_value<std::string>(value.at("ActiveResearchId"));
  result.active_research_progress = number(value.at("ActiveResearchProgress"));
  return result;
}

TechnologyState technology(const Json &value) {
  TechnologyState result;
  result.civilization_id = value.at("CivilizationId");
  for (const auto &id : value.at("CompletedTechnologyIds"))
    result.completed_technology_ids.insert(id.get<std::string>());
  result.active_research_id =
      optional_value<std::string>(value.at("ActiveResearchId"));
  result.active_research_progress = number(value.at("ActiveResearchProgress"));
  return result;
}

void check_technology(const TechnologyState &actual, const Json &expected,
                      const std::string &label) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        label + ".CivilizationId");
  const auto expected_ids =
      expected.at("CompletedTechnologyIds").get<std::vector<std::string>>();
  check(std::vector<std::string>(actual.completed_technology_ids.values().begin(),
                                 actual.completed_technology_ids.values().end()) ==
            expected_ids,
        label + ".CompletedTechnologyIds");
  check(actual.active_research_id ==
            optional_value<std::string>(expected.at("ActiveResearchId")),
        label + ".ActiveResearchId");
  equal_number(actual.active_research_progress,
               number(expected.at("ActiveResearchProgress")),
               label + ".ActiveResearchProgress");
}

void check_technology_dto(const TechnologySaveDto &actual,
                          const Json &expected, const std::string &label) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        label + ".CivilizationId");
  if (expected.at("CompletedTechnologyIds").is_null()) {
    check(!actual.completed_technology_ids,
          label + ".CompletedTechnologyIds null");
  } else {
    check(actual.completed_technology_ids == std::optional(
              expected.at("CompletedTechnologyIds")
                  .get<std::vector<std::string>>()),
          label + ".CompletedTechnologyIds");
  }
  check(actual.active_research_id ==
            optional_value<std::string>(expected.at("ActiveResearchId")),
        label + ".ActiveResearchId");
  equal_number(actual.active_research_progress,
               number(expected.at("ActiveResearchProgress")),
               label + ".ActiveResearchProgress");
}

ConstructionSaveDto construction_dto(const Json &value) {
  ConstructionSaveDto result;
  result.civilization_id = value.at("CivilizationId");
  if (!value.at("CompletedProjectIds").is_null())
    result.completed_project_ids =
        value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id =
      optional_value<std::string>(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  if (!value.at("QueuedProjects").is_null()) {
    result.queued_projects.emplace();
    for (const auto &order : value.at("QueuedProjects")) {
      if (order.is_null()) {
        result.queued_projects->push_back(std::nullopt);
      } else {
        result.queued_projects->push_back(QueuedConstructionProjectSaveDto{
            order.at("ProjectId"), number(order.at("AuthorizationCredits"))});
      }
    }
  }
  return result;
}

ConstructionState construction(const Json &value) {
  ConstructionState result;
  result.civilization_id = value.at("CivilizationId");
  result.completed_project_ids =
      value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id =
      optional_value<std::string>(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  for (const auto &order : value.at("QueuedProjects"))
    result.queued_projects.push_back(
        {order.at("ProjectId"), number(order.at("AuthorizationCredits"))});
  return result;
}

void check_construction(const ConstructionState &actual,
                        const Json &expected, const std::string &label) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        label + ".CivilizationId");
  check(actual.completed_project_ids ==
            expected.at("CompletedProjectIds").get<std::vector<std::string>>(),
        label + ".CompletedProjectIds");
  check(actual.active_project_id ==
            optional_value<std::string>(expected.at("ActiveProjectId")),
        label + ".ActiveProjectId");
  equal_number(actual.active_project_progress,
               number(expected.at("ActiveProjectProgress")),
               label + ".ActiveProjectProgress");
  equal_number(actual.active_project_authorization_credits,
               number(expected.at("ActiveProjectAuthorizationCredits")),
               label + ".ActiveProjectAuthorizationCredits");
  check(actual.queued_projects.size() == expected.at("QueuedProjects").size(),
        label + ".QueuedProjects size");
  for (std::size_t index = 0; index < actual.queued_projects.size(); ++index) {
    const auto &order = actual.queued_projects[index];
    const auto &wanted = expected.at("QueuedProjects")[index];
    check(order.project_id == wanted.at("ProjectId").get<std::string>(),
          label + ".QueuedProjects.ProjectId");
    equal_number(order.authorization_credits,
                 number(wanted.at("AuthorizationCredits")),
                 label + ".QueuedProjects.AuthorizationCredits");
  }
}

void check_construction_dto(const ConstructionSaveDto &actual,
                            const Json &expected, const std::string &label) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        label + ".CivilizationId");
  if (expected.at("CompletedProjectIds").is_null()) {
    check(!actual.completed_project_ids, label + ".CompletedProjectIds null");
  } else {
    check(actual.completed_project_ids == std::optional(
              expected.at("CompletedProjectIds")
                  .get<std::vector<std::string>>()),
          label + ".CompletedProjectIds");
  }
  check(actual.active_project_id ==
            optional_value<std::string>(expected.at("ActiveProjectId")),
        label + ".ActiveProjectId");
  equal_number(actual.active_project_progress,
               number(expected.at("ActiveProjectProgress")),
               label + ".ActiveProjectProgress");
  equal_number(actual.active_project_authorization_credits,
               number(expected.at("ActiveProjectAuthorizationCredits")),
               label + ".ActiveProjectAuthorizationCredits");
  if (expected.at("QueuedProjects").is_null()) {
    check(!actual.queued_projects, label + ".QueuedProjects null");
    return;
  }
  check(actual.queued_projects &&
            actual.queued_projects->size() == expected.at("QueuedProjects").size(),
        label + ".QueuedProjects size");
  for (std::size_t index = 0; index < actual.queued_projects->size(); ++index) {
    const auto &order = (*actual.queued_projects)[index];
    const auto &wanted = expected.at("QueuedProjects")[index];
    if (wanted.is_null()) {
      check(!order, label + ".QueuedProjects null item");
    } else {
      check(order && order->project_id ==
                         wanted.at("ProjectId").get<std::string>(),
            label + ".QueuedProjects.ProjectId");
      equal_number(order->authorization_credits,
                   number(wanted.at("AuthorizationCredits")),
                   label + ".QueuedProjects.AuthorizationCredits");
    }
  }
}

std::vector<Civilization> civilizations(const Json &values) {
  std::vector<Civilization> result;
  for (const auto &value : values) {
    Civilization civilization;
    civilization.id = value.at("Id");
    civilization.species_id = value.at("SpeciesId");
    result.push_back(std::move(civilization));
  }
  return result;
}

void check_civilizations(std::span<const Civilization> actual,
                         const Json &expected, const std::string &label) {
  check(actual.size() == expected.size(), label + " size");
  for (std::size_t index = 0; index < actual.size(); ++index) {
    check(actual[index].id == expected[index].at("Id").get<int>(),
          label + ".Id");
    check(actual[index].species_id ==
              expected[index].at("SpeciesId").get<std::string>(),
          label + ".SpeciesId");
  }
}

struct NativeError {
  std::string type;
  std::string message;
};

void capture_error(std::optional<NativeError> &error,
                   const GalaxyEconomyPersistenceDataError &caught) {
  error = NativeError{"InvalidDataException", caught.what()};
}

void capture_error(
    std::optional<NativeError> &error,
    const GalaxyEconomyPersistenceNullReferenceError &caught) {
  error = NativeError{"NullReferenceException", caught.what()};
}

template <class T, class Check>
void check_list(const std::vector<T> &actual, const Json &expected,
                const std::string &label, Check check_value) {
  check(actual.size() == expected.size(), label + " size");
  for (std::size_t index = 0; index < actual.size(); ++index)
    check_value(actual[index], expected[index], label + "[" +
                                            std::to_string(index) + "]");
}

} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3, "Expected source root and fixture path.");
    const auto source_root = std::filesystem::absolute(argv[1]);
    const auto fixture_path = std::filesystem::absolute(argv[2]);
    const auto fixture_bytes = read_bytes(fixture_path);
    check(sha256(fixture_bytes) ==
              "0FE35AC4D15C630893C4E3261707311644FAF0577F51B7CA9F7EC3C18709E589",
          "fixture hash");
    const auto fixture = Json::parse(fixture_bytes);
    check(fixture.at("Format") ==
              "stellar-galaxy-economy-persistence-actual-source-v1",
          "fixture format");
    check(fixture.at("RowCount").get<std::size_t>() ==
              fixture.at("Rows").size(),
          "fixture row count");
    const auto expected_source_hash =
        fixture.at("SourceSha256Before").get<std::string>();
    check(expected_source_hash ==
              fixture.at("SourceSha256After").get<std::string>(),
          "oracle source changed");
    const auto source_path =
        source_root / "src/Game/Persistence/CampaignSaveService.cs";
    check(sha256(read_bytes(source_path)) == expected_source_hash,
          "source hash before replay");
    check(fixture.at("SourceCaptureAliasProbe").at("Before") !=
              fixture.at("SourceCaptureAliasProbe").at("After"),
          "source alias probe did not observe source reference mutation");

    std::size_t replayed = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto operation = row.at("Operation").get<std::string>();
      const auto version = row.at("Version").get<int>();
      const auto civs = civilizations(row.at("Civilizations"));
      check(operation == "RestoreColonies" ||
                operation == "CaptureColonies" ||
                operation == "RestoreEconomies" ||
                operation == "CaptureEconomies" ||
                operation == "RestoreTechnologies" ||
                operation == "CaptureTechnologies" ||
                operation == "RestoreConstruction" ||
                operation == "CaptureConstruction",
            name + " operation");

      check_civilizations(civs, row.at("Civilizations"),
                          name + " civilizations before");
      std::optional<NativeError> error;
      if (operation == "RestoreColonies") {
        std::vector<ColonySaveDto> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(colony_dto(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_colony_dto);
        std::optional<std::vector<Colony>> result;
        try {
          result = restore_colony_dtos(input, civs, version);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_colony_dto);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_colony);
      } else if (operation == "CaptureColonies") {
        std::vector<Colony> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(colony(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_colony);
        std::optional<std::vector<ColonySaveDto>> result;
        try {
          result = capture_colony_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_colony);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_colony_dto);
      } else if (operation == "RestoreEconomies") {
        std::vector<EconomySaveDto> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(economy_dto(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_economy_dto);
        std::optional<std::vector<CivilizationEconomy>> result;
        try {
          result = restore_economy_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_economy_dto);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_economy_values);
      } else if (operation == "CaptureEconomies") {
        std::vector<CivilizationEconomy> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(economy(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_economy_values);
        std::optional<std::vector<EconomySaveDto>> result;
        try {
          result = capture_economy_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_economy_values);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_economy_dto);
      } else if (operation == "RestoreTechnologies") {
        std::vector<TechnologySaveDto> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(technology_dto(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_technology_dto);
        std::optional<std::vector<TechnologyState>> result;
        try {
          result = restore_technology_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_technology_dto);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_technology);
      } else if (operation == "CaptureTechnologies") {
        std::vector<TechnologyState> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(technology(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_technology);
        std::optional<std::vector<TechnologySaveDto>> result;
        try {
          result = capture_technology_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_technology);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_technology_dto);
      } else if (operation == "RestoreConstruction") {
        std::vector<ConstructionSaveDto> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(construction_dto(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_construction_dto);
        std::optional<std::vector<ConstructionState>> result;
        try {
          result = restore_construction_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_construction_dto);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_construction);
      } else {
        std::vector<ConstructionState> input;
        for (const auto &value : row.at("BeforeInput"))
          input.push_back(construction(value));
        check_list(input, row.at("BeforeInput"), name + " decoded before",
                   check_construction);
        std::optional<std::vector<ConstructionSaveDto>> result;
        try {
          result = capture_construction_dtos(input);
        } catch (const GalaxyEconomyPersistenceDataError &caught) {
          capture_error(error, caught);
        } catch (const GalaxyEconomyPersistenceNullReferenceError &caught) {
          capture_error(error, caught);
        }
        check_list(input, row.at("AfterInput"), name + " input after",
                   check_construction);
        if (result)
          check_list(*result, row.at("Result"), name + " result",
                     check_construction_dto);
      }

      check_civilizations(civs, row.at("Civilizations"),
                          name + " civilizations after");
      if (row.at("Error").is_null()) {
        check(!error, name + " unexpected native error");
        check(!row.at("Result").is_null(), name + " missing source result");
      } else {
        const auto expected_type = row.at("Error").at("Type").get<std::string>();
        check(expected_type == "InvalidDataException" ||
                  expected_type == "NullReferenceException",
              name + " unexpected source error category");
        check(error.has_value(), name + " expected native error");
        check(error->type == expected_type,
              name + " native error category");
        check(error->message ==
                  row.at("Error").at("Message").get<std::string>(),
              name + " native error message");
        check(row.at("Result").is_null(), name + " source error result");
      }
      ++replayed;
    }

    check(replayed == 57, "exact replay row accounting");
    check(sha256(read_bytes(source_path)) == expected_source_hash,
          "source hash after replay");

    const auto &rows = fixture.at("Rows");
    const auto find_row = [&](std::string_view wanted) -> const Json & {
      const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
        const auto name = row.at("Name").template get<std::string>();
        return name.compare(wanted) == 0;
      });
      check(found != rows.end(), "missing ownership fixture row");
      return *found;
    };
    const auto &colony_row = find_row("colony-capture-complete");
    auto live_colony = colony(colony_row.at("BeforeInput")[0]);
    const auto retained_colony =
        capture_colony_dtos(std::span(&live_colony, 1));
    live_colony.surface_buildings[0].type_id = "mutated";
    live_colony.surface_buildings.push_back(live_colony.surface_buildings[0]);
    check_colony_dto(retained_colony[0], colony_row.at("Result")[0],
                     "native detached colony capture");

    const auto &technology_row =
        find_row("technology-capture-utf16-ordinal-sort");
    auto live_technology = technology(technology_row.at("BeforeInput")[0]);
    const auto retained_technology =
        capture_technology_dtos(std::span(&live_technology, 1));
    live_technology.completed_technology_ids.insert("mutated");
    live_technology.active_research_id = "mutated";
    check_technology_dto(retained_technology[0], technology_row.at("Result")[0],
                         "native detached technology capture");

    const auto &construction_row =
        find_row("construction-capture-utf16-sort-and-queue");
    auto live_construction = construction(construction_row.at("BeforeInput")[0]);
    const auto retained_construction =
        capture_construction_dtos(std::span(&live_construction, 1));
    live_construction.completed_project_ids[0] = "mutated";
    live_construction.queued_projects[0].project_id = "mutated";
    check_construction_dto(retained_construction[0],
                           construction_row.at("Result")[0],
                           "native detached construction capture");

    std::cout << "galaxy economy persistence parity: " << replayed
              << " actual-source rows passed; 3 native ownership probes\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "galaxy economy persistence parity failure: "
              << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd: " << std::filesystem::current_path().string() << '\n'
              << "source root: "
              << (argc >= 2 ? std::filesystem::absolute(argv[1]).string()
                            : "<missing>")
              << '\n'
              << "fixture: "
              << (argc >= 3 ? std::filesystem::absolute(argv[2]).string()
                            : "<missing>")
              << '\n';
    return 1;
  }
}
