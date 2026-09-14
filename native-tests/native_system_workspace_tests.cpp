#include "native_system_workspace.hpp"
#include "native_system_view.hpp"
#include "native_system_travel.hpp"
#include "native_fleet_controller.hpp"
#include "native_ui_layout.hpp"
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <ranges>
#include <stdexcept>

using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_ui;
using namespace stellar::native_system_travel;
using namespace stellar::native_fleet;
namespace fs=std::filesystem;
namespace {
void require(bool value,const std::string&message){if(!value)throw std::runtime_error(message);}
CampaignFrame make_frame(const fs::path&root,const fs::path&catalog){return {IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),seed_persistable_fresh_campaign(120500,load_nearby_catalog(catalog),{"2044-05-06T07:08:09Z",500,6,1,"terran_baseline"})),StrategicClock{},CampaignFramePolicy::Player};}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
bool same_camera(const SystemSpatialViewport&a,const SystemSpatialViewport&b){return std::abs(a.center_x-b.center_x)<.001f&&std::abs(a.center_y-b.center_y)<.001f&&std::abs(a.scale-b.scale)<.00001f;}
bool has_central_star(const DrawList&draw,const SystemSpatialViewport&camera,Color color){return std::ranges::any_of(draw.world,[&](const WorldCommand&item){const auto*circle=std::get_if<Circle>(&item);return circle&&std::abs(circle->center.x-camera.center_x)<.001f&&std::abs(circle->center.y-camera.center_y)<.001f&&circle->color.r==color.r&&circle->color.g==color.g&&circle->color.b==color.b&&circle->color.a==color.a;});}
}

