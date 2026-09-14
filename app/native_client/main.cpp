#include "map_camera.hpp"
#include "map_interaction.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using namespace stellar::core;
using namespace stellar::native_map;

struct Options {
  std::filesystem::path asset_root;
  std::optional<std::filesystem::path> smoke_screenshot;
  std::int64_t seed{1701};
  bool windowed{};
};
struct Rect { float x{},y{},w{},h{}; [[nodiscard]] bool contains(Point p)const{return p.x>=x&&p.y>=y&&p.x<x+w&&p.y<y+h;} };

[[nodiscard]] Options parse_options(int argc,char **argv){
  Options result; result.asset_root=stellar::engine::executable_directory();
  for(int i=1;i<argc;++i){ const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--windowed") result.windowed=true;
    else if(arg=="--smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.windowed=true;}
    else throw std::invalid_argument("Unknown or incomplete option: "+arg);
  }
  return result;
}

[[nodiscard]] Color spectral_color(std::optional<StellarClass> value){
  if(!value)return {185,200,225,220};
  switch(*value){
    case StellarClass::MRedDwarf:return {255,119,76,230};
    case StellarClass::KOrangeDwarf:return {255,167,92,235};
    case StellarClass::GYellowDwarf:return {255,230,150,245};
    case StellarClass::FYellowWhiteDwarf:return {255,247,215,245};
    case StellarClass::AWhiteStar:return {225,236,255,245};
    case StellarClass::HotBlueStar:return {126,174,255,245};
    case StellarClass::Giant:return {255,142,88,245};
    case StellarClass::WhiteDwarf:return {221,236,255,235};
    case StellarClass::NeutronStar:return {133,218,255,250};
    case StellarClass::BlackHole:return {126,92,178,230};
    case StellarClass::Protostar:return {255,112,160,230};
    case StellarClass::Pulsar:return {91,229,255,250};
  }
  return {210,220,240,230};
}
[[nodiscard]] std::string spectral_name(std::optional<StellarClass> value){
  if(!value)return "Unclassified";
  switch(*value){case StellarClass::MRedDwarf:return "M red dwarf";case StellarClass::KOrangeDwarf:return "K orange dwarf";case StellarClass::GYellowDwarf:return "G yellow dwarf";case StellarClass::FYellowWhiteDwarf:return "F yellow-white dwarf";case StellarClass::AWhiteStar:return "A white star";case StellarClass::HotBlueStar:return "Hot blue star";case StellarClass::Giant:return "Giant";case StellarClass::WhiteDwarf:return "White dwarf";case StellarClass::NeutronStar:return "Neutron star";case StellarClass::BlackHole:return "Black hole";case StellarClass::Protostar:return "Protostar";case StellarClass::Pulsar:return "Pulsar";} return "Unclassified";
}
void outline(DrawList &out,Rect r,Color c){out.lines.insert(out.lines.end(),{{{r.x,r.y},{r.x+r.w,r.y},c},{{r.x+r.w,r.y},{r.x+r.w,r.y+r.h},c},{{r.x+r.w,r.y+r.h},{r.x,r.y+r.h},c},{{r.x,r.y+r.h},{r.x,r.y},c}});}

class NativeCampaign final {
 public:
  NativeCampaign(const Options &options,int width,int height)
      : frame_(IntegratedAdaptiveCampaignRuntime::create_fresh(
            load_adaptive_research_strategic_runtime(std::filesystem::absolute(options.asset_root)/"Data/research/v1"),
            seed_fresh_campaign(options.seed,load_nearby_catalog(std::filesystem::absolute(options.asset_root)/"Data/astronomy/hyg-nearby-500-v1.json"),500,6,1,"terran_baseline")),
            StrategicClock{},CampaignFramePolicy::Player) {
    const auto &world=frame_.runtime().world().campaign();
    for(const auto &system:world.systems) by_id_.emplace(system.id,&system);
    refresh_knowledge();
    auto &network=frame_.runtime().world().lanes(); const auto view=network.build();
    for(const auto &lane:view){const auto first=std::min(lane.first_system_id,lane.second_system_id),second=std::max(lane.first_system_id,lane.second_system_id);lanes_.push_back({first,second,lane.length_light_years});}
    std::ranges::sort(lanes_,{},[](const auto &lane){return std::pair{lane.first_system_id,lane.second_system_id};});
    lanes_.erase(std::unique(lanes_.begin(),lanes_.end(),[](const auto&a,const auto&b){return a.first_system_id==b.first_system_id&&a.second_system_id==b.second_system_id;}),lanes_.end());
    fit_camera(width,height);
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    if(input.quit_requested)return false;
    const auto screen_width=static_cast<float>(width),screen_height=static_cast<float>(height);
    const Rect pause{18,18,72,28},speed{98,18,86,28};
    const Rect continue_button{screen_width*.5f-80,screen_height*.5f,160,34},exit_button{screen_width*.5f-80,screen_height*.5f+48,160,34};
    for(const auto &event:input.events){
      if(event.type==InputEventType::EscapePressed){toggle_menu();continue;}
      if(event.type==InputEventType::PointerCancelled){gesture_.cancel();continue;}
      if(event.type==InputEventType::LeftPressed){
        bool captured=menu_;
        if(menu_&&continue_button.contains(event.position)){toggle_menu();captured=true;}
        else if(menu_&&exit_button.contains(event.position))return false;
        else if(!menu_&&pause.contains(event.position)){if(frame_.clock().speed()==StrategicSpeed::Paused)frame_.clock().resume();else frame_.clock().set_speed(StrategicSpeed::Paused);captured=true;}
        else if(!menu_&&speed.contains(event.position)){cycle_speed();captured=true;}
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&gesture_.allows_world_drag())camera_.pan_pixels(event.delta.x,event.delta.y);gesture_.move(event.delta);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!gesture_.captured_by_ui())camera_.zoom_at(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_)(void)gesture_.release_as_world_click();}
    }
    if(advance_simulation)(void)frame_.advance(menu_?0.:elapsed);
    refresh_knowledge();
    return true;
  }

  [[nodiscard]] DrawList scene(int width,int height){
    const auto screen_width=static_cast<float>(width),screen_height=static_cast<float>(height);
    DrawList out; const auto &world=frame_.runtime().world().campaign(); const Color lane{49,74,108,125};
    for(const auto &edge:lanes_){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=by_id_.find(edge.first_system_id),b=by_id_.find(edge.second_system_id); if(a==by_id_.end()||b==by_id_.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    for(const auto &system:world.systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);if(p.x<-12||p.y<-12||p.x>width+12||p.y>height+12)continue;const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;const auto c=known?spectral_color(system.primary):Color{135,150,174,190};out.circles.push_back({p,selected?7.f:3.4f,{c.r,c.g,c.b,45}});out.circles.push_back({p,selected?4.2f:2.f,c});if(selected||(known&&camera_.pixels_per_world>7.f))out.text.push_back({{p.x+8,p.y-4},known?system.name:"Unknown",{205,222,245,235}});}
    const Rect pause{18,18,72,28},speed{98,18,86,28};outline(out,pause,{91,151,205,235});outline(out,speed,{91,151,205,235});out.text.push_back({{28,28},frame_.clock().speed()==StrategicSpeed::Paused?"PLAY":"PAUSE",{225,238,250,255}});out.text.push_back({{108,28},speed_text(),{225,238,250,255}});out.text.push_back({{18,58},"Day "+std::to_string(static_cast<int>(frame_.clock().simulation_days())),{154,181,211,235}});
    if(selected_id_){const auto found=by_id_.find(*selected_id_);if(found!=by_id_.end()){const bool known=known_.contains(*selected_id_);const float x=18,y=screen_height-82;out.text.push_back({{x,y},known?found->second->name:"Unknown system",{238,244,255,255}});out.text.push_back({{x,y+18},known?spectral_name(found->second->primary):"No survey data",{154,181,211,235}});}}
    if(menu_){const Rect box{screen_width*.5f-130,screen_height*.5f-74,260,174},cont{screen_width*.5f-80,screen_height*.5f,160,34},leave{screen_width*.5f-80,screen_height*.5f+48,160,34};outline(out,box,{116,174,225,255});outline(out,cont,{116,174,225,255});outline(out,leave,{116,174,225,255});out.text.push_back({{screen_width*.5f-44,screen_height*.5f-45},"PAUSED",{238,244,255,255}});out.text.push_back({{screen_width*.5f-34,screen_height*.5f+12},"CONTINUE",{238,244,255,255}});out.text.push_back({{screen_width*.5f-54,screen_height*.5f+60},"EXIT TO WINDOWS",{238,244,255,255}});}
    return out;
  }
 private:
  void fit_camera(int width,int height){const auto &systems=frame_.runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);}
  void toggle_menu(){menu_=!menu_;frame_.set_menu_open(menu_);if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame_.clock().speed();frame_.clock().set_speed(StrategicSpeed::Paused);frame_.pause_tactical_for_menu();}else{frame_.resume_tactical_after_menu();frame_.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=frame_.runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());}
  void cycle_speed(){auto &clock=frame_.clock();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  [[nodiscard]] std::string speed_text(){switch(frame_.clock().speed()==StrategicSpeed::Paused?frame_.clock().resume_speed():frame_.clock().speed()){case StrategicSpeed::Fast:return "SPEED 2X";case StrategicSpeed::VeryFast:return "SPEED 3X";case StrategicSpeed::Maximum:return "SPEED 8X";default:return "SPEED 1X";}}
  void select(Point pointer,int width,int height){float best=10.f;std::optional<int> id;for(const auto &system:frame_.runtime().world().campaign().systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);const auto d=std::hypot(p.x-pointer.x,p.y-pointer.y);if(d<best){best=d;id=system.id;}}selected_id_=id;}
  CampaignFrame frame_;
  Camera camera_;
  std::unordered_map<int,const StellarSystem*> by_id_;
  std::unordered_set<int> known_;
  std::vector<InterstellarLane> lanes_;
  std::optional<int> selected_id_;
  bool menu_{}; PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
};
}

