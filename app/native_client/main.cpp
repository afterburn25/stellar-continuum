#include "map_camera.hpp"
#include "map_interaction.hpp"
#include "native_galaxy_star_markers.hpp"
#include "native_campaign_session.hpp"
#include "native_colony_controller.hpp"
#include "native_colony_workspace.hpp"
#include "native_settlement_mission_controller.hpp"
#include "native_settlement_workspace.hpp"
#include "native_surface_construction_controller.hpp"
#include "native_surface_workspace.hpp"
#include "native_construction_controller.hpp"
#include "native_construction_workspace.hpp"
#include "native_diplomacy_controller.hpp"
#include "native_diplomacy_workspace.hpp"
#include "native_fleet_controller.hpp"
#include "native_fleet_presentation.hpp"
#include "native_fleet_workspace.hpp"
#include "native_research_controller.hpp"
#include "native_research_workspace.hpp"
#include "native_shipyard_controller.hpp"
#include "native_shipyard_workspace.hpp"
#include "native_system_view.hpp"
#include "native_system_travel.hpp"
#include "native_system_workspace.hpp"
#include "native_planet_disc_assets.hpp"
#include "native_fleet_route_effects.hpp"
#include "native_ship_art_assets.hpp"
#include "native_ui_layout.hpp"
#include "native_startup_entry.hpp"
#include "native_galaxy_backdrop.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/build_version.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <array>
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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using namespace stellar::core;
using namespace stellar::native_map;

using namespace stellar::native_construction;
using namespace stellar::native_construction_ui;
using namespace stellar::native_diplomacy;
using namespace stellar::native_diplomacy_ui;
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;
using namespace stellar::native_research;
using namespace stellar::native_research_ui;
using namespace stellar::native_shipyard;
using namespace stellar::native_shipyard_ui;
using namespace stellar::native_system;
using namespace stellar::native_system_travel;
using namespace stellar::native_system_ui;
using namespace stellar::native_startup_ui;
using namespace stellar::native_galaxy_ui;
using namespace stellar::native_ship_ui;

struct SmokePhaseMaximum {
  double milliseconds{};
  int frame{};
  void observe(double value, int sample_frame) noexcept {
    if (value > milliseconds) {
      milliseconds = value;
      frame = sample_frame;
    }
  }
};

// Retained only by a bounded graphical smoke run. The ordinary client has no
// frame timing history or classification work beyond the smoke option check.
struct SmokeTimingSummary {
  enum class Bucket : std::size_t { cold, save_service, capture_transition, steady, count };
  std::array<std::array<SmokePhaseMaximum, 3>,
             static_cast<std::size_t>(Bucket::count)> maxima{};
  std::array<SmokePhaseMaximum, 3> overall{};

  void observe(int frame, bool capture_or_transition, double update,
               double scene, double render_present) noexcept {
    const auto bucket = frame <= 10 ? Bucket::cold : frame == 61 ? Bucket::save_service :
        capture_or_transition ? Bucket::capture_transition : Bucket::steady;
    const std::array<double, 3> values{update, scene, render_present};
    for (std::size_t index = 0; index < values.size(); ++index) {
      overall[index].observe(values[index], frame);
      maxima[static_cast<std::size_t>(bucket)][index].observe(values[index], frame);
    }
  }

  void write(std::ostream &out) const {
    const auto write_max = [&](const SmokePhaseMaximum &value) {
      out << "{\"max_ms\":" << value.milliseconds << ",\"frame\":" << value.frame << '}';
    };
    const auto write_bucket = [&](Bucket bucket) {
      const auto &values = maxima[static_cast<std::size_t>(bucket)];
      out << "{\"update\":"; write_max(values[0]);
      out << ",\"scene\":"; write_max(values[1]);
      out << ",\"render_present\":"; write_max(values[2]); out << '}';
    };
    out << " update_max_ms=" << overall[0].milliseconds << " update_max_frame=" << overall[0].frame
        << " scene_max_ms=" << overall[1].milliseconds << " scene_max_frame=" << overall[1].frame
        << " render_present_max_ms=" << overall[2].milliseconds
        << " render_present_max_frame=" << overall[2].frame
        << " smoke_timing={\"cold\":"; write_bucket(Bucket::cold);
    out << ",\"save_service\":"; write_bucket(Bucket::save_service);
    out << ",\"capture_transition\":"; write_bucket(Bucket::capture_transition);
    out << ",\"steady\":"; write_bucket(Bucket::steady); out << '}';
  }
};

template<class Character>
[[nodiscard]] int parse_profile_frames(std::basic_string_view<Character> value) {
  int frames{};
  if(value.empty())throw std::invalid_argument("--profile-frames requires 120 to 3600 frames.");
  for(const auto digit:value){
    if(digit<'0'||digit>'9')throw std::invalid_argument("--profile-frames requires whole decimal frames.");
    frames=frames*10+static_cast<int>(digit-'0');
    if(frames>3600)throw std::invalid_argument("--profile-frames must not exceed 3600.");
  }
  if(frames<120)throw std::invalid_argument("--profile-frames requires at least 120 steady frames.");
  return frames;
}

