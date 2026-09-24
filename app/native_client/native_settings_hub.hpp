#pragma once
#include "native_menu_hover.hpp"
#include "native_menu_style.hpp"
#include <stellar/engine/localization.hpp>
#include <stellar/engine/input_actions.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
  // Borrowed mapper — the Controls view lists this context's Button actions
  // and rebinds them through rebind()/bindings(). Null keeps the static
  // control-reference help view.
  void set_input_mapper(stellar::engine::InputMapper* mapper,std::string context_name="GALAXY"){
    mapper_=mapper;context_name_=std::move(context_name);
  }
  // Invoked after every successful rebind so the owner persists
  // save_contexts() wherever it keeps settings files.
  void set_bindings_persist(std::function<void()> persist){persist_=std::move(persist);}
  // One-shot status note set when a rebind steals a key from another
  // action — the dispatcher announces it (focus stays on the row, so it
  // cannot surface through focused_label change detection).
  [[nodiscard]] std::string take_notice(){auto s=std::move(notice_);notice_.clear();return s;}
  void open(){hover_feedback_.reset();visible_=true;controls_=false;pointer_={};focus_=-1;capture_=-1;notice_.clear();}
  void close(){visible_=false;controls_=false;focus_=-1;capture_=-1;notice_.clear();}
  bool visible()const{return visible_;}
  bool showing_categories()const{return visible_&&(!child_visible_||!child_visible_());}
  int focused()const noexcept{return focus_;}
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label()const{
    const auto rows=control_actions();
    if(capture_>=0&&capture_<static_cast<int>(rows.size()))
      return tr("SETTINGS_CONTROLS_CAPTURE","Press a key or button for")+" "+action_label(rows[static_cast<std::size_t>(capture_)]->name);
    if(focus_<0)return {};
    if(controls_){
      if(focus_==static_cast<int>(rows.size()))return tr("SETTINGS_BACK","Back");
      if(focus_<static_cast<int>(rows.size()))
        return action_label(rows[static_cast<std::size_t>(focus_)]->name)+": "+
               stellar::engine::describe_bindings(mapper_->bindings(rows[static_cast<std::size_t>(focus_)]->name));
      return {};
    }
    if(focus_==5)return tr("SETTINGS_BACK","Back");
    constexpr std::array keys{"SETTINGS_NAV_GENERAL","SETTINGS_NAV_AUDIO","SETTINGS_NAV_VIDEO","SETTINGS_NAV_VOICE","SETTINGS_NAV_CONTROLS"};
    constexpr std::array names{"General","Audio","Video","Voice & subtitles","Controls"};
    return focus_<5?tr(keys[static_cast<std::size_t>(focus_)],names[static_cast<std::size_t>(focus_)]):std::string{};
  }
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<UiRect> focused_bounds(int width,int height)const{
    const auto l=HubLayout::for_viewport(width,height);
    const auto rows=control_actions();
    if(capture_>=0&&capture_<static_cast<int>(rows.size())){
      const auto rects=control_row_rects(l);
      return capture_<static_cast<int>(rects.size())?std::optional<UiRect>{rects[static_cast<std::size_t>(capture_)]}:std::nullopt;
    }
    if(focus_<0)return std::nullopt;
    if(controls_){
      if(focus_==static_cast<int>(rows.size()))return l.back;
      const auto rects=control_row_rects(l);
      return focus_<static_cast<int>(rects.size())?std::optional<UiRect>{rects[static_cast<std::size_t>(focus_)]}:std::nullopt;
    }
    if(focus_==5)return l.back;
    return focus_<5?std::optional<UiRect>{l.categories[static_cast<std::size_t>(focus_)]}:std::nullopt;
  }
  bool handle(const InputEvent&e,int width,int height){
    if(!showing_categories())return false;
    if(e.type==InputEventType::PointerMove)pointer_=e.position;
    if(e.type==InputEventType::PointerCancelled){pointer_={};return true;}
    const auto l=HubLayout::for_viewport(width,height);
    auto target=stellar::native_menu_audio::hit(e.position,{l.back});
    if(!controls_)for(std::size_t i=0;i<l.categories.size();++i)if(l.categories[i].contains(e.position))target=10+i;
    else{const auto rects=control_row_rects(l);for(std::size_t r=0;r<rects.size();++r)if(rects[r].contains(e.position))target=20+r;}
    hover_feedback_.update(e,target);
    if(e.type==InputEventType::EscapePressed){
      if(capture_>=0){capture_=-1;return true;}
      if(controls_){controls_=false;focus_=4;}else close();return true;
    }
    // Capture mode: the next trigger input — non-modifier keypress, gamepad
    // button or right-click — becomes the focused action's primary binding.
    // Left-click and pointer loss cancel; everything else is swallowed.
    if(capture_>=0){
      stellar::engine::InputBinding primary;
      if(e.type==InputEventType::KeyPressed){
        switch(e.key){
          case 0x400000e0u:case 0x400000e1u:case 0x400000e2u:case 0x400000e3u:
          case 0x400000e4u:case 0x400000e5u:case 0x400000e6u:case 0x400000e7u:
            return true; // modifier alone — keep waiting for the trigger key
          default:break;
        }
        primary={stellar::engine::RawInputEvent::Kind::KeyPress,static_cast<int>(e.key)};
        if(e.control)primary.chord_keys.push_back(0x400000e0);
        if(e.shift)primary.chord_keys.push_back(0x400000e1);
        if(e.alt)primary.chord_keys.push_back(0x400000e2);
      }else if(e.type==InputEventType::GamepadPressed){
        primary={stellar::engine::RawInputEvent::Kind::GamepadButton,e.gamepad_button};
      }else if(e.type==InputEventType::RightPressed){
        primary={stellar::engine::RawInputEvent::Kind::MouseButton,3};
      }else{
        if(e.type==InputEventType::LeftPressed)capture_=-1;
        return true;
      }
      apply_capture(primary);
      capture_=-1;hover_feedback_.cue(focus_target());return true;
    }
    if(e.type==InputEventType::KeyPressed){
      // SDL_Keycode: Tab/arrows move the focus ring, Return/Space activate.
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const int count=controls_?control_count():6;
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
    if(e.type==InputEventType::LeftPressed){
      focus_=-1;
      if(l.back.contains(e.position)){if(controls_)controls_=false;else close();}
      else if(controls_){
        const auto rects=control_row_rects(l);
        for(std::size_t i=0;i<rects.size();++i)if(rects[i].contains(e.position)){focus_=static_cast<int>(i);capture_=focus_;break;}
      }
      else for(int i=0;i<5;++i)if(l.categories[i].contains(e.position)){
        if(i==4)controls_=true;else if(open_)open_(static_cast<Category>(i));break;
      }
    }return true;
  }
  void render(DrawList& out,int width,int height)const{
    if(!showing_categories())return;
    using namespace stellar::native_menu_style;
    const auto l=HubLayout::for_viewport(width,height);const float s=l.scale;
    panel(out,l.panel,s);text(out,l.title,tr(controls_?"SETTINGS_HUB_CONTROLS_TITLE":"SETTINGS_HUB_TITLE",controls_?"CONTROLS":"SETTINGS"),static_cast<int>(29*s));
    text(out,l.description,tr(controls_?(control_actions().empty()?"SETTINGS_HUB_CONTROLS_DESC":"SETTINGS_HUB_CONTROLS_REBIND_DESC"):"SETTINGS_HUB_DESC",controls_?(control_actions().empty()?"Explore and manage your campaign with the mouse.":"Activate a command, then press a key, right-click or a pad button to bind. Escape or left-click cancels."):"Choose a category. Your campaign stays paused while settings are open."),static_cast<int>(15*s),muted);
    if(controls_){
      const auto rows=control_actions();
      if(rows.empty()){
        // No input mapper bound — fall back to the static reference card.
        constexpr std::array keys{"SETTINGS_CONTROLS_PAN","SETTINGS_CONTROLS_ZOOM","SETTINGS_CONTROLS_SELECT","SETTINGS_CONTROLS_TRAVEL","SETTINGS_CONTROLS_PAUSE","SETTINGS_CONTROLS_SCREENSHOT","SETTINGS_CONTROLS_ESCAPE"};
        constexpr std::array lines{"Left drag — pan the map","Mouse wheel — zoom","Click — select a world or fleet","Right-click destination — plan fleet travel","Space — pause or resume","F12 — save a screenshot","Escape — back / campaign menu"};
        for(int i=0;i<static_cast<int>(lines.size());++i)text(out,{l.categories[0].x,l.categories[0].y+i*39*s,l.categories[0].width,35*s},tr(keys[i],lines[i]),static_cast<int>(16*s));
      }else{
        const auto rects=control_row_rects(l);
        for(std::size_t i=0;i<rows.size();++i){
          const bool hot=rects[i].contains(pointer_)||focus_==static_cast<int>(i);
          const auto bindings=mapper_->bindings(rows[i]->name);
          const std::string value=capture_==static_cast<int>(i)
              ?tr("SETTINGS_CONTROLS_PRESS_KEY","press a key or button…")
              :stellar::engine::describe_bindings(bindings);
          button(out,rects[i],action_label(rows[i]->name)+" — "+value,static_cast<int>(16*s),hot||capture_==static_cast<int>(i),true,s);
        }
        // Gamepad axes bind in the non-rebindable GALAXY_PAD context — the
        // hint keeps stick camera control discoverable beside the rows.
        const float hint_y=l.categories[0].y+static_cast<float>(rects.size())*26.f*s;
        if(hint_y+22.f*s<l.back.y-26.f*s)
          text(out,{l.categories[0].x,hint_y,l.categories[0].width,22.f*s},
               tr("SETTINGS_CONTROLS_PAD","Left stick — pan the map · right stick — zoom"),static_cast<int>(13*s),muted);
        if(!notice_.empty())
          text(out,{l.categories[0].x,l.back.y-26.f*s,l.categories[0].width,22.f*s},notice_,static_cast<int>(14*s),muted);
      }
    }else{
      constexpr std::array keys{"SETTINGS_NAV_GENERAL","SETTINGS_NAV_AUDIO","SETTINGS_NAV_VIDEO","SETTINGS_NAV_VOICE","SETTINGS_NAV_CONTROLS"};
      constexpr std::array names{"General","Audio","Video","Voice & subtitles","Controls"};
      for(int i=0;i<5;++i)button(out,l.categories[i],tr(keys[i],names[i]),static_cast<int>(18*s),l.categories[i].contains(pointer_)||focus_==i,true,s);
    }
    button(out,l.back,tr("SETTINGS_BACK","< Back"),static_cast<int>(17*s),l.back.contains(pointer_)||focus_==(controls_?control_count()-1:5),true,s);
  }
private:
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const{
    if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
    return std::string(fallback);
  }
  // Button actions of the configured context — the rebindable rows. Empty
  // without a mapper (the controls view then shows the static help card).
  [[nodiscard]] std::vector<const stellar::engine::InputAction*> control_actions()const{
    std::vector<const stellar::engine::InputAction*> rows;
    if(mapper_)if(const auto* context=mapper_->context(context_name_))
      for(const auto& action:context->actions)
        if(action.type==stellar::engine::InputAction::Type::Button)rows.push_back(&action);
    return rows;
  }
  [[nodiscard]] int control_count()const{return static_cast<int>(control_actions().size())+1;}
  // Row rects inside the controls view — packed under the description and
  // clipped so the last row never overlaps Back.
  [[nodiscard]] std::vector<UiRect> control_row_rects(const HubLayout& l)const{
    const auto rows=control_actions();
    const float s=l.scale,row_h=26.f*s;
    const int visible=std::min<int>(static_cast<int>(rows.size()),
        std::max(0,static_cast<int>((l.back.y-8.f*s-l.categories[0].y)/row_h)));
    std::vector<UiRect> rects(static_cast<std::size_t>(visible));
    for(int i=0;i<visible;++i)rects[static_cast<std::size_t>(i)]={l.categories[0].x,l.categories[0].y+i*row_h,l.categories[0].width,row_h-3.f*s};
    return rects;
  }
  // "toggle_pause" → SETTINGS_ACTION_TOGGLE_PAUSE, fallback "Toggle pause".
  [[nodiscard]] std::string action_label(std::string_view name)const{
    std::string key="SETTINGS_ACTION_",fallback;
    for(const char ch:name){key+=static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));fallback+=ch=='_'?' ':ch;}
    if(!fallback.empty())fallback.front()=static_cast<char>(std::toupper(static_cast<unsigned char>(fallback.front())));
    return tr(key,fallback);
  }
  // Focusable order: categories 0..4 then Back (index 5); the controls view
  // rings each bindable action row then Back. Without a mapper the controls
  // help card exposes Back alone (index 0).
  std::uint64_t focus_target()const noexcept{
    if(focus_<0)return 0;const int last=controls_?control_count()-1:5;
    return focus_==last?1u:(controls_?20u:10u)+static_cast<std::uint64_t>(focus_);
  }
  // Installs the captured trigger as the focused action's primary binding,
  // stealing the identical trigger+chord from any sibling action so two
  // commands never silently share one input. Alternates survive; the
  // stolen-from names become a one-shot announcer notice.
  void apply_capture(const stellar::engine::InputBinding& primary){
    const auto rows=control_actions();
    if(!mapper_||capture_>=static_cast<int>(rows.size()))return;
    notice_.clear();
    for(const auto* other:rows){
      const auto& target_action=*rows[static_cast<std::size_t>(capture_)];
      if(other==&target_action)continue;
      auto others=mapper_->bindings(other->name);
      const auto clash=std::find_if(others.begin(),others.end(),[&](const stellar::engine::InputBinding& b){
        return b.kind==primary.kind&&b.code==primary.code&&b.chord_keys==primary.chord_keys;});
      if(clash!=others.end()){
        others.erase(clash);
        mapper_->rebind(other->name,std::move(others));
        if(!notice_.empty())notice_+=", ";
        notice_+=action_label(other->name);
      }
    }
    if(!notice_.empty())
      notice_=tr("SETTINGS_CONTROLS_STOLEN","Rebound — removed from")+" "+notice_;
    auto bound=mapper_->bindings(rows[static_cast<std::size_t>(capture_)]->name);
    if(bound.empty())bound.push_back(primary);else bound.front()=primary;
    mapper_->rebind(rows[static_cast<std::size_t>(capture_)]->name,std::move(bound));
    if(persist_)persist_();
  }
  void activate_focus(){
    if(controls_){
      const int last=control_count()-1;
      if(focus_==last){controls_=false;focus_=4;}
      else if(focus_>=0)capture_=focus_;
      return;
    }
    if(focus_==5)close();
    else if(focus_==4){controls_=true;focus_=-1;}
    else if(open_)open_(static_cast<Category>(focus_));
  }
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  bool visible_{},controls_{};Point pointer_{};int focus_{-1};Open open_;std::function<bool()> child_visible_;
  const stellar::engine::LocalizationTable* locale_{};
  stellar::engine::InputMapper* mapper_{};std::string context_name_{"GALAXY"};
  std::function<void()> persist_{};int capture_{-1};std::string notice_;
};
}
