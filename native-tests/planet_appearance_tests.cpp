#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/developer_campaign.hpp>
#include "../core/src/planet_appearance_json.hpp"
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
using namespace stellar::core;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
 if(argc==4){
  std::ifstream input(argv[2]);const std::string text((std::istreambuf_iterator<char>(input)),{});require(!text.empty(),"Audit save missing");
  const auto json=nlohmann::json::parse(text);const auto& original=json.at("Campaign").at("Galaxy");
  auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[3]),text);auto& w=restored.galaxy();
  std::unordered_map<int,nlohmann::json> old;for(const auto& b:original.at("PlanetaryBodies"))old.emplace(b.at("Id").get<int>(),b);
  int imported=0,moved=0;for(const auto& b:w.bodies){const auto& prior=old.at(b.id);
   require(b.name==prior.at("Name").get<std::string>(),"Repair changed body identity");
   if(b.appearance&&!b.appearance->source_asset_id.empty()&&!b.appearance->source_asset_id.starts_with("sol:")){
    ++imported;require(b.appearance->source_asset_id==prior.at("PlanetAppearance").at("source_asset_id").get<std::string>(),"Repair replaced imported artwork");
   }
   if(b.stellar_exposure&&!prior.at("StellarExposure").is_null())moved+=b.stellar_exposure->orbit_au!=prior.at("StellarExposure").at("orbit_au").get<double>();
   if(b.system_id!=sol_system_id&&!b.parent_body_id&&b.appearance&&b.appearance->primary_class==PlanetClass::Frozen&&b.stellar_exposure)
    require(planet_equilibrium_temperature(b.stellar_exposure->incident_flux/std::pow(1-b.orbital_eccentricity,2),0)<=235.0001,"Restored frozen primary remains near the star");
  }
  std::size_t fields=0;for(const auto& system:w.systems)if(system.small_body_fields)for(const auto& f:*system.small_body_fields){++fields;if(f.planet_centered)continue;
   for(const auto& b:w.bodies)if(b.system_id==system.id&&!b.parent_body_id&&!(b.cracked_world&&b.id==f.associated_planet_id)){
    const double a=planetary_orbit_au(system,b);require(f.outer_radius_au<a*(1-b.orbital_eccentricity)||f.inner_radius_au>a*(1+b.orbital_eccentricity),"Restored belt crosses a normal planet orbit");
   }
  }
  const auto systems=w.systems;const auto before_bodies=w.bodies;
  std::size_t binaries=0,triples=0;validate_stellar_orbit_catalog(w.systems,w.bodies);
  for(const auto& system:w.systems)if(system.secondary&&system.stellar_object){require(system.stellar_orbits.has_value(),"Physical multiple-star system has no orbit model");
    (system.tertiary?triples:binaries)++;
    for(const auto& b:w.bodies)if(b.system_id==system.id&&!b.parent_body_id)require(stellar_host_accepts_orbit(system,planetary_stellar_host(system,b.id),planetary_orbit_au(system,b),b.orbital_eccentricity),"Migrated planet escaped stable stellar region");
  }
  reconcile_frozen_planet_orbits(w.systems,w.bodies);reconcile_small_body_orbits(w.systems,w.bodies);
  initialize_stellar_orbits(w.seed,w.systems,w.bodies);
  for(std::size_t i=0;i<w.systems.size();++i)require(w.systems[i].small_body_fields==systems[i].small_body_fields,"Repeated load moves belts again");
  for(std::size_t i=0;i<w.systems.size();++i)require(w.systems[i].stellar_orbits==systems[i].stellar_orbits,"Repeated load changes stellar orbits");
  for(std::size_t i=0;i<w.bodies.size();++i)require(w.bodies[i].appearance==before_bodies[i].appearance,"Repeated load changes frozen climate again");
  std::size_t habitable=0;for(const auto& row:build_developer_planet_index(w,DeveloperPlanetFilter::Habitable))habitable+=row.count;
  std::cout<<"Save audit: "<<w.bodies.size()<<" bodies, "<<imported<<" imported surfaces retained, "<<moved<<" corrected family orbits, "<<fields<<" clear fields, "<<habitable<<" unsettled habitable worlds; repeated repair stable.\n";
  std::cout<<"Multiple-star audit: "<<binaries<<" binaries and "<<triples<<" triples; stable planetary hosts and repeatable orbital records.\n";
  return 0;
 }
 const auto weights=planet_baseline_weights();require(std::abs(std::accumulate(weights.begin(),weights.end(),0.)-100)<1e-10,"Weights total exactly 100 percent");
 require(weights==std::array<double,16>{20,10,12,5,4,5,7,5,2,8,7,7,5,1,.5,1.5},"Authored baseline weights retained");
 require(planet_subclass_definitions().size()>=47,"Every supplied subtype has a definition");
 for(const auto& s:planet_subclass_definitions())require(planet_subclass_definition(s.primary,s.id).id==s.id,"Subtype resolves");
 const auto& registry=planet_type_registry();require(registry.size()==66,"All 66 subclasses have a resolved registry record");
 std::array<double,16> subclass_totals{};double overall=0,gaia=0;std::unordered_set<std::string> accepted_ids,rejected_ids;
 for(const auto& r:registry){const auto& sub=planet_subclass_definition(r.base_class,r.subclass);subclass_totals[static_cast<std::size_t>(r.base_class)]+=r.within_class_percentage;overall+=r.baseline_percentage;if(sub.temperate_group=="gaia")gaia+=r.baseline_percentage;
  require(!r.valid_orbital_zones.empty()&&r.surface_temperature_kelvin[0]<=r.surface_temperature_kelvin[1],"Every subclass has orbital and thermal rules");
  require(!r.atmosphere_rules.composition_rule.empty()&&r.atmosphere_rules.minimum_retention>0,"Every subclass has atmosphere rules");
  require((!sub.water||r.water_allowed)&&(!sub.ice||r.ice_allowed)&&(!sub.emission||r.volcanism_allowed),"Declared surface permissions match generated surfaces");
  for(const auto& id:r.accepted_image_pool)require(accepted_ids.insert(id).second,"Accepted image has one primary owner");
  for(const auto& id:r.rejected_image_pool)require(rejected_ids.insert(id).second,"Rejected image has one primary owner");
 }
 for(double total:subclass_totals)require(std::abs(total-100)<1e-8,"Subclass percentages cover every class");
 require(std::abs(overall-100)<1e-8&&std::abs(gaia-.2)<1e-8,"Nested subclass percentages preserve overall and Gaia rarity");
 require(accepted_ids.size()==705&&rejected_ids.size()==520,"Registry accounts for every audited image");
 for(const auto& id:accepted_ids)require(!rejected_ids.contains(id),"Accepted and rejected image pools never overlap");
 std::size_t duplicate_count=0,earth_count=0;for(const auto& a:rejected_planet_art_definitions()){require(!a.reason.empty(),"Rejected image retains its reason");duplicate_count+=!a.duplicate_of.empty();earth_count+=a.earth_geography;}
 require(duplicate_count==21&&earth_count==70,"Duplicate and Earth-geography exclusions retained");
 require(planet_orbital_zone(119.99)==PlanetOrbitalZone::DeepCold&&planet_orbital_zone(120)==PlanetOrbitalZone::Cold&&planet_orbital_zone(240)==PlanetOrbitalZone::Temperate&&planet_orbital_zone(350)==PlanetOrbitalZone::Warm&&planet_orbital_zone(700)==PlanetOrbitalZone::Hot,"Orbital boundaries have unambiguous ownership");
 require(std::ranges::find(planet_type_record(PlanetClass::Frozen,"global-ice").valid_orbital_zones,PlanetOrbitalZone::Hot)==planet_type_record(PlanetClass::Frozen,"global-ice").valid_orbital_zones.end(),"Frozen worlds never permit hot irradiation zones");
 require(std::abs(planet_equilibrium_temperature(1,.3)-254.74)<1,"Earth absorbed-flux temperature");
 require(planet_liquid_water_possible(288,101.3),"Earth surface liquid water");require(!planet_liquid_water_possible(220,101.3),"Frozen water exclusion");require(!planet_liquid_water_possible(700,101.3),"Steam/supercritical exclusion");require(!planet_liquid_water_possible(288,.1),"Vacuum water exclusion");
 auto star=generate_stellar_physics(12,StellarObjectType::GYellowStar);star.luminosity_solar=1;star.age_myr=4600;star.radiation_modifier=1;star.wind_modifier=1;star.inner_hz_au=.95;star.outer_hz_au=1.7;
 require(planet_atmosphere_retention(1,1,288,28,star,1)>50,"Earth nitrogen retained");require(planet_atmosphere_retention(.001,.1,1500,28,star,100)<12,"Hot small body's thick air rejected");
 StellarSystem system;system.id=1;system.name="Test";system.stellar_object=star;
 {
  auto cold=make_planet_type_example(54,90001,1,2,star,PlanetClass::Frozen,"global-ice");
  cold.environment.temperature_kelvin=95;cold.stellar_exposure=stellar_planet_exposure(star,1);
  auto moon=cold;moon.id=90002;moon.kind=PlanetaryBodyKind::Moon;moon.parent_body_id=cold.id;
  std::vector<PlanetaryBody> old{cold,moon};reconcile_frozen_planet_orbits(std::span{&system,1},old);
  require(old[0].stellar_exposure->orbit_au>5&&old[0].stellar_exposure->orbit_au==old[1].stellar_exposure->orbit_au,"Cold legacy planet/moon family did not move to a cold orbit");
  require(old[0].appearance->source_asset_id==cold.appearance->source_asset_id&&old[0].environment.temperature_kelvin==95,"Thermal repair replaced artwork or an inhabited environment");
  const auto repaired=old[0].stellar_exposure->orbit_au;reconcile_frozen_planet_orbits(std::span{&system,1},old);
  require(old[0].stellar_exposure->orbit_au==repaired,"Thermal repair is not idempotent");
 }
 std::vector<PlanetaryBody> input;for(int i=0;i<2400;++i){PlanetaryBody b;b.id=i+1;b.system_id=1;b.name="Test "+std::to_string(i);b.orbit_index=i%12;b.radius_earth=i%3==0?10:1;b.mass_earth=i%3==0?300:1;b.environment={b.mass_earth/(b.radius_earth*b.radius_earth),300,0,PlanetaryAtmosphereRegime::Vacuum,PlanetarySolventRegime::None,0,false,i%3!=0};b.stellar_exposure=stellar_planet_exposure(star,.07*std::pow(1.75,i%12));input.push_back(b);}
 auto first=input,second=input;generate_planet_appearances(191,std::span{&system,1},first);generate_planet_appearances(191,std::span{&system,1},second);
 std::array<int,16> counts{};for(std::size_t i=0;i<first.size();++i){require(first[i].appearance==second[i].appearance,"Same seed yields exact same appearance");validate_planetary_body(first[i]);
  const auto& appearance=*first[i].appearance;require(!rejected_ids.contains(appearance.source_asset_id),"Generator never chooses rejected artwork");
  if(appearance.subclass!="physical-fallback"){const auto& r=planet_type_record(appearance.primary_class,appearance.subclass);require(std::ranges::find(r.valid_orbital_zones,planet_orbital_zone(appearance.climate.equilibrium_kelvin))!=r.valid_orbital_zones.end(),"Generated subtype obeys its declared orbital zones");if(!appearance.source_asset_id.empty())require(std::ranges::find(r.accepted_image_pool,appearance.source_asset_id)!=r.accepted_image_pool.end()||std::ranges::find(r.compatible_image_pool,appearance.source_asset_id)!=r.compatible_image_pool.end(),"Generated image belongs to its declared pool");}
  const auto errors=planet_appearance_contradictions(first[i]);if(!errors.empty())throw std::runtime_error(std::to_string(first[i].id)+": "+errors.front());++counts[static_cast<std::size_t>(first[i].appearance->primary_class)];
 }
 require(counts[0]>0&&counts[2]>0&&counts[11]>0&&counts[13]>0,"Cold, hot and giant zones produce different families");
 const auto stable=first.front().appearance;generate_planet_appearances(999,std::span{&system,1},first);require(stable==first.front().appearance,"Existing appearance never rerolled");
 auto& a=*first[40].appearance;const nlohmann::json saved=a;auto restored=saved.get<PlanetAppearance>();require(a==restored,"Complete appearance JSON round trip");validate_planet_appearance(restored);
 {
  const std::array legacy_classes{PlanetClass::Barren,PlanetClass::Desert,PlanetClass::Frozen,
   PlanetClass::Ocean,PlanetClass::Temperate,PlanetClass::Tundra,PlanetClass::Volcanic,
   PlanetClass::Greenhouse,PlanetClass::Carbon,PlanetClass::SuperEarth,PlanetClass::MiniNeptune,
   PlanetClass::GasGiant,PlanetClass::IceGiant,PlanetClass::HotJupiter,PlanetClass::Chthonian,PlanetClass::Cracked};
  for(std::size_t i=0;i<legacy_classes.size();++i){
   const nlohmann::json legacy=i;const nlohmann::ordered_json ordered=i;
   require(legacy.get<PlanetClass>()==legacy_classes[i]&&ordered.get<PlanetClass>()==legacy_classes[i],
    "Legacy class ordinal was remapped during save restoration");
   const nlohmann::ordered_json canonical=legacy.get<PlanetClass>();
   require(canonical.is_string()&&canonical.get<std::string>()==planet_class_definition(legacy_classes[i]).id,
    "Restored legacy class was not written as the canonical string ID");
  }
  for(const auto& invalid:nlohmann::json::array({-1,16,1.0,1.5,true,nullptr,"missing-class",18446744073709551615ULL})){
   bool rejected=false;try{(void)invalid.get<PlanetClass>();}catch(const std::exception&){rejected=true;}
   require(rejected,"Malformed legacy planet class was accepted");
  }
  auto legacy=saved;legacy["primary_class"]=static_cast<int>(a.primary_class);
  legacy["compatible_classes"]=nlohmann::json::array();
  for(const auto compatible:a.compatible_classes)legacy["compatible_classes"].push_back(static_cast<int>(compatible));
  require(legacy.get<PlanetAppearance>()==a,"Numeric autosave changed the existing planet artwork or physical metadata");
  for(const auto flag:{std::optional<bool>{},std::optional<bool>{false},std::optional<bool>{true}}){
   auto appearance=a;appearance.tidally_locked=flag;
   const nlohmann::json json=appearance;const nlohmann::ordered_json ordered=appearance;
   require(json.get<PlanetAppearance>()==appearance,"Tidal-lock state did not survive JSON");
   require(nlohmann::json::parse(ordered.dump()).get<PlanetAppearance>()==appearance,"Ordered campaign JSON lost tidal-lock state");
   require(json.contains("tidally_locked")==flag.has_value(),"Legacy lock state should remain optional");
  }
  auto missing=saved;missing.erase("tidally_locked");
  require(!missing.get<PlanetAppearance>().tidally_locked.has_value(),"Old appearance records require a new field");
  missing["tidally_locked"]="yes";bool invalid=false;
  try{(void)missing.get<PlanetAppearance>();}catch(const nlohmann::json::exception&){invalid=true;}
  require(invalid,"Malformed tidal-lock state was accepted");
  auto primary=first[40];primary.appearance->tidally_locked.reset();
  const double radius=primary.stellar_exposure->orbit_au;
  primary.appearance->rotation_period_days=365.25*std::sqrt(radius*radius*radius/star.mass_solar);
  require(planet_is_tidally_locked(primary,&star),"Legacy synchronous primary was unlocked");
  primary.appearance->tidally_locked=false;
  require(!planet_is_tidally_locked(primary,&star),"Explicit free rotation was overridden");
  primary.appearance->tidally_locked=true;primary.appearance->rotation_period_days=1;
  require(planet_is_tidally_locked(primary,&star),"Explicit lock was lost");
  primary.parent_body_id=1;primary.appearance->tidally_locked.reset();
  require(planet_is_tidally_locked(primary,&star),"Legacy regular moon was unlocked");
  primary.appearance->tidally_locked=false;
  require(!planet_is_tidally_locked(primary,&star),"Explicit unlocked moon was forced into locking");
 }
 const auto capture=capture_planetary_bodies(first,std::span{&system,1});const auto bodies=restore_planetary_bodies(capture,std::span{&system,1});require(bodies[40].appearance==a,"Authoritative body DTO preserves visual identity");
 auto legacy=first;for(auto& b:legacy)b.appearance.reset();const auto old=restore_planetary_bodies(capture_planetary_bodies(legacy,std::span{&system,1}),std::span{&system,1});require(old.front().appearance.has_value(),"Old saves receive a canonical visual migration");
 require(old.front().environment.temperature_kelvin==legacy.front().environment.temperature_kelvin&&old.front().mass_earth==legacy.front().mass_earth,"Visual migration preserves saved physical state");
 const auto old_again=restore_planetary_bodies(capture_planetary_bodies(old,std::span{&system,1}),std::span{&system,1});require(old_again.front().appearance==old.front().appearance,"Migrated visual identity persists without rerolling");
 require(planet_art_definitions().size()==705,"Only all 705 reviewed materials enter runtime registry");
 for(const auto& s:planet_subclass_definitions()){if(!s.generation_enabled)continue;std::cout<<"Checking example "<<s.id<<std::endl;auto example=make_planet_type_example(54,10000,1,2,star,s.primary,s.id);require(example.appearance->subclass==s.id,"Every developer subtype resolves exactly");require(planet_appearance_contradictions(example).empty(),"Developer examples pass physical validation");}
 require(std::abs(std::remainder(planet_rotation_phase(a,3+a.rotation_period_days)-planet_rotation_phase(a,3),2*std::numbers::pi))<1e-10,"Surface rotates on a stable period");
 a.source_asset_id="unreviewed-earth";bool rejected=false;try{validate_planet_appearance(a);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Unapproved art cannot enter saved appearances");
 a.source_asset_id=rejected_planet_art_definitions().front().id;rejected=false;try{validate_planet_appearance(a);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Known rejected artwork cannot enter a saved appearance");
 if(argc>1){const auto catalog=load_nearby_catalog(argv[1]);std::size_t checked=0,errors=0;
  const auto coverage=seed_fresh_campaign(42,catalog,250,6,1,"terran_baseline",StellarPopulationOptions{},true);
  const auto index=build_developer_planet_index(coverage);require(index.size()==planet_subclass_definitions().size(),"Developer index lists every subtype");
  for(const auto& row:index)require(row.example.has_value()||!planet_subclass_definition(row.type,row.subclass).generation_enabled,"Full content coverage creates every planet subclass");
  for(const auto& species:species_environment_profiles()){
   const auto world=seed_fresh_campaign(42,catalog,250,6,1,species.id,StellarPopulationOptions{});
   require(world.colonies.size()>=7,"Every species retains viable founding sites");
   for(const auto& b:world.bodies){require(b.appearance.has_value(),"Current campaigns save every planet appearance");++checked;
    for(const auto& error:planet_appearance_contradictions(b)){if(errors++<25)std::cerr<<species.id<<' '<<b.id<<' '<<b.appearance->subclass<<" T="<<b.environment.temperature_kelvin<<" p="<<b.environment.pressure_kpa<<" retention="<<b.appearance->climate.retention_parameter<<": "<<error<<'\n';}
   }
  }
  require(errors==0,"Generated campaigns contain contradictory visuals");std::cout<<checked<<" campaign bodies across all species passed.\n";
 }
 std::cout<<"Planet appearance: taxonomy, weights, 2400 physical cases, determinism, atmosphere/water, persistence and rotation passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
