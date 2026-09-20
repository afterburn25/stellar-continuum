#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/campaign_coordinator.hpp>
#include <cmath>
#include <iostream>

using namespace stellar::core;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct Scenario {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<CivilizationEconomy> economies;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  CivilizationKnowledgeState knowledge;
  Scenario(){
    for(int i=0;i<4;++i){StellarSystem s;s.id=i;s.name="System "+std::to_string(i);s.position={i*100.f,0.f,{}};systems.push_back(s);}
    Civilization c;c.id=0;c.home_system_id=0;c.name="Survey authority";c.development_stage=CivilizationDevelopmentStage::WarpCapable;civilizations.push_back(c);
    CivilizationEconomy economy;economy.civilization_id=0;economies.push_back(economy);
    Colony base;base.id=1;base.system_id=0;base.civilization_id=0;colonies.push_back(base);
    FleetState f;f.id=1;f.civilization_id=0;f.name="Reserve scout";f.role=FleetRole::Scout;
    f.current_system_id=0;f.maximum_leg_range_light_years=110;f.fuel_capacity_light_years=500;f.fuel_remaining_light_years=500;fleets.push_back(f);
    knowledge.mark_system_fully_surveyed(0,0);
  }
  ExplorationAdvanceWorldView view(InterstellarLaneNetwork &lanes){return {systems,bodies,civilizations,fleets,colonies,economies,knowledge,lanes};}
};
int main()try{
  constexpr auto scout=InterstellarMissionKind::ScoutReconnaissance;
  constexpr auto reserve=MissionFuelPolicy::RetainReturnToService;
  Scenario s;InterstellarLaneNetwork lanes(s.systems);
  OperationalReachBatch batch({s.systems,s.colonies,lanes},0);
  auto outgoing=batch.assess(s.fleets[0],3,scout);
  check(outgoing.is_supported&&outgoing.arrival_fuel_light_years==200.,"Manual one-way reach or arrival fuel changed.");
  check(!batch.assess(s.fleets[0],3,scout,reserve).is_supported,"AI accepted a trip without return reserves.");
  check(batch.assess(s.fleets[0],2,scout,reserve).is_supported,"Safe round trip rejected.");
  check(lanes.cached_route_tree_count()<=2,"Return planning built a route tree for each target.");
  auto prospective=s.fleets[0];prospective.current_system_id=2;prospective.fuel_remaining_light_years=300;
  auto back=batch.nearest_refueling(prospective,scout);
  check(back&&back->system_id==0&&back->reach.route_distance_light_years==200.,"Canonical return base selection failed.");
  prospective.fuel_remaining_light_years=100;
  check(!batch.nearest_refueling(prospective,scout),"Recovery invented missing fuel.");
  Colony foreign;foreign.id=2;foreign.civilization_id=1;foreign.system_id=3;s.colonies.push_back(foreign);
  OperationalReachBatch enemy({s.systems,s.colonies,lanes},0);
  check(!enemy.assess(s.fleets[0],3,scout,reserve).is_supported,"Foreign service bypassed fuel reserve rule.");
  s.colonies.back().civilization_id=0;s.colonies.back().kind=SettlementKind::ResourceOutpost;
  OperationalReachBatch outpost({s.systems,s.colonies,lanes},0);
  auto serviced=outpost.assess(s.fleets[0],3,scout,reserve);
  check(serviced.is_supported&&serviced.arrival_fuel_light_years==250.,"Owned outpost did not provide its canonical half-capacity service.");

  // Exercise the real campaign subsystem, including local travel, warp,
  // reconnaissance, automatic return orders and arrival refuelling.
  Scenario campaign;InterstellarLaneNetwork travel(campaign.systems);
  CampaignSubsystemRuntime subsystems;
  bool returned=false,visited=false;
  for(int tick=0;tick<4000;++tick){
    (void)subsystems.exploration.advance(campaign.view(travel),.25);
    const auto &f=campaign.fleets[0];
    returned|=f.return_to_base_requested;visited|=f.current_system_id==2;
    check(f.fuel_remaining_light_years>=0&&!f.return_to_base_failure_reason,"Fresh automated scout became stranded.");
  }
  check(returned&&visited,"Production AI never explored and returned for fuel.");
  check(campaign.fleets[0].current_system_id==0&&!campaign.fleets[0].destination_system_id&&campaign.fleets[0].fuel_remaining_light_years==500.,"AI did not finish at the refuelling settlement.");
  check(campaign.knowledge.system_survey_level(0,2)>=SystemSurveyLevel::partially_surveyed&&
      campaign.knowledge.system_survey_level(0,3)<SystemSurveyLevel::partially_surveyed,"AI survey reached an unsafe target or skipped safe work.");
  // A new outpost changes the next planning scope, without stale reach state.
  auto new_base=campaign.colonies[0];new_base.id=2;new_base.system_id=3;campaign.colonies.push_back(new_base);
  for(int tick=0;tick<1600;++tick)(void)subsystems.exploration.advance(campaign.view(travel),.25);
  check(campaign.knowledge.system_survey_level(0,3)>=SystemSurveyLevel::partially_surveyed,"New refuelling base did not unlock more exploration.");

  Scenario stranded;InterstellarLaneNetwork old_lanes(stranded.systems);
  auto &f=stranded.fleets[0];f.current_system_id=2;f.position={200,0};f.fuel_remaining_light_years=8;
  stranded.knowledge.mark_system_fully_surveyed(0,2);
  (void)subsystems.exploration.advance(stranded.view(old_lanes),.25);
  check(f.return_to_base_failure_reason&&f.current_system_id==2&&f.fuel_remaining_light_years==8&&!f.destination_system_id,"Old stranded ship was teleported, refuelled or silently ignored.");
  const auto revision=f.mission_order_revision;
  (void)subsystems.exploration.advance(stranded.view(old_lanes),.25);
  check(f.mission_order_revision==revision,"Repeated unavailable return manufactured orders.");
  stranded.civilizations[0].is_player=true;f.return_to_base_failure_reason.reset();
  (void)subsystems.exploration.advance(stranded.view(old_lanes),.25);
  check(!f.return_to_base_failure_reason&&!f.destination_system_id,"AI reserve policy took over a human-controlled ship.");
  std::cout<<"exploration return-fuel safety tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
