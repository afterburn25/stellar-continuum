#include <stellar/core/surface_construction.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  require(value.is_string(), "expected numeric fixture value");
  auto text = value.get<std::string>();
  if (text == "NaN")
    return NAN;
  if (text == "Infinity")
    return INFINITY;
  if (text == "-Infinity")
    return -INFINITY;
  throw std::runtime_error("unknown floating-point token: " + text);
}
bool eq(double a, double b) {
  if (std::isnan(a) || std::isnan(b))
    return std::isnan(a) && std::isnan(b);
  if (std::isinf(a) || std::isinf(b))
    return a == b;
  return std::abs(a - b) <= 1e-9;
}
std::optional<int> opt_int(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional{value.get<int>()};
}
std::optional<double> opt_double(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional{number(value)};
}
std::optional<std::string> opt_string(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional{value.get<std::string>()};
}

SurfaceBuilding building(const Json &x) {
  return {x.at("Id"),
          x.at("TypeId"),
          static_cast<float>(number(x.at("X"))),
          static_cast<float>(number(x.at("Z"))),
          static_cast<float>(number(x.at("RotationDegrees"))),
          number(x.at("IndustryProgress")),
          x.at("IsComplete"),
          x.at("IsEnabled"),
          opt_string(x.at("PendingUpgradeTypeId")),
          number(x.at("UpgradeDaysRemaining")),
          x.at("OperatingPriority"),
          number(x.at("Condition")),
          number(x.at("StoredPowerDays"))};
}
Colony colony(const Json &x) {
  Colony c;
  c.id = x.at("Id");
  c.civilization_id = x.at("CivilizationId");
  c.system_id = x.at("SystemId");
  c.planetary_body_id = opt_int(x.at("PlanetaryBodyId"));
  c.name = x.at("Name");
  c.kind = static_cast<SettlementKind>(x.at("Kind").get<int>());
  c.population_species_id = x.at("PopulationSpeciesId");
  c.population_millions = number(x.at("PopulationMillions"));
  c.infrastructure = number(x.at("Infrastructure"));
  c.stability = number(x.at("Stability"));
  c.stored_food_population_days_millions =
      number(x.at("StoredFoodPopulationDaysMillions"));
  c.stored_water_population_days_millions =
      number(x.at("StoredWaterPopulationDaysMillions"));
  c.stored_extracted_materials = number(x.at("StoredExtractedMaterials"));
  c.remaining_extractable_materials =
      opt_double(x.at("RemainingExtractableMaterials"));
  c.surface_hub_level = x.at("SurfaceHubLevel");
  c.surface_hub_upgrade_days_remaining =
      number(x.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &b : x.at("SurfaceBuildings"))
    c.surface_buildings.push_back(building(b));
  return c;
}
CivilizationEconomy economy(const Json &x) {
  CivilizationEconomy e;
  e.civilization_id = x.at("CivilizationId");
  e.credits = number(x.at("Credits"));
  e.industry = number(x.at("Industry"));
  e.science = number(x.at("Science"));
  e.last_credits_per_second = number(x.at("LastCreditsPerSecond"));
  e.last_industry_per_second = number(x.at("LastIndustryPerSecond"));
  e.last_science_per_second = number(x.at("LastSciencePerSecond"));
  e.last_research_spending_per_day = number(x.at("LastResearchSpendingPerDay"));
  e.last_research_funding_fraction =
      number(x.at("LastResearchFundingFraction"));
  e.operating_arrears = number(x.at("OperatingArrears"));
  e.last_base_operations_funding_fraction =
      number(x.at("LastBaseOperationsFundingFraction"));
  if (!x.at("IndustryPriority").is_null())
    e.industry_priority =
        static_cast<IndustryPriority>(x.at("IndustryPriority").get<int>());
  return e;
}
PlanetaryBody body(const Json &x) {
  PlanetaryBody b;
  b.id = x.at("Id");
  b.system_id = x.at("SystemId");
  b.parent_body_id = opt_int(x.at("ParentBodyId"));
  b.orbit_index = x.at("OrbitIndex");
  b.name = x.at("Name");
  b.kind = static_cast<PlanetaryBodyKind>(x.at("Kind").get<int>());
  b.radius_earth = number(x.at("RadiusEarth"));
  b.mass_earth = number(x.at("MassEarth"));
  const auto &e = x.at("Environment");
  b.environment = {
      number(e.at("GravityG")),
      number(e.at("TemperatureKelvin")),
      number(e.at("PressureKPa")),
      static_cast<PlanetaryAtmosphereRegime>(e.at("Atmosphere").get<int>()),
      static_cast<PlanetarySolventRegime>(e.at("AvailableSolvent").get<int>()),
      number(e.at("RadiationHazard")),
      e.at("IsImmersedEnvironment"),
      e.at("HasSolidSurface")};
  b.legacy_colonization_candidate = x.at("LegacyColonizationCandidate");
  b.has_rare_resource = x.at("HasRareResource");
  b.has_anomaly = x.at("HasAnomaly");
  b.has_pre_warp_civilization = x.at("HasPreWarpCivilization");
  return b;
}

