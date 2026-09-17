#include "native_construction_controller.hpp"
#include "native_fleet_controller.hpp"
#include "native_research_controller.hpp"
#include "native_shipyard_controller.hpp"
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_save.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace stellar::core;
using namespace stellar::native_construction;
using namespace stellar::native_fleet;
using namespace stellar::native_research;
using namespace stellar::native_shipyard;
namespace fs=std::filesystem;
namespace {
constexpr double maximum_days=10'958.;
struct Run {bool reached_route{};bool physical_yard_complete{};bool interstellar_designs_locked{};std::size_t fleet_count{},routed_count{},colony_count{};ResearchMaturity warp_maturity{ResearchMaturity::rumored};std::string terminal;double day{};double treasury{};std::vector<std::string> trace;};
void require(bool v,const std::string&m){if(!v)throw std::runtime_error(m);}
std::string stamp(double day,std::string_view text){std::ostringstream out;out<<day<<':'<<text;return out.str();}
PreparedPlayerCampaignSave checked_snapshot(CampaignFrame &frame,const fs::path &research_root){
 const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(),"0.1.7-alpha","2044-05-06T07:08:09Z"};
 const auto prepared=PreparedPlayerCampaignSave::capture(frame.runtime(),options);
 const auto bytes=encode_player_campaign_v17_json(prepared.payload());
 try{
  auto restored=restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),bytes);
  require(restored.simulation_days()==options.simulation_days,"progressed Player17 restore changed the clock");
  auto resumed=std::move(restored).activate();
  const auto recaptured=PreparedPlayerCampaignSave::capture(resumed,options);
  const auto recaptured_bytes=encode_player_campaign_v17_json(recaptured.payload());
  // JSON object member order is not state: leadership dictionaries are sorted
  // on restore. Compare every value and array element, without dropping fields.
  const auto before=nlohmann::json::parse(bytes),after=nlohmann::json::parse(recaptured_bytes);
  if(before!=after){
   throw std::runtime_error("progressed Player17 roundtrip changed the campaign: "+nlohmann::json::diff(before,after).dump().substr(0,4000));
  }
 }catch(const PlayerCampaignJsonError &error){
  throw std::runtime_error(std::string("Progressed save roundtrip failed: ")+error.what()+" path="+error.path()+" inner="+error.inner_type().value_or("")+": "+error.inner_message().value_or(""));
 }
 return prepared;
}
ResearchMaturity maturity(CampaignFrame &frame,std::string_view id){const auto &state=frame.runtime().research().get_civilization(frame.runtime().world().campaign().player_civilization_id);const auto *node=state.try_get_node_state(id);return node?node->maturity:ResearchMaturity::rumored;}
CivilizationEconomy &economy(CampaignFrame &frame){auto&w=frame.runtime().world().campaign();return *std::ranges::find(w.economies,w.player_civilization_id,&CivilizationEconomy::civilization_id);}
void advance_day(CampaignFrame &frame){const auto result=frame.advance(1.);require(result.route==CampaignFrameRoute::Strategic&&result.completed_substeps.size()<=4,"campaign left bounded strategic stepping");}
bool research_to(CampaignFrame &frame,NativeResearchController &controller,std::uint64_t generation,std::string_view id,ResearchMaturity target,Run&run){
 const auto opening=maturity(frame,id);if(opening==ResearchMaturity::archived){run.terminal="research already archived: "+std::string(id);return false;}if(static_cast<int>(opening)>=static_cast<int>(target)){run.trace.push_back(stamp(frame.clock().simulation_days(),"research-already "+std::string(id)));return true;}
 auto window=controller.build(frame,generation);auto node=std::ranges::find(window.nodes,id,&NativeResearchNode::id);if(node==window.nodes.end()){run.terminal="required research is not visible: "+std::string(id);return false;}if(!node->active){if(node->primary_action.intent!=NativeResearchIntent::Start||!node->primary_action.enabled){run.terminal="research cannot start "+std::string(id)+": "+node->primary_action.reason;return false;}auto result=controller.execute(frame,generation,window.research_revision,window.funding_revision,NativeResearchIntent::Start,id);if(!result.accepted){run.terminal="research start rejected "+std::string(id)+": "+result.message;return false;}run.trace.push_back(stamp(frame.clock().simulation_days(),"research-start "+std::string(id)));}
 while(true){const auto state=maturity(frame,id);if(state==ResearchMaturity::archived){run.terminal="research archived before target: "+std::string(id);run.trace.push_back(stamp(frame.clock().simulation_days(),run.terminal));return false;}if(static_cast<int>(state)>=static_cast<int>(target))break;if(frame.clock().simulation_days()>=maximum_days){run.terminal="30-year research bound reached at "+std::string(id);return false;}advance_day(frame);}
 run.trace.push_back(stamp(frame.clock().simulation_days(),"research-target "+std::string(id)));return true;
}
Run run(std::uint64_t seed,const fs::path&research_root,const fs::path&catalog,const std::optional<fs::path>&profile_save={}){
 auto world=seed_persistable_fresh_campaign(seed,load_nearby_catalog(catalog),{"2044-05-06T07:08:09Z",500,6,1,"terran_baseline"});
 StrategicClock clock;clock.set_speed(StrategicSpeed::Demo);
 CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(research_root),std::move(world)),std::move(clock),CampaignFramePolicy::Developer);
 Run result;constexpr std::uint64_t generation=1;NativeResearchController research;NativeConstructionController construction;NativeShipyardController shipyard;NativeFleetController fleets;auto finish=[&]{if(!profile_save)(void)checked_snapshot(frame,research_root);result.day=frame.clock().simulation_days();result.treasury=economy(frame).credits;result.warp_maturity=maturity(frame,"warp_metric_theory");const auto construction_state=construction.build(frame,generation);const auto yard=std::ranges::find(construction_state.projects,std::string("orbital_shipyard"),&NativeConstructionProject::id);result.physical_yard_complete=yard!=construction_state.projects.end()&&yard->complete;const auto fleet_state=fleets.build(frame,generation);result.fleet_count=fleet_state.own_fleets.size();result.colony_count=frame.runtime().world().campaign().colonies.size();const auto ship_state=shipyard.build(frame,generation);result.interstellar_designs_locked=std::ranges::none_of(ship_state.available_designs,[](const auto&design){return design.id=="warp_scout"||design.id=="science_vessel";});return result;};
 auto construction_view=construction.build(frame,generation);auto launch=std::ranges::find(construction_view.projects,std::string("orbital_launch_complex"),&NativeConstructionProject::id);require(launch!=construction_view.projects.end()&&launch->queue.enabled,"fresh launch complex is not canonically available");auto launch_result=construction.queue(frame,generation,construction_view.construction_revision,launch->id);require(launch_result.accepted,"launch complex authorization failed: "+launch_result.message);result.trace.push_back(stamp(0.,"construction-start orbital_launch_complex"));
 const std::vector<std::string> orbital_nodes={"in_space_assembly","asteroid_prospecting","asteroid_mining","vacuum_refining","orbital_manufacturing","orbital_shipyard"};
 for(const auto&id:orbital_nodes)if(!research_to(frame,research,generation,id,ResearchMaturity::mature,result))return finish();
 construction_view=construction.build(frame,generation);auto yard_project=std::ranges::find(construction_view.projects,std::string("orbital_shipyard"),&NativeConstructionProject::id);if(yard_project==construction_view.projects.end()||!yard_project->queue.enabled){result.terminal="physical orbital shipyard cannot start: "+(yard_project==construction_view.projects.end()?std::string("not visible"):yard_project->queue.message);return finish();}if(!yard_project->complete&&!yard_project->active&&!yard_project->queued){auto order=construction.queue(frame,generation,construction_view.construction_revision,yard_project->id);if(!order.accepted){result.terminal="physical orbital shipyard rejected: "+order.message;return finish();}result.trace.push_back(stamp(frame.clock().simulation_days(),"construction-start orbital_shipyard"));}
 const std::vector<std::string> interstellar_nodes={"gravitational_physics","field_theory","warp_metric_theory","exotic_energy_coupling","micro_field_distortion","warp_field_control"};
 for(const auto&id:interstellar_nodes)if(!research_to(frame,research,generation,id,ResearchMaturity::mature,result))return finish();
 construction_view=construction.build(frame,generation);auto test_facility=std::ranges::find(construction_view.projects,std::string("warp_test_facility"),&NativeConstructionProject::id);if(test_facility==construction_view.projects.end()||!test_facility->queue.enabled){result.terminal="warp test facility cannot start: "+(test_facility==construction_view.projects.end()?std::string("not visible"):test_facility->queue.message);return finish();}{auto order=construction.queue(frame,generation,construction_view.construction_revision,test_facility->id);if(!order.accepted){result.terminal="warp test facility rejected: "+order.message;return finish();}result.trace.push_back(stamp(frame.clock().simulation_days(),"construction-start warp_test_facility"));}
 while(true){construction_view=construction.build(frame,generation);test_facility=std::ranges::find(construction_view.projects,std::string("warp_test_facility"),&NativeConstructionProject::id);if(test_facility!=construction_view.projects.end()&&test_facility->complete)break;if(frame.clock().simulation_days()>=maximum_days){result.terminal="warp test facility missed 30-year bound";return finish();}advance_day(frame);}advance_day(frame);result.trace.push_back(stamp(frame.clock().simulation_days(),"construction-complete warp_test_facility"));
 if(!research_to(frame,research,generation,"prototype_warp_drive",ResearchMaturity::demonstrated,result))return finish();
 while(true){construction_view=construction.build(frame,generation);yard_project=std::ranges::find(construction_view.projects,std::string("orbital_shipyard"),&NativeConstructionProject::id);if(yard_project!=construction_view.projects.end()&&yard_project->complete)break;if(frame.clock().simulation_days()>=maximum_days){result.terminal="physical orbital shipyard missed 30-year bound";return finish();}advance_day(frame);}
 {auto view=shipyard.build(frame,generation);auto scout=std::ranges::find(view.available_designs,std::string("warp_scout"),&NativeShipDesign::id);auto science=std::ranges::find(view.available_designs,std::string("science_vessel"),&NativeShipDesign::id);if(scout==view.available_designs.end()||science==view.available_designs.end()){result.terminal="interstellar designs remain canonically locked";return finish();}const auto scout_id=scout->id;const auto science_id=science->id;auto first=shipyard.start(frame,generation,view.shipyard_revision,scout_id);if(!first.accepted){result.terminal="scout authorization failed: "+first.message;return finish();}view=shipyard.build(frame,generation);auto second=shipyard.start(frame,generation,view.shipyard_revision,science_id);if(!second.accepted){result.terminal="science authorization failed: "+second.message;return finish();}require(fleets.build(frame,generation).own_fleets.empty(),"ship authorization created fleets before canonical frame advancement");result.trace.push_back(stamp(frame.clock().simulation_days(),"ship-orders scout science"));}
 while(fleets.build(frame,generation).own_fleets.size()<2){if(frame.clock().simulation_days()>=maximum_days){result.terminal="ship builds missed 30-year bound";return finish();}advance_day(frame);}
 if(profile_save){
  constexpr double export_bound_days=36'525.;
  while(fleets.build(frame,generation).own_fleets.size()<24){
   if(frame.clock().simulation_days()>=export_bound_days){result.terminal="developed fleet fixture exceeded 100-year bound";return finish();}
   auto view=shipyard.build(frame,generation);
   const auto design=(fleets.build(frame,generation).own_fleets.size()%2u)==0u?"warp_scout":"science_vessel";
   auto found=std::ranges::find(view.available_designs,std::string(design),&NativeShipDesign::id);
   if(found==view.available_designs.end()){result.terminal="developed fleet design unavailable: "+std::string(design);return finish();}
   auto order=shipyard.start(frame,generation,view.shipyard_revision,found->id);
   if(!order.accepted){result.terminal="developed fleet authorization failed: "+order.message;return finish();}
   const auto target=fleets.build(frame,generation).own_fleets.size()+1;
   while(fleets.build(frame,generation).own_fleets.size()<target){if(frame.clock().simulation_days()>=export_bound_days){result.terminal="developed fleet build exceeded 100-year bound";return finish();}advance_day(frame);}
  }
  auto view=fleets.build(frame,generation);std::size_t routed{};
  for(const auto&fleet:view.own_fleets){
   if(!fleets.select(frame,generation,fleet.id).accepted){result.terminal="developed fleet selection failed";return finish();}
   std::vector<NativeFleetRoutePreview> routes;
   for(const auto&system:frame.runtime().world().campaign().systems){
    if(fleet.current_system_id==system.id)continue;
    auto preview=fleets.preview_selected_route(frame,generation,system.id);
    if(preview.command_available)routes.push_back(std::move(preview));
   }
   std::ranges::sort(routes,[](const auto&left,const auto&right){return left.route_distance_light_years==right.route_distance_light_years?left.target_system_id<right.target_system_id:left.route_distance_light_years>right.route_distance_light_years;});
   if(routes.empty()){result.terminal="developed fleet has no reachable route";return finish();}
   const auto&chosen=routes[static_cast<std::size_t>(fleet.id)%std::min<std::size_t>(8,routes.size())];
   auto order=fleets.issue_selected_route(frame,chosen);
   if(!order.accepted){result.terminal="developed fleet route rejected: "+order.message;return finish();}
   ++routed;
  }
  if(routed!=24){result.terminal="developed fleet routing did not cover all ships";return finish();}result.routed_count=routed;
  // Route acceptance is not motion: the ordinary simulation step must admit
  // local departure before this artifact can claim an active fleet workload.
  advance_day(frame);
  const auto active=fleets.build(frame,generation);
  require(active.own_fleets.size()==24&&std::ranges::all_of(active.own_fleets,[](const auto&fleet){return fleet.transit_phase!=FleetTransitPhase::None;}),"developed fleet orders did not enter real transit");
  require(!fs::exists(*profile_save),"--profile-save path appeared during developed fleet generation");
  const auto prepared=checked_snapshot(frame,research_root);write_prepared_player_campaign(*profile_save,prepared,false);result.reached_route=true;result.terminal="exported developed fleet fixture";return finish();
 }
 {auto view=fleets.build(frame,generation);auto scout=std::ranges::find_if(view.own_fleets,[](const auto&f){return f.role==FleetRole::Scout;});require(scout!=view.own_fleets.end(),"completed builds lack scout");require(fleets.select(frame,generation,scout->id).accepted,"scout selection failed");const auto&w=frame.runtime().world().campaign();for(const auto&system:w.systems){if(scout->current_system_id==system.id)continue;auto preview=fleets.preview_selected_route(frame,generation,system.id);if(!preview.command_available)continue;auto order=fleets.issue_selected_route(frame,preview);if(!order.accepted)continue;const auto ordered=fleets.build(frame,generation);const auto before=std::ranges::find(ordered.own_fleets,scout->id,&NativeOwnFleet::id);require(before!=ordered.own_fleets.end(),"ordered scout disappeared");const auto before_progress=before->transit_progress;const auto before_phase=before->transit_phase;const auto before_position=before->position;advance_day(frame);auto after=fleets.build(frame,generation);auto moved=std::ranges::find(after.own_fleets,scout->id,&NativeOwnFleet::id);require(moved!=after.own_fleets.end(),"advancing scout disappeared");const auto advanced=(std::isfinite(moved->transit_progress)&&moved->transit_progress>before_progress)||moved->transit_phase!=before_phase||moved->position.x!=before_position.x||moved->position.y!=before_position.y||moved->current_system_id==system.id;require(advanced,"route assignment did not produce transit progress through CampaignFrame");result.trace.push_back(stamp(frame.clock().simulation_days(),"scout-route-progress "+std::to_string(system.id)+" "+std::to_string(before_progress)+"->"+std::to_string(moved->transit_progress)));result.reached_route=true;result.terminal="reached canonical scout route";break;}if(!result.reached_route)result.terminal="no reachable real catalog route for completed scout";}
 return finish();
}
}
int main(int argc,char**argv)try{require(argc==3||argc==5,"Usage: fresh_progression <research-root> <catalog> [--discover seed|--profile-save absolute-new-path]");const auto research_root=fs::absolute(argv[1]);const auto catalog=fs::absolute(argv[2]);auto print=[](std::uint64_t seed,const Run&outcome){std::cout<<"seed="<<seed<<'\n';for(const auto&line:outcome.trace)std::cout<<line<<'\n';std::cout<<"terminal="<<outcome.terminal<<" day="<<outcome.day<<" treasury="<<outcome.treasury<<" yard="<<outcome.physical_yard_complete<<" fleets="<<outcome.fleet_count<<" routed="<<outcome.routed_count<<" colonies="<<outcome.colony_count<<" designs_locked="<<outcome.interstellar_designs_locked<<" developer_offline_stepping=1"<<'\n';};if(argc==5){
  if(std::string_view(argv[3])=="--discover"){
    const auto seed=std::stoull(argv[4]);const auto outcome=run(seed,research_root,catalog);print(seed,outcome);return 0;
  }
  require(std::string_view(argv[3])=="--profile-save","unknown discovery option");
  const fs::path output(argv[4]);
  require(output.is_absolute()&&fs::is_directory(output.parent_path())&&!fs::exists(output),"--profile-save requires an absolute nonexistent path with an existing parent");
  const auto outcome=run(115501,research_root,catalog,output);print(115501,outcome);
  require(outcome.reached_route&&outcome.fleet_count==24&&outcome.terminal=="exported developed fleet fixture","developed fleet fixture did not reach all real routes");
  return 0;
}auto same=[](const Run&a,const Run&b){return a.reached_route==b.reached_route&&a.physical_yard_complete==b.physical_yard_complete&&a.interstellar_designs_locked==b.interstellar_designs_locked&&a.fleet_count==b.fleet_count&&a.routed_count==b.routed_count&&a.colony_count==b.colony_count&&a.warp_maturity==b.warp_maturity&&a.terminal==b.terminal&&a.day==b.day&&a.treasury==b.treasury&&a.trace==b.trace;};const auto negative=run(115500,research_root,catalog);const auto negative_repeat=run(115500,research_root,catalog);require(same(negative,negative_repeat),"fresh progression is nondeterministic for seed 115500");require(!negative.reached_route&&negative.terminal=="research archived before target: warp_metric_theory"&&negative.day==3642.,"seed 115500 no longer reproduces the canonical warp-metric dead end");require(negative.warp_maturity==ResearchMaturity::archived&&negative.physical_yard_complete&&negative.fleet_count==0&&negative.interstellar_designs_locked,"archived warp hypothesis did not preserve the expected construction and ship lock boundary");require(std::ranges::find(negative.trace,"3170:research-target orbital_shipyard")!=negative.trace.end(),"ordinary orbital-industry research did not precede the dead end");const auto positive=run(115501,research_root,catalog);const auto positive_repeat=run(115501,research_root,catalog);require(same(positive,positive_repeat),"fresh progression is nondeterministic for seed 115501");require(positive.reached_route&&positive.terminal=="reached canonical scout route"&&positive.day==6575.,"seed 115501 no longer reproduces the complete ordinary progression route");require(positive.warp_maturity==ResearchMaturity::mature&&positive.physical_yard_complete&&positive.fleet_count==2&&!positive.interstellar_designs_locked,"positive route did not preserve expected research, yard, fleet and design state");require(std::ranges::find(positive.trace,"5580:construction-complete warp_test_facility")!=positive.trace.end(),"specialist facility did not precede prototype research");print(115500,negative);print(115501,positive);return 0;}catch(const std::exception&e){std::cerr<<"fresh progression failed: "<<e.what()<<'\n';return 1;}

