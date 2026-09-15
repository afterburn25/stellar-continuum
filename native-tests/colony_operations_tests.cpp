#include <stellar/core/colony_operations.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using Json=nlohmann::json; using namespace stellar::core;
namespace {
void check(bool v,const std::string& m){if(!v)throw std::runtime_error(m);} double num(const Json& v){if(v.is_number())return v.get<double>();auto s=v.get<std::string>();if(s=="NaN")return NAN;if(s=="Infinity")return INFINITY;if(s=="-Infinity")return -INFINITY;throw std::runtime_error("bad float");}
template<class T> std::optional<T> opt(const Json& v){return v.is_null()?std::nullopt:std::optional<T>{v.get<T>()};}
SurfaceBuilding building(const Json& v){SurfaceBuilding b;b.id=v.at("Id");b.type_id=v.at("TypeId");b.x=v.at("X").get<float>();b.z=v.at("Z").get<float>();b.rotation_degrees=v.at("RotationDegrees").get<float>();b.industry_progress=num(v.at("IndustryProgress"));b.is_complete=v.at("IsComplete");b.is_enabled=v.at("IsEnabled");b.pending_upgrade_type_id=opt<std::string>(v.at("PendingUpgradeTypeId"));b.upgrade_days_remaining=num(v.at("UpgradeDaysRemaining"));b.operating_priority=v.at("OperatingPriority");b.condition=num(v.at("Condition"));b.stored_power_days=num(v.at("StoredPowerDays"));return b;}
Json building_json(const SurfaceBuilding& b){return {{"Id",b.id},{"TypeId",b.type_id},{"X",b.x},{"Z",b.z},{"RotationDegrees",b.rotation_degrees},{"IndustryProgress",b.industry_progress},{"IsComplete",b.is_complete},{"IsEnabled",b.is_enabled},{"PendingUpgradeTypeId",b.pending_upgrade_type_id},{"UpgradeDaysRemaining",b.upgrade_days_remaining},{"OperatingPriority",b.operating_priority},{"Condition",b.condition},{"StoredPowerDays",b.stored_power_days}};}
Colony colony(const Json& v){Colony c;c.id=v.at("Id");c.civilization_id=v.at("CivilizationId");c.system_id=v.at("SystemId");c.planetary_body_id=opt<int>(v.at("PlanetaryBodyId"));c.name=v.at("Name");c.kind=static_cast<SettlementKind>(v.at("Kind").get<int>());c.population_species_id=v.at("PopulationSpeciesId");c.population_millions=num(v.at("PopulationMillions"));c.infrastructure=num(v.at("Infrastructure"));c.stability=num(v.at("Stability"));c.stored_food_population_days_millions=num(v.at("StoredFoodPopulationDaysMillions"));c.stored_water_population_days_millions=num(v.at("StoredWaterPopulationDaysMillions"));c.stored_extracted_materials=num(v.at("StoredExtractedMaterials"));c.remaining_extractable_materials=v.at("RemainingExtractableMaterials").is_null()?std::nullopt:std::optional<double>{num(v.at("RemainingExtractableMaterials"))};c.surface_hub_level=v.at("SurfaceHubLevel");c.surface_hub_upgrade_days_remaining=num(v.at("SurfaceHubUpgradeDaysRemaining"));for(auto& b:v.at("SurfaceBuildings"))c.surface_buildings.push_back(building(b));return c;}
Json colony_json(const Colony& c){Json b=Json::array();for(auto& x:c.surface_buildings)b.push_back(building_json(x));return {{"Id",c.id},{"CivilizationId",c.civilization_id},{"SystemId",c.system_id},{"PlanetaryBodyId",c.planetary_body_id},{"Name",c.name},{"Kind",static_cast<int>(c.kind)},{"PopulationSpeciesId",c.population_species_id},{"PopulationMillions",c.population_millions},{"Infrastructure",c.infrastructure},{"Stability",c.stability},{"StoredFoodPopulationDaysMillions",c.stored_food_population_days_millions},{"StoredWaterPopulationDaysMillions",c.stored_water_population_days_millions},{"StoredExtractedMaterials",c.stored_extracted_materials},{"RemainingExtractableMaterials",c.remaining_extractable_materials},{"SurfaceHubLevel",c.surface_hub_level},{"SurfaceHubUpgradeDaysRemaining",c.surface_hub_upgrade_days_remaining},{"SurfaceBuildings",b}};}
PlanetaryBody body(const Json& v){PlanetaryBody b;b.id=v.at("Id");b.system_id=v.at("SystemId");b.parent_body_id=opt<int>(v.at("ParentBodyId"));b.orbit_index=v.at("OrbitIndex");b.name=v.at("Name");b.kind=static_cast<PlanetaryBodyKind>(v.at("Kind").get<int>());b.radius_earth=num(v.at("RadiusEarth"));b.mass_earth=num(v.at("MassEarth"));auto&e=v.at("Environment");b.environment={num(e.at("GravityG")),num(e.at("TemperatureKelvin")),num(e.at("PressureKPa")),static_cast<PlanetaryAtmosphereRegime>(e.at("Atmosphere").get<int>()),static_cast<PlanetarySolventRegime>(e.at("AvailableSolvent").get<int>()),num(e.at("RadiationHazard")),e.at("IsImmersedEnvironment"),e.at("HasSolidSurface")};b.legacy_colonization_candidate=v.at("LegacyColonizationCandidate");b.has_rare_resource=v.at("HasRareResource");b.has_anomaly=v.at("HasAnomaly");b.has_pre_warp_civilization=v.at("HasPreWarpCivilization");b.orbital_eccentricity=v.contains("OrbitalEccentricity")?num(v.at("OrbitalEccentricity")):0;b.orbital_inclination_degrees=v.contains("OrbitalInclinationDegrees")?num(v.at("OrbitalInclinationDegrees")):0;return b;}
CivilizationEconomy economy(const Json& v){CivilizationEconomy e;e.civilization_id=v.at("CivilizationId");e.last_base_operations_funding_fraction=num(v.at("LastBaseOperationsFundingFraction"));return e;}
void eq(const Json&a,const Json&e,const std::string&p){if(a.is_number()&&e.is_string()){const auto x=a.get<double>();const auto s=e.get<std::string>();check((s=="NaN"&&std::isnan(x))||(s=="Infinity"&&std::isinf(x)&&x>0)||(s=="-Infinity"&&std::isinf(x)&&x<0),p);return;}if(a.is_number()&&e.is_number()){const auto x=a.get<double>();const auto y=e.get<double>();check((std::isnan(x)&&std::isnan(y))||(std::isinf(x)&&std::isinf(y)&&std::signbit(x)==std::signbit(y))||std::abs(x-y)<=1e-12*std::max(1.,std::abs(y)),p);return;}if(a.is_array()){check(e.is_array()&&a.size()==e.size(),p);for(size_t i=0;i<a.size();++i)eq(a[i],e[i],p+"[]");return;}if(a.is_object()){check(e.is_object()&&a.size()==e.size(),p);for(auto&[k,v]:e.items()){check(a.contains(k),p+" missing "+k);eq(a.at(k),v,p+"."+k);}return;}check(a==e,p+": expected "+e.dump()+", got "+a.dump());}
Json profile_json(const ResourceDepositProfile&p){return {{"MaterialName",p.material_name},{"Grade",p.grade},{"GradeMultiplier",p.grade_multiplier},{"Accessibility",p.accessibility},{"EnvironmentalHazard",p.environmental_hazard},{"ExtractionYieldMultiplier",p.extraction_yield_multiplier}};}
Json snapshot_json(const ResourceOutpostOperationsSnapshot&s){return {{"IsResourceOutpost",s.is_resource_outpost},{"HasConfirmedDeposit",s.has_confirmed_deposit},{"ExtractionPerDay",s.extraction_per_day},{"StoredMaterials",s.stored_materials},{"StorageCapacity",s.storage_capacity},{"RemainingDepositMaterials",s.remaining_deposit_materials},{"InitialDepositMaterials",s.initial_deposit_materials},{"DepositMaterialName",s.deposit_material_name},{"DepositGrade",s.deposit_grade},{"DepositAccessibility",s.deposit_accessibility},{"ExtractionYieldMultiplier",s.extraction_yield_multiplier},{"Status",s.status}};}
bool matching_error(const Json& test, const std::string& actual) {
    const auto expected = test.at("ExpectedError").get<std::string>();
    const auto category = test.value("ErrorCategory", "");
    if (category == "Funding")
        return expected.find("Operating funding fraction") != std::string::npos && actual.find("Operating funding fraction") != std::string::npos;
    if (category == "Maintenance")
        return expected.find("Surface maintenance requires") != std::string::npos && actual.find("Surface maintenance requires") != std::string::npos;
    return actual.find(expected) != std::string::npos;
}
template<class F> void operation(const Json& test, F call) {
    std::function<void()> compare;
    std::string error;
    try { compare = call(); }
    catch (const std::exception& exception) { error = exception.what(); }
    const auto name = test.at("Name").get<std::string>();
    if (test.contains("ExpectedError")) {
        check(!error.empty(), name + ": expected operation failure but succeeded");
        check(matching_error(test, error), name + ": unexpected native error: " + error);
        return;
    }
    check(error.empty(), name + ": native operation threw: " + error);
    compare();
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected colony operations oracle path");
        std::ifstream input(argv[1]); check(input.good(), "Unable to read oracle");
        const auto fixture = Json::parse(input);
        check(fixture.at("Format") == "stellar-colony-operations-parity-v1", "Unknown oracle");
        size_t count = 0;
        for (const auto& test : fixture.at("Cases")) {
            std::vector<PlanetaryBody> bodies;
            for (const auto& item : test.value("Bodies", Json::array())) bodies.push_back(body(item));
            std::vector<CivilizationEconomy> economies;
            for (const auto& item : test.value("Economies", Json::array())) economies.push_back(economy(item));
            const auto kind = test.at("Kind").get<std::string>();
            check(kind == "Profile" || kind == "Snapshot" || kind == "Advance" || kind == "Wear", "Unknown case kind: " + kind);
            const auto colony_input = kind == "Profile" ? Colony{} : colony(test.at("Colony"));
            const auto profile_input = kind == "Profile" ? std::optional<PlanetaryBody>{body(test.at("Body"))} : std::nullopt;
            const auto funding = test.contains("Funding") && !test.at("Funding").is_null() ? std::optional<double>{num(test.at("Funding"))} : std::nullopt;
            const double days = test.contains("Days") ? num(test.at("Days")) : 0;
            operation(test, [&]() -> std::function<void()> {
                if (kind == "Profile") {
                    const auto profile = resource_deposit_profile(*profile_input);
                    const auto reserve = initial_deposit_reserve(*profile_input);
                    return [profile, reserve, &test] { eq(profile_json(profile), test.at("Expected"), "profile"); eq(Json(reserve), test.at("ExpectedReserve"), "reserve"); };
                }
                if (kind == "Snapshot") {
                    const auto result = resource_outpost_snapshot(bodies, economies, colony_input, funding);
                    return [result, colony_input, &test] { eq(colony_json(colony_input), test.at("ExpectedColonyBefore"), "before"); eq(colony_json(colony_input), test.at("ExpectedColonyAfter"), "after"); eq(snapshot_json(result), test.at("Expected"), "snapshot"); };
                }
                auto result = colony_input;
                if (kind == "Advance") {
                    advance_resource_outpost(bodies, economies, result, days, *funding);
                    return [result, colony_input, &test] { eq(colony_json(colony_input), test.at("ExpectedColonyBefore"), "before"); eq(colony_json(result), test.at("ExpectedColonyAfter"), "after"); };
                }
                if (kind == "Wear") {
                    const auto multiplier = surface_environmental_wear(bodies, result);
                    advance_surface_condition(bodies, result, *funding, days);
                    return [multiplier, result, colony_input, &test] { eq(Json(multiplier), test.at("ExpectedMultiplier"), "wear"); eq(colony_json(colony_input), test.at("ExpectedColonyBefore"), "before"); eq(colony_json(result), test.at("ExpectedColonyAfter"), "after"); };
                }
                return [] {};
            });
            ++count;
        }
        std::cout << "colony_operations_tests: passed; " << count << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "colony_operations_tests failed: " << error.what() << '\n'; return 1;
    }
}
