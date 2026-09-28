#pragma once
#include "native_dropdown.hpp"
#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>
namespace stellar::native_map {
class NativeDeveloperCelestialIndex {
public:
  void open(const stellar::core::FreshCampaignState &world){
    index_=stellar::core::build_developer_celestial_index(world);visible_=true;selected_=-1;first_=0;filter_=-1;search_.clear();search_focused_=false;pressed_=-1;focus_.reset();dropdown_.close();rebuild();
  }
  void close(){visible_=false;dropdown_.close();search_focused_=false;pressed_=-1;}
  [[nodiscard]] bool visible()const{return visible_;}
  [[nodiscard]] bool wants_text_input()const{return visible_&&search_focused_&&!dropdown_.visible();}
  [[nodiscard]] std::optional<stellar::core::DeveloperCelestialEntry> take_focus_request(){return std::exchange(focus_,{});}
  bool handle(const InputEvent &e,int w,int h,stellar::core::CampaignFrame &frame){
    if(!visible_)return false;
    const auto l=layout(w,h);pointer_=e.position;
    if(dropdown_.visible()){
      const int id=dropdown_.id();if(const auto chosen=dropdown_.handle(e,id==0?l.filter:l.state,w,h)){
        if(id==0){filter_=*chosen-1;first_=0;rebuild();}
        else stellar::core::set_developer_central_black_hole_state(frame.runtime(),static_cast<stellar::core::CentralBlackHoleState>(*chosen));
      }return true;
    }
    if(e.type==InputEventType::EscapePressed){close();return true;}
    if(e.type==InputEventType::PointerCancelled){pressed_=-1;search_focused_=false;return true;}
    if(search_focused_&&e.type==InputEventType::TextEntered){if(search_.size()+e.text.size()<=96){search_+=e.text;first_=0;rebuild();}return true;}
    if(search_focused_&&e.type==InputEventType::BackspacePressed){if(!search_.empty()){auto pos=search_.size()-1;while(pos&&(static_cast<unsigned char>(search_[pos])&0xc0)==0x80)--pos;search_.resize(pos);first_=0;rebuild();}return true;}
    if(e.type==InputEventType::Wheel&&l.list.contains(e.position))first_=std::clamp(first_-static_cast<int>(std::round(e.wheel_y)),0,std::max(0,static_cast<int>(rows_.size())-8));
    if(e.type==InputEventType::LeftPressed){
      search_focused_=l.search.contains(e.position);
      if(l.filter.contains(e.position)){
        std::vector<std::string> choices{"All celestial categories"};for(const auto &c:index_.counts)choices.push_back(c.type_name+" ("+std::to_string(c.natural)+" natural + "+std::to_string(c.forced)+" QA)");
        dropdown_.open(0,std::move(choices),filter_+1);return true;
      }
      if(l.state.contains(e.position)&&selected()&&selected()->central){
        const auto &core=frame.runtime().world().campaign().galactic_core;
        dropdown_.open(1,{"Quiescent","Accreting / Active","Relativistic Jets"},static_cast<int>(core->black_hole->state));return true;
      }
      for(int i=0;i<8&&first_+i<static_cast<int>(rows_.size());++i)if(row(l,i).contains(e.position)){selected_=rows_[first_+i];return true;}
      pressed_=l.close.contains(e.position)?0:l.focus.contains(e.position)?1:-1;
    }
    if(e.type==InputEventType::LeftReleased){
      const auto pressed=std::exchange(pressed_,-1);
      if(pressed==0&&l.close.contains(e.position))close();
      if(pressed==1&&l.focus.contains(e.position)&&selected()){focus_=*selected();close();}
    }
    return true; // Modal input never reaches gameplay underneath.
  }
  void render(DrawList &out,int w,int h,stellar::core::CampaignFrame &frame)const{
    if(!visible_)return;const auto l=layout(w,h);const float s=l.scale;const int font=std::max(12,static_cast<int>(16*s));
    native_menu_style::panel(out,l.panel,s);
    const auto text=[&](UiRect box,std::string value,Color color=native_menu_style::ink){native_menu_style::text(out,box,std::move(value),font,color);};
    text({l.panel.x+20*s,l.panel.y+16*s,l.panel.width-160*s,30*s},"DEVELOPER · CELESTIAL INDEX",native_menu_style::cyan);
    native_menu_style::button(out,l.close,"CLOSE",font,l.close.contains(pointer_),true,s);
    native_menu_style::button(out,l.filter,(filter_<0?"All celestial categories":index_.counts[filter_].type_name)+"  ▾",font,l.filter.contains(pointer_),true,s);
    out.overlay.emplace_back(FilledRectangle{l.search,{3,13,23,255}});out.overlay.emplace_back(StrokedRectangle{l.search,search_focused_?native_menu_style::cyan:Color{62,121,146,225}});
    text({l.search.x+9*s,l.search.y+6*s,l.search.width-18*s,l.search.height},search_.empty()?"Search name, class or region...":search_);
    text({l.list.x,l.list.y-25*s,l.list.width,24*s},std::to_string(rows_.size())+(rows_.size()==1?" object":" objects")+" · Natural / Developer coverage",native_menu_style::muted);
    for(int i=0;i<8&&first_+i<static_cast<int>(rows_.size());++i){
      const auto r=row(l,i);const auto &e=index_.entries[rows_[first_+i]];
      if(rows_[first_+i]==selected_)out.overlay.emplace_back(FilledRectangle{r,{18,54,73,245}});
      out.overlay.emplace_back(StrokedRectangle{r,{62,121,146,225}});
      text({r.x+8*s,r.y+5*s,r.width-16*s,22*s},e.name+(e.forced?" · QA":""),e.forced?Color{240,189,93,255}:native_menu_style::ink);
      text({r.x+8*s,r.y+28*s,r.width-16*s,22*s},e.type_name,native_menu_style::muted);
    }
    if(const auto *e=selected()){
      float y=l.details.y;
      const auto add=[&](std::string label,std::string value){text({l.details.x,y,l.details.width,26*s},std::move(label)+"  "+std::move(value));y+=30*s;};
      add("Object",e->name);add("Class",e->type_name);add("Origin",e->forced?"Developer Coverage":"Natural");add("Region",e->region);
      add("Mass",number(e->mass_solar*1.98847e30)+" kg");
      if(!e->central){add("Radius",number(e->radius_solar*695700.)+" km");add("Luminosity",number(e->luminosity_solar*3.828e26)+" W");
        add("Safe approach",number(e->safe_approach_au*stellar::core::astronomical_unit_km)+" km");
        add("Habitable zone",number(e->inner_hz_au*stellar::core::astronomical_unit_km)+" – "+number(e->outer_hz_au*stellar::core::astronomical_unit_km)+" km");}
      else{
        const auto state=frame.runtime().world().campaign().galactic_core->black_hole->state;
        native_menu_style::button(out,l.state,std::string("Central state: ")+(state==stellar::core::CentralBlackHoleState::Quiescent?"Quiescent":state==stellar::core::CentralBlackHoleState::Accreting?"Accreting":"Relativistic Jets")+" ▾",font,l.state.contains(pointer_),true,s);
      }
      add("Stable ID",e->key);
    }else text(l.details,"Select an object to inspect its actual physical properties.",native_menu_style::muted);
    native_menu_style::button(out,l.focus,"CENTER GALAXY MAP",font,l.focus.contains(pointer_),selected()!=nullptr,s);
    text({l.panel.x+20*s,l.panel.y+l.panel.height-37*s,l.panel.width-40*s,30*s},"Inspection and centering preserve exploration. Normal player saves remain separate.",native_menu_style::muted);
    if(dropdown_.visible())dropdown_.render(out,dropdown_.id()==0?l.filter:l.state,w,h,font);
  }
private:
  struct Layout{float scale;UiRect panel,close,filter,search,list,details,focus,state;};
  static Layout layout(int w,int h){
    const float s=std::clamp(std::min(w/1920.f,h/1080.f),.67f,2.f);const UiRect p{w*.5f-555*s,h*.5f-350*s,1110*s,700*s};
    return {s,p,{p.x+p.width-114*s,p.y+12*s,94*s,32*s},{p.x+20*s,p.y+58*s,510*s,38*s},{p.x+550*s,p.y+58*s,540*s,38*s},
      {p.x+20*s,p.y+136*s,510*s,440*s},{p.x+550*s,p.y+122*s,540*s,420*s},
      {p.x+550*s,p.y+600*s,540*s,40*s},{p.x+550*s,p.y+505*s,540*s,40*s}};
  }
  static UiRect row(const Layout &l,int i){return {l.list.x,l.list.y+i*55*l.scale,l.list.width,52*l.scale};}
  static std::string number(double value){std::ostringstream out;out<<std::setprecision(3)<<std::scientific<<value;return out.str();}
  static std::string lower(std::string value){for(auto &c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
  const stellar::core::DeveloperCelestialEntry *selected()const{return selected_>=0&&selected_<static_cast<int>(index_.entries.size())?&index_.entries[selected_]:nullptr;}
  void rebuild(){rows_.clear();const auto query=lower(search_);for(std::size_t i=0;i<index_.entries.size();++i){const auto &e=index_.entries[i];if(filter_>=0&&e.type_id!=index_.counts[filter_].type_id)continue;if(!query.empty()&&lower(e.name+" "+e.type_name+" "+e.region).find(query)==std::string::npos)continue;rows_.push_back(static_cast<int>(i));}if(std::ranges::find(rows_,selected_)==rows_.end())selected_=rows_.empty()?-1:rows_.front();}
  stellar::core::DeveloperCelestialIndex index_;stellar::native_ui::Dropdown dropdown_;std::vector<int> rows_;
  std::optional<stellar::core::DeveloperCelestialEntry> focus_;std::string search_;
  bool visible_{},search_focused_{};int selected_{-1},filter_{-1},first_{},pressed_{-1};Point pointer_{};
};
}
