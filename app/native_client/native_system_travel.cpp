#include "native_system_travel.hpp"

#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/knowledge.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace stellar::native_system_travel {
namespace {
using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_system;

bool overlap(UiRect a,UiRect b,float margin=0)noexcept{return a.x-margin<b.x+b.width&&a.x+a.width+margin>b.x&&a.y-margin<b.y+b.height&&a.y+a.height+margin>b.y;}
float side(Point p1,Point p2,Point p3)noexcept{return (p1.x-p3.x)*(p2.y-p3.y)-(p2.x-p3.x)*(p1.y-p3.y);}
Point add(Point a,Vec2 b,float amount=1)noexcept{return {a.x+b.x*amount,a.y+b.y*amount};}
Vec2 normalized(Vec2 value)noexcept{const auto squared=value.x*value.x+value.y*value.y;if(squared<=.000001F)return {1,0};const auto length=std::sqrt(squared);return {value.x/length,value.y/length};}
float distance(Point a,Point b)noexcept{return std::hypot(a.x-b.x,a.y-b.y);}
const StellarSystem* find_system(std::span<const StellarSystem> systems,int id){const auto found=std::ranges::find(systems,id,&StellarSystem::id);return found==systems.end()?nullptr:&*found;}
} // namespace

void NativeSystemTravelController::require_owner()const{if(std::this_thread::get_id()!=owner_)throw std::logic_error("Native system travel projection must run on its owner thread.");}
bool NativeSystemTravelController::is_current_generation(std::uint64_t value)const noexcept{return generation_&&*generation_==value;}

NativeSystemTravelBuildResult NativeSystemTravelController::build(CampaignFrame&frame,const std::uint64_t campaign_generation,const NativeSystemSnapshot&system_view){
  require_owner();
  if(generation_&&campaign_generation<*generation_)throw std::invalid_argument("A stale campaign generation cannot replace the current system travel view.");
  auto&simulation=frame.runtime().world();auto&world=simulation.campaign();const auto observer=world.player_civilization_id;
  const auto player=std::ranges::find(world.civilizations,observer,&Civilization::id);
  if(player==world.civilizations.end()||!player->is_player)return {std::nullopt,"The campaign has no valid player observer."};
  if(system_view.campaign_generation!=campaign_generation||system_view.observer_civilization_id!=observer)return {std::nullopt,"The system view belongs to a different campaign observer."};
  if(!world.knowledge.is_system_known(observer,system_view.system_id)||world.knowledge.system_survey_level(observer,system_view.system_id)<SystemSurveyLevel::partially_surveyed)return {std::nullopt,"Reconnaissance-grade knowledge is required for local travel presentation."};
  const auto*current=find_system(world.systems,system_view.system_id);if(!current)return {std::nullopt,"The known system is unavailable in this campaign."};

  NativeSystemTravelSnapshot result{.campaign_generation=campaign_generation,.observer_civilization_id=observer,.system_id=system_view.system_id};
  std::vector<const FleetState*> local;
  for(const auto&fleet:world.fleets)if(fleet.is_active&&fleet.civilization_id==observer&&fleet.current_system_id==system_view.system_id&&fleet.transit_phase!=FleetTransitPhase::InterstellarWarp)local.push_back(&fleet);
  std::ranges::sort(local,{},[](const FleetState*fleet){return fleet->id;});if(local.size()>64)local.resize(64);result.fleets.reserve(local.size());
  for(const auto*fleet:local)result.fleets.push_back({fleet->id,fleet->name,fleet->role,fleet->design_id,fleet->local_transit_position,fleet->local_transit_target,fleet->transit_phase,fleet->transit_phase==FleetTransitPhase::LocalDeparture||fleet->transit_phase==FleetTransitPhase::LocalArrival,fleet->hold_requested,fleet->mission_order_revision});

  std::map<int,NativeLocalLaneMarker> lanes;
  for(const auto&lane:simulation.lanes().build()){
    if(lane.first_system_id==lane.second_system_id||!lane.connects(system_view.system_id))continue;
    const auto destination_id=lane.other(system_view.system_id);if(lanes.contains(destination_id))continue;
    const auto*destination=find_system(world.systems,destination_id);if(!destination)continue;
    const Vec2 current_position{current->position.x,current->position.y},destination_position{destination->position.x,destination->position.y};
    const bool known=world.knowledge.system_survey_level(observer,destination_id)>=SystemSurveyLevel::partially_surveyed;
    lanes.emplace(destination_id,NativeLocalLaneMarker{destination_id,known?std::optional<std::string>{destination->name}:std::nullopt,{destination_position.x-current_position.x,destination_position.y-current_position.y},fleet_gate_towards(destination_position,current_position),known?std::optional<double>{lane.length_light_years}:std::nullopt});
  }
  result.lanes.reserve(lanes.size());for(auto&[id,lane]:lanes){(void)id;result.lanes.push_back(std::move(lane));}
  generation_=campaign_generation;return {std::move(result),{}};
}

Point local_fleet_anchor(const NativeLocalFleetMarker&fleet,const SystemSpatialSnapshot&system,const SystemSpatialViewport&viewport)noexcept{const auto factor=system.design_radius*local_chart_render_radius_factor*viewport.scale;return {viewport.center_x+fleet.chart_position.x*factor,viewport.center_y+fleet.chart_position.y*factor};}

