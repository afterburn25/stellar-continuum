#include "native_campaign_calendar.hpp"
#include "native_system_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_planet_rings.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <numbers>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_system_ui {
namespace {
using namespace stellar::native_map;using namespace stellar::native_system;using namespace stellar::native_system_travel;using namespace stellar::core;
constexpr Color orbit{53,91,126,125},text{220,234,248,245},muted{143,173,203,230},border{74,128,176,235};
void overlay_fill(DrawList&o,UiRect b,Color c){o.overlay.emplace_back(FilledRectangle{b,c});}
void overlay_stroke(DrawList&o,UiRect b,Color c){o.overlay.emplace_back(StrokedRectangle{b,c});}
void overlay_text(DrawList&o,float x,float y,std::string value,Color c,int size=15,float wrap=0,std::optional<UiRect> clip=std::nullopt){o.overlay.emplace_back(Text{{x,y},std::move(value),c,size,wrap,clip});}
std::string number(double value,int precision=2){std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();}
// Compact kilometres: below a million print the integer; beyond it use the
// body-inspection scientific convention so a wide value never orphans its
// unit onto a second wrapped line in the inspector panel.
std::string compact_km(double kilometres){
  if(kilometres<1'000'000.)return number(kilometres,0);
  const auto exponent=static_cast<int>(std::floor(std::log10(kilometres)));
  constexpr std::string_view sup[]={"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
  std::string superscript;for(const char digit:std::to_string(exponent))superscript+=sup[digit-'0'];
  return number(kilometres/std::pow(10.,exponent),3)+" × 10"+superscript;
}
std::string translate(const stellar::engine::LocalizationTable *locale,
                      std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}
std::string environmental_hazard(EnvironmentalLimitingFactor value,
                                 const stellar::engine::LocalizationTable *locale){
  switch(value){
  case EnvironmentalLimitingFactor::Gravity:return translate(locale,"SYSTEM_FACTOR_GRAVITY","Gravity");
  case EnvironmentalLimitingFactor::Temperature:return translate(locale,"SYSTEM_FACTOR_TEMPERATURE","Temperature");
  case EnvironmentalLimitingFactor::Pressure:return translate(locale,"SYSTEM_FACTOR_PRESSURE","Pressure");
  case EnvironmentalLimitingFactor::Atmosphere:return translate(locale,"SYSTEM_FACTOR_ATMOSPHERE","Atmosphere");
  case EnvironmentalLimitingFactor::Solvent:return translate(locale,"SYSTEM_FACTOR_SOLVENT","Biological solvent");
  case EnvironmentalLimitingFactor::Immersion:return translate(locale,"SYSTEM_FACTOR_IMMERSION","Immersion");
  case EnvironmentalLimitingFactor::Radiation:return translate(locale,"SYSTEM_FACTOR_RADIATION","Radiation");
  case EnvironmentalLimitingFactor::None:return translate(locale,"SYSTEM_FACTOR_NONE","None");
  }return translate(locale,"SYSTEM_UNCONFIRMED","Unconfirmed");
}
Color visual_color(NativeSystemBodyVisualClass value){switch(value){case NativeSystemBodyVisualClass::rocky:return {178,143,115,255};case NativeSystemBodyVisualClass::oceanic:return {70,152,213,255};case NativeSystemBodyVisualClass::frozen:return {171,217,235,255};case NativeSystemBodyVisualClass::hot_rocky:return {222,116,66,255};case NativeSystemBodyVisualClass::gas_giant:return {211,167,114,255};case NativeSystemBodyVisualClass::ice_giant:return {111,190,215,255};case NativeSystemBodyVisualClass::moon:return {180,184,190,255};case NativeSystemBodyVisualClass::unknown_moon:return {112,137,160,255};default:return {94,132,166,255};}}
Color star_color(std::optional<StellarClass> value){if(!value)return {135,150,174,235};switch(*value){case StellarClass::MRedDwarf:return {255,119,76,230};case StellarClass::KOrangeDwarf:return {255,167,92,235};case StellarClass::GYellowDwarf:return {255,230,150,245};case StellarClass::FYellowWhiteDwarf:return {255,247,215,245};case StellarClass::AWhiteStar:return {225,236,255,245};case StellarClass::HotBlueStar:return {126,174,255,245};case StellarClass::Giant:return {255,142,88,245};case StellarClass::WhiteDwarf:return {221,236,255,235};case StellarClass::NeutronStar:return {133,218,255,250};case StellarClass::BlackHole:return {126,92,178,230};case StellarClass::Protostar:return {255,112,160,230};case StellarClass::Pulsar:return {91,229,255,250};}return {135,150,174,235};}
std::uint32_t mix(std::uint32_t value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);}
std::optional<std::pair<Point,Point>> clipped(Point a,Point b,UiRect r){float t0=0,t1=1;const float dx=b.x-a.x,dy=b.y-a.y;const std::array p{-dx,dx,-dy,dy};const std::array q{a.x-r.x,r.x+r.width-a.x,a.y-r.y,r.y+r.height-a.y};for(std::size_t i=0;i<p.size();++i){if(p[i]==0){if(q[i]<0)return std::nullopt;continue;}const auto t=q[i]/p[i];if(p[i]<0)t0=std::max(t0,t);else t1=std::min(t1,t);if(t0>t1)return std::nullopt;}return std::pair{Point{a.x+t0*dx,a.y+t0*dy},Point{a.x+t1*dx,a.y+t1*dy}};}
bool intersects(UiRect r,float x,float y,float radius){return x+radius>=r.x&&x-radius<=r.x+r.width&&y+radius>=r.y&&y-radius<=r.y+r.height;}
bool contains_disc(UiRect r,Point center,float radius){return center.x-radius>=r.x&&center.x+radius<=r.x+r.width&&center.y-radius>=r.y&&center.y+radius<=r.y+r.height;}
bool contains_rect(UiRect outer,UiRect inner){return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.width<=outer.x+outer.width&&inner.y+inner.height<=outer.y+outer.height;}
bool overlaps(UiRect left,UiRect right,float padding=0.f){return left.x<right.x+right.width+padding&&left.x+left.width+padding>right.x&&left.y<right.y+right.height+padding&&left.y+left.height+padding>right.y;}
std::string fleet_role(FleetRole role){switch(role){case FleetRole::Scout:return "SC";case FleetRole::Science:return "RS";case FleetRole::Colony:return "CO";case FleetRole::Military:return "MI";case FleetRole::Logistics:return "LG";}return "FL";}
struct BodyLabelCandidate {int body_id{};bool selected{};std::string value;std::array<UiRect,4> placements;};
std::string lane_label(const NativeLocalLaneMarker&lane){return lane.known_label.value_or("????");}
SystemSpatialViewport workspace_fit(const SystemSpatialSnapshot&spatial,int width,int height,const SystemTextMeasurer&measure,const std::optional<NativeSystemTravelSnapshot>&travel,const std::vector<NativeLaneLabelMetrics>&lane_metrics){
  const auto layout=SystemWorkspaceLayout::for_viewport(width,height);const auto&world=layout.world_field;const auto inset=std::min(12.f,std::min(world.width,world.height)*.05f);const UiRect field{world.x+inset,world.y+inset,world.width-2.f*inset,world.height-2.f*inset};const auto center_x=field.x+field.width*.5f,center_y=field.y+field.height*.5f;
  const auto text_extent=[&](const std::string&value){const Text label{{},value,text,15};const auto measured=measure?measure(label):TextExtent{static_cast<int>(value.size()*7u),19};if(measured.width<0||measured.height<0)throw std::runtime_error("Renderer returned invalid system label bounds.");return measured;};
  std::map<int,TextExtent> label_extents;for(const auto&body:spatial.bodies)label_extents.emplace(body.body_id,text_extent(body.label));
  const auto fit_center=[&](float scale,bool with_lanes)->std::optional<Point>{
    const SystemSpatialViewport view{center_x,center_y,scale};float minimum_x=field.x,maximum_x=field.x+field.width,minimum_y=field.y,maximum_y=field.y+field.height;
    const auto reserve=[&](UiRect bounds){const auto left=bounds.x-center_x,right=left+bounds.width,top=bounds.y-center_y,bottom=top+bounds.height;minimum_x=std::max(minimum_x,field.x-left);maximum_x=std::min(maximum_x,field.x+field.width-right);minimum_y=std::max(minimum_y,field.y-top);maximum_y=std::min(maximum_y,field.y+field.height-bottom);};
    const auto boundary=std::max(local_orbital_boundary_radius(spatial,view),star_screen_radius(scale)*1.75f);reserve({center_x-boundary,center_y-boundary,2.f*boundary,2.f*boundary});
    for(const auto&body:spatial.bodies){if(!view.is_body_visible(spatial,body))continue;const auto point=view.world_to_screen(body.offset_x,body.offset_y);const auto radius=view.body_radius(body)*planet_ring_extent(body.sol_texture_key.value_or(""))+4.f;reserve({point.x-radius,point.y-radius,2.f*radius,2.f*radius});const auto measured=label_extents.at(body.body_id);reserve({point.x-static_cast<float>(measured.width)*.5f,point.y+radius+5.f,static_cast<float>(measured.width),static_cast<float>(measured.height)});}
    if(travel&&with_lanes)for(const auto&geometry:layout_local_lanes(spatial,view,{},travel->lanes,lane_metrics))reserve(geometry.bounds);
    if(minimum_x>maximum_x||minimum_y>maximum_y)return std::nullopt;return Point{std::clamp(center_x,minimum_x,maximum_x),std::clamp(center_y,minimum_y,maximum_y)};
  };
  const auto initial_low=std::min(.001f,.0001f*std::min(field.width,field.height)/std::max(1.f,spatial.design_radius));
  const auto search=[&](bool with_lanes){float low=initial_low,high=1.15f;for(int iteration=0;iteration<24;++iteration){const auto candidate=(low+high)*.5f;if(fit_center(candidate,with_lanes))low=candidate;else high=candidate;}return std::pair{low,fit_center(low,with_lanes).value_or(Point{center_x,center_y})};};
  // Lane arrows use fixed pixel offsets that a compact field cannot contain at
  // any zoom; fit the chart alone in that case and let the field clamp pull
  // the arrows inside world_field where input can still reach them.
  auto fitted=search(true);if(!fit_center(fitted.first,true))fitted=search(false);
  return {fitted.second.x,fitted.second.y,fitted.first};
}
double presentation_seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
} // namespace

