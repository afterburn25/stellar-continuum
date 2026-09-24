#pragma once
#include "native_menu_hover.hpp"
#include "native_menu_style.hpp"
#include <stellar/engine/localization.hpp>
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace stellar::native_settings {
using namespace stellar::native_map;
enum class Category { General, Audio, Video, Voice, Controls };
struct HubLayout {
  float scale{}; UiRect panel,title,description,back;
  std::array<UiRect,5> categories;
  static HubLayout for_viewport(int width,int height){
    const float s=std::clamp(height/1080.f,.8f,2.5f);
    const float w=std::min(560*s,width-32.f),h=std::min(520*s,height-32.f);
    HubLayout l; l.scale=s;l.panel={(width-w)*.5f,(height-h)*.5f,w,h};
    const float x=l.panel.x+24*s,inner=w-48*s;
    l.title={x,l.panel.y+22*s,inner,34*s};
    l.description={x,l.panel.y+65*s,inner,48*s};
    for(int i=0;i<5;++i)l.categories[i]={x,l.panel.y+(128+57*i)*s,inner,45*s};
    l.back={x,l.panel.y+h-65*s,112*s,40*s};return l;
  }
};
class NativeSettingsHub {
public:
  using Open=std::function<void(Category)>;
  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void set_callbacks(Open open,std::function<bool()> child_visible){open_=std::move(open);child_visible_=std::move(child_visible);}
  void set_localization(const stellar::engine::LocalizationTable* table)noexcept{locale_=table;}
  void open(){hover_feedback_.reset();visible_=true;controls_=false;pointer_={};focus_=-1;}
  void close(){visible_=false;controls_=false;focus_=-1;}
  bool visible()const{return visible_;}
  bool showing_categories()const{return visible_&&(!child_visible_||!child_visible_());}
  int focused()const noexcept{return focus_;}
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label()const{
    if(focus_<0)return {};
    if(controls_||focus_==5)return tr("SETTINGS_BACK","Back");
    constexpr std::array keys{"SETTINGS_NAV_GENERAL","SETTINGS_NAV_AUDIO","SETTINGS_NAV_VIDEO","SETTINGS_NAV_VOICE","SETTINGS_NAV_CONTROLS"};
    constexpr std::array names{"General","Audio","Video","Voice & subtitles","Controls"};
    return focus_<5?tr(keys[static_cast<std::size_t>(focus_)],names[static_cast<std::size_t>(focus_)]):std::string{};
  }
  bool handle(const InputEvent&e,int width,int height){
    if(!showing_categories())return false;
    if(e.type==InputEventType::PointerMove)pointer_=e.position;
    if(e.type==InputEventType::PointerCancelled){pointer_={};return true;}
    const auto l=HubLayout::for_viewport(width,height);
    auto target=stellar::native_menu_audio::hit(e.position,{l.back});
    if(!controls_)for(std::size_t i=0;i<l.categories.size();++i)if(l.categories[i].contains(e.position))target=10+i;
    hover_feedback_.update(e,target);
    if(e.type==InputEventType::EscapePressed){if(controls_){controls_=false;focus_=4;}else close();return true;}
    if(e.type==InputEventType::KeyPressed){
      // SDL_Keycode: Tab/arrows move the focus ring, Return/Space activate.
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const int count=controls_?1:6;
      if(e.key==kHome||e.key==kEnd){
        focus_=e.key==kHome?0:count-1;hover_feedback_.cue(focus_target());return true;
      }
      const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
      const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
      if(fwd||bwd){
        if(focus_<0)focus_=bwd?count-1:0;else focus_=(focus_+(bwd?-1:1)+count)%count;
        hover_feedback_.cue(focus_target());return true;
      }
      if((e.key==kReturn||e.key==kSpace)&&focus_>=0){activate_focus();return true;}
    }
    if(e.type==InputEventType::LeftPressed){focus_=-1;
      if(l.back.contains(e.position)){if(controls_)controls_=false;else close();}
      else if(!controls_)for(int i=0;i<5;++i)if(l.categories[i].contains(e.position)){
        if(i==4)controls_=true;else if(open_)open_(static_cast<Category>(i));break;
      }
    }return true;
  }
  void render(DrawList& out,int width,int height)const{
    if(!showing_categories())return;
    using namespace stellar::native_menu_style;
    const auto l=HubLayout::for_viewport(width,height);const float s=l.scale;
    panel(out,l.panel,s);text(out,l.title,tr(controls_?"SETTINGS_HUB_CONTROLS_TITLE":"SETTINGS_HUB_TITLE",controls_?"CONTROLS":"SETTINGS"),static_cast<int>(29*s));
    text(out,l.description,tr(controls_?"SETTINGS_HUB_CONTROLS_DESC":"SETTINGS_HUB_DESC",controls_?"Explore and manage your campaign with the mouse.":"Choose a category. Your campaign stays paused while settings are open."),static_cast<int>(15*s),muted);
    if(controls_){
      constexpr std::array keys{"SETTINGS_CONTROLS_PAN","SETTINGS_CONTROLS_ZOOM","SETTINGS_CONTROLS_SELECT","SETTINGS_CONTROLS_TRAVEL","SETTINGS_CONTROLS_PAUSE","SETTINGS_CONTROLS_SCREENSHOT","SETTINGS_CONTROLS_ESCAPE"};
      constexpr std::array lines{"Left drag — pan the map","Mouse wheel — zoom","Click — select a world or fleet","Right-click destination — plan fleet travel","Space — pause or resume","F12 — save a screenshot","Escape — back / campaign menu"};
      for(int i=0;i<static_cast<int>(lines.size());++i)text(out,{l.categories[0].x,l.categories[0].y+i*39*s,l.categories[0].width,35*s},tr(keys[i],lines[i]),static_cast<int>(16*s));
    }else{
      constexpr std::array keys{"SETTINGS_NAV_GENERAL","SETTINGS_NAV_AUDIO","SETTINGS_NAV_VIDEO","SETTINGS_NAV_VOICE","SETTINGS_NAV_CONTROLS"};
      constexpr std::array names{"General","Audio","Video","Voice & subtitles","Controls"};
      for(int i=0;i<5;++i)button(out,l.categories[i],tr(keys[i],names[i]),static_cast<int>(18*s),l.categories[i].contains(pointer_)||focus_==i,true,s);
    }
    button(out,l.back,tr("SETTINGS_BACK","< Back"),static_cast<int>(17*s),l.back.contains(pointer_)||focus_==(controls_?0:5),true,s);
  }
private:
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const{
    if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
    return std::string(fallback);
  }
  // Focusable order: categories 0..4 then Back (index 5); the controls
  // help view exposes Back alone (index 0).
  std::uint64_t focus_target()const noexcept{
    if(focus_<0)return 0;const int last=controls_?0:5;
    return focus_==last?1u:10u+static_cast<std::uint64_t>(focus_);
  }
  void activate_focus(){
    const int last=controls_?0:5;
    if(focus_==last){if(controls_){controls_=false;focus_=4;}else close();}
    else if(focus_==4){controls_=true;focus_=-1;}
    else if(open_)open_(static_cast<Category>(focus_));
  }
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  bool visible_{},controls_{};Point pointer_{};int focus_{-1};Open open_;std::function<bool()> child_visible_;
  const stellar::engine::LocalizationTable* locale_{};
};
}
