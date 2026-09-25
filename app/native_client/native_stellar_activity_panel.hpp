#pragma once
#include "native_menu_style.hpp"
#include <stellar/engine/accessibility.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/stellar_activity.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numbers>
#include <utility>
namespace stellar::native_map {
class StellarActivityPanel {
public:
 void open(stellar::core::CampaignFrame& frame,std::optional<int> selected){visible_=true;ring_=-1;system_=selected.value_or(-1);event_=0;ensure_target(frame);}
 void close(){visible_=false;ring_=-1;}
 bool visible()const{return visible_;}
 // Keyboard-focus contract: the twenty-four control buttons ring in (y,x)
 // order and Return/Space replay the same dispatch a pointer press takes
 // (activate_button runs the identical command switch). Escape releases
 // the ring before closing.
 [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
 [[nodiscard]] int focus()const noexcept{return ring_;}
 [[nodiscard]] std::string focused_label(int,int)const{
  return ring_>=0&&ring_<static_cast<int>(labels.size())?std::string(labels[static_cast<std::size_t>(ring_)]):std::string{};
 }
 [[nodiscard]] std::optional<UiRect> focused_bounds(int w,int h)const{
  return ring_>=0&&ring_<static_cast<int>(labels.size())?std::optional<UiRect>{button(layout(w,h),ring_)}:std::nullopt;
 }
 [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int,int)const{
  return ring_>=0&&ring_<static_cast<int>(labels.size())?stellar::engine::AnnouncementControl::Button:stellar::engine::AnnouncementControl::Custom;
 }
 std::optional<int> take_focus(){return std::exchange(focus_,{});}
 bool handle(const InputEvent& input,int width,int height,stellar::core::CampaignFrame& frame){
  using namespace stellar::core;if(!visible_)return false;
  if(input.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}close();return true;}
  if(input.type==InputEventType::PointerCancelled){ring_=-1;return true;}
  if(input.type==InputEventType::KeyPressed&&input.key){
   constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
   constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
   constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
   const int count=static_cast<int>(labels.size());
   const bool fwd=(input.key==kTab&&!input.shift)||input.key==kRight||input.key==kDown;
   const bool bwd=(input.key==kTab&&input.shift)||input.key==kLeft||input.key==kUp;
   if(fwd||bwd){if(ring_<0)ring_=bwd?count-1:0;else ring_=(ring_+(fwd?1:count-1))%count;return true;}
   if(input.key==kHome||input.key==kEnd){ring_=input.key==kHome?0:count-1;return true;}
   if((input.key==kReturn||input.key==kSpace)&&ring_>=0){activate_button(ring_,frame);return true;}
  }
  const auto l=layout(width,height);if(input.type!=InputEventType::LeftPressed)return l.panel.contains(input.position);
  ring_=-1;
  int hit=-1;for(int i=0;i<static_cast<int>(labels.size());++i)if(button(l,i).contains(input.position))hit=i;
  if(hit<0)return l.panel.contains(input.position);
  activate_button(hit,frame);
  return true;
 }
 void activate_button(int hit,stellar::core::CampaignFrame& frame){
  using namespace stellar::core;
  try{
   auto& world=frame.runtime().world().campaign();const double day=frame.runtime().stellar_activity_day();auto* s=target(world);if(!s)return;
   auto& state=s->stellar_activity->at(component_);StellarActivityCommand cmd;cmd.system_id=system_;cmd.component=component_;cmd.event_id=event_;cmd.variant=variant_;cmd.magnitude=magnitude_;cmd.latitude=latitude_;cmd.longitude=longitude_;cmd.orientation=orientation_;
   if(hit<5){cmd.action=StellarActivityAction::Force;cmd.type=static_cast<StellarEruptionType>(hit);cmd.randomize_position=true;event_=apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),day,cmd);
    const auto& eruption=state.events.back();latitude_=eruption.latitude;longitude_=eruption.longitude;orientation_=eruption.orientation;status_="New eruption at a random surface location.";}
   else if(hit==5){cmd.action=StellarActivityAction::SetLevel;cmd.level=static_cast<StellarActivityLevel>((static_cast<int>(state.profile.level)+1)%5);(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),day,cmd);}
   else if(hit==6||hit==7){cycle_class(frame,hit==6?-1:1);}
   else if(hit==8||hit==9){variant_=(variant_+(hit==8?11:1))%12;cmd.variant=variant_;cmd.action=StellarActivityAction::SetVariant;apply_if_event(frame,cmd);}
   else if(hit==10||hit==11){magnitude_=std::clamp(magnitude_+(hit==10?-.25:.25),.25,3.);cmd.magnitude=magnitude_;cmd.action=StellarActivityAction::SetMagnitude;apply_if_event(frame,cmd);}
   else if(hit==12){cmd.action=StellarActivityAction::TogglePause;apply_if_event(frame,cmd);}
   else if(hit==13){cmd.action=StellarActivityAction::Restart;apply_if_event(frame,cmd);}
   else if(hit==14||hit==15){fraction_=std::clamp(fraction_+(hit==14?-.05:.05),0.,1.);cmd.action=StellarActivityAction::Scrub;cmd.fraction=fraction_;apply_if_event(frame,cmd);}
   else if(hit==16||hit==17){longitude_=std::remainder(longitude_+(hit==16?-.2:.2),2*std::numbers::pi);cmd.action=StellarActivityAction::MoveRegion;cmd.longitude=longitude_;apply_if_event(frame,cmd);}
   else if(hit==18){latitude_=latitude_>.9?-.9:latitude_+.3;cmd.action=StellarActivityAction::MoveRegion;cmd.latitude=latitude_;apply_if_event(frame,cmd);}
   else if(hit==19){orientation_=std::remainder(orientation_+.3,2*std::numbers::pi);cmd.action=StellarActivityAction::MoveRegion;cmd.orientation=orientation_;apply_if_event(frame,cmd);}
   else if(hit==20){cmd.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),day,cmd);event_=0;frame.set_developer_speed(25);frame.clock().resume();status_="Game at 25x; natural flare timing stays at 1x.";}
   else if(hit==21){coverage(frame);}
   else if(hit==22){component_=(component_+1)%static_cast<int>(s->stellar_activity->size());event_=0;}
   else close();
  }catch(const std::exception& e){status_=e.what();}
 }
 void render(DrawList& out,int width,int height,stellar::core::CampaignFrame& frame)const{
  using namespace stellar::core;if(!visible_)return;const auto l=layout(width,height);
  native_menu_style::panel(out,l.panel,l.scale);const int font=std::max(11,static_cast<int>(14*l.scale));
  out.overlay.emplace_back(Text{{l.panel.x+14*l.scale,l.panel.y+12*l.scale},"STELLAR ACTIVITY",{144,220,249,255},font+3});
  std::ostringstream text;text<<std::fixed<<std::setprecision(2);
  const auto& world=frame.runtime().world().campaign();const auto* s=target(world);
  if(s){const auto& a=s->stellar_activity->at(component_);const auto& p=stellar_host_physics(*s,component_);const auto day=frame.runtime().stellar_activity_day();
   int active=0;for(const auto& e:a.events)active+=!stellar_eruption_sample(e,day).finished;
   text<<s->name<<" / "<<system_<<"  component "<<component_+1<<"  "<<eruption_spectral_name(a.profile.spectral)<<"\nAge "<<p.age_myr<<" Myr  Rotation "<<a.profile.rotation_days<<" d"<<(a.profile.inferred_rotation?" (estimated)":"")<<"\n"<<stellar_activity_name(a.profile.level)<<" / "<<a.profile.score<<"  Rate "<<stellar_eruption_rate(a.profile)<<" / activity day (fixed 1x)\nActive "<<active<<"  Natural P/F/M/S/C ";
   for(auto count:a.generated_counts)text<<count<<" ";text<<"\nSelected variant "<<variant_+1<<"  Magnitude "<<magnitude_;
   auto e=std::ranges::find(a.events,event_,&StellarEruptionEvent::id);if(e==a.events.end()&&!a.events.empty())e=std::prev(a.events.end());
   if(e!=a.events.end()){auto t=stellar_eruption_sample(*e,day);text<<"\nEvent "<<e->id<<"  "<<stellar_eruption_name(e->type)<<"\nStage "<<t.stage+1<<"  "<<t.fraction*100<<"%  "<<(e->paused?"paused":"live")<<"\nElapsed "<<t.elapsed*1440<<" min / left "<<t.remaining*1440<<" min\nLat "<<e->latitude<<" / Long "<<e->longitude<<" / Angle "<<e->orientation<<"\nCME "<<(e->cme_associated?"associated":"no")<<"  chance "<<e->cme_probability*100<<"%";}
  }
  out.overlay.emplace_back(Text{{l.panel.x+14*l.scale,l.panel.y+42*l.scale},text.str(),{223,233,243,255},font,l.panel.width-28*l.scale,UiRect{l.panel.x,l.panel.y+40*l.scale,l.panel.width,244*l.scale}});
  for(int i=0;i<static_cast<int>(labels.size());++i)native_menu_style::button(out,button(l,i),std::string(labels[i]),font,false,true,l.scale);
  if(ring_>=0&&ring_<static_cast<int>(labels.size())){
   const auto r=button(l,ring_);
   const UiRect outer{r.x-3*l.scale,r.y-3*l.scale,r.width+6*l.scale,r.height+6*l.scale};
   out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
   out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
  }
  out.overlay.emplace_back(Text{{l.panel.x+14*l.scale,l.panel.y+710*l.scale},status_,{165,213,204,255},font,l.panel.width-28*l.scale,l.panel});
 }
