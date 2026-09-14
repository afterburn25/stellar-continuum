#include "native_startup_workspace.hpp"

#include <algorithm>
#include <utility>

namespace stellar::native_startup_ui {
namespace {
using namespace stellar::native_map;
constexpr Color background{3,9,18,255},panel{8,20,36,252},raised{12,31,54,250};
constexpr Color hover{24,61,94,252},selected{23,67,102,255};
constexpr Color border{91,151,205,235},bright{235,244,255,255};
constexpr Color muted{154,181,211,240},accent{122,230,190,255};
constexpr Color warning{255,190,112,255};
void fill(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(FilledRectangle{r,c});}
void stroke(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(StrokedRectangle{r,c});}
void text(DrawList&o,UiRect r,std::string v,Color c,int p,TextAlign a=TextAlign::Left,FontFace f=FontFace::Interface){const float x=a==TextAlign::Left?r.x:a==TextAlign::Center?r.x+r.width*.5f:r.x+r.width;o.overlay.emplace_back(Text{{x,r.y},std::move(v),c,p,r.width,r,a,f});}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
UiRect intersect(UiRect a,UiRect b){const float x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.f,right-x),std::max(0.f,bottom-y)};}
}

StartupLayout StartupLayout::for_viewport(int width,int height) noexcept{
  const float s=std::clamp(static_cast<float>(height)/900.f,1.f,2.4f);
  const float margin=18.f*s,w=std::min(900.f*s,static_cast<float>(width)-2*margin);
  const float h=std::min(700.f*s,static_cast<float>(height)-2*margin);
  UiRect p{(width-w)*.5f,(height-h)*.5f,w,h};const float pad=22.f*s;
  UiRect title{p.x+pad,p.y+pad,p.width-2*pad,44*s};
  UiRect subtitle{p.x+pad,p.y+72*s,p.width-2*pad,44*s};
  const float bw=std::min(520.f*s,p.width-2*pad),bx=p.x+(p.width-bw)*.5f;
  UiRect n{bx,p.y+150*s,bw,58*s},l{bx,p.y+220*s,bw,58*s},e{bx,p.y+290*s,bw,58*s};
  UiRect list{p.x+pad,p.y+126*s,p.width-2*pad,p.height-220*s};
  UiRect back{p.x+pad,p.y+p.height-66*s,120*s,40*s};
  UiRect primary{p.x+p.width-pad-190*s,p.y+p.height-66*s,190*s,40*s};
  UiRect status{p.x+pad,p.y+130*s,p.width-2*pad,p.height-230*s};
  return {s,static_cast<int>(28*s),static_cast<int>(16*s),static_cast<int>(13*s),p,title,subtitle,n,l,e,list,back,primary,status};
}

void NativeStartupWorkspace::set_setup(stellar::native_setup::NativeNewCampaignSetupView view){setup_.set_view(std::move(view));}
void NativeStartupWorkspace::reset_pointer() noexcept{pointer_={};}
void NativeStartupWorkspace::show_entry() noexcept{screen_=StartupScreen::Entry;selected_slot_.reset();load_scroll_=0;failure_.clear();reset_pointer();}
void NativeStartupWorkspace::set_slots(stellar::native_startup::NativeStartupSaveSlots slots){slots_=std::move(slots);selected_slot_=slots_.slots.empty()?std::nullopt:std::optional<std::size_t>{0};load_scroll_=0;screen_=StartupScreen::LoadSlots;reset_pointer();}
void NativeStartupWorkspace::set_setup_message(std::string message,bool accepted){setup_.set_assessment_message(std::move(message),accepted);}
void NativeStartupWorkspace::set_operation(stellar::native_startup::NativeStartupView view){operation_=std::move(view);screen_=StartupScreen::Busy;reset_pointer();}
void NativeStartupWorkspace::show_failure(std::string value){failure_=std::move(value);screen_=StartupScreen::Failure;reset_pointer();}
bool NativeStartupWorkspace::wants_text_input()const noexcept{return screen_==StartupScreen::Setup&&setup_.seed_focused();}

