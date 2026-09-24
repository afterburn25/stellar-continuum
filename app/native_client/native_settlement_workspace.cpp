#include <stellar/engine/native_ui_skin.hpp>
#include "native_campaign_calendar.hpp"
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
std::string kind_name(NativeSettlementMissionKind kind,const stellar::engine::LocalizationTable* locale){
  const std::string_view key=kind==NativeSettlementMissionKind::Colony?"SETTLE_KIND_COLONY":"SETTLE_KIND_OUTPOST";
  const std::string_view fallback=kind==NativeSettlementMissionKind::Colony?"COLONY EXPEDITION":"RESOURCE OUTPOST EXPEDITION";
  if(locale&&locale->contains(key))return std::string(locale->translate(key));
  return std::string(fallback);}
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

std::string NativeSettlementWorkspace::tr(std::string_view key,
                                          std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}
std::string NativeSettlementWorkspace::trf(
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

void NativeSettlementWorkspace::reset_gesture() noexcept { pointer_owned_=false; pressed_=PressTarget::None; press_width_=press_height_=0; }
void NativeSettlementWorkspace::set_preview(NativeSettlementTargetPreview value){reset_gesture();focus_=-1;preview_=std::move(value);}
void NativeSettlementWorkspace::clear()noexcept{preview_.reset();pointer_={};focus_=-1;reset_gesture();}

SettlementWorkspaceCommand NativeSettlementWorkspace::handle(const InputEvent&event,int width,int height){
  if(!preview_)return {};
  pointer_=event.position;
  if(event.type==InputEventType::EscapePressed){clear();return {SettlementWorkspaceCommandKind::Cancel,true};}
  const auto layout=SettlementWorkspaceLayout::for_viewport(width,height);
  if(event.type==InputEventType::PointerCancelled){reset_gesture();return {SettlementWorkspaceCommandKind::None,true};}
  if(pointer_owned_&&(width!=press_width_||height!=press_height_)){reset_gesture();}
  if(event.type==InputEventType::KeyPressed&&event.key){
    // SDL_Keycode: Tab/arrows move the ring, Return/Space replay the click
    // gesture. Confirm is unreachable while the preview rejects the target.
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const int count=preview_->accepted?2:1;
    const bool fwd=(event.key==kTab&&!event.shift)||event.key==kRight||event.key==kDown;
    const bool bwd=(event.key==kTab&&event.shift)||event.key==kLeft||event.key==kUp;
    if(event.key==kHome||event.key==kEnd)focus_=event.key==kHome?0:count-1;
    else if(fwd||bwd)focus_=focus_<0?(bwd?count-1:0):(focus_+(bwd?-1:1)+count)%count;
    else if((event.key==kReturn||event.key==kSpace)&&focus_>=0){
      const auto&rect=focus_==0?layout.cancel:layout.confirm;
      InputEvent press{InputEventType::LeftPressed},release{InputEventType::LeftReleased};
      press.position=release.position={rect.x+rect.width*.5f,rect.y+rect.height*.5f};
      const int keep=focus_;static_cast<void>(handle(press,width,height));
      auto command=handle(release,width,height);
      if(preview_)focus_=keep;return command;
    }
    return {SettlementWorkspaceCommandKind::None,true};
  }
  if(event.type==InputEventType::LeftPressed){
    if(!layout.panel.contains(event.position))return {SettlementWorkspaceCommandKind::None,true};
    focus_=-1;
    pointer_owned_=true;press_width_=width;press_height_=height;
    pressed_=layout.cancel.contains(event.position)?PressTarget::Cancel:
             (layout.confirm.contains(event.position)&&preview_->accepted?PressTarget::Confirm:PressTarget::None);
    return {SettlementWorkspaceCommandKind::None,true};
  }
  if(event.type==InputEventType::PointerMove){
    if(pointer_owned_ && pressed_==PressTarget::Confirm && !layout.confirm.contains(event.position)) pressed_=PressTarget::None;
    if(pointer_owned_ && pressed_==PressTarget::Cancel && !layout.cancel.contains(event.position)) pressed_=PressTarget::None;
    return {SettlementWorkspaceCommandKind::None,true};
  }
  if(event.type==InputEventType::LeftReleased){
    const bool captured=true;
    const auto target=pressed_;
    const bool activate=pointer_owned_&&target!=PressTarget::None&&
      ((target==PressTarget::Cancel&&layout.cancel.contains(event.position))||
       (target==PressTarget::Confirm&&preview_->accepted&&layout.confirm.contains(event.position)));
    reset_gesture();
    if(!activate)return {SettlementWorkspaceCommandKind::None,captured};
    if(target==PressTarget::Cancel){clear();return {SettlementWorkspaceCommandKind::Cancel,true};}
    return {SettlementWorkspaceCommandKind::Confirm,true};
  }
  return {SettlementWorkspaceCommandKind::None,true};
}

void NativeSettlementWorkspace::render(DrawList&out,int width,int height)const{
  if(!preview_)return;
  const auto layout=SettlementWorkspaceLayout::for_viewport(width,height);const auto&p=*preview_;
  fill(out,layout.shade,shade);stellar::engine::ui_skin::surface(out,layout.panel,layout.scale);
  float x=layout.panel.x+20.f*layout.scale,y=layout.panel.y+17.f*layout.scale;
  const float content_w=layout.panel.width-40.f*layout.scale,line=25.f*layout.scale;
  auto add=[&](std::string value,Color color=muted,int size=0,float step=0.f){label(out,{x,y,content_w,line},std::move(value),color,size?size:layout.body_font);y+=step?step:line;};
  add(kind_name(p.kind,locale_),text_color,layout.title_font,38.f*layout.scale);
  add(p.fleet_name+"  |  "+p.personnel_species_name,text_color);
  add(trf("SETTLE_PERSONNEL",{number(p.personnel_millions,3)},"{0}M personnel"),muted,layout.body_font,32.f*layout.scale);
  if(p.candidate){
    add(p.candidate->body_name+"  /  "+p.candidate->system_name,text_color,layout.title_font,36.f*layout.scale);
    add(trf(p.requires_new_authorization?"SETTLE_AUTH_NEW":"SETTLE_AUTH_RETAINED",{p.formatted_authorization},p.requires_new_authorization?"New authorization  {0}":"Retarget authorization retained  ·  New charge  {0}"));
    add(trf("SETTLE_TREASURY",{p.formatted_treasury},"Current treasury  {0}"));
    add(trf("SETTLE_ROUTE",{stellar::core::format_interstellar_metric_primary(p.candidate->reach.route_distance_light_years)},"Route distance  {0}"));
    if(p.candidate->reach.route_system_ids)add(trf("SETTLE_LANES",{std::to_string(p.candidate->reach.route_system_ids->size()>0?p.candidate->reach.route_system_ids->size()-1:0)},"Confirmed lane route  {0} hop(s)"));
    add(tr("SETTLE_ETA_UNKNOWN","Travel duration estimate unavailable"),muted,layout.small_font);
    add(trf("SETTLE_ESTABLISH",{stellar::native_campaign::format_campaign_duration(establishment_days(p.kind))},"Establishment after arrival  {0}"));
    if(p.kind==NativeSettlementMissionKind::Colony){
      add(trf("SETTLE_HABITABILITY",{number(p.candidate->natural_habitability*100.,1),number(p.candidate->unprotected_operational_capacity*100.,1)},"Natural habitability  {0}%  |  Operational capacity  {1}%"));
    }else{
      add(p.candidate->has_confirmed_deposit?p.candidate->deposit_material_name+"  "+p.candidate->deposit_grade:tr("SETTLE_NO_DEPOSIT","No confirmed extractable deposit"),p.candidate->has_confirmed_deposit?good:bad);
    }
  }
  y=layout.confirm.y-66.f*layout.scale;
  label(out,{x,y,content_w,55.f*layout.scale},p.message,p.accepted?good:bad,layout.small_font);
  stellar::engine::ui_skin::control(out,layout.cancel,layout.cancel.contains(pointer_),false,true,layout.scale);label(out,layout.cancel,tr("SETTLE_CANCEL","CANCEL"),text_color,layout.body_font,TextAlign::Center);
  const auto can_confirm=p.accepted;stellar::engine::ui_skin::control(out,layout.confirm,layout.confirm.contains(pointer_),true,can_confirm,layout.scale);label(out,layout.confirm,tr("SETTLE_CONFIRM","CONFIRM MISSION"),can_confirm?text_color:muted,layout.body_font,TextAlign::Center);
  if(focus_>=0)stroke(out,focus_==0?layout.cancel:layout.confirm,{160,210,255,255});
}
} // namespace stellar::native_colony_ui
