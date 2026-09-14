#include "map_camera.hpp"
#include "map_interaction.hpp"
#include "native_campaign_session.hpp"
#include "native_fleet_controller.hpp"
#include "native_fleet_presentation.hpp"
#include "native_fleet_workspace.hpp"
#include "native_research_controller.hpp"
#include "native_research_workspace.hpp"
#include "native_shipyard_controller.hpp"
#include "native_shipyard_workspace.hpp"
#include "native_ui_layout.hpp"

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
using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;
using namespace stellar::native_research;
using namespace stellar::native_research_ui;
using namespace stellar::native_shipyard;
using namespace stellar::native_shipyard_ui;

struct Options {
  std::filesystem::path asset_root;
  std::optional<std::filesystem::path> smoke_screenshot;
  std::filesystem::path save_path;
  std::int64_t seed{1701};
  int window_width{1280};
  int window_height{720};
  bool windowed{};
  bool load{};
  bool research_smoke{};
  bool fleet_smoke{};
  bool shipyard_smoke{};
  bool save_path_overridden{};
};

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
    else if(arg==L"--width"&&i+1<argc) result.window_width=std::stoi(argv[++i]);
    else if(arg==L"--height"&&i+1<argc) result.window_height=std::stoi(argv[++i]);
    else if(arg==L"--smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.windowed=true;}
    else if(arg==L"--research-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.research_smoke=true;result.windowed=true;}
    else if(arg==L"--fleet-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.fleet_smoke=true;result.windowed=true;}
    else if(arg==L"--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.shipyard_smoke=true;result.windowed=true;}
#else
    const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--windowed") result.windowed=true;
    else if(arg=="--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg=="--load") result.load=true;
    else if(arg=="--width"&&i+1<argc) result.window_width=std::stoi(argv[++i]);
    else if(arg=="--height"&&i+1<argc) result.window_height=std::stoi(argv[++i]);
    else if(arg=="--smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.windowed=true;}
    else if(arg=="--research-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.research_smoke=true;result.windowed=true;}
    else if(arg=="--fleet-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.fleet_smoke=true;result.windowed=true;}
    else if(arg=="--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.shipyard_smoke=true;result.windowed=true;}
#endif
    else throw std::invalid_argument("Unknown or incomplete native client option.");
  }
  if(result.smoke_screenshot&&!result.save_path_overridden)throw std::invalid_argument("--smoke requires an isolated --save-path.");
  if(static_cast<int>(result.research_smoke)+static_cast<int>(result.fleet_smoke)+static_cast<int>(result.shipyard_smoke)>1)throw std::invalid_argument("Choose one native graphical smoke mode.");
  if(result.fleet_smoke&&!result.load)throw std::invalid_argument("--fleet-smoke requires --load with a player campaign fixture.");
  if(result.window_width<640||result.window_width>3840||result.window_height<360||result.window_height>2160)throw std::invalid_argument("Native window dimensions are out of range.");
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
void fill(DrawList &out,UiRect bounds,Color color){out.overlay.emplace_back(FilledRectangle{bounds,color});}
void stroke(DrawList &out,UiRect bounds,Color color){out.overlay.emplace_back(StrokedRectangle{bounds,color});}
[[nodiscard]] Point center(UiRect bounds)noexcept{return {bounds.x+bounds.width*.5f,bounds.y+bounds.height*.5f};}
void label(DrawList &out, UiRect bounds, std::string value, Color color,
           int size, float scale, FontFace face = FontFace::Interface) {
  out.overlay.emplace_back(Text{
      {bounds.x + bounds.width * .5f,
       bounds.y + (bounds.height - static_cast<float>(size)) * .5f},
      std::move(value), color, size, bounds.width - 12.f * scale, bounds,
      TextAlign::Center, face});
}

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

struct ResearchStamp {
  std::uint64_t campaign_generation{};
  std::int64_t research_revision{};
  double credits{};
  double funding_fraction{};
  double free_labs{};
  double total_labs{};
  std::string player_species_id;
  bool operator==(const ResearchStamp &) const = default;
};

class NativeCampaign final {
 public:
  NativeCampaign(std::unique_ptr<NativeCampaignSession> session,int width,int height)
      : session_(std::move(session)) {
    refresh_knowledge();
    fit_camera(width,height);
    refresh_fleets(true);
  }

  void prepare_smoke_ui(){if(!menu_)toggle_menu();smoke_save_pending_=true;}
  void prepare_research_smoke(int width,int height,bool execute_action){
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Research smoke input closed the campaign.");
    };
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click({main_layout.research.x+main_layout.research.width*.5f,
           main_layout.research.y+main_layout.research.height*.5f});
    if(!research_workspace_.visible()||!research_workspace_.window())
      throw std::runtime_error("Research smoke could not open the workspace.");
    if(execute_action){
      const auto card=research_workspace_.first_actionable_card(width,height);
      if(!card)throw std::runtime_error("Research smoke found no visible affordable program.");
      click({card->x+card->width*.5f,card->y+card->height*.5f});
      const auto workspace_layout=ResearchWorkspaceLayout::for_viewport(
          width,height,research_workspace_.window()->domain_tabs.size());
      click({workspace_layout.action.x+workspace_layout.action.width*.5f,
             workspace_layout.action.y+workspace_layout.action.height*.5f});
      if(!last_research_command_accepted_)
        throw std::runtime_error("Research smoke canonical action was rejected.");
      const auto refreshed_layout=NativeUiLayout::for_viewport(width,height);
      if(session_->frame().clock().speed()==StrategicSpeed::Paused)
        click({refreshed_layout.pause.x+refreshed_layout.pause.width*.5f,
               refreshed_layout.pause.y+refreshed_layout.pause.height*.5f});
    }
    smoke_research_node_=research_workspace_.selected_id();
  }
  void prepare_fleet_smoke(int width,int height){
    const auto click=[&](Point point,InputEventType press=InputEventType::LeftPressed){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{press,point},
                    {press==InputEventType::LeftPressed
                         ?InputEventType::LeftReleased
                         :InputEventType::RightReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Fleet smoke input closed the campaign.");
    };
    if(!fleet_workspace_.view()||fleet_workspace_.view()->own_fleets.empty())
      throw std::runtime_error("Fleet smoke loaded no owned fleet fixture.");
    const auto &fleets=fleet_workspace_.view()->own_fleets;
    auto selected=std::ranges::find_if(fleets,[](const auto &fleet){
      return fleet.destination_system_id.has_value();
    });
    if(selected==fleets.end())selected=std::ranges::find_if(
        fleets,[](const auto &fleet){return fleet.role==FleetRole::Colony;});
    if(selected==fleets.end())selected=fleets.begin();
    const auto index=static_cast<std::size_t>(selected-fleets.begin());
    const auto selected_fleet_id=selected->id;
    const auto selected_current_system=selected->current_system_id;
    const auto selected_destination=selected->destination_system_id;
    const auto layout=FleetWorkspaceLayout::for_viewport(width,height);
    click({layout.list.x+12.f*layout.scale,
           layout.list.y+(static_cast<float>(index)*45.f+20.f)*layout.scale});
    if(fleet_controller_.selection()!=std::optional<int>{selected_fleet_id})
      throw std::runtime_error("Fleet smoke mouse selection failed.");
    smoke_fleet_id_=selected_fleet_id;
    if(!selected_destination){
      const auto panel=layout.panel;
      std::optional<int> target;
      Point target_point;
      double longest_eta=-1.;
      for(const auto &system:session_->frame().runtime().world().campaign().systems){
        if(selected_current_system==system.id)continue;
        const auto candidate=fleet_controller_.preview_selected_route(
            session_->frame(),session_->cache().generation,system.id);
        const auto point=camera_.project({system.position.x,system.position.y},
                                         width,height);
        if(candidate.command_available&&candidate.estimated_transit_days&&
           *candidate.estimated_transit_days>longest_eta&&point.x>=0&&
           point.y>=0&&point.x<width&&point.y<height&&!panel.contains(point)){
          target=system.id;target_point=point;
          longest_eta=*candidate.estimated_transit_days;
        }
      }
      if(!target)throw std::runtime_error(
          "Fleet smoke found no visible authoritative route target.");
      click(target_point,InputEventType::RightPressed);
      if(!fleet_workspace_.preview()||
         !fleet_workspace_.preview()->command_available)
        throw std::runtime_error("Fleet smoke right-click preview failed.");
      click(center(layout.confirm));
      if(!last_fleet_command_accepted_)
        throw std::runtime_error("Fleet smoke confirmation was rejected.");
      smoke_fleet_destination_=target;
      const auto main_layout=NativeUiLayout::for_viewport(width,height);
      if(session_->frame().clock().speed()==StrategicSpeed::Paused)
        click(center(main_layout.pause));
    }else smoke_fleet_destination_=selected_destination;
  }
  void prepare_shipyard_smoke(int width,int height){
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Shipyard smoke input closed the campaign.");
    };
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click(center(main_layout.shipyard));
    if(!shipyard_workspace_.visible()||!shipyard_workspace_.view())
      throw std::runtime_error("Shipyard smoke could not open the workspace.");
    const auto layout=ShipyardWorkspaceLayout::for_viewport(width,height);
    const auto first_actionable_index=[&]()->std::optional<std::size_t>{
      const auto &designs=shipyard_workspace_.view()->available_designs;
      const auto found=std::ranges::find(designs,true,&NativeShipDesign::can_start);
      if(found==designs.end())return std::nullopt;
      return static_cast<std::size_t>(found-designs.begin());
    };
    if(first_actionable_index()){
      if(session_->frame().clock().speed()==StrategicSpeed::Paused)
        click(center(main_layout.pause));
      smoke_shipyard_start_was_running_=
          session_->frame().clock().speed()!=StrategicSpeed::Paused;
      InputSnapshot advancing;
      advancing.drawable_width=width;
      advancing.drawable_height=height;
      if(!update(advancing,width,height,.25,true))
        throw std::runtime_error("Shipyard smoke closed while advancing.");
      const auto index=first_actionable_index();
      if(!index)throw std::runtime_error(
          "Shipyard readiness changed before the running Start input.");
      click({layout.designs.x+20.f*layout.scale,
             layout.designs.y+(27.f+static_cast<float>(*index)*58.f+24.f)*layout.scale});
      click(center(layout.action));
      if(!last_shipyard_command_accepted_)
        throw std::runtime_error("Shipyard smoke canonical start was rejected.");
      smoke_shipyard_order_started_=true;
    }
    if(shipyard_workspace_.view()&&!shipyard_workspace_.view()->orders.empty()){
      if(session_->frame().clock().speed()==StrategicSpeed::Paused)
        click(center(main_layout.pause));
      smoke_shipyard_was_running_=
          session_->frame().clock().speed()!=StrategicSpeed::Paused;
      click({layout.orders.x+20.f*layout.scale,
             layout.orders.y+51.f*layout.scale});
      click(center(layout.action));
      smoke_shipyard_order_id_=shipyard_workspace_.view()->orders.front().order_id;
    }
  }
  void request_smoke_save(){session_->request_save();}
  [[nodiscard]] bool smoke_save_succeeded()const{return session_->notice().kind==SessionNoticeKind::Saved;}
  [[nodiscard]] std::size_t system_count()const{return session_->frame().runtime().world().campaign().systems.size();}
  [[nodiscard]] std::string research_smoke_status()const{
    if(!smoke_research_node_||!research_workspace_.window())return "unavailable";
    const auto found=std::ranges::find(research_workspace_.window()->nodes,
                                      *smoke_research_node_,
                                      &NativeResearchNode::id);
    if(found==research_workspace_.window()->nodes.end())return "unavailable";
    std::ostringstream out;
    out<<*smoke_research_node_<<":"<<(found->active?"active":"inactive")
       <<":"<<std::fixed<<std::setprecision(6)<<found->total_progress;
    return out.str();
  }
  [[nodiscard]] std::string fleet_smoke_status()const{
    if(!smoke_fleet_id_||!smoke_fleet_destination_)return "unavailable";
    const auto &fleets=session_->frame().runtime().world().campaign().fleets;
    const auto found=std::ranges::find(fleets,*smoke_fleet_id_,&FleetState::id);
    if(found==fleets.end())return "unavailable";
    std::ostringstream out;
    out<<found->id<<":"<<*smoke_fleet_destination_<<":"
       <<found->mission_order_revision<<":"<<std::fixed
       <<std::setprecision(6)<<found->transit_progress;
    return out.str();
  }
  [[nodiscard]] std::string shipyard_smoke_status()const{
    if(!shipyard_workspace_.view())return "unavailable";
    std::ostringstream out;
    out<<(smoke_shipyard_order_started_
             ?"started"
             :shipyard_workspace_.view()->orders.empty()?"locked":"orders")<<":"
       <<shipyard_workspace_.view()->shipyard_revision<<":"
       <<shipyard_workspace_.view()->orders.size();
    if(smoke_shipyard_order_id_)out<<":"<<*smoke_shipyard_order_id_;
    out<<":"<<(smoke_shipyard_start_was_running_?"start-running":"start-not-run")
       <<":"<<(smoke_shipyard_was_running_&&
                         session_->frame().clock().speed()==StrategicSpeed::Paused
                    ?"running-to-paused":"clock-unproven");
    return out.str();
  }
  [[nodiscard]] bool wants_text_input() const noexcept {
    return research_workspace_.wants_text_input() &&
           !shipyard_workspace_.visible();
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    pointer_=input.pointer;
    const auto timestamp=utc_timestamp();
    if(session_->service(timestamp,menu_)){
      selected_id_.reset();
      pre_menu_speed_=StrategicSpeed::Normal;
      fit_camera(width,height);
      refresh_knowledge();
      research_workspace_.discard_campaign();
      projected_research_stamp_.reset();
      fleet_workspace_.discard_campaign();
      shipyard_workspace_.discard_campaign();
      fleet_marker_offsets_.clear();
      pending_fleet_preview_.reset();
      refresh_fleets(true);
      if(research_workspace_.visible())refresh_research(true);
      if(shipyard_workspace_.visible())refresh_shipyard(true);
    }
    if(session_->exit_ready())return false;
    if(input.quit_requested)session_->request_exit();
    const auto layout=NativeUiLayout::for_viewport(width,height);
    for(const auto &event:input.events){
      if(event.type==InputEventType::EscapePressed){
        if(shipyard_workspace_.visible())shipyard_workspace_.close();
        else if(research_workspace_.visible())research_workspace_.close();
        else toggle_menu();
        continue;
      }
      if(event.type==InputEventType::PointerCancelled){
        gesture_.cancel();
        (void)research_workspace_.handle(event,width,height);
        (void)shipyard_workspace_.handle(event,width,height);
        continue;
      }
      if(shipyard_workspace_.visible()){
        const auto command=shipyard_workspace_.handle(event,width,height);
        if(command.kind!=ShipyardWorkspaceCommandKind::None)
          execute_shipyard(command);
        if(command.captured)continue;
      }
      if(research_workspace_.visible()){
        const auto command=research_workspace_.handle(event,width,height);
        if(command.kind==WorkspaceCommandKind::Select){
          research_controller_.select(command.node_id);
          refresh_research(true);
        }else if(command.kind==WorkspaceCommandKind::Execute){
          execute_research(command);
        }
        if(command.captured||event.type==InputEventType::TextEntered||
           event.type==InputEventType::BackspacePressed)continue;
      }
      if(!menu_&&!research_workspace_.visible()&&!shipyard_workspace_.visible()){
        const auto markers=fleet_markers(width,height);
        const auto target=event.type==InputEventType::RightPressed
                              ?system_hit(event.position,width,height)
                              :std::nullopt;
        const auto fleet_command=fleet_workspace_.handle(
            event,width,height,markers,target);
        handle_fleet_command(fleet_command);
        if(fleet_command.captured){
          if(event.type==InputEventType::LeftPressed)gesture_.begin(true);
          continue;
        }
      }
      if(event.type==InputEventType::LeftPressed){
        const auto action=layout.hit(event.position,menu_);bool captured=menu_||action!=UiAction::None;
        if(action==UiAction::Continue)toggle_menu();
        else if(action==UiAction::Save)session_->request_save();
        else if(action==UiAction::Load)session_->request_load();
        else if(action==UiAction::Exit)session_->request_exit();
        else if(action==UiAction::Pause){if(session_->frame().clock().speed()==StrategicSpeed::Paused)session_->frame().clock().resume();else session_->frame().clock().set_speed(StrategicSpeed::Paused);}
        else if(action==UiAction::Speed)cycle_speed();
        else if(action==UiAction::Research){
          if(research_workspace_.visible())research_workspace_.close();
          else{shipyard_workspace_.close();research_workspace_.open();refresh_research(true);}
          captured=true;
        }
        else if(action==UiAction::Shipyard){
          if(shipyard_workspace_.visible())shipyard_workspace_.close();
          else{research_workspace_.close();shipyard_workspace_.open();refresh_shipyard(true);}
          captured=true;
        }
        if(research_workspace_.visible()||shipyard_workspace_.visible())captured=true;
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&gesture_.allows_world_drag())camera_.pan_pixels(event.delta.x,event.delta.y);gesture_.move(event.delta);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!gesture_.captured_by_ui())camera_.zoom_at(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_||research_workspace_.visible()||shipyard_workspace_.visible())(void)gesture_.release_as_world_click();}
    }
    if(research_workspace_.take_refresh_request())refresh_research(true);
    if(advance_simulation){
      (void)session_->advance(menu_?0.:elapsed,timestamp);
      research_refresh_elapsed_+=elapsed;
      refresh_research(false);
      fleet_refresh_elapsed_+=elapsed;
      refresh_fleets(false);
      shipyard_refresh_elapsed_+=elapsed;
      refresh_shipyard(false);
      if(smoke_save_pending_){smoke_save_pending_=false;session_->request_save();}
    }
    refresh_knowledge();
    return true;
  }

  [[nodiscard]] DrawList scene(int width,int height){
    const auto screen_height=static_cast<float>(height);
    DrawList out; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    for(const auto &system:world.systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);if(p.x<-12||p.y<-12||p.x>width+12||p.y>height+12)continue;const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;const auto c=known?spectral_color(system.primary):Color{135,150,174,190};out.circles.push_back({p,selected?7.f:3.4f,{c.r,c.g,c.b,45}});out.circles.push_back({p,selected?4.2f:2.f,c});if(selected||(known&&camera_.pixels_per_world>7.f))out.text.push_back({{p.x+8,p.y-4},known?system.name:"Unknown",{205,222,245,235}});}
    if(const auto &preview=fleet_workspace_.preview();preview){
      for(std::size_t index=1;index<preview->route_system_ids.size();++index){
        if(!known_.contains(preview->route_system_ids[index-1])||
           !known_.contains(preview->route_system_ids[index]))continue;
        const auto from=cache.systems_by_id.find(preview->route_system_ids[index-1]);
        const auto to=cache.systems_by_id.find(preview->route_system_ids[index]);
        if(from==cache.systems_by_id.end()||to==cache.systems_by_id.end())continue;
        out.lines.push_back({camera_.project({from->second->position.x,from->second->position.y},width,height),camera_.project({to->second->position.x,to->second->position.y},width,height),preview->command_available?Color{102,232,164,220}:Color{255,190,112,210}});
      }
    }
    const auto layout = NativeUiLayout::for_viewport(width, height);
    const Color panel{7, 17, 32, 238};
    const Color button{12, 31, 54, 245};
    const Color hover{24, 61, 94, 250};
    const Color selected{23, 67, 102, 255};
    const Color border{91, 151, 205, 235};
    fill(out, layout.pause, layout.pause.contains(pointer_) ? hover : button);
    stroke(out, layout.pause, border);
    label(out, layout.pause,
          session_->frame().clock().speed() == StrategicSpeed::Paused
              ? "PLAY"
              : "PAUSE",
          {225, 238, 250, 255}, layout.control_font_pixels, layout.scale);
    fill(out, layout.speed, layout.speed.contains(pointer_) ? hover : button);
    stroke(out, layout.speed, border);
    label(out, layout.speed, speed_text(), {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.research,
         research_workspace_.visible()
             ? selected
             : layout.research.contains(pointer_) ? hover : button);
    stroke(out, layout.research,
           research_workspace_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.research, "RESEARCH", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.shipyard,
         shipyard_workspace_.visible()
             ? selected
             : layout.shipyard.contains(pointer_) ? hover : button);
    stroke(out, layout.shipyard,
           shipyard_workspace_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.shipyard, "SHIPYARD", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y + 2.f * layout.scale},
        "Day " + std::to_string(static_cast<int>(
                     session_->frame().clock().simulation_days())),
        {154, 181, 211, 235}, layout.metric_font_pixels,
        layout.day_text.width, layout.day_text});
    if(selected_id_){const auto found=cache.systems_by_id.find(*selected_id_);if(found!=cache.systems_by_id.end()){const bool known=known_.contains(*selected_id_);const float x=18,y=screen_height-82;out.text.push_back({{x,y},known?found->second->name:"Unknown system",{238,244,255,255}});out.text.push_back({{x,y+18},known?spectral_name(found->second->primary):"No survey data",{154,181,211,235}});}}
    const auto &notice = session_->notice();
    if (notice.kind != SessionNoticeKind::None) {
      auto message = notice.message;
      if (notice.kind == SessionNoticeKind::Loading) {
        message += " " +
                   std::to_string(static_cast<int>(notice.progress * 100.)) +
                   "%";
      }
      out.overlay.emplace_back(Text{
          {layout.status_text.x,
           layout.status_text.y + 2.f * layout.scale},
          visible_notice(std::move(message)),
          notice.kind == SessionNoticeKind::Failure
              ? Color{255, 133, 123, 255}
              : Color{154, 211, 183, 255},
          layout.metric_font_pixels, layout.status_text.width,
          layout.status_text});
    }
    if (menu_) {
      fill(out, layout.menu_panel, panel);
      stroke(out, layout.menu_panel, {116, 174, 225, 255});
      label(out, layout.menu_heading, "PAUSED", {238, 244, 255, 255},
            layout.heading_font_pixels, layout.scale, FontFace::Heading);
      const auto draw_button = [&](UiRect bounds, std::string text) {
        fill(out, bounds, bounds.contains(pointer_) ? hover : button);
        stroke(out, bounds, border);
        label(out, bounds, std::move(text), {238, 244, 255, 255},
              layout.control_font_pixels, layout.scale);
      };
      draw_button(layout.continue_button, "CONTINUE");
      draw_button(layout.save_button, "SAVE");
      draw_button(layout.load_button, "LOAD");
      draw_button(layout.exit_button, "EXIT TO WINDOWS");
    }
    if(!menu_&&!research_workspace_.visible()&&!shipyard_workspace_.visible())
      fleet_workspace_.render(out,width,height,fleet_markers(width,height));
    research_workspace_.render(out, width, height);
    shipyard_workspace_.render(out, width, height);
    return out;
  }
 private:
  [[nodiscard]] ResearchStamp current_research_stamp() const {
    auto &frame=session_->frame();
    const auto &world=frame.runtime().world().campaign();
    const auto *state=frame.runtime().research().try_get_civilization(
        world.player_civilization_id);
    if(!state)throw std::runtime_error("The player has no research state.");
    ResearchStamp stamp;
    stamp.campaign_generation=session_->cache().generation;
    stamp.research_revision=state->revision();
    stamp.free_labs=state->free_effective_labs();
    stamp.total_labs=state->total_effective_research_labs();
    const auto player=std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
    if(player!=world.civilizations.end())stamp.player_species_id=player->species_id;
    const auto economy=std::ranges::find(world.economies,
                                         world.player_civilization_id,
                                         &CivilizationEconomy::civilization_id);
    if(economy!=world.economies.end()){
      stamp.credits=economy->credits;
      stamp.funding_fraction=economy->last_research_funding_fraction;
    }
    return stamp;
  }

  void refresh_research(bool force){
    if(!research_workspace_.visible())return;
    const auto stamp=current_research_stamp();
    if(!force&&projected_research_stamp_&&*projected_research_stamp_==stamp)
      return;
    if(!force&&research_refresh_elapsed_<.2)return;
    auto view=research_controller_.build(session_->frame(),
                                         stamp.campaign_generation,{});
    research_workspace_.set_window(std::move(view));
    projected_research_stamp_=stamp;
    research_refresh_elapsed_=0.;
  }

  void execute_research(const WorkspaceCommand &command){
    const auto &view=research_workspace_.window();
    if(!view){
      research_workspace_.set_notice("Research details are still loading.",false);
      return;
    }
    const auto outcome=research_controller_.execute(
        session_->frame(),session_->cache().generation,
        view->research_revision,view->funding_revision,
        command.intent,command.node_id);
    research_workspace_.set_notice(outcome.message,outcome.accepted);
    last_research_command_accepted_=outcome.accepted;
    if(outcome.accepted)smoke_research_node_=command.node_id;
    refresh_research(true);
  }

  void refresh_shipyard(bool force){
    if(!shipyard_workspace_.visible())return;
    if(!force&&shipyard_refresh_elapsed_<.2)return;
    shipyard_workspace_.set_view(shipyard_controller_.build(
        session_->frame(),session_->cache().generation));
    shipyard_refresh_elapsed_=0.;
  }

  void execute_shipyard(const ShipyardWorkspaceCommand &command){
    const auto &view=shipyard_workspace_.view();
    if(!view){
      shipyard_workspace_.set_notice("Shipyard details are still loading.",false);
      return;
    }
    NativeShipyardCommandOutcome outcome;
    if(command.kind==ShipyardWorkspaceCommandKind::PrepareCancel){
      auto &clock=session_->frame().clock();
      if(clock.speed()!=StrategicSpeed::Paused)
        clock.set_speed(StrategicSpeed::Paused);
      refresh_shipyard(true);
      if(!shipyard_workspace_.arm_cancel_confirmation(command.id))
        shipyard_workspace_.set_notice(
            "The cancellation quote changed; review the refreshed order.",false);
      return;
    }
    if(command.kind==ShipyardWorkspaceCommandKind::Start)
      outcome=shipyard_controller_.start(session_->frame(),
          session_->cache().generation,view->shipyard_revision,command.id);
    else if(command.kind==ShipyardWorkspaceCommandKind::Cancel)
      outcome=shipyard_controller_.cancel(session_->frame(),
          session_->cache().generation,view->shipyard_revision,command.id);
    else return;
    shipyard_workspace_.set_notice(outcome.message,outcome.accepted);
    last_shipyard_command_accepted_=outcome.accepted;
    refresh_shipyard(true);
  }

  [[nodiscard]] std::vector<FleetScreenMarker> fleet_markers(
      int width,int height)const{
    std::vector<FleetScreenMarker> result;
    if(!fleet_workspace_.view())return result;
    result.reserve(fleet_workspace_.view()->own_fleets.size());
    std::size_t offset_index{};
    for(const auto &fleet:fleet_workspace_.view()->own_fleets){
      auto point=camera_.project({fleet.position.x,fleet.position.y},width,height);
      while(offset_index<fleet_marker_offsets_.size()&&
            fleet_marker_offsets_[offset_index].fleet_id<fleet.id)
        ++offset_index;
      if(offset_index<fleet_marker_offsets_.size()&&
         fleet_marker_offsets_[offset_index].fleet_id==fleet.id){
        point.x+=fleet_marker_offsets_[offset_index].pixels.x;
        point.y+=fleet_marker_offsets_[offset_index].pixels.y;
      }
      result.push_back({fleet.id,point});
    }
    return result;
  }

  [[nodiscard]] std::optional<int> system_hit(Point pointer,int width,
                                               int height)const{
    float best=10.f;
    std::optional<int> result;
    for(const auto &system:session_->frame().runtime().world().campaign().systems){
      const auto point=camera_.project({system.position.x,system.position.y},width,height);
      const auto distance=std::hypot(point.x-pointer.x,point.y-pointer.y);
      if(distance<best){best=distance;result=system.id;}
    }
    return result;
  }

  [[nodiscard]] std::string system_display_name(int system_id)const{
    if(!known_.contains(system_id))return "Unknown system";
    const auto found=session_->cache().systems_by_id.find(system_id);
    return found==session_->cache().systems_by_id.end()?"Unknown system":found->second->name;
  }

  [[nodiscard]] std::vector<ObservedSystemName> observed_system_names()const{
    const auto &systems=session_->frame().runtime().world().campaign().systems;
    std::vector<ObservedSystemName> result;
    result.reserve(systems.size());
    for(const auto &system:systems)
      result.push_back({system.id,system.name,known_.contains(system.id)});
    return result;
  }

  void refresh_fleets(bool force){
    if(!force&&fleet_refresh_elapsed_<.1)return;
    auto view=fleet_controller_.build(session_->frame(),
                                      session_->cache().generation);
    if(pending_fleet_preview_){
      const auto selected=view.selected_fleet_id
                              ?std::ranges::find(view.own_fleets,
                                                 *view.selected_fleet_id,
                                                 &NativeOwnFleet::id)
                              :view.own_fleets.end();
      if(selected==view.own_fleets.end()||
         selected->id!=pending_fleet_preview_->fleet_id||
         selected->mission_order_revision!=
             pending_fleet_preview_->expected_mission_order_revision)
        pending_fleet_preview_.reset();
    }
    fleet_marker_offsets_=deterministic_fleet_marker_offsets(view.own_fleets);
    fleet_workspace_.set_view(std::move(view));
    fleet_refresh_elapsed_=0.;
  }

  void handle_fleet_command(const FleetWorkspaceCommand &command){
    if(command.kind==FleetWorkspaceCommandKind::None)return;
    NativeFleetSelectionOutcome selection;
    if(command.kind==FleetWorkspaceCommandKind::Select)
      selection=fleet_controller_.select(session_->frame(),
                                         session_->cache().generation,
                                         command.fleet_id);
    else if(command.kind==FleetWorkspaceCommandKind::SelectHits)
      selection=fleet_controller_.select_next_hit(
          session_->frame(),session_->cache().generation,
          command.hit_fleet_ids);
    if(command.kind==FleetWorkspaceCommandKind::Select||
       command.kind==FleetWorkspaceCommandKind::SelectHits){
      pending_fleet_preview_.reset();
      fleet_workspace_.set_notice(selection.message,selection.accepted);
      refresh_fleets(true);
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Preview){
      auto preview=fleet_controller_.preview_selected_route(
          session_->frame(),session_->cache().generation,
          command.target_system_id);
      pending_fleet_preview_=preview;
      preview.message=observer_safe_fleet_message(preview.message,
                                                  observed_system_names());
      fleet_workspace_.set_preview(std::move(preview),
                                   system_display_name(command.target_system_id));
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Confirm){
      if(!pending_fleet_preview_)return;
      const auto outcome=fleet_controller_.issue_selected_route(
          session_->frame(),*pending_fleet_preview_);
      pending_fleet_preview_.reset();
      fleet_workspace_.clear_preview();
      fleet_workspace_.set_notice(observer_safe_fleet_message(
                                      outcome.message,observed_system_names()),
                                  outcome.accepted);
      last_fleet_command_accepted_=outcome.accepted;
      refresh_fleets(true);
    }
  }

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
  NativeResearchController research_controller_;
  NativeResearchWorkspace research_workspace_;
  NativeFleetController fleet_controller_;
  NativeFleetWorkspace fleet_workspace_;
  NativeShipyardController shipyard_controller_;
  NativeShipyardWorkspace shipyard_workspace_;
  std::vector<FleetMarkerOffset> fleet_marker_offsets_;
  std::optional<NativeFleetRoutePreview> pending_fleet_preview_;
  std::optional<ResearchStamp> projected_research_stamp_;
  double research_refresh_elapsed_{};
  double fleet_refresh_elapsed_{};
  double shipyard_refresh_elapsed_{};
  bool last_research_command_accepted_{};
  std::optional<std::string> smoke_research_node_;
  bool last_fleet_command_accepted_{};
  bool last_shipyard_command_accepted_{};
  bool smoke_shipyard_order_started_{};
  bool smoke_shipyard_start_was_running_{};
  bool smoke_shipyard_was_running_{};
  std::optional<std::string> smoke_shipyard_order_id_;
  std::optional<int> smoke_fleet_id_;
  std::optional<int> smoke_fleet_destination_;
  bool menu_{};bool smoke_save_pending_{};Point pointer_{};PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
};
}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
  try{
    const auto options=parse_options(argc,argv);
    const auto startup_begin=std::chrono::steady_clock::now();
    auto session=make_session(options);
    Window window("Stellar Continuum - Native Galaxy",options.window_width,
                  options.window_height,!options.windowed,
                  std::filesystem::absolute(options.asset_root)/
                      "assets/visual/fonts/Rajdhani-SemiBold.ttf");
    NativeCampaign campaign(std::move(session),window.drawable_width(),
                            window.drawable_height());
    if(options.smoke_screenshot){
      if(options.research_smoke)
        campaign.prepare_research_smoke(window.drawable_width(),
                                        window.drawable_height(),!options.load);
      else if(options.fleet_smoke)
        campaign.prepare_fleet_smoke(window.drawable_width(),
                                     window.drawable_height());
      else if(options.shipyard_smoke)
        campaign.prepare_shipyard_smoke(window.drawable_width(),
                                        window.drawable_height());
      else
        campaign.prepare_smoke_ui();
    }
    const auto startup_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-startup_begin).count();
    auto prior=std::chrono::steady_clock::now();int frames=0;std::vector<double> frame_ms;bool discard_elapsed{};
    while(true){
      const auto now=std::chrono::steady_clock::now();
      const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();
      prior=now;
      const auto input=window.poll();
      if(!input.renderable()){
        discard_elapsed=true;
        if(!campaign.update(input,input.drawable_width,input.drawable_height,0.,false))break;
        window.set_text_input(campaign.wants_text_input());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }
      const auto elapsed=discard_elapsed?0.:measured_elapsed;
      if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);
      discard_elapsed=false;
      if(!campaign.update(input,input.drawable_width,input.drawable_height,
                          elapsed))break;
      window.set_text_input(campaign.wants_text_input());
      if(options.smoke_screenshot){
        ++frames;
        if((options.research_smoke||options.fleet_smoke||options.shipyard_smoke)&&frames==60)
          campaign.request_smoke_save();
      }
      const bool capture=options.smoke_screenshot&&frames>=120;
      window.draw(campaign.scene(input.drawable_width,input.drawable_height),
                  capture?options.smoke_screenshot:std::nullopt);
      if(capture){
        if(!campaign.smoke_save_succeeded())
          throw std::runtime_error("Native session smoke did not complete its manual save.");
        std::ranges::sort(frame_ms);
        const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);
        const auto p95=frame_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(frame_ms.size())*.95))-1];
        std::cout<<std::fixed<<std::setprecision(3)
                 <<"native-map smoke ok: gpu_driver="<<window.gpu_driver()
                 <<" presentation="<<window.presentation_mode()
                 <<" systems="<<campaign.system_count()<<" frames="<<frames
                 <<" startup_ms="<<startup_ms
                 <<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())
                 <<" frame_p95_ms="<<p95<<" save=ok screenshot="
                 <<utf8_path(*options.smoke_screenshot);
        if(options.research_smoke)
          std::cout<<" research="<<campaign.research_smoke_status();
        if(options.fleet_smoke)
          std::cout<<" fleet="<<campaign.fleet_smoke_status();
        if(options.shipyard_smoke)
          std::cout<<" shipyard="<<campaign.shipyard_smoke_status();
        std::cout<<'\n';
        break;
      }
    }
    return 0;
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
