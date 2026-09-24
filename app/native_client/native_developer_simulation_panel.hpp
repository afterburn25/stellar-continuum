#pragma once
#include "native_menu_style.hpp"
#include "native_ui_layout.hpp"
#include "native_campaign_calendar.hpp"
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/accessibility.hpp>
#include <array>
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stellar::native_map {
// Presentation of the authoritative CampaignFrame clock. No second simulation
// loop, resource state or research state lives in this workspace.
class NativeDeveloperSimulationPanel {
public:
  void toggle() noexcept {visible_=!visible_;pressed_=-1;ring_=-1;}
  void close() noexcept {visible_=false;pressed_=-1;ring_=-1;step_requested_=false;index_requested_=false;planet_index_requested_=false;export_requested_=false;diagnostics_requested_=false;empires_requested_=false;reveal_requested_=false;}
  bool visible()const noexcept{return visible_;}
  // Keyboard-focus contract: every rendered, enabled button rings in
  // (y,x) order; disabled controls (ADVANCE ONE TICK without a pending
  // step, EXPORT while busy) are skipped exactly like pointer input.
  // Return/Space replay the same hit dispatch a matched press/release
  // takes, and Escape releases the ring before closing.
  [[nodiscard]] bool wants_keyboard_focus()const noexcept{return visible_&&ring_>=0;}
  [[nodiscard]] int focus()const noexcept{return visible_?ring_:-1;}
  [[nodiscard]] std::string focused_label(int width,int height,stellar::core::CampaignFrame &frame)const{
    const auto targets=focusables(layout(width,height),frame);
    return ring_>=0&&ring_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(ring_)].label:std::string{};
  }
  [[nodiscard]] std::optional<UiRect> focused_bounds(int width,int height,stellar::core::CampaignFrame &frame)const{
    const auto targets=focusables(layout(width,height),frame);
    return ring_>=0&&ring_<static_cast<int>(targets.size())?std::optional<UiRect>{targets[static_cast<std::size_t>(ring_)].rect}:std::nullopt;
  }
  [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int width,int height,stellar::core::CampaignFrame &frame)const{
    const auto targets=focusables(layout(width,height),frame);
    return ring_>=0&&ring_<static_cast<int>(targets.size())?stellar::engine::AnnouncementControl::Button:stellar::engine::AnnouncementControl::Custom;
  }
  bool take_stellar_request() noexcept {return std::exchange(stellar_requested_,false);}
  bool take_empires_request() noexcept {return std::exchange(empires_requested_,false);}
  bool take_reveal_request() noexcept {return std::exchange(reveal_requested_,false);}
  bool take_step_request() noexcept {return std::exchange(step_requested_,false);}
  void set_export_busy(bool value) noexcept {export_busy_=value;}
  bool take_diagnostics_request() noexcept {return std::exchange(diagnostics_requested_,false);}
  bool take_export_request() noexcept {return std::exchange(export_requested_,false);}
  bool take_index_request() noexcept {return std::exchange(index_requested_,false);}
  bool take_planet_index_request() noexcept {return std::exchange(planet_index_requested_,false);}
  bool handle(const InputEvent &event,int width,int height,stellar::core::CampaignFrame &frame){
    if(!frame.runtime().world().campaign().developer_provenance)return false;
    if(!visible_){
      const auto banner=banner_rect(width,height);pointer_=event.position;
      if(event.type==InputEventType::PointerCancelled){pressed_=-1;return false;}
      if(event.type==InputEventType::LeftPressed&&banner.contains(event.position)){pressed_=99;return true;}
      if(event.type==InputEventType::LeftReleased&&std::exchange(pressed_,-1)==99){
        if(banner.contains(event.position))visible_=true;return true;
      }
      return false;
    }
    if(event.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}close();return true;}
    if(event.type==InputEventType::PointerCancelled){pressed_=-1;ring_=-1;return false;}
    const auto l=layout(width,height);pointer_=event.position;
    if(event.type==InputEventType::KeyPressed&&event.key){
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const auto targets=focusables(l,frame);const int count=static_cast<int>(targets.size());
      if(count){
        const bool fwd=(event.key==kTab&&!event.shift)||event.key==kRight||event.key==kDown;
        const bool bwd=(event.key==kTab&&event.shift)||event.key==kLeft||event.key==kUp;
        const bool home=event.key==kHome,end=event.key==kEnd;
        if(fwd||bwd||home||end){
          if(ring_<0)ring_=bwd||end?count-1:0;
          else if(fwd)ring_=(ring_+1)%count;
          else if(bwd)ring_=(ring_+count-1)%count;
          else if(home)ring_=0;
          else ring_=count-1;
          return true;
        }
        if((event.key==kReturn||event.key==kSpace)&&ring_>=0){activate(targets[static_cast<std::size_t>(ring_)].hit,frame);return true;}
      }
    }
    int hit=-1;for(std::size_t i=0;i<l.buttons.size();++i)if(l.buttons[i].contains(event.position))hit=static_cast<int>(i);
    if(event.type==InputEventType::LeftPressed){pressed_=hit;ring_=-1;return l.panel.contains(event.position);}
    if(event.type==InputEventType::LeftReleased){
      const int selected=std::exchange(pressed_,-1);
      if(selected>=0&&selected==hit){activate(hit,frame);return true;}
    }
    return l.panel.contains(event.position);
  }
  void render(DrawList &out,int width,int height,stellar::core::CampaignFrame &frame) const {
    const auto &provenance=frame.runtime().world().campaign().developer_provenance;
    if(!provenance)return;
    const float s=std::clamp(std::min(width/1920.f,height/1080.f),.67f,2.f);
    const auto label=[&](UiRect r,std::string value,Color color,int size){out.overlay.emplace_back(Text{{r.x,r.y},std::move(value),color,size,r.width,r});};
    const auto banner=banner_rect(width,height);
    const int banner_font=std::max(12,static_cast<int>(16*s));
    stellar::engine::ui_skin::control(out,banner,banner.contains(pointer_),false,true,s);
    native_menu_style::text(out,{banner.x+8*s,banner.y+(banner.height-banner_font)*.5f,banner.width-16*s,static_cast<float>(banner_font)+1.f},
        provenance->player_ai_control?"DEV CONTROLS · AI CONTROL":"DEV CONTROLS · ISOLATED CAMPAIGN",banner_font);
    if(!visible_)return;
    const auto l=layout(width,height);
    out.overlay.emplace_back(FilledRectangle{l.panel,{3,14,25,250}});
    out.overlay.emplace_back(StrokedRectangle{l.panel,{67,153,193,255}});
    const int font=std::max(13,static_cast<int>(17*s));
    label({l.panel.x+18*s,l.panel.y+14*s,l.panel.width-36*s,30*s},"DEVELOPER · SIMULATION",{129,218,249,255},font+3);
    label({l.panel.x+18*s,l.panel.y+55*s,l.panel.width-36*s,27*s},
        "Fixed steps: "+std::to_string(tactical(frame)?provenance->simulation.tactical_completed_ticks:provenance->simulation.completed_ticks)+"   Pending: "+std::to_string(frame.developer_ticks_behind()),{212,231,241,255},font);
    label({l.panel.x+18*s,l.panel.y+86*s,l.panel.width-36*s,26*s},
        "Requested speed: "+std::to_string(provenance->simulation.speed)+(tactical(frame)?"× · 0.1 combat second per tick":"× · 1x = 1 hour/sec"),{155,189,207,255},font);
    for(std::size_t i=0;i<speeds.size();++i){
      native_menu_style::button(out,l.buttons[i],button_label(i,frame),font,l.buttons[i].contains(pointer_),true,s);
      if(provenance->simulation.speed==speeds[i])out.overlay.emplace_back(StrokedRectangle{l.buttons[i],{99,222,255,255}});
    }
    native_menu_style::button(out,l.buttons[5],button_label(5,frame),font,l.buttons[5].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[6],button_label(6,frame),font,l.buttons[6].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[7],button_label(7,frame),font,l.buttons[7].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[8],button_label(8,frame),font,l.buttons[8].contains(pointer_),frame.can_step_developer(),s);
    native_menu_style::button(out,l.buttons[9],button_label(9,frame),font,l.buttons[9].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[14],button_label(14,frame),font,l.buttons[14].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[10],button_label(10,frame),font,l.buttons[10].contains(pointer_),!export_busy_,s);
    native_menu_style::button(out,l.buttons[11],button_label(11,frame),font,l.buttons[11].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[12],button_label(12,frame),font,l.buttons[12].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[15],button_label(15,frame),font,l.buttons[15].contains(pointer_),true,s);
    native_menu_style::button(out,l.buttons[13],button_label(13,frame),font,l.buttons[13].contains(pointer_),true,s);
    label({l.panel.x+18*s,l.panel.y+576*s,l.panel.width-36*s,42*s},
        frame.developer_ticks_behind()?"Simulation is behind. Pending ticks are retained.":"Simulation caught up. Player saves remain separate.",
        frame.developer_ticks_behind()?Color{247,190,92,255}:Color{139,210,190,255},font);
    if(ring_>=0){
      const auto targets=focusables(l,frame);
      if(ring_<static_cast<int>(targets.size())){
        const auto &r=targets[static_cast<std::size_t>(ring_)].rect;
        const UiRect outer{r.x-3*s,r.y-3*s,r.width+6*s,r.height+6*s};
        out.overlay.emplace_back(StrokedRectangle{outer,{99,222,255,255}});
        out.overlay.emplace_back(StrokedRectangle{r,{99,222,255,255}});
      }
    }
  }