int main(int argc,char**argv)try{
  require(argc==3,"Usage: native_system_workspace_tests <research-root> <catalog>");
  auto campaign=make_frame(fs::absolute(argv[1]),fs::absolute(argv[2]));
  NativeSystemViewController controller;auto built=controller.build(campaign,1,sol_system_id);
  require(built.snapshot.has_value(),"fresh Sol view unavailable");
  const auto reference=*built.snapshot;auto display=reference;
  const auto display_earth=std::ranges::find(display.bodies,earth_body_id,&NativeSystemBody::id);
  require(display_earth!=display.bodies.end(),"Sol view lacks Earth");
  display_earth->positive_signatures={NativePositiveSignature::rare_resource,NativePositiveSignature::anomaly,NativePositiveSignature::activity};
  const auto projected=project_system(display);
  int image_requests{},texture_requests{};
  int measurements{};NativeSystemWorkspace workspace([&](const SystemBodyAppearance&appearance){++image_requests;if(appearance.texture_key)++texture_requests;return std::shared_ptr<const RgbaImage>{};},[&](const Text&label){++measurements;return TextExtent{static_cast<int>(label.value.size()*7u),14};});
  workspace.open(std::move(display),1280,720);
  require(workspace.visible()&&workspace.system_id()==sol_system_id,"workspace did not open fresh Sol");
  DrawList draw;workspace.render(draw,1280,720);
  require(!draw.world.empty()&&image_requests>0&&texture_requests>0,"workspace did not request eligible Sol appearances");
  require(has_central_star(draw,*workspace.viewport(),{255,230,150,245}),"full-survey Sol did not use the native G-class palette");
  const auto earth=std::ranges::find(projected.bodies,earth_body_id,&SystemSpatialBodyMarker::body_id);
  require(earth!=projected.bodies.end(),"Sol projection lacks Earth");
  auto earth_screen=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
  auto command=workspace.handle({InputEventType::LeftPressed,{earth_screen.x,earth_screen.y}},1280,720);
  require(command.captured&&workspace.selected_body_id()==earth_body_id,"drawn Earth was not selected");
  (void)workspace.handle({InputEventType::LeftReleased,{earth_screen.x,earth_screen.y}},1280,720);

  draw={};workspace.render(draw,1280,720);const auto layout720=SystemWorkspaceLayout::for_viewport(1280,720);
  int clipped_inspector_text{};
  for(const auto&item:draw.overlay)if(const auto*label=std::get_if<Text>(&item);label&&label->clip&&layout720.inspector.contains(label->at)){++clipped_inspector_text;require(label->at.y<layout720.inspector.y+layout720.inspector.height,"720p inspector content escaped its panel");}
  require(clipped_inspector_text>=12,"full three-signature inspector was not rendered compactly and clipped");
  const auto protected_camera=*workspace.viewport();const auto protected_selection=workspace.selected_body_id();const auto panel_point=center(layout720.inspector);
  (void)workspace.handle({InputEventType::LeftPressed,panel_point},1280,720);
  (void)workspace.handle({InputEventType::PointerMove,{panel_point.x+24,panel_point.y+16},{24,16}},1280,720);
  (void)workspace.handle({InputEventType::Wheel,panel_point,{},3},1280,720);
  (void)workspace.handle({InputEventType::LeftReleased,panel_point},1280,720);
  require(same_camera(protected_camera,*workspace.viewport())&&workspace.selected_body_id()==protected_selection,"inspector gestures changed camera or selected a covered body");

  const Point anchor{500,350};const auto before=workspace.viewport()->screen_to_world(anchor.x,anchor.y);
  (void)workspace.handle({InputEventType::Wheel,anchor,{},2},1280,720);const auto after=workspace.viewport()->screen_to_world(anchor.x,anchor.y);
  require(std::abs(before.x-after.x)<.01&&std::abs(before.y-after.y)<.01,"workspace zoom lost its pointer anchor");
  const auto prior_center_x=workspace.viewport()->center_x,prior_scale=workspace.viewport()->scale;
  (void)workspace.handle({InputEventType::LeftPressed,{20,300}},1280,720);(void)workspace.handle({InputEventType::PointerMove,{57,323},{37,23}},1280,720);(void)workspace.handle({InputEventType::LeftReleased,{57,323}},1280,720);
  require(std::abs(workspace.viewport()->center_x-prior_center_x-37)<.01,"workspace drag did not pan");
  draw={};workspace.render(draw,1920,1080);
  require(workspace.viewport()->scale==prior_scale&&std::abs(workspace.viewport()->center_x-prior_center_x-37)>.01,"resize discarded or failed to relocate the existing camera");
  const auto system_layout=SystemWorkspaceLayout::for_viewport(1920,1080);const auto global_layout=NativeUiLayout::for_viewport(1920,1080);
  require(global_layout.hit(center(system_layout.back),false)==UiAction::None&&global_layout.hit(center(system_layout.reset),false)==UiAction::None,"system controls overlap the actual main toolbar routing");
  (void)workspace.handle({InputEventType::LeftPressed,center(system_layout.reset)},1920,1080);
  require(std::abs(workspace.viewport()->center_x-(system_layout.world_field.x+system_layout.world_field.width*.5f))<.001f&&std::abs(workspace.viewport()->center_y-(system_layout.world_field.y+system_layout.world_field.height*.5f))<.001f&&workspace.viewport()->scale>0,"reset fit did not restore the system field camera");
  command=workspace.handle({InputEventType::LeftPressed,center(system_layout.back)},1920,1080);
  require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Back did not route to the system workspace");
  workspace.discard_campaign();require(!workspace.visible()&&!workspace.selected_body_id()&&!workspace.campaign_generation(),"campaign replacement retained system state or image eligibility");

  auto recon=reference;recon.campaign_generation=2;recon.survey_level=SystemSurveyLevel::partially_surveyed;recon.archetype.reset();recon.primary_stellar_class.reset();
  for(auto&body:recon.bodies){body.details.reset();body.sol_texture_key.reset();body.visual_class=body.kind==PlanetaryBodyKind::Moon?NativeSystemBodyVisualClass::unknown_moon:NativeSystemBodyVisualClass::unknown_planet;}
  image_requests=texture_requests=0;workspace.open(std::move(recon),1280,720);draw={};workspace.render(draw,1280,720);
  require(image_requests>0&&texture_requests==0,"reconnaissance workspace requested hidden texture eligibility");
  require(has_central_star(draw,*workspace.viewport(),{135,150,174,235}),"reconnaissance star exposed a known spectral palette");
  command=workspace.handle({InputEventType::EscapePressed},1280,720);require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Escape did not request galaxy return");
  workspace.close();require(!workspace.visible(),"workspace close retained the system");

  auto&simulation=campaign.runtime().world();auto&world=simulation.campaign();const auto player=world.player_civilization_id;std::map<int,std::vector<int>> connected;for(const auto&lane:simulation.lanes().build()){connected[lane.first_system_id].push_back(lane.second_system_id);connected[lane.second_system_id].push_back(lane.first_system_id);}int local_system=-1;std::vector<int> destinations;for(const auto&[candidate,neighbors]:connected)if(neighbors.size()>destinations.size()&&world.knowledge.system_survey_level(player,candidate)<SystemSurveyLevel::partially_surveyed){local_system=candidate;destinations=neighbors;}require(local_system>=0&&destinations.size()>=2,"actual lane graph lacks a useful observer-hidden local system fixture");world.knowledge.record_reconnaissance(player,local_system,.35);world.knowledge.record_reconnaissance(player,destinations.front(),.35);const auto known_destination=destinations.front();const auto unknown_found=std::ranges::find_if(destinations,[&](int id){return id!=known_destination&&world.knowledge.system_survey_level(player,id)<SystemSurveyLevel::partially_surveyed;});require(unknown_found!=destinations.end(),"actual lane graph lacks an unknown connected destination");const auto unknown_destination=*unknown_found;
  FleetState local_fleet;local_fleet.id=99001;local_fleet.civilization_id=player;local_fleet.name="ISS Green Horizon";local_fleet.role=FleetRole::Scout;local_fleet.design_id="scout_probe";local_fleet.current_system_id=local_system;local_fleet.local_transit_position={.10f,.08f};local_fleet.local_transit_target={.82f,.18f};local_fleet.transit_phase=FleetTransitPhase::LocalDeparture;local_fleet.mission_order_revision=7;local_fleet.is_active=true;world.fleets.push_back(local_fleet);auto held_fleet=local_fleet;held_fleet.id=99002;held_fleet.name="ISS Patient Horizon";held_fleet.hold_requested=true;world.fleets.push_back(held_fleet);const auto original_revision=local_fleet.mission_order_revision;const auto original_route=local_fleet.planned_route_system_ids;
  auto local_view=controller.build(campaign,3,local_system);require(local_view.snapshot.has_value(),"observer-safe local system view was denied");NativeSystemTravelController travel_controller;auto travel=travel_controller.build(campaign,3,*local_view.snapshot);require(travel.snapshot.has_value(),"actual local fleet/lane projection was denied");const auto known_lane=std::ranges::find(travel.snapshot->lanes,known_destination,&NativeLocalLaneMarker::destination_system_id),unknown_lane=std::ranges::find(travel.snapshot->lanes,unknown_destination,&NativeLocalLaneMarker::destination_system_id);require(known_lane!=travel.snapshot->lanes.end()&&known_lane->known_label&&known_lane->known_length_light_years,"known connected lane omitted observer-safe label or length");require(unknown_lane!=travel.snapshot->lanes.end()&&!unknown_lane->known_label&&!unknown_lane->known_length_light_years,"unknown connected lane leaked a label or length");
  workspace.open(std::move(*local_view.snapshot),1280,720);workspace.refresh_travel(std::move(*travel.snapshot),std::nullopt);require(measurements==static_cast<int>(workspace.travel_snapshot()->lanes.size()),"lane labels were not measured exactly once when the snapshot refreshed");const auto fleet=std::ranges::find(workspace.travel_snapshot()->fleets,99001,&NativeLocalFleetMarker::fleet_id);require(fleet!=workspace.travel_snapshot()->fleets.end(),"owned local fleet was not projected");const auto local_spatial=project_system(*workspace.snapshot());const auto fleet_point=local_fleet_anchor(*fleet,local_spatial,*workspace.viewport());
  command=workspace.handle({InputEventType::LeftPressed,fleet_point},1280,720);require(command.kind==SystemWorkspaceCommandKind::select_fleet&&command.target_id==99001&&command.captured&&command.hit_fleet_ids==std::vector<int>({99001,99002}),"owned co-located fleet click did not preserve the sorted hit group");NativeFleetController fleet_controller;(void)fleet_controller.build(campaign,3);const auto selected=fleet_controller.select_next_hit(campaign,3,command.hit_fleet_ids);require(selected.accepted&&fleet_controller.selection()==std::optional<int>{99001},"first local fleet click was not accepted by the existing fleet controller");const auto cycled=fleet_controller.select_next_hit(campaign,3,command.hit_fleet_ids);require(cycled.accepted&&fleet_controller.selection()==std::optional<int>{99002},"co-located local fleet click did not cycle selection");for(const auto id:{99001,99002}){const auto current=std::ranges::find(world.fleets,id,&FleetState::id);require(current!=world.fleets.end()&&current->mission_order_revision==original_revision&&current->planned_route_system_ids==original_route,"local fleet selection mutated a canonical order");}
  draw={};workspace.render(draw,1280,720);require(std::ranges::any_of(draw.world,[](const WorldCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value=="SC";}),"owned scout role icon was not visible");const auto thrusters=std::ranges::count_if(draw.world,[](const WorldCommand&item){const auto*line=std::get_if<Line>(&item);return line&&line->color.r==92&&line->color.g==225&&line->color.b==154&&line->color.a==170;});require(thrusters==2,"moving fleet thrusters were missing or rendered for a held fleet");
  const auto layout=SystemWorkspaceLayout::for_viewport(1280,720);const auto geometry=workspace.lane_geometry();const auto known_geometry=std::ranges::find(geometry,known_destination,&NativeLocalLaneGeometry::destination_system_id),unknown_geometry=std::ranges::find(geometry,unknown_destination,&NativeLocalLaneGeometry::destination_system_id);require(known_geometry!=geometry.end()&&unknown_geometry!=geometry.end(),"connected lanes lacked stable arrow geometry");require(layout.world_field.contains(known_geometry->center)&&layout.world_field.contains(unknown_geometry->center),"actual lane fixture arrows were not reachable at 720p");const auto protected_command=workspace.handle({InputEventType::LeftPressed,center(layout.inspector)},1280,720);require(protected_command.kind==SystemWorkspaceCommandKind::none&&protected_command.captured,"inspector click reached local travel geometry");
  command=workspace.handle({InputEventType::LeftPressed,unknown_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::none&&command.captured&&workspace.notice().find("Telemetry unavailable")!=std::string::npos,"unknown lane click did not give observer-safe scout guidance");require(workspace.notice().find(std::to_string(unknown_destination))==std::string::npos,"unknown lane notice disclosed its opaque destination identity");command=workspace.handle({InputEventType::LeftPressed,known_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::open_destination&&command.target_id==known_destination&&command.captured,"known lane click did not request observer-gated navigation");const auto destination_view=controller.build(campaign,3,command.target_id);require(destination_view.snapshot.has_value(),"known lane navigation failed the observer gate");for(const auto id:{99001,99002}){const auto current=std::ranges::find(world.fleets,id,&FleetState::id);require(current!=world.fleets.end()&&current->mission_order_revision==original_revision&&current->planned_route_system_ids==original_route,"lane navigation issued or changed a fleet order");}draw={};workspace.render(draw,1920,1080);require(!workspace.lane_geometry().empty(),"1080p resize discarded local lanes");workspace.discard_campaign();require(!workspace.travel_snapshot()&&!workspace.selected_fleet_id()&&workspace.notice().empty(),"generation discard retained local travel presentation");
  std::cout<<"native system workspace cases passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"native system workspace failed: "<<e.what()<<'\n';return 1;}
