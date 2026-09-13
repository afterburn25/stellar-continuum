#include <stellar/core/campaign_economy.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
constexpr double numeric_tolerance = 1e-12;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

double number(const Json& value) {
    if (value.is_number()) return value.get<double>();
    const auto text = value.get<std::string>();
    if (text == "Infinity") return INFINITY;
    if (text == "-Infinity") return -INFINITY;
    if (text == "NaN") return NAN;
    throw std::runtime_error("invalid named floating-point literal: " + text);
}

template<class T> std::optional<T> optional(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()};
}

SurfaceBuilding building(const Json& value) {
    SurfaceBuilding result;
    result.id = value.at("Id"); result.type_id = value.at("TypeId");
    result.x = value.at("X").get<float>(); result.z = value.at("Z").get<float>();
    result.rotation_degrees = value.at("RotationDegrees").get<float>();
    result.industry_progress = number(value.at("IndustryProgress"));
    result.is_complete = value.at("IsComplete"); result.is_enabled = value.at("IsEnabled");
    result.pending_upgrade_type_id = optional<std::string>(value.at("PendingUpgradeTypeId"));
    result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
    result.operating_priority = value.at("OperatingPriority"); result.condition = number(value.at("Condition"));
    result.stored_power_days = number(value.at("StoredPowerDays"));
    return result;
}

Json building_json(const SurfaceBuilding& value) {
    return {{"Id", value.id}, {"TypeId", value.type_id}, {"X", value.x}, {"Z", value.z},
        {"RotationDegrees", value.rotation_degrees}, {"IndustryProgress", value.industry_progress},
        {"IsComplete", value.is_complete}, {"IsEnabled", value.is_enabled},
        {"PendingUpgradeTypeId", value.pending_upgrade_type_id}, {"UpgradeDaysRemaining", value.upgrade_days_remaining},
        {"OperatingPriority", value.operating_priority}, {"Condition", value.condition}, {"StoredPowerDays", value.stored_power_days}};
}

Colony colony(const Json& value) {
    Colony result;
    result.id = value.at("Id"); result.civilization_id = value.at("CivilizationId"); result.system_id = value.at("SystemId");
    result.planetary_body_id = optional<int>(value.at("PlanetaryBodyId")); result.name = value.at("Name");
    result.kind = value.at("Kind"); result.population_species_id = value.at("PopulationSpeciesId");
    result.population_millions = number(value.at("PopulationMillions")); result.infrastructure = number(value.at("Infrastructure"));
    result.stability = number(value.at("Stability")); result.stored_food_population_days_millions = number(value.at("StoredFoodPopulationDaysMillions"));
    result.stored_water_population_days_millions = number(value.at("StoredWaterPopulationDaysMillions"));
    result.stored_extracted_materials = number(value.at("StoredExtractedMaterials"));
    result.remaining_extractable_materials = value.at("RemainingExtractableMaterials").is_null() ? std::nullopt : std::optional<double>{number(value.at("RemainingExtractableMaterials"))};
    result.surface_hub_level = value.at("SurfaceHubLevel"); result.surface_hub_upgrade_days_remaining = number(value.at("SurfaceHubUpgradeDaysRemaining"));
    for (const auto& item : value.at("SurfaceBuildings")) result.surface_buildings.push_back(building(item));
    return result;
}

Json colony_json(const Colony& value) {
    Json buildings = Json::array();
    for (const auto& item : value.surface_buildings) buildings.push_back(building_json(item));
    return {{"Id", value.id}, {"CivilizationId", value.civilization_id}, {"SystemId", value.system_id},
        {"PlanetaryBodyId", value.planetary_body_id}, {"Name", value.name}, {"Kind", value.kind},
        {"PopulationSpeciesId", value.population_species_id}, {"PopulationMillions", value.population_millions},
        {"Infrastructure", value.infrastructure}, {"Stability", value.stability},
        {"StoredFoodPopulationDaysMillions", value.stored_food_population_days_millions},
        {"StoredWaterPopulationDaysMillions", value.stored_water_population_days_millions},
        {"StoredExtractedMaterials", value.stored_extracted_materials}, {"RemainingExtractableMaterials", value.remaining_extractable_materials},
        {"SurfaceHubLevel", value.surface_hub_level}, {"SurfaceHubUpgradeDaysRemaining", value.surface_hub_upgrade_days_remaining},
        {"SurfaceBuildings", buildings}};
}

