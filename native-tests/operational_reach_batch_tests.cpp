#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/exploration_planning.hpp>
#include <iostream>
using namespace stellar::core;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
bool same(const MissionReachAssessment &a,const MissionReachAssessment &b){
  return a.is_supported==b.is_supported&&a.is_authoritative==b.is_authoritative&&a.reason==b.reason&&
      a.route_system_ids==b.route_system_ids&&a.route_distance_light_years==b.route_distance_light_years&&a.arrival_fuel_light_years==b.arrival_fuel_light_years;
}
int main(int argc,char **argv)try{
  check(argc==2,"Expected astronomy catalog.");
  auto world=seed_persistable_fresh_campaign(9142050,load_nearby_catalog(argv[1]),
      {"2050-03-21T00:00:00Z",500,3,0,"terran_baseline",StellarPopulationOptions{}});
  InterstellarLaneNetwork lanes(world.systems);
  FleetState fleet;fleet.id=7123;fleet.civilization_id=world.player_civilization_id;fleet.name="Batch assessment";
  fleet.is_active=true;fleet.role=FleetRole::Scout;fleet.current_system_id=world.systems[20].id;
  fleet.fuel_capacity_light_years=1200;fleet.maximum_leg_range_light_years=420;
  OperationalReachWorldView view{world.systems,world.colonies,lanes};
  for(const auto fuel:{8.,500.,1200.}){
    fleet.fuel_remaining_light_years=fuel;
    OperationalReachBatch batch(view,fleet.civilization_id);
    for(const auto &target:world.systems){
      const auto expected=assess_operational_reach(view,fleet.civilization_id,fleet,target.id,InterstellarMissionKind::ScoutReconnaissance);
      check(same(expected,batch.assess(fleet,target.id,InterstellarMissionKind::ScoutReconnaissance)),"Batched reach changed routes, fuel or explanations.");
    }
    fleet.fuel_remaining_light_years=0;
    for(int i=0;i<20;++i)check(same(assess_operational_reach(view,fleet.civilization_id,fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey),
        batch.assess(fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey)),"Batch reused stale fleet fuel.");
    check(!batch.assess(fleet,999999,InterstellarMissionKind::Colony).is_supported,"Batch accepted absent target.");
    ++fleet.civilization_id;check(!batch.assess(fleet,world.systems[0].id,InterstellarMissionKind::Logistics).is_supported,"Batch crossed civilization authority.");--fleet.civilization_id;
  }
  world.fleets={fleet};
  ExplorationPlanningWorldView planning{world.systems,world.bodies,world.fleets,world.colonies,world.knowledge,lanes};
  const auto optimized=ExplorationMissionPlanner{}.build_plan(planning,fleet.id,64);
  const auto unbatched=ExplorationMissionPlanner{[](OperationalReachWorldView w,int c,const FleetState &f,int target,InterstellarMissionKind kind){
    return assess_operational_reach(w,c,f,target,kind);
  }}.build_plan(planning,fleet.id,64);
  check(optimized.candidates.size()==unbatched.candidates.size()&&optimized.status==unbatched.status,"Batch altered planning window.");
  for(std::size_t i=0;i<optimized.candidates.size();++i){const auto &a=optimized.candidates[i],&b=unbatched.candidates[i];
    check(a.system_id==b.system_id&&a.reason==b.reason&&same(a.reach,b.reach),"Batch changed ordering or bypassed a custom reach provider.");
  }
  // A new planning scope must pick up changed refueling/ownership immediately.
  world.colonies.clear();OperationalReachWorldView changed{world.systems,world.colonies,lanes};OperationalReachBatch batch(changed,fleet.civilization_id);
  check(same(batch.assess(fleet,world.systems[0].id,InterstellarMissionKind::Colony),
      assess_operational_reach(changed,fleet.civilization_id,fleet,world.systems[0].id,InterstellarMissionKind::Colony)),"New scope retained old refueling state.");
  std::cout<<"operational reach batch tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
