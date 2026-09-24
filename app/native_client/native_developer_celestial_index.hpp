#pragma once
#include "native_dropdown.hpp"
#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>
namespace stellar::native_map {
class NativeDeveloperCelestialIndex {
public:
  void open(const stellar::core::FreshCampaignState &world){
    index_=stellar::core::build_developer_celestial_index(world);visible_=true;selected_=-1;list_view_.scroll_offset=0;filter_=-1;search_.clear();search_focused_=false;pressed_=-1;ring_=-1;focus_.reset();dropdown_.close();rebuild();
  }
  void close(){visible_=false;dropdown_.close();search_focused_=false;pressed_=-1;ring_=-1;}
  [[nodiscard]] bool visible()const{return visible_;}
  [[nodiscard]] bool wants_text_input()const{return visible_&&search_focused_&&!dropdown_.visible();}
  // Keyboard-focus contract: the ring walks the rendered controls in (y,x)
  // order — search field, category filter, close, each clipped-visible row,
  // the central-state dropdown (central objects only) and the center-map
  // action — with activation replaying the same dispatch pointer input
  // takes. The search field enters edit mode on activation and owns its
  // keys until Tab/Return commit out.
  [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
  [[nodiscard]] int focus()const noexcept{return ring_;}
  [[nodiscard]] std::optional<stellar::core::DeveloperCelestialEntry> take_focus_request(){return std::exchange(focus_,{});}
  [[nodiscard]] std::string focused_label(int w,int h)const{
    const auto targets=focusables(layout(w,h));
    return ring_>=0&&ring_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(ring_)].label:std::string{};
  }
  [[nodiscard]] std::optional<UiRect> focused_bounds(int w,int h)const{
    const auto targets=focusables(layout(w,h));
    return ring_>=0&&ring_<static_cast<int>(targets.size())?std::optional<UiRect>{targets[static_cast<std::size_t>(ring_)].rect}:std::nullopt;
  }
  [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int w,int h)const{
    const auto targets=focusables(layout(w,h));
    return ring_>=0&&ring_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(ring_)].control:stellar::engine::AnnouncementControl::Custom;
  }
  bool handle(const InputEvent &e,int w,int h,stellar::core::CampaignFrame &frame){
    if(!visible_)return false;
    const auto l=layout(w,h);pointer_=e.position;
    if(dropdown_.visible()){
      const int id=dropdown_.id();if(const auto chosen=dropdown_.handle(e,id==0?l.filter:l.state,w,h)){
        if(id==0){filter_=*chosen-1;list_view_.scroll_offset=0;rebuild();}
        else stellar::core::set_developer_central_black_hole_state(frame.runtime(),static_cast<stellar::core::CentralBlackHoleState>(*chosen));
      }return true;
    }
    if(e.type==InputEventType::EscapePressed){
      if(search_focused_){search_focused_=false;return true;}
      if(ring_>=0){ring_=-1;return true;}
      close();return true;
    }
    if(e.type==InputEventType::PointerCancelled){pressed_=-1;search_focused_=false;ring_=-1;return true;}
    if(search_focused_&&e.type==InputEventType::TextEntered){if(search_.size()+e.text.size()<=96){search_+=e.text;list_view_.scroll_offset=0;rebuild();}return true;}
    if(search_focused_&&e.type==InputEventType::BackspacePressed){if(!search_.empty()){auto pos=search_.size()-1;while(pos&&(static_cast<unsigned char>(search_[pos])&0xc0)==0x80)--pos;search_.resize(pos);list_view_.scroll_offset=0;rebuild();}return true;}
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    if(search_focused_&&e.type==InputEventType::KeyPressed&&e.key&&(e.key==kTab||e.key==kReturn)){search_focused_=false;return true;}
    if(!search_focused_&&e.type==InputEventType::KeyPressed&&e.key){
      const auto targets=focusables(l);const int count=static_cast<int>(targets.size());
      if(count){
        const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
        const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
        const bool home=e.key==kHome,end=e.key==kEnd;
        // Arrow scroll-follow: the ring covers rendered rows only, so Up on
        // the first row scrolls one row up and Down on the last scrolls one
        // row down — the slot stays focused and re-resolves to the newly
        // revealed row. Other nav keys leave the list to the next control.
        if(ring_>=0&&ring_<count&&(e.key==kDown||e.key==kUp)){
          const auto&t=targets[static_cast<std::size_t>(ring_)];
          if(t.kind==FocusTarget::Kind::Row&&t.row>=0){
            const int first=first_row(l);
            const int last=std::min(first+8,static_cast<int>(rows_.size()))-1;
            if(e.key==kDown&&t.row==last&&last+1<static_cast<int>(rows_.size())){
              list_view_.scroll_to(list_view_.scroll_offset+list_view_.row_height);return true;
            }
            if(e.key==kUp&&t.row==first&&first>0){
              list_view_.scroll_to(list_view_.scroll_offset-list_view_.row_height);return true;
            }
          }
        }
        if(fwd||bwd||home||end){
          if(ring_<0)ring_=bwd||end?count-1:0;
          else if(fwd)ring_=(ring_+1)%count;
          else if(bwd)ring_=(ring_+count-1)%count;
          else if(home)ring_=0;
          else ring_=count-1;
          const auto&t=targets[static_cast<std::size_t>(ring_)];
          if(t.kind==FocusTarget::Kind::Row&&t.row>=0)list_view_.ensure_visible(static_cast<std::size_t>(t.row));
          return true;
        }
        if((e.key==kReturn||e.key==kSpace)&&ring_>=0){
          activate(targets[static_cast<std::size_t>(ring_)],frame);return true;
        }
      }
    }
    if(e.type==InputEventType::Wheel&&l.list.contains(e.position)){(void)first_row(l);list_view_.scroll_to(list_view_.scroll_offset-std::round(e.wheel_y)*list_view_.row_height);}
    if(e.type==InputEventType::LeftPressed){
      ring_=-1;
      search_focused_=l.search.contains(e.position);
      if(l.filter.contains(e.position)){open_filter_dropdown();return true;}
      if(l.state.contains(e.position)&&selected()&&selected()->central){open_state_dropdown(frame);return true;}
      const int first=first_row(l);
      for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i)if(row(l,i).contains(e.position)){selected_=rows_[first+i];return true;}
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
    const int first=first_row(l);
    for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i){
      const auto r=row(l,i);const auto &e=index_.entries[rows_[first+i]];
      if(rows_[first+i]==selected_)out.overlay.emplace_back(FilledRectangle{r,{18,54,73,245}});
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
    if(ring_>=0){
      const auto targets=focusables(l);
      if(ring_<static_cast<int>(targets.size())){
        const auto &r=targets[static_cast<std::size_t>(ring_)].rect;
        const UiRect outer{r.x-3*s,r.y-3*s,r.width+6*s,r.height+6*s};
        out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
        out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
      }
    }
    if(dropdown_.visible())dropdown_.render(out,dropdown_.id()==0?l.filter:l.state,w,h,font);
  }
private:
  struct Layout{float scale;UiRect panel,close,filter,search,list,details,focus,state;};
  struct FocusTarget{
    enum class Kind{Close,Filter,Search,Row,State,Center};
    UiRect rect;Kind kind{};int row{-1};std::string label;
    stellar::engine::AnnouncementControl control{stellar::engine::AnnouncementControl::Button};
  };
  std::vector<FocusTarget> focusables(const Layout &l)const{
    std::vector<FocusTarget> out;
    const auto push=[&](UiRect r,FocusTarget::Kind kind,std::string label,int row=-1,
                        stellar::engine::AnnouncementControl control=stellar::engine::AnnouncementControl::Button){
      if(r.width>0&&r.height>0)out.push_back({r,kind,row,std::move(label),control});};
    push(l.search,FocusTarget::Kind::Search,"Search name, class or region",-1,stellar::engine::AnnouncementControl::Edit);
    push(l.filter,FocusTarget::Kind::Filter,std::string("Category filter · ")+(filter_<0?"All celestial categories":index_.counts[filter_].type_name));
    push(l.close,FocusTarget::Kind::Close,"Close celestial index");
    const int first=first_row(l);
    for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i){
      const auto&e=index_.entries[static_cast<std::size_t>(rows_[static_cast<std::size_t>(first+i)])];
      push(row(l,i),FocusTarget::Kind::Row,e.name+(e.forced?" · QA":""),first+i);
    }
    if(selected()&&selected()->central)push(l.state,FocusTarget::Kind::State,"Central black hole state");
    if(selected())push(l.focus,FocusTarget::Kind::Center,"Center galaxy map on "+selected()->name);
    std::ranges::sort(out,[](const FocusTarget&a,const FocusTarget&b){return a.rect.y==b.rect.y?a.rect.x<b.rect.x:a.rect.y<b.rect.y;});
    return out;
  }
  void open_filter_dropdown(){
    std::vector<std::string> choices{"All celestial categories"};for(const auto &c:index_.counts)choices.push_back(c.type_name+" ("+std::to_string(c.natural)+" natural + "+std::to_string(c.forced)+" QA)");
    dropdown_.open(0,std::move(choices),filter_+1);
  }
  void open_state_dropdown(stellar::core::CampaignFrame &frame){
    const auto &core=frame.runtime().world().campaign().galactic_core;
    dropdown_.open(1,{"Quiescent","Accreting / Active","Relativistic Jets"},static_cast<int>(core->black_hole->state));
  }
  void activate(const FocusTarget &t,stellar::core::CampaignFrame &frame){
    switch(t.kind){
      case FocusTarget::Kind::Close:close();break;
      case FocusTarget::Kind::Filter:open_filter_dropdown();break;
      case FocusTarget::Kind::Search:search_focused_=true;break;
      case FocusTarget::Kind::Row:if(t.row>=0&&t.row<static_cast<int>(rows_.size()))selected_=rows_[static_cast<std::size_t>(t.row)];break;
      case FocusTarget::Kind::State:open_state_dropdown(frame);break;
      case FocusTarget::Kind::Center:if(selected()){focus_=*selected();close();}break;
    }
  }
  static Layout layout(int w,int h){
    const float s=std::clamp(std::min(w/1920.f,h/1080.f),.67f,2.f);const UiRect p{w*.5f-555*s,h*.5f-350*s,1110*s,700*s};
    return {s,p,{p.x+p.width-114*s,p.y+12*s,94*s,32*s},{p.x+20*s,p.y+58*s,510*s,38*s},{p.x+550*s,p.y+58*s,540*s,38*s},
      {p.x+20*s,p.y+136*s,510*s,440*s},{p.x+550*s,p.y+122*s,540*s,420*s},
      {p.x+550*s,p.y+600*s,540*s,40*s},{p.x+550*s,p.y+505*s,540*s,40*s}};
  }
  static UiRect row(const Layout &l,int i){return {l.list.x,l.list.y+i*55*l.scale,l.list.width,52*l.scale};}
  // The engine VirtualizedList owns the scroll offset — synced per
  // call so a rebuild that shrinks rows can never leave a stale offset
  // past the tail; the panel scrolls whole rows.
  int first_row(const Layout &l)const{return static_cast<int>(list_view_.sync_rows(rows_.size(),55*l.scale,l.list.height));}
  static std::string number(double value){std::ostringstream out;out<<std::setprecision(3)<<std::scientific<<value;return out.str();}
  static std::string lower(std::string value){for(auto &c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
  const stellar::core::DeveloperCelestialEntry *selected()const{return selected_>=0&&selected_<static_cast<int>(index_.entries.size())?&index_.entries[selected_]:nullptr;}
  void rebuild(){rows_.clear();const auto query=lower(search_);for(std::size_t i=0;i<index_.entries.size();++i){const auto &e=index_.entries[i];if(filter_>=0&&e.type_id!=index_.counts[filter_].type_id)continue;if(!query.empty()&&lower(e.name+" "+e.type_name+" "+e.region).find(query)==std::string::npos)continue;rows_.push_back(static_cast<int>(i));}if(std::ranges::find(rows_,selected_)==rows_.end())selected_=rows_.empty()?-1:rows_.front();}
  stellar::core::DeveloperCelestialIndex index_;stellar::native_ui::Dropdown dropdown_;std::vector<int> rows_;
  std::optional<stellar::core::DeveloperCelestialEntry> focus_;std::string search_;
  bool visible_{},search_focused_{};int selected_{-1},filter_{-1},pressed_{-1};Point pointer_{};
  mutable stellar::engine::VirtualizedList list_view_{};
  int ring_{-1};
};
}
