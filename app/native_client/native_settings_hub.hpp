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
  void open(){hover_feedback_.reset();visible_=true;controls_=false;pointer_={};}
  void close(){visible_=false;controls_=false;}
  bool visible()const{return visible_;}
  bool showing_categories()const{return visible_&&(!child_visible_||!child_visible_());}
  bool handle(const InputEvent&e,int width,int height){
    if(!showing_categories())return false;
    if(e.type==InputEventType::PointerMove)pointer_=e.position;
    if(e.type==InputEventType::PointerCancelled){pointer_={};return true;}
    const auto l=HubLayout::for_viewport(width,height);
    auto target=stellar::native_menu_audio::hit(e.position,{l.back});
    if(!controls_)for(std::size_t i=0;i<l.categories.size();++i)if(l.categories[i].contains(e.position))target=10+i;
    hover_feedback_.update(e,target);
    if(e.type==InputEventType::EscapePressed){if(controls_)controls_=false;else close();return true;}
    if(e.type==InputEventType::LeftPressed){
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
      for(int i=0;i<5;++i)button(out,l.categories[i],tr(keys[i],names[i]),static_cast<int>(18*s),l.categories[i].contains(pointer_),true,s);
    }
    button(out,l.back,tr("SETTINGS_BACK","< Back"),static_cast<int>(17*s),l.back.contains(pointer_),true,s);
  }
private:
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const{
    if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
    return std::string(fallback);
  }
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  bool visible_{},controls_{};Point pointer_{};Open open_;std::function<bool()> child_visible_;
  const stellar::engine::LocalizationTable* locale_{};
};
}
