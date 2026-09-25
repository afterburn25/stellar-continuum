#pragma once
#include "native_system_background.hpp"
#include "native_menu_style.hpp"
#include <stellar/engine/accessibility.hpp>
#include <sstream>
namespace stellar::native_map {
class NativeBackgroundDebug {
 bool visible_{},navigate_{};std::size_t selected_{};std::vector<std::pair<std::string,int>> examples_;
 std::string diagnostic_;int ring_{-1};
 struct Layout{float scale;UiRect panel;};
 static Layout layout(int w,int h){const float s=std::min(w/1280.f,h/720.f);return {s,{(w-1140*s)*.5f,(h-670*s)*.5f,1140*s,670*s}};}
 static UiRect button(const Layout& l,int i){return {l.panel.x+(18+(i%4)*280)*l.scale,l.panel.y+(50+(i/4)*40)*l.scale,270*l.scale,32*l.scale};}
public:
 bool visible()const{return visible_;}
 void toggle(NativeSystemBackground& sky){visible_=!visible_;if(visible_)examples_=sky.catalog.region_examples();}
 std::optional<int> navigation(){if(!navigate_||examples_.empty())return {};navigate_=false;visible_=false;return examples_[selected_%examples_.size()].second;}
 void data(NativeSystemBackground& sky,int id){const auto p=sky.resolved(id);diagnostic_=stellar::core::system_background_diagnostics(p);diagnostic_+="\nResident source cache: "+std::to_string(sky.cache_bytes()/1024/1024)+" MiB\nLayers: deep stars → haze / gas → dark dust → objects / belts → labels → UI";}
 // Keyboard-focus contract: the ring walks the eight rendered buttons in
 // (y,x) order with Return/Space replaying the same switch pointer presses
 // take; Escape releases the ring before closing.
 [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
 [[nodiscard]] int focus()const noexcept{return ring_;}
 [[nodiscard]] std::string focused_label(int,int,const NativeSystemBackground& sky)const{
  return ring_>=0&&ring_<8?button_labels(sky)[static_cast<std::size_t>(ring_)]:std::string{};
 }
 [[nodiscard]] std::optional<UiRect> focused_bounds(int w,int h)const{
  return ring_>=0&&ring_<8?std::optional<UiRect>{button(layout(w,h),ring_)}:std::nullopt;
 }
 [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int,int)const{
  return ring_>=0&&ring_<8?stellar::engine::AnnouncementControl::Button:stellar::engine::AnnouncementControl::Custom;
 }
 static std::array<std::string,8> button_labels(const NativeSystemBackground& sky){
  const auto& o=sky.options;
  return {o.nebula?"NEBULA: ON":"NEBULA: OFF","NEBULA OPACITY: "+std::to_string(static_cast<int>(o.nebula_opacity*100))+"%",o.dark_test?"DARK TEST: ON":"DARK TEST: OFF","EXPOSURE: "+std::to_string(static_cast<int>(o.exposure_scale*100))+"%","PREVIOUS REGION","NEXT REGION","GO TO REGION","RESET"};
 }
 void activate_button(int i,NativeSystemBackground& sky){
  auto& o=sky.options;
  switch(i){
   case 0:o.nebula=!o.nebula;break;
   case 1:o.nebula_opacity=o.nebula_opacity>=1?0:o.nebula_opacity+.25;break;
   case 2:o.dark_test=!o.dark_test;break;
   case 3:o.exposure_scale=o.exposure_scale>=1?.5:o.exposure_scale+.25;break;
   case 4:if(!examples_.empty())selected_=(selected_+examples_.size()-1)%examples_.size();break;
   case 5:if(!examples_.empty())selected_=(selected_+1)%examples_.size();break;
   case 6:navigate_=true;break;case 7:o={};break;
  }
 }
 bool handle(const InputEvent& e,int w,int h,NativeSystemBackground& sky){
  if(!visible_)return false;
  if(e.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}visible_=false;return true;}
  if(e.type==InputEventType::PointerCancelled){ring_=-1;return true;}
  if(e.type==InputEventType::KeyPressed&&e.key){
   constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
   constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
   constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
   const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
   const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
   if(fwd||bwd){
    if(ring_<0)ring_=bwd?7:0;else ring_=(ring_+(fwd?1:7))%8;return true;
   }
   if(e.key==kHome||e.key==kEnd){ring_=e.key==kHome?0:7;return true;}
   if((e.key==kReturn||e.key==kSpace)&&ring_>=0){activate_button(ring_,sky);return true;}
  }
  if(e.type!=InputEventType::LeftPressed)return true;
  ring_=-1;
  const auto l=layout(w,h);
  for(int i=0;i<8;++i)if(button(l,i).contains(e.position)){activate_button(i,sky);break;}
  return true;
 }
 void render(DrawList& out,int w,int h,const NativeSystemBackground& sky)const{
  if(!visible_)return;const auto l=layout(w,h);const auto s=l.scale;const int font=std::max(11,static_cast<int>(14*s));native_menu_style::panel(out,l.panel,s);
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+12*s,1100*s,28*s},"SYSTEM BACKGROUND DEBUG · Ctrl+Alt+B",font+3);
  const auto labels=button_labels(sky);
  for(int i=0;i<8;++i)native_menu_style::button(out,button(l,i),labels[i],font,false,true,s);
  if(ring_>=0&&ring_<8){
   const auto& r=button(l,ring_);
   const UiRect outer{r.x-3*s,r.y-3*s,r.width+6*s,r.height+6*s};
   out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
   out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
  }
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+145*s,1104*s,27*s},"Approved sky: faint star-map reference. Bright and crowded plates are excluded. Esc to close.",font,native_menu_style::muted);
  std::istringstream lines(diagnostic_);std::string line;int n=0;while(std::getline(lines,line)&&n<15)native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+(267+n++*22)*s,1104*s,22*s},line,font);
  const auto example=examples_.empty()?"No generated examples":examples_[selected_%examples_.size()].first+" · system "+std::to_string(examples_[selected_%examples_.size()].second);
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+625*s,1104*s,27*s},"Jump target: "+example+" · Overrides affect presentation only.",font,native_menu_style::muted);
 }
};
}
