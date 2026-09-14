#include "map_camera.hpp"
#include "map_interaction.hpp"
#include "native_campaign_session.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/build_version.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using namespace stellar::core;
using namespace stellar::native_map;

struct Options {
  std::filesystem::path asset_root;
  std::optional<std::filesystem::path> smoke_screenshot;
  std::filesystem::path save_path;
  std::int64_t seed{1701};
  bool windowed{};
  bool load{};
  bool save_path_overridden{};
};
struct Rect { float x{},y{},w{},h{}; [[nodiscard]] bool contains(Point p)const{return p.x>=x&&p.y>=y&&p.x<x+w&&p.y<y+h;} };

[[nodiscard]] Options parse_options(int argc,
#ifdef _WIN32
                                    wchar_t **argv
#else
                                    char **argv
#endif
){
  Options result; result.asset_root=stellar::engine::executable_directory();result.save_path=default_native_campaign_save_path();
  for(int i=1;i<argc;++i){
#ifdef _WIN32
    const std::wstring arg=argv[i];
    if(arg==L"--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg==L"--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg==L"--windowed") result.windowed=true;
    else if(arg==L"--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg==L"--load") result.load=true;
    else if(arg==L"--smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.windowed=true;}
#else
    const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--windowed") result.windowed=true;
    else if(arg=="--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg=="--load") result.load=true;
    else if(arg=="--smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.windowed=true;}
#endif
    else throw std::invalid_argument("Unknown or incomplete native client option.");
  }
  if(result.smoke_screenshot&&!result.save_path_overridden)throw std::invalid_argument("--smoke requires an isolated --save-path.");
  return result;
}

[[nodiscard]] std::string visible_notice(std::string value){
  std::ranges::replace(value,'\n',' ');std::ranges::replace(value,'\r',' ');
  constexpr std::size_t limit=96;if(value.size()<=limit)return value;
  std::size_t end=limit-3;while(end>0&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
  value.resize(end);value+="...";return value;
}

[[nodiscard]] std::string utf8_path(const std::filesystem::path &path){
  const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}

[[nodiscard]] std::string utc_timestamp(){
  const auto now=std::time(nullptr);std::tm utc{};
#ifdef _WIN32
  if(gmtime_s(&utc,&now)!=0)throw std::runtime_error("Unable to create a save timestamp.");
#else
  if(gmtime_r(&now,&utc)==nullptr)throw std::runtime_error("Unable to create a save timestamp.");
#endif
  std::ostringstream out;out<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%S+00:00");return out.str();
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

[[nodiscard]] std::unique_ptr<NativeCampaignSession> make_session(const Options &options){
  const auto asset_root=std::filesystem::absolute(options.asset_root);
  const auto research_root=asset_root/"Data/research/v1";
  if(options.load){
    return NativeCampaignSession::load_startup(research_root,options.save_path,STELLAR_GAME_VERSION,
      [](const PlayerCampaignRestorationProgress &progress){std::cerr<<"Loading: "<<static_cast<int>(progress.fraction*100.)<<"% "<<progress.status<<'\n';});
  }
  return NativeCampaignSession::create_fresh(
    IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      seed_persistable_fresh_campaign(options.seed,
        load_nearby_catalog(asset_root/"Data/astronomy/hyg-nearby-500-v1.json"),
        {utc_timestamp(),500,6,1,"terran_baseline"})),
    research_root,options.save_path,STELLAR_GAME_VERSION);
}

class NativeCampaign final {
 public:
  NativeCampaign(std::unique_ptr<NativeCampaignSession> session,int width,int height)
      : session_(std::move(session)) {
    refresh_knowledge();
    fit_camera(width,height);
  }

  void prepare_smoke_ui(){if(!menu_)toggle_menu();smoke_save_pending_=true;}
  [[nodiscard]] bool smoke_save_succeeded()const noexcept{return session_->notice().kind==SessionNoticeKind::Saved;}
  [[nodiscard]] std::size_t system_count()const noexcept{return session_->frame().runtime().world().campaign().systems.size();}

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    const auto timestamp=utc_timestamp();
    if(session_->service(timestamp,menu_)){selected_id_.reset();pre_menu_speed_=StrategicSpeed::Normal;fit_camera(width,height);refresh_knowledge();}
    if(session_->exit_ready())return false;
    if(input.quit_requested)session_->request_exit();
    const auto screen_width=static_cast<float>(width),screen_height=static_cast<float>(height);
    const Rect pause{18,18,72,28},speed{98,18,86,28};
    const Rect continue_button{screen_width*.5f-80,screen_height*.5f-30,160,32},save_button{screen_width*.5f-80,screen_height*.5f+10,160,32},load_button{screen_width*.5f-80,screen_height*.5f+50,160,32},exit_button{screen_width*.5f-80,screen_height*.5f+90,160,32};
    for(const auto &event:input.events){
      if(event.type==InputEventType::EscapePressed){toggle_menu();continue;}
      if(event.type==InputEventType::PointerCancelled){gesture_.cancel();continue;}
      if(event.type==InputEventType::LeftPressed){
        bool captured=menu_;
        if(menu_&&continue_button.contains(event.position)){toggle_menu();captured=true;}
        else if(menu_&&save_button.contains(event.position)){session_->request_save();captured=true;}
        else if(menu_&&load_button.contains(event.position)){session_->request_load();captured=true;}
        else if(menu_&&exit_button.contains(event.position)){session_->request_exit();captured=true;}
        else if(!menu_&&pause.contains(event.position)){if(session_->frame().clock().speed()==StrategicSpeed::Paused)session_->frame().clock().resume();else session_->frame().clock().set_speed(StrategicSpeed::Paused);captured=true;}
        else if(!menu_&&speed.contains(event.position)){cycle_speed();captured=true;}
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&gesture_.allows_world_drag())camera_.pan_pixels(event.delta.x,event.delta.y);gesture_.move(event.delta);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!gesture_.captured_by_ui())camera_.zoom_at(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_)(void)gesture_.release_as_world_click();}
    }
    if(advance_simulation){(void)session_->advance(menu_?0.:elapsed,timestamp);if(smoke_save_pending_){smoke_save_pending_=false;session_->request_save();}}
    refresh_knowledge();
    return true;
  }

  [[nodiscard]] DrawList scene(int width,int height){
    const auto screen_width=static_cast<float>(width),screen_height=static_cast<float>(height);
    DrawList out; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    for(const auto &system:world.systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);if(p.x<-12||p.y<-12||p.x>width+12||p.y>height+12)continue;const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;const auto c=known?spectral_color(system.primary):Color{135,150,174,190};out.circles.push_back({p,selected?7.f:3.4f,{c.r,c.g,c.b,45}});out.circles.push_back({p,selected?4.2f:2.f,c});if(selected||(known&&camera_.pixels_per_world>7.f))out.text.push_back({{p.x+8,p.y-4},known?system.name:"Unknown",{205,222,245,235}});}
    const Rect pause{18,18,72,28},speed{98,18,86,28};outline(out,pause,{91,151,205,235});outline(out,speed,{91,151,205,235});out.text.push_back({{28,28},session_->frame().clock().speed()==StrategicSpeed::Paused?"PLAY":"PAUSE",{225,238,250,255}});out.text.push_back({{108,28},speed_text(),{225,238,250,255}});out.text.push_back({{18,58},"Day "+std::to_string(static_cast<int>(session_->frame().clock().simulation_days())),{154,181,211,235}});
    if(selected_id_){const auto found=cache.systems_by_id.find(*selected_id_);if(found!=cache.systems_by_id.end()){const bool known=known_.contains(*selected_id_);const float x=18,y=screen_height-82;out.text.push_back({{x,y},known?found->second->name:"Unknown system",{238,244,255,255}});out.text.push_back({{x,y+18},known?spectral_name(found->second->primary):"No survey data",{154,181,211,235}});}}
    const auto &notice=session_->notice();if(notice.kind!=SessionNoticeKind::None){auto message=notice.message;if(notice.kind==SessionNoticeKind::Loading)message+=" "+std::to_string(static_cast<int>(notice.progress*100.))+"%";out.text.push_back({{18,80},visible_notice(std::move(message)),notice.kind==SessionNoticeKind::Failure?Color{255,133,123,255}:Color{154,211,183,255}});}
    if(menu_){const Rect box{screen_width*.5f-130,screen_height*.5f-100,260,246},cont{screen_width*.5f-80,screen_height*.5f-30,160,32},save{screen_width*.5f-80,screen_height*.5f+10,160,32},load{screen_width*.5f-80,screen_height*.5f+50,160,32},leave{screen_width*.5f-80,screen_height*.5f+90,160,32};outline(out,box,{116,174,225,255});outline(out,cont,{116,174,225,255});outline(out,save,{116,174,225,255});outline(out,load,{116,174,225,255});outline(out,leave,{116,174,225,255});out.text.push_back({{screen_width*.5f-44,screen_height*.5f-72},"PAUSED",{238,244,255,255}});out.text.push_back({{screen_width*.5f-34,screen_height*.5f-19},"CONTINUE",{238,244,255,255}});out.text.push_back({{screen_width*.5f-17,screen_height*.5f+21},"SAVE",{238,244,255,255}});out.text.push_back({{screen_width*.5f-17,screen_height*.5f+61},"LOAD",{238,244,255,255}});out.text.push_back({{screen_width*.5f-54,screen_height*.5f+101},"EXIT TO WINDOWS",{238,244,255,255}});}
    return out;
  }
 private:
  void fit_camera(int width,int height){const auto &systems=session_->frame().runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);}
  void toggle_menu(){menu_=!menu_;auto &frame=session_->frame();frame.set_menu_open(menu_);if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame.clock().speed();frame.clock().set_speed(StrategicSpeed::Paused);frame.pause_tactical_for_menu();}else{frame.resume_tactical_after_menu();frame.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=session_->frame().runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());}
  void cycle_speed(){auto &clock=session_->frame().clock();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  [[nodiscard]] std::string speed_text(){const auto &clock=session_->frame().clock();switch(clock.speed()==StrategicSpeed::Paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Fast:return "SPEED 2X";case StrategicSpeed::VeryFast:return "SPEED 3X";case StrategicSpeed::Maximum:return "SPEED 8X";default:return "SPEED 1X";}}
  void select(Point pointer,int width,int height){float best=10.f;std::optional<int> id;for(const auto &system:session_->frame().runtime().world().campaign().systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);const auto d=std::hypot(p.x-pointer.x,p.y-pointer.y);if(d<best){best=d;id=system.id;}}selected_id_=id;}
  std::unique_ptr<NativeCampaignSession> session_;
  Camera camera_;
  std::unordered_set<int> known_;
  std::optional<int> selected_id_;
  bool menu_{};bool smoke_save_pending_{};PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
};
}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
  try{
    const auto options=parse_options(argc,argv);const auto startup_begin=std::chrono::steady_clock::now();auto session=make_session(options);Window window("Stellar Continuum - Native Galaxy",1280,720,!options.windowed);NativeCampaign campaign(std::move(session),window.drawable_width(),window.drawable_height());if(options.smoke_screenshot)campaign.prepare_smoke_ui();const auto startup_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-startup_begin).count();
    auto prior=std::chrono::steady_clock::now();int frames=0;std::vector<double> frame_ms;bool discard_elapsed{};
    while(true){const auto now=std::chrono::steady_clock::now();const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();prior=now;const auto input=window.poll();if(!input.renderable()){discard_elapsed=true;if(!campaign.update(input,input.drawable_width,input.drawable_height,0.,false))break;std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;}const auto elapsed=discard_elapsed?0.:measured_elapsed;if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);discard_elapsed=false;if(!campaign.update(input,input.drawable_width,input.drawable_height,elapsed))break;const bool capture=options.smoke_screenshot&&++frames>=120;window.draw(campaign.scene(input.drawable_width,input.drawable_height),capture?options.smoke_screenshot:std::nullopt);if(capture){if(!campaign.smoke_save_succeeded())throw std::runtime_error("Native session smoke did not complete its manual save.");std::ranges::sort(frame_ms);const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);const auto p95=frame_ms[static_cast<std::size_t>(std::ceil(static_cast<double>(frame_ms.size())*.95))-1];std::cout<<std::fixed<<std::setprecision(3)<<"native-map smoke ok: gpu_driver="<<window.gpu_driver()<<" presentation="<<window.presentation_mode()<<" systems="<<campaign.system_count()<<" frames="<<frames<<" startup_ms="<<startup_ms<<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())<<" frame_p95_ms="<<p95<<" save=ok screenshot="<<utf8_path(*options.smoke_screenshot)<<'\n';break;}}
    return 0;
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
