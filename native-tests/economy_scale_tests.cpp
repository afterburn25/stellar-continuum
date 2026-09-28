#define main economy_fixture_main
#include "campaign_economy_tests.cpp"
#undef main
#include <chrono>
#include <stellar/core/settlement_body_index.hpp>
void body_lookup_contract() {
  std::vector<PlanetaryBody> bodies(5);
  bodies[0].id=4;bodies[0].system_id=1;
  bodies[1].id=4;bodies[1].system_id=2;
  bodies[2]=bodies[1]; // First duplicate must win, just like find_if.
  bodies[3].id=-7;bodies[3].system_id=-9;
  bodies[4].id=88;bodies[4].system_id=5;
  std::vector<Colony> colonies(5);
  colonies[0].system_id=2;colonies[0].planetary_body_id=4;
  colonies[1].system_id=99;colonies[1].planetary_body_id=4;
  colonies[2].system_id=1;colonies[2].planetary_body_id.reset();
  colonies[3].system_id=-9;colonies[3].planetary_body_id=-7;
  colonies[4]=colonies[0];
  {
    const SettlementBodyIndex index(colonies,bodies);
    for(const auto& colony:colonies){
      auto expected=bodies.end();
      if(colony.planetary_body_id)expected=std::find_if(bodies.begin(),bodies.end(),[&](const auto& b){return b.id==*colony.planetary_body_id&&b.system_id==colony.system_id;});
      const auto actual=index.bodies_for(colony);
      check(expected==bodies.end()?actual.empty():actual.size()==1&&actual.data()==&*expected,"Scoped body lookup changed parent/first-match semantics");
    }
    bool legacy_error=false,indexed_error=false;
    try{(void)colony_habitat_support(colonies[1],bodies);}catch(const std::invalid_argument&){legacy_error=true;}
    try{(void)colony_habitat_support(colonies[1],index.bodies_for(colonies[1]));}catch(const std::invalid_argument&){indexed_error=true;}
    check(legacy_error&&indexed_error,"Missing body no longer raises canonical error");
  }
  colonies[0].system_id=5;colonies[0].planetary_body_id=88;
  const SettlementBodyIndex refreshed(colonies,bodies);
  check(refreshed.bodies_for(colonies[0]).data()==&bodies[4],"New request retained stale body binding");
  check(SettlementBodyIndex({},bodies).bodies_for(colonies[0]).empty(),"Empty index invents a body");
}
int main(int argc,char**)try{
  body_lookup_contract();
  Inputs state;
  const int body_count=argc>1?350000:5000,colony_count=argc>1?5000:200,fleet_count=argc>1?25000:1000;
  state.civilizations.resize(32);
  for(int i=0;i<32;++i){state.civilizations[i].id=i;state.civilizations[i].species_id="terran_baseline";}
  state.economies=seed_economies(state.civilizations);
  state.construction=seed_economic_construction(state.civilizations);
  for(auto& c:state.construction)c.completed_project_ids={"industrial_automation","research_network","orbital_shipyard"};
  state.bodies.resize(body_count);
  for(int i=0;i<body_count;++i){auto& b=state.bodies[i];b.id=i*3+7;b.system_id=i/7;b.name="Scale body "+std::to_string(i);b.radius_earth=1.;b.mass_earth=1.;
    b.environment={1.,288.,101.325,PlanetaryAtmosphereRegime::OxygenNitrogen,PlanetarySolventRegime::Water,.05,false,true};}
  state.colonies.resize(colony_count);
  for(int i=0;i<colony_count;++i){auto& c=state.colonies[i];const auto& b=state.bodies[static_cast<std::size_t>(i)*body_count/colony_count];
    c.id=i;c.civilization_id=i%32;c.system_id=b.system_id;c.planetary_body_id=b.id;c.population_millions=1200+i%200;
    c.infrastructure=1.8;c.stability=.91;c.stored_food_population_days_millions=500;c.stored_water_population_days_millions=200;}
  state.fleets.resize(fleet_count);for(int i=0;i<fleet_count;++i){state.fleets[i].civilization_id=i%32;state.fleets[i].role=EconomyFleetRole::Military;}
  const auto start=std::chrono::steady_clock::now();
  advance_colony_economies(state.world(),state.colonies,state.economies,.25,true);
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  // Existing oracle serializer covers canonical colony/economy mutable fields.
  const auto encoded=native_state(state).dump();std::uint64_t hash=14695981039346656037ULL;
  for(unsigned char c:encoded){hash^=c;hash*=1099511628211ULL;}
  // Captured with the unindexed production implementation before optimization.
  check(hash==(argc>1?788429135333795526ULL:13584217053300038634ULL),"Economic state differs from the original implementation");
  std::cout<<Json{{"bodies",body_count},{"colonies",colony_count},{"fleetEconomicRecords",fleet_count},{"milliseconds",elapsed},{"stateHash",std::to_string(hash)}}.dump()<<'\n';
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
