#pragma once
#include "native_menu_style.hpp"
#include "native_campaign_calendar.hpp"
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign.hpp>
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
  void close(){visible_=false;pressed_=-1;focus_.reset();}
  bool visible()const{return visible_;}
  std::optional<int> take_focus_request(){return std::exchange(focus_,std::nullopt);}
  bool handle(const InputEvent &e,int w,int h,stellar::core::CampaignFrame &frame){
    if(!visible_)return false;const auto l=layout(w,h);pointer_=e.position;
    if(e.type==InputEventType::EscapePressed){close();return true;}
    if(e.type==InputEventType::PointerCancelled){pressed_=-1;return true;}
    if(e.type==InputEventType::Wheel){
      const float delta=std::round(e.wheel_y);
      if(l.list.contains(e.position)){(void)first_row(l);empire_view_.scroll_to(empire_view_.scroll_offset-delta*empire_view_.row_height);}
      if(l.projects.contains(e.position)){(void)project_row(l);project_view_.scroll_to(project_view_.scroll_offset-delta*project_view_.row_height);}
    }
    int hit=l.close.contains(e.position)?0:l.focus.contains(e.position)?1:l.refresh.contains(e.position)?2:-1;
    const int first=first_row(l);
    for(int i=0;i<12&&first+i<static_cast<int>(rows_.size());++i)if(row_rect(l,i).contains(e.position))hit=10+i;
    if(e.type==InputEventType::LeftPressed)pressed_=hit;
    if(e.type==InputEventType::LeftReleased&&std::exchange(pressed_,-1)==hit){
      if(hit==0)close();
      else if(hit==1){for(const auto &r:rows_)if(r.civilization_id==selected_){const int id=r.home_system_id;close();focus_=id;break;}}
      else if(hit==2)refresh(frame);
      else if(hit>=10){selected_=rows_.at(first+hit-10).civilization_id;project_view_.scroll_offset=0;refresh(frame);}
    }
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
    v.row_height=stride*l.s;v.viewport_height=viewport;v.row_count=rows;
    v.scroll_to(v.scroll_offset);
    if(v.row_height>0)v.scroll_offset=std::floor(v.scroll_offset/v.row_height)*v.row_height;
    return v.row_height>0?static_cast<int>(v.scroll_offset/v.row_height):0;
  }
  static std::string number(double v){std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<v;return o.str();}
  static std::string delta(double v){return (v>=0?"+":"")+number(v);}
  void refresh(stellar::core::CampaignFrame &frame){
    rows_=stellar::core::developer_empire_summaries(frame.runtime());
    if(!rows_.empty())research_=stellar::core::developer_empire_research(frame.runtime(),selected_);
    day_=frame.clock().simulation_days();next_refresh_=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
  }
  bool visible_{};int selected_{},pressed_{-1};Point pointer_{};
  mutable stellar::engine::VirtualizedList empire_view_{},project_view_{};
  double day_{};std::optional<int> focus_;std::chrono::steady_clock::time_point next_refresh_{};
  std::vector<stellar::core::DeveloperEmpireSummary> rows_;
  std::map<int,stellar::core::DeveloperEmpireSummary> baseline_;
  stellar::core::AdaptiveResearchView research_;
};
}
