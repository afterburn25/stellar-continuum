#include <stellar/core/construction_projects.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool value, const std::string &message) {
  if (!value)
    fail(message);
}
double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("Unknown named number: " + text);
}
void equal_number(double actual, double expected, const std::string &label) {
  if (std::isnan(expected)) {
    check(std::isnan(actual), label);
    return;
  }
  if (std::isinf(expected)) {
    check(actual == expected, label);
    return;
  }
  const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
  check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-11 * scale,
        label);
}
std::optional<std::string> optional_string(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<std::string>(value.get<std::string>());
}

Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.home_system_id = value.at("HomeSystemId").get<int>();
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound").get<bool>()};
  result.is_player = value.at("IsPlayer").get<bool>();
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient").get<bool>();
  result.expansion_allowed = value.at("ExpansionAllowed").get<bool>();
  result.neutral_unless_provoked =
      value.at("NeutralUnlessProvoked").get<bool>();
  result.species_id = value.at("SpeciesId").get<std::string>();
  return result;
}
CivilizationEconomy parse_economy(const Json &value) {
  CivilizationEconomy result;
  result.civilization_id = value.at("CivilizationId").get<int>();
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
    result.industry_priority =
        static_cast<IndustryPriority>(value.at("IndustryPriority").get<int>());
  return result;
}
ConstructionState parse_state(const Json &value) {
  ConstructionState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.completed_project_ids =
      value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id = optional_string(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  for (const auto &order : value.at("QueuedProjects"))
    result.queued_projects.push_back(
        {order.at("ProjectId").get<std::string>(),
         number(order.at("AuthorizationCredits"))});
  return result;
}
SurfaceBuilding parse_building(const Json &value) {
  SurfaceBuilding result;
  result.id = value.at("Id").get<int>();
  result.type_id = value.at("TypeId").get<std::string>();
  result.x = value.at("X").get<float>();
  result.z = value.at("Z").get<float>();
  result.rotation_degrees = value.at("RotationDegrees").get<float>();
  result.industry_progress = number(value.at("IndustryProgress"));
  result.is_complete = value.at("IsComplete").get<bool>();
  result.is_enabled = value.at("IsEnabled").get<bool>();
  result.pending_upgrade_type_id =
      optional_string(value.at("PendingUpgradeTypeId"));
  result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
  result.operating_priority = value.at("OperatingPriority").get<int>();
  result.condition = number(value.at("Condition"));
  result.stored_power_days = number(value.at("StoredPowerDays"));
  return result;
}
Colony parse_colony(const Json &value) {
  Colony result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  if (!value.at("PlanetaryBodyId").is_null())
    result.planetary_body_id = value.at("PlanetaryBodyId").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id =
      value.at("PopulationSpeciesId").get<std::string>();
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials =
      number(value.at("StoredExtractedMaterials"));
  if (!value.at("RemainingExtractableMaterials").is_null())
    result.remaining_extractable_materials =
        number(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = value.at("SurfaceHubLevel").get<int>();
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &building : value.at("SurfaceBuildings"))
    result.surface_buildings.push_back(parse_building(building));
  return result;
}

struct OwnedWorld {
  std::vector<Civilization> civilizations;
  std::vector<PlanetaryBody> bodies;
  std::vector<ConstructionState> construction;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<CivilizationConstructionCapabilities> capabilities;
  ConstructionWorld mutable_view() {
    return {civilizations, bodies,    construction,
            colonies,      economies, capabilities};
  }
  ConstructionReadView read_view() const {
    return {civilizations, bodies,    construction,
            colonies,      economies, capabilities};
  }
};
OwnedWorld parse_world(const Json &snapshot, const Json &capability_values) {
  OwnedWorld world;
  for (const auto &value : snapshot.at("Civilizations"))
    world.civilizations.push_back(parse_civilization(value));
  for (const auto &value : snapshot.at("ConstructionStates"))
    world.construction.push_back(parse_state(value));
  for (const auto &value : snapshot.at("Colonies"))
    world.colonies.push_back(parse_colony(value));
  for (const auto &value : snapshot.at("Economies"))
    world.economies.push_back(parse_economy(value));
  for (const auto &value : capability_values)
    world.capabilities.push_back(
        {value.at("CivilizationId").get<int>(),
         value.at("CapabilityIds").get<std::vector<std::string>>()});
  return world;
}

void check_building(const SurfaceBuilding &a, const Json &e,
                    const std::string &l) {
  check(a.id == e.at("Id").get<int>(), l + ".Id");
  check(a.type_id == e.at("TypeId").get<std::string>(), l + ".TypeId");
  equal_number(a.x, number(e.at("X")), l + ".X");
  equal_number(a.z, number(e.at("Z")), l + ".Z");
  equal_number(a.rotation_degrees, number(e.at("RotationDegrees")),
               l + ".Rotation");
  equal_number(a.industry_progress, number(e.at("IndustryProgress")),
               l + ".Progress");
  check(a.is_complete == e.at("IsComplete").get<bool>(), l + ".Complete");
  check(a.is_enabled == e.at("IsEnabled").get<bool>(), l + ".Enabled");
  check(a.pending_upgrade_type_id ==
            optional_string(e.at("PendingUpgradeTypeId")),
        l + ".Pending");
  equal_number(a.upgrade_days_remaining, number(e.at("UpgradeDaysRemaining")),
               l + ".UpgradeDays");
  check(a.operating_priority == e.at("OperatingPriority").get<int>(),
        l + ".Priority");
  equal_number(a.condition, number(e.at("Condition")), l + ".Condition");
  equal_number(a.stored_power_days, number(e.at("StoredPowerDays")),
               l + ".Power");
}
void check_state(const ConstructionState &a, const Json &e,
                 const std::string &l) {
  check(a.civilization_id == e.at("CivilizationId").get<int>(),
        l + ".CivilizationId");
  check(a.completed_project_ids ==
            e.at("CompletedProjectIds").get<std::vector<std::string>>(),
        l + ".Completed");
  check(a.active_project_id == optional_string(e.at("ActiveProjectId")),
        l + ".Active");
  equal_number(a.active_project_progress, number(e.at("ActiveProjectProgress")),
               l + ".Progress");
  equal_number(a.active_project_authorization_credits,
               number(e.at("ActiveProjectAuthorizationCredits")),
               l + ".Authorization");
  check(a.queued_projects.size() == e.at("QueuedProjects").size(),
        l + ".QueueCount");
  for (size_t i = 0; i < a.queued_projects.size(); ++i) {
    check(a.queued_projects[i].project_id ==
              e.at("QueuedProjects")[i].at("ProjectId").get<std::string>(),
          l + ".QueueId");
    equal_number(a.queued_projects[i].authorization_credits,
                 number(e.at("QueuedProjects")[i].at("AuthorizationCredits")),
                 l + ".QueueAuthorization");
  }
}
void check_world(const OwnedWorld &w, const Json &e, const std::string &l) {
  check(w.civilizations.size() == e.at("Civilizations").size(),
        l + ".Civilizations");
  for (size_t i = 0; i < w.civilizations.size(); ++i) {
    const auto &j = e.at("Civilizations")[i];
    const auto &a = w.civilizations[i];
    check(a.id == j.at("Id").get<int>() &&
              a.name == j.at("Name").get<std::string>() &&
              a.home_system_id == j.at("HomeSystemId").get<int>() &&
              a.is_player == j.at("IsPlayer").get<bool>() &&
              a.is_seeded_ancient == j.at("IsSeededAncient").get<bool>() &&
              a.species_id == j.at("SpeciesId").get<std::string>() &&
              static_cast<int>(a.archetype) == j.at("Archetype").get<int>() &&
              static_cast<int>(a.development_stage) ==
                  j.at("DevelopmentStage").get<int>() &&
              a.expansion_allowed == j.at("ExpansionAllowed").get<bool>() &&
              a.neutral_unless_provoked ==
                  j.at("NeutralUnlessProvoked").get<bool>(),
          l + ".Civilization");
    const auto &t = j.at("Traits");
    equal_number(a.traits.aggression, number(t.at("Aggression")),
                 l + ".Aggression");
    equal_number(a.traits.territoriality, number(t.at("Territoriality")),
                 l + ".Territoriality");
    equal_number(a.traits.greed, number(t.at("Greed")), l + ".Greed");
    equal_number(a.traits.scientific_curiosity,
                 number(t.at("ScientificCuriosity")), l + ".Curiosity");
    equal_number(a.traits.risk_tolerance, number(t.at("RiskTolerance")),
                 l + ".Risk");
    equal_number(a.traits.survival_priority, number(t.at("SurvivalPriority")),
                 l + ".Survival");
    check(a.traits.honor_bound == t.at("HonorBound").get<bool>(), l + ".Honor");
  }
  check(w.construction.size() == e.at("ConstructionStates").size(),
        l + ".States");
  for (size_t i = 0; i < w.construction.size(); ++i)
    check_state(w.construction[i], e.at("ConstructionStates")[i],
                l + ".State" + std::to_string(i));
  check(w.economies.size() == e.at("Economies").size(), l + ".Economies");
  for (size_t i = 0; i < w.economies.size(); ++i) {
    const auto &a = w.economies[i];
    const auto &j = e.at("Economies")[i];
    check(a.civilization_id == j.at("CivilizationId").get<int>(),
          l + ".EconomyId");
    equal_number(a.credits, number(j.at("Credits")), l + ".Credits");
    equal_number(a.industry, number(j.at("Industry")), l + ".Industry");
    equal_number(a.science, number(j.at("Science")), l + ".Science");
    equal_number(a.last_credits_per_second,
                 number(j.at("LastCreditsPerSecond")), l + ".CreditsRate");
    equal_number(a.last_industry_per_second,
                 number(j.at("LastIndustryPerSecond")), l + ".IndustryRate");
    equal_number(a.last_science_per_second,
                 number(j.at("LastSciencePerSecond")), l + ".ScienceRate");
    equal_number(a.last_research_spending_per_day,
                 number(j.at("LastResearchSpendingPerDay")),
                 l + ".ResearchSpend");
    equal_number(a.last_research_funding_fraction,
                 number(j.at("LastResearchFundingFraction")),
                 l + ".ResearchFunding");
    equal_number(a.operating_arrears, number(j.at("OperatingArrears")),
                 l + ".Arrears");
    equal_number(a.last_base_operations_funding_fraction,
                 number(j.at("LastBaseOperationsFundingFraction")),
                 l + ".OperationsFunding");
    const auto expected_priority =
        j.at("IndustryPriority").is_null()
            ? std::optional<IndustryPriority>{}
            : std::optional<IndustryPriority>{static_cast<IndustryPriority>(
                  j.at("IndustryPriority").get<int>())};
    check(a.industry_priority == expected_priority, l + ".IndustryPriority");
  }
  check(w.colonies.size() == e.at("Colonies").size(), l + ".Colonies");
  for (size_t i = 0; i < w.colonies.size(); ++i) {
    const auto &a = w.colonies[i];
    const auto &j = e.at("Colonies")[i];
    check(a.id == j.at("Id").get<int>() &&
              a.civilization_id == j.at("CivilizationId").get<int>() &&
              a.system_id == j.at("SystemId").get<int>() &&
              a.name == j.at("Name").get<std::string>() &&
              static_cast<int>(a.kind) == j.at("Kind").get<int>() &&
              a.population_species_id ==
                  j.at("PopulationSpeciesId").get<std::string>() &&
              a.surface_hub_level == j.at("SurfaceHubLevel").get<int>(),
          l + ".Colony");
    check(a.planetary_body_id ==
              (j.at("PlanetaryBodyId").is_null()
                   ? std::optional<int>{}
                   : std::optional<int>{j.at("PlanetaryBodyId").get<int>()}),
          l + ".Body");
    check(a.remaining_extractable_materials ==
              (j.at("RemainingExtractableMaterials").is_null()
                   ? std::optional<double>{}
                   : std::optional<double>{number(
                         j.at("RemainingExtractableMaterials"))}),
          l + ".RemainingMaterials");
    equal_number(a.population_millions, number(j.at("PopulationMillions")),
                 l + ".Population");
    equal_number(a.infrastructure, number(j.at("Infrastructure")),
                 l + ".Infrastructure");
    equal_number(a.stability, number(j.at("Stability")), l + ".Stability");
    equal_number(a.stored_food_population_days_millions,
                 number(j.at("StoredFoodPopulationDaysMillions")), l + ".Food");
    equal_number(a.stored_water_population_days_millions,
                 number(j.at("StoredWaterPopulationDaysMillions")),
                 l + ".Water");
    equal_number(a.stored_extracted_materials,
                 number(j.at("StoredExtractedMaterials")), l + ".Materials");
    equal_number(a.surface_hub_upgrade_days_remaining,
                 number(j.at("SurfaceHubUpgradeDaysRemaining")),
                 l + ".HubDays");
    check(a.surface_buildings.size() == j.at("SurfaceBuildings").size(),
          l + ".Buildings");
    for (size_t k = 0; k < a.surface_buildings.size(); ++k)
      check_building(a.surface_buildings[k], j.at("SurfaceBuildings")[k],
                     l + ".Building");
  }
}

void check_catalog(const Json &test) {
  auto actual = construction_project_catalog();
  const auto &e = test.at("Catalog");
  check(actual.size() == e.size(), "catalog count");
  for (size_t i = 0; i < actual.size(); ++i) {
    const auto &a = actual[i];
    const auto &j = e[i];
    check(a.id == j.at("Id").get<std::string>() &&
              a.name == j.at("Name").get<std::string>() &&
              a.description == j.at("Description").get<std::string>(),
          "catalog text/order");
    equal_number(a.industry_cost, number(j.at("IndustryCost")),
                 "catalog IndustryCost");
    check(a.required_technologies ==
              j.at("RequiredTechnologies").get<std::vector<std::string>>(),
          "catalog technologies");
    check(static_cast<int>(a.category) == j.at("Category").get<int>(),
          "catalog category");
    equal_number(a.credit_cost, number(j.at("CreditCost")),
                 "catalog CreditCost");
    equal_number(a.industry_per_day, number(j.at("IndustryPerDay")),
                 "catalog IndustryPerDay");
    equal_number(a.upkeep_credits_per_day, number(j.at("UpkeepCreditsPerDay")),
                 "catalog Upkeep");
    const auto required =
        j.at("RequiredProjects").is_null()
            ? std::vector<std::string>{}
            : j.at("RequiredProjects").get<std::vector<std::string>>();
    check(a.required_projects == required, "catalog projects");
    check(find_construction_project(a.id) == &a, "catalog find identity");
    check(&get_construction_project(a.id) == &a, "catalog get identity");
  }
  check(find_construction_project("missing") == nullptr,
        "catalog missing find");
}

void check_error(const std::exception_ptr &actual, const Json &expected,
                 const std::string &name) {
  check(actual != nullptr, name + ": expected exception");
  const auto type = expected.at("Type").get<std::string>();
  const auto oracle = expected.at("Message").get<std::string>();
  try {
    std::rethrow_exception(actual);
  } catch (const std::out_of_range &error) {
    const auto message = std::string(error.what());
    if (type == "InvalidOperationException") {
      check(oracle.find("Sequence contains no matching element") !=
                std::string::npos,
            name + ": unsupported InvalidOperationException");
      check(message == "Sequence contains no matching element",
            name + ": sequence error");
    } else if (type == "ArgumentOutOfRangeException") {
      if (oracle.find("simulationDays") != std::string::npos)
        check(message.find("elapsed days") != std::string::npos,
              name + ": simulationDays error");
      else if (oracle.find("industryBudgets") != std::string::npos)
        check(message.find("Industry budgets") != std::string::npos,
              name + ": industryBudgets error");
      else if (oracle.find("availableIndustry") != std::string::npos)
        check(message.find("Available Industry") != std::string::npos,
              name + ": availableIndustry error");
      else
        fail(name + ": unrecognized ArgumentOutOfRangeException field");
    } else
      fail(name + ": unsupported oracle error category");
  } catch (const std::exception &) {
    fail(name + ": wrong native exception category");
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  if (kind == "Catalog") {
    check_catalog(test);
    return;
  }
  check(kind == "Start" || kind == "Queue" || kind == "Cancel" ||
            kind == "QueueCycle" || kind == "Available" || kind == "Lock" ||
            kind == "Blocker" || kind == "Demand" || kind == "Refund" ||
            kind == "SelectedAdvance" || kind == "GlobalAdvance",
        name + ": unsupported case kind");

  auto world = parse_world(test.at("Before"), test.at("Capabilities"));
  const auto &arguments = test.at("Arguments");
  const auto civilization_id = arguments.value("CivilizationId", 0);
  const auto project_id = arguments.value("ProjectId", std::string{});
  const auto days =
      arguments.contains("Days") ? number(arguments.at("Days")) : 0.0;
  const auto budget =
      arguments.contains("Budget") ? number(arguments.at("Budget")) : 0.0;
  const auto *project =
      kind == "Lock" ? find_construction_project(project_id) : nullptr;
  check(kind != "Lock" || project != nullptr,
        name + ": fixture project does not exist");
  const ConstructionState *refund_state = nullptr;
  if (kind == "Refund") {
    check(!world.construction.empty(), name + ": fixture refund state missing");
    refund_state = &world.construction.front();
  }
  std::vector<ConstructionIndustryBudget> budgets;
  std::optional<std::span<const ConstructionIndustryBudget>> budget_span;
  if (kind == "GlobalAdvance" && !arguments.at("Budgets").is_null()) {
    for (auto item = arguments.at("Budgets").begin();
         item != arguments.at("Budgets").end(); ++item)
      budgets.push_back({std::stoi(item.key()), number(item.value())});
    budget_span = budgets;
  }
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "InvalidOperationException" ||
            expected_error_type == "ArgumentOutOfRangeException",
        name + ": unsupported oracle error category");
  check_world(world, test.at("Before"), name + ".Before");

  std::optional<ConstructionOrderResult> order;
  std::optional<ConstructionCancellationResult> cancellation;
  std::optional<double> scalar;
  std::optional<std::string> text;
  std::optional<std::vector<ConstructionProjectDefinition>> projects;
  std::optional<std::vector<ConstructionEvent>> events;
  std::vector<ConstructionOrderResult> cycle_orders;
  std::optional<ConstructionCancellationResult> cycle_cancellation;
  std::exception_ptr operation_error;
  try {
    if (kind == "Start")
      order = start_construction_project(world.mutable_view(), civilization_id,
                                         project_id);
    else if (kind == "Queue")
      order = queue_construction_project(world.mutable_view(), civilization_id,
                                         project_id);
    else if (kind == "Cancel")
      cancellation = cancel_construction_project(world.mutable_view(),
                                                 civilization_id, project_id);
    else if (kind == "QueueCycle") {
      cycle_orders.push_back(queue_construction_project(
          world.mutable_view(), civilization_id, project_id));
      cycle_cancellation = cancel_construction_project(
          world.mutable_view(), civilization_id, project_id);
      cycle_orders.push_back(queue_construction_project(
          world.mutable_view(), civilization_id, project_id));
    } else if (kind == "Available")
      projects =
          available_construction_projects(world.read_view(), civilization_id);
    else if (kind == "Lock")
      text = construction_project_lock_reason(world.read_view(),
                                              civilization_id, *project)
                 .value_or("");
    else if (kind == "Blocker")
      text = construction_queue_blocker(world.read_view(), civilization_id)
                 .value_or("");
    else if (kind == "Demand")
      scalar = construction_industry_demand(world.read_view(), civilization_id,
                                            days);
    else if (kind == "Refund")
      scalar =
          construction_cancellation_refund_preview(*refund_state, project_id);
    else if (kind == "SelectedAdvance")
      events = advance_construction_for_civilization(
          world.mutable_view(), civilization_id, budget, days);
    else if (kind == "GlobalAdvance")
      events = advance_construction(world.mutable_view(), budget_span, days);
  } catch (...) {
    operation_error = std::current_exception();
  }

  if (!test.at("Error").is_null())
    check_error(operation_error, test.at("Error"), name);
  else {
    check(operation_error == nullptr, name + ": unexpected exception");
    const auto &result = test.at("Result");
    if (order)
      check(order->accepted == result.at("Accepted").get<bool>() &&
                order->message == result.at("Message").get<std::string>(),
            name + ": order result");
    else if (cancellation) {
      check(cancellation->accepted == result.at("Accepted").get<bool>() &&
                cancellation->message ==
                    result.at("Message").get<std::string>(),
            name + ": cancellation result");
      equal_number(cancellation->refunded_credits,
                   number(result.at("RefundedCredits")), name + ": refund");
    } else if (kind == "QueueCycle") {
      check(result.size() == 3 && cycle_orders.size() == 2 &&
                cycle_cancellation.has_value(),
            name + ": cycle result count");
      check(cycle_orders[0].accepted == result[0].at("Accepted").get<bool>() &&
                cycle_orders[0].message ==
                    result[0].at("Message").get<std::string>(),
            name + ": cycle queue");
      check(cycle_cancellation->accepted ==
                    result[1].at("Accepted").get<bool>() &&
                cycle_cancellation->message ==
                    result[1].at("Message").get<std::string>(),
            name + ": cycle cancel");
      equal_number(cycle_cancellation->refunded_credits,
                   number(result[1].at("RefundedCredits")),
                   name + ": cycle refund");
      check(cycle_orders[1].accepted == result[2].at("Accepted").get<bool>() &&
                cycle_orders[1].message ==
                    result[2].at("Message").get<std::string>(),
            name + ": cycle requeue");
    } else if (scalar)
      equal_number(*scalar, number(result), name + ": scalar");
    else if (text)
      check(*text ==
                (result.is_null() ? std::string{} : result.get<std::string>()),
            name + ": text");
    else if (projects) {
      check(projects->size() == result.size(), name + ": available count");
      for (size_t i = 0; i < projects->size(); ++i)
        check((*projects)[i].id == result[i].at("Id").get<std::string>(),
              name + ": available order");
    } else if (events) {
      check(events->size() == result.size(), name + ": event count");
      for (size_t i = 0; i < events->size(); ++i)
        check((*events)[i].civilization_id ==
                      result[i].at("CivilizationId").get<int>() &&
                  (*events)[i].project_id ==
                      result[i].at("ProjectId").get<std::string>() &&
                  (*events)[i].message ==
                      result[i].at("Message").get<std::string>(),
              name + ": event");
    }
  }
  check_world(world, test.at("After"), name + ".After");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected construction project fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open construction project fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-construction-projects-oracle-v2",
          "Unknown project fixture format");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "construction_projects_tests: passed "
              << fixture.at("Cases").size() << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "construction_projects_tests failed: " << error.what() << '\n';
    return 1;
  }
}
