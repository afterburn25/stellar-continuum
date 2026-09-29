#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/exploration_planning.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
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
      // explain=false must return the identical verdict and arrival fuel
      // for every target — the prefix memo must never change semantics.
      const auto quick=batch.assess(fleet,target.id,InterstellarMissionKind::ScoutReconnaissance,
          MissionFuelPolicy::ReachDestination,false);
      check(quick.is_supported==expected.is_supported&&
          quick.arrival_fuel_light_years==expected.arrival_fuel_light_years,
          "Verdict-only assess changed support or arrival fuel.");
      const auto kept= batch.assess(fleet,target.id,InterstellarMissionKind::ScoutReconnaissance,
          MissionFuelPolicy::RetainReturnToService);
      const auto kept_quick=batch.assess(fleet,target.id,InterstellarMissionKind::ScoutReconnaissance,
          MissionFuelPolicy::RetainReturnToService,false);
      check(kept.is_supported==kept_quick.is_supported,"Verdict-only assess changed return-service gating.");
      // Slot probes must return the identical is_supported verdict —
      // they are the scan loop's replacement for full assess calls.
      const int slot=lanes.slot_of_system(target.id);
      check(slot>=0,"Catalog system missing from lane slot table.");
      check(batch.probe_supported(fleet,slot,MissionFuelPolicy::ReachDestination)==quick.is_supported,
          "Slot probe changed support verdict.");
      check(batch.probe_supported(fleet,slot,MissionFuelPolicy::RetainReturnToService)==kept_quick.is_supported,
          "Slot probe changed return-service gating.");
    }
    fleet.fuel_remaining_light_years=0;
    for(int i=0;i<20;++i)check(same(assess_operational_reach(view,fleet.civilization_id,fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey),
        batch.assess(fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey)),"Batch reused stale fleet fuel.");
    for(int i=0;i<20;++i)check(batch.assess(fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey,
        MissionFuelPolicy::ReachDestination,false).is_supported==
        batch.assess(fleet,world.systems[i].id,InterstellarMissionKind::ScienceSurvey).is_supported,
        "Verdict-only assess used a stale fuel key.");
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
  check(batch.assess(fleet,world.systems[0].id,InterstellarMissionKind::Colony,
      MissionFuelPolicy::ReachDestination,false).is_supported==
      assess_operational_reach(changed,fleet.civilization_id,fleet,world.systems[0].id,InterstellarMissionKind::Colony).is_supported,
      "New scope retained old refueling state in verdict path.");
  // RouteTreeView contract: prior-chain reconstruction must reproduce
  // find_shortest_route's exact vector; invalid range and unknown ids
  // mirror the route-query contract.
  const auto slots=lanes.route_slots();
  check(slots.size()==world.systems.size(),"RouteSlot table size diverged from systems.");
  for(std::size_t i=0;i<world.systems.size();i+=17){
    const int origin=world.systems[i].id;
    const double range=300.0+static_cast<double>(i%5)*100.0;
    const auto tree=lanes.route_tree_view(origin,range);
    check(tree.distance.size()==slots.size()&&tree.prior.size()==slots.size(),"RouteTreeView size mismatch.");
    const int origin_slot=lanes.slot_of_system(origin);
    check(origin_slot>=0&&tree.distance[origin_slot]==0.0,"RouteTreeView origin slot/distance wrong.");
    for(std::size_t j=1;j<world.systems.size();j+=29){
      const int target=world.systems[j].id;
      const auto expected=lanes.find_shortest_route(origin,target,range);
      const int target_slot=lanes.slot_of_system(target);
      check(target_slot>=0&&slots[target_slot].id==target,"slot_of_system round-trip failed.");
      if(expected.empty()){
        check(!std::isfinite(tree.distance[target_slot]),"RouteTreeView marked an unreachable target finite.");
        continue;
      }
      std::vector<int> walked{target};
      for(int s=target_slot;s!=origin_slot;){s=tree.prior[s];walked.push_back(slots[s].id);}
      std::reverse(walked.begin(),walked.end());
      check(walked==expected,"RouteTreeView prior chain diverged from find_shortest_route.");
    }
  }
  check(lanes.route_tree_view(world.systems[0].id,0.0).distance.empty()&&
      lanes.route_tree_view(world.systems[0].id,std::numeric_limits<double>::quiet_NaN()).prior.empty(),
      "RouteTreeView accepted a non-positive or NaN range.");
  check(lanes.slot_of_system(999999)==-1,"slot_of_system accepted an unknown id.");
  {bool threw=false;try{lanes.route_tree_view(999999,500.0);}catch(const std::out_of_range&){threw=true;}
   check(threw,"RouteTreeView accepted an unknown origin.");}
  std::cout<<"operational reach batch tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
