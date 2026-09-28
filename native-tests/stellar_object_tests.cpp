#include <stellar/core/stellar_object.hpp>
#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace stellar::core;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)try {
  check(argc>=2,"Catalog path required");auto catalog=load_nearby_catalog(argv[1]);
  const auto& defs=stellar_object_definitions();check(defs.size()==23,"23 classes");
  check(std::accumulate(defs.begin(),defs.end(),0,[](int n,const auto& d){return n+d.weight_millionths;})==1000000,"Exact baseline total");
  auto w=stellar_population_weights();auto spiral=stellar_population_weights({GalaxyMorphology::Spiral,PopulationState::Mature});
  check(w==spiral,"Mature spiral and barred spiral use same reference");
  constexpr int samples=1000000;std::array<int,stellar_object_type_count> counts{};
  std::cout<<std::unitbuf;
  for(int seed=0;seed<samples;++seed)++counts[static_cast<std::size_t>(sample_stellar_object(static_cast<std::uint64_t>(seed)))];
  for(std::size_t i=0;i<w.size();++i)check(std::abs(counts[i]-samples*w[i])<6*std::sqrt(samples*w[i]*(1-w[i]))+3,"Statistical convergence");
  std::cout<<"Million-seed convergence passed.\n";
  for(const auto& d:defs)for(int seed=0;seed<20;++seed){auto p=generate_stellar_physics(seed,d.type);check(p==generate_stellar_physics(seed,d.type),"Physics reproducibility");check(p.mass_solar>=d.mass_solar.minimum&&p.mass_solar<=d.mass_solar.maximum,"Mass bounds");validate_stellar_physics(p);}
  auto h1=stellar_habitable_zone(1),h100=stellar_habitable_zone(100);check(std::abs(h100.first-h1.first*10)<1e-10,"Square root HZ");
  auto giant=generate_stellar_physics(5,StellarObjectType::RedSupergiant);check(giant.inner_hz_au>90,"Supergiant HZ displaced");check(stellar_planet_exposure(giant,giant.safe_approach_au*.9).baked,"Baked world");
  auto hole=generate_stellar_physics(9,StellarObjectType::QuiescentBlackHole);check(hole.luminosity_solar==0&&hole.inner_hz_au==0,"Quiet BH emits no stellar heating");
  auto jet=generate_stellar_physics(9,StellarObjectType::JetBlackHole);check(stellar_approach_unsafe(jet,std::cos(jet.jet_axis_radians)*5,std::sin(jet.jet_axis_radians)*5),"Jet hazard along axis");check(!stellar_approach_unsafe(jet,-std::sin(jet.jet_axis_radians)*5,std::cos(jet.jet_axis_radians)*5),"Jet hazard not spherical");
  CivilizationKnowledgeState knowledge;check(!knowledge.is_galactic_core_discovered(0),"Core hidden");knowledge.unlock_galactic_core_access(0);check(!knowledge.is_galactic_core_discovered(0),"Unlock alone not discovery");knowledge.record_galactic_core_exploration(0);check(knowledge.is_galactic_core_discovered(0),"Exploration reveals core");
  const GalaxyPayloadCaptureOptions capture{0,"test","2026-09-17T00:00:00+00:00"};
  std::cout<<"Physics and disclosure checks passed.\n";
  for(int size:{250,500,1000,2500}) {
    PersistableFreshCampaignOptions options{"2026-09-17T00:00:00+00:00",size,6,1,"terran_baseline",StellarPopulationOptions{}};
    auto start=std::chrono::steady_clock::now();std::cout<<"Generating "<<size<<" systems...\n";auto first=seed_persistable_fresh_campaign(42,catalog,options);auto second=seed_persistable_fresh_campaign(42,catalog,options);
    std::cout<<"Comparing generated campaigns...\n";
    auto encoded=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(first,capture));
    check(encoded==encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(second,capture)),"Same seed and options reproduces entire galaxy");
    auto restored=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(encoded));
    auto reencoded=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(restored.galaxy,capture));
    check(nlohmann::json::parse(encoded)==nlohmann::json::parse(reencoded),"Exact saved values round trip");
    check(first.galactic_core&&first.galactic_core->black_hole&&first.core,"One separate central object");
    check(first.core->position.x==full_galaxy_core(size).position.x&&first.core->position.y==full_galaxy_core(size).position.y,"Core at configured dynamical center");
    for(const auto& s:first.systems)check(std::hypot(s.position.x-first.core->position.x,s.position.y-first.core->position.y)>=first.core->exclusion_radius,"Empty central zone");
    for(const auto& b:first.bodies) {const auto& sys=first.systems.at(static_cast<std::size_t>(b.system_id));check(b.stellar_exposure.has_value(),"Persisted orbital exposure");const auto host=stellar_host_physics(sys,planetary_stellar_host(sys,b.id));check(b.stellar_exposure->orbit_au>host.destruction_radius_au,"No intact engulfed planet");if(b.stellar_exposure->baked)check(!b.legacy_colonization_candidate,"No baked candidate");}
    std::cout<<size<<" systems deterministic/save round trip passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    if(size==500&&argc>2){std::ofstream report(argv[2]);report<<stellar_population_diagnostics(first.systems,first.bodies,*options.stellar_population,first.galactic_core->black_hole,42);}
  }
  for(auto m:{GalaxyMorphology::Spiral,GalaxyMorphology::Elliptical,GalaxyMorphology::Lenticular,GalaxyMorphology::Irregular,GalaxyMorphology::Ring}) {
    std::cout<<"Morphology "<<static_cast<int>(m)<<'\n';
    auto systems=generate_stellar_catalog(145,500,catalog);apply_stellar_population(145,systems,{m,PopulationState::Mature});check(systems.size()==500,"Morphology generation");
  }
  for(auto m:{GalaxyMorphology::Spiral,GalaxyMorphology::Elliptical,GalaxyMorphology::Lenticular,GalaxyMorphology::Irregular,GalaxyMorphology::Ring}){
    const StellarPopulationOptions options{m,stellar_default_population_state(m)};
    std::array<std::size_t,12> totals{},young_counts{};
    for(int seed=81;seed<89;++seed){
      auto systems=generate_stellar_catalog(seed,2500,catalog);apply_stellar_population(seed,systems,options);
      for(const auto& system:systems)if(!system.stellar_catalog_id){
        check(system.stellar_region.has_value(),"Generated region persists with stellar object");
        const auto region=static_cast<std::size_t>(*system.stellar_region);++totals[region];
        if(stellar_object_definition(system.stellar_object->type).young)++young_counts[region];
      }
    }
    const auto young_rate=[&](StellarRegion region){const auto i=static_cast<std::size_t>(region);return totals[i]?static_cast<double>(young_counts[i])/totals[i]:0.;};
    if(m==GalaxyMorphology::Ring)check(young_rate(StellarRegion::Ring)>young_rate(StellarRegion::Interior)*2,"Actual maps concentrate young stars in ring");
    if(m==GalaxyMorphology::Irregular)check(young_rate(StellarRegion::IrregularClump)>young_rate(StellarRegion::Disk)*2,"Actual maps concentrate young stars in irregular clumps");
    if(m==GalaxyMorphology::Elliptical||m==GalaxyMorphology::Lenticular)check(totals[static_cast<std::size_t>(StellarRegion::Arm)]==0,"Old morphologies do not use spiral-arm placement");
    if(m==GalaxyMorphology::Spiral)check(young_rate(StellarRegion::Arm)>young_rate(StellarRegion::InterArm)*2,"Actual maps concentrate young stars in arms");
  }
  std::cout<<"Spatial distribution checks across 100,000 generated systems passed.\n";
  for(int index=0;index<20;++index){
    std::cout<<"Home/profile case "<<index<<'\n';
    const auto& species=species_environment_profiles()[static_cast<std::size_t>(index)%4];
    PersistableFreshCampaignOptions options{"2026-09-17T00:00:00+00:00",250,6,1,species.id,StellarPopulationOptions{static_cast<GalaxyMorphology>(index%6),static_cast<PopulationState>(index%5)}};
    auto galaxy=seed_persistable_fresh_campaign(900+index,catalog,options);
    check(galaxy.colonies.size()>=7,"Every species/profile has a viable home");
    for(const auto& colony:galaxy.colonies)for(const auto& body:galaxy.bodies)if(colony.planetary_body_id==body.id)check(!body.stellar_exposure->baked,"Never found colonies on baked worlds");
  }
  for(auto type:{StellarObjectType::GYellowStar,StellarObjectType::Hypergiant,StellarObjectType::JetBlackHole}){
    std::cout<<"Navigation type "<<static_cast<int>(type)<<'\n';
    auto p=generate_stellar_physics(79,type);FleetState ship;ship.strategic_speed=22;
    begin_fleet_local_transit(ship,FleetTransitPhase::LocalArrival,{-.82f,0},{.82f,0},&p);
    const double scale=stellar_navigation_au_per_unit(p),expected=fleet_local_transit_remaining_days(ship);double spent=0;
    for(int step=0;step<2000&&!fleet_local_transit_complete(ship);++step){spent+=advance_fleet_local_transit(ship,.001);check(!stellar_approach_unsafe(p,ship.local_transit_position.x*scale,ship.local_transit_position.y*scale),"Navigation never crosses stellar/jet hazards");}
    while(!fleet_local_transit_complete(ship))spent+=advance_fleet_local_transit(ship,.001);
    check(std::abs(spent-expected)<1e-5,"Safe path travel time follows full route length");
    auto galaxy=seed_persistable_fresh_campaign(42,catalog,{"2026-09-17T00:00:00+00:00",250,6,1,"terran_baseline",StellarPopulationOptions{}});
    ship.id=91000;ship.civilization_id=galaxy.player_civilization_id;ship.name="Hazard navigation test";ship.current_system_id=0;
    galaxy.fleets={ship};auto& saved=galaxy.fleets.front();begin_fleet_local_transit(saved,FleetTransitPhase::LocalArrival,{-.82f,0},{.82f,0},&p);advance_fleet_local_transit(saved,.2);
    auto restored=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(galaxy,capture))));
    auto& loaded=restored.galaxy.fleets.front();check(loaded.stellar_transit_path==saved.stellar_transit_path,"Hazard route survives save/load");advance_fleet_local_transit(saved,.17);advance_fleet_local_transit(loaded,.17);check(saved.local_transit_position.x==loaded.local_transit_position.x&&saved.local_transit_position.y==loaded.local_transit_position.y,"Mid-route reload continues exact movement");
  }
  auto legacy=seed_persistable_fresh_campaign(42,catalog,{"2026-09-17T00:00:00+00:00",250});auto old=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(legacy,capture));check(old.find("StellarObject")==std::string::npos,"Old saves remain legacy");auto load=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(old));
  const auto migrated=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(load.galaxy,capture));
  auto physical_old=nlohmann::json::parse(old),physical_new=nlohmann::json::parse(migrated);
  const auto remove_visual_migration=[](auto&& self,nlohmann::json& value)->void{if(value.is_object()){value.erase("PlanetAppearance");for(auto& child:value)self(self,child);}else if(value.is_array())for(auto& child:value)self(self,child);};
  remove_visual_migration(remove_visual_migration,physical_old);remove_visual_migration(remove_visual_migration,physical_new);
  check(physical_old==physical_new,"Old simulation save unchanged by visual migration");
  for(const auto& b:load.galaxy.bodies)check(b.appearance.has_value(),"Legacy body receives canonical appearance");
  auto load_again=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(migrated));
  check(nlohmann::json::parse(migrated)==nlohmann::json::parse(encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(load_again.galaxy,capture))),"Migrated appearance never rerolls on subsequent loads");
  if(argc>3){
    const std::filesystem::path captures=argv[3];std::filesystem::create_directories(captures);
    const auto research=std::filesystem::path(argv[1]).parent_path().parent_path()/"research/v1";
    for(int index=0;index<6;++index){
      const auto morphology=static_cast<GalaxyMorphology>(index);
      auto galaxy=seed_persistable_fresh_campaign(41877,catalog,{"2026-09-17T00:00:00+00:00",2500,6,1,"terran_baseline",StellarPopulationOptions{morphology,stellar_default_population_state(morphology)}});
      auto runtime=IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(research),std::move(galaxy));
      std::ofstream file(captures/("morphology-"+std::to_string(index)+".player17.json"));
      file<<encode_player_campaign_v17_json(capture_player_campaign_v17(runtime,{0,"stellar-validation","2026-09-17T00:00:00+00:00"}));
      check(static_cast<bool>(file),"Native visual fixture saved");
    }
  }
  std::cout<<"Population, physics, morphology, hazards, secrecy and persistence checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
