#include <stellar/core/species_environment.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace stellar::core;
using Json=nlohmann::json;
namespace {
void require(bool condition,const std::string& message) { if(!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& call) {
    try { call(); } catch(const std::exception& e) { require(std::string(e.what()).size()>8,"Failure has no useful message"); return; }
    throw std::runtime_error("Invalid species/homeworld input was accepted");
}
template<class F> void rejects_with(F&& call,const std::string& diagnostic) {
    try { call(); } catch(const std::exception& e) {
        require(std::string(e.what()).find(diagnostic)!=std::string::npos,"Failure lost diagnostic context: "+std::string(e.what())); return;
    }
    throw std::runtime_error("Invalid home selection was accepted");
}
template<class T> std::optional<T> optional(const Json& j) { if(j.is_null()) return {}; return j.get<T>(); }
void compare(const Json& actual,const Json& expected,const std::string& path) {
    if(actual.is_number() && expected.is_number()) {
        const double a=actual.get<double>(),b=expected.get<double>();
        require(std::isfinite(a) && std::abs(a-b)<=1e-13*std::max(1.0,std::abs(b)),path+": "+actual.dump()+" != "+expected.dump());
    } else if(expected.is_object()) {
        require(actual.is_object() && actual.size()==expected.size(),path+" object fields changed");
        for(auto it=expected.begin();it!=expected.end();++it) compare(actual.at(it.key()),it.value(),path+"."+it.key());
    } else if(expected.is_array()) {
        require(actual.is_array() && actual.size()==expected.size(),path+" array length changed");
        for(std::size_t i=0;i<expected.size();++i) compare(actual[i],expected[i],path+"["+std::to_string(i)+"]");
    } else require(actual==expected,path+": "+actual.dump()+" != "+expected.dump());
}
Json band_json(const ToleranceBand& b) { return {{"Preferred",b.preferred},{"ComfortableDeviation",b.comfortable_deviation},{"SurvivableDeviation",b.survivable_deviation}}; }
Json profile_json(const SpeciesEnvironmentProfile& s) {
    auto atmospheres=s.breathable_atmospheres; auto solvents=s.compatible_solvents;
    std::sort(atmospheres.begin(),atmospheres.end()); std::sort(solvents.begin(),solvents.end());
    return {{"Id",s.id},{"DisplayName",s.display_name},{"Biochemistry",s.biochemistry},
        {"GravityG",band_json(s.gravity_g)},{"TemperatureKelvin",band_json(s.temperature_kelvin)},{"PressureKPa",band_json(s.pressure_kpa)},
        {"PreferredAtmosphere",s.preferred_atmosphere},{"BiologicalSolvent",s.biological_solvent},
        {"RequiresImmersion",s.requires_immersion},{"CanOperateInVacuumUnprotected",s.can_operate_in_vacuum_unprotected},
        {"RadiationTolerance",s.radiation_tolerance},{"BreathableAtmospheres",atmospheres},{"CompatibleSolvents",solvents}};
}
HabitatEnvironment habitat(const Json& j) {
    return {j.at("GravityG"),j.at("TemperatureKelvin"),j.at("PressureKPa"),j.at("Atmosphere"),
        j.at("AvailableSolvent"),j.at("RadiationHazard"),j.at("IsImmersed")};
}
Json habitat_json(const HabitatEnvironment& h) {
    return {{"GravityG",h.gravity_g},{"TemperatureKelvin",h.temperature_kelvin},{"PressureKPa",h.pressure_kpa},
        {"Atmosphere",h.atmosphere},{"AvailableSolvent",h.available_solvent},{"RadiationHazard",h.radiation_hazard},{"IsImmersed",h.is_immersed}};
}
PopulationAdaptation adaptation(const Json& j) {
    return {j.at("SpeciesId"),j.at("GravityPreferenceShiftG"),j.at("GravityToleranceBonusG"),
        j.at("TemperaturePreferenceShiftKelvin"),j.at("TemperatureToleranceBonusKelvin"),j.at("PressurePreferenceShiftKPa"),
        j.at("PressureToleranceBonusKPa"),j.at("RadiationToleranceBonus"),j.at("Acclimatization")};
}
Json assessment_json(const SpeciesEnvironmentAssessment& a) {
    return {{"NaturalHabitability",a.natural_habitability},{"UnprotectedOperationalCapacity",a.unprotected_operational_capacity},
        {"GravitySuitability",a.gravity_suitability},{"TemperatureSuitability",a.temperature_suitability},{"PressureSuitability",a.pressure_suitability},
        {"AtmosphereSuitability",a.atmosphere_suitability},{"SolventSuitability",a.solvent_suitability},{"ImmersionSuitability",a.immersion_suitability},
        {"RadiationSuitability",a.radiation_suitability},{"LimitingFactor",a.limiting_factor},
        {"RequiresGravityMitigation",a.requires_gravity_mitigation},{"RequiresThermalControl",a.requires_thermal_control},
        {"RequiresPressureControl",a.requires_pressure_control},{"RequiresSealedHabitat",a.requires_sealed_habitat},
        {"RequiresArtificialBiosphere",a.requires_artificial_biosphere},{"RequiresRadiationShielding",a.requires_radiation_shielding},
        {"HealthStress",1.0-a.natural_habitability}};
}
PlanetaryBody read_body(const Json& j) {
    const auto& e=j.at("Environment");
    return {j.at("Id"),j.at("SystemId"),optional<int>(j.at("ParentBodyId")),j.at("OrbitIndex"),j.at("Name"),j.at("Kind"),
        j.at("RadiusEarth"),j.at("MassEarth"),{e.at("GravityG"),e.at("TemperatureKelvin"),e.at("PressureKPa"),e.at("Atmosphere"),
            e.at("AvailableSolvent"),e.at("RadiationHazard"),e.at("IsImmersedEnvironment"),e.at("HasSolidSurface")},
        j.at("LegacyColonizationCandidate"),j.at("HasRareResource"),j.at("HasAnomaly"),j.at("HasPreWarpCivilization"),
        j.value("OrbitalEccentricity",0.0),j.value("OrbitalInclinationDegrees",0.0)};
}
StellarSystem read_system(const Json& j) {
    return {j.at("Id"),j.at("Name"),{j.at("Position").at("X"),j.at("Position").at("Y"),optional<double>(j.at("GalacticDepthLightYears"))},
        optional<StellarClass>(j.at("StellarClass")),optional<StellarClass>(j.at("SecondaryStellarClass")),optional<StellarClass>(j.at("TertiaryStellarClass")),
        optional<std::string>(j.at("CatalogPresetId")),optional<std::string>(j.at("StellarCatalogId")),j.at("Archetype"),
        j.at("HasHabitableWorld"),j.at("HasAnomaly"),j.at("HasRareResource"),j.at("HasPreWarpCivilization")};
}
Json home_json(const SpeciesHomeworldAssignment& h) {
    return {{"CivilizationId",h.civilization_id},{"SpeciesId",h.species_id},{"SystemId",h.system_id},
        {"PlanetaryBodyId",h.planetary_body_id},{"NaturalHabitability",h.natural_habitability},{"Suitability",h.suitability}};
}
void invalid_inputs() {
    const auto& species=species_environment_profile("terran_baseline");
    const HabitatEnvironment ideal{1,288,101.3,SpeciesAtmosphere::OxygenNitrogen,SpeciesSolvent::Water,.06,false};
    rejects([] { species_environment_profile("unknown_species"); });
    rejects([] { assign_species(1,-1,true); });
    auto invalid=ideal; invalid.gravity_g=std::numeric_limits<double>::quiet_NaN();
    rejects([&] { evaluate_species_environment(species,invalid); });
    PopulationAdaptation wrong{"pelagic_high_pressure"};
    rejects([&] { evaluate_species_environment(species,ideal,&wrong); });
    wrong.species_id=species.id; wrong.pressure_tolerance_bonus_kpa=-1;
    rejects([&] { evaluate_species_environment(species,ideal,&wrong); });
    wrong.pressure_tolerance_bonus_kpa=0; wrong.acclimatization=1.1;
    rejects([&] { evaluate_species_environment(species,ideal,&wrong); });
    const std::vector<std::string> empty;
    require(plan_species_homeworlds({}, {},empty).empty(),"Empty civilization list should have no homes");
    const std::vector<std::string> one{species.id};
    rejects([&] { plan_species_homeworlds({}, {},one); });
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected species parity fixture path");
        std::ifstream input(argv[1]); const auto fixture=Json::parse(input);
        require(fixture.at("Format")=="stellar-species-parity-v1","Unknown species fixture format");
        require(species_environment_profiles().size()==fixture.at("Profiles").size(),"Species count changed");
        for(const auto& j:fixture.at("Profiles")) compare(profile_json(species_environment_profile(j.at("Id"))),j,"profile");
        std::size_t environment_count=0,home_count=0;
        for(const auto& c:fixture.at("EnvironmentCases")) {
            const auto& species=species_environment_profile(c.at("SpeciesId"));
            std::optional<PopulationAdaptation> adapted;
            if(!c.at("Adaptation").is_null()) adapted=adaptation(c.at("Adaptation"));
            const auto actual=evaluate_species_environment(species,habitat(c.at("Habitat")),adapted?&*adapted:nullptr);
            compare(assessment_json(actual),c.at("Expected"),"environment "+std::to_string(environment_count++));
        }
        for(const auto& c:fixture.at("AssignmentCases"))
            compare(assign_species(c.at("Seed"),c.at("CivilizationId"),c.at("Canonical")),c.at("Expected"),"assignment");
        for(const auto& c:fixture.at("PlanetCases")) {
            const auto body=read_body(c.at("Body"));
            const auto actual=assess_species_planet(species_environment_profile(c.at("SpeciesId")),body);
            compare(Json{{"PlanetaryBodyId",actual.planetary_body_id},{"SpeciesId",actual.species_id},
                {"Habitat",habitat_json(to_species_habitat(body.environment))},{"Environment",assessment_json(actual.environment)},
                {"Suitability",actual.suitability},{"HasPhysicalSettlementSite",actual.has_physical_settlement_site},
                {"NaturallyColonizable",actual.naturally_colonizable},{"NaturalHabitability",actual.environment.natural_habitability}},
                c.at("Expected"),"planet "+std::to_string(body.id));
        }
        for(const auto& c:fixture.at("HomeCases")) {
            std::vector<StellarSystem> systems; for(const auto& s:c.at("Systems")) systems.push_back(read_system(s));
            std::vector<PlanetaryBody> bodies; for(const auto& b:c.at("Bodies")) bodies.push_back(read_body(b));
            const auto species=c.at("SpeciesIds").get<std::vector<std::string>>();
            Json homes=Json::array();
            for(const auto& home:plan_species_homeworlds(systems,bodies,species)) homes.push_back(home_json(home));
            compare(homes,c.at("Expected"),"home scenario "+std::to_string(home_count++));
            if(c.at("Kind")=="canonical") {
                const std::vector<std::string> competing_humans{"terran_baseline","terran_baseline"};
                rejects_with([&] { plan_species_homeworlds(systems,bodies,competing_humans); },"civ 1 / terran_baseline. Compatible systems: [0]");
                auto inhabited=bodies;
                for(auto& body:inhabited) if(body.system_id==0 && body.id==3) body.has_pre_warp_civilization=true;
                rejects_with([&] { plan_species_homeworlds(systems,inhabited,species); },"civ 0 / terran_baseline");
            }
            if(!systems.empty()) { systems.push_back(systems.front()); rejects([&] { plan_species_homeworlds(systems,bodies,species); }); }
        }
        invalid_inputs();
        std::cout<<"species_tests: passed "<<environment_count<<" environments, "<<fixture.at("AssignmentCases").size()
            <<" seeded assignments, "<<fixture.at("PlanetCases").size()<<" planet assessments and "<<home_count<<" home scenarios\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"species_tests failed: "<<e.what()<<'\n'; return 1; }
}