// Diagnostic-only, bounded samples taken after warm-up and before screenshots.
// The display interval includes waiting; submission/present are CPU wall times.
struct SmokeSteadyProfile {
  std::array<std::vector<double>,7> samples;
  std::size_t requested{};
  explicit SmokeSteadyProfile(int frames):requested(static_cast<std::size_t>(frames)){
    for(auto &phase:samples)phase.reserve(requested);
  }
  void observe(double interval,double update,double scene,const FrameTiming &timing){
    if(samples[0].size()>=requested)throw std::logic_error("Steady profile exceeded its sample budget.");
    const std::array values{interval,update,scene,timing.submission_ms,timing.readback_ms,timing.throttle_ms,timing.present_ms};
    for(std::size_t i=0;i<values.size();++i){
      if(!std::isfinite(values[i])||values[i]<0)throw std::runtime_error("Invalid steady-frame timing.");
      samples[i].push_back(values[i]);
    }
  }
  void write(std::ostream &out){
    if(samples[0].size()!=requested)throw std::runtime_error("Steady profiling was interrupted; rerun without minimizing the window.");
    constexpr std::array names{"interval","update","scene","submission","readback","throttle","present"};
    out<<" steady_profile={\"samples\":"<<requested;
    for(std::size_t i=0;i<samples.size();++i){
      auto &values=samples[i];std::ranges::sort(values);
      const auto percentile=[&](double p){return values[static_cast<std::size_t>(std::ceil(static_cast<double>(values.size())*p))-1];};
      out<<",\""<<names[i]<<"\":{\"mean_ms\":"<<std::accumulate(values.begin(),values.end(),0.)/static_cast<double>(values.size())
         <<",\"p50_ms\":"<<percentile(.5)<<",\"p95_ms\":"<<percentile(.95)
         <<",\"p99_ms\":"<<percentile(.99)<<",\"max_ms\":"<<values.back()<<'}';
    }
    out<<'}';
  }
};

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
  bool construction_smoke{};
  bool system_smoke{};
  bool system_travel_smoke{};
  bool system_travel_reload_smoke{};
  bool colony_smoke{};
  bool colony_reload_smoke{};
  bool settlement_smoke{};
  bool settlement_reload_smoke{};
  bool surface_smoke{};
  bool surface_reload_smoke{};
  bool new_game_smoke{};
  bool galaxy_art_smoke{};
  bool ship_art_smoke{};
  bool diplomacy_smoke{},diplomacy_reload_smoke{};
  bool save_path_overridden{};
  std::optional<int> profile_frames;
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
    else if(arg==L"--profile-frames"&&i+1<argc) result.profile_frames=parse_profile_frames(std::wstring_view(argv[++i]));
    else if(arg==L"--smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.windowed=true;}
    else if(arg==L"--new-game-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.windowed=true;}
    else if(arg==L"--research-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.research_smoke=true;result.windowed=true;}
    else if(arg==L"--fleet-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.fleet_smoke=true;result.windowed=true;}
    else if(arg==L"--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.shipyard_smoke=true;result.windowed=true;}
    else if(arg==L"--construction-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.construction_smoke=true;result.windowed=true;}
    else if(arg==L"--system-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_smoke=true;result.windowed=true;}
    else if(arg==L"--system-travel-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_travel_smoke=true;result.windowed=true;}
    else if(arg==L"--system-travel-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_travel_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--colony-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_smoke=true;result.windowed=true;}
    else if(arg==L"--colony-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--settlement-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_smoke=true;result.windowed=true;}
    else if(arg==L"--settlement-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--surface-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.surface_smoke=true;result.windowed=true;}
    else if(arg==L"--surface-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.surface_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg==L"--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.ship_art_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_reload_smoke=true;result.windowed=true;}
#else
    const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--windowed") result.windowed=true;
    else if(arg=="--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg=="--load") result.load=true;
    else if(arg=="--width"&&i+1<argc) result.window_width=std::stoi(argv[++i]);
    else if(arg=="--height"&&i+1<argc) result.window_height=std::stoi(argv[++i]);
    else if(arg=="--profile-frames"&&i+1<argc) result.profile_frames=parse_profile_frames(std::string_view(argv[++i]));
    else if(arg=="--smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.windowed=true;}
    else if(arg=="--new-game-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.windowed=true;}
    else if(arg=="--research-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.research_smoke=true;result.windowed=true;}
    else if(arg=="--fleet-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.fleet_smoke=true;result.windowed=true;}
    else if(arg=="--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.shipyard_smoke=true;result.windowed=true;}
    else if(arg=="--construction-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.construction_smoke=true;result.windowed=true;}
    else if(arg=="--system-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_smoke=true;result.windowed=true;}
    else if(arg=="--system-travel-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_travel_smoke=true;result.windowed=true;}
    else if(arg=="--system-travel-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_travel_reload_smoke=true;result.windowed=true;}
    else if(arg=="--colony-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_smoke=true;result.windowed=true;}
    else if(arg=="--colony-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_reload_smoke=true;result.windowed=true;}
    else if(arg=="--settlement-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_smoke=true;result.windowed=true;}
    else if(arg=="--settlement-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_reload_smoke=true;result.windowed=true;}
    else if(arg=="--surface-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.surface_smoke=true;result.windowed=true;}
    else if(arg=="--surface-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.surface_reload_smoke=true;result.windowed=true;}
    else if(arg=="--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg=="--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.ship_art_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_reload_smoke=true;result.windowed=true;}
#endif
    else throw std::invalid_argument("Unknown or incomplete native client option.");
  }
  if(result.smoke_screenshot&&!result.save_path_overridden)throw std::invalid_argument("--smoke requires an isolated --save-path.");
  if(result.profile_frames&&!result.system_smoke&&!result.galaxy_art_smoke)throw std::invalid_argument("--profile-frames requires --system-smoke or --galaxy-art-smoke.");
  if(static_cast<int>(result.research_smoke)+static_cast<int>(result.fleet_smoke)+static_cast<int>(result.shipyard_smoke)+static_cast<int>(result.construction_smoke)+static_cast<int>(result.system_smoke)+static_cast<int>(result.system_travel_smoke)+static_cast<int>(result.system_travel_reload_smoke)+static_cast<int>(result.colony_smoke)+static_cast<int>(result.colony_reload_smoke)+static_cast<int>(result.settlement_smoke)+static_cast<int>(result.settlement_reload_smoke)+static_cast<int>(result.surface_smoke)+static_cast<int>(result.surface_reload_smoke)+static_cast<int>(result.new_game_smoke)+static_cast<int>(result.galaxy_art_smoke)+static_cast<int>(result.ship_art_smoke)+static_cast<int>(result.diplomacy_smoke)+static_cast<int>(result.diplomacy_reload_smoke)>1)throw std::invalid_argument("Choose one native graphical smoke mode.");
  if(result.new_game_smoke&&result.load)throw std::invalid_argument("--new-game-smoke cannot be combined with --load.");
  if(result.fleet_smoke&&!result.load)throw std::invalid_argument("--fleet-smoke requires --load with a player campaign fixture.");
  if(result.ship_art_smoke&&!result.load)throw std::invalid_argument("--ship-art-smoke requires --load with a player campaign fixture.");
  if((result.diplomacy_smoke||result.diplomacy_reload_smoke)&&!result.load)throw std::invalid_argument("Diplomacy smoke requires --load with an isolated diplomatic campaign fixture.");
  if(result.system_travel_smoke&&!result.load)throw std::invalid_argument("--system-travel-smoke requires --load with a routed player fleet fixture.");
  if(result.system_travel_reload_smoke&&!result.load)throw std::invalid_argument("--system-travel-reload-smoke requires --load with the paused system travel save.");
  if(result.colony_reload_smoke&&!result.load)throw std::invalid_argument("--colony-reload-smoke requires --load with the paused colony save.");
  if((result.settlement_smoke||result.settlement_reload_smoke)&&!result.load)throw std::invalid_argument("Settlement smoke requires --load with a test-authored funded populated settlement vessel.");
  if((result.surface_smoke||result.surface_reload_smoke)&&!result.load)throw std::invalid_argument("Surface smoke requires --load with the isolated native campaign save.");
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

[[nodiscard]] std::string json_string(std::string_view value){
  std::ostringstream out;out<<'"';constexpr char hex[]="0123456789abcdef";
  for(const unsigned char c:value){switch(c){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
    case '\b':out<<"\\b";break;case '\f':out<<"\\f";break;case '\n':out<<"\\n";break;
    case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
    default:if(c<0x20)out<<"\\u00"<<hex[c>>4]<<hex[c&15];else out<<static_cast<char>(c);}}
  out<<'"';return out.str();
}
[[nodiscard]] std::filesystem::path sidecar_path(const std::filesystem::path &path,
                                                  const wchar_t *suffix){
  return path.parent_path()/(path.stem().wstring()+suffix+path.extension().wstring());
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
[[nodiscard]] GalaxyStarVisualClass galaxy_star_visual(StellarClass value){
  switch(value){
    case StellarClass::MRedDwarf:return GalaxyStarVisualClass::m_red_dwarf;
    case StellarClass::KOrangeDwarf:return GalaxyStarVisualClass::k_orange_dwarf;
    case StellarClass::GYellowDwarf:return GalaxyStarVisualClass::g_yellow_dwarf;
    case StellarClass::FYellowWhiteDwarf:return GalaxyStarVisualClass::f_yellow_white_dwarf;
    case StellarClass::AWhiteStar:return GalaxyStarVisualClass::a_white_star;
    case StellarClass::HotBlueStar:return GalaxyStarVisualClass::hot_blue_star;
    case StellarClass::Giant:return GalaxyStarVisualClass::giant;
    case StellarClass::WhiteDwarf:return GalaxyStarVisualClass::white_dwarf;
    case StellarClass::NeutronStar:return GalaxyStarVisualClass::neutron_star;
    case StellarClass::BlackHole:return GalaxyStarVisualClass::black_hole;
    case StellarClass::Protostar:return GalaxyStarVisualClass::protostar;
    case StellarClass::Pulsar:return GalaxyStarVisualClass::pulsar;
  }
  return GalaxyStarVisualClass::unknown;
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

struct GalaxyArtSceneEvidence {
  GalaxyBackdropRenderStats backdrop;
  std::size_t catalog_markers{};
  std::size_t known_markers{};
  std::size_t unknown_markers{};
  std::size_t revealed_unknown_labels{};
};

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
  NativeCampaign(std::unique_ptr<NativeCampaignSession> session,int width,int height,
                 const std::filesystem::path &asset_root,SystemTextMeasurer text_measurer)
      : session_(std::move(session)),
        galaxy_assets_(std::filesystem::absolute(asset_root)),
        galaxy_backdrop_(galaxy_assets_),
        planet_discs_(std::filesystem::absolute(asset_root)/"assets/visual/sol"),
        ship_art_(std::filesystem::absolute(asset_root)),
        asset_root_(std::filesystem::absolute(asset_root)),
        system_workspace_([this](const SystemBodyAppearance &appearance){return planet_discs_.request_image(appearance);},std::move(text_measurer)) {
    galaxy_assets_.use_background_preparation(image_preparation_);
    planet_discs_.use_background_preparation(image_preparation_);
    system_workspace_.use_background_preparation(image_preparation_);
    refresh_knowledge();
    fit_camera(width,height);
    bind_galaxy_backdrop(width,height);
    refresh_fleets(true);
  }

  void prepare_smoke_ui(){if(!menu_)toggle_menu();smoke_save_pending_=true;}
  void prepare_galaxy_art_smoke(int width,int height,bool reload){
    smoke_galaxy_mode_=true;smoke_galaxy_reload_=reload;
    smoke_galaxy_day_=session_->frame().clock().simulation_days();
    const auto click_pause=[&]{const auto layout=NativeUiLayout::for_viewport(width,height);const auto point=center(layout.pause);InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke pause input closed the campaign.");};
    if(session_->frame().clock().speed()==StrategicSpeed::Paused)click_pause();
    click_pause();
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)throw std::runtime_error("Galaxy artwork smoke did not pause through player input.");
    smoke_galaxy_paused_=true;fit_camera(width,height);smoke_galaxy_fitted_scale_=camera_.pixels_per_world;
  }
  [[nodiscard]] GalaxyArtSceneEvidence galaxy_scene_evidence(int width,int height){
    GalaxyArtSceneEvidence evidence;
    galaxy_backdrop_.clear_render_stats();
    const auto draw=scene(width,height);
    evidence.backdrop=galaxy_backdrop_.last_render_stats();
    const auto &world=session_->frame().runtime().world().campaign();
    if(!system_workspace_.visible())
      for(const auto &system:world.systems){
        const auto p=camera_.project({system.position.x,system.position.y},width,height);
        if(p.x<-14||p.y<-14||p.x>width+14||p.y>height+14)continue;
        const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;
        ++evidence.catalog_markers;
        if(known)++evidence.known_markers;else ++evidence.unknown_markers;
        if(selected&&!known)++evidence.revealed_unknown_labels;
      }
    return evidence;
  }
  void capture_galaxy_overview(int width,int height){smoke_galaxy_overview_=galaxy_scene_evidence(width,height);}
  void prepare_galaxy_regional(int width,int height){
    const auto &world=session_->frame().runtime().world().campaign();
    const auto sol=std::ranges::find_if(world.systems,[](const auto &system){return system.name=="Sol";});
    if(sol==world.systems.end())throw std::runtime_error("Galaxy artwork smoke could not locate Sol in the catalog.");
    const auto sol_anchor=camera_.project({sol->position.x,sol->position.y},width,height);
    const auto before=camera_.unproject(sol_anchor,width,height);
    for(int wheel=0;wheel<12;++wheel){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=sol_anchor;input.events={{InputEventType::Wheel,sol_anchor,{},1.f}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke wheel input closed the campaign.");}
    const auto after=camera_.unproject(sol_anchor,width,height);
    if(std::hypot(after.x-before.x,after.y-before.y)>1e-9)throw std::runtime_error("Galaxy artwork smoke wheel zoom moved the world anchor beneath the pointer.");
    if(camera_.pixels_per_world<smoke_galaxy_fitted_scale_*5.)throw std::runtime_error("Galaxy artwork smoke did not reach regional magnification.");
    smoke_galaxy_regional_scale_=camera_.pixels_per_world;smoke_galaxy_wheel_input_=true;
  }
  void capture_galaxy_regional(int width,int height){smoke_galaxy_regional_=galaxy_scene_evidence(width,height);}
  void prepare_galaxy_system(int width,int height){
    const auto &world=session_->frame().runtime().world().campaign();
    const auto sol=std::ranges::find_if(world.systems,[](const auto &system){return system.name=="Sol";});
    if(sol==world.systems.end())throw std::runtime_error("Galaxy artwork smoke could not locate Sol in the catalog.");
    const auto anchor=camera_.project({sol->position.x,sol->position.y},width,height);
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=anchor;input.events={{InputEventType::LeftPressed,anchor,{},0,{},2},{InputEventType::LeftReleased,anchor}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke system entry input closed the campaign.");
    if(!system_workspace_.visible())throw std::runtime_error("Galaxy artwork smoke double click did not open the observed system.");
    smoke_galaxy_system_entry_=true;
  }
  void capture_galaxy_system(int width,int height){smoke_galaxy_system_=galaxy_scene_evidence(width,height);}
  [[nodiscard]] std::string galaxy_art_smoke_status()const{
    const auto view=[&](const GalaxyArtSceneEvidence &evidence,bool system){
      std::ostringstream out;
      out<<"{\"deep_field\":"<<evidence.backdrop.deep_field_images
         <<",\"galaxy_layer\":"<<evidence.backdrop.galaxy_layer_images
         <<",\"regional_nebula\":"<<evidence.backdrop.regional_nebula_images
         <<",\"regional_points\":"<<evidence.backdrop.regional_points
         <<",\"undisclosed_core_fog\":"<<evidence.backdrop.undisclosed_core_fog_images
         <<",\"background_images\":"<<(system?evidence.backdrop.deep_field_images+evidence.backdrop.galaxy_layer_images+evidence.backdrop.regional_nebula_images+evidence.backdrop.undisclosed_core_fog_images:0)
         <<",\"catalog_markers\":"<<evidence.catalog_markers
         <<",\"known_markers\":"<<evidence.known_markers
         <<",\"unknown_markers\":"<<evidence.unknown_markers
         <<",\"revealed_unknown_labels\":"<<evidence.revealed_unknown_labels<<"}";
      return out.str();
    };
    std::ostringstream out;
    out<<"{\"mode\":"<<json_string(smoke_galaxy_reload_?"paused_reload":"fresh")
       <<",\"wheel_input\":"<<(smoke_galaxy_wheel_input_?"true":"false")
       <<",\"system_entry\":"<<(smoke_galaxy_system_entry_?"true":"false")
       <<",\"paused\":"<<((smoke_galaxy_paused_&&session_->frame().clock().speed()==StrategicSpeed::Paused)?"true":"false")
       <<",\"day_unchanged\":"<<((session_->frame().clock().simulation_days()==smoke_galaxy_day_)?"true":"false")
       <<",\"fitted_scale\":"<<smoke_galaxy_fitted_scale_
       <<",\"regional_scale\":"<<smoke_galaxy_regional_scale_
       <<",\"decoded_sources\":"<<galaxy_assets_.decoded_count()
       <<",\"overview\":"<<view(smoke_galaxy_overview_,false)
       <<",\"regional\":"<<view(smoke_galaxy_regional_,false)
       <<",\"system\":"<<view(smoke_galaxy_system_,true)<<"}";
    return out.str();
  }
  void prepare_system_smoke(int width,int height){
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    smoke_system_day_=session_->frame().clock().simulation_days();
    const auto found=session_->cache().systems_by_id.find(sol_system_id);
    if(found==session_->cache().systems_by_id.end())throw std::runtime_error("System smoke lacks Sol.");
    const auto point=camera_.project({found->second->position.x,found->second->position.y},width,height);
    InputSnapshot arm;arm.drawable_width=width;arm.drawable_height=height;arm.pointer={static_cast<float>(width)*.5f,static_cast<float>(height)-4.f};arm.events={{InputEventType::LeftPressed,arm.pointer}};(void)update(arm,width,height,0.,false);
    InputSnapshot enter;enter.drawable_width=width;enter.drawable_height=height;enter.pointer=point;
    enter.events={{InputEventType::LeftPressed,point,{},0,{},2},{InputEventType::LeftReleased,point}};
    if(!update(enter,width,height,0.,false)||!system_workspace_.visible())throw std::runtime_error("System smoke double-click did not enter Sol.");
    smoke_system_entered_=true;
    auto safe=system_controller_.build(session_->frame(),session_->cache().generation,sol_system_id);
    if(!safe.snapshot)throw std::runtime_error("System smoke lost its observer-safe Sol view.");
    const auto spatial=project_system(*safe.snapshot);
    const auto system_layout=SystemWorkspaceLayout::for_viewport(width,height);
    const auto click=[&](Point location){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=location;input.events={{InputEventType::LeftPressed,location},{InputEventType::LeftReleased,location}};(void)update(input,width,height,0.,false);};
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click({main_layout.pause.x+main_layout.pause.width*.5f,main_layout.pause.y+main_layout.pause.height*.5f});
    const auto resumed_speed=session_->frame().clock().speed();
    smoke_system_pause_retained_=system_workspace_.visible()&&resumed_speed!=StrategicSpeed::Paused;
    click({main_layout.speed.x+main_layout.speed.width*.5f,main_layout.speed.y+main_layout.speed.height*.5f});
    smoke_system_speed_retained_=system_workspace_.visible()&&session_->frame().clock().speed()!=resumed_speed;
    click({main_layout.pause.x+main_layout.pause.width*.5f,main_layout.pause.y+main_layout.pause.height*.5f});
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused||!smoke_system_pause_retained_||!smoke_system_speed_retained_)throw std::runtime_error("System smoke global clock controls closed the workspace or failed to route.");
    (void)system_workspace_.handle({InputEventType::Wheel,{420,320},{},2},width,height);
    click({system_layout.reset.x+system_layout.reset.width*.5f,system_layout.reset.y+system_layout.reset.height*.5f});
    smoke_system_reset_=system_workspace_.visible()&&system_workspace_.viewport()&&system_workspace_.viewport()->scale>0.f;
    const auto map_center_before_back=camera_.center;
    const Point back_point{system_layout.back.x+system_layout.back.width*.5f,system_layout.back.y+system_layout.back.height*.5f};
    InputSnapshot back_input;back_input.drawable_width=width;back_input.drawable_height=height;back_input.pointer={back_point.x+31,back_point.y+17};back_input.events={{InputEventType::LeftPressed,back_point},{InputEventType::PointerMove,back_input.pointer,{31,17}},{InputEventType::LeftReleased,back_input.pointer}};(void)update(back_input,width,height,0.,false);
    smoke_system_back_=!system_workspace_.visible();
    smoke_system_gesture_cleared_=camera_.center.x==map_center_before_back.x&&camera_.center.y==map_center_before_back.y;
    if(!smoke_system_reset_||!smoke_system_back_||!smoke_system_gesture_cleared_)throw std::runtime_error("System smoke Back/Reset or galaxy gesture ownership failed.");
    if(!update(enter,width,height,0.,false)||!system_workspace_.visible())throw std::runtime_error("System smoke could not re-enter Sol after Back.");
    const auto earth=std::ranges::find(spatial.bodies,earth_body_id,&SystemSpatialBodyMarker::body_id);
    if(earth==spatial.bodies.end()||!system_workspace_.viewport())throw std::runtime_error("System smoke lacks projected Earth.");
    const auto earth_point=system_workspace_.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
    InputSnapshot select_input;select_input.drawable_width=width;select_input.drawable_height=height;select_input.pointer={earth_point.x,earth_point.y};select_input.events={{InputEventType::LeftPressed,select_input.pointer},{InputEventType::LeftReleased,select_input.pointer}};(void)update(select_input,width,height,0.,false);
    if(system_workspace_.selected_body_id()!=earth_body_id)throw std::runtime_error("System smoke could not select Earth.");
    smoke_system_hit_=true;const auto before=*system_workspace_.viewport();InputSnapshot zoom;zoom.drawable_width=width;zoom.drawable_height=height;zoom.pointer={420,320};zoom.events={{InputEventType::Wheel,zoom.pointer,{},1}};(void)update(zoom,width,height,0.,false);const auto zoomed=*system_workspace_.viewport();smoke_system_zoomed_=zoomed.scale>before.scale;InputSnapshot move;move.drawable_width=width;move.drawable_height=height;move.pointer={392,318};move.events={{InputEventType::LeftPressed,{360,300}},{InputEventType::PointerMove,{392,318},{32,18}},{InputEventType::LeftReleased,{392,318}}};(void)update(move,width,height,0.,false);const auto after=*system_workspace_.viewport();smoke_system_panned_=after.center_x!=zoomed.center_x||after.center_y!=zoomed.center_y;
    if(!smoke_system_zoomed_||!smoke_system_panned_)throw std::runtime_error("System smoke did not preserve zoom and pan input.");
  }
  void prepare_colony_smoke(int width,int height,bool reload){
    smoke_colony_reload_=reload;
    auto &frame=session_->frame();
    auto &world=frame.runtime().world().campaign();
    frame.clock().set_speed(StrategicSpeed::Paused);
    smoke_colony_day_=frame.clock().simulation_days();
    const auto colony=std::ranges::find_if(world.colonies,[&](const Colony&candidate){return candidate.civilization_id==world.player_civilization_id&&candidate.planetary_body_id.has_value();});
    if(colony==world.colonies.end())throw std::runtime_error("Colony smoke requires a player-owned planetary settlement.");
    const auto system=session_->cache().systems_by_id.find(colony->system_id);
    if(system==session_->cache().systems_by_id.end())throw std::runtime_error("Colony smoke settlement system is absent from the campaign cache.");
    const auto click=[&](Point point,std::uint8_t count=1){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point,{},0,{},count},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Colony smoke input closed the campaign.");};
    const auto system_point=camera_.project({system->second->position.x,system->second->position.y},width,height);
    click(system_point,2);
    if(!system_workspace_.visible()||!system_workspace_.snapshot()||!system_workspace_.viewport())throw std::runtime_error("Colony smoke could not open its observer-safe system.");
    const auto spatial=project_system(*system_workspace_.snapshot());
    const auto body=std::ranges::find(spatial.bodies,*colony->planetary_body_id,&SystemSpatialBodyMarker::body_id);
    if(body==spatial.bodies.end())throw std::runtime_error("Colony smoke owned body is not visible to the player observer.");
    const auto body_point=system_workspace_.viewport()->world_to_screen(body->offset_x,body->offset_y);
    click({body_point.x,body_point.y});
    smoke_colony_selected_=system_workspace_.selected_body_id()==colony->planetary_body_id;
    click(center(SystemWorkspaceLayout::for_viewport(width,height).colony_action));
    if(!colony_workspace_.visible()||!colony_workspace_.view())throw std::runtime_error("Colony smoke did not open through the owned-body action.");
    smoke_colony_opened_=true;
    const auto expected_system=system_workspace_.system_id();
    const auto expected_body=system_workspace_.selected_body_id();
    InputSnapshot back;back.drawable_width=width;back.drawable_height=height;back.events={{InputEventType::EscapePressed}};(void)update(back,width,height,0.,false);
    smoke_colony_back_=!colony_workspace_.visible()&&system_workspace_.visible()&&system_workspace_.system_id()==expected_system&&system_workspace_.selected_body_id()==expected_body;
    click(center(SystemWorkspaceLayout::for_viewport(width,height).colony_action));
    const auto ui=NativeUiLayout::for_viewport(width,height);
    click(center(ui.pause));
    smoke_colony_pause_retained_=colony_workspace_.visible()&&frame.clock().speed()!=StrategicSpeed::Paused;
    for(int index=0;index<4;++index)click(center(ui.speed));
    smoke_colony_speed_retained_=colony_workspace_.visible()&&frame.clock().speed()==StrategicSpeed::Normal;
    click(center(ui.pause));
    if(!smoke_colony_selected_||!smoke_colony_opened_||!smoke_colony_back_||!smoke_colony_pause_retained_||!smoke_colony_speed_retained_||frame.clock().speed()!=StrategicSpeed::Paused||frame.clock().simulation_days()!=smoke_colony_day_)throw std::runtime_error("Colony smoke input routing or paused state was not preserved.");
  }
  void prepare_surface_smoke(int width,int height,bool reload){
    smoke_surface_mode_=true;smoke_surface_reload_=reload;
    prepare_colony_smoke(width,height,reload);
    const auto click=[&](Point point){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Surface smoke input closed the campaign.");};
    const auto move=[&](Point point){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::PointerMove,point,{}}};if(!update(input,width,height,0.,false))throw std::runtime_error("Surface smoke pointer input closed the campaign.");};
    click(center(ColonyWorkspaceLayout::for_viewport(width,height).open_surface));
    if(!surface_workspace_.visible()||!surface_workspace_.view())throw std::runtime_error("Surface smoke did not open from the owned colony through input.");
    const auto initial=*surface_workspace_.view();
    smoke_surface_system_id_=initial.system_id;smoke_surface_body_id_=initial.body_id;smoke_surface_colony_id_=initial.colony_id;smoke_surface_site_count_before_=initial.construction_sites.size();smoke_surface_treasury_before_=initial.treasury_budget_units;smoke_surface_before_day_=session_->frame().clock().simulation_days();
    const auto layout=SurfaceWorkspaceLayout::for_viewport(width,height);
    if(reload){
      const auto site=std::ranges::max_element(initial.construction_sites,{},&NativeSurfaceSite::building_id);
      if(site==initial.construction_sites.end()||site->complete)throw std::runtime_error("Surface reload smoke found no persisted unfinished site.");
      smoke_surface_type_id_=site->type_id;smoke_surface_site_id_=site->building_id;smoke_surface_x_=site->x;smoke_surface_z_=site->z;smoke_surface_rotation_=site->rotation_degrees;smoke_surface_progress_=site->industry_progress;smoke_surface_treasury_saved_=initial.treasury_budget_units;smoke_surface_site_count_saved_=initial.construction_sites.size();smoke_surface_saved_day_=smoke_surface_before_day_;
      const auto point=surface_workspace_.viewport().world_to_screen(site->x,site->z,layout.terrain);click(point);
      smoke_surface_persisted_site_=surface_workspace_.selected_building_id()==std::optional<int>{site->building_id};
      InputSnapshot paused;paused.drawable_width=width;paused.drawable_height=height;(void)update(paused,width,height,1.,true);
      const auto stable=surface_workspace_.view()?std::ranges::find(surface_workspace_.view()->construction_sites,site->building_id,&NativeSurfaceSite::building_id):initial.construction_sites.end();
      if(!surface_workspace_.view()||stable==surface_workspace_.view()->construction_sites.end()||stable->x!=smoke_surface_x_||stable->z!=smoke_surface_z_||stable->rotation_degrees!=smoke_surface_rotation_||stable->industry_progress!=smoke_surface_progress_||session_->frame().clock().simulation_days()!=smoke_surface_before_day_)throw std::runtime_error("Surface reload smoke changed paused canonical site state.");
      return;
    }
    if(initial.available_buildings.empty())throw std::runtime_error("Surface smoke owned colony has no canonical available building.");
    const auto option=initial.available_buildings.front();smoke_surface_type_id_=option.type_id;
    click({layout.palette_rows.x+14.f*layout.scale,layout.palette_rows.y+20.f*layout.scale});smoke_surface_palette_selected_=surface_workspace_.selected_type_id()==std::optional<std::string>{option.type_id};
    std::optional<NativeSurfacePlacementQuote> available;
    for(float z=-420.f;z<=420.f&&!available;z+=35.f)for(float x=-420.f;x<=420.f;++x){auto quote=surface_controller_.preview_placement(session_->frame(),session_->cache().generation,*surface_workspace_.view(),option.type_id,x,z,0.f);if(quote.accepted){available=quote;break;}}
    if(!available)throw std::runtime_error("Surface smoke found no accepted canonical position.");
    (void)surface_controller_.cancel_quote(session_->cache().generation,available->quote_revision);
    const auto point=surface_workspace_.viewport().world_to_screen(available->x,available->z,layout.terrain);
    if(!layout.terrain.contains(point))throw std::runtime_error("Surface smoke accepted coordinate is outside the visible operational terrain.");
    move(point);smoke_surface_ghost_previewed_=surface_workspace_.placement_quote().has_value();
    click(point);
    if(!surface_workspace_.placement_quote()||!surface_workspace_.placement_quote()->accepted)throw std::runtime_error("Surface smoke click did not open an accepted exact placement quote.");
    smoke_surface_authorization_=surface_workspace_.placement_quote()->authorization_budget_units;
    click(center(layout.cancel));
    refresh_surface(true);
    smoke_surface_placement_cancelled_=!surface_workspace_.placement_quote();
    smoke_surface_cancel_no_change_=surface_workspace_.view()->construction_sites.size()==smoke_surface_site_count_before_&&surface_workspace_.view()->treasury_budget_units==smoke_surface_treasury_before_;
    click(point);
    if(!surface_workspace_.placement_quote()||!surface_workspace_.placement_quote()->accepted)throw std::runtime_error("Surface smoke could not reopen exact placement confirmation.");
    click(center(layout.confirm));
    smoke_surface_placement_confirmed_=surface_workspace_.view()->construction_sites.size()==smoke_surface_site_count_before_+1;
    smoke_surface_treasury_after_place_=surface_workspace_.view()->treasury_budget_units;
    const auto placed=std::ranges::max_element(surface_workspace_.view()->construction_sites,{},&NativeSurfaceSite::building_id);
    if(placed==surface_workspace_.view()->construction_sites.end()||placed->complete)throw std::runtime_error("Surface smoke did not create an unfinished canonical site.");
    const auto first_id=placed->building_id;
    click(surface_workspace_.viewport().world_to_screen(placed->x,placed->z,layout.terrain));
    click(center(layout.remove));
    if(!surface_workspace_.removal_quote()||!surface_workspace_.removal_quote()->accepted||!surface_workspace_.removal_quote()->cancellation)throw std::runtime_error("Surface smoke did not prepare an exact unfinished cancellation quote.");
    smoke_surface_removal_previewed_=true;smoke_surface_refund_=surface_workspace_.removal_quote()->refund_budget_units;
    click(center(layout.confirm));
    smoke_surface_removal_confirmed_=std::ranges::none_of(surface_workspace_.view()->construction_sites,[&](const auto&site){return site.building_id==first_id;});
    smoke_surface_treasury_after_refund_=surface_workspace_.view()->treasury_budget_units;
    smoke_surface_refund_exact_=std::abs(smoke_surface_treasury_after_refund_-(smoke_surface_treasury_after_place_+smoke_surface_refund_))<1e-9;
    click({layout.palette_rows.x+14.f*layout.scale,layout.palette_rows.y+20.f*layout.scale});
    move(point);click(point);
    if(!surface_workspace_.placement_quote()||!surface_workspace_.placement_quote()->accepted)throw std::runtime_error("Surface smoke final placement quote failed.");
    click(center(layout.confirm));
    const auto final_site=std::ranges::max_element(surface_workspace_.view()->construction_sites,{},&NativeSurfaceSite::building_id);
    if(final_site==surface_workspace_.view()->construction_sites.end()||final_site->complete)throw std::runtime_error("Surface smoke final unfinished site is absent.");
    smoke_surface_site_id_=final_site->building_id;smoke_surface_x_=final_site->x;smoke_surface_z_=final_site->z;smoke_surface_rotation_=final_site->rotation_degrees;
    session_->frame().clock().set_speed(StrategicSpeed::Normal);
    for(int index=0;index<64&&smoke_surface_progress_<=0.;++index){InputSnapshot tick;tick.drawable_width=width;tick.drawable_height=height;(void)update(tick,width,height,.05,true);if(surface_workspace_.view()){const auto current=std::ranges::find(surface_workspace_.view()->construction_sites,*smoke_surface_site_id_,&NativeSurfaceSite::building_id);if(current!=surface_workspace_.view()->construction_sites.end())smoke_surface_progress_=current->industry_progress;}}
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    if(!smoke_surface_palette_selected_||!smoke_surface_ghost_previewed_||!smoke_surface_placement_cancelled_||!smoke_surface_cancel_no_change_||!smoke_surface_placement_confirmed_||!smoke_surface_removal_previewed_||!smoke_surface_removal_confirmed_||!smoke_surface_refund_exact_||smoke_surface_progress_<=0.)throw std::runtime_error("Surface smoke did not prove its canonical placement/cancellation loop.");
  }
  void prepare_system_travel_smoke(int width,int height,bool paused_reload){
    auto &world=session_->frame().runtime().world().campaign();
    const auto fleet=std::ranges::find_if(world.fleets,[&](const FleetState&candidate){return candidate.is_active&&candidate.civilization_id==world.player_civilization_id&&candidate.current_system_id&&candidate.destination_system_id&&(candidate.transit_phase==FleetTransitPhase::LocalDeparture||candidate.transit_phase==FleetTransitPhase::LocalArrival)&&!candidate.hold_requested;});
    if(fleet==world.fleets.end())throw std::runtime_error("System travel smoke requires a moving local player fleet fixture.");
    smoke_system_travel_fleet_id_=fleet->id;smoke_system_travel_system_id_=*fleet->current_system_id;smoke_system_travel_destination_id_=fleet->destination_system_id;smoke_system_travel_mission_revision_=fleet->mission_order_revision;smoke_system_travel_before_x_=fleet->local_transit_position.x;smoke_system_travel_before_y_=fleet->local_transit_position.y;smoke_system_travel_before_day_=session_->frame().clock().simulation_days();
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)session_->frame().clock().set_speed(StrategicSpeed::Paused);
    const auto system=session_->cache().systems_by_id.find(*fleet->current_system_id);if(system==session_->cache().systems_by_id.end())throw std::runtime_error("System travel smoke fleet system is absent from the campaign cache.");
    const auto system_point=camera_.project({system->second->position.x,system->second->position.y},width,height);InputSnapshot enter;enter.drawable_width=width;enter.drawable_height=height;enter.pointer=system_point;enter.events={{InputEventType::LeftPressed,system_point,{},0,{},2},{InputEventType::LeftReleased,system_point}};if(!update(enter,width,height,0.,false)||!system_workspace_.visible())throw std::runtime_error("System travel smoke mouse entry was denied.");
    const auto click=[&](Point point){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("System travel smoke input closed the campaign.");};
    const auto *initial_travel=system_workspace_.travel_snapshot();if(!initial_travel)throw std::runtime_error("System travel smoke did not receive an observer-safe local travel view.");std::unordered_set<int> connected;for(const auto&lane:session_->frame().runtime().world().lanes().build())if(lane.connects(*smoke_system_travel_system_id_))connected.insert(lane.other(*smoke_system_travel_system_id_));smoke_system_travel_lane_count_=initial_travel->lanes.size();smoke_system_travel_lanes_connected_=smoke_system_travel_lane_count_==connected.size()&&std::ranges::all_of(initial_travel->lanes,[&](const auto&lane){return connected.contains(lane.destination_system_id);});if(!smoke_system_travel_lanes_connected_)throw std::runtime_error("System travel smoke destinations did not match the canonical connected set.");
    const auto known_lane=std::ranges::find_if(initial_travel->lanes,[](const auto&lane){return lane.known_label.has_value();}),unknown_lane=std::ranges::find_if(initial_travel->lanes,[](const auto&lane){return !lane.known_label.has_value();});if(known_lane==initial_travel->lanes.end()||unknown_lane==initial_travel->lanes.end())throw std::runtime_error("System travel smoke requires one authored known neighbor and one unknown neighbor.");const auto known_id=known_lane->destination_system_id,unknown_id=unknown_lane->destination_system_id;const auto geometry=system_workspace_.lane_geometry();const auto known_geometry=std::ranges::find(geometry,known_id,&NativeLocalLaneGeometry::destination_system_id),unknown_geometry=std::ranges::find(geometry,unknown_id,&NativeLocalLaneGeometry::destination_system_id);if(known_geometry==geometry.end()||unknown_geometry==geometry.end())throw std::runtime_error("System travel smoke could not place its connected lane arrows.");
    const auto unknown_level=world.knowledge.system_survey_level(world.player_civilization_id,unknown_id);click(unknown_geometry->center);smoke_system_travel_unknown_denied_=system_workspace_.system_id()==smoke_system_travel_system_id_&&system_workspace_.notice().find("Telemetry unavailable")!=std::string::npos;smoke_system_travel_knowledge_unchanged_=world.knowledge.system_survey_level(world.player_civilization_id,unknown_id)==unknown_level;click(known_geometry->center);smoke_system_travel_known_opened_=system_workspace_.system_id()==known_id;if(!smoke_system_travel_known_opened_)throw std::runtime_error("System travel smoke known lane did not open its observer-gated destination.");const auto destination_layout=SystemWorkspaceLayout::for_viewport(width,height);click(center(destination_layout.back));if(system_workspace_.visible()||!update(enter,width,height,0.,false)||system_workspace_.system_id()!=smoke_system_travel_system_id_)throw std::runtime_error("System travel smoke could not return to its local system after known-lane navigation.");
    const auto *before_snapshot=system_workspace_.travel_snapshot();if(!before_snapshot)throw std::runtime_error("System travel smoke did not receive an observer-safe local travel view.");const auto before_marker=std::ranges::find(before_snapshot->fleets,fleet->id,&NativeLocalFleetMarker::fleet_id);if(before_marker==before_snapshot->fleets.end())throw std::runtime_error("System travel smoke did not render the local player fleet.");const auto spatial=project_system(*system_workspace_.snapshot());const auto before_screen=local_fleet_anchor(*before_marker,spatial,*system_workspace_.viewport());const auto before_canonical=fleet->local_transit_position;
    InputSnapshot select;select.drawable_width=width;select.drawable_height=height;select.pointer=before_screen;select.events={{InputEventType::LeftPressed,before_screen},{InputEventType::LeftReleased,before_screen}};(void)update(select,width,height,0.,false);smoke_system_travel_selected_=fleet_controller_.selection()==std::optional<int>{fleet->id};
    smoke_system_travel_reload_=paused_reload;
    if(paused_reload){smoke_system_travel_pause_retained_=system_workspace_.visible()&&session_->frame().clock().speed()==StrategicSpeed::Paused;InputSnapshot paused_tick;paused_tick.drawable_width=width;paused_tick.drawable_height=height;(void)update(paused_tick,width,height,1.,true);const auto stable_fleet=std::ranges::find(world.fleets,*smoke_system_travel_fleet_id_,&FleetState::id);const auto stable_snapshot=system_workspace_.travel_snapshot();if(stable_fleet==world.fleets.end()||!stable_snapshot)throw std::runtime_error("System travel reload smoke lost the paused fleet.");const auto stable_marker=std::ranges::find(stable_snapshot->fleets,*smoke_system_travel_fleet_id_,&NativeLocalFleetMarker::fleet_id);if(stable_marker==stable_snapshot->fleets.end())throw std::runtime_error("System travel reload smoke lost the paused marker.");const auto stable_screen=local_fleet_anchor(*stable_marker,project_system(*system_workspace_.snapshot()),*system_workspace_.viewport());smoke_system_travel_after_x_=stable_fleet->local_transit_position.x;smoke_system_travel_after_y_=stable_fleet->local_transit_position.y;smoke_system_travel_after_day_=session_->frame().clock().simulation_days();smoke_system_travel_paused_stable_=stable_fleet->local_transit_position.x==before_canonical.x&&stable_fleet->local_transit_position.y==before_canonical.y&&stable_screen.x==before_screen.x&&stable_screen.y==before_screen.y;const auto order_unchanged=stable_fleet->mission_order_revision==smoke_system_travel_mission_revision_&&stable_fleet->destination_system_id==smoke_system_travel_destination_id_;if(!smoke_system_travel_selected_||!smoke_system_travel_pause_retained_||!smoke_system_travel_paused_stable_||!smoke_system_travel_known_opened_||!smoke_system_travel_unknown_denied_||!smoke_system_travel_knowledge_unchanged_||!order_unchanged||smoke_system_travel_after_day_!=smoke_system_travel_before_day_)throw std::runtime_error("System travel reload smoke did not preserve its paused campaign and local presentation.");return;}
    const auto layout=NativeUiLayout::for_viewport(width,height);click(center(layout.pause));if(session_->frame().clock().speed()==StrategicSpeed::Paused||!system_workspace_.visible())throw std::runtime_error("System travel smoke could not resume while retaining the view.");
    for(int frame_index=0;frame_index<64&&!smoke_system_travel_canonical_moved_;++frame_index){InputSnapshot tick;tick.drawable_width=width;tick.drawable_height=height;(void)update(tick,width,height,.002,true);const auto current=std::ranges::find(world.fleets,*smoke_system_travel_fleet_id_,&FleetState::id);if(current==world.fleets.end())throw std::runtime_error("System travel smoke lost its canonical fleet.");smoke_system_travel_canonical_moved_=std::hypot(current->local_transit_position.x-before_canonical.x,current->local_transit_position.y-before_canonical.y)>.000001f;}
    const auto *after_snapshot=system_workspace_.travel_snapshot();if(!after_snapshot)throw std::runtime_error("System travel smoke lost its local travel presentation.");const auto after_marker=std::ranges::find(after_snapshot->fleets,*smoke_system_travel_fleet_id_,&NativeLocalFleetMarker::fleet_id);if(after_marker==after_snapshot->fleets.end())throw std::runtime_error("System travel smoke fleet left the local display before movement could be measured.");const auto after_screen=local_fleet_anchor(*after_marker,project_system(*system_workspace_.snapshot()),*system_workspace_.viewport());smoke_system_travel_rendered_moved_=std::hypot(after_screen.x-before_screen.x,after_screen.y-before_screen.y)>.0001f;
    click(center(layout.pause));smoke_system_travel_pause_retained_=system_workspace_.visible()&&session_->frame().clock().speed()==StrategicSpeed::Paused;const auto paused_fleet=std::ranges::find(world.fleets,*smoke_system_travel_fleet_id_,&FleetState::id);if(paused_fleet==world.fleets.end())throw std::runtime_error("System travel smoke lost its canonical fleet before the pause check.");smoke_system_travel_after_x_=paused_fleet->local_transit_position.x;smoke_system_travel_after_y_=paused_fleet->local_transit_position.y;smoke_system_travel_after_day_=session_->frame().clock().simulation_days();const auto paused_canonical=paused_fleet->local_transit_position;const auto paused_screen=after_screen;InputSnapshot paused_tick;paused_tick.drawable_width=width;paused_tick.drawable_height=height;(void)update(paused_tick,width,height,1.,true);const auto stable_fleet=std::ranges::find(world.fleets,*smoke_system_travel_fleet_id_,&FleetState::id);const auto stable_snapshot=system_workspace_.travel_snapshot();if(stable_fleet==world.fleets.end()||!stable_snapshot)throw std::runtime_error("System travel smoke lost the paused fleet.");const auto stable_marker=std::ranges::find(stable_snapshot->fleets,*smoke_system_travel_fleet_id_,&NativeLocalFleetMarker::fleet_id);if(stable_marker==stable_snapshot->fleets.end())throw std::runtime_error("System travel smoke lost the paused fleet marker.");const auto stable_screen=local_fleet_anchor(*stable_marker,project_system(*system_workspace_.snapshot()),*system_workspace_.viewport());smoke_system_travel_paused_stable_=stable_fleet->local_transit_position.x==paused_canonical.x&&stable_fleet->local_transit_position.y==paused_canonical.y&&stable_screen.x==paused_screen.x&&stable_screen.y==paused_screen.y&&session_->frame().clock().simulation_days()==smoke_system_travel_after_day_;
    const auto order_unchanged=stable_fleet->mission_order_revision==smoke_system_travel_mission_revision_&&stable_fleet->destination_system_id==smoke_system_travel_destination_id_;if(!smoke_system_travel_selected_||!smoke_system_travel_canonical_moved_||!smoke_system_travel_rendered_moved_||!smoke_system_travel_pause_retained_||!smoke_system_travel_paused_stable_||!smoke_system_travel_known_opened_||!smoke_system_travel_unknown_denied_||!smoke_system_travel_knowledge_unchanged_||!order_unchanged||smoke_system_travel_after_day_<=smoke_system_travel_before_day_)throw std::runtime_error("System travel smoke did not prove navigation, selection, canonical movement, rendered movement, and paused stability.");
  }
  void prepare_settlement_smoke(int width,int height,bool reload){
    smoke_settlement_mode_=true;smoke_settlement_reload_=reload;
    auto click=[&](Point point,InputEventType press=InputEventType::LeftPressed,std::uint8_t count=1){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{press,point,{},0,{},count},{press==InputEventType::RightPressed?InputEventType::RightReleased:InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Settlement smoke input closed the campaign.");};
    auto views=settlement_controller_.build(session_->frame(),session_->cache().generation);
    if(views.empty())throw std::runtime_error("Settlement smoke requires a test-authored populated colony or outpost vessel.");
    const auto chosen=reload?std::ranges::find_if(views,has_active_settlement_target):views.begin();
    if(chosen==views.end())throw std::runtime_error("Settlement reload smoke found no active settlement mission.");
    smoke_settlement_fleet_id_=chosen->fleet_id;
    if(reload){smoke_settlement_kind_=chosen->kind;smoke_settlement_authorization_=chosen->authorization_budget_units;smoke_settlement_requires_authorization_=chosen->requires_new_authorization;smoke_settlement_treasury_before_=chosen->treasury_budget_units;smoke_settlement_colonies_before_=session_->frame().runtime().world().campaign().colonies.size();smoke_settlement_before_day_=session_->frame().clock().simulation_days();}
    if(!fleet_workspace_.view())refresh_fleets(true);
    const auto fleet=std::ranges::find(fleet_workspace_.view()->own_fleets,chosen->fleet_id,&NativeOwnFleet::id);
    if(fleet==fleet_workspace_.view()->own_fleets.end())throw std::runtime_error("Settlement vessel is absent from the owned fleet view.");
    const auto fleet_index=static_cast<std::size_t>(fleet-fleet_workspace_.view()->own_fleets.begin());
    const auto fleet_current_system_id=fleet->current_system_id;
    const auto fleet_layout=FleetWorkspaceLayout::for_viewport(width,height);
    click({fleet_layout.list.x+12.f*fleet_layout.scale,fleet_layout.list.y+(static_cast<float>(fleet_index)*45.f+20.f)*fleet_layout.scale});
    smoke_settlement_selected_=fleet_controller_.selection()==smoke_settlement_fleet_id_;
    if(reload){
      smoke_settlement_system_id_=chosen->destination_system_id
          ?chosen->destination_system_id:fleet_current_system_id;
      smoke_settlement_body_id_=chosen->settlement_body_id
          ?chosen->settlement_body_id:chosen->destination_body_id;
      if(!smoke_settlement_system_id_||!smoke_settlement_body_id_)
        throw std::runtime_error("Settlement reload smoke lost its canonical current target.");
    }else{
      const auto&world=session_->frame().runtime().world().campaign();
      bool found{};
      for(const auto&body:world.bodies){
        const auto system=session_->cache().systems_by_id.find(body.system_id);
        if(system==session_->cache().systems_by_id.end())continue;
        const auto point=camera_.project({system->second->position.x,system->second->position.y},width,height);
        if(point.x<0||point.y<0||point.x>=width||point.y>=height||fleet_layout.panel.contains(point))continue;
        auto candidate=settlement_controller_.preview_exact(session_->frame(),session_->cache().generation,chosen->fleet_id,body.system_id,body.id);
        if(candidate.accepted&&candidate.candidate){smoke_settlement_system_id_=body.system_id;smoke_settlement_body_id_=body.id;found=true;break;}
      }
      if(!found)throw std::runtime_error("Settlement smoke found no observer-admitted exact target for the authored vessel.");
    }
    const auto system=session_->cache().systems_by_id.find(*smoke_settlement_system_id_);
    if(system==session_->cache().systems_by_id.end())throw std::runtime_error("Settlement target system is absent from the campaign cache.");
    click(camera_.project({system->second->position.x,system->second->position.y},width,height),InputEventType::LeftPressed,2);
    if(!system_workspace_.snapshot()||!system_workspace_.viewport())throw std::runtime_error("Settlement smoke could not open the target system.");
    const auto spatial=project_system(*system_workspace_.snapshot());
    const auto body=std::ranges::find(spatial.bodies,*smoke_settlement_body_id_,&SystemSpatialBodyMarker::body_id);
    if(body==spatial.bodies.end())throw std::runtime_error("Settlement target is not visible to the player observer.");
    const auto body_point=system_workspace_.viewport()->world_to_screen(body->offset_x,body->offset_y);
    if(reload){click({body_point.x,body_point.y});session_->frame().clock().set_speed(StrategicSpeed::Paused);capture_settlement_smoke_state();return;}
    click({body_point.x,body_point.y},InputEventType::RightPressed);
    smoke_settlement_previewed_=settlement_workspace_.preview()&&settlement_workspace_.preview()->accepted;
    if(!smoke_settlement_previewed_)throw std::runtime_error("Settlement right-click did not produce an accepted exact quote.");
    smoke_settlement_authorization_=settlement_workspace_.preview()->authorization_budget_units;
    smoke_settlement_requires_authorization_=settlement_workspace_.preview()->requires_new_authorization;
    smoke_settlement_kind_=settlement_workspace_.preview()->kind;
    smoke_settlement_treasury_before_=settlement_workspace_.preview()->treasury_budget_units;
    smoke_settlement_colonies_before_=session_->frame().runtime().world().campaign().colonies.size();
    smoke_settlement_before_day_=session_->frame().clock().simulation_days();
    const auto modal=SettlementWorkspaceLayout::for_viewport(width,height);
    click(center(modal.cancel));
    smoke_settlement_cancelled_=!settlement_workspace_.visible();
    auto cancelled_check=settlement_controller_.preview_exact(session_->frame(),session_->cache().generation,chosen->fleet_id,*smoke_settlement_system_id_,*smoke_settlement_body_id_);
    smoke_settlement_cancel_no_charge_=cancelled_check.treasury_budget_units==smoke_settlement_treasury_before_;
    click({body_point.x,body_point.y},InputEventType::RightPressed);
    if(!settlement_workspace_.preview()||!settlement_workspace_.preview()->accepted)throw std::runtime_error("Settlement smoke could not reopen the exact quote after cancellation.");
    click(center(modal.confirm));
    if(!smoke_settlement_accepted_)throw std::runtime_error("Settlement confirmation was rejected.");
    session_->frame().clock().set_speed(StrategicSpeed::Normal);
  }

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
  void prepare_ship_art_smoke(int width,int height){
    // Route an owned fleet exactly like the fleet input smoke, then open the
    // shipyard: the next rendered frame shows bounded design artwork while the
    // following map frame shows fleet rows and live route effects.
    prepare_fleet_smoke(width,height);
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Ship art smoke input closed the campaign.");
    };
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click(center(main_layout.shipyard));
    if(!shipyard_workspace_.visible()||!shipyard_workspace_.view())
      throw std::runtime_error(
          "Ship art smoke could not open the shipyard workspace.");
  }
  void diplomacy_smoke_click(Point point,int width,int height){
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
    input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
    if(!update(input,width,height,0.,false))throw std::runtime_error("Diplomacy smoke input closed the campaign.");
  }
  void diplomacy_smoke_text(const std::string&value,UiRect region,int width,int height){
    DrawList draw;diplomacy_workspace_.render(draw,width,height,&diplomacy_portrait_provider_);
    for(const auto&item:draw.overlay){
      const auto*label=std::get_if<Text>(&item);
      if(label&&label->value==value&&label->clip&&region.contains(label->at)){
        const auto point=center(*label->clip);
        if(region.contains(point)){diplomacy_smoke_click(point,width,height);return;}
      }
    }
    throw std::runtime_error("Diplomacy smoke cannot locate visible control: "+value);
  }
  void diplomacy_smoke_select(const std::string&contact_id,int width,int height){
    const auto&view=*diplomacy_workspace_.view();
    const auto found=std::ranges::find(view.contacts,contact_id,&NativeDiplomacyContact::contact_id);
    if(found==view.contacts.end())throw std::runtime_error("Diplomacy smoke lost its observed contact.");
    const auto expected_target=found->civilization_id;
    const auto expected_index=found->source_index;
    diplomacy_smoke_text(found->display_name,DiplomacyWorkspaceLayout::for_viewport(width,height).contact_rows,width,height);
    const auto&selected=diplomacy_workspace_.view()->selected;
    if(selected.contact_index!=expected_index||selected.target_civilization_id!=expected_target)
      throw std::runtime_error("Diplomacy contact click did not immediately update paused details.");
  }
  void capture_diplomacy_unknown(int width,int height){
    diplomacy_smoke_select(smoke_diplomacy_unknown_,width,height);
    const auto&selected=diplomacy_workspace_.view()->selected;
    if(selected.target_civilization_id||selected.species_id||selected.trust||selected.hostility||selected.cooperation)
      throw std::runtime_error("Unidentified diplomacy contact disclosed hidden details.");
    DrawList draw;diplomacy_workspace_.render(draw,width,height,&diplomacy_portrait_provider_);
    if(std::ranges::any_of(draw.overlay,[](const auto&item){return std::holds_alternative<Image>(item);}))
      throw std::runtime_error("Unidentified diplomacy contact disclosed a portrait.");
  }
  void prepare_diplomacy_smoke(int width,int height,bool reload){
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)
      diplomacy_smoke_click(center(main_layout.pause),width,height);
    diplomacy_smoke_click(center(main_layout.diplomacy),width,height);
    if(!diplomacy_workspace_.visible()||!diplomacy_workspace_.view())
      throw std::runtime_error("Relations button did not open native diplomacy.");
    const auto state=session_->frame().runtime().diplomacy().snapshot();
    const auto observer=session_->frame().runtime().world().campaign().player_civilization_id;
    const auto expected_status=reload?DiplomaticProposalStatus::accepted:DiplomaticProposalStatus::pending;
    const auto proposal=std::ranges::find_if(state.proposals,[&](const auto&p){return p.recipient_civilization_id==observer&&p.status==expected_status&&p.agreement_type==DiplomaticAgreementType::research_exchange;});
    if(proposal==state.proposals.end())throw std::runtime_error("Diplomacy smoke requires an incoming research-exchange proposal.");
    const auto target=proposal->proposer_civilization_id;
    const auto contacts=diplomacy_workspace_.view()->contacts;
    const auto primary=std::ranges::find(contacts,std::optional<int>{target},&NativeDiplomacyContact::civilization_id);
    const auto unknown=std::ranges::find_if(contacts,[](const auto&c){return !c.identified;});
    const auto other=std::ranges::find_if(contacts,[&](const auto&c){return c.identified&&c.civilization_id!=target;});
    if(primary==contacts.end()||unknown==contacts.end()||other==contacts.end())
      throw std::runtime_error("Diplomacy smoke requires two identified contacts and one unknown signal.");
    smoke_diplomacy_unknown_=unknown->contact_id;
    diplomacy_smoke_select(primary->contact_id,width,height);
    capture_diplomacy_unknown(width,height);
    diplomacy_smoke_select(other->contact_id,width,height);
    diplomacy_smoke_select(primary->contact_id,width,height);
    const auto layout=DiplomacyWorkspaceLayout::for_viewport(width,height);
    if(!reload){
      diplomacy_smoke_text("PROPOSALS",layout.tabs,width,height);
      diplomacy_smoke_text("Accept",layout.detail_rows,width,height);
    }
    const auto after=session_->frame().runtime().diplomacy().snapshot();
    const auto resolved=std::ranges::find(after.proposals,proposal->proposal_id,&DiplomaticProposalSnapshot::proposal_id);
    if(resolved==after.proposals.end()||resolved->status!=DiplomaticProposalStatus::accepted)
      throw std::runtime_error("Incoming diplomatic proposal was not accepted through player input.");
    if(!std::ranges::any_of(after.agreements,[&](const auto&a){return a.type==DiplomaticAgreementType::research_exchange&&a.status==DiplomaticAgreementStatus::active&&((a.civilization_a_id==observer&&a.civilization_b_id==target)||(a.civilization_a_id==target&&a.civilization_b_id==observer));}))
      throw std::runtime_error("Diplomatic acceptance did not activate the canonical agreement.");
    diplomacy_smoke_text("AGREEMENTS",layout.tabs,width,height);
    InputSnapshot scroll;scroll.drawable_width=width;scroll.drawable_height=height;
    scroll.pointer=center(layout.detail_rows);scroll.events={{InputEventType::Wheel,scroll.pointer,{},-10.f}};
    if(!update(scroll,width,height,0.,false))throw std::runtime_error("Diplomacy agreement scrolling closed the campaign.");
    DrawList draw;diplomacy_workspace_.render(draw,width,height,&diplomacy_portrait_provider_);
    if(!std::ranges::any_of(draw.overlay,[&](const auto&item){const auto*label=std::get_if<Text>(&item);return label&&label->value=="Research Exchange"&&label->clip&&layout.detail_rows.contains(label->at);}))
      throw std::runtime_error("Accepted research agreement is not visible after scrolling.");
    if(!std::ranges::any_of(draw.overlay,[&](const auto&item){const auto*image=std::get_if<Image>(&item);return image&&image->resource&&image->resource->width()==2172&&image->resource->height()==724&&layout.stage.contains(center(image->destination));}))
      throw std::runtime_error("Native diplomacy did not render the reviewed transmission artwork.");
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Diplomacy smoke unexpectedly resumed time.");
    std::ostringstream evidence;
    evidence<<"{\"mode\":\""<<(reload?"paused_reload":"progress")<<"\",\"selection_changed\":true,\"unknown_redacted\":true,\"portrait_visible\":true,\"accepted\":"<<(reload?"false":"true")<<",\"proposal_id\":"<<proposal->proposal_id<<",\"target_id\":"<<target<<",\"paused\":true}";
    smoke_diplomacy_status_=evidence.str();
  }
  [[nodiscard]] const std::string&diplomacy_smoke_status()const noexcept{return smoke_diplomacy_status_;}
  void capture_ship_art_shipyard(){
    smoke_shipyard_art_rows_=shipyard_workspace_.last_ship_art_rows();
    smoke_ship_art_decoded_=ship_art_.decoded_count();
    smoke_ship_art_cached_=ship_art_.cached_count();
    smoke_ship_art_bytes_=ship_art_.cache_bytes();
    shipyard_workspace_.close();
  }
  void capture_ship_art_map(){
    smoke_fleet_art_rows_=fleet_workspace_.last_ship_art_rows();
    smoke_route_stats_=last_route_stats_;
  }
  [[nodiscard]] std::string ship_art_smoke_status()const{
    std::ostringstream out;
    out<<"{\"shipyard_art_rows\":"<<smoke_shipyard_art_rows_
       <<",\"fleet_art_rows\":"<<smoke_fleet_art_rows_
       <<",\"decoded_sources\":"<<smoke_ship_art_decoded_
       <<",\"cached_entries\":"<<smoke_ship_art_cached_
       <<",\"cache_bytes\":"<<smoke_ship_art_bytes_
       <<",\"routed_fleets\":"<<smoke_route_stats_.routed_fleets
       <<",\"drawn_legs\":"<<smoke_route_stats_.drawn_legs
       <<",\"chevron_segments\":"<<smoke_route_stats_.chevron_segments
       <<",\"trail_strokes\":"<<smoke_route_stats_.trail_strokes
       <<",\"position_circles\":"<<smoke_route_stats_.position_circles
       <<",\"route_lines\":"<<smoke_route_stats_.emitted_lines
       <<",\"routed_fleet_id\":"<<smoke_fleet_id_.value_or(-1)
       <<",\"destination\":"<<smoke_fleet_destination_.value_or(-1)
       <<"}";
    return out.str();
  }
  void prepare_construction_smoke(int width,int height){
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Construction smoke input closed the campaign.");
    };
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click(center(main_layout.construction));
    if(!construction_workspace_.visible()||!construction_workspace_.view())
      throw std::runtime_error(
          "Construction smoke could not open the workspace.");
    const auto layout=ConstructionWorkspaceLayout::for_viewport(width,height);
    const auto actionable_index=[&]()->std::optional<std::size_t>{
      const auto &projects=construction_workspace_.view()->projects;
      auto found=std::ranges::find(projects,std::string("orbital_shipyard"),
                                   &NativeConstructionProject::id);
      if(found==projects.end()||!found->start.enabled)
        found=std::ranges::find_if(projects,[](const auto &project){
          return project.start.enabled&&!project.complete&&!project.active&&
                 !project.queued;
        });
      if(found==projects.end())return std::nullopt;
      return static_cast<std::size_t>(found-projects.begin());
    };
    if(const auto index=actionable_index()){
      if(session_->frame().clock().speed()==StrategicSpeed::Paused)
        click(center(main_layout.pause));
      smoke_construction_start_was_running_=
          session_->frame().clock().speed()!=StrategicSpeed::Paused;
      InputSnapshot advancing;
      advancing.drawable_width=width;
      advancing.drawable_height=height;
      if(!update(advancing,width,height,.25,true))
        throw std::runtime_error("Construction smoke closed while advancing.");
      const auto refreshed=actionable_index();
      if(!refreshed)throw std::runtime_error(
          "Construction readiness changed before running Start input.");
      click({layout.projects.x+20.f*layout.scale,
             layout.projects.y+
                 (27.f+static_cast<float>(*refreshed)*58.f+24.f)*
                     layout.scale});
      smoke_construction_project_id_=
          construction_workspace_.selected_project_id();
      click(center(layout.primary_action));
      if(!last_construction_command_accepted_)
        throw std::runtime_error(
            "Construction smoke canonical Start was rejected.");
      smoke_construction_started_=true;
      click(center(layout.secondary_action));
      smoke_construction_paused_for_quote_=
          session_->frame().clock().speed()==StrategicSpeed::Paused;
    }
  }
  void capture_surface_smoke_state(){
    if(!smoke_surface_site_id_)return;
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    refresh_surface(true);
    if(!surface_workspace_.view())throw std::runtime_error("Surface smoke lost its observer-safe colony view before save.");
    const auto&view=*surface_workspace_.view();
    const auto site=std::ranges::find(view.construction_sites,*smoke_surface_site_id_,&NativeSurfaceSite::building_id);
    if(site==view.construction_sites.end()||site->complete)throw std::runtime_error("Surface smoke lost its unfinished canonical site before save.");
    smoke_surface_x_=site->x;smoke_surface_z_=site->z;smoke_surface_rotation_=site->rotation_degrees;smoke_surface_progress_=site->industry_progress;smoke_surface_site_count_saved_=view.construction_sites.size();smoke_surface_treasury_saved_=view.treasury_budget_units;smoke_surface_saved_day_=session_->frame().clock().simulation_days();smoke_surface_persisted_site_=true;
  }
  void request_smoke_save(){if(smoke_settlement_mode_){session_->frame().clock().set_speed(StrategicSpeed::Paused);capture_settlement_smoke_state();}if(smoke_surface_mode_)capture_surface_smoke_state();session_->request_save();}
  [[nodiscard]] bool smoke_save_succeeded()const{return session_->notice().kind==SessionNoticeKind::Saved;}
  [[nodiscard]] std::size_t system_count()const{return session_->frame().runtime().world().campaign().systems.size();}
  [[nodiscard]] std::string player_species_id()const{
    const auto &world=session_->frame().runtime().world().campaign();
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    if(player==world.civilizations.end())throw std::runtime_error("The activated campaign has no player civilization.");
    return player->species_id;
  }
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
  [[nodiscard]] std::string construction_smoke_status()const{
    if(!construction_workspace_.view())return "unavailable";
    std::ostringstream out;
    out<<(smoke_construction_started_?"started":"locked")<<":"
       <<construction_workspace_.view()->construction_revision<<":"
       <<smoke_construction_project_id_.value_or("none")<<":"
       <<(smoke_construction_start_was_running_?"start-running":"start-not-run")
       <<":"<<(smoke_construction_paused_for_quote_?"quote-paused"
                                                        :"quote-unproven");
    return out.str();
  }
  [[nodiscard]] std::string system_smoke_status()const{
    if(!system_workspace_.visible()||!system_workspace_.viewport())return "unavailable";
    std::ostringstream out;out<<"id="<<system_workspace_.system_id().value_or(-1)
      <<":body="<<system_workspace_.selected_body_id().value_or(-1)
      <<":visible="<<system_workspace_.visible_body_count()
      <<":scale="<<std::fixed<<std::setprecision(6)<<system_workspace_.viewport()->scale
      <<":entry="<<smoke_system_entered_<<":hit="<<smoke_system_hit_
      <<":pan="<<smoke_system_panned_<<":zoom="<<smoke_system_zoomed_
      <<":reset="<<smoke_system_reset_<<":back="<<smoke_system_back_
      <<":pause_retained="<<smoke_system_pause_retained_
      <<":speed_retained="<<smoke_system_speed_retained_
      <<":gesture_cleared="<<smoke_system_gesture_cleared_
      <<":paused="<<(session_->frame().clock().speed()==StrategicSpeed::Paused)
      <<":day_unchanged="<<(session_->frame().clock().simulation_days()==smoke_system_day_);
    return out.str();
  }
  [[nodiscard]] std::string colony_smoke_status()const{
    if(!colony_workspace_.view())return "unavailable";
    const auto&view=*colony_workspace_.view();
    std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<std::boolalpha
      <<"{\"mode\":\""<<(smoke_colony_reload_?"paused_reload":"fresh")
      <<"\",\"player_id\":"<<view.player_civilization_id
      <<",\"system_id\":"<<view.system_id
      <<",\"body_id\":"<<view.body_id
      <<",\"colony_id\":"<<view.colony_id
      <<",\"revision\":"<<view.revision
      <<",\"site_count\":"<<view.construction_sites.size()
      <<",\"population_millions\":"<<view.population_millions
      <<",\"support_ratio\":"<<view.sustenance_support_ratio
      <<",\"food_reserve_days\":"<<view.food_reserve_days
      <<",\"water_reserve_days\":"<<view.water_reserve_days
      <<",\"power_supply\":"<<view.power_supply
      <<",\"power_demand\":"<<view.power_demand
      <<",\"selected\":"<<smoke_colony_selected_
      <<",\"opened\":"<<smoke_colony_opened_
      <<",\"back_restored\":"<<smoke_colony_back_
      <<",\"pause_retained\":"<<smoke_colony_pause_retained_
      <<",\"speed_retained\":"<<smoke_colony_speed_retained_
      <<",\"paused\":"<<(session_->frame().clock().speed()==StrategicSpeed::Paused)
      <<",\"day_unchanged\":"<<(session_->frame().clock().simulation_days()==smoke_colony_day_)<<'}';
    return out.str();
  }
  [[nodiscard]] std::string settlement_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<std::boolalpha<<"{\"mode\":\""<<(smoke_settlement_reload_?"paused_reload":"ordered")<<"\",\"kind\":\""<<(smoke_settlement_kind_==NativeSettlementMissionKind::ResourceOutpost?"outpost":"colony")<<"\",\"fleet_id\":"<<smoke_settlement_fleet_id_.value_or(-1)<<",\"system_id\":"<<smoke_settlement_system_id_.value_or(-1)<<",\"body_id\":"<<smoke_settlement_body_id_.value_or(-1)<<",\"mission_revision\":"<<smoke_settlement_revision_<<",\"before_days\":"<<smoke_settlement_before_day_<<",\"saved_days\":"<<smoke_settlement_saved_day_<<",\"settlement_days\":"<<smoke_settlement_progress_<<",\"authorization\":"<<smoke_settlement_authorization_<<",\"treasury_before\":"<<smoke_settlement_treasury_before_<<",\"treasury_after\":"<<smoke_settlement_treasury_after_<<",\"requires_authorization\":"<<smoke_settlement_requires_authorization_<<",\"selected\":"<<smoke_settlement_selected_<<",\"previewed\":"<<smoke_settlement_previewed_<<",\"cancelled\":"<<smoke_settlement_cancelled_<<",\"cancel_no_charge\":"<<smoke_settlement_cancel_no_charge_<<",\"accepted\":"<<smoke_settlement_accepted_<<",\"no_instant_colony\":"<<smoke_settlement_no_instant_colony_<<",\"paused\":"<<(session_->frame().clock().speed()==StrategicSpeed::Paused)<<'}';return out.str();}
  [[nodiscard]] std::string surface_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<std::boolalpha<<"{\"mode\":\""<<(smoke_surface_reload_?"paused_reload":"ordered")<<"\",\"system_id\":"<<smoke_surface_system_id_<<",\"body_id\":"<<smoke_surface_body_id_<<",\"colony_id\":"<<smoke_surface_colony_id_<<",\"type_id\":\""<<smoke_surface_type_id_<<"\",\"site_id\":"<<smoke_surface_site_id_.value_or(-1)<<",\"x\":"<<smoke_surface_x_<<",\"z\":"<<smoke_surface_z_<<",\"rotation\":"<<smoke_surface_rotation_<<",\"authorization\":"<<smoke_surface_authorization_<<",\"refund\":"<<smoke_surface_refund_<<",\"treasury_before\":"<<smoke_surface_treasury_before_<<",\"treasury_after_cancel\":"<<smoke_surface_treasury_before_<<",\"treasury_after_place\":"<<smoke_surface_treasury_after_place_<<",\"treasury_after_refund\":"<<smoke_surface_treasury_after_refund_<<",\"treasury_saved\":"<<smoke_surface_treasury_saved_<<",\"site_count_before\":"<<smoke_surface_site_count_before_<<",\"site_count_saved\":"<<smoke_surface_site_count_saved_<<",\"progress\":"<<smoke_surface_progress_<<",\"before_days\":"<<smoke_surface_before_day_<<",\"saved_days\":"<<smoke_surface_saved_day_<<",\"palette_selected\":"<<smoke_surface_palette_selected_<<",\"ghost_previewed\":"<<smoke_surface_ghost_previewed_<<",\"placement_cancelled\":"<<smoke_surface_placement_cancelled_<<",\"cancel_no_change\":"<<smoke_surface_cancel_no_change_<<",\"placement_confirmed\":"<<smoke_surface_placement_confirmed_<<",\"removal_previewed\":"<<smoke_surface_removal_previewed_<<",\"removal_confirmed\":"<<smoke_surface_removal_confirmed_<<",\"refund_exact\":"<<smoke_surface_refund_exact_<<",\"persisted_site\":"<<smoke_surface_persisted_site_<<",\"paused\":"<<(session_->frame().clock().speed()==StrategicSpeed::Paused)<<'}';return out.str();}
  [[nodiscard]] std::string system_travel_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(9)<<std::boolalpha<<"{\"mode\":\""<<(smoke_system_travel_reload_?"paused_reload":"progress")<<"\",\"fleet_id\":"<<smoke_system_travel_fleet_id_.value_or(-1)<<",\"system_id\":"<<smoke_system_travel_system_id_.value_or(-1)<<",\"destination_id\":"<<smoke_system_travel_destination_id_.value_or(-1)<<",\"order_revision\":"<<smoke_system_travel_mission_revision_<<",\"lane_count\":"<<smoke_system_travel_lane_count_<<",\"before_x\":"<<smoke_system_travel_before_x_<<",\"before_y\":"<<smoke_system_travel_before_y_<<",\"after_x\":"<<smoke_system_travel_after_x_<<",\"after_y\":"<<smoke_system_travel_after_y_<<",\"before_days\":"<<smoke_system_travel_before_day_<<",\"after_days\":"<<smoke_system_travel_after_day_<<",\"selected\":"<<smoke_system_travel_selected_<<",\"canonical_moved\":"<<smoke_system_travel_canonical_moved_<<",\"rendered_moved\":"<<smoke_system_travel_rendered_moved_<<",\"paused_stable\":"<<smoke_system_travel_paused_stable_<<",\"pause_retained\":"<<smoke_system_travel_pause_retained_<<",\"known_arrow\":"<<smoke_system_travel_known_opened_<<",\"unknown_denied\":"<<smoke_system_travel_unknown_denied_<<",\"knowledge_unchanged\":"<<smoke_system_travel_knowledge_unchanged_<<",\"lanes_connected\":"<<smoke_system_travel_lanes_connected_<<'}';return out.str();}
  [[nodiscard]] bool wants_text_input() const noexcept {
    return research_workspace_.wants_text_input() &&
           !shipyard_workspace_.visible() &&
           !construction_workspace_.visible();
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    pointer_=input.pointer;
    const auto timestamp=utc_timestamp();
    if(session_->service(timestamp,menu_)){
      selected_id_.reset();
      pre_menu_speed_=StrategicSpeed::Normal;
      fit_camera(width,height);
      galaxy_backdrop_.discard_campaign();
      bind_galaxy_backdrop(width,height);
      refresh_knowledge();
      research_workspace_.discard_campaign();
      projected_research_stamp_.reset();
      fleet_workspace_.discard_campaign();
      shipyard_workspace_.discard_campaign();
      construction_workspace_.discard_campaign();
      diplomacy_workspace_.discard_campaign();
      diplomacy_portraits_.clear();
      colony_workspace_.discard_campaign();
      surface_workspace_.discard_campaign();
      settlement_workspace_.discard_campaign();
      colony_entry_view_.reset();
      system_workspace_.discard_campaign();
      planet_discs_.discard_campaign();
      fleet_marker_offsets_.clear();
      pending_fleet_preview_.reset();
      refresh_fleets(true);
      if(research_workspace_.visible())refresh_research(true);
      if(shipyard_workspace_.visible())refresh_shipyard(true);
      if(construction_workspace_.visible())refresh_construction(true);
    }
    if(session_->exit_ready())return false;
    if(input.quit_requested)session_->request_exit();
    const auto layout=NativeUiLayout::for_viewport(width,height);
    for(const auto &event:input.events){
      if(surface_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        const auto opens_workspace=top_action==UiAction::Research||top_action==UiAction::Shipyard||top_action==UiAction::Construction||top_action==UiAction::Diplomacy;
        if(opens_workspace)surface_workspace_.close();
        else if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=surface_workspace_.handle(event,width,height);
          if(command.kind!=SurfaceWorkspaceCommandKind::None)execute_surface(command);
          if(command.captured)continue;
        }
      }
      if(settlement_workspace_.visible()&&!menu_){
        const auto command=settlement_workspace_.handle(event,width,height);
        if(command.kind==SettlementWorkspaceCommandKind::Confirm)
          execute_settlement();
        if(command.captured)continue;
      }
      if(colony_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        const auto opens_workspace=top_action==UiAction::Research||top_action==UiAction::Shipyard||top_action==UiAction::Construction||top_action==UiAction::Diplomacy;
        if(opens_workspace)colony_workspace_.close();
        else if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=colony_workspace_.handle(event,width,height);
          if(command.kind==ColonyWorkspaceCommandKind::Close)gesture_.cancel();
          else if(command.kind==ColonyWorkspaceCommandKind::OpenSurface)open_surface(width,height);
          if(command.captured)continue;
        }
      }
      if(system_workspace_.visible()&&!colony_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        const auto opens_workspace=top_action==UiAction::Research||top_action==UiAction::Shipyard||top_action==UiAction::Construction||top_action==UiAction::Diplomacy;
        if(opens_workspace)system_workspace_.close();
        else if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=system_workspace_.handle(event,width,height);
          if(command.kind==SystemWorkspaceCommandKind::close){system_workspace_.close();gesture_.cancel();}
          else if(command.kind==SystemWorkspaceCommandKind::select_fleet){const auto selected=fleet_controller_.select_next_hit(session_->frame(),session_->cache().generation,command.hit_fleet_ids);system_workspace_.set_notice(selected.message);refresh_fleets(true);refresh_system_travel(true);}
          else if(command.kind==SystemWorkspaceCommandKind::open_destination){if(!enter_system(command.target_id,width,height))system_workspace_.set_notice("Destination details are not available to this observer.");}
           else if(command.kind==SystemWorkspaceCommandKind::open_colony){open_colony_from_system(command.target_id);}
           else if(command.kind==SystemWorkspaceCommandKind::settlement_target){preview_settlement(command.target_id,width,height);}
          refresh_colony_entry(false);
          continue;
        }
      }
      if(event.type==InputEventType::EscapePressed){
        if(diplomacy_workspace_.modal_open())diplomacy_workspace_.dismiss_modal();
        else if(diplomacy_workspace_.visible())diplomacy_workspace_.close();
        else if(surface_workspace_.visible())surface_workspace_.close();
        else if(colony_workspace_.visible())colony_workspace_.close();
        else if(construction_workspace_.visible())construction_workspace_.close();
        else if(shipyard_workspace_.visible())shipyard_workspace_.close();
        else if(research_workspace_.visible())research_workspace_.close();
        else toggle_menu();
        continue;
      }
      if(event.type==InputEventType::PointerCancelled){
        gesture_.cancel();
        (void)research_workspace_.handle(event,width,height);
        (void)shipyard_workspace_.handle(event,width,height);
        (void)construction_workspace_.handle(event,width,height);
        (void)diplomacy_workspace_.handle(event,width,height);
        (void)colony_workspace_.handle(event,width,height);
        (void)surface_workspace_.handle(event,width,height);
        continue;
      }
      if(diplomacy_workspace_.visible()){
        const auto command=diplomacy_workspace_.handle(event,width,height);
        if(command.kind==DiplomacyWorkspaceCommandKind::Close)
          diplomacy_workspace_.close();
        else if(command.kind==DiplomacyWorkspaceCommandKind::SelectContact)
          refresh_diplomacy(true);
        else if(command.kind==DiplomacyWorkspaceCommandKind::Action||
                command.kind==DiplomacyWorkspaceCommandKind::ProposalAction)
          execute_diplomacy(command);
        else if(command.kind==DiplomacyWorkspaceCommandKind::FocusSystem){
          if(!enter_system(command.focus_system_id,width,height))
            diplomacy_workspace_.set_notice(
                "The last observation is outside surveyed space.",false);
        }
        if(command.captured)continue;
      }
      if(construction_workspace_.visible()){
        const auto command=construction_workspace_.handle(event,width,height);
        if(command.kind!=ConstructionWorkspaceCommandKind::None)
          execute_construction(command);
        if(command.captured)continue;
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
      if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
        if(event.type==InputEventType::LeftPressed&&event.click_count>=2){
          if(const auto target=system_hit(event.position,width,height);target&&enter_system(*target,width,height)){
            gesture_.capture_for_ui();continue;
          }
        }
        if(event.type==InputEventType::Wheel&&event.wheel_y>0&&camera_.pixels_per_world>=99.){
          const auto target=system_hit(event.position,width,height).or_else([&]{return selected_id_;});
          if(target&&enter_system(*target,width,height)){gesture_.capture_for_ui();continue;}
        }
      }
      if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
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
          else{shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();research_workspace_.open();refresh_research(true);}
          captured=true;
        }
        else if(action==UiAction::Shipyard){
          if(shipyard_workspace_.visible())shipyard_workspace_.close();
          else{research_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();shipyard_workspace_.open();refresh_shipyard(true);}
          captured=true;
        }
        else if(action==UiAction::Construction){
          if(construction_workspace_.visible())construction_workspace_.close();
          else{research_workspace_.close();shipyard_workspace_.close();diplomacy_workspace_.close();construction_workspace_.open();refresh_construction(true);}
          captured=true;
        }
        else if(action==UiAction::Diplomacy){
          if(diplomacy_workspace_.visible())diplomacy_workspace_.close();
          else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.open();refresh_diplomacy(true);}
          captured=true;
        }
        if(research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible()||diplomacy_workspace_.visible()||colony_workspace_.visible()||surface_workspace_.visible())captured=true;
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.allows_world_drag())camera_.pan_pixels(event.delta.x,event.delta.y);gesture_.move(event.delta);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&!gesture_.captured_by_ui())camera_.zoom_at(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_||surface_workspace_.visible()||colony_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible())(void)gesture_.release_as_world_click();}
    }
    if(const auto request=surface_workspace_.take_preview_request())execute_surface(*request);
    if(research_workspace_.take_refresh_request())refresh_research(true);
    if(advance_simulation){
      const auto frame_result=session_->advance(menu_?0.:elapsed,timestamp);
      research_refresh_elapsed_+=elapsed;
      refresh_research(false);
      fleet_refresh_elapsed_+=elapsed;
      refresh_fleets(false);
      shipyard_refresh_elapsed_+=elapsed;
      refresh_shipyard(false);
      construction_refresh_elapsed_+=elapsed;
      refresh_construction(false);
      diplomacy_refresh_elapsed_+=elapsed;
      refresh_diplomacy(false);
      colony_refresh_elapsed_+=elapsed;
      refresh_colony(false);
      system_refresh_elapsed_+=elapsed;
      refresh_system(false);
      if(std::ranges::any_of(frame_result.completed_substeps,[](double step){return step>0.;}))refresh_system_travel(true);
      if(smoke_save_pending_){smoke_save_pending_=false;session_->request_save();}
    }
    refresh_knowledge();
    return true;
  }

  [[nodiscard]] bool artwork_ready()const noexcept{return system_workspace_.visible()?system_workspace_.artwork_ready():galaxy_backdrop_.artwork_ready();}

  [[nodiscard]] DrawList scene(int width,int height){
    const auto screen_height=static_cast<float>(height);
    DrawList out; std::optional<std::size_t> galaxy_marker_begin; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    if(system_workspace_.visible())system_workspace_.render(out,width,height);else{
    galaxy_backdrop_.append(out,{cache.generation,width,height,camera_,fitted_pixels_per_world_,true});
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    galaxy_marker_begin=out.world.size();
    for(const auto &system:world.systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);if(p.x<-14||p.y<-14||p.x>width+14||p.y>height+14)continue;const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;NativeGalaxyStarAppearance appearance;const auto survey=world.knowledge.system_survey_level(world.player_civilization_id,system.id);if(survey==SystemSurveyLevel::fully_surveyed){if(system.primary)appearance.primary=galaxy_star_visual(*system.primary);if(system.secondary)appearance.secondary=galaxy_star_visual(*system.secondary);if(system.tertiary)appearance.tertiary=galaxy_star_visual(*system.tertiary);}galaxy_star_markers_.append(out,p,selected?4.2f:2.f,appearance,selected,UiRect{0,0,static_cast<float>(width),static_cast<float>(height)});if(selected||(known&&camera_.pixels_per_world>7.f))out.text.push_back({{p.x+8,p.y-4},known?system.name:"Unknown",{205,222,245,235}});}
    if(const auto &fleet_view=fleet_workspace_.view();fleet_view)
      last_route_stats_=append_fleet_route_effects(out,fleet_view->own_fleets,cache.systems_by_id,camera_,width,height);
    else last_route_stats_={};
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
    fill(out, layout.construction,
         construction_workspace_.visible()
             ? selected
             : layout.construction.contains(pointer_) ? hover : button);
    stroke(out, layout.construction,
           construction_workspace_.visible() ? Color{154, 225, 188, 255}
                                             : border);
    label(out, layout.construction, "CONSTRUCTION", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.diplomacy,
         diplomacy_workspace_.visible()
             ? selected
             : layout.diplomacy.contains(pointer_) ? hover : button);
    stroke(out, layout.diplomacy,
           diplomacy_workspace_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.diplomacy, "RELATIONS", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y + 2.f * layout.scale},
        "Day " + std::to_string(static_cast<int>(
                     session_->frame().clock().simulation_days())),
        {154, 181, 211, 235}, layout.metric_font_pixels,
        layout.day_text.width, layout.day_text});
    if(selected_id_){const auto found=cache.systems_by_id.find(*selected_id_);if(found!=cache.systems_by_id.end()){const bool known=known_.contains(*selected_id_);const float x=18,y=screen_height-82;out.text.push_back({{x,y},known?found->second->name:"Unknown system",{238,244,255,255}});out.text.push_back({{x,y+18},known?spectral_name(found->second->primary):"No survey data",{154,181,211,235}});}}
    const auto &notice = session_->notice();
    const bool preparing_galaxy=!system_workspace_.visible()&&!galaxy_backdrop_.artwork_ready();
    if (notice.kind != SessionNoticeKind::None || preparing_galaxy) {
      auto message = notice.message;
      if(preparing_galaxy&&(notice.kind==SessionNoticeKind::None||notice.kind==SessionNoticeKind::Saved||notice.kind==SessionNoticeKind::Loaded))
        message="Preparing galaxy imagery...";
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
    if(!menu_&&!system_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible())
      fleet_workspace_.render(out,width,height,fleet_markers(width,height),&ship_art_);
    if(galaxy_marker_begin)
      promote_legacy_galaxy_foreground(out,*galaxy_marker_begin);
    research_workspace_.render(out, width, height);
    shipyard_workspace_.render(out, width, height, &ship_art_);
    construction_workspace_.render(out, width, height);
    diplomacy_workspace_.render(out, width, height, &diplomacy_portrait_provider_);
    if(!surface_workspace_.visible())colony_workspace_.render(out, width, height);
    surface_workspace_.render(out,width,height);
    settlement_workspace_.render(out,width,height);
    return out;
  }
 private:
  [[nodiscard]] bool enter_system(int system_id,int width,int height){
    auto built=system_controller_.build(session_->frame(),session_->cache().generation,system_id);
    if(!built.snapshot)return false;
    auto travel=system_travel_controller_.build(session_->frame(),session_->cache().generation,*built.snapshot);
    gesture_.cancel();research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();colony_workspace_.close();surface_workspace_.close();settlement_workspace_.clear();colony_entry_view_.reset();
    system_workspace_.open(std::move(*built.snapshot),width,height);selected_id_=system_id;
    if(travel.snapshot)system_workspace_.refresh_travel(std::move(*travel.snapshot),fleet_controller_.selection());else system_workspace_.set_notice(travel.denial);
    system_refresh_elapsed_=0.;return true;
  }

  void refresh_system(bool force){
    if(!system_workspace_.visible()||!system_workspace_.system_id())return;
    const auto &world=session_->frame().runtime().world().campaign();
    const auto level=world.knowledge.system_survey_level(world.player_civilization_id,*system_workspace_.system_id());
    if(!force&&system_refresh_elapsed_<1.&&system_workspace_.survey_level()==level)return;
    auto built=system_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.system_id());
    if(!built.snapshot){system_workspace_.close();return;}
    system_workspace_.refresh(std::move(*built.snapshot));system_refresh_elapsed_=0.;refresh_system_travel(true);refresh_colony_entry(true);
  }

  void refresh_system_travel(bool force){if(!force||!system_workspace_.visible()||!system_workspace_.snapshot())return;auto built=system_travel_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.snapshot());if(built.snapshot)system_workspace_.refresh_travel(std::move(*built.snapshot),fleet_controller_.selection());else{system_workspace_.clear_travel();system_workspace_.set_notice(built.denial);}refresh_settlement_status();}
  void refresh_settlement_status(){const auto selected=fleet_controller_.selection();if(!selected){system_workspace_.set_settlement_status(std::nullopt);return;}const auto status=settlement_controller_.live_status(session_->frame(),session_->cache().generation,*selected);if(!status){system_workspace_.set_settlement_status(std::nullopt);return;}system_workspace_.set_settlement_status(NativeSystemSettlementStatus{status->fleet_id,status->status,status->destination_system_id,status->destination_body_id,status->settlement_days_completed,status->establishment_days});}

  void refresh_colony_entry(bool force){
    if(!system_workspace_.visible()||!system_workspace_.snapshot()||!system_workspace_.selected_body_id()){
      colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);return;
    }
    if(!force&&colony_entry_view_&&colony_entry_view_->campaign_generation==session_->cache().generation&&colony_entry_view_->system_id==*system_workspace_.system_id()&&colony_entry_view_->body_id==*system_workspace_.selected_body_id())return;
    auto built=colony_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.snapshot(),*system_workspace_.selected_body_id());
    if(built.view){colony_entry_view_=std::move(*built.view);system_workspace_.set_colony_body(colony_entry_view_->body_id);}
    else{colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);}
  }

  void open_colony_from_system(int body_id){
    refresh_colony_entry(true);
    if(!colony_entry_view_||colony_entry_view_->body_id!=body_id){system_workspace_.set_notice("Colony operations are unavailable for this body.");return;}
    colony_workspace_.open(*colony_entry_view_);gesture_.capture_for_ui();colony_refresh_elapsed_=0.;
  }

  void refresh_colony(bool force){
    if(!colony_workspace_.visible()||!system_workspace_.snapshot()||!system_workspace_.selected_body_id())return;
    if(!force&&colony_refresh_elapsed_<.2)return;
    auto built=colony_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.snapshot(),*system_workspace_.selected_body_id());
    if(!built.view){surface_workspace_.close();colony_workspace_.close();colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);return;}
    colony_entry_view_=*built.view;if(surface_workspace_.visible())surface_workspace_.set_view(*built.view);colony_workspace_.set_view(std::move(*built.view));system_workspace_.set_colony_body(colony_entry_view_->body_id);colony_refresh_elapsed_=0.;
  }

  void open_surface(int width,int height){
    if(!colony_workspace_.view()||!colony_workspace_.view()->solid_surface){
      return;
    }
    surface_workspace_.open(*colony_workspace_.view(),width,height);
    gesture_.capture_for_ui();
  }

  void refresh_surface(bool force){
    if(!surface_workspace_.visible()||!system_workspace_.snapshot()||
       !system_workspace_.selected_body_id())return;
    if(!force&&colony_refresh_elapsed_<.2)return;
    auto built=colony_controller_.build(
        session_->frame(),session_->cache().generation,
        *system_workspace_.snapshot(),*system_workspace_.selected_body_id());
    if(!built.view||!built.view->solid_surface){
      surface_workspace_.close();
      return;
    }
    colony_entry_view_=*built.view;
    colony_workspace_.set_view(*built.view);
    surface_workspace_.set_view(std::move(*built.view));
    colony_refresh_elapsed_=0.;
  }

  void execute_surface(const SurfaceWorkspaceCommand&command){
    if(!surface_workspace_.view())return;
    const auto generation=session_->cache().generation;
    if(command.kind==SurfaceWorkspaceCommandKind::PreviewPlacement){
      if(command.open_confirmation)
        session_->frame().clock().set_speed(StrategicSpeed::Paused);
      auto quote=surface_controller_.preview_placement(
          session_->frame(),generation,*surface_workspace_.view(),
          command.type_id,command.x,command.z,command.rotation_degrees);
      surface_workspace_.set_placement_quote(std::move(quote),
                                             command.open_confirmation);
      gesture_.capture_for_ui();
      return;
    }
    if(command.kind==SurfaceWorkspaceCommandKind::PreviewRemoval){
      session_->frame().clock().set_speed(StrategicSpeed::Paused);
      auto quote=surface_controller_.preview_removal(
          session_->frame(),generation,*surface_workspace_.view(),
          command.building_id);
      surface_workspace_.set_removal_quote(std::move(quote));
      gesture_.capture_for_ui();
      return;
    }
    if(command.kind==SurfaceWorkspaceCommandKind::CancelQuote){
      (void)surface_controller_.cancel_quote(generation,
                                             command.quote_revision);
      return;
    }
    NativeSurfaceCommandOutcome outcome;
    if(command.kind==SurfaceWorkspaceCommandKind::ConfirmPlacement){
      if(!surface_workspace_.placement_quote()||
         surface_workspace_.placement_quote()->quote_revision!=
             command.quote_revision)return;
      outcome=surface_controller_.confirm_placement(
          session_->frame(),generation,*surface_workspace_.placement_quote());
    }else if(command.kind==SurfaceWorkspaceCommandKind::ConfirmRemoval){
      if(!surface_workspace_.removal_quote()||
         surface_workspace_.removal_quote()->quote_revision!=
             command.quote_revision)return;
      outcome=surface_controller_.confirm_removal(
          session_->frame(),generation,*surface_workspace_.removal_quote());
    }else return;
    surface_workspace_.complete_command(visible_notice(outcome.message));
    refresh_surface(true);
  }

  void preview_settlement(int body_id,int width,int height){
    if(!system_workspace_.system_id()||!fleet_controller_.selection()){
      system_workspace_.set_notice("Select an owned colony or outpost vessel before choosing a settlement target.");
      return;
    }
    if(!settlement_controller_.live_status(session_->frame(),session_->cache().generation,*fleet_controller_.selection())){
      system_workspace_.set_notice("The selected fleet is not a populated settlement vessel.");
      return;
    }
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    auto preview=settlement_controller_.preview_exact(
        session_->frame(),session_->cache().generation,
        *fleet_controller_.selection(),*system_workspace_.system_id(),body_id);
    settlement_workspace_.set_preview(std::move(preview));
    (void)system_workspace_.handle({InputEventType::PointerCancelled},width,height);
    gesture_.cancel();
    gesture_.capture_for_ui();
  }

  void execute_settlement(){
    if(!settlement_workspace_.preview())return;
    const auto outcome=settlement_controller_.issue_exact(
        session_->frame(),session_->cache().generation,
        settlement_workspace_.preview()->revision);
    smoke_settlement_accepted_=outcome.accepted;
    if(outcome.accepted){const auto&world=session_->frame().runtime().world().campaign();const auto economy=std::ranges::find(world.economies,world.player_civilization_id,&CivilizationEconomy::civilization_id);if(economy==world.economies.end())throw std::runtime_error("Settlement command lost the player treasury.");smoke_settlement_treasury_after_=economy->credits;}
    settlement_workspace_.clear();
    system_workspace_.set_notice(outcome.message);
    refresh_fleets(true);
    refresh_system_travel(true);
  }

  void capture_settlement_smoke_state(){
    if(!smoke_settlement_fleet_id_)return;
    const auto&world=session_->frame().runtime().world().campaign();
    const auto fleet=std::ranges::find(world.fleets,*smoke_settlement_fleet_id_,&FleetState::id);
    if(fleet==world.fleets.end())throw std::runtime_error("Settlement smoke lost its canonical vessel.");
    smoke_settlement_saved_day_=session_->frame().clock().simulation_days();
    smoke_settlement_revision_=fleet->mission_order_revision;
    smoke_settlement_progress_=fleet->settlement_days_completed;
    const auto economy=std::ranges::find(world.economies,world.player_civilization_id,&CivilizationEconomy::civilization_id);
    if(economy==world.economies.end())throw std::runtime_error("Settlement smoke lost the player treasury.");
    if(smoke_settlement_reload_)smoke_settlement_treasury_after_=economy->credits;
    smoke_settlement_no_instant_colony_=world.colonies.size()==smoke_settlement_colonies_before_;
    if(!smoke_settlement_reload_&&smoke_settlement_saved_day_<=smoke_settlement_before_day_)
      throw std::runtime_error("Settlement smoke recorded no positive canonical simulation advancement.");
  }

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

  void refresh_construction(bool force){
    if(!construction_workspace_.visible())return;
    if(!force&&construction_refresh_elapsed_<.2)return;
    construction_workspace_.set_view(construction_controller_.build(
        session_->frame(),session_->cache().generation));
    construction_refresh_elapsed_=0.;
  }

  void execute_construction(const ConstructionWorkspaceCommand &command){
    const auto &view=construction_workspace_.view();
    if(!view){
      construction_workspace_.set_notice(
          "Construction details are still loading.",false);
      return;
    }
    if(command.kind==ConstructionWorkspaceCommandKind::PrepareCancel){
      auto &clock=session_->frame().clock();
      if(clock.speed()!=StrategicSpeed::Paused)
        clock.set_speed(StrategicSpeed::Paused);
      refresh_construction(true);
      if(!construction_workspace_.arm_cancel_confirmation(command.project_id))
        construction_workspace_.set_notice(
            "The cancellation quote changed; review the refreshed project.",
            false);
      return;
    }
    NativeConstructionCommandOutcome outcome;
    if(command.kind==ConstructionWorkspaceCommandKind::Start)
      outcome=construction_controller_.start(
          session_->frame(),session_->cache().generation,
          view->construction_revision,command.project_id);
    else if(command.kind==ConstructionWorkspaceCommandKind::Queue)
      outcome=construction_controller_.queue(
          session_->frame(),session_->cache().generation,
          view->construction_revision,command.project_id);
    else if(command.kind==ConstructionWorkspaceCommandKind::Cancel)
      outcome=construction_controller_.cancel(
          session_->frame(),session_->cache().generation,
          view->construction_revision,command.project_id);
    else return;
    construction_workspace_.set_notice(outcome.message,outcome.accepted);
    last_construction_command_accepted_=outcome.accepted;
    refresh_construction(true);
  }

  void refresh_diplomacy(bool force){
    if(!diplomacy_workspace_.visible())return;
    if(!force&&diplomacy_refresh_elapsed_<.2)return;
    const auto generation=session_->cache().generation;
    diplomacy_workspace_.set_view(diplomacy_controller_.build(
        session_->frame(),generation,diplomacy_workspace_.selected_contact_index()));
    // Selection preservation can move the index when the contact list changed;
    // re-project once so the details panel tracks the same civilization.
    if(const auto &view=diplomacy_workspace_.view();
       view&&!view->contacts.empty()&&
       view->selected.contact_index!=diplomacy_workspace_.selected_contact_index())
      diplomacy_workspace_.set_view(diplomacy_controller_.build(
          session_->frame(),generation,diplomacy_workspace_.selected_contact_index()));
    diplomacy_refresh_elapsed_=0.;
  }

  void execute_diplomacy(const DiplomacyWorkspaceCommand &command){
    const auto &view=diplomacy_workspace_.view();
    if(!view){
      diplomacy_workspace_.set_notice("Diplomatic channels are still loading.",false);
      return;
    }
    const auto outcome=diplomacy_controller_.execute(
        session_->frame(),command.campaign_generation,command.diplomacy_revision,
        command.action,command.target_civilization_id,command.proposal_id);
    diplomacy_workspace_.set_notice(outcome.message,outcome.accepted);
    refresh_diplomacy(true);
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

  void fit_camera(int width,int height){if(galaxy_backdrop_.artwork_frame()){camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;return;}const auto &systems=session_->frame().runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void toggle_menu(){menu_=!menu_;auto &frame=session_->frame();frame.set_menu_open(menu_);if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame.clock().speed();frame.clock().set_speed(StrategicSpeed::Paused);frame.pause_tactical_for_menu();}else{frame.resume_tactical_after_menu();frame.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=session_->frame().runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());if(galaxy_backdrop_.artwork_frame())galaxy_backdrop_.set_galactic_core_discovered(session_->cache().generation,world.knowledge.is_galactic_core_discovered(world.player_civilization_id));}
  void bind_galaxy_backdrop(int width,int height){const auto &world=session_->frame().runtime().world().campaign();GalaxyBackdropCatalog view;view.campaign_generation=session_->cache().generation;view.campaign_seed=world.seed;view.system_positions.reserve(world.systems.size());for(const auto &system:world.systems)view.system_positions.push_back({system.position.x,system.position.y});if(world.core){view.galactic_core=WorldPoint{world.core->position.x,world.core->position.y};view.galactic_core_exclusion_radius=world.core->exclusion_radius;view.galactic_core_discovered=world.knowledge.is_galactic_core_discovered(world.player_civilization_id);}galaxy_backdrop_.bind(std::move(view));camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void cycle_speed(){auto &clock=session_->frame().clock();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  [[nodiscard]] std::string speed_text(){const auto &clock=session_->frame().clock();switch(clock.speed()==StrategicSpeed::Paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Fast:return "SPEED 2X";case StrategicSpeed::VeryFast:return "SPEED 3X";case StrategicSpeed::Maximum:return "SPEED 8X";default:return "SPEED 1X";}}
  void select(Point pointer,int width,int height){float best=10.f;std::optional<int> id;for(const auto &system:session_->frame().runtime().world().campaign().systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);const auto d=std::hypot(p.x-pointer.x,p.y-pointer.y);if(d<best){best=d;id=system.id;}}selected_id_=id;}
  std::unique_ptr<NativeCampaignSession> session_;
  Camera camera_;
  NativeGalaxyStarMarkerRenderer galaxy_star_markers_;
  std::unordered_set<int> known_;
  std::optional<int> selected_id_;
  NativeResearchController research_controller_;
  NativeResearchWorkspace research_workspace_;
  NativeFleetController fleet_controller_;
  NativeFleetWorkspace fleet_workspace_;
  NativeShipyardController shipyard_controller_;
  NativeShipyardWorkspace shipyard_workspace_;
  NativeConstructionController construction_controller_;
  NativeConstructionWorkspace construction_workspace_;
  NativeDiplomacyController diplomacy_controller_;
  NativeDiplomacyWorkspace diplomacy_workspace_;
  std::filesystem::path asset_root_;
  std::unordered_map<std::string,std::shared_ptr<const RgbaImage>> diplomacy_portraits_;
  std::size_t diplomacy_portrait_bytes_{};
  std::string smoke_diplomacy_unknown_,smoke_diplomacy_status_;
  NativeDiplomacyWorkspace::PortraitProvider diplomacy_portrait_provider_ =
      [this](std::string_view relative){
        static constexpr std::array<std::string_view,4> approved{
          "assets/visual/species/terran-baseline-communications-v2.png",
          "assets/visual/species/pelagic-high-pressure-communications-v2.png",
          "assets/visual/species/compact-high-gravity-communications-v2.png",
          "assets/visual/species/cryogenic-hydrocarbon-communications-v2.png"};
        if(std::ranges::find(approved,relative)==approved.end())return std::shared_ptr<const RgbaImage>{};
        const std::string key(relative);
        if(const auto found=diplomacy_portraits_.find(key);found!=diplomacy_portraits_.end())return found->second;
        const auto path=asset_root_/key;
        std::shared_ptr<const RgbaImage> decoded;
        try{decoded=decode_rgba_image(path);}
        catch(const std::exception&error){throw std::runtime_error("Diplomacy transmission artwork failed to load: "+utf8_path(path)+": "+error.what());}
        constexpr std::size_t budget=32u*1024u*1024u;
        if(diplomacy_portraits_.size()>=approved.size()||decoded->byte_size()>budget-diplomacy_portrait_bytes_)
          throw std::runtime_error("Diplomacy transmission artwork exceeded its 32 MiB cache budget.");
        diplomacy_portrait_bytes_+=decoded->byte_size();
        diplomacy_portraits_.emplace(key,decoded);return decoded;
      };
  NativeColonyController colony_controller_;
  NativeColonyWorkspace colony_workspace_;
  NativeSurfaceConstructionController surface_controller_;
  NativeSurfaceWorkspace surface_workspace_;
  NativeSettlementMissionController settlement_controller_;
  NativeSettlementWorkspace settlement_workspace_;
  std::optional<NativeColonyView> colony_entry_view_;
  NativeSystemViewController system_controller_;
  NativeSystemTravelController system_travel_controller_;
  NativeGalaxyBackdropAssets galaxy_assets_;
  NativeGalaxyBackdrop galaxy_backdrop_;
  double fitted_pixels_per_world_{.01};
  std::shared_ptr<ImagePreparationQueue> image_preparation_{std::make_shared<ImagePreparationQueue>()};
  NativePlanetDiscAssets planet_discs_;
  NativeShipArtAssets ship_art_;
  NativeSystemWorkspace system_workspace_;
  std::vector<FleetMarkerOffset> fleet_marker_offsets_;
  std::optional<NativeFleetRoutePreview> pending_fleet_preview_;
  std::optional<ResearchStamp> projected_research_stamp_;
  double research_refresh_elapsed_{};
  double fleet_refresh_elapsed_{};
  double shipyard_refresh_elapsed_{};
  double construction_refresh_elapsed_{};
  double diplomacy_refresh_elapsed_{};
  double colony_refresh_elapsed_{};
  double system_refresh_elapsed_{};
  bool last_construction_command_accepted_{};
  bool smoke_construction_started_{};
  bool smoke_construction_start_was_running_{};
  bool smoke_construction_paused_for_quote_{};
  std::optional<std::string> smoke_construction_project_id_;
  bool last_research_command_accepted_{};
  std::optional<std::string> smoke_research_node_;
  bool last_fleet_command_accepted_{};
  bool last_shipyard_command_accepted_{};
  bool smoke_shipyard_order_started_{};
  bool smoke_shipyard_start_was_running_{};
  bool smoke_shipyard_was_running_{};
  std::optional<std::string> smoke_shipyard_order_id_;
  FleetRouteEffectStats last_route_stats_{};
  FleetRouteEffectStats smoke_route_stats_{};
  int smoke_shipyard_art_rows_{};
  int smoke_fleet_art_rows_{};
  std::size_t smoke_ship_art_decoded_{};
  std::size_t smoke_ship_art_cached_{};
  std::size_t smoke_ship_art_bytes_{};
  std::optional<int> smoke_fleet_id_;
  std::optional<int> smoke_fleet_destination_;
  bool smoke_system_entered_{},smoke_system_hit_{},smoke_system_panned_{},smoke_system_zoomed_{},smoke_system_reset_{},smoke_system_back_{},smoke_system_pause_retained_{},smoke_system_speed_retained_{},smoke_system_gesture_cleared_{};
  double smoke_system_day_{};
  std::optional<int> smoke_system_travel_fleet_id_,smoke_system_travel_system_id_,smoke_system_travel_destination_id_;
  int smoke_system_travel_mission_revision_{};std::size_t smoke_system_travel_lane_count_{};
  float smoke_system_travel_before_x_{},smoke_system_travel_before_y_{},smoke_system_travel_after_x_{},smoke_system_travel_after_y_{};double smoke_system_travel_before_day_{},smoke_system_travel_after_day_{};
  bool smoke_system_travel_reload_{},smoke_system_travel_selected_{},smoke_system_travel_canonical_moved_{},smoke_system_travel_rendered_moved_{},smoke_system_travel_paused_stable_{},smoke_system_travel_pause_retained_{},smoke_system_travel_known_opened_{},smoke_system_travel_unknown_denied_{},smoke_system_travel_knowledge_unchanged_{},smoke_system_travel_lanes_connected_{};
  bool smoke_colony_reload_{},smoke_colony_selected_{},smoke_colony_opened_{},smoke_colony_back_{},smoke_colony_pause_retained_{},smoke_colony_speed_retained_{};
  double smoke_colony_day_{};
  bool smoke_settlement_mode_{},smoke_settlement_reload_{},smoke_settlement_selected_{},smoke_settlement_previewed_{},smoke_settlement_accepted_{};
  bool smoke_settlement_cancelled_{},smoke_settlement_cancel_no_charge_{},smoke_settlement_requires_authorization_{},smoke_settlement_no_instant_colony_{};
  std::optional<int> smoke_settlement_fleet_id_,smoke_settlement_system_id_,smoke_settlement_body_id_;
  NativeSettlementMissionKind smoke_settlement_kind_{NativeSettlementMissionKind::Colony};
  std::size_t smoke_settlement_colonies_before_{};
  int smoke_settlement_revision_{};double smoke_settlement_before_day_{},smoke_settlement_saved_day_{},smoke_settlement_progress_{},smoke_settlement_authorization_{},smoke_settlement_treasury_before_{},smoke_settlement_treasury_after_{};
  bool smoke_surface_mode_{},smoke_surface_reload_{},smoke_surface_palette_selected_{},smoke_surface_ghost_previewed_{},smoke_surface_placement_cancelled_{},smoke_surface_cancel_no_change_{},smoke_surface_placement_confirmed_{},smoke_surface_removal_previewed_{},smoke_surface_removal_confirmed_{},smoke_surface_refund_exact_{},smoke_surface_persisted_site_{};
  int smoke_surface_system_id_{},smoke_surface_body_id_{},smoke_surface_colony_id_{};std::optional<int> smoke_surface_site_id_;std::string smoke_surface_type_id_;std::size_t smoke_surface_site_count_before_{},smoke_surface_site_count_saved_{};float smoke_surface_x_{},smoke_surface_z_{},smoke_surface_rotation_{};double smoke_surface_authorization_{},smoke_surface_refund_{},smoke_surface_treasury_before_{},smoke_surface_treasury_after_place_{},smoke_surface_treasury_after_refund_{},smoke_surface_treasury_saved_{},smoke_surface_progress_{},smoke_surface_before_day_{},smoke_surface_saved_day_{};
  bool menu_{};bool smoke_save_pending_{};Point pointer_{};PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
  bool smoke_galaxy_mode_{},smoke_galaxy_reload_{},smoke_galaxy_paused_{},smoke_galaxy_wheel_input_{},smoke_galaxy_system_entry_{};
  double smoke_galaxy_day_{},smoke_galaxy_fitted_scale_{},smoke_galaxy_regional_scale_{};
  GalaxyArtSceneEvidence smoke_galaxy_overview_{},smoke_galaxy_regional_{},smoke_galaxy_system_{};
};
}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
  try{
    const auto options=parse_options(argc,argv);
    const auto asset_root=std::filesystem::absolute(options.asset_root);
    const auto startup_begin=std::chrono::steady_clock::now();
    Window window("Stellar Continuum - Native Galaxy",options.window_width,
                  options.window_height,!options.windowed,
                  asset_root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");
    std::unique_ptr<NativeCampaignSession> session;
    StartupEntryEvidence startup_evidence;
    std::optional<std::filesystem::path> generated_save_path,setup_screenshot,loading_screenshot;
    if(options.new_game_smoke){
      if(!std::filesystem::is_regular_file(options.save_path))throw std::invalid_argument("--new-game-smoke requires a preexisting save-path anchor.");
      setup_screenshot=sidecar_path(*options.smoke_screenshot,L"-setup");
      loading_screenshot=sidecar_path(*options.smoke_screenshot,L"-loading");
      StartupEntryAutomation automation{std::to_string(options.seed),"pelagic_high_pressure",250,*setup_screenshot,*loading_screenshot};
      auto result=run_native_startup_entry(window,{{asset_root/"Data/research/v1",asset_root/"Data/astronomy/hyg-nearby-500-v1.json",options.save_path,STELLAR_GAME_VERSION},asset_root,utc_timestamp},&automation);
      if(result.exit_requested||!result.session)throw std::runtime_error("Automated new campaign startup did not activate a session.");
      startup_evidence=std::move(result.evidence);generated_save_path=result.session->save_path();
      if(*generated_save_path==options.save_path)throw std::runtime_error("New campaign startup overwrote the requested save anchor.");
      session=std::move(result.session);
    }else if(options.load||options.smoke_screenshot){session=make_session(options);}
    else{
      auto result=run_native_startup_entry(window,{{asset_root/"Data/research/v1",asset_root/"Data/astronomy/hyg-nearby-500-v1.json",options.save_path,STELLAR_GAME_VERSION},asset_root,utc_timestamp});
      if(result.exit_requested)return 0;
      if(!result.session)throw std::runtime_error("Startup ended without a campaign session.");
      session=std::move(result.session);
    }
    NativeCampaign campaign(std::move(session),window.drawable_width(),window.drawable_height(),options.asset_root,
                             [&window](const Text &label){return window.measure_text(label);});
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
      else if(options.construction_smoke)
        campaign.prepare_construction_smoke(window.drawable_width(),
                                            window.drawable_height());
      else if(options.system_smoke)
        campaign.prepare_system_smoke(window.drawable_width(),
                                      window.drawable_height());
      else if(options.system_travel_smoke||options.system_travel_reload_smoke)
        campaign.prepare_system_travel_smoke(window.drawable_width(),
                                             window.drawable_height(),options.system_travel_reload_smoke);
      else if(options.colony_smoke||options.colony_reload_smoke)
        campaign.prepare_colony_smoke(window.drawable_width(),
                                       window.drawable_height(),options.colony_reload_smoke);
      else if(options.settlement_smoke||options.settlement_reload_smoke)
        campaign.prepare_settlement_smoke(window.drawable_width(),
                                           window.drawable_height(),options.settlement_reload_smoke);
      else if(options.surface_smoke||options.surface_reload_smoke)
        campaign.prepare_surface_smoke(window.drawable_width(),
                                       window.drawable_height(),options.surface_reload_smoke);
      else if(options.galaxy_art_smoke)
        campaign.prepare_galaxy_art_smoke(window.drawable_width(),
                                          window.drawable_height(),options.load);
      else if(options.diplomacy_smoke||options.diplomacy_reload_smoke)
        campaign.prepare_diplomacy_smoke(window.drawable_width(),window.drawable_height(),options.diplomacy_reload_smoke);
      else if(options.ship_art_smoke)
        campaign.prepare_ship_art_smoke(window.drawable_width(),
                                        window.drawable_height());
      else
        campaign.prepare_smoke_ui();
    }
    const auto startup_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-startup_begin).count();
    auto prior=std::chrono::steady_clock::now();int frames=0;std::vector<double> frame_ms;bool discard_elapsed{};
    // Samples are retained only by the bounded smoke run. Normal play keeps
    // no history; rendering time includes submission and presentation wait.
    std::vector<double> update_ms,scene_ms,render_present_ms;
    std::unique_ptr<SmokeTimingSummary> smoke_timing;
    if(options.smoke_screenshot)smoke_timing=std::make_unique<SmokeTimingSummary>();
    const auto steady_end_frame=120+options.profile_frames.value_or(0);
    auto capture_frame=steady_end_frame;
    int artwork_wait_frames{},artwork_pending_frames{};
    double artwork_prepare_max_ms{};
    std::optional<std::chrono::steady_clock::time_point> artwork_pending_since;
    std::unique_ptr<SmokeSteadyProfile> steady_profile;
    if(options.profile_frames)steady_profile=std::make_unique<SmokeSteadyProfile>(*options.profile_frames);
    FrameTiming draw_timing;
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
      const bool valid_interval=!discard_elapsed;
      if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);
      discard_elapsed=false;
      const auto update_begin=std::chrono::steady_clock::now();
      if(!campaign.update(input,input.drawable_width,input.drawable_height,
                          elapsed))break;
      const auto update_end=std::chrono::steady_clock::now();
      window.set_text_input(campaign.wants_text_input());
      if(options.smoke_screenshot){
        ++frames;
        if((options.research_smoke||options.fleet_smoke||options.shipyard_smoke||options.construction_smoke||options.system_smoke||options.system_travel_smoke||options.system_travel_reload_smoke||options.colony_smoke||options.colony_reload_smoke||options.settlement_smoke||options.settlement_reload_smoke||options.surface_smoke||options.surface_reload_smoke||options.galaxy_art_smoke||options.ship_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)&&frames==60)
          campaign.request_smoke_save();
      }
      std::optional<std::filesystem::path> screenshot;
      if(options.smoke_screenshot){
        if(options.galaxy_art_smoke){
          if(frames==capture_frame)screenshot=options.smoke_screenshot;
          else if(frames==capture_frame+1)screenshot=sidecar_path(*options.smoke_screenshot,L"-regional");
          else if(frames==capture_frame+2)screenshot=sidecar_path(*options.smoke_screenshot,L"-system");
        }else if(options.diplomacy_smoke||options.diplomacy_reload_smoke){
          if(frames==capture_frame)screenshot=options.smoke_screenshot;
          else if(frames==capture_frame+1)screenshot=sidecar_path(*options.smoke_screenshot,L"-unknown");
        }else if(options.ship_art_smoke){
          if(frames==capture_frame)screenshot=options.smoke_screenshot;
          else if(frames==capture_frame+1)screenshot=sidecar_path(*options.smoke_screenshot,L"-map");
        }else if(frames>=capture_frame)screenshot=options.smoke_screenshot;
      }
      const auto scene_begin=std::chrono::steady_clock::now();
      const auto scene=campaign.scene(input.drawable_width,input.drawable_height);
      const auto scene_end=std::chrono::steady_clock::now();
      const bool artwork_ready=campaign.artwork_ready();
      const bool waiting_for_artwork=options.smoke_screenshot&&frames>=capture_frame&&!artwork_ready;
      if(options.smoke_screenshot){
        if(!artwork_ready){++artwork_pending_frames;if(!artwork_pending_since)artwork_pending_since=scene_begin;}
        else if(artwork_pending_since){artwork_prepare_max_ms=std::max(artwork_prepare_max_ms,std::chrono::duration<double,std::milli>(scene_end-*artwork_pending_since).count());artwork_pending_since.reset();}
      }
      if(waiting_for_artwork){screenshot.reset();if(++artwork_wait_frames>600)throw std::runtime_error("Map artwork did not finish preparation before capture.");}
      window.draw(scene,screenshot,steady_profile?&draw_timing:nullptr);
      const auto render_end=std::chrono::steady_clock::now();
      if(smoke_timing)
        smoke_timing->observe(frames,screenshot.has_value()||frames>=capture_frame,
          std::chrono::duration<double,std::milli>(update_end-update_begin).count(),
          std::chrono::duration<double,std::milli>(scene_end-scene_begin).count(),
          std::chrono::duration<double,std::milli>(render_end-scene_end).count());
      if(steady_profile&&frames>=120&&frames<steady_end_frame&&valid_interval)
        steady_profile->observe(elapsed*1000.,
          std::chrono::duration<double,std::milli>(update_end-update_begin).count(),
          std::chrono::duration<double,std::milli>(scene_end-scene_begin).count(),draw_timing);
      if(options.smoke_screenshot&&!screenshot){
        // GPU readback/file writes deliberately stay out of phase samples.
        update_ms.push_back(std::chrono::duration<double,std::milli>(update_end-update_begin).count());
        scene_ms.push_back(std::chrono::duration<double,std::milli>(scene_end-scene_begin).count());
        render_present_ms.push_back(std::chrono::duration<double,std::milli>(render_end-scene_end).count());
      }
      if(!waiting_for_artwork&&options.galaxy_art_smoke){
        if(frames==capture_frame){campaign.capture_galaxy_overview(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_regional(input.drawable_width,input.drawable_height);}
        else if(frames==capture_frame+1){campaign.capture_galaxy_regional(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_system(input.drawable_width,input.drawable_height);}
        else if(frames==capture_frame+2)campaign.capture_galaxy_system(input.drawable_width,input.drawable_height);
      }
      if(!waiting_for_artwork&&options.ship_art_smoke){
        if(frames==capture_frame)campaign.capture_ship_art_shipyard();
        else if(frames==capture_frame+1)campaign.capture_ship_art_map();
      }
      if(!waiting_for_artwork&&(options.diplomacy_smoke||options.diplomacy_reload_smoke)&&frames==capture_frame)
        campaign.capture_diplomacy_unknown(input.drawable_width,input.drawable_height);
      const bool capture=!waiting_for_artwork&&options.smoke_screenshot&&(options.galaxy_art_smoke?frames>=capture_frame+3:(options.ship_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)?frames>=capture_frame+2:frames>=capture_frame);
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
                 <<" artwork_pending_frames="<<artwork_pending_frames<<" artwork_prepare_max_ms="<<artwork_prepare_max_ms
                 <<" artwork_capture_wait_frames="<<artwork_wait_frames
                 <<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())
                 <<" frame_p95_ms="<<p95<<" image_uploads="<<window.image_upload_count()<<" save=ok screenshot="
                 <<utf8_path(*options.smoke_screenshot);
        const auto print_phase=[&](const char* name,std::vector<double>&samples){
          if(samples.empty())throw std::runtime_error("Native smoke has no frame phase samples.");
          std::ranges::sort(samples);
          const auto mean=std::accumulate(samples.begin(),samples.end(),0.)/static_cast<double>(samples.size());
          const auto percentile=samples[static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size())*.95))-1];
          std::cout<<' '<<name<<"_mean_ms="<<mean<<' '<<name<<"_p95_ms="<<percentile;
        };
        std::cout<<" phase_samples="<<update_ms.size();
        print_phase("update",update_ms);print_phase("scene",scene_ms);print_phase("render_present",render_present_ms);
        smoke_timing->write(std::cout);
        if(steady_profile)steady_profile->write(std::cout);
        if(options.research_smoke)
          std::cout<<" research="<<campaign.research_smoke_status();
        if(options.fleet_smoke)
          std::cout<<" fleet="<<campaign.fleet_smoke_status();
        if(options.shipyard_smoke)
          std::cout<<" shipyard="<<campaign.shipyard_smoke_status();
        if(options.construction_smoke)
          std::cout<<" construction="<<campaign.construction_smoke_status();
        if(options.system_smoke)
          std::cout<<" system="<<campaign.system_smoke_status();
        if(options.system_travel_smoke||options.system_travel_reload_smoke)
          std::cout<<" system_travel="<<campaign.system_travel_smoke_status();
        if(options.colony_smoke||options.colony_reload_smoke)
          std::cout<<" colony="<<campaign.colony_smoke_status();
        if(options.settlement_smoke||options.settlement_reload_smoke)
          std::cout<<" settlement="<<campaign.settlement_smoke_status();
        if(options.surface_smoke||options.surface_reload_smoke)
          std::cout<<" surface="<<campaign.surface_smoke_status();
        if(options.galaxy_art_smoke)
          std::cout<<" galaxy_art="<<campaign.galaxy_art_smoke_status();
        if(options.ship_art_smoke)
          std::cout<<" ship_art="<<campaign.ship_art_smoke_status();
        if(options.diplomacy_smoke||options.diplomacy_reload_smoke)
          std::cout<<" diplomacy="<<campaign.diplomacy_smoke_status();
        if(options.new_game_smoke){
          std::cout<<" new_game={\"mode\":\"fresh\",\"entry_opened\":"<<(startup_evidence.entry_opened?"true":"false")
            <<",\"setup_opened\":"<<(startup_evidence.setup_opened?"true":"false")<<",\"species_selected\":"<<(startup_evidence.species_selected?"true":"false")
            <<",\"size_selected\":"<<(startup_evidence.size_selected?"true":"false")<<",\"seed_entered\":"<<(startup_evidence.seed_entered?"true":"false")
            <<",\"create_requested\":"<<(startup_evidence.create_requested?"true":"false")<<",\"indeterminate_observed\":"<<(startup_evidence.indeterminate_observed?"true":"false")
            <<",\"activated\":true,\"saved\":true,\"species_id\":"<<json_string(campaign.player_species_id())<<",\"system_count\":"<<campaign.system_count()
            <<",\"seed\":"<<json_string(std::to_string(options.seed))<<",\"requested_save_path\":"<<json_string(utf8_path(options.save_path))
            <<",\"generated_save_path\":"<<json_string(utf8_path(*generated_save_path))<<",\"unique_slot\":true,\"setup_screenshot\":"<<json_string(utf8_path(*setup_screenshot))
            <<",\"loading_screenshot\":"<<json_string(utf8_path(*loading_screenshot))<<",\"statuses\":[";
          for(std::size_t i=0;i<startup_evidence.displayed_statuses.size();++i){if(i)std::cout<<',';std::cout<<json_string(startup_evidence.displayed_statuses[i]);}
          std::cout<<"]}";
        }
        std::cout<<'\n';
        break;
      }
      if(waiting_for_artwork)++capture_frame;
    }
    return 0;
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
