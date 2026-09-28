#include "native_system_travel.hpp"
#include "native_system_view.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <set>
#include <stdexcept>

using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_travel;
namespace fs=std::filesystem;
namespace {
void require(bool value,const std::string&message){if(!value)throw std::runtime_error(message);}
CampaignFrame make_frame(const fs::path&root,const fs::path&catalog){return {IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),seed_persistable_fresh_campaign(123500,load_nearby_catalog(catalog),{"2044-05-06T07:08:09Z",500,6,1,"terran_baseline"})),StrategicClock{},CampaignFramePolicy::Player};}
bool overlaps(UiRect a,UiRect b){return a.x<b.x+b.width&&a.x+a.width>b.x&&a.y<b.y+b.height&&a.y+a.height>b.y;}
float radius(Point point,Point origin){return std::hypot(point.x-origin.x,point.y-origin.y);}
void require_lane_geometry(std::span<const NativeLocalLaneGeometry> geometry,Point origin,float boundary){for(const auto&lane:geometry){const auto dx=lane.apex.x-(lane.base_a.x+lane.base_b.x)*.5f,dy=lane.apex.y-(lane.base_a.y+lane.base_b.y)*.5f;require(std::abs(std::cos(lane.label_rotation_radians)*dx+std::sin(lane.label_rotation_radians)*dy)<.001f,"lane name is not across the wide arrow base");require(std::cos(lane.label_rotation_radians)>-.0001f,"lane label is upside down");require(std::hypot(lane.base_a.x-lane.base_b.x,lane.base_a.y-lane.base_b.y)>47.9f,"arrow was not enlarged");const std::array corners{Point{lane.label_bounds.x,lane.label_bounds.y},Point{lane.label_bounds.x+lane.label_bounds.width,lane.label_bounds.y},Point{lane.label_bounds.x,lane.label_bounds.y+lane.label_bounds.height},Point{lane.label_bounds.x+lane.label_bounds.width,lane.label_bounds.y+lane.label_bounds.height}};for(const auto corner:corners)require(radius(corner,origin)>boundary,"horizontal label intersects the orbit boundary");for(const auto point:{lane.base_a,lane.base_b,lane.apex})require(radius(point,origin)>boundary,"lane arrow intersects the orbit boundary");}for(std::size_t i=0;i<geometry.size();++i)for(std::size_t j=i+1;j<geometry.size();++j)require(!overlaps(geometry[i].bounds,geometry[j].bounds),"lane arrow/label layouts overlap");}
}