std::vector<int> hit_local_fleets(std::span<const NativeLocalFleetMarker>fleets,const SystemSpatialSnapshot&system,const SystemSpatialViewport&viewport,Point point,const float hit_radius){std::vector<int> result;for(const auto&fleet:fleets)if(distance(local_fleet_anchor(fleet,system,viewport),point)<=hit_radius)result.push_back(fleet.fleet_id);std::ranges::sort(result);result.erase(std::unique(result.begin(),result.end()),result.end());return result;}

float local_orbital_boundary_radius(const SystemSpatialSnapshot&system,const SystemSpatialViewport&viewport)noexcept{auto extent=system.design_radius*viewport.scale;for(const auto&body:system.bodies)extent=std::max(extent,std::hypot(body.offset_x,body.offset_y)*viewport.scale+viewport.body_radius(body));return extent+std::max(36.F,extent*.15F);}

std::vector<NativeLocalLaneGeometry> layout_local_lanes(const SystemSpatialSnapshot&system,const SystemSpatialViewport&viewport,std::span<const NativeLocalLaneMarker>lanes,std::span<const NativeLaneLabelMetrics>metrics){
  std::map<int,NativeLaneLabelMetrics> measured;for(const auto&item:metrics){if(!std::isfinite(item.width)||!std::isfinite(item.height)||item.width<0||item.height<0)throw std::invalid_argument("Lane label metrics must be finite and non-negative.");if(!measured.emplace(item.destination_system_id,item).second)throw std::invalid_argument("Lane label metrics contain a duplicate destination.");}
  std::vector<const NativeLocalLaneMarker*> ordered;ordered.reserve(lanes.size());for(const auto&lane:lanes)ordered.push_back(&lane);std::ranges::sort(ordered,{},[](const auto*lane){return lane->destination_system_id;});
  const Point origin{viewport.center_x,viewport.center_y};const auto factor=system.design_radius*local_chart_render_radius_factor*viewport.scale;const auto boundary=local_orbital_boundary_radius(system,viewport);std::vector<NativeLocalLaneGeometry> result;result.reserve(ordered.size());
  for(const auto*lane:ordered){const auto found=measured.find(lane->destination_system_id);if(found==measured.end())throw std::invalid_argument("Lane label metrics are missing a destination.");const auto direction=normalized(lane->direction);const Point transit{origin.x+lane->transit_gate.x*factor,origin.y+lane->transit_gate.y*factor};const auto gate_distance=std::max(distance(origin,transit),boundary+12.F);float stagger{};NativeLocalLaneGeometry placed;
    for(std::size_t attempt=0;attempt<=result.size();++attempt){const auto half_width=found->second.width*.5F,half_height=found->second.height*.5F,label_half_diagonal=std::hypot(half_width,half_height);const auto label_center=add(origin,direction,gate_distance+label_half_diagonal+stagger);const UiRect label_bounds{label_center.x-half_width,label_center.y-half_height,found->second.width,found->second.height};const auto visual=add(origin,direction,gate_distance+2.F*label_half_diagonal+8.F+stagger);const Vec2 normal{-direction.y,direction.x};const auto base_a=add(visual,normal,20),base_b=add(visual,normal,-20),apex=add(visual,direction,34);const std::array<Point,3> points{base_a,base_b,apex};float min_x=std::min(label_bounds.x,points[0].x),max_x=std::max(label_bounds.x+label_bounds.width,points[0].x),min_y=std::min(label_bounds.y,points[0].y),max_y=std::max(label_bounds.y+label_bounds.height,points[0].y);for(const auto point:points){min_x=std::min(min_x,point.x);max_x=std::max(max_x,point.x);min_y=std::min(min_y,point.y);max_y=std::max(max_y,point.y);}placed={lane->destination_system_id,transit,add(visual,direction,12),base_a,base_b,apex,label_center,{min_x,min_y,max_x-min_x,max_y-min_y},label_bounds,0.F};std::vector<const NativeLocalLaneGeometry*> blockers;for(const auto&prior:result)if(overlap(placed.bounds,prior.bounds,6))blockers.push_back(&prior);if(blockers.empty())break;float step=12.F+std::hypot(placed.bounds.width,placed.bounds.height);for(const auto*blocker:blockers)step=std::max(step,12.F+std::hypot(blocker->bounds.width,blocker->bounds.height)+std::hypot(placed.bounds.width,placed.bounds.height));stagger+=step;}
    result.push_back(placed);
  }return result;
}

bool hit_local_lane(const NativeLocalLaneGeometry&lane,Point point)noexcept{if(!lane.bounds.contains(point))return false;const auto a=side(point,lane.base_a,lane.base_b),b=side(point,lane.base_b,lane.apex),c=side(point,lane.apex,lane.base_a);return !((a<0||b<0||c<0)&&(a>0||b>0||c>0));}
bool local_lane_visible(const NativeLocalLaneGeometry&lane,UiRect viewport,const float margin)noexcept{return overlap(lane.bounds,viewport,margin);}

} // namespace stellar::native_system_travel