Civilization economy_civilization(const Json& value) {
    Civilization result;
    result.id = value.at("Id"); result.name = value.at("Name"); result.home_system_id = value.at("HomeSystemId");
    result.archetype = value.at("Archetype");
    const auto& traits = value.at("Traits");
    result.traits = {number(traits.at("Aggression")), number(traits.at("Territoriality")), number(traits.at("Greed")),
        number(traits.at("ScientificCuriosity")), number(traits.at("RiskTolerance")), number(traits.at("SurvivalPriority")), traits.at("HonorBound")};
    result.is_player = value.at("IsPlayer"); result.development_stage = value.at("DevelopmentStage");
    result.is_seeded_ancient = value.at("IsSeededAncient"); result.expansion_allowed = value.at("ExpansionAllowed");
    result.neutral_unless_provoked = value.at("NeutralUnlessProvoked"); result.species_id = value.at("SpeciesId");
    return result;
}

PlanetaryBody body(const Json& value) {
    PlanetaryBody result;
    result.id=value.at("Id"); result.system_id=value.at("SystemId"); result.parent_body_id=optional<int>(value.at("ParentBodyId"));
    result.orbit_index=value.at("OrbitIndex"); result.name=value.at("Name"); result.kind=value.at("Kind");
    result.radius_earth=number(value.at("RadiusEarth")); result.mass_earth=number(value.at("MassEarth"));
    const auto& environment=value.at("Environment");
    result.environment={number(environment.at("GravityG")), number(environment.at("TemperatureKelvin")), number(environment.at("PressureKPa")),
        environment.at("Atmosphere"), environment.at("AvailableSolvent"), number(environment.at("RadiationHazard")),
        environment.at("IsImmersedEnvironment"), environment.at("HasSolidSurface")};
    result.legacy_colonization_candidate=value.at("LegacyColonizationCandidate"); result.has_rare_resource=value.at("HasRareResource");
    result.has_anomaly=value.at("HasAnomaly"); result.has_pre_warp_civilization=value.at("HasPreWarpCivilization");
    result.orbital_eccentricity=number(value.value("OrbitalEccentricity", Json(0.0)));
    result.orbital_inclination_degrees=number(value.value("OrbitalInclinationDegrees", Json(0.0)));
    return result;
}

CivilizationEconomy economy(const Json& value) {
    CivilizationEconomy result;
    result.civilization_id=value.at("CivilizationId"); result.credits=number(value.at("Credits")); result.industry=number(value.at("Industry")); result.science=number(value.at("Science"));
    result.last_credits_per_second=number(value.at("LastCreditsPerSecond")); result.last_industry_per_second=number(value.at("LastIndustryPerSecond"));
    result.last_science_per_second=number(value.at("LastSciencePerSecond")); result.last_research_spending_per_day=number(value.at("LastResearchSpendingPerDay"));
    result.last_research_funding_fraction=number(value.at("LastResearchFundingFraction")); result.operating_arrears=number(value.at("OperatingArrears"));
    result.last_base_operations_funding_fraction=number(value.at("LastBaseOperationsFundingFraction"));
    result.industry_priority=optional<IndustryPriority>(value.at("IndustryPriority"));
    return result;
}

Json economy_json(const CivilizationEconomy& value) {
    return {{"CivilizationId", value.civilization_id}, {"Credits", value.credits}, {"Industry", value.industry}, {"Science", value.science},
        {"LastCreditsPerSecond", value.last_credits_per_second}, {"LastIndustryPerSecond", value.last_industry_per_second},
        {"LastSciencePerSecond", value.last_science_per_second}, {"LastResearchSpendingPerDay", value.last_research_spending_per_day},
        {"LastResearchFundingFraction", value.last_research_funding_fraction}, {"OperatingArrears", value.operating_arrears},
        {"LastBaseOperationsFundingFraction", value.last_base_operations_funding_fraction}, {"IndustryPriority", value.industry_priority}};
}

