#include <stellar/core/surface_economy.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using Json = nlohmann::json;
using namespace stellar::core;
namespace {
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
double number(const Json& value) {
    if (value.is_number()) return value.get<double>();
    const auto text = value.get<std::string>();
    if (text == "Infinity") return INFINITY; if (text == "-Infinity") return -INFINITY; if (text == "NaN") return NAN;
    throw std::runtime_error("invalid named floating-point literal: " + text);
}
template<class T> std::optional<T> optional(const Json& value) { return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()}; }
SurfaceBuilding building(const Json& value) {
    SurfaceBuilding result; result.id=value.at("Id"); result.type_id=value.at("TypeId"); result.x=value.at("X").get<float>(); result.z=value.at("Z").get<float>(); result.rotation_degrees=value.at("RotationDegrees").get<float>();
    result.industry_progress=number(value.at("IndustryProgress")); result.is_complete=value.at("IsComplete"); result.is_enabled=value.at("IsEnabled"); result.pending_upgrade_type_id=optional<std::string>(value.at("PendingUpgradeTypeId")); result.upgrade_days_remaining=number(value.at("UpgradeDaysRemaining")); result.operating_priority=value.at("OperatingPriority"); result.condition=number(value.at("Condition")); result.stored_power_days=number(value.at("StoredPowerDays")); return result;
}
Json building_json(const SurfaceBuilding& b) { return {{"Id",b.id},{"TypeId",b.type_id},{"X",b.x},{"Z",b.z},{"RotationDegrees",b.rotation_degrees},{"IndustryProgress",b.industry_progress},{"IsComplete",b.is_complete},{"IsEnabled",b.is_enabled},{"PendingUpgradeTypeId",b.pending_upgrade_type_id},{"UpgradeDaysRemaining",b.upgrade_days_remaining},{"OperatingPriority",b.operating_priority},{"Condition",b.condition},{"StoredPowerDays",b.stored_power_days}}; }
Colony colony(const Json& value) {
    Colony result; result.id=value.at("Id"); result.civilization_id=value.at("CivilizationId"); result.system_id=value.at("SystemId"); result.planetary_body_id=optional<int>(value.at("PlanetaryBodyId")); result.name=value.at("Name"); result.kind=value.at("Kind"); result.population_species_id=value.at("PopulationSpeciesId"); result.population_millions=number(value.at("PopulationMillions")); result.infrastructure=number(value.at("Infrastructure")); result.stability=number(value.at("Stability")); result.stored_food_population_days_millions=number(value.at("StoredFoodPopulationDaysMillions")); result.stored_water_population_days_millions=number(value.at("StoredWaterPopulationDaysMillions")); result.stored_extracted_materials=number(value.at("StoredExtractedMaterials")); result.remaining_extractable_materials=value.at("RemainingExtractableMaterials").is_null()?std::nullopt:std::optional<double>{number(value.at("RemainingExtractableMaterials"))}; result.surface_hub_level=value.at("SurfaceHubLevel"); result.surface_hub_upgrade_days_remaining=number(value.at("SurfaceHubUpgradeDaysRemaining")); for(const auto& item:value.at("SurfaceBuildings")) result.surface_buildings.push_back(building(item)); return result;
}
Json colony_json(const Colony& c) { Json buildings=Json::array(); for(const auto& b:c.surface_buildings) buildings.push_back(building_json(b)); return {{"Id",c.id},{"CivilizationId",c.civilization_id},{"SystemId",c.system_id},{"PlanetaryBodyId",c.planetary_body_id},{"Name",c.name},{"Kind",c.kind},{"PopulationSpeciesId",c.population_species_id},{"PopulationMillions",c.population_millions},{"Infrastructure",c.infrastructure},{"Stability",c.stability},{"StoredFoodPopulationDaysMillions",c.stored_food_population_days_millions},{"StoredWaterPopulationDaysMillions",c.stored_water_population_days_millions},{"StoredExtractedMaterials",c.stored_extracted_materials},{"RemainingExtractableMaterials",c.remaining_extractable_materials},{"SurfaceHubLevel",c.surface_hub_level},{"SurfaceHubUpgradeDaysRemaining",c.surface_hub_upgrade_days_remaining},{"SurfaceBuildings",buildings}}; }
Json output_json(const SurfaceColonyOutput& o) { return {{"Supply",o.supply},{"Demand",o.demand},{"SciencePerDay",o.science_per_day},{"IndustryPerDay",o.industry_per_day},{"CreditsPerDay",o.credits_per_day},{"UpkeepCreditsPerDay",o.upkeep_credits_per_day},{"PoweredBuildingIds",o.powered_building_ids},{"HabitatSupportReduction",o.habitat_support_reduction},{"FoodCapacityMillions",o.food_capacity_millions},{"WaterCapacityMillions",o.water_capacity_millions},{"HousingCapacityMillions",o.housing_capacity_millions},{"WorkforceAvailableMillions",o.workforce_available_millions},{"WorkforceDemandMillions",o.workforce_demand_millions},{"StaffedBuildingIds",o.staffed_building_ids},{"StoredPowerDays",o.stored_power_days},{"PowerStorageCapacityDays",o.power_storage_capacity_days},{"StorageChargePerDay",o.storage_charge_per_day},{"StorageDischargePerDay",o.storage_discharge_per_day},{"CargoTransferCapacityPerDay",o.cargo_transfer_capacity_per_day}}; }
void equal_json(const Json& actual,const Json& expected,const std::string& path) {
    if(actual.is_number()&&expected.is_string()){const auto text=expected.get<std::string>(); const auto value=actual.get<double>(); check((text=="Infinity"&&std::isinf(value)&&value>0)||(text=="-Infinity"&&std::isinf(value)&&value<0)||(text=="NaN"&&std::isnan(value)),path+": named float mismatch"); return;}
    if(actual.is_number()&&expected.is_number()){if(actual.is_number_integer()&&expected.is_number_integer()){check(actual==expected,path);return;}const auto a=actual.get<double>(),e=expected.get<double>();const bool f=path.ends_with(".X")||path.ends_with(".Z")||path.ends_with(".RotationDegrees")||path.ends_with(".FootprintRadius");check((f&&static_cast<float>(a)==static_cast<float>(e))||(!f&&((std::isnan(a)&&std::isnan(e))||(std::isinf(a)&&std::isinf(e)&&std::signbit(a)==std::signbit(e))||std::abs(a-e)<=1e-13*std::max(1.0,std::abs(e)))),path+": expected "+expected.dump()+", got "+actual.dump());return;}
    if(actual.is_array()&&expected.is_array()){check(actual.size()==expected.size(),path+": array size");for(size_t i=0;i<actual.size();++i)equal_json(actual[i],expected[i],path+"["+std::to_string(i)+"]");return;}
    if(actual.is_object()&&expected.is_object()){check(actual.size()==expected.size(),path+": field count");for(const auto&[key,value]:expected.items()){check(actual.contains(key),path+": missing "+key);equal_json(actual.at(key),value,path+"."+key);}return;}check(actual==expected,path+": expected "+expected.dump()+", got "+actual.dump());
}
void equal_id_sets(Json actual,Json expected,const std::string& path) { std::sort(actual.begin(),actual.end()); std::sort(expected.begin(),expected.end()); equal_json(actual,expected,path); }
void equal_output(Json actual, Json expected, const std::string& path) {
    equal_id_sets(actual.at("PoweredBuildingIds"), expected.at("PoweredBuildingIds"), path + ".powered ids");
    equal_id_sets(actual.at("StaffedBuildingIds"), expected.at("StaffedBuildingIds"), path + ".staffed ids");
    actual.erase("PoweredBuildingIds"); actual.erase("StaffedBuildingIds");
    expected.erase("PoweredBuildingIds"); expected.erase("StaffedBuildingIds");
    equal_json(actual, expected, path);
}
Json sustenance_json(const ColonySustenanceCapacity& value) { return {
    {"NaturalFoodCapacityMillions",value.natural_food_capacity_millions},
    {"NaturalWaterCapacityMillions",value.natural_water_capacity_millions},
    {"NaturalHousingCapacityMillions",value.natural_housing_capacity_millions},
    {"BuiltFoodCapacityMillions",value.built_food_capacity_millions},
    {"BuiltWaterCapacityMillions",value.built_water_capacity_millions},
    {"BuiltHousingCapacityMillions",value.built_housing_capacity_millions},
    {"FoodCapacityMillions",value.food_capacity_millions},
    {"WaterCapacityMillions",value.water_capacity_millions},
    {"HousingCapacityMillions",value.housing_capacity_millions},
    {"SupportedPopulationMillions",value.supported_population_millions},
    {"SupportRatio",value.support_ratio},{"LimitingSupply",value.limiting_supply}}; }