StartupIntent NativeStartupWorkspace::handle(const InputEvent&e,int width,int height,const TextMeasurer&measure){
  if(e.type==InputEventType::PointerMove)pointer_=e.position;
  if(e.type==InputEventType::PointerCancelled){reset_pointer();if(screen_==StartupScreen::Setup)(void)setup_.handle(e,width,height,measure);return {StartupIntentKind::None,true};}
  const auto l=StartupLayout::for_viewport(width,height);
  if(screen_==StartupScreen::Setup){const auto child=setup_.handle(e,width,height,measure);switch(child.kind){case stellar::native_setup_ui::NativeNewGameIntentKind::Cancel:show_entry();return {StartupIntentKind::Back,true};case stellar::native_setup_ui::NativeNewGameIntentKind::Create:return {StartupIntentKind::Create,true,child.seed_text,child.species_id,child.system_count};default:return {StartupIntentKind::None,child.captured};}}
  if(e.type==InputEventType::EscapePressed){if(screen_==StartupScreen::Busy)return {StartupIntentKind::CancelOperation,true};show_entry();return {StartupIntentKind::Back,true};}
  if(screen_==StartupScreen::LoadSlots&&e.type==InputEventType::Wheel&&l.list.contains(e.position)){
    const float pitch=46.f*l.scale;
    const float maximum=std::max(0.f,pitch*static_cast<float>(slots_.slots.size())-l.list.height);
    load_scroll_=std::clamp(load_scroll_-e.wheel_y*pitch*3.f,0.f,maximum);
    return {StartupIntentKind::None,true};
  }
  if(e.type!=InputEventType::LeftPressed)return {StartupIntentKind::None,l.panel.contains(e.position)};
  if(screen_==StartupScreen::Entry){if(l.new_campaign.contains(e.position)){screen_=StartupScreen::Setup;return {StartupIntentKind::OpenSetup,true};}if(l.load_campaign.contains(e.position))return {StartupIntentKind::OpenLoad,true};if(l.exit.contains(e.position))return {StartupIntentKind::Exit,true};}
  else if(screen_==StartupScreen::LoadSlots){if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}const float pitch=46.f*l.scale;if(l.list.contains(e.position))for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*l.scale};const auto visible=intersect(row,l.list);if(visible.width>0&&visible.height>0&&visible.contains(e.position)){selected_slot_=i;return {StartupIntentKind::None,true};}}if(l.primary.contains(e.position)&&selected_slot_)return {StartupIntentKind::LoadSelected,true,{},{},0,slots_.slots[*selected_slot_].path};}
  else if(screen_==StartupScreen::Busy&&l.back.contains(e.position))return {StartupIntentKind::CancelOperation,true};
  else if(screen_==StartupScreen::Failure&&l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}
  return {StartupIntentKind::None,l.panel.contains(e.position)};
}

void NativeStartupWorkspace::render(DrawList&out,int width,int height,const TextMeasurer&measure,const PortraitProvider*portraits)const{
  if(screen_==StartupScreen::Setup){setup_.render(out,width,height,measure,portraits);return;}
  const auto l=StartupLayout::for_viewport(width,height);const float s=l.scale;
  fill(out,{0,0,static_cast<float>(width),static_cast<float>(height)},background);fill(out,l.panel,panel);stroke(out,l.panel,border);
  text(out,l.title,"STELLAR CONTINUUM",bright,l.heading_font,TextAlign::Center,FontFace::Heading);
  if(screen_==StartupScreen::Entry){text(out,l.subtitle,"Choose how to begin your native campaign.",muted,l.body_font,TextAlign::Center);for(const auto&item:{std::pair{l.new_campaign,"NEW CAMPAIGN"},std::pair{l.load_campaign,"LOAD CAMPAIGN"},std::pair{l.exit,"EXIT TO WINDOWS"}}){fill(out,item.first,item.first.contains(pointer_)?hover:raised);stroke(out,item.first,border);text(out,item.first,item.second,bright,l.body_font,TextAlign::Center);}return;}
  if(screen_==StartupScreen::LoadSlots){text(out,l.subtitle,"SELECT A NATIVE CAMPAIGN SAVE",bright,l.body_font);if(!slots_.error.empty())text(out,l.list,slots_.error,warning,l.body_font);else if(slots_.slots.empty())text(out,l.list,"No native campaign saves are available.",muted,l.body_font);else{const float pitch=46.f*s;for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*s};const auto visible=intersect(row,l.list);if(visible.width<=0||visible.height<=0)continue;fill(out,visible,selected_slot_==i?selected:raised);stroke(out,visible,selected_slot_==i?accent:border);UiRect label{row.x+10*s,row.y+11*s,row.width-20*s,row.height-12*s};const auto clip=intersect(label,l.list);if(clip.width>0&&clip.height>0)out.overlay.emplace_back(Text{{label.x,label.y},slots_.slots[i].filename,bright,l.body_font,label.width,clip,TextAlign::Left,FontFace::Interface});}}fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,"BACK",bright,l.body_font,TextAlign::Center);fill(out,l.primary,selected_slot_?selected:raised);stroke(out,l.primary,selected_slot_?accent:muted);text(out,l.primary,"LOAD SELECTED",selected_slot_?bright:muted,l.body_font,TextAlign::Center);return;}
  if(screen_==StartupScreen::Busy){text(out,l.subtitle,"PREPARING CAMPAIGN",bright,l.body_font,TextAlign::Center);text(out,l.status,operation_.status,muted,l.body_font,TextAlign::Center);if(operation_.determinate_progress){UiRect track{l.status.x+80*s,l.status.y+80*s,l.status.width-160*s,12*s};fill(out,track,raised);fill(out,{track.x,track.y,track.width*static_cast<float>(std::clamp(*operation_.determinate_progress,0.,1.)),track.height},accent);}else text(out,{l.status.x,l.status.y+80*s,l.status.width,24*s},"ACTIVE WORK · progress is indeterminate",accent,l.small_font,TextAlign::Center);fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,operation_.worker_running?"CANCEL":"BACK",bright,l.body_font,TextAlign::Center);return;}
  text(out,l.subtitle,"STARTUP COULD NOT COMPLETE",warning,l.body_font,TextAlign::Center);text(out,l.status,failure_,warning,l.body_font,TextAlign::Center);fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,"BACK",bright,l.body_font,TextAlign::Center);
}
} // namespace stellar::native_startup_ui
