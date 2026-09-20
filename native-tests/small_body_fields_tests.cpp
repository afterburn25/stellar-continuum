#include <stellar/core/small_body_fields.hpp>
#include <stellar/core/small_body_commands.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/campaign_foundation_persistence.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace stellar::core;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid field accepted");}
int main(){try{
  StellarSystem sol;sol.id=0;sol.name="Sol";sol.catalog_preset_id=std::string(sol_catalog_preset_id);
  auto bodies=create_sol_catalog(sol);auto fields=generate_small_body_fields(1701,sol,bodies);
  check(fields.size()==2&&fields[0].type==SmallBodyFieldType::Mixed&&fields[1].type==SmallBodyFieldType::Ice,"Sol field types");
  check(fields[0].inner_radius_au>planetary_orbit_au(sol,bodies[3])&&fields[0].outer_radius_au<planetary_orbit_au(sol,bodies[4])&&fields[1].inner_radius_au>planetary_orbit_au(sol,bodies[7]),"Sol field placement");
  check(fields==generate_small_body_fields(1701,sol,bodies),"Seed determinism");
  check(fields[0].visible_count==2048&&fields[1].visible_count==2048,"Sol belts lack restored visual density");
  auto legacy=fields;legacy[0].visible_count=700;legacy[1].visible_count=720;
  extract_small_body_resource(legacy[0],0,SmallBodyResource::Minerals,1);
  sol.small_body_fields=legacy;std::vector<StellarSystem> old_systems{sol};
  reconcile_small_body_orbits(old_systems,bodies);
  for(std::size_t i=0;i<legacy.size();++i){auto expected=legacy[i];expected.visible_count=2048;
    check(old_systems[0].small_body_fields->at(i)==expected,"Sol detail migration changed its orbits, identity, resources or depletion");
    const auto before=small_body_instances(legacy[i],2048),after=small_body_instances(expected,2048);
    check(after.size()==2048&&std::equal(before.begin(),before.end(),after.begin()),"Denser belts rerolled existing rocks");
  }
  const auto once=old_systems[0].small_body_fields;reconcile_small_body_orbits(old_systems,bodies);
  check(once==old_systems[0].small_body_fields,"Sol detail migration repeated destructively");
  const auto clear=[&](const StellarSystem& system,const auto& planets,const SmallBodyField& f){
    if(f.planet_centered)return;
    for(const auto& planet:planets)if(!planet.parent_body_id&&!(planet.cracked_world&&f.associated_planet_id==planet.id)){
      const double a=planetary_orbit_au(system,planet),e=planet.orbital_eccentricity;
      check(f.outer_radius_au<a*(1-e)||f.inner_radius_au>a*(1+e),"Belt crosses an ordinary eccentric planetary orbit");
    }
  };
  for(const auto& f:fields)clear(sol,bodies,f);
  auto misplaced=fields[0];misplaced.inner_radius_au=.8;misplaced.outer_radius_au=1.7;
  const auto resources=small_body_resources(misplaced,0);const auto original_seed=misplaced.seed;
  extract_small_body_resource(misplaced,0,SmallBodyResource::Minerals,1);
  const auto ledger=misplaced.extraction;place_small_body_field(sol,bodies,misplaced);clear(sol,bodies,misplaced);
  const auto repaired=misplaced;place_small_body_field(sol,bodies,misplaced);
  check(misplaced==repaired&&misplaced.seed==original_seed&&misplaced.extraction==ledger,"Orbit repair rerolled bodies or was not idempotent");
  auto instances=small_body_instances(fields[0],2048);std::set<int> variants;int irregular=0;
  for(const auto& b:instances){variants.insert(b.asset_variant_id);irregular+=b.spin.wobble>0;const auto p=stellar::engine::analytic_orbit_position(b.orbit,0);const auto q=stellar::engine::analytic_orbit_position(b.orbit,50);check(p!=q,"Bodies stationary");check(std::hypot(p[0],p[1],p[2])>=fields[0].inner_radius_au-1e-10,"Body inside inner gap");const auto spin=stellar::engine::analytic_spin_rotation(b.spin,90);check(spin==stellar::engine::analytic_spin_rotation(b.spin,90)&&spin!=stellar::engine::analytic_spin_rotation(b.spin,91),"Spin reconstruction");check(b==small_body_instance(fields[0],b.id),"Instance prefix instability");}
  check(variants.size()==4&&irregular>int(instances.size()*.1)&&irregular<int(instances.size()*.25),"Variant or irregular spin distribution");
  const auto empty_space=small_body_environment(fields[0],{0,0,0},0);check(empty_space.density==0&&empty_space.navigation_multiplier==1&&empty_space.scan_difficulty==1,"Empty space changed navigation or scanning costs");
  const double radius=2,rate=.3;stellar::engine::AnalyticOrbit orbit{radius,0,0,0,0,0,rate};const auto p=stellar::engine::analytic_orbit_position(orbit,std::numbers::pi/(2*rate));check(std::abs(p[0])<1e-10&&std::abs(p[1]-radius)<1e-10,"Independent circular orbit reference");
  auto resource=small_body_resources(fields[0],0);auto slot=std::max_element(resource.begin(),resource.end())-resource.begin();const double mined=extract_small_body_resource(fields[0],0,static_cast<SmallBodyResource>(slot),10);check(mined==10&&std::abs(small_body_resources(fields[0],0)[slot]-(resource[slot]-10))<1e-8,"Depletion is authoritative");
  sol.small_body_fields=fields;bodies[3].cracked_world=true;std::vector<StellarSystem> systems{sol};validate_small_body_catalog(systems,bodies);
  GalaxyPayloadV16Dto dto;dto.saved_at_utc="2026-09-18T00:00:00+00:00";dto.systems=capture_stellar_systems(systems);dto.planetary_bodies=capture_planetary_bodies(bodies,systems);
  const auto loaded=decode_galaxy_payload_v16_json(encode_galaxy_payload_v16_json(dto));const auto restored=restore_stellar_systems(*loaded.systems);const auto restored_bodies=restore_planetary_bodies(loaded.planetary_bodies,restored);
  check(restored[0].small_body_fields==sol.small_body_fields&&restored_bodies[3].cracked_world,"JSON roundtrip lost fields/depletion/cracked world");
  auto explicit_empty=systems;explicit_empty[0].small_body_fields=std::vector<SmallBodyField>{};initialize_small_body_fields(55,explicit_empty,bodies);check(explicit_empty[0].small_body_fields->empty(),"Explicit empty field catalog rerolled");
  auto bad=fields[0];bad.inner_radius_au=0;rejects([&]{validate_small_body_field(bad);});bad=fields[0];bad.version=2;rejects([&]{validate_small_body_field(bad);});
  auto broken=systems;broken[0].small_body_fields->at(0).associated_belt_parent_id=1;broken[0].small_body_fields->at(1).associated_belt_parent_id=999;rejects([&]{validate_small_body_catalog(broken,bodies);});
  auto malformed=encode_galaxy_payload_v16_json(dto);const auto count_pos=malformed.find("\"body_count\":");check(count_pos!=std::string::npos,"Field JSON missing");malformed.insert(count_pos+std::string("\"body_count\":").size(),"0.5e");rejects([&]{(void)decode_galaxy_payload_v16_json(malformed);});
  for(const auto key:{"composition","asset_variants","extracted"}){auto oversized=encode_galaxy_payload_v16_json(dto);const auto start=oversized.find(std::string("\"")+key+"\":");check(start!=std::string::npos,"Field array missing");const auto end=oversized.find(']',start);check(end!=std::string::npos,"Field array end missing");oversized.insert(end,",0");rejects([&]{(void)decode_galaxy_payload_v16_json(oversized);});}
  FreshCampaignState dev;dev.seed=73;dev.systems=systems;dev.bodies=bodies;
  rejects([&]{(void)force_developer_small_body_field(dev,0,SmallBodyFieldType::Ice,10);});
  check(dev.systems[0].small_body_fields==systems[0].small_body_fields,"Unauthorized spawn mutated world");dev.developer_provenance=CampaignDeveloperProvenance{};
  const int new_id=force_developer_small_body_field(dev,0,SmallBodyFieldType::CrackedCluster,10,bodies[3].id);
  check(dev.systems[0].small_body_fields->back().id==new_id&&dev.systems[0].small_body_fields->back().epoch_days==10&&dev.developer_provenance->tools_used,"Developer spawn lost authoritative identity/epoch");
  auto supply_field=fields[1];std::array<double,9> demand;demand.fill(1000);const auto supply=harvest_small_body_supply(supply_field,0,10,demand);double delivered=0;for(double amount:supply)delivered+=amount;check(std::abs(delivered-10)<1e-8,"Harvest exceeded cargo capacity");
  check(supply_field.extraction.size()==1,"Supply did not persist depletion");
  StellarSystem ordinary;ordinary.id=1;ordinary.name="Test";auto config=small_body_configuration();config.giant_belt_multiplier=1;
  std::array<int,5> distribution{};int disks=0,shattered=0,cracked=0;auto local=bodies;for(auto& b:local){b.system_id=1;b.cracked_world=false;}const int samples=12000;
  for(int seed=0;seed<samples;++seed){auto generated=generate_small_body_fields(seed,ordinary,local,config);int rock=0,ice=0;for(const auto& f:generated){rock+=static_cast<int>(f.type)<4;ice+=f.type==SmallBodyFieldType::Ice;disks+=f.type==SmallBodyFieldType::DebrisDisk;shattered+=f.type==SmallBodyFieldType::Shattered;}++distribution[rock+ice==0?0:(rock+ice>2?4:(rock&&ice?3:(ice?2:1)))];}
  const std::array<double,5> low{.20,.30,.10,.15,.05},high{.30,.40,.20,.25,.12};for(std::size_t i=0;i<5;++i)check(double(distribution[i])/samples>low[i]&&double(distribution[i])/samples<high[i],"Belt rarity outside requested range");
  check(double(disks)/samples>.05&&double(disks)/samples<.10&&double(shattered)/samples>.02&&double(shattered)/samples<.06,"Overlay rarity");
  local[3].cracked_world=true;for(int seed=0;seed<2000;++seed){const auto generated=generate_small_body_fields(seed,ordinary,local,config);for(const auto& f:generated)cracked+=f.type==SmallBodyFieldType::CrackedCluster;}
  check(cracked>1450&&cracked<1750,"Cracked-world local probability");
  for(int i=0;i<9;++i){const auto f=make_small_body_field(ordinary,local,static_cast<SmallBodyFieldType>(i),i,99,local[3].id);validate_small_body_field(f);clear(ordinary,local,f);check(!small_body_instances(f,10).empty(),"Missing field type");}
  std::cout<<"small bodies: deterministic generation, 12000 seeds, Sol, orbits, spin, variants, depletion and JSON passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
