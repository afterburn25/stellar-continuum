#include <stellar/core/civilization_catalog.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace stellar::core;
using Json=nlohmann::json;
namespace {
void check(bool ok,const std::string& context) { if(!ok) throw std::runtime_error(context); }
template<class T> std::optional<T> optional(const Json& j) {
    return j.is_null()?std::nullopt:std::optional<T>{j.get<T>()};
}
StellarSystem read_system(const Json& j) {
    StellarSystem s; s.id=j.at("Id"); s.name=j.at("Name");
    s.position={j.at("X"),j.at("Y"),optional<double>(j.at("Depth"))};
    s.primary=optional<StellarClass>(j.at("Primary")); s.secondary=optional<StellarClass>(j.at("Secondary"));
    s.tertiary=optional<StellarClass>(j.at("Tertiary")); s.catalog_preset_id=optional<std::string>(j.at("CatalogPresetId"));
    s.stellar_catalog_id=optional<std::string>(j.at("StellarCatalogId")); s.archetype=j.at("Archetype");
    s.has_habitable_world=j.at("HasHabitableWorld"); s.has_anomaly=j.at("HasAnomaly");
    s.has_rare_resource=j.at("HasRareResource"); s.has_pre_warp_civilization=j.at("HasPreWarpCivilization"); return s;
}
Json system_json(const StellarSystem& s) {
    return {{"Id",s.id},{"Name",s.name},{"X",s.position.x},{"Y",s.position.y},{"Depth",s.position.depth_light_years},
        {"Primary",s.primary},{"Secondary",s.secondary},{"Tertiary",s.tertiary},{"CatalogPresetId",s.catalog_preset_id},
        {"StellarCatalogId",s.stellar_catalog_id},{"Archetype",s.archetype},{"HasHabitableWorld",s.has_habitable_world},
        {"HasAnomaly",s.has_anomaly},{"HasRareResource",s.has_rare_resource},{"HasPreWarpCivilization",s.has_pre_warp_civilization}};
}
PlanetaryBody read_body(const Json& r) {
    PlanetaryBody b; b.id=r.at(0); b.system_id=r.at(1); b.parent_body_id=optional<int>(r.at(2));
    b.orbit_index=r.at(3); b.name=r.at(4); b.kind=r.at(5); b.radius_earth=r.at(6); b.mass_earth=r.at(7);
    b.environment={r.at(8),r.at(9),r.at(10),r.at(11),r.at(12),r.at(13),r.at(14),r.at(15)};
    b.legacy_colonization_candidate=r.at(16); b.has_rare_resource=r.at(17); b.has_anomaly=r.at(18);
    b.has_pre_warp_civilization=r.at(19); b.orbital_eccentricity=r.at(20); b.orbital_inclination_degrees=r.at(21); return b;
}
Json body_json(const PlanetaryBody& b) {
    const auto& e=b.environment;
    return Json::array({b.id,b.system_id,b.parent_body_id,b.orbit_index,b.name,b.kind,b.radius_earth,b.mass_earth,
        e.gravity_g,e.temperature_kelvin,e.pressure_kpa,e.atmosphere,e.available_solvent,e.radiation_hazard,
        e.is_immersed_environment,e.has_solid_surface,b.legacy_colonization_candidate,b.has_rare_resource,
        b.has_anomaly,b.has_pre_warp_civilization,b.orbital_eccentricity,b.orbital_inclination_degrees});
}
Civilization read_civilization(const Json& j) {
    Civilization c; c.id=j.at("Id"); c.name=j.at("Name"); c.home_system_id=j.at("HomeSystemId");
    c.archetype=j.at("Archetype"); const auto& t=j.at("Traits");
    c.traits={t.at("Aggression"),t.at("Territoriality"),t.at("Greed"),t.at("ScientificCuriosity"),
        t.at("RiskTolerance"),t.at("SurvivalPriority"),t.at("HonorBound")};
    c.is_player=j.at("IsPlayer"); c.development_stage=j.at("DevelopmentStage");
    c.is_seeded_ancient=j.at("IsSeededAncient"); c.expansion_allowed=j.at("ExpansionAllowed");
    c.neutral_unless_provoked=j.at("NeutralUnlessProvoked"); c.species_id=j.at("SpeciesId"); return c;
}
Json civilization_json(const Civilization& c) {
    const auto& t=c.traits; Json offices=Json::object();
    for(const auto& o:c.leadership) offices[o.office]={{"Id",o.character.id},{"DisplayName",o.character.display_name},
        {"VoiceProfileId",o.character.voice_profile_id},{"Portrait",o.character.portrait}};
    return {{"Id",c.id},{"Name",c.name},{"HomeSystemId",c.home_system_id},{"Archetype",c.archetype},
        {"Traits",{{"Aggression",t.aggression},{"Territoriality",t.territoriality},{"Greed",t.greed},
        {"ScientificCuriosity",t.scientific_curiosity},{"RiskTolerance",t.risk_tolerance},
        {"SurvivalPriority",t.survival_priority},{"HonorBound",t.honor_bound}}},
        {"IsPlayer",c.is_player},{"DevelopmentStage",c.development_stage},{"IsSeededAncient",c.is_seeded_ancient},
        {"ExpansionAllowed",c.expansion_allowed},{"NeutralUnlessProvoked",c.neutral_unless_provoked},
        {"SpeciesId",c.species_id},{"Leadership",{{"Offices",std::move(offices)}}}};
}
Json home_json(const SpeciesHomeworldAssignment& h) {
    return {{"CivilizationId",h.civilization_id},{"SpeciesId",h.species_id},{"SystemId",h.system_id},
        {"PlanetaryBodyId",h.planetary_body_id},{"NaturalHabitability",h.natural_habitability},{"Suitability",h.suitability}};
}
void equal(const Json& actual,const Json& expected,const std::string& path) {
    if(actual.is_number()&&expected.is_number()) {
        // System.Text.Json emits the shortest float32 round-trip decimal for Vector2.
        // Compare that declared storage type exactly rather than loosening double tolerances.
        if(path.ends_with(".X")||path.ends_with(".Y")) check(actual.get<float>()==expected.get<float>(),path+" float32 coordinate changed");
        else if(actual.is_number_integer()&&expected.is_number_integer()) check(actual==expected,path+" integer changed");
        else {
            const double a=actual.get<double>(),e=expected.get<double>();
            check(std::isfinite(a)&&std::abs(a-e)<=1e-13*std::max(1.0,std::abs(e)),path+": expected "+expected.dump()+", got "+actual.dump());
        }
    } else if(actual.is_array()&&expected.is_array()) {
        check(actual.size()==expected.size(),path+" array size changed");
        for(std::size_t i=0;i<actual.size();++i) equal(actual[i],expected[i],path+"["+std::to_string(i)+"]");
    } else if(actual.is_object()&&expected.is_object()) {
        check(actual.size()==expected.size(),path+" object fields changed");
        for(const auto& [key,value]:expected.items()) equal(actual.at(key),value,path+"."+key);
    } else check(actual==expected,path+": expected "+expected.dump()+", got "+actual.dump());
}
template<class T,class F> Json project(const std::vector<T>& values,F fn) {
    Json j=Json::array(); for(const auto& value:values) j.push_back(fn(value)); return j;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected civilization oracle path"); std::ifstream input(argv[1]); const auto fixture=Json::parse(input);
        check(fixture.at("Format")=="stellar-civilization-parity-v1","Unknown civilization oracle format");
        std::size_t completed=0,failures=0,civilization_count=0,body_count=0;
        for(const auto& c:fixture.at("Cases")) {
            const std::string name=c.at("Name"),kind=c.at("Kind");
            std::vector<StellarSystem> systems; if(c.contains("Systems")) for(const auto& j:c.at("Systems")) systems.push_back(read_system(j));
            std::vector<PlanetaryBody> bodies; if(c.contains("Bodies")) for(const auto& j:c.at("Bodies")) bodies.push_back(read_body(j));
            std::vector<Civilization> civilizations; if(c.contains("Civilizations")) for(const auto& j:c.at("Civilizations")) civilizations.push_back(read_civilization(j));
            std::vector<SpeciesHomeworldAssignment> homes; std::string error;
            try {
                if(kind=="Seed") civilizations=seed_civilizations(systems,bodies,c.at("PreWarpCount"),c.at("AncientCount"),c.at("Seed"),c.at("PlayerSpeciesId"));
                else if(kind=="Founding") {
                    auto result=create_founding_catalog(c.at("Seed"),systems,c.at("PreWarpCount"),c.at("AncientCount"),c.at("PlayerSpeciesId"));
                    systems=std::move(result.systems); bodies=std::move(result.bodies); civilizations=std::move(result.civilizations);
                } else if(kind=="Guarantee") bodies=apply_nearby_habitable_guarantees(c.at("Seed"),systems,bodies,civilizations,c.at("GuaranteedCount"));
                else if(kind=="Constrained") {
                    const auto species=c.at("SpeciesIds").get<std::vector<std::string>>();
                    homes=plan_species_homeworlds_with_nearby_expansion(systems,bodies,species,c.at("MajorCount"),c.at("SearchLimit"));
                } else if(kind=="Resolve") homes.push_back(resolve_species_homeworld(c.at("CivilizationId"),c.at("SpeciesId"),c.at("SystemId"),bodies));
                else throw std::runtime_error("Unknown oracle case kind: "+kind);
            } catch(const std::exception& e) { error=e.what(); }
            if(c.contains("ExpectedError")) {
                check(!error.empty(),name+": reference failure was silently accepted");
                check(error==c.at("ExpectedError").get<std::string>(),name+": expected failure "+c.at("ExpectedError").dump()+", got "+error);
                ++failures;
            } else check(error.empty(),name+": unexpected native failure: "+error);
            if(c.contains("ExpectedSystems")) equal(project(systems,system_json),c.at("ExpectedSystems"),name+".Systems");
            if(c.contains("ExpectedBodies")) { equal(project(bodies,body_json),c.at("ExpectedBodies"),name+".Bodies"); body_count+=bodies.size(); }
            if(c.contains("ExpectedCivilizations")) {
                equal(project(civilizations,civilization_json),c.at("ExpectedCivilizations"),name+".Civilizations"); civilization_count+=civilizations.size();
                const std::vector<std::string> offices={"FleetCommander","ChiefScientist","Diplomat","Governor","EconomicAdvisor","OperationsOfficer","ExpeditionCommander"};
                for(const auto& civ:civilizations) {
                    check(civ.leadership.size()==offices.size(),name+": founding office count changed");
                    for(std::size_t i=0;i<offices.size();++i) check(civ.leadership[i].office==offices[i],name+": office insertion order changed");
                }
            }
            if(c.contains("ExpectedHomes")) equal(project(homes,home_json),c.at("ExpectedHomes"),name+".Homes");
            ++completed;
        }
        std::cout<<"civilization_tests: passed; "<<completed<<" scenarios, "<<failures<<" expected failures, "<<civilization_count<<" civilization records, "<<body_count<<" body records\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"civilization_tests failed: "<<e.what()<<'\n'; return 1; }
}
