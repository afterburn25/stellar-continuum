#include "native_settlement_workspace.hpp"

#include <stellar/core/colonization_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace stellar::native_colony_ui {

bool has_active_settlement_target(
    const stellar::native_colony::NativeSettlementMissionView& mission) noexcept {
  return mission.destination_body_id.has_value() ||
         mission.settlement_body_id.has_value();
}
namespace {
using namespace stellar::native_map;
using namespace stellar::native_colony;
constexpr Color shade{0,4,10,190}, panel{7,18,33,255}, row{12,32,54,255};
constexpr Color border{92,154,205,255}, text_color{235,244,255,255};
constexpr Color muted{158,185,211,255}, good{102,232,164,255}, bad{245,183,93,255};
void fill(DrawList& out,UiRect rect,Color color){out.overlay.emplace_back(FilledRectangle{rect,color});}
void stroke(DrawList& out,UiRect rect,Color color){out.overlay.emplace_back(StrokedRectangle{rect,color});}
void label(DrawList&out,UiRect bounds,std::string value,Color color,int size,TextAlign align=TextAlign::Left){const float x=align==TextAlign::Center?bounds.x+bounds.width*.5f:bounds.x;out.overlay.emplace_back(Text{{x,bounds.y},std::move(value),color,size,bounds.width,bounds,align,FontFace::Interface});}
std::string number(double value,int precision=1){std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();}
std::string kind_name(NativeSettlementMissionKind kind){return kind==NativeSettlementMissionKind::Colony?"COLONY EXPEDITION":"RESOURCE OUTPOST EXPEDITION";}
double establishment_days(NativeSettlementMissionKind kind){return kind==NativeSettlementMissionKind::Colony?stellar::core::ColonizationSimulation::colony_establishment_days:stellar::core::ColonizationSimulation::outpost_establishment_days;}
}

SettlementWorkspaceLayout SettlementWorkspaceLayout::for_viewport(int width,int height)noexcept{
  const float w=static_cast<float>(width),h=static_cast<float>(height);
  const float scale=std::max(.72f,std::min({h/900.f,w/1100.f,1.6f}));
  const float pw=std::min(w-28.f*scale,720.f*scale),ph=std::min(h-96.f*scale,510.f*scale);
  const UiRect panel_rect{(w-pw)*.5f,(h-ph)*.5f,pw,ph};
  const float button_w=150.f*scale,button_h=40.f*scale,bottom=panel_rect.y+panel_rect.height-54.f*scale;
  return {scale,static_cast<int>(std::lround(23.f*scale)),static_cast<int>(std::lround(16.f*scale)),static_cast<int>(std::lround(13.f*scale)),{0,0,w,h},panel_rect,{panel_rect.x+panel_rect.width-button_w-18.f*scale,bottom,button_w,button_h},{panel_rect.x+18.f*scale,bottom,button_w,button_h}};
}

void NativeSettlementWorkspace::set_preview(NativeSettlementTargetPreview value){preview_=std::move(value);}
void NativeSettlementWorkspace::clear()noexcept{preview_.reset();pointer_={};}

SettlementWorkspaceCommand NativeSettlementWorkspace::handle(const InputEvent&event,int width,int height){
  if(!preview_)return {};
  pointer_=event.position;
  if(event.type==InputEventType::EscapePressed){clear();return {SettlementWorkspaceCommandKind::Cancel,true};}
  const auto layout=SettlementWorkspaceLayout::for_viewport(width,height);
  if(event.type==InputEventType::LeftPressed){
    if(layout.cancel.contains(event.position)){clear();return {SettlementWorkspaceCommandKind::Cancel,true};}
    if(layout.confirm.contains(event.position)&&preview_->accepted)return {SettlementWorkspaceCommandKind::Confirm,true};
  }
  return {SettlementWorkspaceCommandKind::None,true};
}

void NativeSettlementWorkspace::render(DrawList&out,int width,int height)const{
  if(!preview_)return;
  const auto layout=SettlementWorkspaceLayout::for_viewport(width,height);const auto&p=*preview_;
  fill(out,layout.shade,shade);fill(out,layout.panel,panel);stroke(out,layout.panel,border);
  float x=layout.panel.x+20.f*layout.scale,y=layout.panel.y+17.f*layout.scale;
  const float content_w=layout.panel.width-40.f*layout.scale,line=25.f*layout.scale;
  auto add=[&](std::string value,Color color=muted,int size=0,float step=0.f){label(out,{x,y,content_w,line},std::move(value),color,size?size:layout.body_font);y+=step?step:line;};
  add(kind_name(p.kind),text_color,layout.title_font,38.f*layout.scale);
  add(p.fleet_name+"  |  "+p.personnel_species_name,text_color);
  add(number(p.personnel_millions,3)+"M personnel",muted,layout.body_font,32.f*layout.scale);
  if(p.candidate){
    add(p.candidate->body_name+"  /  "+p.candidate->system_name,text_color,layout.title_font,36.f*layout.scale);
    add(std::string(p.requires_new_authorization?"New authorization  ":"Retarget authorization retained  ·  New charge  ")+p.formatted_authorization);
    add("Current treasury  "+p.formatted_treasury);
    add("Route distance  "+stellar::core::format_interstellar_metric_primary(p.candidate->reach.route_distance_light_years));
    if(p.candidate->reach.route_system_ids)add("Confirmed lane route  "+std::to_string(p.candidate->reach.route_system_ids->size()>0?p.candidate->reach.route_system_ids->size()-1:0)+" hop(s)");
    add("Travel duration estimate unavailable",muted,layout.small_font);
    add("Establishment after arrival  "+number(establishment_days(p.kind),0)+" days");
    if(p.kind==NativeSettlementMissionKind::Colony){
      add("Natural habitability  "+number(p.candidate->natural_habitability*100.,1)+"%  |  Operational capacity  "+number(p.candidate->unprotected_operational_capacity*100.,1)+"%");
    }else{
      add(p.candidate->has_confirmed_deposit?p.candidate->deposit_material_name+"  "+p.candidate->deposit_grade:"No confirmed extractable deposit",p.candidate->has_confirmed_deposit?good:bad);
    }
  }
  y=layout.confirm.y-66.f*layout.scale;
  label(out,{x,y,content_w,55.f*layout.scale},p.message,p.accepted?good:bad,layout.small_font);
  fill(out,layout.cancel,layout.cancel.contains(pointer_)?Color{33,67,91,255}:row);stroke(out,layout.cancel,border);label(out,layout.cancel,"CANCEL",text_color,layout.body_font,TextAlign::Center);
  const auto can_confirm=p.accepted;fill(out,layout.confirm,can_confirm?(layout.confirm.contains(pointer_)?Color{30,91,72,255}:Color{17,66,56,255}):Color{31,39,49,255});stroke(out,layout.confirm,can_confirm?good:muted);label(out,layout.confirm,"CONFIRM MISSION",can_confirm?text_color:muted,layout.body_font,TextAlign::Center);
}
} // namespace stellar::native_colony_ui