struct Owner {
  std::vector<Civilization> civilizations;
  std::vector<PlanetaryBody> bodies;
  std::vector<ConstructionState> construction;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<CivilizationConstructionCapabilities> capabilities;
  ConstructionWorld world() {
    return {civilizations, bodies,    construction,
            colonies,      economies, capabilities};
  }
  ConstructionReadView read() const {
    return {civilizations, bodies,    construction,
            colonies,      economies, capabilities};
  }
};
Owner owner(const Json &x) {
  Owner w;
  for (const auto &c : x.at("Civilizations")) {
    Civilization v;
    v.id = c.at("Id");
    v.name = c.at("Name");
    v.home_system_id = c.at("HomeSystemId");
    v.archetype =
        static_cast<CivilizationArchetype>(c.at("Archetype").get<int>());
    v.is_player = c.at("IsPlayer");
    v.development_stage = static_cast<CivilizationDevelopmentStage>(
        c.at("DevelopmentStage").get<int>());
    v.is_seeded_ancient = c.at("IsSeededAncient");
    v.expansion_allowed = c.at("ExpansionAllowed");
    v.neutral_unless_provoked = c.at("NeutralUnlessProvoked");
    v.species_id = c.at("SpeciesId");
    w.civilizations.push_back(std::move(v));
  }
  for (const auto &b : x.at("Bodies"))
    w.bodies.push_back(body(b));
  for (const auto &s : x.at("Construction")) {
    ConstructionState v;
    v.civilization_id = s.at("CivilizationId");
    v.completed_project_ids =
        s.at("CompletedProjectIds").get<std::vector<std::string>>();
    v.active_project_id = opt_string(s.at("ActiveProjectId"));
    v.active_project_progress = number(s.at("ActiveProjectProgress"));
    v.active_project_authorization_credits =
        number(s.at("ActiveProjectAuthorizationCredits"));
    for (const auto &q : s.at("QueuedProjects"))
      v.queued_projects.push_back(
          {q.at("ProjectId"), number(q.at("AuthorizationCredits"))});
    w.construction.push_back(std::move(v));
  }
  for (const auto &c : x.at("Colonies"))
    w.colonies.push_back(colony(c));
  for (const auto &e : x.at("Economies"))
    w.economies.push_back(economy(e));
  for (const auto &c : x.at("Capabilities"))
    w.capabilities.push_back(
        {c.at("CivilizationId"),
         c.at("CapabilityIds").get<std::vector<std::string>>()});
  return w;
}

