#pragma once
#include "native_menu_style.hpp"
#include "native_campaign_calendar.hpp"
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>

namespace stellar::native_map {
// Read-only Core snapshots, refreshed only while visible, at most four times
// per second. Scrolling and focus never advance or edit another empire.
class NativeDeveloperEmpireMonitor {
public:
  void open(stellar::core::CampaignFrame &frame) {
    close();rows_=stellar::core::developer_empire_summaries(frame.runtime());
    baseline_.clear();for(const auto &r:rows_)baseline_[r.civilization_id]=r;
    selected_=rows_.empty()?0:rows_.front().civilization_id;
    for(const auto &r:rows_)if(!r.player){selected_=r.civilization_id;break;}
    empire_view_.scroll_offset=project_view_.scroll_offset=0;visible_=true;refresh(frame);
  }
  void close(){visible_=false;pressed_=-1;ring_=-1;focus_.reset();}
  bool visible()const{return visible_;}
  // Keyboard-focus contract: close, refresh, show-home and the rendered
  // empire rows ring in (y,x) order; Return/Space replay the same
  // press/release dispatch pointer input takes (activate_hit runs the
  // identical hit switch). Escape releases the ring before closing.
  [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
  [[nodiscard]] int focus()const noexcept{return ring_;}
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
    return ring_>=0&&ring_<static_cast<int>(targets.size())?stellar::engine::AnnouncementControl::Button:stellar::engine::AnnouncementControl::Custom;
  }
  std::optional<int> take_focus_request(){return std::exchange(focus_,std::nullopt);}
  bool handle(const InputEvent &e,int w,int h,stellar::core::CampaignFrame &frame){
    if(!visible_)return false;const auto l=layout(w,h);pointer_=e.position;
    if(e.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}close();return true;}
    if(e.type==InputEventType::PointerCancelled){pressed_=-1;ring_=-1;return true;}
    if(e.type==InputEventType::KeyPressed&&e.key){
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const auto targets=focusables(l);const int count=static_cast<int>(targets.size());
      if(count){
        const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
        const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
        const bool home=e.key==kHome,end=e.key==kEnd;
        // Arrow scroll-follow: the ring covers the rendered rows, so Up on
        // the first row scrolls one row up and Down on the last scrolls
        // one row down — the slot stays focused and re-resolves to the
        // newly revealed row. Only Up/Down scroll; Tab/arrows off the
        // edge move to the next control.
        if(ring_>=0&&ring_<count&&(e.key==kDown||e.key==kUp)){
          const auto&t=targets[static_cast<std::size_t>(ring_)];
          if(t.hit>=10){
            const int first=first_row(l);
            const int last=std::min(first+12,static_cast<int>(rows_.size()))-1;
            if(e.key==kDown&&t.row==last&&last+1<static_cast<int>(rows_.size())){
              empire_view_.scroll_to(empire_view_.scroll_offset+empire_view_.row_height);return true;
            }
            if(e.key==kUp&&t.row==first&&first>0){
              empire_view_.scroll_to(empire_view_.scroll_offset-empire_view_.row_height);return true;
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
          if(t.hit>=10)empire_view_.ensure_visible(static_cast<std::size_t>(t.row));
          return true;
        }
        if((e.key==kReturn||e.key==kSpace)&&ring_>=0){activate_hit(targets[static_cast<std::size_t>(ring_)].hit,first_row(l),frame);return true;}
      }
    }
    if(e.type==InputEventType::Wheel){
      const float delta=std::round(e.wheel_y);
      if(l.list.contains(e.position)){(void)first_row(l);empire_view_.scroll_to(empire_view_.scroll_offset-delta*empire_view_.row_height);}
      if(l.projects.contains(e.position)){(void)project_row(l);project_view_.scroll_to(project_view_.scroll_offset-delta*project_view_.row_height);}
    }
    int hit=l.close.contains(e.position)?0:l.focus.contains(e.position)?1:l.refresh.contains(e.position)?2:-1;
    const int first=first_row(l);
    for(int i=0;i<12&&first+i<static_cast<int>(rows_.size());++i)if(row_rect(l,i).contains(e.position))hit=10+i;
    if(e.type==InputEventType::LeftPressed){pressed_=hit;ring_=-1;}
    if(e.type==InputEventType::LeftReleased&&std::exchange(pressed_,-1)==hit)activate_hit(hit,first,frame);
    return true;
  }
  void render(DrawList &out,int w,int h,stellar::core::CampaignFrame &frame){
    if(!visible_)return;
    if(std::chrono::steady_clock::now()>=next_refresh_)refresh(frame);
    const auto l=layout(w,h);const float s=l.s;const int font=std::max(12,static_cast<int>(16*s));
    native_menu_style::panel(out,l.panel,s);
    const auto text=[&](UiRect r,std::string value,Color c=native_menu_style::ink){native_menu_style::text(out,r,std::move(value),font,c);};
    const auto button=[&](UiRect r,std::string value){native_menu_style::button(out,r,std::move(value),font,r.contains(pointer_),true,s);};
    text({l.panel.x+20*s,l.panel.y+16*s,890*s,28*s},"DEVELOPER · EMPIRE MONITOR",native_menu_style::cyan);
    button(l.close,"CLOSE");button(l.focus,"SHOW HOME SYSTEM");button(l.refresh,"REFRESH NOW");
    text({l.panel.x+20*s,l.panel.y+64*s,1040*s,26*s},"Live simulation day "+number(day_)+" · "+std::to_string(rows_.size())+" empires · Scroll the empire and research lists",native_menu_style::muted);
    const int first=first_row(l);
    for(int i=0;i<12&&first+i<static_cast<int>(rows_.size());++i){
      const auto &r=rows_[first+i];const auto rect=row_rect(l,i);
      button(rect,r.name+(r.player?" (player)":""));
      if(r.civilization_id==selected_)out.overlay.emplace_back(StrokedRectangle{rect,native_menu_style::cyan});
    }
    if(ring_>=0){
      const auto targets=focusables(l);
      if(ring_<static_cast<int>(targets.size())){
        const auto &r=targets[static_cast<std::size_t>(ring_)].rect;
        const UiRect outer{r.x-3*s,r.y-3*s,r.width+6*s,r.height+6*s};
        out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
        out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
      }
    }
    const auto found=std::ranges::find(rows_,selected_,&stellar::core::DeveloperEmpireSummary::civilization_id);
    if(found==rows_.end())return;const auto &r=*found;
    const auto before=baseline_.find(selected_);const auto &base=before==baseline_.end()?r:before->second;
    const float x=l.panel.x+370*s,y=l.panel.y+110*s,width=700*s;
    const auto line=[&](float row,std::string value,Color c=native_menu_style::ink){text({x,y+row*s,width,27*s},std::move(value),c);};
    line(0,r.name,native_menu_style::cyan);
    const char *stage=r.stage==stellar::core::CivilizationDevelopmentStage::PreWarp?"Pre-warp":r.stage==stellar::core::CivilizationDevelopmentStage::WarpCapable?"Warp capable":"Ancient spacefaring";
    line(32,std::string(stage)+" · Expansion "+(r.expansion_allowed?"allowed":"restricted"));
    line(64,"Colonies: "+std::to_string(r.colonies)+"   Outposts: "+std::to_string(r.outposts)+"   Fleets: "+std::to_string(r.fleets));
    line(96,"Active claims: "+std::to_string(r.active_claims)+"   Buildings in progress: "+std::to_string(r.buildings_in_progress));
    line(128,"Population: "+number(r.population_millions)+" million ("+delta(r.population_millions-base.population_millions)+" since opened)");
    line(160,"Credits: "+number(r.credits)+"   Industry: "+number(r.industry));
    line(192,"Research spending/day: "+number(r.research_spending_per_day)+"   Funding: "+number(r.research_funding_fraction*100)+"%");
    line(224,"Established research: "+std::to_string(r.established_research)+" ("+delta(r.established_research-base.established_research)+")   Active: "+std::to_string(r.active_projects));
    line(260,"ACTIVE RESEARCH · progress within the current stage",native_menu_style::cyan);
    const int project_first=project_row(l);
    for(int i=0;i<4&&project_first+i<static_cast<int>(research_.active_projects.size());++i){
      const auto &p=research_.active_projects[project_first+i];const auto py=l.projects.y+i*56*s;
      const auto &name=frame.runtime().research_runtime().authority().catalog().get_node(p.node_id).name;
      text({x,py,width,25*s},name);
      text({x,py+25*s,width,25*s},number(p.stage_progress*100)+"% · "+number(p.assigned_effective_labs)+" labs · "+(p.paused?"Paused: "+p.pause_reason.value_or("unspecified"):"Running"),native_menu_style::muted);
    }
    if(research_.active_projects.empty())text(l.projects,"No active research programs.",native_menu_style::muted);
    text({l.panel.x+20*s,l.panel.y+641*s,1060*s,26*s},"Changes are measured since opening this monitor. Simulation follows the current pause/speed setting.",native_menu_style::muted);
    text({l.panel.x+20*s,l.panel.y+674*s,1060*s,26*s},"Use REVEAL ENTIRE GALAXY in developer controls to show all territories and the central black hole.",native_menu_style::muted);
  }
private:
  struct Layout{float s;UiRect panel,close,focus,refresh,list,projects;};
  struct FocusTarget{UiRect rect;int hit{-1};int row{-1};std::string label;};
  std::vector<FocusTarget> focusables(const Layout &l)const{
    std::vector<FocusTarget> out;
    const auto push=[&](UiRect r,int hit,std::string label,int row=-1){if(r.width>0&&r.height>0)out.push_back({r,hit,row,std::move(label)});};
    push(l.close,0,"Close empire monitor");
    const int first=first_row(l);
    for(int i=0;i<12&&first+i<static_cast<int>(rows_.size());++i)
      push(row_rect(l,i),10+i,rows_[static_cast<std::size_t>(first+i)].name,first+i);
    push(l.refresh,2,"Refresh empire monitor");
    push(l.focus,1,"Show home system");
    std::ranges::sort(out,[](const FocusTarget&a,const FocusTarget&b){return a.rect.y==b.rect.y?a.rect.x<b.rect.x:a.rect.y<b.rect.y;});
    return out;
  }
  void activate_hit(int hit,int first,stellar::core::CampaignFrame &frame){
    if(hit==0)close();
    else if(hit==1){for(const auto &r:rows_)if(r.civilization_id==selected_){const int id=r.home_system_id;close();focus_=id;break;}}
    else if(hit==2)refresh(frame);
    else if(hit>=10&&first+hit-10<static_cast<int>(rows_.size())){selected_=rows_.at(static_cast<std::size_t>(first+hit-10)).civilization_id;project_view_.scroll_offset=0;refresh(frame);}
  }
  static Layout layout(int w,int h){
    const float s=std::clamp(std::min(w/1920.f,h/1080.f),.67f,2.f);const UiRect p{w*.5f-550*s,h*.5f-360*s,1100*s,720*s};
    return {s,p,{p.x+984*s,p.y+12*s,96*s,32*s},{p.x+20*s,p.y+575*s,330*s,38*s},{p.x+20*s,p.y+525*s,330*s,38*s},
      {p.x+20*s,p.y+110*s,330*s,408*s},{p.x+370*s,p.y+404*s,700*s,224*s}};
  }
  static UiRect row_rect(const Layout &l,int i){return {l.list.x,l.list.y+i*34*l.s,l.list.width,30*l.s};}
  // Engine VirtualizedList scroll models — configured per call so a
  // refresh that shrinks a list can never leave a stale offset past the
  // tail; both panels scroll whole rows.
  int first_row(const Layout &l)const{return first_of(empire_view_,l,34.f,l.list.height,rows_.size());}
  int project_row(const Layout &l)const{return first_of(project_view_,l,56.f,l.projects.height,research_.active_projects.size());}
  static int first_of(stellar::engine::VirtualizedList &v,const Layout &l,float stride,float viewport,std::size_t rows){
    return static_cast<int>(v.sync_rows(rows,stride*l.s,viewport));
  }
  static std::string number(double v){std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<v;return o.str();}
  static std::string delta(double v){return (v>=0?"+":"")+number(v);}
  void refresh(stellar::core::CampaignFrame &frame){
    rows_=stellar::core::developer_empire_summaries(frame.runtime());
    if(!rows_.empty())research_=stellar::core::developer_empire_research(frame.runtime(),selected_);
    day_=frame.clock().simulation_days();next_refresh_=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
  }
  bool visible_{};int selected_{},pressed_{-1},ring_{-1};Point pointer_{};
  mutable stellar::engine::VirtualizedList empire_view_{},project_view_{};
  double day_{};std::optional<int> focus_;std::chrono::steady_clock::time_point next_refresh_{};
  std::vector<stellar::core::DeveloperEmpireSummary> rows_;
  std::map<int,stellar::core::DeveloperEmpireSummary> baseline_;
  stellar::core::AdaptiveResearchView research_;
};
}