EconomyConstructionState construction(const Json& value) {
    EconomyConstructionState result; result.civilization_id=value.at("CivilizationId");
    result.completed_project_ids=value.at("CompletedProjectIds").get<std::vector<std::string>>(); return result;
}

EconomyFleetState fleet(const Json& value) {
    EconomyFleetState result; result.civilization_id=value.at("CivilizationId"); result.role=value.at("Role");
    result.is_active=value.value("IsActive", true); return result;
}

struct Inputs {
    std::vector<Civilization> civilizations; std::vector<PlanetaryBody> bodies;
    std::vector<EconomyConstructionState> construction; std::vector<EconomyFleetState> fleets;
    std::vector<Colony> colonies; std::vector<CivilizationEconomy> economies;
    EconomyWorldView world() const { return {civilizations, bodies, construction, fleets}; }
};

Inputs inputs(const Json& test) {
    const Json& world = test.contains("World") ? test.at("World") : test;
    Inputs result;
    for(const auto& item: world.value("Civilizations", Json::array())) result.civilizations.push_back(economy_civilization(item));
    for(const auto& item: world.value("PlanetaryBodies", Json::array())) result.bodies.push_back(body(item));
    for(const auto& item: world.value("ConstructionStates", Json::array())) result.construction.push_back(construction(item));
    for(const auto& item: world.value("Fleets", Json::array())) result.fleets.push_back(fleet(item));
    for(const auto& item: world.value("Colonies", Json::array())) result.colonies.push_back(colony(item));
    for(const auto& item: world.value("Economies", Json::array())) result.economies.push_back(economy(item));
    return result;
}

Json credit_flow_json(const CreditFlowSnapshot& value) {
    return {{"ColonyRevenuePerDay",value.colony_revenue_per_day},{"TradeRevenuePerDay",value.trade_revenue_per_day},
        {"ColonyAdministrationPerDay",value.colony_administration_per_day},{"PopulationServicesPerDay",value.population_services_per_day},
        {"HabitatSupportPerDay",value.habitat_support_per_day},{"FleetOperationsPerDay",value.fleet_operations_per_day},
        {"OrbitalMaintenancePerDay",value.orbital_maintenance_per_day},{"SurfaceMaintenancePerDay",value.surface_maintenance_per_day},
        {"ResearchOperationsPerDay",value.research_operations_per_day},{"GrossIncomePerDay",value.gross_income_per_day},
        {"OperatingCostsPerDay",value.operating_costs_per_day},{"NetCreditsPerDay",value.net_credits_per_day}};
}

void equal_json(const Json& actual, const Json& expected, const std::string& path) {
    if (actual.is_number() && expected.is_string()) {
        const auto value=actual.get<double>();
        const auto text=expected.get<std::string>();
        check((text=="Infinity" && std::isinf(value) && value>0) || (text=="-Infinity" && std::isinf(value) && value<0) || (text=="NaN" && std::isnan(value)), path+": named floating mismatch"); return;
    }
    if (actual.is_number() && expected.is_number()) {
        if (actual.is_number_integer() && expected.is_number_integer()) { check(actual==expected,path); return; }
        const auto a=actual.get<double>(), e=expected.get<double>();
        const bool same=(std::isnan(a)&&std::isnan(e)) || (std::isinf(a)&&std::isinf(e)&&std::signbit(a)==std::signbit(e)) || std::abs(a-e)<=numeric_tolerance*std::max(1.0,std::abs(e));
        check(same,path+": expected "+expected.dump()+", got "+actual.dump()); return;
    }
    if (actual.is_array() && expected.is_array()) { check(actual.size()==expected.size(),path+": array size"); for(size_t i=0;i<actual.size();++i) equal_json(actual[i],expected[i],path+"["+std::to_string(i)+"]"); return; }
    if (actual.is_object() && expected.is_object()) { check(actual.size()==expected.size(),path+": field count"); for(const auto& [key,value]:expected.items()) { check(actual.contains(key),path+": missing "+key); equal_json(actual.at(key),value,path+"."+key); } return; }
    check(actual==expected,path+": expected "+expected.dump()+", got "+actual.dump());
}