void compare_building(const SurfaceBuilding &a, const SurfaceBuilding &e,
                      const std::string &n) {
  require(a.id == e.id && a.type_id == e.type_id, n + ": building identity");
  require(eq(a.x, e.x) && eq(a.z, e.z) &&
              eq(a.rotation_degrees, e.rotation_degrees),
          n + ": building placement");
  require(eq(a.industry_progress, e.industry_progress) &&
              a.is_complete == e.is_complete && a.is_enabled == e.is_enabled,
          n + ": building construction/status");
  require(a.pending_upgrade_type_id == e.pending_upgrade_type_id &&
              eq(a.upgrade_days_remaining, e.upgrade_days_remaining),
          n + ": building upgrade");
  require(a.operating_priority == e.operating_priority &&
              eq(a.condition, e.condition) &&
              eq(a.stored_power_days, e.stored_power_days),
          n + ": building operations");
}
void compare_colony(const Colony &a, const Colony &e, const std::string &n) {
  require(a.id == e.id && a.civilization_id == e.civilization_id &&
              a.system_id == e.system_id &&
              a.planetary_body_id == e.planetary_body_id && a.name == e.name &&
              a.kind == e.kind,
          n + ": colony identity");
  require(a.population_species_id == e.population_species_id &&
              eq(a.population_millions, e.population_millions) &&
              eq(a.infrastructure, e.infrastructure) &&
              eq(a.stability, e.stability),
          n + ": colony population");
  require(eq(a.stored_food_population_days_millions,
             e.stored_food_population_days_millions) &&
              eq(a.stored_water_population_days_millions,
                 e.stored_water_population_days_millions) &&
              eq(a.stored_extracted_materials, e.stored_extracted_materials),
          n + ": colony stores");
  require((!a.remaining_extractable_materials &&
           !e.remaining_extractable_materials) ||
              (a.remaining_extractable_materials &&
               e.remaining_extractable_materials &&
               eq(*a.remaining_extractable_materials,
                  *e.remaining_extractable_materials)),
          n + ": colony reserves");
  require(a.surface_hub_level == e.surface_hub_level &&
              eq(a.surface_hub_upgrade_days_remaining,
                 e.surface_hub_upgrade_days_remaining),
          n + ": hub state");
  require(a.surface_buildings.size() == e.surface_buildings.size(),
          n + ": building count/order");
  for (size_t i = 0; i < e.surface_buildings.size(); ++i)
    compare_building(a.surface_buildings[i], e.surface_buildings[i],
                     n + " building " + std::to_string(i));
}
void compare_economy(const CivilizationEconomy &a, const CivilizationEconomy &e,
                     const std::string &n) {
  require(a.civilization_id == e.civilization_id && eq(a.credits, e.credits) &&
              eq(a.industry, e.industry) && eq(a.science, e.science),
          n + ": economy balances");
  require(eq(a.last_credits_per_second, e.last_credits_per_second) &&
              eq(a.last_industry_per_second, e.last_industry_per_second) &&
              eq(a.last_science_per_second, e.last_science_per_second) &&
              eq(a.last_research_spending_per_day,
                 e.last_research_spending_per_day) &&
              eq(a.last_research_funding_fraction,
                 e.last_research_funding_fraction) &&
              eq(a.operating_arrears, e.operating_arrears) &&
              eq(a.last_base_operations_funding_fraction,
                 e.last_base_operations_funding_fraction) &&
              a.industry_priority == e.industry_priority,
          n + ": unrelated economy fields");
}
void compare_world(const Owner &a, const Json &expected, const std::string &n) {
  auto e = owner(expected);
  require(a.civilizations.size() == e.civilizations.size() &&
              a.bodies.size() == e.bodies.size() &&
              a.colonies.size() == e.colonies.size() &&
              a.economies.size() == e.economies.size(),
          n + ": world collection sizes");
  for (size_t i = 0; i < e.civilizations.size(); ++i) {
    const auto &x = a.civilizations[i];
    const auto &y = e.civilizations[i];
    require(x.id == y.id && x.name == y.name &&
                x.home_system_id == y.home_system_id &&
                x.archetype == y.archetype && x.is_player == y.is_player &&
                x.development_stage == y.development_stage &&
                x.is_seeded_ancient == y.is_seeded_ancient &&
                x.expansion_allowed == y.expansion_allowed &&
                x.neutral_unless_provoked == y.neutral_unless_provoked &&
                x.species_id == y.species_id,
            n + ": civilization changed");
  }
  for (size_t i = 0; i < e.bodies.size(); ++i) {
    const auto &x = a.bodies[i];
    const auto &y = e.bodies[i];
    require(x.id == y.id && x.system_id == y.system_id &&
                x.parent_body_id == y.parent_body_id &&
                x.orbit_index == y.orbit_index && x.name == y.name &&
                x.kind == y.kind && eq(x.radius_earth, y.radius_earth) &&
                eq(x.mass_earth, y.mass_earth),
            n + ": body identity changed");
    require(eq(x.environment.gravity_g, y.environment.gravity_g) &&
                eq(x.environment.temperature_kelvin,
                   y.environment.temperature_kelvin) &&
                eq(x.environment.pressure_kpa, y.environment.pressure_kpa) &&
                x.environment.atmosphere == y.environment.atmosphere &&
                x.environment.available_solvent ==
                    y.environment.available_solvent &&
                eq(x.environment.radiation_hazard,
                   y.environment.radiation_hazard) &&
                x.environment.is_immersed_environment ==
                    y.environment.is_immersed_environment &&
                x.environment.has_solid_surface ==
                    y.environment.has_solid_surface,
            n + ": body environment changed");
  }
  for (size_t i = 0; i < e.colonies.size(); ++i)
    compare_colony(a.colonies[i], e.colonies[i],
                   n + " colony " + std::to_string(i));
  for (size_t i = 0; i < e.economies.size(); ++i)
    compare_economy(a.economies[i], e.economies[i],
                    n + " economy " + std::to_string(i));
  require(a.construction.size() == e.construction.size(),
          n + ": construction state count");
  for (size_t i = 0; i < e.construction.size(); ++i) {
    const auto &x = a.construction[i];
    const auto &y = e.construction[i];
    require(x.civilization_id == y.civilization_id &&
                x.completed_project_ids == y.completed_project_ids &&
                x.active_project_id == y.active_project_id &&
                eq(x.active_project_progress, y.active_project_progress) &&
                eq(x.active_project_authorization_credits,
                   y.active_project_authorization_credits) &&
                x.queued_projects.size() == y.queued_projects.size(),
            n + ": construction state changed");
    for (size_t q = 0; q < y.queued_projects.size(); ++q)
      require(x.queued_projects[q].project_id ==
                      y.queued_projects[q].project_id &&
                  eq(x.queued_projects[q].authorization_credits,
                     y.queued_projects[q].authorization_credits),
              n + ": construction queue changed");
  }
  require(a.capabilities.size() == e.capabilities.size(),
          n + ": capabilities changed");
  for (size_t i = 0; i < e.capabilities.size(); ++i)
    require(a.capabilities[i].civilization_id ==
                    e.capabilities[i].civilization_id &&
                a.capabilities[i].capability_ids ==
                    e.capabilities[i].capability_ids,
            n + ": capabilities changed");
}