private:
  static UiRect banner_rect(int w,int h){
    const auto ui=NativeUiLayout::for_viewport(w,h);
    const float s=std::clamp(std::min(w/1920.f,h/1080.f),.67f,2.f);
    const float right=std::max(0.f,ui.day_text.x-12.f*ui.scale);
    const float left=std::min(580.f*ui.scale,right);
    const float width=std::min(420.f*s,right-left);
    return {std::clamp(w*.5f-width*.5f,left,right-width),4.f*ui.scale,width,28.f*ui.scale};
  }
  static bool tactical(stellar::core::CampaignFrame &frame){const auto &battle=frame.runtime().world().campaign().active_combat_encounter;return battle&&!battle->reconciled;}
  static bool paused(stellar::core::CampaignFrame &frame){return tactical(frame)?frame.tactical_clock().speed_multiplier()==0.:frame.clock().speed()==stellar::core::StrategicSpeed::Paused;}
  static constexpr std::array<std::uint32_t,5> speeds{1,2,5,10,25};
  struct Layout {UiRect panel;std::array<UiRect,16> buttons;};
  std::string button_label(std::size_t i,stellar::core::CampaignFrame &frame)const{
    const auto &provenance=frame.runtime().world().campaign().developer_provenance;
    if(i<5)return std::to_string(speeds[i])+"×";
    switch(i){
      case 5:return paused(frame)?"RESUME":"PAUSE";
      case 6:return "CLOSE";
      case 7:return provenance->player_ai_control?"PLAYER EMPIRE: AI CONTROL":"PLAYER EMPIRE: HUMAN CONTROL";
      case 8:return "ADVANCE ONE TICK";
      case 9:return "CELESTIAL INDEX";
      case 10:return export_busy_?"EXPORTING DIAGNOSTIC BUNDLE...":"EXPORT DIAGNOSTIC BUNDLE";
      case 11:return "PERFORMANCE & DIAGNOSTICS";
      case 12:return "EMPIRE MONITOR";
      case 13:return provenance->full_exploration?"ENTIRE GALAXY REVEALED":"REVEAL ENTIRE GALAXY";
      case 14:return "PLANET INDEX";
      case 15:return "STELLAR ACTIVITY";
      default:return {};
    }
  }
  struct FocusTarget{UiRect rect;int hit{-1};std::string label;};
  std::vector<FocusTarget> focusables(const Layout &l,stellar::core::CampaignFrame &frame)const{
    std::vector<FocusTarget> out;
    for(std::size_t i=0;i<l.buttons.size();++i){
      if(i==8&&!frame.can_step_developer())continue;
      if(i==10&&export_busy_)continue;
      out.push_back({l.buttons[i],static_cast<int>(i),button_label(i,frame)});
    }
    std::ranges::sort(out,[](const FocusTarget&a,const FocusTarget&b){return a.rect.y==b.rect.y?a.rect.x<b.rect.x:a.rect.y<b.rect.y;});
    return out;
  }
  void activate(int hit,stellar::core::CampaignFrame &frame){
    if(hit<5)frame.set_developer_speed(speeds[static_cast<std::size_t>(hit)]);
    else if(hit==5){
      if(tactical(frame))frame.set_tactical_speed(frame.tactical_clock().speed_multiplier()==0.?frame.tactical_resume_speed():0.);
      else if(frame.clock().speed()==stellar::core::StrategicSpeed::Paused)frame.clock().resume();else frame.clock().set_speed(stellar::core::StrategicSpeed::Paused);
    }
    else if(hit==7)stellar::core::set_developer_ai_control(frame.runtime(),
        !frame.runtime().world().campaign().developer_provenance->player_ai_control);
    else if(hit==8){if(frame.can_step_developer())step_requested_=true;}
    else if(hit==12){close();empires_requested_=true;}
    else if(hit==13){reveal_requested_=true;}
    else if(hit==11){close();diagnostics_requested_=true;}
    else if(hit==10){if(!export_busy_)export_requested_=true;}
    else if(hit==9){close();index_requested_=true;}
    else if(hit==14){close();planet_index_requested_=true;}
    else if(hit==15){close();stellar_requested_=true;}
    else close();
  }
  static Layout layout(int width,int height){
    const float s=std::clamp(std::min(width/1920.f,height/1080.f),.67f,2.f);
    const UiRect panel{width*.5f-300*s,height*.5f-315*s,600*s,630*s};Layout l{panel,{}};
    for(int i=0;i<5;++i)l.buttons[i]={panel.x+(18+114*i)*s,panel.y+124*s,108*s,36*s};
    l.buttons[5]={panel.x+18*s,panel.y+174*s,276*s,36*s};
    l.buttons[6]={panel.x+306*s,panel.y+174*s,276*s,36*s};
    l.buttons[7]={panel.x+18*s,panel.y+224*s,564*s,36*s};
    l.buttons[8]={panel.x+18*s,panel.y+274*s,564*s,36*s};
    l.buttons[9]={panel.x+18*s,panel.y+324*s,276*s,36*s};
    l.buttons[14]={panel.x+306*s,panel.y+324*s,276*s,36*s};
    l.buttons[10]={panel.x+18*s,panel.y+374*s,564*s,36*s};
    l.buttons[11]={panel.x+18*s,panel.y+424*s,564*s,36*s};
    l.buttons[12]={panel.x+18*s,panel.y+474*s,276*s,36*s};
    l.buttons[15]={panel.x+306*s,panel.y+474*s,276*s,36*s};
    l.buttons[13]={panel.x+18*s,panel.y+524*s,564*s,36*s};return l;
  }
  bool stellar_requested_{};
  bool visible_{},step_requested_{},index_requested_{},export_requested_{},export_busy_{},diagnostics_requested_{},empires_requested_{},reveal_requested_{},planet_index_requested_{};int pressed_{-1},ring_{-1};Point pointer_{};
};
}