int main(int argc,char **argv){
  try{
    const auto options=parse_options(argc,argv);const auto startup_begin=std::chrono::steady_clock::now();Window window("Stellar Continuum - Native Galaxy",1280,720,!options.windowed);NativeCampaign campaign(options,window.drawable_width(),window.drawable_height());const auto startup_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-startup_begin).count();
    auto prior=std::chrono::steady_clock::now();int frames=0;std::vector<double> frame_ms;bool discard_elapsed{};
    while(true){const auto now=std::chrono::steady_clock::now();const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();prior=now;const auto input=window.poll();if(!input.renderable()){discard_elapsed=true;if(!campaign.update(input,input.drawable_width,input.drawable_height,0.,false))break;std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;}const auto elapsed=discard_elapsed?0.:measured_elapsed;if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);discard_elapsed=false;if(!campaign.update(input,input.drawable_width,input.drawable_height,elapsed))break;const bool capture=options.smoke_screenshot&&++frames>=120;window.draw(campaign.scene(input.drawable_width,input.drawable_height),capture?options.smoke_screenshot:std::nullopt);if(capture){std::ranges::sort(frame_ms);const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);const auto p95=frame_ms[static_cast<std::size_t>(std::ceil(static_cast<double>(frame_ms.size())*.95))-1];std::cout<<std::fixed<<std::setprecision(3)<<"native-map smoke ok: gpu_driver="<<window.gpu_driver()<<" presentation="<<window.presentation_mode()<<" systems=500 frames="<<frames<<" startup_ms="<<startup_ms<<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())<<" frame_p95_ms="<<p95<<" screenshot="<<options.smoke_screenshot->string()<<'\n';break;}}
    return 0;
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