Json colonies_json(const std::vector<Colony>& values) { Json result=Json::array(); for(const auto& value:values) result.push_back(colony_json(value)); return result; }
Json economies_json(const std::vector<CivilizationEconomy>& values) { Json result=Json::array(); for(const auto& value:values) result.push_back(economy_json(value)); return result; }

Json projected_state(const Json& world) {
    return {{"Colonies", world.value("Colonies", Json::array())}, {"Economies", world.value("Economies", Json::array())}};
}

Json native_state(const Inputs& state) {
    return {{"Colonies", colonies_json(state.colonies)}, {"Economies", economies_json(state.economies)}};
}

Json projected_construction_seed(const Json& values) {
    Json result=Json::array();
    for(const auto& value:values) result.push_back({{"CivilizationId",value.at("CivilizationId")},{"CompletedProjectIds",value.at("CompletedProjectIds")}});
    return result;
}

Json profile_json(const ConstructionEconomicProfile& value) {
    return {{"Id", value.id}, {"IndustryPerDay", value.industry_per_day}, {"UpkeepCreditsPerDay", value.upkeep_credits_per_day}};
}

template<class Operation> void run_case(const std::string& name, const std::optional<std::string>& expected_error, Operation operation) {
    std::function<void()> compare;
    std::optional<std::string> operation_error;
    try { compare=operation(); }
    catch(const std::exception& error) {
        operation_error=error.what();
    }
    if(operation_error) {
        check(expected_error.has_value(),name+": native operation threw: "+*operation_error);
        const bool equivalent=operation_error->find(*expected_error)!=std::string::npos ||
            (expected_error->find("finite positive interval")!=std::string::npos && operation_error->find("finite positive")!=std::string::npos);
        check(equivalent,name+": expected equivalent error to '"+*expected_error+"', got '"+*operation_error+"'");
        return;
    }
    check(!expected_error.has_value(),name+": expected operation failure but succeeded");
    compare();
}
}

