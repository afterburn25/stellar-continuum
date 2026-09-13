#include <stellar/core/colony_economy.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using Json=nlohmann::json; using namespace stellar::core;
namespace {
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
double num(const Json& value){if(value.is_number())return value.get<double>();auto text=value.get<std::string>();if(text=="Infinity")return INFINITY;if(text=="-Infinity")return -INFINITY;if(text=="NaN")return NAN;throw std::runtime_error("invalid named floating-point literal: "+text);}
template<class T>std::optional<T> opt(const Json& value){return value.is_null()?std::nullopt:std::optional<T>{value.get<T>()};}
SurfaceBuilding building(const Json& v){SurfaceBuilding b;b.id=v.at("Id");b.type_id=v.at("TypeId");b.x=v.at("X").get<float>();b.z=v.at("Z").get<float>();b.rotation_degrees=v.at("RotationDegrees").get<float>();b.industry_progress=num(v.at("IndustryProgress"));b.is_complete=v.at("IsComplete");b.is_enabled=v.at("IsEnabled");b.pending_upgrade_type_id=opt<std::string>(v.at("PendingUpgradeTypeId"));b.upgrade_days_remaining=num(v.at("UpgradeDaysRemaining"));b.operating_priority=v.at("OperatingPriority");b.condition=num(v.at("Condition"));b.stored_power_days=num(v.at("StoredPowerDays"));return b;}
Json building_json(const SurfaceBuilding& b){return {{"Id",b.id},{"TypeId",b.type_id},{"X",b.x},{"Z",b.z},{"RotationDegrees",b.rotation_degrees},{"IndustryProgress",b.industry_progress},{"IsComplete",b.is_complete},{"IsEnabled",b.is_enabled},{"PendingUpgradeTypeId",b.pending_upgrade_type_id},{"UpgradeDaysRemaining",b.upgrade_days_remaining},{"OperatingPriority",b.operating_priority},{"Condition",b.condition},{"StoredPowerDays",b.stored_power_days}};}
PlanetaryBody body(const Json& r){PlanetaryBody b;b.id=r.at(0);b.system_id=r.at(1);b.parent_body_id=opt<int>(r.at(2));b.orbit_index=r.at(3);b.name=r.at(4);b.kind=r.at(5);b.radius_earth=num(r.at(6));b.mass_earth=num(r.at(7));b.environment={num(r.at(8)),num(r.at(9)),num(r.at(10)),r.at(11),r.at(12),num(r.at(13)),r.at(14),r.at(15)};b.legacy_colonization_candidate=r.at(16);b.has_rare_resource=r.at(17);b.has_anomaly=r.at(18);b.has_pre_warp_civilization=r.at(19);b.orbital_eccentricity=num(r.at(20));b.orbital_inclination_degrees=num(r.at(21));return b;}
Civilization civilization(const Json& j){Civilization c;c.id=j.at("Id");c.name=j.at("Name");c.home_system_id=j.at("HomeSystemId");c.archetype=j.at("Archetype");auto&t=j.at("Traits");c.traits={num(t.at("Aggression")),num(t.at("Territoriality")),num(t.at("Greed")),num(t.at("ScientificCuriosity")),num(t.at("RiskTolerance")),num(t.at("SurvivalPriority")),t.at("HonorBound")};c.is_player=j.at("IsPlayer");c.development_stage=j.at("DevelopmentStage");c.is_seeded_ancient=j.at("IsSeededAncient");c.expansion_allowed=j.at("ExpansionAllowed");c.neutral_unless_provoked=j.at("NeutralUnlessProvoked");c.species_id=j.at("SpeciesId");return c;}
Colony colony(const Json& j){Colony c;c.id=j.at("Id");c.civilization_id=j.at("CivilizationId");c.system_id=j.at("SystemId");c.planetary_body_id=opt<int>(j.at("PlanetaryBodyId"));c.name=j.at("Name");c.kind=j.at("Kind");c.population_species_id=j.at("PopulationSpeciesId");c.population_millions=num(j.at("PopulationMillions"));c.infrastructure=num(j.at("Infrastructure"));c.stability=num(j.at("Stability"));c.stored_food_population_days_millions=num(j.at("StoredFoodPopulationDaysMillions"));c.stored_water_population_days_millions=num(j.at("StoredWaterPopulationDaysMillions"));c.stored_extracted_materials=num(j.at("StoredExtractedMaterials"));c.remaining_extractable_materials=j.at("RemainingExtractableMaterials").is_null()?std::nullopt:std::optional<double>{num(j.at("RemainingExtractableMaterials"))};c.surface_hub_level=j.at("SurfaceHubLevel");c.surface_hub_upgrade_days_remaining=num(j.at("SurfaceHubUpgradeDaysRemaining"));for(const auto& b:j.at("SurfaceBuildings"))c.surface_buildings.push_back(building(b));return c;}
Json colony_json(const Colony& c){Json buildings=Json::array();for(const auto& b:c.surface_buildings)buildings.push_back(building_json(b));return {{"Id",c.id},{"CivilizationId",c.civilization_id},{"SystemId",c.system_id},{"PlanetaryBodyId",c.planetary_body_id},{"Name",c.name},{"Kind",c.kind},{"PopulationSpeciesId",c.population_species_id},{"PopulationMillions",c.population_millions},{"Infrastructure",c.infrastructure},{"Stability",c.stability},{"StoredFoodPopulationDaysMillions",c.stored_food_population_days_millions},{"StoredWaterPopulationDaysMillions",c.stored_water_population_days_millions},{"StoredExtractedMaterials",c.stored_extracted_materials},{"RemainingExtractableMaterials",c.remaining_extractable_materials},{"SurfaceHubLevel",c.surface_hub_level},{"SurfaceHubUpgradeDaysRemaining",c.surface_hub_upgrade_days_remaining},{"SurfaceBuildings",buildings}};}
Json economy_json(const CivilizationEconomy& e){return {{"CivilizationId",e.civilization_id},{"Credits",e.credits},{"Industry",e.industry},{"Science",e.science},{"LastCreditsPerSecond",e.last_credits_per_second},{"LastIndustryPerSecond",e.last_industry_per_second},{"LastSciencePerSecond",e.last_science_per_second},{"LastResearchSpendingPerDay",e.last_research_spending_per_day},{"LastResearchFundingFraction",e.last_research_funding_fraction},{"OperatingArrears",e.operating_arrears},{"LastBaseOperationsFundingFraction",e.last_base_operations_funding_fraction},{"IndustryPriority",e.industry_priority}};}
void equal_json(const Json&a,const Json&e,const std::string&p){if(a.is_number()&&e.is_string()){auto s=e.get<std::string>();auto x=a.get<double>();check((s=="Infinity"&&std::isinf(x)&&x>0)||(s=="-Infinity"&&std::isinf(x)&&x<0)||(s=="NaN"&&std::isnan(x)),p+": named floating mismatch");return;}if(a.is_number()&&e.is_number()){if(a.is_number_integer()&&e.is_number_integer()){check(a==e,p);return;}auto x=a.get<double>(),y=e.get<double>();const bool surface_float=p.ends_with(".X")||p.ends_with(".Z")||p.ends_with(".RotationDegrees");check((surface_float&&static_cast<float>(x)==static_cast<float>(y))||(!surface_float&&((std::isnan(x)&&std::isnan(y))||(std::isinf(x)&&std::isinf(y)&&std::signbit(x)==std::signbit(y))||std::abs(x-y)<=1e-13*std::max(1.0,std::abs(y)))),p+": expected "+e.dump()+", got "+a.dump());return;}if(a.is_array()&&e.is_array()){check(a.size()==e.size(),p+": array size");for(size_t i=0;i<a.size();++i)equal_json(a[i],e[i],p+"["+std::to_string(i)+"]");return;}if(a.is_object()&&e.is_object()){check(a.size()==e.size(),p+": field count");for(const auto&[k,v]:e.items()){check(a.contains(k),p+": missing "+k);equal_json(a.at(k),v,p+"."+k);}return;}check(a==e,p+": expected "+e.dump()+", got "+a.dump());}
std::string expected_native_error(const std::string& e){if(e.find("Parameter 'balance'")!=std::string::npos)return "balance";if(e.find("Parameter 'netPerDay'")!=std::string::npos)return "net_per_day";if(e.find("Parameter 'arrears'")!=std::string::npos)return "arrears";return e;}
template<class Compute> void run_case(const Json& test, Compute compute) {
    const auto name = test.at("Name").get<std::string>();
    std::function<void()> compare;
    try {
        // This try block intentionally surrounds only fixture decoding and the native call.
        // Assertions execute below so a test failure can never satisfy ExpectedError.
        compare = compute();
    } catch (const std::exception& error) {
        if (!test.contains("ExpectedError")) throw std::runtime_error(name + ": native operation threw: " + error.what());
        const auto expected = expected_native_error(test.at("ExpectedError").get<std::string>());
        check(std::string(error.what()).find(expected) != std::string::npos,
            name + ": expected native error containing '" + expected + "', got '" + error.what() + "'");
        return;
    }
    check(!test.contains("ExpectedError"), name + ": expected operation failure but succeeded");
    compare();
}
ColonySustenanceCapacity capacity(const Json& c){return {num(c.at("NaturalFoodCapacityMillions")),num(c.at("NaturalWaterCapacityMillions")),num(c.at("NaturalHousingCapacityMillions")),num(c.at("BuiltFoodCapacityMillions")),num(c.at("BuiltWaterCapacityMillions")),num(c.at("BuiltHousingCapacityMillions")),num(c.at("FoodCapacityMillions")),num(c.at("WaterCapacityMillions")),num(c.at("HousingCapacityMillions")),num(c.at("SupportedPopulationMillions")),num(c.at("SupportRatio")),c.at("LimitingSupply")};}
Json reserve_json(const ColonySustenanceReserveSnapshot& r){return {{"FoodReserveDays",r.food_reserve_days},{"WaterReserveDays",r.water_reserve_days},{"EffectiveSupportRatio",r.effective_support_ratio},{"LimitingSupply",r.limiting_supply}};}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected colony economy oracle path");
        std::ifstream input(argv[1]);
        check(input.good(), "Unable to read colony economy oracle");
        const auto fixture = Json::parse(input);
        check(fixture.at("Format") == "stellar-colony-economy-parity-v1", "Unknown colony economy oracle format");
        size_t count = 0;
        for (const auto& test : fixture.at("Cases")) {
            const auto kind = test.at("Kind").get<std::string>();
            run_case(test, [&]() -> std::function<void()> {
                if (kind == "Seed") {
                    std::vector<Civilization> civilizations;
                    for (const auto& item : test.at("Civilizations")) civilizations.push_back(civilization(item));
                    std::vector<PlanetaryBody> bodies;
                    for (const auto& item : test.value("Bodies", Json::array())) bodies.push_back(body(item));
                    const auto colonies = test.value("Legacy", false) ? seed_legacy_colonies(civilizations) : seed_colonies(civilizations, bodies);
                    Json actual = Json::array();
                    for (const auto& item : colonies) actual.push_back(colony_json(item));
                    if (test.contains("ExpectedEconomies")) {
                        Json economies = Json::array();
                        for (const auto& item : seed_economies(civilizations)) economies.push_back(economy_json(item));
                        return [actual, economies, &test] {
                            equal_json(actual, test.at("ExpectedColonies"), test.at("Name").get<std::string>() + ".colonies");
                            equal_json(economies, test.at("ExpectedEconomies"), test.at("Name").get<std::string>() + ".economies");
                        };
                    }
                    return [actual, &test] { equal_json(actual, test.at("ExpectedColonies"), test.at("Name").get<std::string>() + ".colonies"); };
                } else if (kind == "Labor") {
                    const auto value = colony_labor(colony(test.at("Colony")), test.value("Automation", false), num(test.value("AdditionalJobs", Json(0.0))));
                    return [value, &test] { equal_json({{"PopulationMillions", value.population_millions}, {"WorkingAgePopulationMillions", value.working_age_population_millions}, {"EmployedPopulationMillions", value.employed_population_millions}, {"UnemployedPopulationMillions", value.unemployed_population_millions}, {"EmploymentRate", value.employment_rate}}, test.at("Expected"), test.at("Name")); };
                } else if (kind == "Capacity") {
                    std::vector<PlanetaryBody> bodies;
                    for (const auto& item : test.value("Bodies", Json::array())) bodies.push_back(body(item));
                    const auto& surface = test.at("Surface");
                    const auto value = colony_sustenance_capacity(bodies, colony(test.at("Colony")), {num(surface.at("FoodCapacityMillions")), num(surface.at("WaterCapacityMillions")), num(surface.at("HousingCapacityMillions"))});
                    return [value, &test] { equal_json({{"NaturalFoodCapacityMillions", value.natural_food_capacity_millions}, {"NaturalWaterCapacityMillions", value.natural_water_capacity_millions}, {"NaturalHousingCapacityMillions", value.natural_housing_capacity_millions}, {"BuiltFoodCapacityMillions", value.built_food_capacity_millions}, {"BuiltWaterCapacityMillions", value.built_water_capacity_millions}, {"BuiltHousingCapacityMillions", value.built_housing_capacity_millions}, {"FoodCapacityMillions", value.food_capacity_millions}, {"WaterCapacityMillions", value.water_capacity_millions}, {"HousingCapacityMillions", value.housing_capacity_millions}, {"SupportedPopulationMillions", value.supported_population_millions}, {"SupportRatio", value.support_ratio}, {"LimitingSupply", value.limiting_supply}}, test.at("Expected"), test.at("Name")); };
                } else if (kind == "Reserves") {
                    auto value = colony(test.at("Colony")); const auto original = value; const auto input_capacity = capacity(test.at("Capacity")); const auto days = num(test.at("Days"));
                    const auto preview = preview_colony_reserves(value, input_capacity, days);
                    const auto after_preview = value;
                    const auto advanced = advance_colony_reserves(value, input_capacity, days);
                    return [value, original, after_preview, preview, advanced, &test] {
                        equal_json(colony_json(after_preview), colony_json(original), test.at("Name").get<std::string>() + ".preview mutation");
                        equal_json(reserve_json(preview), test.at("ExpectedPreview"), test.at("Name").get<std::string>() + ".preview");
                        equal_json(reserve_json(advanced), test.at("ExpectedAdvance"), test.at("Name").get<std::string>() + ".advance");
                        equal_json(colony_json(value), test.at("ExpectedColony"), test.at("Name").get<std::string>() + ".colony");
                    };
                } else if (kind == "Treasury") {
                    const auto value = assess_treasury(num(test.at("Balance")), num(test.at("NetPerDay")), num(test.value("Arrears", Json(0.0))));
                    return [value, &test] { equal_json({{"State", value.state}, {"RunwayDays", value.runway_days}, {"Balance", value.balance}, {"NetPerDay", value.net_per_day}}, test.at("Expected"), test.at("Name")); };
                } else {
                    throw std::runtime_error("Unknown colony case kind: " + kind);
                }
            });
            ++count;
        }
        std::cout << "colony_economy_tests: passed; " << count << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "colony_economy_tests failed: " << error.what() << '\n';
        return 1;
    }
}
