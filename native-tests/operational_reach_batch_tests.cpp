#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/exploration_planning.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_set>
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
    // nearest_refueling_verdict must pick the identical base with an
    // identical distance/arrival fuel — only the reason payload is
    // omitted; the winner's route materializes from the settled prior
    // chain so assign_fleet_route consumes it without a cache lookup.
    const auto rich=batch.nearest_refueling(fleet,InterstellarMissionKind::ScoutReconnaissance);
    const auto lean=batch.nearest_refueling_verdict(fleet);
    check(rich.has_value()==lean.has_value(),"nearest_refueling_verdict diverged on emptiness.");
    if(rich){
      check(rich->system_id==lean->system_id,"nearest_refueling_verdict picked a different base.");
      check(lean->reach.route_distance_light_years==rich->reach.route_distance_light_years,
          "nearest_refueling_verdict distance diverged.");
      check(lean->reach.arrival_fuel_light_years==rich->reach.arrival_fuel_light_years,
          "nearest_refueling_verdict arrival fuel diverged.");
      check(lean->reach.route_system_ids==rich->reach.route_system_ids,
          "nearest_refueling_verdict materialized a different winner route.");
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
  // route_tree_toward contract: settled entries are bit-identical to the
  // full build's, needed slots settle in (distance, id) order, and the
  // accept callback's early stop leaves only a prefix resolved.
  for(std::size_t i=0;i<world.systems.size();i+=41){
    const int origin=world.systems[i].id;
    const double range=300.0+static_cast<double>(i%5)*100.0;
    const auto full=lanes.route_tree_view(origin,range);
    std::vector<int> needed;
    for(std::size_t j=1;j<world.systems.size();j+=23)
      needed.push_back(lanes.slot_of_system(world.systems[j].id));
    std::vector<double> dist;std::vector<int> prior;
    std::vector<int> settled_order;
    lanes.route_tree_toward(origin,range,needed,dist,prior,
        [&](int slot){settled_order.push_back(slot);return false;});
    check(dist.size()==slots.size()&&prior.size()==slots.size(),"route_tree_toward size mismatch.");
    // Settlement order is ascending (distance, system id).
    std::vector<std::pair<double,int>> full_needed;
    for(const int slot:needed)
      if(std::isfinite(full.distance[slot]))full_needed.emplace_back(full.distance[slot],slots[slot].id);
    std::sort(full_needed.begin(),full_needed.end());
    check(settled_order.size()==full_needed.size(),"route_tree_toward settled count diverged.");
    for(std::size_t k=0;k<settled_order.size();++k){
      const int slot=settled_order[k];
      check(slots[slot].id==full_needed[k].second,"route_tree_toward settle order diverged from (distance,id).");
      check(dist[slot]==full.distance[slot]&&prior[slot]==full.prior[slot],"route_tree_toward settled entry diverged from full tree.");
    }
    // Early accept stops at the minimum-(distance, id) approved slot.
    if(!full_needed.empty()){
      const auto approved=lanes.slot_of_system(full_needed[full_needed.size()/2].second);
      int accepted=-1;std::vector<double> d2;std::vector<int> p2;
      lanes.route_tree_toward(origin,range,needed,d2,p2,
          [&](int slot){if(slot==approved){accepted=slot;return true;}return false;});
      check(accepted==approved,"route_tree_toward early accept missed the approved slot.");
    }
  }
  {bool threw=false;try{std::vector<double> d;std::vector<int> p;lanes.route_tree_toward(999999,500.0,{},d,p);}catch(const std::out_of_range&){threw=true;}
   check(threw,"route_tree_toward accepted an unknown origin.");}
  {std::vector<double> d{1.,2.};std::vector<int> p{3};
   lanes.route_tree_toward(world.systems[0].id,0.0,{},d,p);
   check(d.empty()&&p.empty(),"route_tree_toward kept outputs on a non-positive range.");}
  // Survey-work dirty-patch parity: after survey-level writes, the
  // patched shared (civilization, role) list must drive the identical
  // select and existence answers a fresh index's full rebuild produces.
  // The mutations cover removal (level exits the scout work set) and
  // band update (unknown -> detected keeps membership but reprioritizes).
  fleet.fuel_remaining_light_years=fleet.fuel_capacity_light_years;
  {
    ExplorationMissionPlanner planner;
    ExplorationPlanningSharedIndex shared;
    std::unordered_set<int> reservations;
    bool fallback=false;
    planner.select_supported_candidate(planning,fleet,
        MissionFuelPolicy::ReachDestination,reservations,fallback,&shared);
    (void)planner.has_supported_mission_target(planning,fleet.id,
        MissionFuelPolicy::ReachDestination,&shared);
    world.knowledge.mark_system_fully_surveyed(fleet.civilization_id,
                                               world.systems[40].id);
    world.knowledge.advance_system_survey(fleet.civilization_id,
                                          world.systems[41].id,0.5);
    world.knowledge.reveal_system(fleet.civilization_id,
                                  world.systems[42].id);
    // Another civilization's write must not invalidate this list at all.
    world.knowledge.mark_system_fully_surveyed(fleet.civilization_id+1,
                                               world.systems[43].id);
    bool patched_fallback=false;
    const auto patched=planner.select_supported_candidate(planning,fleet,
        MissionFuelPolicy::ReachDestination,reservations,patched_fallback,&shared);
    const auto patched_has=planner.has_supported_mission_target(planning,fleet.id,
        MissionFuelPolicy::ReachDestination,&shared);
    ExplorationPlanningSharedIndex fresh;
    bool rebuilt_fallback=false;
    const auto rebuilt=planner.select_supported_candidate(planning,fleet,
        MissionFuelPolicy::ReachDestination,reservations,rebuilt_fallback,&fresh);
    const auto rebuilt_has=planner.has_supported_mission_target(planning,fleet.id,
        MissionFuelPolicy::ReachDestination,&fresh);
    check(patched.has_value()==rebuilt.has_value(),"Dirty-patch changed select emptiness.");
    if(patched)check(patched->system_id==rebuilt->system_id,"Dirty-patch changed the selected system.");
    check(patched_fallback==rebuilt_fallback,"Dirty-patch changed the shared-fallback flag.");
    check(patched_has==rebuilt_has,"Dirty-patch changed the existence verdict.");
  }
  std::cout<<"operational reach batch tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