SystemWorkspaceLayout SystemWorkspaceLayout::for_viewport(int width,int height) noexcept {
  const auto w=static_cast<float>(std::max(width,1));
  const auto h=static_cast<float>(std::max(height,1));
  const auto s=NativeUiLayout::for_viewport(width,height).scale;
  const auto panel_width=254.f*s;
  const auto left=native_navigation_content_left*s;
  const auto top=native_workspace_top(width,height);
  const UiRect controls{left,top,w-panel_width-left-28.f*s,36.f*s};
  const UiRect panel{left+8*s,top+58.f*s,panel_width,std::max(1.f,std::min(540.f*s,h-top-144.f*s))};
  return {controls,{left+6*s,top+4*s,78*s,28*s},{left+90*s,top+4*s,98*s,28*s},panel,
          {panel.x+10*s,panel.y+panel.height-44*s,panel.width-20*s,32*s},
          {left+panel_width+24*s,top+44*s,std::max(1.f,w-left-panel_width-384*s),std::max(1.f,h-top-116*s)},
          {panel.x+10*s,panel.y+panel.height-84*s,panel.width-20*s,32*s}};
}

std::string NativeSystemWorkspace::tr(std::string_view key,
                                      std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}
std::string NativeSystemWorkspace::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale_->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

NativeSystemWorkspace::NativeSystemWorkspace(SystemImageProvider provider,SystemTextMeasurer measurer):image_provider_(std::move(provider)),text_measurer_(std::move(measurer)){body_inspection_.set_text_measurer(text_measurer_);}
void NativeSystemWorkspace::use_background_preparation(std::shared_ptr<ImagePreparationQueue> queue){celestial_appearance_.use_background_preparation(std::move(queue));}
void NativeSystemWorkspace::open(NativeSystemSnapshot snapshot,int width,int height){tracked_body_id_.reset();small_body_focus_=false;small_bodies_.clear();small_body_panel_=false;small_body_debug_=false;small_body_field_=0;small_body_index_=0;celestial_appearance_.cancel_preparation();artwork_ready_=false;clear_travel();snapshot_=std::move(snapshot);spatial_=project_system(*snapshot_);body_inspection_.clear();shipyard_owner_.reset();shipyard_selected_=false;shipyard_pressed_=false;preparation_.reset();preparation_pressed_=false;selected_body_id_.reset();colony_body_id_.reset();inspector_focus_=InspectorFocus::automatic;dragging_=false;pending_initial_travel_fit_=true;width_=width;height_=height;viewport_=workspace_fit(*spatial_,width,height,text_measurer_,std::nullopt,{});zoom_reference_scale_=viewport_->scale;}
void NativeSystemWorkspace::track_body(int body_id){
  if(!spatial_||!viewport_)return;
  const auto body=std::ranges::find(spatial_->bodies,body_id,&SystemSpatialBodyMarker::body_id);if(body==spatial_->bodies.end())return;
  const auto field=SystemWorkspaceLayout::for_viewport(width_,height_).world_field;
  const auto point=viewport_->world_to_screen(body->offset_x,body->offset_y);
  tracking_anchor_={(point.x-field.x)/field.width,(point.y-field.y)/field.height};tracked_body_id_=body_id;
}
void NativeSystemWorkspace::update_camera_tracking(){
  if(!tracked_body_id_||!spatial_||!viewport_)return;
  const auto body=std::ranges::find(spatial_->bodies,*tracked_body_id_,&SystemSpatialBodyMarker::body_id);
  if(body==spatial_->bodies.end()){tracked_body_id_.reset();return;}
  const auto field=SystemWorkspaceLayout::for_viewport(width_,height_).world_field;
  viewport_->center_x=field.x+tracking_anchor_.x*field.width-body->offset_x*viewport_->scale;
  viewport_->center_y=field.y+tracking_anchor_.y*field.height-body->offset_y*viewport_->scale;
}
void NativeSystemWorkspace::set_simulation_days(double days){
  if(!snapshot_||snapshot_->simulation_days==days)return;
  snapshot_->simulation_days=days;
  if(spatial_){update_system_motion(*snapshot_,*spatial_);update_camera_tracking();}
}
void NativeSystemWorkspace::refresh(NativeSystemSnapshot snapshot){
  if(!snapshot_||snapshot.campaign_generation!=snapshot_->campaign_generation||snapshot.system_id!=snapshot_->system_id)
    throw std::invalid_argument("A system workspace can refresh only its current campaign and system.");
  if(snapshot.observer_civilization_id!=snapshot_->observer_civilization_id){close();return;}
  snapshot_=std::move(snapshot);spatial_=project_system(*snapshot_);update_camera_tracking();
  if(selected_body_id_&&std::ranges::find(snapshot_->bodies,*selected_body_id_,&NativeSystemBody::id)==snapshot_->bodies.end()){
    selected_body_id_.reset();colony_body_id_.reset();inspector_focus_=InspectorFocus::automatic;
  }
  sync_body_inspection();
}
void NativeSystemWorkspace::refresh_travel(NativeSystemTravelSnapshot travel,std::optional<int> selected_fleet_id){if(!snapshot_||travel.campaign_generation!=snapshot_->campaign_generation||travel.system_id!=snapshot_->system_id||travel.observer_civilization_id!=snapshot_->observer_civilization_id)throw std::invalid_argument("Local travel presentation belongs to a different system view.");if(!text_measurer_&&!travel.lanes.empty())throw std::logic_error("Local lane layout requires renderer text measurement.");std::vector<NativeLaneLabelMetrics> measured;measured.reserve(travel.lanes.size());for(const auto&lane:travel.lanes){const auto value=lane_label(lane);const auto extent=text_measurer_(Text{{},value,{210,226,218,245},15});if(extent.width<0||extent.height<0)throw std::runtime_error("Renderer returned invalid lane label bounds.");measured.push_back({lane.destination_system_id,static_cast<float>(extent.width),static_cast<float>(extent.height)});}travel_=std::move(travel);lane_metrics_=std::move(measured);if(pending_initial_travel_fit_&&viewport_&&spatial_){viewport_=workspace_fit(*spatial_,width_,height_,text_measurer_,travel_,lane_metrics_);zoom_reference_scale_=viewport_->scale;}pending_initial_travel_fit_=false;selected_fleet_id_=selected_fleet_id;if(selected_fleet_id_&&std::ranges::none_of(travel_->fleets,[&](const auto&fleet){return fleet.fleet_id==*selected_fleet_id_;})){selected_fleet_id_.reset();if(inspector_focus_==InspectorFocus::fleet)inspector_focus_=InspectorFocus::automatic;}if(hovered_fleet_id_&&std::ranges::none_of(travel_->fleets,[&](const auto& f){return f.fleet_id==*hovered_fleet_id_;}))hovered_fleet_id_.reset();if(hovered_lane_id_&&std::ranges::none_of(travel_->lanes,[&](const auto& l){return l.destination_system_id==*hovered_lane_id_;}))hovered_lane_id_.reset();if(pressed_lane_id_&&std::ranges::none_of(travel_->lanes,[&](const auto& l){return l.destination_system_id==*pressed_lane_id_;}))pressed_lane_id_.reset();}
void NativeSystemWorkspace::clear_travel()noexcept{travel_.reset();pressed_lane_id_.reset();lane_metrics_.clear();selected_fleet_id_.reset();settlement_status_.reset();hovered_fleet_id_.reset();hovered_lane_id_.reset();notice_.clear();}
void NativeSystemWorkspace::set_colony_body(std::optional<int> body_id)noexcept{colony_body_id_=body_id;}
void NativeSystemWorkspace::set_settlement_preparation(std::optional<stellar::native_settlement_preparation::View> value){
  if(value&&(!snapshot_||!selected_body()||snapshot_->survey_level!=SystemSurveyLevel::fully_surveyed||
      value->campaign_generation!=snapshot_->campaign_generation||value->player_civilization_id!=snapshot_->observer_civilization_id||
      value->system_id!=snapshot_->system_id||value->body_id!=selected_body_id_))value.reset();
  preparation_=std::move(value);sync_body_inspection();
}
void NativeSystemWorkspace::set_settlement_status(std::optional<NativeSystemSettlementStatus> value){
  if(value==settlement_status_)return;
  const auto applies=[&](const std::optional<NativeSystemSettlementStatus>&status){
    return status&&snapshot_&&selected_body_id_&&
      snapshot_->survey_level==SystemSurveyLevel::fully_surveyed&&
      status->destination_body_id==selected_body_id_&&
      (!status->destination_system_id||*status->destination_system_id==snapshot_->system_id);
  };
  const bool relevant=applies(settlement_status_)||applies(value);
  settlement_status_=std::move(value);
  if(relevant)sync_body_inspection();
}
void NativeSystemWorkspace::set_notice(std::string value){notice_=std::move(value);}
void NativeSystemWorkspace::close()noexcept{tracked_body_id_.reset();small_bodies_.clear();small_body_panel_=false;small_body_focus_=false;small_body_ring_=-1;preparation_.reset();preparation_pressed_=false;celestial_appearance_.cancel_preparation();artwork_ready_=true;body_inspection_.clear();snapshot_.reset();spatial_.reset();viewport_.reset();selected_body_id_.reset();colony_body_id_.reset();clear_travel();dragging_=false;pending_initial_travel_fit_=false;width_=height_=0;}
void NativeSystemWorkspace::discard_campaign()noexcept{close();celestial_appearance_.clear();}
std::optional<int> NativeSystemWorkspace::system_id()const noexcept{return snapshot_?std::optional<int>{snapshot_->system_id}:std::nullopt;}
std::optional<std::uint64_t> NativeSystemWorkspace::campaign_generation()const noexcept{return snapshot_?std::optional<std::uint64_t>{snapshot_->campaign_generation}:std::nullopt;}
std::optional<SystemSurveyLevel> NativeSystemWorkspace::survey_level()const noexcept{return snapshot_?std::optional<SystemSurveyLevel>{snapshot_->survey_level}:std::nullopt;}
const SystemSpatialViewport *NativeSystemWorkspace::viewport()const noexcept{return viewport_?&*viewport_:nullptr;}
const NativeSystemSnapshot *NativeSystemWorkspace::snapshot()const noexcept{return snapshot_?&*snapshot_:nullptr;}
const NativeSystemTravelSnapshot *NativeSystemWorkspace::travel_snapshot()const noexcept{return travel_?&*travel_:nullptr;}
std::size_t NativeSystemWorkspace::visible_body_count()const noexcept{if(!spatial_||!viewport_)return 0;return static_cast<std::size_t>(std::ranges::count_if(spatial_->bodies,[&](const auto&body){return viewport_->is_body_visible(*spatial_,body);}));}
void NativeSystemWorkspace::resize(int width,int height){if(!snapshot_||!spatial_||!viewport_||width<=0||height<=0||width==width_&&height==height_)return;const auto old_fit=workspace_fit(*spatial_,width_,height_,text_measurer_,travel_,lane_metrics_);const auto next_fit=workspace_fit(*spatial_,width,height,text_measurer_,travel_,lane_metrics_);viewport_->center_x+=next_fit.center_x-old_fit.center_x;viewport_->center_y+=next_fit.center_y-old_fit.center_y;width_=width;height_=height;zoom_reference_scale_=next_fit.scale;viewport_->scale=std::max(viewport_->scale,zoom_reference_scale_*.05f);update_camera_tracking();}
void NativeSystemWorkspace::reset_fit(int width,int height){tracked_body_id_.reset();small_body_focus_=false;if(!spatial_)return;width_=width;height_=height;viewport_=workspace_fit(*spatial_,width,height,text_measurer_,travel_,lane_metrics_);zoom_reference_scale_=viewport_->scale;pending_initial_travel_fit_=false;}
std::vector<NativeLocalLaneGeometry> NativeSystemWorkspace::lane_geometry()const{if(!travel_||!spatial_||!viewport_)return {};const auto world=SystemWorkspaceLayout::for_viewport(width_,height_).world_field;const auto inset=std::min(12.f,std::min(world.width,world.height)*.05f);const UiRect field{world.x+inset,world.y+inset,world.width-2.f*inset,world.height-2.f*inset};return layout_local_lanes(*spatial_,*viewport_,field,travel_->lanes,lane_metrics_);}
std::vector<int> NativeSystemWorkspace::fleet_hits(Point point)const{if(!travel_||!spatial_||!viewport_)return {};return hit_local_fleets(travel_->fleets,*spatial_,*viewport_,point);}
SystemWorkspaceCommand NativeSystemWorkspace::handle(const InputEvent&e,int width,int height){if(!visible())return {};pointer_=e.position;
const bool resized=width!=width_||height!=height_;resize(width,height);
if(const auto command=handle_small_bodies(e,width,height))return *command;
if(resized||e.type==InputEventType::PointerCancelled){preparation_pressed_=false;pressed_lane_id_.reset();}
if(e.type==InputEventType::LeftReleased&&pressed_lane_id_){
  const auto target=*pressed_lane_id_;pressed_lane_id_.reset();dragging_=false;
  if(travel_)for(const auto& geometry:lane_geometry())if(geometry.destination_system_id==target&&hit_local_lane(geometry,e.position)){
    const auto marker=std::ranges::find(travel_->lanes,target,&NativeLocalLaneMarker::destination_system_id);
    if(marker!=travel_->lanes.end()&&marker->known_label){notice_.clear();return {SystemWorkspaceCommandKind::open_destination,true,target};}
    notice_=tr("SYSTEM_TELEMETRY_UNAVAILABLE","Telemetry unavailable. Dispatch a scout to establish a local survey.");
    return {SystemWorkspaceCommandKind::reconnaissance_required,true};
  }
  return {SystemWorkspaceCommandKind::none,true};
}
const auto layout=SystemWorkspaceLayout::for_viewport(width,height);
if(e.type==InputEventType::LeftReleased&&shipyard_pressed_){shipyard_pressed_=false;dragging_=false;if(const auto bounds=shipyard_bounds();bounds&&bounds->contains(e.position)&&shipyard_owner_){shipyard_selected_=true;selected_body_id_.reset();body_inspection_.clear();return {SystemWorkspaceCommandKind::open_orbital_shipyard,true,*shipyard_owner_};}return {SystemWorkspaceCommandKind::none,true};}
if(e.type==InputEventType::LeftPressed)if(const auto bounds=shipyard_bounds();bounds&&bounds->contains(e.position)&&layout.world_field.contains(e.position)){shipyard_pressed_=true;dragging_=false;return {SystemWorkspaceCommandKind::none,true};}
if(e.type==InputEventType::PointerCancelled)shipyard_pressed_=false;
if(e.type==InputEventType::LeftReleased&&preparation_pressed_){
preparation_pressed_=false;dragging_=false;
if(preparation_&&!colony_body_id_&&layout.colony_action.contains(e.position))return {SystemWorkspaceCommandKind::open_shipyard,true,preparation_->body_id};
return {SystemWorkspaceCommandKind::none,true};}
if(e.type==InputEventType::LeftPressed){preparation_pressed_=false;
if(preparation_&&!colony_body_id_&&layout.colony_action.contains(e.position)){preparation_pressed_=true;dragging_=false;return {SystemWorkspaceCommandKind::none,true};}}
if(e.type==InputEventType::EscapePressed)return {SystemWorkspaceCommandKind::close,true};if(e.type==InputEventType::PointerCancelled){dragging_=false;hovered_fleet_id_.reset();hovered_lane_id_.reset();return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::LeftPressed&&layout.back.contains(e.position))return {SystemWorkspaceCommandKind::close,true};if(e.type==InputEventType::LeftPressed&&layout.reset.contains(e.position)){dragging_=false;reset_fit(width,height);return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::LeftPressed&&colony_body_id_&&selected_body_id_==colony_body_id_&&layout.colony_action.contains(e.position))return {SystemWorkspaceCommandKind::open_colony,true,*colony_body_id_};if(e.type==InputEventType::LeftPressed&&selected_body()&&layout.focus_action.contains(e.position)){focus_selected_body(width,height);return {SystemWorkspaceCommandKind::none,true};}
if(e.type==InputEventType::Wheel&&layout.inspector.contains(e.position)&&body_inspection_.visible()){body_inspection_.scroll(e.wheel_y,layout.inspector,layout.focus_action.y);dragging_=false;return {SystemWorkspaceCommandKind::none,true};}
if(layout.inspector.contains(e.position)||layout.controls_row.contains(e.position)||!layout.world_field.contains(e.position)){if(e.type==InputEventType::LeftPressed||e.type==InputEventType::LeftReleased||e.type==InputEventType::RightPressed||e.type==InputEventType::RightReleased||e.type==InputEventType::PointerMove)dragging_=false;hovered_fleet_id_.reset();hovered_lane_id_.reset();return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::RightPressed){if(!fleet_hits(e.position).empty())return {SystemWorkspaceCommandKind::none,true};if(const auto hit=viewport_->hit_body(*spatial_,e.position.x,e.position.y);hit)return {SystemWorkspaceCommandKind::settlement_target,true,*hit};return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::Wheel){const auto zoom_target=viewport_->hit_body(*spatial_,e.position.x,e.position.y);pending_initial_travel_fit_=false;viewport_=viewport_->zoomed_at(std::pow(1.16f,std::clamp(e.wheel_y,-100.f,100.f)),e.position.x,e.position.y,zoom_reference_scale_*.05f,55.f);viewport_->center_x=std::clamp(viewport_->center_x,-std::max(32000.f,spatial_->design_radius*viewport_->scale+width),std::max(32000.f,spatial_->design_radius*viewport_->scale+width));viewport_->center_y=std::clamp(viewport_->center_y,-std::max(32000.f,spatial_->design_radius*viewport_->scale+height),std::max(32000.f,spatial_->design_radius*viewport_->scale+height));if(zoom_target){const auto marker=std::ranges::find(spatial_->bodies,*zoom_target,&SystemSpatialBodyMarker::body_id);if(marker!=spatial_->bodies.end()&&viewport_->body_radius(*marker)>=24.f){small_body_focus_=false;track_body(*zoom_target);}}
if(tracked_body_id_){const auto marker=std::ranges::find(spatial_->bodies,*tracked_body_id_,&SystemSpatialBodyMarker::body_id);if(marker==spatial_->bodies.end())tracked_body_id_.reset();else track_body(*tracked_body_id_);}
return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::LeftPressed){const auto hits=fleet_hits(e.position);if(!hits.empty()){tracked_body_id_.reset();selected_body_id_.reset();body_inspection_.clear();preparation_.reset();preparation_pressed_=false;colony_body_id_.reset();inspector_focus_=InspectorFocus::fleet;dragging_=false;notice_.clear();return {SystemWorkspaceCommandKind::select_fleet,true,hits.front(),hits};}if(travel_)for(const auto&geometry:lane_geometry())if(hit_local_lane(geometry,e.position)){dragging_=false;pressed_lane_id_=geometry.destination_system_id;hovered_lane_id_=geometry.destination_system_id;return {SystemWorkspaceCommandKind::none,true};}if(const auto hit=viewport_->hit_body(*spatial_,e.position.x,e.position.y);hit){(void)select_body(*hit);if(e.click_count>=2)return {SystemWorkspaceCommandKind::open_colony,true,*hit};}else dragging_=true;return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::PointerMove&&dragging_){tracked_body_id_.reset();small_body_focus_=false;pending_initial_travel_fit_=false;*viewport_=viewport_->translated(e.delta.x,e.delta.y);viewport_->center_x=std::clamp(viewport_->center_x,-std::max(32000.f,spatial_->design_radius*viewport_->scale+width),std::max(32000.f,spatial_->design_radius*viewport_->scale+width));viewport_->center_y=std::clamp(viewport_->center_y,-std::max(32000.f,spatial_->design_radius*viewport_->scale+height),std::max(32000.f,spatial_->design_radius*viewport_->scale+height));hovered_fleet_id_.reset();hovered_lane_id_.reset();return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::PointerMove){const auto hits=fleet_hits(e.position);hovered_fleet_id_=hits.empty()?std::nullopt:std::optional<int>{hits.front()};hovered_lane_id_.reset();if(!hovered_fleet_id_)for(const auto&geometry:lane_geometry())if(hit_local_lane(geometry,e.position)){hovered_lane_id_=geometry.destination_system_id;break;}return {SystemWorkspaceCommandKind::none,true};}if(e.type==InputEventType::LeftReleased){dragging_=false;return {SystemWorkspaceCommandKind::none,true};}return {SystemWorkspaceCommandKind::none,true};}

