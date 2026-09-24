#pragma once
#include "native_phenomena.hpp"
#include "native_dropdown.hpp"
#include <stellar/engine/ui_viewmodels.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
namespace stellar::native_phenomena {
class PhenomenaDebug {
public:
  VisualOptions options;
  bool visible{};
  std::optional<std::size_t> take_navigation(std::size_t count){if(!navigate_||!count)return {};navigate_=false;selected_=(selected_%static_cast<int>(count)+static_cast<int>(count))%static_cast<int>(count);return static_cast<std::size_t>(selected_);}
  void toggle(){visible=!visible;dropdown_.close();}
  void data(std::string text){data_=std::move(text);}
  bool handle(const InputEvent& e,int w,int h){
    if(!visible)return false;const auto l=layout(w,h);
    if(dropdown_.visible()){if(auto selected=dropdown_.handle(e,l.density,w,h))options.developer_percent=*selected==0?-1:(*selected-1)*25;return true;}
    if(e.type==InputEventType::EscapePressed){visible=false;return true;}
    if(e.type==InputEventType::Wheel){(void)first_line(l);list_view_.scroll_to(list_view_.scroll_offset-std::round(e.wheel_y)*2*list_view_.row_height);return true;}
    if(e.type!=InputEventType::LeftPressed)return true;
    if(l.close.contains(e.position)){visible=false;return true;}
    if(l.density.contains(e.position)){dropdown_.open(0,{"Use player setting","0%","25%","50%","75%","100%"},options.developer_percent<0?0:options.developer_percent/25+1);return true;}
    for(int i=0;i<3;++i)if(l.navigation[i].contains(e.position)){selected_+=i==0?-1:i==1?1:0;navigate_=true;visible=false;return true;}
    const std::array<bool*,6> values{&options.bounds,&options.labels,&options.heatmap,&options.membership,&options.region_bias,&options.filenames};
    for(int i=0;i<6;++i)if(l.toggles[i].contains(e.position))*values[i]=!*values[i];return true;
  }
  void render(DrawList& out,int w,int h)const{
    if(!visible)return;const auto l=layout(w,h);native_menu_style::panel(out,l.panel,l.s);
    const int font=static_cast<int>(15*l.s);native_menu_style::text(out,{l.panel.x+18*l.s,l.panel.y+14*l.s,600*l.s,28*l.s},"DEVELOPER · SPACE PHENOMENA",font+3);
    native_menu_style::button(out,l.close,"CLOSE",font,false,true,l.s);
    native_menu_style::button(out,l.density,"Visual override: "+(options.developer_percent<0?std::string("Player setting"):std::to_string(options.developer_percent)+"%")+" ▾",font,false,true,l.s);
    const std::array<std::string,6> names{"Bounds","Types","Density","Overlap","Region bias","Filename"};const std::array values{options.bounds,options.labels,options.heatmap,options.membership,options.region_bias,options.filenames};
    for(int i=0;i<6;++i)native_menu_style::button(out,l.toggles[i],std::string(values[i]?"✓ ":"□ ")+names[i],font,false,true,l.s);
    for(int i=0;i<3;++i)native_menu_style::button(out,l.navigation[i],std::array<std::string,3>{"PREVIOUS","NEXT","GO TO"}[i],font,false,true,l.s);
    std::istringstream lines(data_);std::string line;const int skip=first_line(l);int i=0,drawn=0;while(std::getline(lines,line)){if(i++<skip)continue;if(drawn>=18)break;native_menu_style::text(out,{l.panel.x+18*l.s,l.panel.y+(168+drawn++*23)*l.s,l.panel.width-36*l.s,23*l.s},line,font);}
    native_menu_style::text(out,{l.panel.x+18*l.s,l.panel.y+597*l.s,l.panel.width-36*l.s,40*l.s},"Ctrl+Alt+N · Scroll data · Close to inspect overlays. Visual overrides never change simulation.",font,native_menu_style::muted);
    dropdown_.render(out,l.density,w,h,font);
  }
private:
  struct Layout{float s;UiRect panel,close,density;std::array<UiRect,6> toggles;std::array<UiRect,3> navigation;};
  static Layout layout(int w,int h){const auto s=std::clamp(std::min(w/1280.f,h/720.f),.5f,2.5f);const UiRect p{(w-940*s)*.5f,(h-650*s)*.5f,940*s,650*s};Layout l{s,p,{p.x+822*s,p.y+12*s,100*s,32*s},{p.x+18*s,p.y+56*s,400*s,36*s}};for(int i=0;i<6;++i)l.toggles[i]={p.x+(18+i*151)*s,p.y+110*s,141*s,36*s};for(int i=0;i<3;++i)l.navigation[i]={p.x+(445+i*159)*s,p.y+56*s,149*s,36*s};return l;}
  // The engine VirtualizedList owns the line scroll — configured per
  // call so a shorter dump can never leave a stale offset (the old
  // int scroll_ had no upper clamp at all).
  int first_line(const Layout& l)const{
    return static_cast<int>(list_view_.sync_rows(data_.empty()?0:static_cast<std::size_t>(std::ranges::count(data_,'\n'))+1,23*l.s,18*23*l.s));
  }
  stellar::native_ui::Dropdown dropdown_;std::string data_;mutable stellar::engine::VirtualizedList list_view_{};int selected_{};bool navigate_{};
};
}
