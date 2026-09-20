#include "native_system_workspace.hpp"
#include "native_ui_layout.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace stellar::native_system_ui {
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::core;
namespace {
struct Layout {
  UiRect launcher,panel,close,previous,next,body,large,focus,debug,motion;
  std::array<UiRect,4> spawn;
  float scale{};
};
Layout layout_for(int w,int h){
  const auto field=SystemWorkspaceLayout::for_viewport(w,h).world_field;
  const float s=NativeUiLayout::for_viewport(w,h).scale;
  Layout l;l.scale=s;l.launcher={field.x+12*s,field.y+field.height-35*s,180*s,29*s};
  l.motion={field.x+field.width-168*s,field.y+field.height-69*s,156*s,29*s};
  l.panel={field.x+12*s,field.y+8*s,std::min(450*s,field.width-24*s),446*s};
  const auto p=l.panel;l.close={p.x+p.width-65*s,p.y+9*s,55*s,25*s};
  const float half=(p.width-30*s)*.5f;
  l.previous={p.x+10*s,p.y+48*s,half,27*s};l.next={p.x+20*s+half,p.y+48*s,half,27*s};
  const float third=(p.width-40*s)/3;
  l.body={p.x+10*s,p.y+286*s,third,28*s};l.large={p.x+20*s+third,p.y+286*s,third,28*s};l.focus={p.x+30*s+third*2,p.y+286*s,third,28*s};
  l.debug={p.x+10*s,p.y+323*s,p.width-20*s,27*s};
  for(int i=0;i<4;++i)l.spawn[i]={p.x+(10+(i%2)*(half/s+10))*s,p.y+(362+(i/2)*33)*s,half,27*s};
  return l;
}
std::string number(double n,int precision=2){std::ostringstream s;s<<std::fixed<<std::setprecision(precision)<<n;return s.str();}
void label(DrawList& out,UiRect r,std::string value,Color c={207,224,238,255},int size=13){out.overlay.emplace_back(Text{{r.x+8,r.y+5},std::move(value),c,size,r.width-16,r});}
void button(DrawList& out,UiRect r,std::string value){out.overlay.emplace_back(FilledRectangle{r,{13,35,51,252}});out.overlay.emplace_back(StrokedRectangle{r,{65,130,157,255}});label(out,r,std::move(value));}
}
void NativeSystemWorkspace::focus_small_body(int width,int height){tracked_body_id_.reset();
  if(!snapshot_||!spatial_||!viewport_||snapshot_->small_body_fields.empty())return;
  small_body_field_%=snapshot_->small_body_fields.size();const auto& f=snapshot_->small_body_fields[small_body_field_];small_body_index_%=f.visible_count;
  const auto body=small_body_instance(f,small_body_index_);SystemSpatialViewport unit{0,0,1};
  const auto p=NativeSmallBodyRenderer::position(f,body,*spatial_,unit,snapshot_->simulation_days);
  const auto field=SystemWorkspaceLayout::for_viewport(width,height).world_field;
  viewport_->scale=std::clamp(std::min(220.f,field.height*.35f)/small_body_display_radius(body),2.5f,45.f);viewport_->center_x=field.x+field.width*.5f-p.x*viewport_->scale;viewport_->center_y=field.y+field.height*.5f-p.y*viewport_->scale;
  small_body_focus_=true;
  dragging_=false;pending_initial_travel_fit_=false;small_body_panel_=false;
}
std::optional<SystemWorkspaceCommand> NativeSystemWorkspace::handle_small_bodies(const InputEvent& e,int width,int height){
  if(!snapshot_||snapshot_->survey_level!=SystemSurveyLevel::fully_surveyed)return std::nullopt;
  const auto l=layout_for(width,height);const SystemWorkspaceCommand handled{SystemWorkspaceCommandKind::none,true};
  if(l.motion.contains(e.position)){
    dragging_=false;
    if(e.type==InputEventType::LeftPressed)return SystemWorkspaceCommand{SystemWorkspaceCommandKind::toggle_motion,true};
    return handled;
  }
  if(e.type==InputEventType::LeftPressed&&l.launcher.contains(e.position)){small_body_panel_=!small_body_panel_;dragging_=false;return handled;}
  if(small_body_panel_){
    if(e.type==InputEventType::EscapePressed){small_body_panel_=false;return handled;}
    if(l.panel.contains(e.position)){
      dragging_=false;
      if(e.type==InputEventType::LeftPressed){
        const auto count=snapshot_->small_body_fields.size();
        if(l.close.contains(e.position))small_body_panel_=false;
        else if(count&&l.previous.contains(e.position)){small_body_field_=(small_body_field_+count-1)%count;small_body_index_=0;}
        else if(count&&l.next.contains(e.position)){small_body_field_=(small_body_field_+1)%count;small_body_index_=0;}
        else if(count&&l.body.contains(e.position))small_body_index_=(small_body_index_+1)%snapshot_->small_body_fields[small_body_field_%count].visible_count;
        else if(count&&l.large.contains(e.position)){
          const auto& f=snapshot_->small_body_fields[small_body_field_%count];auto bodies=small_body_instances(f,2048);
          std::erase_if(bodies,[](const auto& b){return small_body_display_radius(b)<13;});
          std::stable_sort(bodies.begin(),bodies.end(),[](const auto& a,const auto& b){return small_body_display_radius(a)>small_body_display_radius(b);});
          if(!bodies.empty()){const auto it=std::ranges::find(bodies,small_body_index_,&SmallBodyInstance::id);
            small_body_index_=it==bodies.end()||std::next(it)==bodies.end()?bodies.front().id:std::next(it)->id;}
        }
        else if(count&&l.focus.contains(e.position))focus_small_body(width,height);
        else if(snapshot_->developer){
          if(l.debug.contains(e.position))small_body_debug_=!small_body_debug_;
          constexpr std::array types{SmallBodyFieldType::Mixed,SmallBodyFieldType::Ice,SmallBodyFieldType::DebrisDisk,SmallBodyFieldType::CrackedCluster};
          for(std::size_t i=0;i<4;++i)if(l.spawn[i].contains(e.position))return SystemWorkspaceCommand{SystemWorkspaceCommandKind::spawn_small_body_field,true,static_cast<int>(types[i])};
        }
      }return handled;
    }
  }
  if(e.type==InputEventType::LeftPressed&&SystemWorkspaceLayout::for_viewport(width,height).world_field.contains(e.position)&&spatial_&&viewport_&&!viewport_->hit_body(*spatial_,e.position.x,e.position.y)&&fleet_hits(e.position).empty()){
    if(const auto hit=small_bodies_.hit(e.position)){const auto it=std::ranges::find(snapshot_->small_body_fields,hit->field_id,&SmallBodyField::id);if(it!=snapshot_->small_body_fields.end()){small_body_field_=static_cast<std::size_t>(it-snapshot_->small_body_fields.begin());small_body_index_=hit->body_index;small_body_panel_=true;dragging_=false;return handled;}}
  }return std::nullopt;
}
void NativeSystemWorkspace::render_small_body_panel(DrawList& out,int width,int height){
  if(!snapshot_||snapshot_->survey_level!=SystemSurveyLevel::fully_surveyed)return;
  const auto l=layout_for(width,height);button(out,l.launcher,"BELTS & DEBRIS  "+std::to_string(snapshot_->small_body_fields.size()));
  button(out,l.motion,motion_running_?"Motion ON / Pause":"Paused / Resume");
  if(!small_body_panel_)return;
  out.overlay.emplace_back(FilledRectangle{l.panel,{5,17,28,252}});out.overlay.emplace_back(StrokedRectangle{l.panel,{77,151,178,255}});
  label(out,{l.panel.x,l.panel.y+6*l.scale,l.panel.width-76*l.scale,32*l.scale},"SMALL-BODY SURVEY",{164,221,237,255},15);
  button(out,l.close,"Close");button(out,l.previous,"Previous field");button(out,l.next,"Next field");
  float y=l.panel.y+82*l.scale;
  const auto row=[&](std::string value){label(out,{l.panel.x+3*l.scale,y,l.panel.width-6*l.scale,22*l.scale},std::move(value));y+=22*l.scale;};
  if(snapshot_->small_body_fields.empty())row("No small-body fields recorded in this system.");
  else {
    small_body_field_%=snapshot_->small_body_fields.size();const auto& f=snapshot_->small_body_fields[small_body_field_];small_body_index_%=f.visible_count;
    const auto body=small_body_instance(f,small_body_index_);
    row(std::string(small_body_field_name(f.type))+"  #"+std::to_string(f.id));
    row(number(f.inner_radius_au,f.planet_centered?5:2)+" - "+number(f.outer_radius_au,f.planet_centered?5:2)+" AU"+(f.planet_centered?" from parent":" from star"));
    row("Body "+std::to_string(body.id)+" / "+small_body_size_name(body)+" / "+std::string(small_body_material_name(body.material)));
    const auto resources=small_body_resources(f,body.id);std::string r;
    for(std::size_t i=0;i<resources.size();++i)if(resources[i]>0){if(!r.empty())r+="  ";r+=std::string(small_body_resource_name(static_cast<SmallBodyResource>(i)))+" "+number(resources[i],0);}
    row(r);
    const auto position=stellar::engine::analytic_orbit_position(body.orbit,snapshot_->simulation_days-f.epoch_days);
    const auto environment=small_body_environment(f,position,snapshot_->simulation_days);
    row("Hazard "+number(environment.hazard*100,0)+"%   Concealment "+number(environment.concealment*100,0)+"%   Travel x"+number(environment.navigation_multiplier));
    row("Scan difficulty x"+number(environment.scan_difficulty)+"   Density "+number(f.density,3));
    row("Variant "+std::to_string(body.asset_variant_id)+" / 4   Slow tumble / fixed visual speed");
    const double period_days=2*std::numbers::pi/std::abs(body.orbit.angular_speed);
    const double angle=std::fmod(std::atan2(position[1],position[0])*180/std::numbers::pi+360,360);
    row("Orbit "+number(period_days>=365.25?period_days/365.25:period_days,1)+(period_days>=365.25?" years":" days")+" / Position "+number(angle,3)+" deg");
    row(std::to_string(f.body_count)+" bodies / "+std::to_string(f.visible_count)+" visual samples");
    button(out,l.body,"Next body");button(out,l.large,"Next large");button(out,l.focus,"Focus body");
  }
  if(snapshot_->developer){
    button(out,l.debug,small_body_debug_?"Hide orbital bands / density debug":"Show orbital bands / density debug");
    for(std::size_t i=0;i<4;++i)button(out,l.spawn[i],std::array<std::string,4>{"+ Asteroid belt","+ Ice belt","+ Debris disk","+ Cracked debris"}[i]);
    const auto& cfg=small_body_configuration();label(out,{l.panel.x,l.panel.y+429*l.scale,l.panel.width,17*l.scale},"Orbit exponent "+number(cfg.orbit_exponent)+" / planet x"+number(cfg.planet_scale)+" / draws "+std::to_string(small_bodies_.statistics().batches),{137,176,195,255},11);
  }
}
}