int main(int argc,char**argv)try{
  require(argc==3,"Usage: native_system_travel_tests <research-root> <catalog>");
  auto campaign=make_frame(fs::absolute(argv[1]),fs::absolute(argv[2]));auto&simulation=campaign.runtime().world();auto&world=simulation.campaign();const auto player=world.player_civilization_id;
  int current_id=-1,unknown_destination=-1;
  for(const auto&lane:simulation.lanes().build()){
    const auto first_known=world.knowledge.system_survey_level(player,lane.first_system_id)>=SystemSurveyLevel::partially_surveyed;
    const auto second_known=world.knowledge.system_survey_level(player,lane.second_system_id)>=SystemSurveyLevel::partially_surveyed;
    if(!first_known&&!second_known){current_id=lane.first_system_id;unknown_destination=lane.second_system_id;break;}
  }
  require(current_id>=0,"fixture has no unknown connected lane pair");(void)world.knowledge.record_reconnaissance(player,current_id,.35);
  NativeSystemViewController view_controller;auto view=view_controller.build(campaign,1,current_id);require(view.snapshot.has_value(),"authored current system did not pass the observer-safe view gate");

  for(int index=0;index<66;++index){FleetState fleet;fleet.id=1000+index;fleet.civilization_id=player;fleet.name="Owned "+std::to_string(index);fleet.role=FleetRole::Scout;fleet.design_id="scout_probe";fleet.current_system_id=current_id;fleet.local_transit_position={index*.001F,index*.002F};fleet.local_transit_target={.82F,0};fleet.is_active=true;world.fleets.push_back(std::move(fleet));}
  auto&moving=world.fleets.back();moving.id=900;moving.transit_phase=FleetTransitPhase::LocalDeparture;moving.local_transit_position={.2F,.1F};moving.local_transit_start={0,0};moving.local_transit_target={.82F,0};moving.destination_system_id=unknown_destination;moving.planned_route_system_ids={unknown_destination};moving.hold_requested=true;moving.mission_order_revision=7;const FleetState moving_fixture=moving;
  FleetState foreign=moving_fixture;foreign.id=800;foreign.civilization_id=player+1;foreign.name="Foreign secret";world.fleets.push_back(foreign);
  FleetState inactive=moving_fixture;inactive.id=801;inactive.is_active=false;world.fleets.push_back(inactive);
  FleetState warp=moving_fixture;warp.id=802;warp.transit_phase=FleetTransitPhase::InterstellarWarp;world.fleets.push_back(warp);

  NativeSystemTravelController controller;auto built=controller.build(campaign,1,*view.snapshot);require(built.snapshot.has_value()&&built.denial.empty(),"local travel projection failed");const auto&travel=*built.snapshot;
  require(travel.fleets.size()==64&&travel.fleets.front().fleet_id==900,"owned local fleet ordering/cap differs from source");
  require(std::ranges::none_of(travel.fleets,[](const auto&fleet){return fleet.fleet_id==800||fleet.fleet_id==801||fleet.fleet_id==802;}),"foreign, inactive, or warp fleet leaked into local projection");
  const auto projected_moving=std::ranges::find(travel.fleets,900,&NativeLocalFleetMarker::fleet_id);require(projected_moving!=travel.fleets.end()&&projected_moving->moving&&projected_moving->held&&projected_moving->mission_order_revision==7&&projected_moving->chart_position.x==.2F,"canonical local transit fields were not copied exactly");
  const auto unknown=std::ranges::find(travel.lanes,unknown_destination,&NativeLocalLaneMarker::destination_system_id);require(unknown!=travel.lanes.end(),"actual connected unknown lane is absent");require(!unknown->known_label&&!unknown->known_length_light_years,"unknown lane leaked destination name or distance");
  const auto current=std::ranges::find(world.systems,current_id,&StellarSystem::id),destination=std::ranges::find(world.systems,unknown_destination,&StellarSystem::id);require(current!=world.systems.end()&&destination!=world.systems.end(),"lane systems missing");const auto expected_gate=fleet_gate_towards({destination->position.x,destination->position.y},{current->position.x,current->position.y});require(std::abs(unknown->transit_gate.x-expected_gate.x)<.000001F&&std::abs(unknown->transit_gate.y-expected_gate.y)<.000001F,"lane gate differs from canonical transit bearing");
  std::set<int> connected;for(const auto&lane:simulation.lanes().build())if(lane.first_system_id!=lane.second_system_id&&lane.connects(current_id))connected.insert(lane.other(current_id));require(travel.lanes.size()==connected.size()&&std::ranges::all_of(travel.lanes,[&](const auto&lane){return connected.contains(lane.destination_system_id);}),"projection admitted a nonconnected edge or omitted a canonical edge");const auto nonconnected=std::ranges::find_if(world.systems,[&](const auto&system){return system.id!=current_id&&!connected.contains(system.id);});require(nonconnected!=world.systems.end()&&std::ranges::none_of(travel.lanes,[&](const auto&lane){return lane.destination_system_id==nonconnected->id;}),"nonconnected system appeared as a local lane");
  const auto retained_name=projected_moving->name;const auto retained_position=projected_moving->chart_position;destination->name="Revealed destination";const auto live_moving=std::ranges::find(world.fleets,900,&FleetState::id);require(live_moving!=world.fleets.end(),"moving fleet disappeared");live_moving->name="Changed live fleet";live_moving->local_transit_position={.3F,.1F};require(projected_moving->name==retained_name&&projected_moving->chart_position.x==retained_position.x&&!unknown->known_label,"owned snapshot changed after live world mutation");
  (void)world.knowledge.record_reconnaissance(player,unknown_destination,.35);const auto known=controller.build(campaign,1,*view.snapshot);require(known.snapshot.has_value(),"known-lane refresh failed");const auto known_lane=std::ranges::find(known.snapshot->lanes,unknown_destination,&NativeLocalLaneMarker::destination_system_id);require(known_lane!=known.snapshot->lanes.end()&&known_lane->known_label==std::optional<std::string>{"Revealed destination"}&&known_lane->known_length_light_years&&*known_lane->known_length_light_years>0,"known destination did not expose its current safe label and distance");
  live_moving->hold_requested=false;const auto before_frame=live_moving->local_transit_position;const auto frame_result=campaign.advance(1.);require(frame_result.route==CampaignFrameRoute::Strategic,"actual campaign frame did not take the strategic route");const auto after_frame=std::ranges::find(world.fleets,900,&FleetState::id);require(after_frame!=world.fleets.end()&&(after_frame->local_transit_position.x!=before_frame.x||after_frame->local_transit_position.y!=before_frame.y||after_frame->transit_phase!=FleetTransitPhase::LocalDeparture),"actual CampaignFrame did not advance canonical local transit");const auto advanced=controller.build(campaign,1,*view.snapshot);require(advanced.snapshot.has_value(),"travel refresh after actual CampaignFrame failed");const auto advanced_marker=std::ranges::find(advanced.snapshot->fleets,900,&NativeLocalFleetMarker::fleet_id);require(advanced_marker!=advanced.snapshot->fleets.end()&&advanced_marker->chart_position.x==after_frame->local_transit_position.x&&advanced_marker->chart_position.y==after_frame->local_transit_position.y,"refreshed snapshot did not copy actual CampaignFrame transit position");
  auto wrong=*view.snapshot;wrong.observer_civilization_id=player+1;const auto denied=controller.build(campaign,1,wrong);require(!denied.snapshot&&denied.denial.find("observer")!=std::string::npos,"wrong observer was not rejected");
  bool stale{};try{(void)controller.build(campaign,0,*view.snapshot);}catch(const std::invalid_argument&){stale=true;}require(stale,"stale campaign generation was accepted");

  SystemSpatialSnapshot spatial{.system_id=current_id,.design_radius=100,.bodies={{.body_id=1,.offset_x=90,.offset_y=0,.display_radius=10}}};SystemSpatialViewport viewport{500,400,1};NativeLocalFleetMarker local{.fleet_id=7,.chart_position={.2F,-.1F}};const auto anchor=local_fleet_anchor(local,spatial,viewport);require(std::abs(anchor.x-533)<.001F&&std::abs(anchor.y-383.5F)<.001F,"fleet anchor changed the source chart factor");const std::array fleet_list{local};require(hit_local_fleets(fleet_list,spatial,viewport,anchor)==std::vector<int>{7},"fleet draw/hit geometry disagrees");
  FleetState canonical;canonical.strategic_speed=22;begin_fleet_local_transit(canonical,FleetTransitPhase::LocalDeparture,{0,0},{fleet_local_gate_radius,0});const auto initial=canonical.local_transit_position;(void)advance_fleet_local_transit(canonical,.25);require(canonical.local_transit_position.x>initial.x,"canonical local transit fixture did not advance");local.chart_position=canonical.local_transit_position;require(local_fleet_anchor(local,spatial,viewport).x>viewport.center_x,"fleet anchor did not follow canonical advanced position");
  const float diagonal=std::sqrt(.5F);const std::array lane_list{NativeLocalLaneMarker{.destination_system_id=4,.direction={1,0},.transit_gate={fleet_local_gate_radius,0}},NativeLocalLaneMarker{.destination_system_id=5,.direction={0,1},.transit_gate={0,fleet_local_gate_radius}},NativeLocalLaneMarker{.destination_system_id=6,.direction={1,1},.transit_gate={fleet_local_gate_radius*diagonal,fleet_local_gate_radius*diagonal}},NativeLocalLaneMarker{.destination_system_id=7,.direction={1,.001F},.transit_gate={fleet_local_gate_radius,0}}};const std::array metrics{NativeLaneLabelMetrics{4,184,18},NativeLaneLabelMetrics{5,176,18},NativeLaneLabelMetrics{6,220,18},NativeLaneLabelMetrics{7,196,18}};const auto geometry=layout_local_lanes(spatial,viewport,lane_list,metrics);require(geometry.size()==lane_list.size()&&std::abs(geometry[0].transit_gate.x-(500+100*local_chart_render_radius_factor*fleet_local_gate_radius))<.001F,"decorative geometry moved the canonical transit gate");const auto boundary=local_orbital_boundary_radius(spatial,viewport);require_lane_geometry(geometry,{viewport.center_x,viewport.center_y},boundary);require(hit_local_lane(geometry[0],geometry[0].center),"lane draw and hit triangles disagree");require(local_lane_visible(geometry[0],{0,0,1280,720}),"720p cardinal lane was incorrectly culled");require(!local_lane_visible(geometry[0],{-5000,-5000,100,100}),"offscreen lane was not culled");SystemSpatialViewport viewport4k{1920,1080,2};const auto geometry4k=layout_local_lanes(spatial,viewport4k,lane_list,metrics);require_lane_geometry(geometry4k,{viewport4k.center_x,viewport4k.center_y},local_orbital_boundary_radius(spatial,viewport4k));require(local_lane_visible(geometry4k[0],{0,0,3840,2160}),"4K cardinal lane was incorrectly culled");
  // Check all quadrants independently of the crowded-lane fixture.
  for(int i=0;i<16;++i){
    const float angle=i*3.14159265358979323846f/8;
    const std::array lanes{NativeLocalLaneMarker{.destination_system_id=1,.direction={std::cos(angle),std::sin(angle)},.transit_gate={fleet_local_gate_radius*std::cos(angle),fleet_local_gate_radius*std::sin(angle)}}};
    const std::array sizes{NativeLaneLabelMetrics{1,160,20}};
    const auto layout=layout_local_lanes(spatial,viewport,lanes,sizes);
    require_lane_geometry(layout,{viewport.center_x,viewport.center_y},boundary);
    const auto& g=layout.front();
    const Point base{(g.base_a.x+g.base_b.x)*.5f,(g.base_a.y+g.base_b.y)*.5f};
    require(std::abs(radius(base,g.label_center)-16.f)<.001f,"lane name is detached from the broad base");
    for(float x:{-80.f,80.f})for(float y:{-10.f,10.f}){
      const Point corner{g.label_center.x+x*std::cos(g.label_rotation_radians)-y*std::sin(g.label_rotation_radians),g.label_center.y+x*std::sin(g.label_rotation_radians)+y*std::cos(g.label_rotation_radians)};
      require(corner.x>=g.label_bounds.x-.001f&&corner.x<=g.label_bounds.x+g.label_bounds.width+.001f&&corner.y>=g.label_bounds.y-.001f&&corner.y<=g.label_bounds.y+g.label_bounds.height+.001f,"rotated text escapes collision bounds");
    }
  }
  NativeLocalLaneMarker fallback{.destination_system_id=5,.direction={},.transit_gate={fleet_local_gate_radius,0}};const std::array fallback_list{fallback};const std::array fallback_metrics{NativeLaneLabelMetrics{5,40,14}};const auto fallback_geometry=layout_local_lanes(spatial,viewport,fallback_list,fallback_metrics);require(fallback_geometry[0].apex.x>fallback_geometry[0].base_a.x,"zero-length direction did not preserve canonical +X fallback");
  std::cout<<"native system travel cases passed\n";return 0;
}catch(const std::exception&error){std::cerr<<"native system travel failed: "<<error.what()<<'\n';return 1;}