int main(int argc, char** argv) {
    try {
        check(argc==2,"Expected campaign economy oracle path"); std::ifstream input(argv[1]); check(input.good(),"Unable to read campaign economy oracle");
        const auto fixture=Json::parse(input); check(fixture.at("Format")=="stellar-funded-economy-oracle-v1","Unknown funded economy oracle format");
        size_t count=0;
        for(const auto& test:fixture.at("Cases")) {
            const auto name=test.at("Name").get<std::string>();
            const auto kind=test.at("Kind").get<std::string>();
            const auto expected_error=test.contains("ExpectedError") ? std::optional<std::string>{test.at("ExpectedError").get<std::string>()} : std::nullopt;
            auto state=inputs(test);
            const auto before=native_state(state);
            const auto arguments=test.value("Arguments",Json::object());
            const auto civilization_id=(kind=="CreditFlow" || kind=="Error") ? arguments.at("CivilizationId").get<int>() : 0;
            const auto include_research=(kind=="CreditFlow" || kind=="Error") ? arguments.value("IncludeResearch",true) : true;
            const auto power_interval=(kind=="CreditFlow" || kind=="Error") ? number(arguments.value("PowerIntervalDays",Json(1.0))) : 1.0;
            const auto days=kind=="Advance" ? number(arguments.at("Days")) : 0.0;
            const auto accrue_legacy_science=kind=="Advance" ? arguments.value("AccrueLegacyScience",true) : true;
            std::vector<IndustryReserve> reserves;
            if(kind=="Storage") for(const auto& [id,value]:arguments.value("ExistingReserves",Json::object()).items()) reserves.push_back({std::stoi(id),number(value)});
            std::vector<Civilization> profile_civilizations;
            if(kind=="Profiles") for(const auto& civilization:test.at("Civilizations")) profile_civilizations.push_back(economy_civilization(civilization));
            run_case(name,expected_error,[&]() -> std::function<void()> {
                if(kind=="CreditFlow") {
                    const auto actual=credit_flow_json(economy_credit_flow(state.world(),state.colonies,state.economies,civilization_id,include_research,power_interval));
                    const auto after=native_state(state); const auto expected=test.at("Expected"); const auto expected_after=projected_state(test.at("After"));
                    return [actual,before,after,expected,expected_after,name] { equal_json(after,before,name+".nonmutation"); equal_json(actual,expected,name); equal_json(after,expected_after,name+".after"); };
                }
                if(kind=="Storage") {
                    apply_industry_storage_caps(state.world(),state.colonies,state.economies,reserves);
                    const auto capacity=industry_storage_capacity(state.world(),state.colonies,state.economies.front().civilization_id);
                    const auto after=native_state(state); const auto expected_before=projected_state(test.at("Before")); const auto expected_after=projected_state(test.at("After")); const auto expected_capacity=test.at("ExpectedCapacity");
                    return [before,capacity,after,expected_before,expected_after,expected_capacity,name] { equal_json(before,expected_before,name+".before"); equal_json(after,expected_after,name+".after"); equal_json(capacity,expected_capacity,name+".capacity"); };
                }
                if(kind=="Advance") {
                    advance_colony_economies(state.world(),state.colonies,state.economies,days,accrue_legacy_science);
                    const auto after=native_state(state); const auto expected_before=projected_state(test.contains("Before") ? test.at("Before") : test.at("World")); const auto expected_after=projected_state(test.at("After"));
                    return [before,after,expected_before,expected_after,name] { equal_json(before,expected_before,name+".before"); equal_json(after,expected_after,name+".after"); };
                }
                if(kind=="Profiles") {
                    Json profiles=Json::array(); for(const auto& profile:construction_economic_profiles()) profiles.push_back(profile_json(profile));
                    Json seeded=Json::array(); for(const auto& item:seed_economic_construction(profile_civilizations)) seeded.push_back({{"CivilizationId",item.civilization_id},{"CompletedProjectIds",item.completed_project_ids}});
                    const auto expected_profiles=test.at("ExpectedEconomicProfiles"); const auto expected_seed=projected_construction_seed(test.at("ExpectedSeed"));
                    return [profiles,seeded,expected_profiles,expected_seed,name] { equal_json(profiles,expected_profiles,name+".profiles"); equal_json(seeded,expected_seed,name+".seed"); };
                }
                if(kind=="Error") {
                    (void)economy_credit_flow(state.world(),state.colonies,state.economies,civilization_id,include_research,power_interval);
                    return [] {};
                }
                throw std::runtime_error("Unknown campaign economy case kind: "+kind);
            });
            ++count;
        }
        const Json* storage_case=nullptr;
        for(const auto& test:fixture.at("Cases")) if(test.at("Kind")=="Storage") { storage_case=&test; break; }
        check(storage_case!=nullptr,"Fixture has no storage case for native duplicate-reserve validation");
        auto duplicate_reserve_state=inputs(*storage_case);
        const auto civilization_id=duplicate_reserve_state.economies.front().civilization_id;
        bool duplicate_reserve_rejected=false;
        try {
            const std::array<IndustryReserve,2> duplicate_reserves{{{civilization_id,1.0},{civilization_id,2.0}}};
            apply_industry_storage_caps(duplicate_reserve_state.world(),duplicate_reserve_state.colonies,duplicate_reserve_state.economies,duplicate_reserves);
        } catch(const std::invalid_argument& error) {
            duplicate_reserve_rejected=std::string(error.what()).find("unique civilization IDs")!=std::string::npos;
        }
        check(duplicate_reserve_rejected,"Native duplicate industry reserves must be rejected with 'unique civilization IDs'");
        std::cout<<"campaign_economy_tests: passed; "<<count<<" oracle cases and 1 native validation\\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"campaign_economy_tests failed: "<<error.what()<<'\\n'; return 1; }
}
