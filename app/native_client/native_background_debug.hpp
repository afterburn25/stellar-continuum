#pragma once
#include "native_system_background.hpp"
#include "native_menu_style.hpp"
#include <sstream>
namespace stellar::native_map {
class NativeBackgroundDebug {
 bool visible_{},navigate_{};std::size_t selected_{};std::vector<std::pair<std::string,int>> examples_;
 std::string diagnostic_;
 struct Layout{float scale;UiRect panel;};
 static Layout layout(int w,int h){const float s=std::min(w/1280.f,h/720.f);return {s,{(w-1140*s)*.5f,(h-670*s)*.5f,1140*s,670*s}};}
 static UiRect button(const Layout& l,int i){return {l.panel.x+(18+(i%4)*280)*l.scale,l.panel.y+(50+(i/4)*40)*l.scale,270*l.scale,32*l.scale};}
public:
 bool visible()const{return visible_;}
 void toggle(NativeSystemBackground& sky){visible_=!visible_;if(visible_)examples_=sky.catalog.region_examples();}
 std::optional<int> navigation(){if(!navigate_||examples_.empty())return {};navigate_=false;visible_=false;return examples_[selected_%examples_.size()].second;}
 void data(NativeSystemBackground& sky,int id){const auto p=sky.resolved(id);diagnostic_=stellar::core::system_background_diagnostics(p);diagnostic_+="\nResident source cache: "+std::to_string(sky.cache_bytes()/1024/1024)+" MiB\nLayers: deep stars → haze / gas → dark dust → objects / belts → labels → UI";}
 bool handle(const InputEvent& e,int w,int h,NativeSystemBackground& sky){
  if(!visible_)return false;if(e.type==InputEventType::EscapePressed){visible_=false;return true;}if(e.type!=InputEventType::LeftPressed)return true;
  const auto l=layout(w,h);auto& o=sky.options;
  for(int i=0;i<8;++i)if(button(l,i).contains(e.position)){
   switch(i){
    case 0:o.nebula=!o.nebula;break;
    case 1:o.nebula_opacity=o.nebula_opacity>=1?0:o.nebula_opacity+.25;break;
    case 2:o.dark_test=!o.dark_test;break;
    case 3:o.exposure_scale=o.exposure_scale>=1?.5:o.exposure_scale+.25;break;
    case 4:if(!examples_.empty())selected_=(selected_+examples_.size()-1)%examples_.size();break;
    case 5:if(!examples_.empty())selected_=(selected_+1)%examples_.size();break;
    case 6:navigate_=true;break;case 7:o={};break;
   }break;
  }return true;
 }
 void render(DrawList& out,int w,int h,const NativeSystemBackground& sky)const{
  if(!visible_)return;const auto l=layout(w,h);const auto s=l.scale;const int font=std::max(11,static_cast<int>(14*s));native_menu_style::panel(out,l.panel,s);
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+12*s,1100*s,28*s},"SYSTEM BACKGROUND DEBUG · Ctrl+Alt+B",font+3);
  const auto& o=sky.options;
  const std::array<std::string,8> labels{o.nebula?"NEBULA: ON":"NEBULA: OFF","NEBULA OPACITY: "+std::to_string(static_cast<int>(o.nebula_opacity*100))+"%",o.dark_test?"DARK TEST: ON":"DARK TEST: OFF","EXPOSURE: "+std::to_string(static_cast<int>(o.exposure_scale*100))+"%","PREVIOUS REGION","NEXT REGION","GO TO REGION","RESET"};
  for(int i=0;i<8;++i)native_menu_style::button(out,button(l,i),labels[i],font,false,true,s);
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+145*s,1104*s,27*s},"Approved sky: faint star-map reference. Bright and crowded plates are excluded. Esc to close.",font,native_menu_style::muted);
  std::istringstream lines(diagnostic_);std::string line;int n=0;while(std::getline(lines,line)&&n<15)native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+(267+n++*22)*s,1104*s,22*s},line,font);
  const auto example=examples_.empty()?"No generated examples":examples_[selected_%examples_.size()].first+" · system "+std::to_string(examples_[selected_%examples_.size()].second);
  native_menu_style::text(out,{l.panel.x+18*s,l.panel.y+625*s,1104*s,27*s},"Jump target: "+example+" · Overrides affect presentation only.",font,native_menu_style::muted);
 }
};
}
