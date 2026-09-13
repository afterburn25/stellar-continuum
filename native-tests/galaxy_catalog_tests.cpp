#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace stellar::core;
using Json=nlohmann::json;
namespace {
void check(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
void close(double actual,double expected,const std::string& label,double tolerance=0.00004) {
    check(std::isfinite(actual) && std::abs(actual-expected)<=tolerance,label+": expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
template<class T> void optional_equal(const std::optional<T>& actual,const Json& expected,const std::string& label) {
    check(actual.has_value()!=expected.is_null(),label+" nullability changed");
    if(actual) check(Json(*actual)==expected,label+" value changed");
}
void system_equal(const StellarSystem& s,const Json& j,bool compare_traits=false) {
    const auto context="system "+std::to_string(s.id)+" ";
    check(s.id==j.at("Id") && s.name==j.at("Name").get<std::string>(),context+"identity changed");
    close(s.position.x,j.at("X"),context+"X"); close(s.position.y,j.at("Y"),context+"Y");
    optional_equal(s.position.depth_light_years,j.at("Depth"),context+"depth");
    optional_equal(s.primary,j.at("Primary"),context+"primary");
    optional_equal(s.secondary,j.at("Secondary"),context+"secondary");
    optional_equal(s.tertiary,j.at("Tertiary"),context+"tertiary");
    optional_equal(s.catalog_preset_id,j.at("CatalogPresetId"),context+"preset");
    optional_equal(s.stellar_catalog_id,j.at("StellarCatalogId"),context+"catalog");
    if(compare_traits) check(static_cast<int>(s.archetype)==j.at("Archetype") && s.has_habitable_world==j.at("HasHabitableWorld") &&
        s.has_anomaly==j.at("HasAnomaly") && s.has_rare_resource==j.at("HasRareResource") && s.has_pre_warp_civilization==j.at("HasPreWarpCivilization"),
        context+"archetype or physical flags changed");
}
void body_equal(const PlanetaryBody& b,const Json& j) {
    check(b.id==j.at("Id") && b.system_id==j.at("SystemId") && b.name==j.at("Name").get<std::string>() && b.orbit_index==j.at("OrbitIndex") && static_cast<int>(b.kind)==j.at("Kind"),
        "body identity changed: expected id="+j.at("Id").dump()+" system="+j.at("SystemId").dump()+" name="+j.at("Name").dump()+" orbit="+j.at("OrbitIndex").dump()+" kind="+j.at("Kind").dump()+
        ", got id="+std::to_string(b.id)+" system="+std::to_string(b.system_id)+" name="+b.name+" orbit="+std::to_string(b.orbit_index)+" kind="+std::to_string(static_cast<int>(b.kind)));
    optional_equal(b.parent_body_id,j.at("ParentBodyId"),"parent");
    close(b.radius_earth,j.at("RadiusEarth"),"radius",1e-14); close(b.mass_earth,j.at("MassEarth"),"mass",1e-14);
    close(b.orbital_eccentricity,j.value("OrbitalEccentricity",0.0),"eccentricity",1e-14);
    close(b.orbital_inclination_degrees,j.value("OrbitalInclinationDegrees",0.0),"inclination",1e-14);
    check(b.legacy_colonization_candidate==j.at("LegacyColonizationCandidate") && b.has_rare_resource==j.at("HasRareResource") &&
          b.has_anomaly==j.at("HasAnomaly") && b.has_pre_warp_civilization==j.at("HasPreWarpCivilization"),"Sol flags changed");
    const auto& e=b.environment; const auto& expected=j.at("Environment");
    close(e.gravity_g,expected.at("GravityG"),"gravity",1e-14);
    close(e.temperature_kelvin,expected.at("TemperatureKelvin"),"temperature",1e-14);
    close(e.pressure_kpa,expected.at("PressureKPa"),"pressure",1e-14);
    close(e.radiation_hazard,expected.at("RadiationHazard"),"radiation",1e-14);
    check(static_cast<int>(e.atmosphere)==expected.at("Atmosphere") && static_cast<int>(e.available_solvent)==expected.at("AvailableSolvent") &&
          e.is_immersed_environment==expected.at("IsImmersedEnvironment") && e.has_solid_surface==expected.at("HasSolidSurface"),"Sol environment changed");
}
Json expand_body(const Json& row) {
    return {{"Id",row.at(0)},{"SystemId",row.at(1)},{"ParentBodyId",row.at(2)},{"OrbitIndex",row.at(3)},
        {"Name",row.at(4)},{"Kind",row.at(5)},{"RadiusEarth",row.at(6)},{"MassEarth",row.at(7)},
        {"Environment",{{"GravityG",row.at(8)},{"TemperatureKelvin",row.at(9)},{"PressureKPa",row.at(10)},
            {"Atmosphere",row.at(11)},{"AvailableSolvent",row.at(12)},{"RadiationHazard",row.at(13)},
            {"IsImmersedEnvironment",row.at(14)},{"HasSolidSurface",row.at(15)}}},
        {"LegacyColonizationCandidate",row.at(16)},{"HasRareResource",row.at(17)},{"HasAnomaly",row.at(18)},
        {"HasPreWarpCivilization",row.at(19)},{"OrbitalEccentricity",row.at(20)},{"OrbitalInclinationDegrees",row.at(21)}};
}
StellarSystem read_system(const Json& j) {
    const auto optional_double=[](const Json& value) -> std::optional<double> { return value.is_null() ? std::nullopt : std::optional<double>{value.get<double>()}; };
    const auto optional_class=[](const Json& value) -> std::optional<StellarClass> { return value.is_null() ? std::nullopt : std::optional<StellarClass>{static_cast<StellarClass>(value.get<int>())}; };
    const auto optional_string=[](const Json& value) -> std::optional<std::string> { return value.is_null() ? std::nullopt : std::optional<std::string>{value.get<std::string>()}; };
    StellarSystem s; s.id=j.at("Id"); s.name=j.at("Name"); s.position.x=j.at("X"); s.position.y=j.at("Y");
    s.position.depth_light_years=optional_double(j.at("Depth"));
    s.primary=optional_class(j.at("Primary"));
    s.secondary=optional_class(j.at("Secondary"));
    s.tertiary=optional_class(j.at("Tertiary"));
    s.catalog_preset_id=optional_string(j.at("CatalogPresetId"));
    s.stellar_catalog_id=optional_string(j.at("StellarCatalogId"));
    s.archetype=j.at("Archetype"); s.has_habitable_world=j.at("HasHabitableWorld");
    s.has_anomaly=j.at("HasAnomaly"); s.has_rare_resource=j.at("HasRareResource"); s.has_pre_warp_civilization=j.at("HasPreWarpCivilization");
    return s;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"Expected reference fixture and runtime catalog paths");
        std::ifstream in(argv[1]); const auto fixture=Json::parse(in);
        check(fixture.at("Format")=="stellar-galaxy-parity-v1","Unknown fixture version");
        const auto catalog=load_nearby_catalog(argv[2]);
        for(const auto& scenario:fixture.at("Random")) {
            LegacyRandom random(scenario.at("Seed")); int i=0;
            for(const auto& value:scenario.at("Values")) {
                check(random.next()==value.at("Whole"),"Seeded .NET whole-number stream diverged");
                check(random.next(i++%17)==value.at("Bounded"),"Seeded .NET bounded stream diverged");
                close(random.next_double(),value.at("Unit"),"Seeded .NET unit stream",1e-16);
            }
        }
        for(const auto& s:fixture.at("Spectra")) optional_equal(classify_spectral_type(s.at("Input")),s.at("Class"),"spectral classification");
        for(std::size_t i=0;i<catalog.size();++i) {
            StellarSystem s; s.id=static_cast<int>(i); apply_catalog_star(s,catalog[i]);
            system_equal(s,fixture.at("Catalog").at(i));
        }
        std::size_t compared=0;
        for(const auto& scenario:fixture.at("Cases")) {
            const int count=scenario.at("Count"); const std::int64_t seed=scenario.at("Seed");
            const auto core=full_galaxy_core(count); const auto systems=generate_stellar_catalog(seed,count,catalog);
            close(full_galaxy_radius(count),scenario.at("Radius"),"radius");
            close(core.position.x,scenario.at("Core").at("X"),"core X");
            close(core.position.y,scenario.at("Core").at("Y"),"core Y");
            close(core.exclusion_radius,scenario.at("Core").at("ExclusionRadius"),"core exclusion");
            check(Json(target_stellar_class_counts(count))==scenario.at("Targets"),"Stellar quotas changed");
            check(Json(procedural_system_names(seed,count))==scenario.at("Names"),"Seeded names changed");
            check(systems.size()==static_cast<std::size_t>(count),"System count changed");
            std::array<int,12> populations{};
            for(std::size_t i=0;i<systems.size();++i) {
                try { system_equal(systems[i],scenario.at("Systems").at(i),true); }
                catch(const std::exception& error) { throw std::runtime_error("seed "+std::to_string(seed)+" count "+std::to_string(count)+": "+error.what()); }
                ++populations[static_cast<int>(*systems[i].primary)]; ++compared;
                const double dx=systems[i].position.x-core.position.x,dy=systems[i].position.y-core.position.y;
                check(std::hypot(dx,dy)>=core.exclusion_radius-0.00004,"Star placed inside reserved core");
                if(i>=96) for(std::size_t j=96;j<i;++j) {
                    const double x=systems[i].position.x-systems[j].position.x,y=systems[i].position.y-systems[j].position.y;
                    check(x*x+y*y>=3.5*3.5-0.00004,"Generated star spacing violated");
                }
            }
            check(populations==target_stellar_class_counts(count),"Generated class totals violate target");
            const auto sol=create_sol_catalog(systems[0]);
            for(std::size_t i=0;i<sol.size();++i) body_equal(sol[i],fixture.at("Sol").at(i));
            const std::vector<PlanetaryBody> legacy(sol.begin(),sol.end()-1);
            const auto upgraded=upgrade_saved_sol_catalog(legacy,systems);
            for(std::size_t i=0;i<upgraded.size();++i) body_equal(upgraded[i],fixture.at("UpgradedSol").at(i));
        }
        bool rejected=false; try { (void)generate_stellar_catalog(0,100,catalog); } catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"Unsupported full-galaxy count was accepted");
        std::size_t body_count=0;
        for(const auto& scenario:fixture.at("PlanetCases")) {
            std::vector<StellarSystem> inputs; for(const auto& j:scenario.at("Systems")) inputs.push_back(read_system(j));
            const auto bodies=generate_planetary_catalog(scenario.at("Seed"),inputs);
            const auto expected_count=scenario.at("Bodies").size();
            for(std::size_t i=0;i<std::min(bodies.size(),expected_count);++i) {
                try { body_equal(bodies[i],expand_body(scenario.at("Bodies").at(i))); }
                catch(const std::exception& error) { throw std::runtime_error("planet seed "+scenario.at("Seed").dump()+" systems "+std::to_string(inputs.size())+" body "+std::to_string(bodies[i].id)+": "+error.what()); }
                ++body_count;
            }
            check(bodies.size()==expected_count,"Procedural body count changed for seed "+scenario.at("Seed").dump()+
                ": expected "+std::to_string(expected_count)+", got "+std::to_string(bodies.size()));
        }
        std::cout<<"galaxy_catalog_tests: passed; "<<compared<<" generated systems, "<<body_count<<" procedural bodies, 500 catalog records, 768 RNG triplets, Sol and upgrade parity\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"galaxy_catalog_tests failed: "<<error.what()<<'\n'; return 1;}
}