std::string error_type(const std::exception &e) {
  if (dynamic_cast<const std::out_of_range *>(&e))
    return "ArgumentOutOfRangeException";
  if (dynamic_cast<const std::invalid_argument *>(&e))
    return std::string_view(e.what()) ==
                   "The surface building has an unknown type."
               ? "NullReferenceException"
               : "ArgumentException";
  if (dynamic_cast<const std::runtime_error *>(&e))
    return std::string_view(e.what()).starts_with("Colony ")
               ? "InvalidDataException"
               : "InvalidOperationException";
  return "Exception";
}
struct Error {
  std::string type, message;
};
std::optional<Error> wanted_error(const Json &value) {
  if (value.is_null())
    return {};
  return Error{value.at("Type"), value.at("Message")};
}
void compare_error(const std::optional<Error> &actual,
                   const std::optional<Error> &expected,
                   const std::string &name, bool exact_message = false) {
  require(actual.has_value() == expected.has_value(),
          name + (expected ? ": expected error" : ": unexpected error"));
  if (!expected)
    return;
  require(actual->type == expected->type, name + ": wrong error category " +
                                              actual->type + " expected " +
                                              expected->type);
  if (exact_message)
    require(actual->message == expected->message,
            name + ": wrong error message: " + actual->message);
}
struct ParsedOperation {
  std::string kind;
  int civilization_id{}, colony_id{}, building_id{};
  std::optional<std::string> type_id;
  float x{}, z{}, rotation{};
  bool flag{};
  double budget{}, days{};
};
ParsedOperation parse_operation(const Json &op) {
  return {op.at("Kind"),
          op.at("CivilizationId"),
          op.at("ColonyId"),
          op.at("BuildingId"),
          opt_string(op.at("TypeId")),
          static_cast<float>(number(op.at("X"))),
          static_cast<float>(number(op.at("Z"))),
          static_cast<float>(number(op.at("Rotation"))),
          op.at("Flag"),
          number(op.at("Budget")),
          number(op.at("Days"))};
}
ConstructionOrderResult command(Owner &w, const ParsedOperation &op) {
  auto world = w.world();
  if (op.kind == "Place") {
    require(op.type_id.has_value(), "place operation missing type");
    return place_surface_building(world, op.civilization_id, op.colony_id,
                                  *op.type_id, op.x, op.z, op.rotation);
  }
  if (op.kind == "Remove")
    return remove_surface_building(world, op.civilization_id, op.colony_id,
                                   op.building_id);
  if (op.kind == "Upgrade")
    return upgrade_surface_building(world, op.civilization_id, op.colony_id,
                                    op.building_id);
  if (op.kind == "UpgradeHub")
    return upgrade_surface_hub(world, op.civilization_id, op.colony_id);
  if (op.kind == "Repair")
    return repair_surface_building(world, op.civilization_id, op.colony_id,
                                   op.building_id);
  if (op.kind == "SetEnabled")
    return set_surface_building_enabled(world, op.civilization_id, op.colony_id,
                                        op.building_id, op.flag);
  if (op.kind == "SetPriority")
    return set_surface_building_priority(world, op.civilization_id,
                                         op.colony_id, op.building_id, op.flag);
  throw std::runtime_error("unknown command kind");
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "fixture path required");
    std::ifstream f(argv[1]);
    require(f.good(), "fixture missing");
    Json root;
    f >> root;
    require(root.at("Format") == "stellar-surface-construction-oracle-v1",
            "fixture format");
    size_t count = 0;
    for (const auto &test : root.at("Cases")) {
      ++count;
      auto name = test.at("Name").get<std::string>();
      auto kind = test.at("Kind").get<std::string>();
      require(kind == "Sequence" || kind == "Query" || kind == "Placement" ||
                  kind == "Validate",
              name + ": unknown case kind");
      if (kind == "Sequence") {
        Owner w = owner(test.at("Before"));
        compare_world(w, test.at("Before"), name + " before");
        size_t index = 0;
        for (const auto &step : test.at("Steps")) {
          auto expected_error = wanted_error(step.at("Error"));
          auto op = parse_operation(step.at("Operation"));
          std::optional<ConstructionOrderResult> expected_result;
          if (!expected_error && op.kind != "Advance")
            expected_result =
                ConstructionOrderResult{step.at("Result").at("Accepted"),
                                        step.at("Result").at("Message")};
          std::optional<Error> actual_error;
          std::optional<ConstructionOrderResult> result;
          try {
            if (op.kind == "Advance")
              advance_surface_construction(w.world(), op.civilization_id,
                                           op.budget, op.days);
            else
              result = command(w, op);
          } catch (const std::exception &e) {
            actual_error = Error{error_type(e), e.what()};
          }
          const bool exact_error_message =
              expected_error && (std::string_view(expected_error->message)
                                     .starts_with("Civilization ") ||
                                 expected_error->message ==
                                     "Sequence contains no matching element");
          compare_error(actual_error, expected_error,
                        name + " step " + std::to_string(index),
                        exact_error_message);
          if (expected_result) {
            require(result.has_value(), name + ": missing command result");
            require(result->accepted == expected_result->accepted &&
                        result->message == expected_result->message,
                    name + ": command result differs at " +
                        std::to_string(index));
          }
          compare_world(w, step.at("After"),
                        name + " step " + std::to_string(index));
          ++index;
        }
      } else if (kind == "Placement") {
        std::vector<SurfaceBuilding> buildings;
        for (const auto &b : test.at("Buildings"))
          buildings.push_back(building(b));
        auto expected = opt_string(test.at("Expected"));
        auto actual = surface_placement_error(
            buildings, test.at("TypeId").get<std::string>(),
            static_cast<float>(number(test.at("X"))),
            static_cast<float>(number(test.at("Z"))),
            static_cast<float>(number(test.at("Rotation"))));
        require(actual == expected, name + ": placement result differs");
      } else if (kind == "Validate") {
        auto c = colony(test.at("Colony"));
        auto before = c;
        auto expected = wanted_error(test.at("Error"));
        std::optional<Error> actual;
        try {
          validate_surface_construction(c);
        } catch (const std::exception &e) {
          actual = Error{error_type(e), e.what()};
        }
        compare_error(actual, expected, name, true);
        compare_colony(c, before, name + " mutation");
      } else {
        Owner w = owner(test.at("Before"));
        auto expected = wanted_error(test.at("Error"));
        auto query = test.at("Query").get<std::string>();
        const bool known_query =
            query == "Multiplier" || query == "Authorization" ||
            query == "UpgradeAuthorization" || query == "BuildingLock" ||
            query == "HubLock" || query == "HubCost" || query == "Demand";
        require(known_query, name + ": unknown query kind");
        require(!w.colonies.empty(),
                name + ": query fixture requires a colony");
        const auto &first_colony = w.colonies.front();
        const auto *fabricator = find_surface_building("fabricator");
        require(fabricator != nullptr,
                name + ": missing fabricator definition");
        const auto argument =
            query == "Demand"
                ? std::optional<double>{number(test.at("Argument"))}
                : std::nullopt;

        std::optional<Error> actual_error;
        Json actual;
        try {
          if (query == "Multiplier")
            actual =
                surface_construction_cost_multiplier(w.read(), first_colony);
          else if (query == "Authorization")
            actual =
                surface_authorization_cost(w.read(), first_colony, *fabricator);
          else if (query == "UpgradeAuthorization")
            actual = surface_upgrade_authorization_cost(w.read(), first_colony,
                                                        *fabricator);
          else if (query == "BuildingLock") {
            auto value =
                surface_building_upgrade_lock_reason(w.read(), 1, *fabricator);
            actual = value ? Json(*value) : Json(nullptr);
          } else if (query == "HubLock") {
            auto value =
                surface_hub_upgrade_lock_reason(w.read(), 1, first_colony);
            actual = value ? Json(*value) : Json(nullptr);
          } else if (query == "HubCost") {
            auto value = surface_hub_upgrade_cost(w.read(), first_colony);
            actual = value ? Json{{"CreditCost", value->credit_cost},
                                  {"IndustryCost", value->industry_cost}}
                           : Json(nullptr);
          } else {
            actual =
                surface_construction_industry_demand(w.read(), 1, *argument);
          }
        } catch (const std::exception &e) {
          actual_error = Error{error_type(e), e.what()};
        }
        compare_error(actual_error, expected, name);
        if (!expected) {
          const auto &wanted = test.at("Result");
          if (wanted.is_number() || (wanted.is_string() && query == "Demand"))
            require(eq(number(actual), number(wanted)),
                    name + ": numeric query differs");
          else
            require(actual == wanted, name + ": query result differs");
        }
        compare_world(w, test.at("Before"), name + " mutation");
      }
    }
    std::cout << "surface construction parity passed: " << count
              << " oracle cases\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