Json reserve_json(const ColonySustenanceReserveSnapshot& value) { return {
    {"FoodReserveDays",value.food_reserve_days},{"WaterReserveDays",value.water_reserve_days},
    {"EffectiveSupportRatio",value.effective_support_ratio},{"LimitingSupply",value.limiting_supply}}; }
Json catalog_json(const SurfaceBuildingDefinition& d) { return {{"Id",d.id},{"Name",d.name},{"Description",d.description},{"IndustryCost",d.industry_cost},{"FootprintRadius",d.footprint_radius},{"PowerSupply",d.power_supply},{"PowerDemand",d.power_demand},{"SciencePerDay",d.science_per_day},{"IndustryPerDay",d.industry_per_day},{"CreditCost",d.credit_cost},{"CreditsPerDay",d.credits_per_day},{"UpkeepCreditsPerDay",d.upkeep_credits_per_day},{"AvailableForPlacement",d.available_for_placement},{"UpgradeTypeId",d.upgrade_type_id},{"UpgradeCreditCost",d.upgrade_credit_cost},{"UpgradeIndustryCost",d.upgrade_industry_cost},{"HabitatSupportReduction",d.habitat_support_reduction},{"FoodCapacityMillions",d.food_capacity_millions},{"WaterCapacityMillions",d.water_capacity_millions},{"HousingCapacityMillions",d.housing_capacity_millions},{"WorkforceRequiredMillions",d.workforce_required_millions},{"UpgradeRequirementId",d.upgrade_requirement_id},{"UpgradeRequirementName",d.upgrade_requirement_name},{"PowerStorageDays",d.power_storage_days},{"PowerChargeRate",d.power_charge_rate},{"PowerDischargeRate",d.power_discharge_rate},{"CargoTransferCapacityPerDay",d.cargo_transfer_capacity_per_day}}; }
template<class Compute> void operation(const Json& test, Compute compute) {
    std::function<void()> compare;
    try { compare=compute(); }
    catch(const std::exception& error) {
        if(!test.contains("ExpectedError")) throw std::runtime_error(test.at("Name").get<std::string>()+": native operation threw: "+error.what());
        const auto wanted=test.at("ExpectedError").get<std::string>(); const auto native=std::string(error.what());
        const bool equivalent =
            (wanted.find("finite positive interval")!=std::string::npos && native.find("finite positive interval")!=std::string::npos) ||
            (wanted.find("finite nonnegative elapsed days")!=std::string::npos && native.find("finite nonnegative elapsed days")!=std::string::npos) ||
            (test.at("Kind")=="UnknownType" && native.find("unknown type")!=std::string::npos) || native.find(wanted)!=std::string::npos;
        check(equivalent,test.at("Name").get<std::string>()+": unexpected native error: "+error.what()); return;
    }
    check(!test.contains("ExpectedError"),test.at("Name").get<std::string>()+": expected operation failure but succeeded"); compare();
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected surface economy oracle path"); std::ifstream input(argv[1]); check(input.good(),"Unable to read surface economy oracle"); const auto fixture=Json::parse(input); check(fixture.at("Format")=="stellar-surface-economy-parity-v1","Unknown surface economy oracle format");
        Json catalog=Json::array(); for(const auto& definition:surface_building_catalog()) catalog.push_back(catalog_json(definition)); equal_json(catalog,fixture.at("Catalog"),"Catalog");
        size_t count=0;
        for(const auto& test:fixture.at("Cases")) {
            const auto kind=test.at("Kind").get<std::string>();
            operation(test,[&]() -> std::function<void()> {
                if(kind=="Catalog") { return [&test]{ Json actual=Json::array(); for(const auto& definition:surface_building_catalog()) actual.push_back(catalog_json(definition)); equal_json(actual,test.at("Expected"),test.at("Name")); }; }
                if(kind=="Output") { const auto before=colony(test.at("Colony")); auto value=before; const auto output=surface_colony_output(value,number(test.value("PowerIntervalDays",Json(1.0)))); const auto after=value; return [before,after,output,&test]{equal_json(colony_json(after),colony_json(before),test.at("Name").get<std::string>()+".preview mutation"); equal_output(output_json(output),test.at("Expected"),test.at("Name"));}; }
                if(kind=="AdvancePower") { auto value=colony(test.at("Colony")); const auto before=value; const auto output=surface_colony_output(value,number(test.value("Interval",Json(1.0)))); advance_surface_power_storage(value,output,number(test.at("Days"))); return [before,value,output,&test]{equal_json(colony_json(before),test.at("ExpectedColonyBefore"),test.at("Name").get<std::string>()+".before");equal_output(output_json(output),test.at("ExpectedOutput"),test.at("Name").get<std::string>()+".output");equal_json(colony_json(value),test.at("ExpectedColonyAfter"),test.at("Name").get<std::string>()+".after");}; }
                if(kind=="Specialization") { const auto value=surface_colony_specialization(colony(test.at("Colony"))); Json actual={{"Id",value.id},{"Name",value.name},{"Description",value.description},{"CompletedComplexes",value.completed_complexes},{"Active",value.active}}; return [actual,&test]{equal_json(actual,test.at("Expected"),test.at("Name"));}; }
                if(kind=="Stage") { const auto value=surface_construction_stage(building(test.at("Building"))); Json actual={{"Id",value.id},{"Name",value.name},{"PhaseProgress",value.phase_progress},{"OverallProgress",value.overall_progress},{"RemainingMaterials",value.remaining_materials}}; return [actual,&test]{equal_json(actual,test.at("Expected"),test.at("Name"));}; }
                if(kind=="Capacity") { const auto actual=surface_building_capacity(colony(test.at("Colony"))); return [actual,&test]{equal_json(actual,test.at("Expected"),test.at("Name"));}; }
                if(kind=="Integration") {
                    const auto value=colony(test.at("Colony")); const auto output=surface_colony_output(value);
                    const auto capacity=colony_sustenance_capacity({},value,surface_sustenance_projection(output));
                    const auto reserve=preview_colony_reserves(value,capacity,number(test.at("Days")));
                    return [value,output,capacity,reserve,&test]{
                        equal_json(colony_json(value),test.at("Colony"),test.at("Name").get<std::string>()+".state");
                        equal_output(output_json(output),test.at("ExpectedOutput"),test.at("Name").get<std::string>()+".output");
                        equal_json(sustenance_json(capacity),test.at("ExpectedSustenance"),test.at("Name").get<std::string>()+".sustenance");
                        equal_json(reserve_json(reserve),test.at("ExpectedReservePreview"),test.at("Name").get<std::string>()+".reserve");
                    };
                }
                if(kind=="InvalidOutput") { auto value=colony(test.at("Colony")); (void)surface_colony_output(value,number(test.at("PowerIntervalDays"))); return []{}; }
                if(kind=="InvalidAdvance") { auto value=colony(test.at("Colony")); const auto output=surface_colony_output(value); advance_surface_power_storage(value,output,number(test.at("Days"))); return []{}; }
                if(kind=="UnknownType") { auto value=colony(test.at("Colony")); (void)surface_colony_output(value); return []{}; }
                throw std::runtime_error("Unknown surface case kind: "+kind);
            }); ++count;
        }
        std::cout<<"surface_economy_tests: passed; "<<count<<" cases\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"surface_economy_tests failed: "<<error.what()<<'\n'; return 1; }
}