void NativeSystemWorkspace::render(DrawList &out,int width,int height,bool draw_starfield){artwork_ready_=true;celestial_appearance_.begin_frame();if(!snapshot_||!spatial_||!viewport_)return;resize(width,height);if(draw_starfield)for(std::uint32_t i=0;i<96;++i){const auto a=mix(static_cast<std::uint32_t>(snapshot_->system_id)*131u+i);const auto b=mix(a+17u);const Point p{static_cast<float>(a%static_cast<std::uint32_t>(std::max(1,width))),static_cast<float>(b%static_cast<std::uint32_t>(std::max(1,height)))};out.world.emplace_back(Circle{p,(a&3u)?0.65f:1.1f,{102,133,169,static_cast<std::uint8_t>(70u+a%80u)}});}
  const auto layout=SystemWorkspaceLayout::for_viewport(width,height);const auto panel=layout.inspector;const auto field=layout.world_field;std::map<int,const SystemSpatialBodyMarker*> markers;for(const auto&m:spatial_->bodies)markers.emplace(m.body_id,&m);
  small_bodies_.render(out,*snapshot_,*spatial_,*viewport_,field,snapshot_->simulation_days,small_body_debug_&&snapshot_->developer);
  artwork_ready_=small_bodies_.statistics().missing_images==0;
  for(const auto &body:spatial_->bodies){if(body.parent_body_id&&!viewport_->is_body_visible(*spatial_,body))continue;float cx=spatial_->stellar_hosts[body.stellar_host].x,cy=spatial_->stellar_hosts[body.stellar_host].y;if(body.parent_body_id&&markers.contains(*body.parent_body_id)){cx=markers.at(*body.parent_body_id)->offset_x;cy=markers.at(*body.parent_body_id)->offset_y;if(body.satellite_orbit&&body.satellite_orbit->binary){const float f=static_cast<float>(body.satellite_orbit->parent_mass_fraction);cx+=(body.offset_x-cx)*f;cy+=(body.offset_y-cy)*f;}}const auto path=projected_orbit_path(*spatial_,body);for(std::size_t i=1;i<path.size();++i){const auto a=viewport_->world_to_screen(cx+path[i-1].x,cy+path[i-1].y),b=viewport_->world_to_screen(cx+path[i].x,cy+path[i].y);if(const auto segment=clipped({a.x,a.y},{b.x,b.y},field))out.world.emplace_back(Line{segment->first,segment->second,orbit});}}
  if(travel_){const auto boundary=local_orbital_boundary_radius(*spatial_,*viewport_);constexpr int boundary_segments=96;for(int index=0;index<boundary_segments;index+=2){const auto first=2.f*std::numbers::pi_v<float>*index/boundary_segments,second=2.f*std::numbers::pi_v<float>*(index+1)/boundary_segments;const Point a{viewport_->center_x+std::cos(first)*boundary,viewport_->center_y+std::sin(first)*boundary},b{viewport_->center_x+std::cos(second)*boundary,viewport_->center_y+std::sin(second)*boundary};if(const auto segment=clipped(a,b,field))out.world.emplace_back(Line{segment->first,segment->second,{52,102,78,105}});}for(const auto&geometry:lane_geometry()){if(!local_lane_visible(geometry,field))continue;const auto hovered=hovered_lane_id_==geometry.destination_system_id;const Color lane_color=hovered?Color{245,157,70,255}:Color{59,126,88,225};const bool pressed=pressed_lane_id_==geometry.destination_system_id&&hovered;
const Point center{(geometry.base_a.x+geometry.base_b.x+geometry.apex.x)/3.f,(geometry.base_a.y+geometry.base_b.y+geometry.apex.y)/3.f};
const auto vertex=[&](Point p){return pressed?Point{center.x+(p.x-center.x)*.91f,center.y+(p.y-center.y)*.91f+2.f}:p;};
out.overlay.emplace_back(TriangleMesh{{vertex(geometry.base_a),vertex(geometry.base_b),vertex(geometry.apex)},{0,1,2},pressed?Color{201,113,36,255}:lane_color,field});
out.overlay.emplace_back(Line{vertex(geometry.base_a),vertex(geometry.apex),hovered?Color{255,208,120,255}:Color{82,152,107,255}});
out.overlay.emplace_back(Line{vertex(geometry.apex),vertex(geometry.base_b),hovered?Color{255,208,120,255}:Color{82,152,107,255}});const auto marker=std::ranges::find(travel_->lanes,geometry.destination_system_id,&NativeLocalLaneMarker::destination_system_id);if(marker!=travel_->lanes.end()){const auto metric=std::ranges::find(lane_metrics_,geometry.destination_system_id,&NativeLaneLabelMetrics::destination_system_id);const auto label_height=metric==lane_metrics_.end()?14.f:metric->height;out.world.emplace_back(Text{{geometry.label_center.x,geometry.label_center.y-label_height*.5f},lane_label(*marker),lane_color,15,0,field,TextAlign::Center,FontFace::Interface,geometry.label_rotation_radians*180.f/std::numbers::pi_v<float>});}}}
  if(snapshot_->stellar_object){
    const auto star_center=viewport_->world_to_screen(spatial_->stellar_hosts[0].x,spatial_->stellar_hosts[0].y);
    const auto& physics=*snapshot_->stellar_object;
    const auto factor=spatial_->design_radius*local_chart_render_radius_factor*viewport_->scale/stellar_navigation_au_per_unit(physics);
    const auto danger=static_cast<float>(physics.safe_approach_au*factor);
    if(danger>=4.f)for(int i=0;i<128;i+=2){
      const float a=2.f*std::numbers::pi_v<float>*i/128,b=2.f*std::numbers::pi_v<float>*(i+1)/128;
      if(const auto segment=clipped({star_center.x+std::cos(a)*danger,star_center.y+std::sin(a)*danger},{star_center.x+std::cos(b)*danger,star_center.y+std::sin(b)*danger},field))
        out.world.emplace_back(Line{segment->first,segment->second,{243,130,70,150}});
    }
    if(physics.jet_half_angle_radians>0){
      const auto reach=static_cast<float>(stellar_hazard_extent_au(physics)*factor);
      for(const double side:{0.,std::numbers::pi})for(const double edge:{-1.,1.}){
        const double angle=physics.jet_axis_radians+side+edge*physics.jet_half_angle_radians;
        const Point endpoint{star_center.x+static_cast<float>(std::cos(angle))*reach,star_center.y+static_cast<float>(std::sin(angle))*reach};
        if(const auto segment=clipped({star_center.x,star_center.y},endpoint,field))out.world.emplace_back(Line{segment->first,segment->second,{143,172,255,150}});
      }
    }
  }
  std::vector<std::pair<Point,float>> visible_discs;
  const auto orbital_system=snapshot_stellar_system(*snapshot_);
  const int stellar_count=snapshot_->stellar_orbits?static_cast<int>(snapshot_->stellar_orbits->companions.size()+1):1;
  std::vector<Text> star_labels;
  for(int component=0;component<stellar_count;++component){
    const auto center=spatial_->stellar_hosts[component];const auto screen=viewport_->world_to_screen(center.x,center.y);
    if(stellar_count>1){const auto& path=spatial_->stellar_paths[component];for(std::size_t i=1;i<path.size();++i){
      const auto p=viewport_->world_to_screen(path[i-1].x,path[i-1].y),q=viewport_->world_to_screen(path[i].x,path[i].y);
      if(const auto segment=clipped({p.x,p.y},{q.x,q.y},field))out.world.emplace_back(Line{segment->first,segment->second,{147,116,74,135}});}}
    const auto physics=component==0?snapshot_->stellar_object:std::optional{snapshot_->stellar_orbits->companions[component-1]};
    const auto cls=component==0?snapshot_->primary_stellar_class:component==1?snapshot_->secondary_stellar_class:snapshot_->tertiary_stellar_class;
    const auto artwork=stellar::native_stellar::observed_stellar_artwork(snapshot_->survey_level,physics,cls);
    float radius=star_screen_radius(viewport_->scale,artwork?artwork->scale:1.f);
    if(stellar_count>1){const auto index=component==2?1:0;const auto& component_orbit=snapshot_->stellar_orbits->relative_orbits[index];
      radius=std::min(radius,std::max(3.f,static_cast<float>(component_orbit.radius)*spatial_->stellar_orbit_scales[index]*viewport_->scale*.18f));}
    if(!intersects(field,screen.x,screen.y,radius*1.6f))continue;
    if(stellar_art_&&artwork)stellar_art_(out,{screen.x,screen.y},radius,*artwork,presentation_seconds(),field);
    else celestial_appearance_.append_stellar_disc(out,{screen.x,screen.y},radius,{star_color(cls),cls==StellarClass::BlackHole,mix(static_cast<std::uint32_t>(snapshot_->system_id)*3+component)},presentation_seconds(),field);
    if(artwork&&stellar_activity_)stellar_activity_(out,{screen.x,screen.y},radius,snapshot_->system_id,component,field);
    visible_discs.emplace_back(Point{screen.x,screen.y},radius*1.45f);
    {std::ostringstream label;label<<snapshot_->catalog_name;if(stellar_count>1)label<<" "<<stellar_host_name(component);
      star_labels.push_back(Text{{screen.x,screen.y+radius*1.45f+8},label.str(),{244,216,164,255},15,0,field,TextAlign::Center});}
  }
  std::vector<BodyLabelCandidate> body_labels;
  std::vector<MeshInstance3D> planet_instances;int detailed_planets=0,close_planets=0;
  for(const auto &body:spatial_->bodies){
    if(!viewport_->is_body_visible(*spatial_,body))continue;
    const auto p=viewport_->world_to_screen(body.offset_x,body.offset_y);const auto radius=viewport_->body_radius(body);
    const auto state=std::ranges::find(snapshot_->bodies,body.body_id,&NativeSystemBody::id);
    const auto* appearance=state!=snapshot_->bodies.end()&&state->appearance?&*state->appearance:nullptr;
    const auto ring_key=body.sol_texture_key.value_or("");
    const float ring_extent=appearance&&appearance->rings.enabled?static_cast<float>(appearance->rings.outer_radius):planet_ring_extent(ring_key);
    const auto footprint=radius*ring_extent+4.f;if(!intersects(field,p.x,p.y,footprint))continue;
    visible_discs.emplace_back(Point{p.x,p.y},footprint);
    if(appearance&&planet_materials_){
      // Full two-dimensional ring maps cost more than radial strips. Bound the
      // close allocations as well as the count of medium-detail worlds, leaving
      // room for stellar effects and the selected planet's independent view.
      const int lod=radius>45&&detailed_planets++<6?(radius>150&&close_planets++<2?2048:256):128;
      auto maps=planet_materials_(*appearance,lod);
      if(maps){const auto host=spatial_->stellar_hosts[state->stellar_host];const auto host_physics=stellar_host_physics(orbital_system,state->stellar_host);const auto light=stellar::native_planets::system_lighting(*snapshot_,*state);
        stellar::native_planets::append_instances(planet_instances,*appearance,*maps,{p.x-field.x-field.width*.5f,field.height*.5f-(p.y-field.y),body.parent_body_id&&markers.contains(*body.parent_body_id)?15000.*std::tanh((body.offset_height-markers.at(*body.parent_body_id)->offset_height)*viewport_->scale/15000.):0},radius,lod,snapshot_->simulation_days,light,{},false,colony_body_id_==body.body_id,visual_seconds_,parent_facing_bearing(*spatial_,body));
      }else{artwork_ready_=false;out.world.emplace_back(Circle{{p.x,p.y},radius,{44,51,61,255}});}
    }else{
      const bool ringed=ring_extent>1;if(ringed)append_planet_rings(out,ring_key,{p.x,p.y},radius/1.08f,field,false);
      std::shared_ptr<const RgbaImage> image;if(image_provider_)image=image_provider_({snapshot_->campaign_generation,body.body_id,snapshot_->survey_level==SystemSurveyLevel::fully_surveyed,body.visual_class,body.sol_texture_key,mix(static_cast<std::uint32_t>(body.body_id)),std::atan2(-body.offset_y,-body.offset_x)});
      if(image_provider_&&!image&&body.visual_class!=NativeSystemBodyVisualClass::unknown_planet&&body.visual_class!=NativeSystemBodyVisualClass::unknown_moon)artwork_ready_=false;
      if(image)out.world.emplace_back(Image{image,{p.x-radius,p.y-radius,2*radius,2*radius},std::nullopt,{255,255,255,255},field});else out.world.emplace_back(Circle{{p.x,p.y},radius,visual_color(body.visual_class)});
      if(ringed)append_planet_rings(out,ring_key,{p.x,p.y},radius/1.08f,field,true);
    }
    if(selected_body_id_==body.body_id)out.world.emplace_back(Circle{{p.x,p.y},radius+4,{122,230,190,90}});
    const Text label{{},body.label,text,15};const auto measured=text_measurer_?text_measurer_(label):TextExtent{static_cast<int>(body.label.size()*7u),19};if(measured.width<0||measured.height<0)throw std::runtime_error("Renderer returned invalid system label bounds.");const auto label_width=static_cast<float>(measured.width),label_height=static_cast<float>(measured.height);std::array<UiRect,4> placements{};for(int row=0;row<4;++row)placements[row]={p.x-label_width*.5f,p.y+footprint+5.f+row*(label_height+4.f),label_width,label_height};body_labels.push_back(BodyLabelCandidate{body.body_id,selected_body_id_==body.body_id,body.label,placements});}
  if(!planet_instances.empty()){Camera3D camera;camera.projection=Projection3D::Orthographic;camera.position={0,0,50000};camera.orthographic_height=field.height;camera.near_plane=.1f;camera.far_plane=100000;
    out.world.emplace_back(Scene3DView{Scene3D::create(camera,std::move(planet_instances)),field});}
  std::stable_sort(body_labels.begin(),body_labels.end(),[](const auto&left,const auto&right){return std::pair{!left.selected,left.body_id}<std::pair{!right.selected,right.body_id};});std::vector<UiRect> accepted_labels;
  for(const auto& label:star_labels){
    const auto measured=text_measurer_?text_measurer_(label):TextExtent{static_cast<int>(label.value.size()*7u),19};
    for(int row=0;row<8;++row){const UiRect bounds{label.at.x-measured.width*.5f,label.at.y+row*(measured.height+4.f),static_cast<float>(measured.width),static_cast<float>(measured.height)};
      if(!contains_rect(field,bounds)||std::ranges::any_of(accepted_labels,[&](UiRect accepted){return overlaps(bounds,accepted,3.f);})||std::ranges::any_of(visible_discs,[&](const auto& disc){return intersects(bounds,disc.first.x,disc.first.y,disc.second);}))continue;
      auto placed=label;placed.at.y=bounds.y;out.world.emplace_back(placed);accepted_labels.push_back(bounds);break;
    }
  }
  for(const auto&candidate:body_labels)for(const auto&bounds:candidate.placements){if(!contains_rect(field,bounds))continue;const auto overlaps_disc=std::ranges::any_of(visible_discs,[&](const auto&disc){return intersects(bounds,disc.first.x,disc.first.y,disc.second);});const auto overlaps_label=std::ranges::any_of(accepted_labels,[&](UiRect accepted){return overlaps(bounds,accepted,3.f);});if(overlaps_disc||overlaps_label)continue;out.world.emplace_back(Text{{bounds.x,bounds.y},candidate.value,text,15,0,field});accepted_labels.push_back(bounds);break;}
  if(travel_)for(const auto&fleet:travel_->fleets){const auto p=local_fleet_anchor(fleet,*spatial_,*viewport_);if(!contains_disc(field,p,16))continue;const auto selected=selected_fleet_id_==fleet.fleet_id,hovered=hovered_fleet_id_==fleet.fleet_id;const Color fleet_color=selected?Color{124,255,182,255}:hovered?Color{94,235,158,255}:Color{66,196,126,240};if(fleet.moving&&!fleet.held){const auto dx=fleet.chart_target.x-fleet.chart_position.x,dy=fleet.chart_target.y-fleet.chart_position.y,length=std::hypot(dx,dy);if(length>.0001){const auto ux=dx/length,uy=dy/length,nx=-uy,ny=ux;for(const auto offset:{-2.5f,2.5f}){const Point a{p.x-ux*7+nx*offset,p.y-uy*7+ny*offset},b{p.x-ux*15+nx*offset,p.y-uy*15+ny*offset};if(const auto segment=clipped(a,b,field))out.world.emplace_back(Line{segment->first,segment->second,{92,225,154,170}});}}}out.world.emplace_back(Circle{p,selected?9.f:7.f,{fleet_color.r,fleet_color.g,fleet_color.b,selected?std::uint8_t{120}:std::uint8_t{80}}});out.world.emplace_back(Circle{p,selected?5.f:4.f,fleet_color});out.world.emplace_back(Text{{p.x,p.y-5},fleet_role(fleet.role),{225,255,237,255},9,0,field,TextAlign::Center});}
  if(const auto r=shipyard_bounds();r&&overlaps(*r,field)){
    if(shipyard_image_)out.world.emplace_back(Image{shipyard_image_,*r,{},{255,255,255,255},field});
    else out.world.emplace_back(Circle{{r->x+r->width*.5f,r->y+r->height*.5f},12,{90,216,239,255}});
    if(shipyard_selected_||r->contains(pointer_))out.overlay.emplace_back(StrokedRectangle{*r,{105,219,246,255}});
    out.world.emplace_back(Text{{r->x+r->width*.5f,r->y+r->height+2},tr("SYSTEM_SHIPYARD","Orbital Shipyard"),{141,221,242,255},12,180,field,TextAlign::Center});
  }
  artwork_ready_=artwork_ready_&&!celestial_appearance_.preparation_pending();
  render_small_body_panel(out,width,height);
  if(!artwork_ready_)overlay_text(out,field.x+18.f,field.y+12.f,tr("SYSTEM_PREPARING","Preparing system imagery..."),muted,14,field.width-36.f,field);
  const auto ui_scale=NativeUiLayout::for_viewport(width,height).scale;overlay_fill(out,layout.controls_row,{5,15,28,238});overlay_stroke(out,layout.controls_row,border);overlay_fill(out,layout.back,{10,27,47,245});overlay_stroke(out,layout.back,border);overlay_text(out,layout.back.x+22.f*ui_scale,layout.back.y+6.f*ui_scale,tr("SYSTEM_BACK","BACK"),text,15);overlay_fill(out,layout.reset,{10,27,47,245});overlay_stroke(out,layout.reset,border);overlay_text(out,layout.reset.x+14.f*ui_scale,layout.reset.y+6.f*ui_scale,tr("SYSTEM_FIT","FIT SYSTEM"),text,12);
  if(tracked_body_id_){const auto body=std::ranges::find(snapshot_->bodies,*tracked_body_id_,&NativeSystemBody::id);
    if(body!=snapshot_->bodies.end())overlay_text(out,field.x+12*ui_scale,field.y+12*ui_scale,trf("SYSTEM_FOLLOWING",{body->name},"Following {0} · drag to release"),{164,221,237,255},13,field.width-24*ui_scale,field);}
  const UiRect zoom_badge{field.x+field.width-168.f*ui_scale,field.y+field.height-35.f*ui_scale,156.f*ui_scale,29.f*ui_scale};
  overlay_fill(out,zoom_badge,{8,25,39,245});overlay_stroke(out,zoom_badge,border);
  overlay_text(out,zoom_badge.x+12.f*ui_scale,zoom_badge.y+6.f*ui_scale,trf("SYSTEM_ZOOM",{number(magnification(),2)},"Zoom {0}x"),{164,221,237,255},13,zoom_badge.width-20.f*ui_scale,zoom_badge);
  const auto title_x=layout.reset.x+layout.reset.width+24.f*ui_scale;const Text title{{title_x,layout.controls_row.y+4.f*ui_scale},snapshot_->catalog_name,text,16,0,layout.controls_row};
  const auto title_extent=text_measurer_?text_measurer_(title):TextExtent{static_cast<int>(title.value.size()*11u),28};
  out.overlay.emplace_back(title);
  overlay_text(out,title_x+static_cast<float>(title_extent.width)+18.f*ui_scale,layout.controls_row.y+11.f*ui_scale,snapshot_->survey_level==SystemSurveyLevel::fully_surveyed?tr("SYSTEM_SURVEY_FULL","FULL SURVEY"):tr("SYSTEM_SURVEY_RECON","RECONNAISSANCE"),muted,12,0,layout.controls_row);
  overlay_fill(out,panel,{6,18,33,242});overlay_stroke(out,panel,border);float y=panel.y+14;const auto add=[&](std::string value,Color color,int size=14,float step=20){const Text label{{panel.x+14,y},std::move(value),color,size,panel.width-28,panel};const auto measured=text_measurer_?text_measurer_(label):TextExtent{0,size+6};out.overlay.emplace_back(label);y+=std::max(step,static_cast<float>(measured.height)+4.f);};if(!selected_body())add(tr("SYSTEM_INSPECTOR","SYSTEM INSPECTOR"),text,18,31);const auto*fleet=selected_fleet();if(inspector_focus_!=InspectorFocus::body&&fleet){add(fleet->foreign_inspection?tr("SYSTEM_FLEET_DEV","DEVELOPER FLEET INSPECTION"):tr("SYSTEM_FLEET_OWNED","OWNED LOCAL FLEET"),muted,13,21);add(fleet->name,text,17,26);add(trf("SYSTEM_FLEET_STATE",{fleet_role(fleet->role),fleet->moving?tr("SYSTEM_STATE_MOVING","Moving"):fleet->held?tr("SYSTEM_STATE_HOLDING","Holding"):tr("SYSTEM_STATE_LOCAL","Local")},"{0}  {1}"),text,14,24);if(settlement_status_&&settlement_status_->fleet_id==fleet->fleet_id){add(settlement_status_->status,{102,232,164,255},13,21);if(settlement_status_->destination_body_id)add(trf("SYSTEM_ESTABLISHMENT",{number(settlement_status_->settlement_days_completed,1),number(settlement_status_->establishment_days,0)},"Establishment  {0} / {1} days"),text,13,20);}}else if(selected_body()){body_inspection_.render(out,panel,layout.focus_action.y);}else {
    if(snapshot_->stellar_object){const auto& p=*snapshot_->stellar_object;const auto& d=stellar::core::stellar_object_definition(p.type);
      add(d.name,text,16,26);
      add(trf("SYSTEM_RADIUS",{compact_km(p.radius_solar*695700.)},"Radius  {0} km"),text,13,20);
      add(trf("SYSTEM_SURFACE_TEMP",{number(p.effective_temperature_kelvin,0)},"Surface  {0} K"),text,13,20);
      add(trf("SYSTEM_LUMINOSITY",{number(p.luminosity_solar,4)},"Luminosity  {0} x Sol"),text,13,20);
      add(trf("SYSTEM_SAFE_APPROACH",{compact_km(p.safe_approach_au*stellar::core::astronomical_unit_km)},"Safe approach  {0} km"),{241,182,98,255},13,20);
      if(p.luminosity_solar>0)add(trf("SYSTEM_HZ",{number(p.inner_hz_au*stellar::core::astronomical_unit_km/1000000.,1),number(p.outer_hz_au*stellar::core::astronomical_unit_km/1000000.,1)},"Temperate zone  {0} - {1} M km"),muted,13,20);
      if(p.jet_half_angle_radians>0)add(tr("SYSTEM_JETS_DANGER","DANGER: directional high-energy jets"),{241,139,98,255},13,20);
      if(p.habitability_modifier<.2)add(tr("SYSTEM_HABITABILITY_LIMIT","Severe radiation or short stellar lifetime limits habitability."),muted,13,20);
    }
    if(snapshot_->stellar_orbits){const auto& a=*snapshot_->stellar_orbits;
      for(std::size_t i=0;i<a.relative_orbits.size();++i){const double years=2*std::numbers::pi/a.relative_orbits[i].angular_speed/365.25;
        add(trf(i==0?"SYSTEM_ORBIT_AB":"SYSTEM_ORBIT_ABC",{number(years,1)},i==0?"A+B orbit: {0} years":"AB+C orbit: {0} years"),muted,12,20);}
    }
    add(tr("SYSTEM_INSPECTOR_HINT","Select a visible body or owned fleet"),muted,14,20);
  }if(selected_body()){
  overlay_fill(out,layout.focus_action,layout.focus_action.contains(pointer_)?Color{26,63,82,255}:Color{12,37,55,255});
  overlay_stroke(out,layout.focus_action,{102,205,224,255});
  overlay_text(out,layout.focus_action.x+10.f,layout.focus_action.y+9.f,tr("SYSTEM_FOCUS","FOCUS PLANET"),text,14,layout.focus_action.width-20.f,layout.focus_action);
}
if(colony_body_id_&&selected_body_id_==colony_body_id_){overlay_fill(out,layout.colony_action,layout.colony_action.contains(pointer_)?Color{24,76,71,255}:Color{13,51,52,255});overlay_stroke(out,layout.colony_action,{102,232,164,255});overlay_text(out,layout.colony_action.x+10,layout.colony_action.y+9,tr("SYSTEM_MANAGE","MANAGE PLANET"),text,14,layout.colony_action.width-20,layout.colony_action);}else if(preparation_&&selected_body()){
overlay_fill(out,layout.colony_action,layout.colony_action.contains(pointer_)?Color{24,76,71,255}:Color{13,51,52,255});
overlay_stroke(out,layout.colony_action,{102,232,164,255});
overlay_text(out,layout.colony_action.x+10,layout.colony_action.y+9,tr("SYSTEM_VIEW_SHIPYARD","VIEW SHIPYARD"),text,14,layout.colony_action.width-20,layout.colony_action);
}else if(!notice_.empty()){const UiRect notice_base=selected_body()?layout.colony_action:UiRect{panel.x+12,panel.y+panel.height-88,panel.width-24,74};
// Order results wrap to several lines — grow the banner upward to fit the
// measured text rather than clipping mid-line.
float notice_height=notice_base.height;
if(text_measurer_){const auto measured=text_measurer_(Text{{},notice_,{245,183,93,250},13,notice_base.width-16.f});if(measured.height>0)notice_height=std::max(notice_base.height,static_cast<float>(measured.height)+18.f);}
notice_height=std::min(notice_height,notice_base.y+notice_base.height-(panel.y+8.f));
// The command HUD's context plate owns the bottom-center strip — lift the
// banner above it where the inspector's x-range reaches under the plate.
auto notice_bottom=notice_base.y+notice_base.height;
const auto plate=CommandHudLayout::make(width_,height_).context;
if(notice_base.x<plate.x+plate.width&&notice_base.x+notice_base.width>plate.x)
  notice_bottom=std::min(notice_bottom,plate.y-4.f);
const auto notice_top=std::max(panel.y+8.f,notice_bottom-notice_height);
const UiRect notice_bounds{notice_base.x,notice_top,notice_base.width,notice_bottom-notice_top};
overlay_fill(out,notice_bounds,{35,25,16,235});overlay_stroke(out,notice_bounds,{139,92,42,255});overlay_text(out,notice_bounds.x+8,notice_bounds.y+8,notice_,{245,183,93,250},13,notice_bounds.width-16,notice_bounds);}}
void NativeSystemWorkspace::sync_body_inspection(){
  if(!snapshot_||!selected_body_id_){preparation_.reset();preparation_pressed_=false;body_inspection_.clear();return;}
  auto inspection=build_body_inspection(*snapshot_,*selected_body_id_,locale_);
  if(preparation_&&(preparation_->campaign_generation!=snapshot_->campaign_generation||
      preparation_->player_civilization_id!=snapshot_->observer_civilization_id||
      preparation_->system_id!=snapshot_->system_id||preparation_->body_id!=selected_body_id_||
      snapshot_->survey_level!=SystemSurveyLevel::fully_surveyed)){
    preparation_.reset();preparation_pressed_=false;
  }
  if(inspection&&preparation_){
    const auto& v=*preparation_;const auto& a=v.suitability;const auto& e=a.environment;
    std::string habitat;
    const auto support=[&](bool needed,std::string name){if(needed){if(!habitat.empty())habitat+="; ";habitat+=name;}};
    support(e.requires_gravity_mitigation,tr("SYSTEM_SUPPORT_GRAVITY","Gravity support"));support(e.requires_thermal_control,tr("SYSTEM_SUPPORT_THERMAL","Thermal control"));
    support(e.requires_pressure_control,tr("SYSTEM_SUPPORT_PRESSURE","Pressure control"));support(e.requires_sealed_habitat,tr("SYSTEM_SUPPORT_SEALED","Sealed habitat"));
    support(e.requires_artificial_biosphere,tr("SYSTEM_SUPPORT_BIOSPHERE","Artificial biosphere"));support(e.requires_radiation_shielding,tr("SYSTEM_SUPPORT_RADIATION","Radiation shielding"));
    std::string site;
    if(!v.solid_surface)site=tr("SYSTEM_SITE_NO_SURFACE","No solid settlement surface");
    else if(v.native_pre_warp_life)site=tr("SYSTEM_SITE_PROTECTED","Protected native civilization");
    else if(a.viability==SpeciesColonizationViability::Unsuitable)site=tr("SYSTEM_SITE_HARSH","Too harsh for a colony");
    else if(a.viability==SpeciesColonizationViability::HabitatSupportedFallback)site=tr("SYSTEM_SITE_HABITAT","Habitat support required");
    else site=tr("SYSTEM_SITE_VIABLE","Naturally viable environment");
    BodySection readiness{tr("SYSTEM_SECTION_ASSESSMENT","SETTLEMENT ASSESSMENT"),{
      {tr("SYSTEM_FACT_POPULATION","Population"),v.species_name},
      {tr("SYSTEM_FACT_ENVIRONMENT","Environment"),site},
      {tr("SYSTEM_FACT_FIT","Natural fit"),trf("SYSTEM_PERCENT",{number(e.natural_habitability*100.,0)},"{0}%")},
      {tr("SYSTEM_FACT_HAZARD","Primary hazard"),environmental_hazard(e.limiting_factor,locale_)},
      {tr("SYSTEM_FACT_LIFE_SUPPORT","Life support"),habitat.empty()?tr("SYSTEM_NO_MITIGATION","No environmental mitigation"):habitat},
      {tr("SYSTEM_FACT_DEPOSIT","Rare deposit"),v.rare_resource?tr("SYSTEM_CONFIRMED","Confirmed"):tr("SYSTEM_NOT_CONFIRMED","None confirmed")},
      {tr("SYSTEM_FACT_NEXT_STEP","Next step"),a.can_found_current_colony?tr("SYSTEM_STEP_COLONY","Prepare a colony vessel"):
          v.solid_surface&&!v.native_pre_warp_life&&v.rare_resource?tr("SYSTEM_STEP_OUTPOST","Review a sealed resource outpost"):tr("SYSTEM_STEP_EXPLORE","Explore other worlds")}
    }};
    inspection->sections.insert(inspection->sections.begin(),std::move(readiness));
    const auto add_option=[&](const stellar::native_settlement_preparation::Option& o){
      inspection->sections.push_back({o.design_name,{
        {tr("SYSTEM_FACT_SHIP_COST","Ship cost"),o.formatted_ship_cost},{tr("SYSTEM_FACT_INDUSTRY","Industry"),trf("SYSTEM_OVER_CONSTRUCTION",{number(o.industry_cost,0)},"{0} over construction")},
        {tr("SYSTEM_FACT_POPULATION","Population"),trf("SYSTEM_POP_RESERVED",{number(o.population_reservation_millions,0)},"{0} million reserved")},
        {tr("SYSTEM_FACT_BUILD_TIME","Build time"),trf("SYSTEM_MIN_PRODUCTION",{stellar::native_campaign::format_campaign_duration(o.minimum_build_days)},"{0} minimum at full production")},
        {tr("SYSTEM_FACT_SHIPYARD","Shipyard"),o.shipbuilding_blocker.value_or(tr("SYSTEM_READY_TO_ORDER","Ready to order"))},
        {tr("SYSTEM_FACT_EXPEDITION","Expedition"),trf("SYSTEM_EXPEDITION_COST",{o.formatted_expedition_cost},"{0} separately authorized")},
        {tr("SYSTEM_FACT_ESTABLISHMENT","Establishment"),trf("SYSTEM_WORK_DAYS",{number(o.establishment_days,0)},"{0} work days after arrival")}}});
    };
    add_option(v.colony_ship);add_option(v.resource_outpost);
    inspection->sections.push_back({tr("SYSTEM_SECTION_COMMITTING","BEFORE COMMITTING"),{{tr("SYSTEM_FACT_TREASURY","Treasury"),v.formatted_treasury},
      {tr("SYSTEM_FACT_TIMING","Timing"),tr("SYSTEM_TIMING_NOTE","Supply and funding can delay completion")},
      {tr("SYSTEM_FACT_MISSION","Mission"),tr("SYSTEM_MISSION_NOTE","Select a populated vessel and right-click a surveyed world. Route, occupancy, reservations and funds are checked before confirmation.")}}});
  }
  if(inspection&&settlement_status_&&snapshot_->survey_level==SystemSurveyLevel::fully_surveyed&&
     settlement_status_->destination_body_id==selected_body_id_&&
     (!settlement_status_->destination_system_id||*settlement_status_->destination_system_id==snapshot_->system_id)){
    const auto& status=*settlement_status_;
    inspection->sections.insert(inspection->sections.begin(),{tr("SYSTEM_SECTION_PROGRESS","SETTLEMENT IN PROGRESS"),{
      {tr("SYSTEM_FACT_STATUS","Status"),status.status},
      {tr("SYSTEM_FACT_ESTABLISHMENT","Establishment"),trf("SYSTEM_ESTABLISHMENT_DAYS",{number(status.settlement_days_completed,1),number(status.establishment_days,1)},"{0} / {1} days")},
      {tr("SYSTEM_FACT_NEXT_STEP","Next step"),tr("SYSTEM_STEP_SUPPLIED","Keep the expedition supplied while establishment completes.")}
    }});
    const auto assessment=tr("SYSTEM_SECTION_ASSESSMENT","SETTLEMENT ASSESSMENT");
    const auto next_step=tr("SYSTEM_FACT_NEXT_STEP","Next step");
    if(preparation_&&!inspection->sections.empty())for(auto&section:inspection->sections)if(section.heading==assessment)for(auto&fact:section.facts)if(fact.label==next_step)fact.value=tr("SYSTEM_STEP_ESTABLISHING","Settlement expedition is establishing this body.");
  }
  body_inspection_.set_inspection(std::move(inspection));
}
bool NativeSystemWorkspace::select_body(int body_id){
  if(!snapshot_||!spatial_||std::ranges::find(snapshot_->bodies,body_id,&NativeSystemBody::id)==snapshot_->bodies.end()||
     std::ranges::find(spatial_->bodies,body_id,&SystemSpatialBodyMarker::body_id)==spatial_->bodies.end())return false;
  shipyard_selected_=false;small_body_focus_=false;track_body(body_id);
  if(selected_body_id_!=body_id)preparation_.reset();preparation_pressed_=false;selected_body_id_=body_id;colony_body_id_.reset();inspector_focus_=InspectorFocus::body;dragging_=false;notice_.clear();sync_body_inspection();return true;
}
void NativeSystemWorkspace::focus_selected_body(int width,int height){small_body_focus_=false;
  if(!selected_body_id_||!spatial_||!viewport_)return;
  const auto marker=std::ranges::find(spatial_->bodies,*selected_body_id_,&SystemSpatialBodyMarker::body_id);
  if(marker==spatial_->bodies.end())return;
  const auto field=SystemWorkspaceLayout::for_viewport(width,height).world_field;
  viewport_->center_x=field.x+field.width*.5f-marker->offset_x*viewport_->scale;
  viewport_->center_y=field.y+field.height*.5f-marker->offset_y*viewport_->scale;
  dragging_=false;pending_initial_travel_fit_=false;track_body(*selected_body_id_);
}
std::optional<UiRect> NativeSystemWorkspace::shipyard_bounds()const{
  if(!shipyard_owner_||!spatial_||!viewport_)return {};
  auto body=std::ranges::find(spatial_->bodies,shipyard_body_,&SystemSpatialBodyMarker::body_id);
  const float x=body==spatial_->bodies.end()?spatial_->design_radius*.18f:body->offset_x;
  const float y=body==spatial_->bodies.end()?0.f:body->offset_y;
  const auto p=viewport_->world_to_screen(x,y);const float radius=body==spatial_->bodies.end()?0.f:viewport_->body_radius(*body);
  const float size=52.f*NativeUiLayout::for_viewport(width_,height_).scale;
  return UiRect{p.x+radius+18.f,p.y-size*.5f,size,size};
}
void NativeSystemWorkspace::focus_shipyard(int width,int height){tracked_body_id_.reset();
  if(!viewport_||!shipyard_owner_)return;const auto r=shipyard_bounds();if(!r)return;
  const auto field=SystemWorkspaceLayout::for_viewport(width,height).world_field;
  viewport_->center_x+=field.x+field.width*.5f-(r->x+r->width*.5f);
  viewport_->center_y+=field.y+field.height*.5f-(r->y+r->height*.5f);
  selected_body_id_.reset();body_inspection_.clear();selected_fleet_id_.reset();shipyard_selected_=true;dragging_=false;pending_initial_travel_fit_=false;
}
void NativeSystemWorkspace::focus_fleet(int fleet_id,int width,int height){tracked_body_id_.reset();
  if(!travel_||!spatial_||!viewport_)return;const auto f=std::ranges::find(travel_->fleets,fleet_id,&NativeLocalFleetMarker::fleet_id);if(f==travel_->fleets.end())return;
  const auto p=local_fleet_anchor(*f,*spatial_,*viewport_);const auto field=SystemWorkspaceLayout::for_viewport(width,height).world_field;
  viewport_->center_x+=field.x+field.width*.5f-p.x;viewport_->center_y+=field.y+field.height*.5f-p.y;
  selected_body_id_.reset();body_inspection_.clear();selected_fleet_id_=fleet_id;shipyard_selected_=false;inspector_focus_=InspectorFocus::fleet;dragging_=false;pending_initial_travel_fit_=false;
}
const NativeSystemBody *NativeSystemWorkspace::selected_body()const noexcept{if(!snapshot_||!selected_body_id_)return nullptr;const auto found=std::ranges::find(snapshot_->bodies,*selected_body_id_,&NativeSystemBody::id);return found==snapshot_->bodies.end()?nullptr:&*found;}
const NativeLocalFleetMarker *NativeSystemWorkspace::selected_fleet()const noexcept{if(!travel_||!selected_fleet_id_)return nullptr;const auto found=std::ranges::find(travel_->fleets,*selected_fleet_id_,&NativeLocalFleetMarker::fleet_id);return found==travel_->fleets.end()?nullptr:&*found;}
} // namespace stellar::native_system_ui