private:
 static constexpr std::array<std::string_view,24> labels{"PROMINENCE","NORMAL FLARE","MAJOR FLARE","SUPERFLARE","CME","CYCLE ACTIVITY","PREVIOUS CLASS","NEXT CLASS","PREVIOUS VARIANT","NEXT VARIANT","MAGNITUDE −","MAGNITUDE +","PAUSE / RESUME","RESTART EVENT","TIMELINE −5%","TIMELINE +5%","LONGITUDE −","LONGITUDE +","MOVE LATITUDE","ROTATE REGION","NATURAL QA · 25X","COVERAGE · NEXT","NEXT COMPONENT","CLOSE"};
 struct Layout{UiRect panel;float scale;};
 static Layout layout(int width,int height){const float scale=std::min({1.5f,width/1300.f,height/800.f});return {{width-440*scale-12,40*scale,440*scale,750*scale},scale};}
 static UiRect button(Layout l,int index){return {l.panel.x+(14+(index%2)*210)*l.scale,l.panel.y+(286+(index/2)*34)*l.scale,202*l.scale,29*l.scale};}
 template<class World> auto target(World& world)const -> decltype(&world.systems.front()) {auto i=std::ranges::find(world.systems,system_,&stellar::core::StellarSystem::id);return i==world.systems.end()||!i->stellar_activity?nullptr:&*i;}
 void ensure_target(stellar::core::CampaignFrame& frame){auto& world=frame.runtime().world().campaign();auto* s=target(world);if(!s||s->stellar_activity->front().profile.spectral==stellar::core::EruptionSpectralClass::Unsupported){for(auto& candidate:world.systems)if(candidate.stellar_activity&&candidate.stellar_activity->front().profile.spectral==stellar::core::EruptionSpectralClass::G){system_=candidate.id;break;}}component_=0;focus_=system_;}
 void cycle_class(stellar::core::CampaignFrame& frame,int direction){using namespace stellar::core;auto& world=frame.runtime().world().campaign();auto* s=target(world);if(!s)return;const int wanted=(static_cast<int>(s->stellar_activity->at(component_).profile.spectral)+direction+7)%7;
  for(auto& candidate:world.systems)if(candidate.stellar_activity)for(int c=0;c<static_cast<int>(candidate.stellar_activity->size());++c)if(static_cast<int>(candidate.stellar_activity->at(c).profile.spectral)==wanted){system_=candidate.id;component_=c;event_=0;focus_=system_;return;}status_="This campaign has no star of the next class.";
 }
 void apply_if_event(stellar::core::CampaignFrame& frame,stellar::core::StellarActivityCommand cmd){if(!event_)return;(void)stellar::core::apply_developer_stellar_activity(frame.runtime().world().campaign(),frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);}
 void coverage(stellar::core::CampaignFrame& frame){using namespace stellar::core;const int c=coverage_/60,kind=(coverage_/12)%5;variant_=coverage_%12;
  auto& world=frame.runtime().world().campaign();for(const auto& s:world.systems)if(s.stellar_activity&&static_cast<int>(s.stellar_activity->front().profile.spectral)==c){system_=s.id;component_=0;break;}
  auto* s=target(world);if(!s||static_cast<int>(s->stellar_activity->front().profile.spectral)!=c){status_="Coverage requires an O/B/A/F/G/K/M developer coverage galaxy.";return;}
  StellarActivityCommand cmd;cmd.system_id=system_;cmd.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);
  cmd.action=StellarActivityAction::Force;cmd.type=static_cast<StellarEruptionType>(kind);cmd.variant=variant_;cmd.longitude=1.35;cmd.orientation=1.57;cmd.magnitude=1;
  event_=apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);cmd.event_id=event_;cmd.action=StellarActivityAction::Scrub;cmd.fraction=.35;apply_if_event(frame,cmd);
  focus_=system_;status_="Coverage "+std::to_string(coverage_+1)+" / 420. Next advances type, variant and class.";coverage_=(coverage_+1)%420;
 }
 bool visible_{};int system_{-1},component_{},variant_{},coverage_{},ring_{-1};std::uint64_t event_{};double magnitude_{1},fraction_{.35},latitude_{},longitude_{1.35},orientation_{1.57};std::optional<int> focus_;std::string status_="Tools change only this isolated developer campaign.";
};
}
