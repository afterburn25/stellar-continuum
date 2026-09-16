#include "map_camera.hpp"
#include "map_interaction.hpp"
#include "native_audio_device.hpp"
#include "native_audio_settings.hpp"
#include "native_battle_workspace.hpp"
#include "native_developer_tools.hpp"
#include "native_development_menu.hpp"
#include "native_notifications.hpp"
#include "native_support.hpp"
#include "native_galaxy_star_markers.hpp"
#include "native_inspection.hpp"
#include "native_economy.hpp"
#include "native_logistics.hpp"
#include "native_missions.hpp"
#include "native_overview.hpp"
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
#include "native_voice.hpp"
#include "native_voice_bridge.hpp"
#include "native_voice_playback.hpp"
#include "native_video_settings.hpp"
#include "native_voice_settings.hpp"
#include "native_startup_entry.hpp"
#include "native_galaxy_backdrop.hpp"
#include "native_territory_overlay.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/build_version.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign_save.hpp>
#include <stellar/core/developer_campaign_session.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <SDL3/SDL.h>

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
namespace native_audio = stellar::native_audio;
namespace native_audio_settings = stellar::native_audio_settings;
namespace native_battle_ui = stellar::native_battle_ui;
namespace native_developer = stellar::native_developer;
namespace native_development = stellar::native_development;
namespace native_inspection = stellar::native_inspection;
namespace native_economy = stellar::native_economy;
namespace native_logistics = stellar::native_logistics;
namespace native_missions = stellar::native_missions;
namespace native_notifications = stellar::native_notifications;
namespace native_overview = stellar::native_overview;
namespace native_support = stellar::native_support;
namespace native_video_settings = stellar::native_video_settings;
namespace native_voice = stellar::native_voice;
namespace native_voice_settings = stellar::native_voice_settings;
using namespace stellar::native_system;
using namespace stellar::native_system_travel;
using namespace stellar::native_system_ui;
using namespace stellar::native_startup_ui;
using namespace stellar::native_galaxy_ui;
using namespace stellar::native_territory;
using namespace stellar::native_ship_ui;

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
  bool new_game_restart_smoke{};
  bool galaxy_art_smoke{};
  bool ship_art_smoke{};
  bool diplomacy_smoke{};
  bool battle_smoke{};
  bool audio_smoke{};
  bool notification_smoke{};
  bool logistics_smoke{};
  bool economy_smoke{};
  bool developer_smoke{};
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
    else if(arg==L"--new-game-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.windowed=true;}
    else if(arg==L"--new-game-restart-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_restart_smoke=true;result.windowed=true;}
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
    else if(arg==L"--battle-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.battle_smoke=true;result.windowed=true;}
    else if(arg==L"--audio-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.audio_smoke=true;result.windowed=true;}
    else if(arg==L"--notification-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.notification_smoke=true;result.windowed=true;}
    else if(arg==L"--logistics-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.logistics_smoke=true;result.windowed=true;}
    else if(arg==L"--economy-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.economy_smoke=true;result.windowed=true;}
    else if(arg==L"--developer-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.developer_smoke=true;result.windowed=true;}
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
    else if(arg=="--new-game-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.windowed=true;}
    else if(arg=="--new-game-restart-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_restart_smoke=true;result.windowed=true;}
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
    else if(arg=="--battle-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.battle_smoke=true;result.windowed=true;}
    else if(arg=="--audio-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.audio_smoke=true;result.windowed=true;}
    else if(arg=="--notification-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.notification_smoke=true;result.windowed=true;}
    else if(arg=="--logistics-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.logistics_smoke=true;result.windowed=true;}
    else if(arg=="--economy-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.economy_smoke=true;result.windowed=true;}
    else if(arg=="--developer-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.developer_smoke=true;result.windowed=true;}
#endif
    else throw std::invalid_argument("Unknown or incomplete native client option.");
  }
  if(result.smoke_screenshot&&!result.save_path_overridden)throw std::invalid_argument("--smoke requires an isolated --save-path.");
  if(static_cast<int>(result.research_smoke)+static_cast<int>(result.fleet_smoke)+static_cast<int>(result.shipyard_smoke)+static_cast<int>(result.construction_smoke)+static_cast<int>(result.system_smoke)+static_cast<int>(result.system_travel_smoke)+static_cast<int>(result.system_travel_reload_smoke)+static_cast<int>(result.colony_smoke)+static_cast<int>(result.colony_reload_smoke)+static_cast<int>(result.settlement_smoke)+static_cast<int>(result.settlement_reload_smoke)+static_cast<int>(result.surface_smoke)+static_cast<int>(result.surface_reload_smoke)+static_cast<int>(result.new_game_smoke)+static_cast<int>(result.new_game_restart_smoke)+static_cast<int>(result.galaxy_art_smoke)+static_cast<int>(result.ship_art_smoke)+static_cast<int>(result.diplomacy_smoke)+static_cast<int>(result.battle_smoke)+static_cast<int>(result.audio_smoke)+static_cast<int>(result.notification_smoke)+static_cast<int>(result.logistics_smoke)+static_cast<int>(result.economy_smoke)+static_cast<int>(result.developer_smoke)>1)throw std::invalid_argument("Choose one native graphical smoke mode.");
  if(result.new_game_smoke&&result.load)throw std::invalid_argument("--new-game-smoke cannot be combined with --load.");
  if(result.fleet_smoke&&!result.load)throw std::invalid_argument("--fleet-smoke requires --load with a player campaign fixture.");
  if(result.ship_art_smoke&&!result.load)throw std::invalid_argument("--ship-art-smoke requires --load with a player campaign fixture.");
  if(result.diplomacy_smoke&&!result.load)throw std::invalid_argument("--diplomacy-smoke requires --load with a diplomacy-bearing player campaign fixture.");
  if(result.battle_smoke&&!result.load)throw std::invalid_argument("--battle-smoke requires --load with an active tactical encounter save.");
  if(result.notification_smoke&&!result.load)throw std::invalid_argument("--notification-smoke requires --load with a diplomacy-bearing player campaign fixture.");
  if(result.logistics_smoke&&!result.load)throw std::invalid_argument("--logistics-smoke requires --load with a player campaign fixture.");
  if(result.developer_smoke&&!result.load)throw std::invalid_argument("--developer-smoke requires --load with a player campaign fixture.");
  if(result.economy_smoke&&!result.load)throw std::invalid_argument("--economy-smoke requires --load with a player campaign fixture.");
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

[[nodiscard]] PlayerCampaignLoadOrigin developer_source_origin(
    const DeveloperCampaignSource source) noexcept {
  switch (source) {
    case DeveloperCampaignSource::RecoveredFromBackup:
    case DeveloperCampaignSource::ImportedLegacyDemoBackup:
      return PlayerCampaignLoadOrigin::Backup;
    default:
      return PlayerCampaignLoadOrigin::Primary;
  }
}

// Source: Main.GameModes UiSwitchToDeveloperMode/UiSwitchToPlayerMode — the
// target mode loads-or-creates against its own save slot; the caller already
// checkpointed the outgoing campaign.
[[nodiscard]] std::unique_ptr<NativeCampaignSession> build_switched_session(
    const bool to_developer, const std::filesystem::path &research_root,
    const std::filesystem::path &catalog_path,
    const std::filesystem::path &player_save_path,
    const std::optional<std::int64_t> new_developer_seed = std::nullopt) {
  const PlayerCampaignRuntimeFactory make_runtime = [&research_root] {
    return load_adaptive_research_strategic_runtime(research_root);
  };
  if (to_developer) {
    const auto save_path =
        developer_save_path_beside(player_save_path);
    NativeCampaignSessionDependencies dependencies;
    dependencies.save_writer = write_prepared_developer_campaign;
    dependencies.loader =
        [](const std::filesystem::path &path,
           const PlayerCampaignRuntimeFactory &factory,
           const std::function<
               void(const PlayerCampaignRestorationProgress &)>
               &progress) {
          auto bootstrap =
              load_existing_developer_campaign(path, factory, progress);
          if (!bootstrap.loaded)
            throw PlayerCampaignLoadError(
                "No Developer campaign save is available.",
                std::move(bootstrap.prior_attempts));
          return LoadedPlayerCampaignV17{
              std::move(*bootstrap.loaded),
              developer_source_origin(bootstrap.source),
              std::move(bootstrap.requested_path),
              std::move(bootstrap.loaded_path),
              std::move(bootstrap.prior_attempts)};
        };
    const auto catalog = load_nearby_catalog(catalog_path);
    // Reference UiNewDeveloperCampaign: a caller seed always spawns a fresh
    // Developer world; the previous Developer save is kept as its .bak.
    if (new_developer_seed)
      return NativeCampaignSession::create_fresh(
          IntegratedAdaptiveCampaignRuntime::create_fresh(
              make_runtime(),
              create_developer_campaign(
                  *new_developer_seed, catalog,
                  {utc_timestamp(), 250, 6, 1, "terran_baseline"})),
          research_root, save_path, STELLAR_GAME_VERSION,
          std::move(dependencies));
    auto bootstrap = load_or_create_developer_campaign(
        save_path, playable_demo_seed, catalog,
        {utc_timestamp(), 250, 6, 1, "terran_baseline"}, make_runtime);
    if (bootstrap.loaded)
      return NativeCampaignSession::create_loaded(
          {std::move(*bootstrap.loaded),
           developer_source_origin(bootstrap.source),
           std::move(bootstrap.requested_path),
           std::move(bootstrap.loaded_path),
           std::move(bootstrap.prior_attempts)},
          research_root, save_path, STELLAR_GAME_VERSION,
          std::move(dependencies));
    return NativeCampaignSession::create_fresh(
        IntegratedAdaptiveCampaignRuntime::create_fresh(
            make_runtime(), std::move(*bootstrap.fresh)),
        research_root, save_path, STELLAR_GAME_VERSION,
        std::move(dependencies));
  }
  // Player target: load the player slot, or generate a fresh campaign when no
  // player save exists (reference LoadOrCreate with a wall-clock seed).
  {
    std::error_code error{};
    auto backup = player_save_path;
    backup += ".bak";
    if (std::filesystem::is_regular_file(player_save_path, error) ||
        std::filesystem::is_regular_file(backup, error))
      return NativeCampaignSession::create_loaded(
          load_existing_player_campaign_v17(player_save_path,
                                                  make_runtime),
          research_root, player_save_path, STELLAR_GAME_VERSION);
    const auto fallback_seed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    return NativeCampaignSession::create_fresh(
        IntegratedAdaptiveCampaignRuntime::create_fresh(
            make_runtime(),
            seed_persistable_fresh_campaign(
                fallback_seed, load_nearby_catalog(catalog_path),
                {utc_timestamp(), 500, 6, 1, "terran_baseline"})),
        research_root, player_save_path, STELLAR_GAME_VERSION);
  }
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

// --developer-smoke evidence accumulates across the per-session
// NativeCampaign rebuilds that Player/Developer mode switching performs.
struct DeveloperSmokeEvidence {
  bool mode{}, demo{}, tools{}, command{}, save{}, fresh{}, backup{};
};

class NativeCampaign final {
 public:
  NativeCampaign(std::unique_ptr<NativeCampaignSession> session,Window &window,int width,int height,
                 const std::filesystem::path &asset_root,SystemTextMeasurer text_measurer,
                 std::filesystem::path player_save_path={})
      : session_(std::move(session)),
        player_save_path_(std::move(player_save_path)),
        window_(&window),
        galaxy_assets_(std::filesystem::absolute(asset_root)),
        galaxy_backdrop_(galaxy_assets_),
        planet_discs_(std::filesystem::absolute(asset_root)/"assets/visual"),
        ship_art_(std::filesystem::absolute(asset_root)),
        asset_root_(std::filesystem::absolute(asset_root)),
        audio_mixer_(session_->save_path().parent_path()/"audio-settings.json"),
        system_workspace_([this](const SystemBodyAppearance &appearance){return planet_discs_.image(appearance);},std::move(text_measurer)) {
    system_workspace_.set_celestial_asset_root(asset_root_/"assets/visual");
    if(audio_mixer_.load_assets(asset_root_))audio_device_.open(audio_mixer_);
    audio_mixer_.complete_startup_loading();
    initialize_voice();
    // VideoSettingsService.LoadAndApply port: persisted display settings apply
    // before the first presented frame.
    video_settings_path_=session_->save_path().parent_path()/"video-settings.json";
    video_settings_=native_video_settings::NativeVideoSettings::load(video_settings_path_);
    apply_video_settings(video_settings_);
    refresh_knowledge();
    fit_camera(width,height);
    bind_galaxy_backdrop(width,height);
    refresh_fleets(true);
  }

  // Loads the reviewed voice catalogue from Data/voice_profiles, binds the
  // router → playback → mixer dialogue chain, and seeds the event bridge.
  // Missing or malformed voice data disables voice without failing the
  // campaign, matching the reference's backend-unavailable fallback.
  void initialize_voice(){
    const auto voice_dir=asset_root_/"Data/voice_profiles";
    const auto user_dir=session_->save_path().parent_path();
    try{
      if(!std::filesystem::is_regular_file(voice_dir/"events.json")||
         !std::filesystem::is_regular_file(voice_dir/"human.json")||
         !std::filesystem::is_regular_file(voice_dir/"roles.json"))
        return;
      voice_profiles_=native_voice::NativeVoiceProfileRegistry::load(voice_dir/"human.json");
      voice_resolver_.emplace(native_voice::NativeCharacterVoiceResolver::load(&voice_profiles_,voice_dir/"roles.json"));
      // ResolveCurrentVoiceCharacter port: only the player's own leadership
      // roster may name speakers; foreign offices are never consulted.
      voice_resolver_->set_current_character(
        [this](const native_voice::NativeVoiceSpeakerContext &context)
          ->std::optional<native_voice::NativeVoiceCharacter>{
          if(!session_)return std::nullopt;
          const auto &campaign=session_->frame().runtime().world().campaign();
          const auto civilization=std::ranges::find(campaign.civilizations,context.source_civilization_id,&Civilization::id);
          if(civilization==campaign.civilizations.end()||civilization->id!=campaign.player_civilization_id)return std::nullopt;
          const std::string office=
            context.role==native_voice::VoiceSpeakerRole::AlienDiplomat?"Diplomat":
            context.role==native_voice::VoiceSpeakerRole::AlienScientist?"ChiefScientist":
            context.role==native_voice::VoiceSpeakerRole::AlienCommander?"FleetCommander":
            std::string(native_voice::speaker_role_name(context.role));
          const auto holder=std::ranges::find_if(civilization->leadership,[&](const auto &entry){
            return context.exact_character_id?entry.character.id==*context.exact_character_id:entry.office==office;});
          if(holder==civilization->leadership.end())return std::nullopt;
          return native_voice::NativeVoiceCharacter{holder->character.id,holder->character.display_name,holder->office,holder->character.voice_profile_id,holder->character.portrait};
        });
      voice_router_.emplace(native_voice::NativeVoiceRouter::from_file(voice_dir/"events.json",
        [this](native_voice::NativeSpeechRequest request){
          if(voice_playback_)voice_playback_->speak(std::move(request));
        },&*voice_resolver_));
      voice_settings_path_=user_dir/"voice-settings.json";
      voice_settings_=native_voice::NativeVoiceSettings::load(voice_settings_path_);
      voice_cache_.emplace(user_dir/"voice-cache");
      voice_playback_.emplace(voice_settings_,&voice_profiles_,&*voice_resolver_,&*voice_cache_);
      voice_playback_->attach_backend(native_voice::create_offline_speech_backend());
      voice_playback_->bind(
        [](const std::filesystem::path &path){
          auto decoded=native_audio::decode_audio_file(path);
          if(!decoded)return native_voice::NativeVoicePlayback::Stream{};
          return native_voice::NativeVoicePlayback::Stream(
            std::make_shared<native_audio::PcmData>(std::move(*decoded)));
        },
        [this](native_voice::NativeVoicePlayback::Stream stream,double){
          audio_mixer_.play_dialogue(std::move(stream));
        },
        [this]{audio_mixer_.stop_dialogue();});
      audio_mixer_.set_dialogue_volume(voice_settings_.volume);
      voice_bridge_.emplace(*voice_router_);
      voice_bridge_->reset(session_->frame().runtime());
    }catch(...){
      voice_bridge_.reset();voice_playback_.reset();voice_cache_.reset();
      voice_router_.reset();voice_resolver_.reset();
    }
  }
  void prepare_smoke_ui(){if(!menu_)toggle_menu();smoke_save_pending_=true;}
  // F8 / SUPPORT BUNDLE parity with the reference UiExportDiagnostics: a
  // store-format ZIP of the session log, system info and campaign save.
  void enable_support_log(native_support::NativeSupportLog::SystemInfo info){
    support_log_.emplace(session_->save_path().parent_path(),
                         std::move(info));
  }
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
  [[nodiscard]] std::string territory_smoke_status()const{
    std::ostringstream out;
    const auto *projection=territory_overlay_.projection();
    if(!projection){out<<"{\"valid\":false}";return out.str();}
    std::size_t contour_points=0,fill_runs=0;
    for(const auto &region:projection->territories){
      fill_runs+=region.fill_runs.size();
      for(const auto &contour:region.contours)contour_points+=contour.size();
    }
    out<<"{\"valid\":true,\"regions\":"<<projection->territories.size()
       <<",\"claims\":"<<projection->claims.size()
       <<",\"fill_runs\":"<<fill_runs
       <<",\"contour_points\":"<<contour_points
       <<",\"fill_images\":"<<projection->territories.size()
       <<",\"fog_texels\":"<<projection->fog.alpha.size()
       <<",\"unexplored\":"<<projection->unexplored_system_ids.size()
       <<"}";
    return out.str();
  }

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
    const auto focus_target=std::ranges::find_if(spatial.bodies,[&](const SystemSpatialBodyMarker&marker){return marker.body_id!=earth_body_id;});
    if(focus_target==spatial.bodies.end())throw std::runtime_error("System smoke lacks a second body for the focus check.");
    const auto target_screen=system_workspace_.viewport()->world_to_screen(focus_target->offset_x,focus_target->offset_y);
    click({target_screen.x,target_screen.y});
    if(system_workspace_.selected_body_id()!=focus_target->body_id)throw std::runtime_error("System smoke could not select its focus target.");
    click({system_layout.colony_action.x+system_layout.colony_action.width*.5f,system_layout.colony_action.y+system_layout.colony_action.height*.5f});
    const auto focused=system_workspace_.viewport()->world_to_screen(focus_target->offset_x,focus_target->offset_y);
    smoke_system_focused_=std::abs(focused.x-(system_layout.world_field.x+system_layout.world_field.width*.5f))<4.f&&std::abs(focused.y-(system_layout.world_field.y+system_layout.world_field.height*.5f))<4.f;
    if(!smoke_system_focused_)throw std::runtime_error("System smoke FOCUS PLANET did not center its body.");
    if(!system_workspace_.select_body(earth_body_id))throw std::runtime_error("System smoke could not restore its Earth selection.");
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
    click(surface_workspace_.viewport().world_to_screen(final_site->x,final_site->z,layout.terrain));
    if(surface_workspace_.selected_building_id()!=std::optional<int>{final_site->building_id})throw std::runtime_error("Surface smoke could not reselect its unfinished site.");
    click(center(layout.priority));
    smoke_surface_managed_=surface_workspace_.notice().find("Complete construction")!=std::string::npos&&!surface_workspace_.view()->construction_sites.empty()&&std::ranges::none_of(surface_workspace_.view()->construction_sites,[](const auto&site){return site.prioritized;});
    if(!smoke_surface_palette_selected_||!smoke_surface_ghost_previewed_||!smoke_surface_placement_cancelled_||!smoke_surface_cancel_no_change_||!smoke_surface_placement_confirmed_||!smoke_surface_removal_previewed_||!smoke_surface_removal_confirmed_||!smoke_surface_refund_exact_||smoke_surface_progress_<=0.||!smoke_surface_managed_)throw std::runtime_error("Surface smoke did not prove its canonical placement/cancellation loop.");
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

  [[nodiscard]] bool send_key(std::uint32_t key,int width,int height){
    InputSnapshot input;
    input.drawable_width=width;
    input.drawable_height=height;
    InputEvent event{};
    event.type=InputEventType::KeyPressed;
    event.key=key;
    input.events.push_back(event);
    return update(input,width,height,0.,false);
  }
  [[nodiscard]] bool shortcut_status_reported()const noexcept{
    return session_->notice().kind==SessionNoticeKind::Status&&
           !session_->notice().message.empty();
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
      // T/R keyboard parity evidence: candidate cycling and the one-key start
      // must both surface a status notice. Only exercised on the mutating
      // launch so the paused-reload payload comparison stays clean.
      smoke_shortcut_=send_key('t',width,height)&&
                      send_key('r',width,height)&&
                      shortcut_status_reported();
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
    {
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
      // Hover parity: UiFleetDestinationPreview shows the route on pointer
      // motion alone, without arming CONFIRM.
      {
        InputSnapshot hover;hover.drawable_width=width;hover.drawable_height=height;
        hover.pointer=target_point;
        hover.events={{InputEventType::PointerMove,target_point,{}}};
        if(!update(hover,width,height,0.,false))
          throw std::runtime_error("Fleet smoke hover closed the campaign.");
      }
      if(!fleet_workspace_.preview()||
         fleet_workspace_.preview()->target_system_id!=*target||
         fleet_workspace_.preview()->command_available)
        throw std::runtime_error("Fleet smoke hover did not produce a display-only preview.");
      smoke_fleet_hover_preview_=true;
      if(!selected_destination){
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
    // Civilian recovery parity: a second civilian fleet proves the HOLD /
    // RESUME buttons and the return-preview Recovery row without mutating the
    // routed fleet's save evidence.
    {
      const auto &view_fleets=fleet_workspace_.view()->own_fleets;
      const auto civilian=std::ranges::find_if(view_fleets,[&](const auto &f){
        return is_civilian_role(f.role)&&f.id!=selected_fleet_id;});
      if(civilian==view_fleets.end())throw std::runtime_error(
          "Fleet smoke found no second civilian fleet for recovery orders.");
      // The row click refreshes the view — own the id before dereferencing.
      const int civilian_fleet_id=civilian->id;
      const auto civilian_index=static_cast<std::size_t>(civilian-view_fleets.begin());
      click({layout.list.x+12.f*layout.scale,
             layout.list.y+(static_cast<float>(civilian_index)*45.f+20.f)*layout.scale});
      if(fleet_controller_.selection()!=std::optional<int>{civilian_fleet_id})
        throw std::runtime_error("Fleet smoke civilian selection failed.");
      const auto &view_after=fleet_workspace_.view();
      const auto selected_row=std::ranges::find(view_after->own_fleets,
                                                civilian_fleet_id,&NativeOwnFleet::id);
      if(selected_row==view_after->own_fleets.end()||
         selected_row->civilian_return_preview.empty())
        throw std::runtime_error(
            "Fleet smoke saw no civilian return preview in the Recovery row.");
      click(center(layout.hold));
      const auto held=std::ranges::find(
          session_->frame().runtime().world().campaign().fleets,
          civilian_fleet_id,&FleetState::id);
      if(held==session_->frame().runtime().world().campaign().fleets.end()||
         !held->hold_requested)
        throw std::runtime_error("Fleet smoke hold order did not land.");
      click(center(layout.hold));
      if(held->hold_requested)
        throw std::runtime_error("Fleet smoke resume order did not land.");
      smoke_civilian_recovery_=true;
    }
    // Reference UiFocusOwnedFleet: LOCATE keeps the selection and centers the
    // strategic map on the fleet's authoritative position. Unarmed fleets
    // draw the action in the right-edge command slot.
    if(const auto selected_id=fleet_controller_.selection()){
      click(center(layout.engage));
      const auto &world_fleets=
          session_->frame().runtime().world().campaign().fleets;
      const auto located=std::ranges::find(world_fleets,*selected_id,
                                           &FleetState::id);
      if(located==world_fleets.end()||
         std::abs(camera_.center.x-located->position.x)>1e-6||
         std::abs(camera_.center.y-located->position.y)>1e-6)
        throw std::runtime_error("Fleet smoke LOCATE did not center its fleet.");
      smoke_fleet_located_=true;
    }
    // Reference UiIssueMilitaryOrder: an armed fleet exposes strategic
    // HOLD / DEFEND / RETREAT orders; the authored patrol-corvette profile
    // proves the full order chain against the authoritative save. Runs
    // without an armed fixture simply report military=0.
    {
      const auto &view_fleets=fleet_workspace_.view()->own_fleets;
      const auto armed=std::ranges::find_if(view_fleets,[](const auto &f){
        return f.combat_status&&f.combat_status->is_armed;});
      if(armed!=view_fleets.end()){
        const int armed_fleet_id=armed->id;
        const auto armed_index=
            static_cast<std::size_t>(armed-view_fleets.begin());
        click({layout.list.x+12.f*layout.scale,
               layout.list.y+(static_cast<float>(armed_index)*45.f+20.f)*
                                 layout.scale});
        if(fleet_controller_.selection()!=std::optional<int>{armed_fleet_id})
          throw std::runtime_error("Fleet smoke armed selection failed.");
        const auto order_of=[&]{
          const auto &world_fleets=
              session_->frame().runtime().world().campaign().fleets;
          const auto it=std::ranges::find(world_fleets,armed_fleet_id,
                                          &FleetState::id);
          if(it==world_fleets.end()||!it->combat)throw std::runtime_error(
              "Fleet smoke lost the armed fleet's combat state.");
          return it->combat->order;
        };
        click(center(layout.order_hold));
        if(order_of()!=MilitaryOrderType::Hold)throw std::runtime_error(
            "Fleet smoke HOLD order did not land.");
        click(center(layout.order_defend));
        if(order_of()!=MilitaryOrderType::Defend)throw std::runtime_error(
            "Fleet smoke DEFEND order did not land.");
        click(center(layout.order_retreat));
        if(order_of()!=MilitaryOrderType::Retreat)throw std::runtime_error(
            "Fleet smoke RETREAT order did not land.");
        // Re-issue DEFEND so the durable save evidence carries a stable
        // order (RETREAT resolves into a disengagement during simulation).
        click(center(layout.order_defend));
        if(order_of()!=MilitaryOrderType::Defend)throw std::runtime_error(
            "Fleet smoke DEFEND re-issue did not land.");
        smoke_military_fleet_id_=armed_fleet_id;
        smoke_military_orders_=true;
      }
    }
    // EmpireOverviewPanel parity: with no fleet selected the detail area
    // lists own colonies; a colony row opens its system's orbital view.
    {
      fleet_controller_.clear_selection();
      refresh_fleets(true);
      const auto *overview=fleet_workspace_.overview();
      const UiRect overview_content{layout.details.x,layout.details.y,
          layout.details.width,
          layout.route.y+layout.route.height-layout.details.y};
      if(!overview||overview->colonies.empty())throw std::runtime_error(
          "Fleet smoke saw no empire overview colonies.");
      const auto overview_layout=native_overview::overview_layout_for(
          *overview,overview_content);
      if(overview_layout.colony_rows.empty())throw std::runtime_error(
          "Fleet smoke saw no empire overview colony rows.");
      const auto colony_system=overview->colonies.front().system_id;
      click({overview_layout.colony_rows[0].x+8.f*overview_layout.scale,
             overview_layout.colony_rows[0].y+8.f*overview_layout.scale});
      if(!system_workspace_.visible()||
         system_workspace_.system_id()!=std::optional<int>{colony_system})
        throw std::runtime_error(
            "Fleet smoke overview colony row did not open its system view.");
      system_workspace_.close();
      smoke_overview_=true;
    }
    // Inspection-card parity: clicking a star selects it and renders the
    // observer-gated intelligence card (reference SystemInspectionPanel).
    {
      const auto &map=session_->frame().runtime().world().campaign();
      std::optional<int> inspect_id;Point inspect_point;
      const auto markers=fleet_markers(width,height);
      const auto marker_radius=11.f*FleetWorkspaceLayout::for_viewport(width,height).scale;
      int inspect_rank=-1;
      for(const auto &system:map.systems){
        const auto point=camera_.project({system.position.x,system.position.y},
                                         width,height);
        if(point.x<0||point.y<0||point.x>=width||point.y>=height||
           layout.panel.contains(point))continue;
        if(std::ranges::any_of(markers,[&](const auto &marker){
             return std::hypot(marker.position.x-point.x,
                               marker.position.y-point.y)<=marker_radius;}))
          continue;
        const int rank=
            map.knowledge.is_system_fully_surveyed(map.player_civilization_id,
                                                   system.id)?2:
            map.knowledge.is_system_known(map.player_civilization_id,system.id)
                ?1:0;
        if(rank>inspect_rank){inspect_rank=rank;inspect_id=system.id;
                              inspect_point=point;}
      }
      if(inspect_id){
        click(inspect_point);
        (void)scene(width,height);
        smoke_inspection_=selected_id_==inspect_id&&
                          inspection_card_bounds_.has_value()&&
                          inspection_card_bounds_->width>100.f;
      }

    }
    // Missions-panel parity: the MISSIONS rail button opens the observer-safe
    // mission board (reference ExplorationMissionPanel missions tab).
    {
      const auto rail=NativeUiLayout::for_viewport(width,height);
      const auto button=center(rail.missions);
      click(button);
      (void)scene(width,height);
      const auto board=native_missions::build_mission_board(
          session_->frame().runtime().world().campaign());
      smoke_missions_=missions_view_.visible()?1:0;
      smoke_mission_count_=static_cast<int>(board.missions.size());
      // Colony Sites tab: switch tabs and record the bounded site-browser
      // state (reference GetUiColonyOpportunityState).
      const auto &campaign=session_->frame().runtime().world().campaign();
      const auto site_fleets=settlement_controller_.build(
          session_->frame(),session_->cache().generation);
      const auto colonies=native_missions::build_owned_colony_rows(campaign);
      const auto sites_layout=native_missions::mission_layout_for(
          board,native_missions::colony_site_selection(site_fleets,0,0),
          colonies.size(),width,height,true);
      click(center(sites_layout.sites_tab));
      const auto selection=native_missions::colony_site_selection(
          site_fleets,0,0);
      smoke_mission_sites_=static_cast<int>(site_fleets.size());
      smoke_mission_site_selection_=selection.available?1:0;
    }
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
  void prepare_diplomacy_smoke(int width,int height){
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Diplomacy smoke input closed the campaign.");
    };
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    click(center(main_layout.diplomacy));
    if(!diplomacy_workspace_.visible()||!diplomacy_workspace_.view())
      throw std::runtime_error(
          "Diplomacy smoke could not open the relations workspace.");
    const auto &view=*diplomacy_workspace_.view();
    std::size_t row=view.contacts.size();
    for(std::size_t i=0;i<view.contacts.size();++i)
      if(view.contacts[i].identified&&view.contacts[i].communication_available){
        row=i;break;}
    if(row==view.contacts.size())
      for(std::size_t i=0;i<view.contacts.size();++i)
        if(view.contacts[i].identified){row=i;break;}
    if(row==view.contacts.size())
      throw std::runtime_error(
          "Diplomacy smoke found no identified contact.");
    const auto layout=DiplomacyWorkspaceLayout::for_viewport(width,height);
    const auto s=layout.scale;
    click(center(UiRect{layout.contact_rows.x+4.f*s,
                        layout.contact_rows.y+4.f*s+
                            static_cast<float>(row)*62.f*s,
                        layout.contact_rows.width-8.f*s,58.f*s}));
    // The selection click carries no elapsed time; rebuild so the action
    // column tracks the selected contact.
    refresh_diplomacy(true);
    const auto &selected=diplomacy_workspace_.view()->selected;
    if(!selected.present)
      throw std::runtime_error("Diplomacy smoke lost the selected contact.");
    std::size_t action_index=0;
    if(selected.has_visible_communication||selected.can_attempt_communication)
      ++action_index;
    const bool negotiate=selected.can_offer_non_aggression||
        selected.can_request_access||selected.can_offer_peace||
        selected.can_offer_ceasefire||selected.can_set_access;
    if(!negotiate)
      throw std::runtime_error("Diplomacy smoke found no negotiable channel.");
    click(center(UiRect{layout.actions.x+8.f*s,
                        layout.actions.y+8.f*s+
                            static_cast<float>(action_index)*36.f*s,
                        layout.actions.width-16.f*s,30.f*s}));
    if(!diplomacy_workspace_.modal_open())
      throw std::runtime_error(
          "Diplomacy smoke could not open the negotiation modal.");
    // The authored save leaves access unspecified, so the first offered term
    // is the transit-access request; confirm it through the follow-up modal.
    click(center(UiRect{layout.modal_panel.x+16.f*s,
                        layout.modal_panel.y+74.f*s,
                        layout.modal_panel.width-32.f*s,36.f*s}));
    if(!diplomacy_workspace_.modal_open())
      throw std::runtime_error(
          "Diplomacy smoke lost the confirmation modal.");
    click(center(UiRect{layout.modal_panel.x+16.f*s,
                        layout.modal_panel.y+layout.modal_panel.height-92.f*s,
                        layout.modal_panel.width-32.f*s,36.f*s}));
  }
  void capture_diplomacy_workspace(){
    const auto &view=diplomacy_workspace_.view();
    if(!view)
      throw std::runtime_error("Diplomacy smoke lost its view.");
    smoke_diplomacy_contacts_=view->contacts.size();
    for(const auto &contact:view->contacts){
      if(contact.identified)++smoke_diplomacy_identified_;
      else{
        ++smoke_diplomacy_unidentified_;
        if(!contact.civilization_id&&!contact.species_name)
          ++smoke_diplomacy_redacted_;
      }
      if(contact.communication_available)++smoke_diplomacy_channels_;
    }
    smoke_diplomacy_agreements_=view->agreements.size();
    smoke_diplomacy_history_=view->history.size();
    smoke_diplomacy_proposals_=view->proposals.size();
    smoke_diplomacy_selected_civ_=
        view->selected.target_civilization_id.value_or(-1);
    smoke_diplomacy_last_system_=
        view->selected.present&&view->selected.contact_index<view->contacts.size()
            ? view->contacts[view->selected.contact_index]
                  .last_observed_system_id.value_or(-1)
            : -1;
    for(const auto &proposal:view->proposals)
      if(proposal.direction=="OUTGOING"&&proposal.can_withdraw)
        smoke_diplomacy_proposal_id_=proposal.proposal_id;
    for(const auto &[path,image]:diplomacy_portraits_)
      if(image){smoke_diplomacy_portrait_=true;break;}
  }
  void prepare_diplomacy_proposals(int width,int height){
    const auto layout=DiplomacyWorkspaceLayout::for_viewport(width,height);
    const auto gap=8.f*layout.scale;
    const auto tab_width=(layout.tabs.width-gap*4.f)/5.f;
    InputSnapshot input;
    input.drawable_width=width;
    input.drawable_height=height;
    input.pointer=center(UiRect{layout.tabs.x+tab_width+gap,layout.tabs.y,
                                tab_width,layout.tabs.height});
    input.events={{InputEventType::LeftPressed,input.pointer},
                  {InputEventType::LeftReleased,input.pointer}};
    if(!update(input,width,height,0.,false))
      throw std::runtime_error("Diplomacy proposals input closed the campaign.");
  }
  void capture_diplomacy_proposals(){
    const auto &view=diplomacy_workspace_.view();
    if(!view)return;
    for(const auto &proposal:view->proposals){
      if(proposal.direction=="OUTGOING"&&proposal.can_withdraw)
        ++smoke_diplomacy_pending_after_;
      if(proposal.can_accept)++smoke_diplomacy_incoming_;
    }
  }
  [[nodiscard]] std::string diplomacy_smoke_status()const{
    std::ostringstream out;
    out<<"{\"contacts\":"<<smoke_diplomacy_contacts_
       <<",\"identified\":"<<smoke_diplomacy_identified_
       <<",\"unidentified\":"<<smoke_diplomacy_unidentified_
       <<",\"redacted\":"<<smoke_diplomacy_redacted_
       <<",\"channels\":"<<smoke_diplomacy_channels_
       <<",\"agreements\":"<<smoke_diplomacy_agreements_
       <<",\"history\":"<<smoke_diplomacy_history_
       <<",\"proposals\":"<<smoke_diplomacy_proposals_
       <<",\"selected_civ\":"<<smoke_diplomacy_selected_civ_
       <<",\"last_observed_system\":"<<smoke_diplomacy_last_system_
       <<",\"portrait\":"<<(smoke_diplomacy_portrait_?1:0)
       <<",\"command_accepted\":"<<(last_diplomacy_command_accepted_?1:0)
       <<",\"proposal_id\":"<<smoke_diplomacy_proposal_id_
       <<",\"pending_after\":"<<smoke_diplomacy_pending_after_
       <<",\"incoming_pending\":"<<smoke_diplomacy_incoming_
       <<"}";
    return out.str();
  }
  void prepare_notification_smoke(int width,int height){
    // The diplomacy path submits a proposal, which harvests into the feed as
    // an observer-filtered Diplomacy bulletin carrying the contact id.
    prepare_diplomacy_smoke(width,height);
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    InputSnapshot input;
    input.drawable_width=width;
    input.drawable_height=height;
    input.pointer=center(main_layout.notifications);
    input.events={{InputEventType::LeftPressed,input.pointer},
                  {InputEventType::LeftReleased,input.pointer}};
    if(!update(input,width,height,0.,false))
      throw std::runtime_error(
          "Notification smoke input closed the campaign.");
    if(!notification_view_.visible())
      throw std::runtime_error(
          "Notification smoke could not open the recent-events panel.");
    smoke_notification_panel_=true;
  }
  void capture_notification_panel(){
    const auto &feed=session_->notifications();
    smoke_notification_items_=static_cast<int>(feed.items().size());
    smoke_notification_unread_=
        feed.unread_count(notification_view_.last_read());
    for(const auto &item:feed.items()){
      if(item.category!="Diplomacy"||!item.diplomatic_contact_id)continue;
      ++smoke_notification_diplomacy_;
      // The panel lists newest first, so the newest contact-bearing item is
      // what the first OPEN RELATIONS button will dispatch.
      smoke_notification_contact_=*item.diplomatic_contact_id;
    }
    if(smoke_notification_diplomacy_==0)
      throw std::runtime_error(
          "Notification smoke found no diplomacy bulletin with a contact.");
  }
  void prepare_notification_contact(int width,int height){
    const auto &feed=session_->notifications();
    const auto layout=native_notifications::notification_layout_for(
        feed.items(),width,height);
    for(std::size_t i=0;i<layout.contact_buttons.size();++i){
      if(!layout.contact_buttons[i])continue;
      const auto &item=feed.items()[feed.items().size()-1-i];
      if(!item.diplomatic_contact_id)continue;
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=center(*layout.contact_buttons[i]);
      input.events={{InputEventType::LeftPressed,input.pointer},
                    {InputEventType::LeftReleased,input.pointer}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error(
            "Notification contact input closed the campaign.");
      smoke_notification_contact_=*item.diplomatic_contact_id;
      return;
    }
    throw std::runtime_error(
        "Notification smoke found no OPEN RELATIONS button.");
  }
  void capture_notification_contact(){
    const auto &view=diplomacy_workspace_.view();
    if(!diplomacy_workspace_.visible()||!view||!view->selected.present)
      throw std::runtime_error(
          "Notification smoke did not focus the relations workspace.");
    smoke_notification_focused_=
        view->selected.target_civilization_id.value_or(-1);
    if(smoke_notification_focused_!=smoke_notification_contact_)
      throw std::runtime_error(
          "Notification OPEN RELATIONS focused the wrong contact.");
  }
  [[nodiscard]] std::string notification_smoke_status()const{
    std::ostringstream out;
    out<<"{\"panel\":"<<(smoke_notification_panel_?1:0)
       <<",\"items\":"<<smoke_notification_items_
       <<",\"unread\":"<<smoke_notification_unread_
       <<",\"diplomacy\":"<<smoke_notification_diplomacy_
       <<",\"contact\":"<<smoke_notification_contact_
       <<",\"focused_civ\":"<<smoke_notification_focused_
       <<"}";
    return out.str();
  }
  void prepare_logistics_smoke(int width,int height){
    // The top-rail SUPPLY button toggles the panel (reference CampaignSidebar
    // LOGISTICS section).
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    InputSnapshot input;
    input.drawable_width=width;
    input.drawable_height=height;
    input.pointer=center(main_layout.logistics);
    input.events={{InputEventType::LeftPressed,input.pointer},
                  {InputEventType::LeftReleased,input.pointer}};
    if(!update(input,width,height,0.,false))
      throw std::runtime_error(
          "Logistics smoke input closed the campaign.");
    if(!logistics_view_.visible())
      throw std::runtime_error(
          "Logistics smoke could not open the supply network panel.");
    smoke_logistics_panel_=true;
  }
  void capture_logistics_panel(){
    const auto &campaign=session_->frame().runtime().world().campaign();
    const auto logistics=native_logistics::build_home_logistics(
        campaign,campaign.player_civilization_id);
    smoke_logistics_ready_=logistics.ready?1:0;
    smoke_logistics_nodes_=static_cast<int>(logistics.nodes.size());
    smoke_logistics_corridors_=logistics.corridor_count;
    smoke_logistics_supply_=logistics.supply_per_day;
    smoke_logistics_demand_=logistics.demand_per_day;
    smoke_logistics_delivered_=logistics.delivered_per_day;
    smoke_logistics_shortfall_=logistics.shortfall_per_day;
    if(smoke_logistics_ready_==0)
      throw std::runtime_error(
          "Logistics smoke found no resolvable home-system network.");
  }
  [[nodiscard]] std::string logistics_smoke_status()const{
    std::ostringstream out;
    out<<"{\"panel\":"<<(smoke_logistics_panel_?1:0)
       <<",\"ready\":"<<smoke_logistics_ready_
       <<",\"nodes\":"<<smoke_logistics_nodes_
       <<",\"corridors\":"<<smoke_logistics_corridors_
       <<",\"supply\":"<<smoke_logistics_supply_
       <<",\"demand\":"<<smoke_logistics_demand_
       <<",\"delivered\":"<<smoke_logistics_delivered_
       <<",\"shortfall\":"<<smoke_logistics_shortfall_
       <<"}";
    return out.str();
  }
  void prepare_economy_smoke(int width,int height){
    // The top-rail ECONOMY button toggles the panel (reference CampaignSidebar
    // "economy" section), then the third priority toggle issues the
    // Shipbuilding-first order through the real dispatch path.
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error(
            "Economy smoke input closed the campaign.");
    };
    click(center(main_layout.economy));
    if(!economy_view_.visible())
      throw std::runtime_error(
          "Economy smoke could not open the treasury panel.");
    smoke_economy_panel_=true;
    const auto &campaign=session_->frame().runtime().world().campaign();
    const auto view=native_economy::build_economy_view(
        campaign,&session_->frame().runtime().research(),
        last_industry_allocation_);
    const auto layout=
        native_economy::economy_layout_for(view,width,height);
    click(center(layout.priority_buttons[2]));
    const auto &after=session_->frame().runtime().world().campaign();
    const auto economy=std::ranges::find(
        after.economies,after.player_civilization_id,
        &CivilizationEconomy::civilization_id);
    smoke_economy_toggled_=
        economy!=after.economies.end()&&
        economy->industry_priority==IndustryPriority::ShipbuildingFirst;
    if(!smoke_economy_toggled_)
      throw std::runtime_error(
          "Economy smoke priority toggle did not update the stored priority.");
  }
  void capture_economy_panel(){
    const auto &campaign=session_->frame().runtime().world().campaign();
    const auto view=native_economy::build_economy_view(
        campaign,&session_->frame().runtime().research(),
        last_industry_allocation_);
    smoke_economy_ready_=view.ready?1:0;
    smoke_economy_cards_=static_cast<int>(view.cards.size());
    smoke_economy_flow_rows_=static_cast<int>(view.income_rows.size()+
                                              view.cost_rows.size());
    smoke_economy_priority_=
        static_cast<int>(view.industry_priority);
    if(smoke_economy_ready_==0||smoke_economy_flow_rows_!=9)
      throw std::runtime_error(
          "Economy smoke found no resolvable treasury view.");
  }
  [[nodiscard]] std::string economy_smoke_status()const{
    std::ostringstream out;
    out<<"{\"panel\":"<<(smoke_economy_panel_?1:0)
       <<",\"ready\":"<<smoke_economy_ready_
       <<",\"cards\":"<<smoke_economy_cards_
       <<",\"rows\":"<<smoke_economy_flow_rows_
       <<",\"priority\":"<<smoke_economy_priority_
       <<",\"toggled\":"<<(smoke_economy_toggled_?1:0)
       <<"}";
    return out.str();
  }
  void prepare_battle_smoke(int width,int height){
    const auto click=[&](Point point){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Battle smoke input closed the campaign.");
    };
    // The first update detects the unreconciled encounter and opens the
    // tactical workspace, mirroring the reference layer-70 takeover.
    InputSnapshot idle;
    idle.drawable_width=width;
    idle.drawable_height=height;
    if(!update(idle,width,height,0.,false))
      throw std::runtime_error("Battle smoke could not run the campaign frame.");
    if(!battle_workspace_.visible()||!battle_workspace_.snapshot())
      throw std::runtime_error("Battle smoke found no active tactical encounter.");
    const auto *snapshot=battle_workspace_.snapshot();
    const auto &battle_world=session_->frame().runtime().world().campaign();
    const auto own=std::ranges::find_if(snapshot->formations,[&](const auto &formation){
      return formation.civilization_id==battle_world.player_civilization_id;});
    if(own==snapshot->formations.end())
      throw std::runtime_error("Battle smoke found no owned formation.");
    click(battle_workspace_.project(own->position,width,height));
    if(battle_workspace_.selection().empty())
      throw std::runtime_error("Battle smoke could not select its formation.");
    const auto layout=native_battle_ui::BattleWorkspaceLayout::for_viewport(width,height);
    click(center(layout.order_buttons[0]));
    if(!last_battle_order_accepted_)
      throw std::runtime_error("Battle smoke order was rejected by the engine.");
    // Resume the tactical clock so the remaining frames advance the battle.
    click(center(layout.play));
  }
  void capture_battle_workspace(){
    const auto *snapshot=battle_workspace_.snapshot();
    if(!snapshot)
      throw std::runtime_error("Battle smoke lost its snapshot.");
    const auto &battle_world=session_->frame().runtime().world().campaign();
    smoke_battle_formations_=snapshot->formations.size();
    smoke_battle_events_=snapshot->events.size();
    smoke_battle_salvos_=snapshot->active_missile_salvos.size();
    smoke_battle_tick_=snapshot->tick;
    for(const auto &formation:snapshot->formations){
      if(formation.civilization_id==battle_world.player_civilization_id){
        ++smoke_battle_own_;
        if(!formation.is_exact)++smoke_battle_own_inexact_;
      }else{
        ++smoke_battle_foreign_;
        if(!formation.is_exact)++smoke_battle_redacted_;
        if(formation.cohorts.empty()&&formation.important_vessels.empty())
          ++smoke_battle_vessels_hidden_;
      }
    }
    smoke_battle_selected_=battle_workspace_.selected_count();
    smoke_battle_tokens_=battle_workspace_.rendered_tokens();
  }
  [[nodiscard]] std::string battle_smoke_status()const{
    std::ostringstream out;
    out<<"{\"formations\":"<<smoke_battle_formations_
       <<",\"own\":"<<smoke_battle_own_
       <<",\"foreign\":"<<smoke_battle_foreign_
       <<",\"redacted\":"<<smoke_battle_redacted_
       <<",\"vessels_hidden\":"<<smoke_battle_vessels_hidden_
       <<",\"own_inexact\":"<<smoke_battle_own_inexact_
       <<",\"selected\":"<<smoke_battle_selected_
       <<",\"tokens\":"<<smoke_battle_tokens_
       <<",\"events\":"<<smoke_battle_events_
       <<",\"salvos\":"<<smoke_battle_salvos_
       <<",\"tick\":"<<smoke_battle_tick_
       <<",\"order_accepted\":"<<(last_battle_order_accepted_?1:0)
       <<"}";
    return out.str();
  }
  void prepare_audio_smoke(int width,int height){
    // Exercise the menu's audio settings view before the generic save: open
    // the menu, enter AUDIO, drag MASTER toward 25%, restore defaults, Done.
    if(!menu_)toggle_menu();
    const auto click=[&](Point point){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Audio smoke input closed the campaign.");
    };
    const auto layout=NativeUiLayout::for_viewport(width,height);
    // Land a strategic frame then a save before any bundle export so each ZIP
    // carries all three entries (log, system info, campaign save).
    InputSnapshot ready;ready.drawable_width=width;ready.drawable_height=height;
    if(!update(ready,width,height,0.,true))
      throw std::runtime_error("Audio smoke readiness frame closed the campaign.");
    session_->request_save();
    click(center(layout.audio_button));
    if(!audio_settings_.visible())
      throw std::runtime_error("Audio smoke could not open the settings view.");
    const auto settings_layout=native_audio_settings::AudioSettingsLayout::for_viewport(width,height);
    const auto &track=settings_layout.tracks[0];
    const Point quarter{track.x+track.width*.25f,track.y+track.height*.5f};
    InputSnapshot drag;drag.drawable_width=width;drag.drawable_height=height;drag.pointer=quarter;
    drag.events={{InputEventType::LeftPressed,quarter},
                 {InputEventType::PointerMove,quarter,{track.width*.25f,0.f}},
                 {InputEventType::LeftReleased,quarter}};
    if(!update(drag,width,height,0.,false))
      throw std::runtime_error("Audio smoke drag closed the campaign.");
    if(std::abs(audio_mixer_.settings().master-.25f)>.03f)
      throw std::runtime_error("Audio smoke slider did not reach the dragged level.");
    click(center(settings_layout.defaults));
    click(center(settings_layout.done));
    if(audio_settings_.visible())
      throw std::runtime_error("Audio smoke could not close the settings view.");
    const auto settings=audio_mixer_.settings();
    if(std::abs(settings.master-.78f)>.001f||std::abs(settings.music-.64f)>.001f||std::abs(settings.sfx-.82f)>.001f)
      throw std::runtime_error("Audio smoke did not restore and persist the default mix.");
    smoke_audio_settings_=1;
    // VOICE & SUBTITLES parity: open the voice settings view, flip a toggle,
    // cycle a choice, close via Escape and verify the payload persisted.
    click(center(layout.voice_button));
    if(!voice_settings_view_.visible())
      throw std::runtime_error("Audio smoke could not open the voice settings view.");
    const auto voice_layout=native_voice_settings::VoiceSettingsLayout::for_viewport(width,height);
    click(center(voice_layout.toggle_boxes[0]));
    click(center(voice_layout.choice_buttons[1]));
    {
      InputSnapshot escape;escape.drawable_width=width;escape.drawable_height=height;
      escape.events={{InputEventType::EscapePressed}};
      if(!update(escape,width,height,0.,false))
        throw std::runtime_error("Audio smoke voice Escape closed the campaign.");
    }
    if(voice_settings_view_.visible())
      throw std::runtime_error("Audio smoke could not close the voice settings view.");
    const auto persisted=native_voice::NativeVoiceSettings::load(voice_settings_path_);
    if(persisted.enable_voices||persisted.frequency!=native_voice::VoiceFrequency::Frequent)
      throw std::runtime_error("Audio smoke did not persist the voice settings.");
    // Restore defaults so downstream voice evidence still synthesizes.
    apply_voice_settings(native_voice::NativeVoiceSettings{});
    smoke_voice_settings_=1;
    // VIDEO parity: open the video settings view, cycle V-SYNC to Adaptive,
    // Apply to arm the CONFIRM DISPLAY rollback, Keep to persist, then
    // restore defaults so downstream pacing stays vsync-bound.
    click(center(layout.video_button));
    if(!video_settings_view_.visible())
      throw std::runtime_error("Audio smoke could not open the video settings view.");
    const auto video_layout=native_video_settings::VideoSettingsLayout::for_viewport(width,height);
    click(center(video_layout.choice_buttons[1]));
    click(center(video_layout.apply));
    if(!video_settings_view_.confirming())
      throw std::runtime_error("Audio smoke Apply did not arm the display rollback.");
    click(center(video_layout.keep));
    if(video_settings_view_.visible()||video_rollback_)
      throw std::runtime_error("Audio smoke Keep did not commit the video settings.");
    if(native_video_settings::NativeVideoSettings::load(video_settings_path_).vsync!=
       native_video_settings::VideoVsync::Adaptive)
      throw std::runtime_error("Audio smoke did not persist the video settings.");
    apply_video_settings(native_video_settings::NativeVideoSettings{});
    video_settings_.save(video_settings_path_);
    smoke_video_settings_=1;
    // SUPPORT BUNDLE / F8 parity: the menu button exports while the menu is
    // open; F8 exports with the menu closed. Both land in the support dir.
    click(center(layout.support_button));
    toggle_menu();
    if(!send_key(0x40000041u,width,height)) // SDLK_F8
      throw std::runtime_error("Audio smoke F8 input closed the campaign.");
    std::error_code support_error;
    for(const auto &entry:std::filesystem::directory_iterator(
            session_->save_path().parent_path()/"support",support_error)){
      if(!support_error&&entry.is_regular_file()&&
         entry.path().extension()==".zip"&&entry.file_size()>22)
        smoke_audio_support_=1;
    }
    smoke_save_pending_=true;
  }
  [[nodiscard]] std::string audio_smoke_status(){
    // Mix bounded chunks on the caller thread so the smoke reports real decode,
    // voice and gain evidence without depending on an attached audio device.
    audio_mixer_.play(native_audio::NativeSfx::ui_confirm);
    audio_mixer_.play_event("exploration");
    float buffer[native_audio::output_channels*240]{};
    std::size_t voiced_chunks=0,clipped=0;
    double peak=0;
    for(int chunk=0;chunk<12;++chunk){
      audio_mixer_.mix(buffer,240);
      bool voiced=false;
      for(const float sample:buffer){
        peak=std::max(peak,std::abs(static_cast<double>(sample)));
        if(sample!=0.f)voiced=true;
        if(sample<-1.f||sample>1.f)++clipped;
      }
      if(voiced)++voiced_chunks;
    }
    const auto settings=audio_mixer_.settings();
    std::ostringstream out;
    out<<"{\"required\":"<<(audio_mixer_.has_required_audio()?1:0)
       <<",\"music\":"<<(audio_mixer_.music_playing()?1:0)
       <<",\"music_frames\":"<<audio_mixer_.music_frame_count()
       <<",\"device\":"<<(audio_device_.is_open()?1:0)
       <<",\"voices\":"<<audio_mixer_.active_voices()
       <<",\"voiced_chunks\":"<<voiced_chunks
       <<",\"clipped_samples\":"<<clipped
       <<",\"peak\":"<<std::fixed<<std::setprecision(4)<<peak
       <<std::defaultfloat
       <<",\"master\":"<<settings.master<<",\"music_gain\":"<<settings.music
       <<",\"sfx_gain\":"<<settings.sfx
       <<",\"settings\":"<<smoke_audio_settings_
       <<",\"support\":"<<smoke_audio_support_
       <<",\"voice_settings\":"<<smoke_voice_settings_
       <<",\"video_settings\":"<<smoke_video_settings_
       <<",\"voice_pipeline\":"<<(voice_playback_?1:0)
       <<",\"voice_backend\":"
       <<json_string(voice_playback_?voice_playback_->backend_status():"none")
       <<",\"voice_lines\":"<<(voice_playback_?voice_playback_->played_lines()+voice_playback_->subtitle_lines():0)
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
    // C/B keyboard parity evidence: the active-project and no-candidate
    // guards keep these presses status-only on both smoke launches.
    smoke_shortcut_=send_key('c',width,height)&&
                    send_key('b',width,height)&&
                    shortcut_status_reported();
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
  [[nodiscard]] bool shortcut_smoke_succeeded()const noexcept{return smoke_shortcut_;}
  // Reference UiNewCampaign saves the live campaign before the sandbox setup
  // runs; the outer loop only exits once that save lands.
  void request_new_game(){if(!new_game_requested_){new_game_requested_=true;session_->request_save();}}
  [[nodiscard]] bool new_game_ready()const{return new_game_requested_&&session_->notice().kind==SessionNoticeKind::Saved;}
  void adopt_developer_smoke_evidence(DeveloperSmokeEvidence &evidence){developer_smoke_=&evidence;}
  [[nodiscard]] bool developer_mode()const{return session_->developer_mode();}
  [[nodiscard]] bool developer_tools_used()const{return session_->developer_tools_used();}
  [[nodiscard]] std::filesystem::path player_save_path()const{
    // Custom --save-path tests pass the anchor through the constructor; the
    // default matches default_native_campaign_save_path().
    return player_save_path_.empty()?default_native_campaign_save_path():player_save_path_;
  }
  [[nodiscard]] std::filesystem::path developer_save_path()const{
    return developer_save_path_beside(player_save_path());
  }
  [[nodiscard]] static bool save_slot_exists(const std::filesystem::path &path){
    std::error_code error{};
    if(std::filesystem::is_regular_file(path,error))return true;
    auto backup=path;backup+=".bak";return std::filesystem::is_regular_file(backup,error);
  }
  [[nodiscard]] bool developer_save_exists()const{return save_slot_exists(developer_save_path());}
  [[nodiscard]] bool player_save_exists()const{return save_slot_exists(player_save_path());}
  // Reference UiSwitchToDeveloperMode/UiSwitchToPlayerMode: the open campaign
  // checkpoints to its own slot, then the outer loop replaces the session.
  void request_developer_switch(){if(!mode_switch_requested_&&session_->checkpoint_now(utc_timestamp()))mode_switch_requested_=true;}
  void request_player_switch(){request_developer_switch();}
  // Reference UiNewDeveloperCampaign: the outgoing Player campaign checkpoints
  // first, then the outer loop builds a fresh seeded Developer world. From
  // Developer mode the current save stays untouched so the fresh campaign's
  // first write preserves it as the slot's .bak.
  void request_new_developer_campaign(std::int64_t seed){
    if(mode_switch_requested_)return;
    if(!session_->developer_mode()&&
       !session_->checkpoint_now(utc_timestamp()))
      return;
    new_developer_seed_=seed;
    mode_switch_requested_=true;
  }
  [[nodiscard]] bool mode_switch_ready()const{return mode_switch_requested_;}
  [[nodiscard]] std::optional<std::int64_t> new_developer_seed()const{return new_developer_seed_;}
  void run_developer_command(std::string_view command_id){
    const auto outcome=session_->run_developer_command(command_id,utc_timestamp());
    developer_result_=outcome.message;developer_result_accepted_=outcome.accepted;
  }
  // --developer-smoke phase A (player session): open the campaign menu, open
  // the DEVELOPMENT submenu and click its OPEN row; the outer loop then
  // replaces the session.
  void prepare_developer_smoke(int width,int height){
    if(session_->developer_mode())
      throw std::runtime_error("Developer smoke must start from a player campaign.");
    InputSnapshot escape;escape.drawable_width=width;escape.drawable_height=height;
    escape.events={{InputEventType::EscapePressed}};
    if(!update(escape,width,height,0.,false))
      throw std::runtime_error("Developer smoke Escape closed the campaign.");
    if(!menu_)throw std::runtime_error("Developer smoke Escape did not open the menu.");
    const auto click=[&](Point at){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=at;
      input.events={{InputEventType::LeftPressed,at},
                    {InputEventType::LeftReleased,at}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Developer smoke input closed the campaign.");
    };
    const auto layout=NativeUiLayout::for_viewport(width,height,false);
    click({layout.developer_button.x+layout.developer_button.width*.5f,
           layout.developer_button.y+layout.developer_button.height*.5f});
    if(!development_menu_.visible())
      throw std::runtime_error("The DEVELOPMENT menu row did not open the submenu.");
    const auto submenu=
        native_development::development_menu_layout_for(width,height);
    click({submenu.open_button.x+submenu.open_button.width*.5f,
           submenu.open_button.y+submenu.open_button.height*.5f});
    if(!mode_switch_ready())
      throw std::runtime_error("The submenu open row did not request the mode switch.");
  }
  // --developer-smoke phase B (Developer session): verify the Demo resume
  // speed, open DEV TOOLS through the DEVELOPMENT submenu, run grant_resources
  // through the panel and confirm the Developer envelope landed on its own
  // slot.
  void prepare_developer_tools_smoke(int width,int height){
    if(!session_->developer_mode())
      throw std::runtime_error("Developer smoke switched to a non-Developer session.");
    developer_smoke_->mode=true;
    developer_smoke_->demo=
        session_->frame().clock().speed()==StrategicSpeed::Demo;
    InputSnapshot escape;escape.drawable_width=width;escape.drawable_height=height;
    escape.events={{InputEventType::EscapePressed}};
    if(!update(escape,width,height,0.,false))
      throw std::runtime_error("Developer smoke Escape closed the campaign.");
    if(!menu_)throw std::runtime_error("Developer smoke Escape did not open the menu.");
    const auto click=[&](Point at){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=at;
      input.events={{InputEventType::LeftPressed,at},
                    {InputEventType::LeftReleased,at}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Developer smoke input closed the campaign.");
    };
    const auto menu_layout=NativeUiLayout::for_viewport(width,height,true);
    click({menu_layout.development_button.x+menu_layout.development_button.width*.5f,
           menu_layout.development_button.y+menu_layout.development_button.height*.5f});
    if(!development_menu_.visible())
      throw std::runtime_error("The DEVELOPMENT row did not open the submenu.");
    const auto submenu=
        native_development::development_menu_layout_for(width,height);
    click({submenu.tools_button.x+submenu.tools_button.width*.5f,
           submenu.tools_button.y+submenu.tools_button.height*.5f});
    if(!developer_tools_.visible()||menu_)
      throw std::runtime_error("The submenu DEVELOPER TOOLS row did not open the tools panel.");
    developer_smoke_->tools=true;
    const auto commands=developer_command_catalog();
    std::size_t row=commands.size();
    for(std::size_t i=0;i<commands.size();++i)
      if(commands[i].id=="grant_resources"){row=i;break;}
    if(row>=commands.size())
      throw std::runtime_error("The Developer catalog lost grant_resources.");
    const auto tools=native_developer::developer_tools_layout_for(width,height);
    click({tools.command_rows[row].x+tools.command_rows[row].width*.5f,
           tools.command_rows[row].y+tools.command_rows[row].height*.5f});
    if(!developer_result_accepted_||!session_->developer_tools_used())
      throw std::runtime_error("The DEV TOOLS grant_resources run was not accepted.");
    developer_smoke_->command=true;
    if(!developer_save_exists())
      throw std::runtime_error("The Developer command wrote no envelope save.");
    developer_smoke_->save=true;
  }
  // --developer-smoke phase C (Developer session): re-open the DEVELOPMENT
  // submenu and arm + confirm NEW DEVELOPER CAMPAIGN; the outer loop then
  // replaces the session with a fresh seeded world.
  void prepare_developer_fresh_smoke(int width,int height){
    if(!session_->developer_mode())
      throw std::runtime_error("Developer fresh-campaign smoke lost Developer mode.");
    // Phase B left the tools panel open; the first Escape closes it, the
    // second opens the campaign menu.
    InputSnapshot escape;escape.drawable_width=width;escape.drawable_height=height;
    escape.events={{InputEventType::EscapePressed}};
    if(!update(escape,width,height,0.,false))
      throw std::runtime_error("Developer smoke Escape closed the campaign.");
    if(developer_tools_.visible())
      throw std::runtime_error("Developer smoke Escape did not close the tools panel.");
    if(!menu_){
      if(!update(escape,width,height,0.,false))
        throw std::runtime_error("Developer smoke Escape closed the campaign.");
      if(!menu_)
        throw std::runtime_error("Developer smoke Escape did not reopen the menu.");
    }
    const auto click=[&](Point at){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=at;
      input.events={{InputEventType::LeftPressed,at},
                    {InputEventType::LeftReleased,at}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Developer smoke input closed the campaign.");
    };
    const auto menu_layout=NativeUiLayout::for_viewport(width,height,true);
    click({menu_layout.development_button.x+menu_layout.development_button.width*.5f,
           menu_layout.development_button.y+menu_layout.development_button.height*.5f});
    if(!development_menu_.visible())
      throw std::runtime_error("The DEVELOPMENT row did not reopen the submenu.");
    const auto submenu=
        native_development::development_menu_layout_for(width,height);
    click({submenu.new_button.x+submenu.new_button.width*.5f,
           submenu.new_button.y+submenu.new_button.height*.5f});
    if(mode_switch_ready())
      throw std::runtime_error("NEW DEVELOPER CAMPAIGN skipped its confirmation.");
    click({submenu.new_button.x+submenu.new_button.width*.5f,
           submenu.new_button.y+submenu.new_button.height*.5f});
    if(!mode_switch_ready()||!new_developer_seed_||*new_developer_seed_!=playable_demo_seed)
      throw std::runtime_error("The confirmed NEW DEVELOPER CAMPAIGN did not request the seeded world.");
  }
  // --developer-smoke phase D (fresh Developer session): the seeded world
  // activated with fresh provenance while the previous Developer save was
  // kept as the slot's .bak.
  void verify_developer_fresh_smoke(int width,int height){
    (void)width;(void)height;
    if(!session_->developer_mode())
      throw std::runtime_error("The fresh-campaign switch left Developer mode.");
    if(session_->frame().clock().speed()!=StrategicSpeed::Demo)
      throw std::runtime_error("The fresh Developer campaign did not resume at Demo speed.");
    if(session_->developer_tools_used())
      throw std::runtime_error("The fresh Developer campaign kept tools-used provenance.");
    if(!developer_save_exists())
      throw std::runtime_error("The fresh Developer campaign wrote no envelope save.");
    auto backup=developer_save_path();backup+=".bak";
    std::error_code error{};
    if(!std::filesystem::is_regular_file(backup,error))
      throw std::runtime_error("The previous Developer save was not kept as the slot backup.");
    developer_smoke_->fresh=true;developer_smoke_->backup=true;
  }
  [[nodiscard]] std::string developer_smoke_status()const{
    std::ostringstream out;
    out<<"{\"mode\":"<<(developer_smoke_->mode?1:0)
       <<",\"demo_speed\":"<<(developer_smoke_->demo?1:0)
       <<",\"tools_panel\":"<<(developer_smoke_->tools?1:0)
       <<",\"command\":"<<(developer_smoke_->command?1:0)
       <<",\"envelope_save\":"<<(developer_smoke_->save?1:0)
       <<",\"fresh_campaign\":"<<(developer_smoke_->fresh?1:0)
       <<",\"backup_kept\":"<<(developer_smoke_->backup?1:0)<<"}";
    return out.str();
  }
  // Mid-session setup cancellation hands the (already saved) session back so
  // the campaign can resume, matching the reference mode-select cancel.
  [[nodiscard]] std::unique_ptr<NativeCampaignSession> release_session(){return std::move(session_);}
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
       <<std::setprecision(6)<<found->transit_progress
       <<":hover="<<(smoke_fleet_hover_preview_?1:0)
       <<":inspect="<<(smoke_inspection_?1:0)
       <<":civilian="<<(smoke_civilian_recovery_?1:0)
       <<":locate="<<(smoke_fleet_located_?1:0)
       <<":overview="<<(smoke_overview_?1:0)
       <<":missions="<<smoke_missions_<<":"<<smoke_mission_count_
       <<":sites="<<smoke_mission_sites_<<":"<<smoke_mission_site_selection_
       <<":military="<<(smoke_military_orders_?1:0)<<":"
       <<(smoke_military_fleet_id_?*smoke_military_fleet_id_:-1);
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
      <<":focused="<<smoke_system_focused_
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
  [[nodiscard]] std::string surface_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<std::boolalpha<<"{\"mode\":\""<<(smoke_surface_reload_?"paused_reload":"ordered")<<"\",\"system_id\":"<<smoke_surface_system_id_<<",\"body_id\":"<<smoke_surface_body_id_<<",\"colony_id\":"<<smoke_surface_colony_id_<<",\"type_id\":\""<<smoke_surface_type_id_<<"\",\"site_id\":"<<smoke_surface_site_id_.value_or(-1)<<",\"x\":"<<smoke_surface_x_<<",\"z\":"<<smoke_surface_z_<<",\"rotation\":"<<smoke_surface_rotation_<<",\"authorization\":"<<smoke_surface_authorization_<<",\"refund\":"<<smoke_surface_refund_<<",\"treasury_before\":"<<smoke_surface_treasury_before_<<",\"treasury_after_cancel\":"<<smoke_surface_treasury_before_<<",\"treasury_after_place\":"<<smoke_surface_treasury_after_place_<<",\"treasury_after_refund\":"<<smoke_surface_treasury_after_refund_<<",\"treasury_saved\":"<<smoke_surface_treasury_saved_<<",\"site_count_before\":"<<smoke_surface_site_count_before_<<",\"site_count_saved\":"<<smoke_surface_site_count_saved_<<",\"progress\":"<<smoke_surface_progress_<<",\"before_days\":"<<smoke_surface_before_day_<<",\"saved_days\":"<<smoke_surface_saved_day_<<",\"palette_selected\":"<<smoke_surface_palette_selected_<<",\"ghost_previewed\":"<<smoke_surface_ghost_previewed_<<",\"placement_cancelled\":"<<smoke_surface_placement_cancelled_<<",\"cancel_no_change\":"<<smoke_surface_cancel_no_change_<<",\"placement_confirmed\":"<<smoke_surface_placement_confirmed_<<",\"removal_previewed\":"<<smoke_surface_removal_previewed_<<",\"removal_confirmed\":"<<smoke_surface_removal_confirmed_<<",\"refund_exact\":"<<smoke_surface_refund_exact_<<",\"managed\":"<<smoke_surface_managed_<<",\"persisted_site\":"<<smoke_surface_persisted_site_<<",\"scene_sprites\":"<<surface_workspace_.scene_cached_images()<<",\"relief_images\":"<<surface_workspace_.relief_cached_images()<<",\"paused\":"<<(session_->frame().clock().speed()==StrategicSpeed::Paused)<<'}';return out.str();}
  [[nodiscard]] std::string system_travel_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(9)<<std::boolalpha<<"{\"mode\":\""<<(smoke_system_travel_reload_?"paused_reload":"progress")<<"\",\"fleet_id\":"<<smoke_system_travel_fleet_id_.value_or(-1)<<",\"system_id\":"<<smoke_system_travel_system_id_.value_or(-1)<<",\"destination_id\":"<<smoke_system_travel_destination_id_.value_or(-1)<<",\"order_revision\":"<<smoke_system_travel_mission_revision_<<",\"lane_count\":"<<smoke_system_travel_lane_count_<<",\"before_x\":"<<smoke_system_travel_before_x_<<",\"before_y\":"<<smoke_system_travel_before_y_<<",\"after_x\":"<<smoke_system_travel_after_x_<<",\"after_y\":"<<smoke_system_travel_after_y_<<",\"before_days\":"<<smoke_system_travel_before_day_<<",\"after_days\":"<<smoke_system_travel_after_day_<<",\"selected\":"<<smoke_system_travel_selected_<<",\"canonical_moved\":"<<smoke_system_travel_canonical_moved_<<",\"rendered_moved\":"<<smoke_system_travel_rendered_moved_<<",\"paused_stable\":"<<smoke_system_travel_paused_stable_<<",\"pause_retained\":"<<smoke_system_travel_pause_retained_<<",\"known_arrow\":"<<smoke_system_travel_known_opened_<<",\"unknown_denied\":"<<smoke_system_travel_unknown_denied_<<",\"knowledge_unchanged\":"<<smoke_system_travel_knowledge_unchanged_<<",\"lanes_connected\":"<<smoke_system_travel_lanes_connected_<<'}';return out.str();}
  [[nodiscard]] bool wants_text_input() const noexcept {
    return (research_workspace_.wants_text_input() &&
            !shipyard_workspace_.visible() &&
            !construction_workspace_.visible()) ||
           development_menu_.wants_text_input();
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    pointer_=input.pointer;
    // VideoSettingsService preview rollback: an unconfirmed display preview
    // restores the previous settings once the 15s window lapses.
    if(video_rollback_&&video_rollback_remaining()<=0.)revert_video_preview();
    const auto hovered_action=NativeUiLayout::for_viewport(width,height).hit(pointer_,menu_);
    if(hovered_action!=hovered_action_){if(hovered_action!=UiAction::None)audio_mixer_.play_hover();hovered_action_=hovered_action;}
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
      battle_workspace_.discard_campaign();
      settlement_workspace_.discard_campaign();
      colony_entry_view_.reset();
      system_workspace_.discard_campaign();
      planet_discs_.discard_campaign();
      fleet_marker_offsets_.clear();
      if(voice_playback_)voice_playback_->reset_campaign();
      if(voice_bridge_)voice_bridge_->reset(session_->frame().runtime());
      pending_fleet_preview_.reset();
      refresh_fleets(true);
      if(research_workspace_.visible())refresh_research(true);
      if(shipyard_workspace_.visible())refresh_shipyard(true);
      if(construction_workspace_.visible())refresh_construction(true);
    }
    if(new_game_requested_&&session_->notice().kind==SessionNoticeKind::Failure)
      new_game_requested_=false;
    if(session_->exit_ready())return false;
    if(input.quit_requested)session_->request_exit();
    // A live unreconciled tactical encounter owns the screen (reference layer
    // 70). The frame's tactical clock drives snapshot refreshes at 10 Hz.
    {
      const auto &battle_world=session_->frame().runtime().world().campaign();
      const auto battle_active=battle_world.active_combat_encounter&&!battle_world.active_combat_encounter->reconciled;
      if(battle_active&&!battle_workspace_.visible())battle_workspace_.open(session_->frame().tactical_snapshot(),battle_world.player_civilization_id,width,height);
      else if(!battle_active&&battle_workspace_.visible())battle_workspace_.close();
      if(battle_workspace_.visible()){battle_refresh_elapsed_+=elapsed;if(battle_refresh_elapsed_>=.1){const auto passed=battle_refresh_elapsed_;battle_refresh_elapsed_=0.;battle_workspace_.set_snapshot(session_->frame().tactical_snapshot(),passed);battle_workspace_.set_tactical_speed(session_->frame().tactical_clock().speed_multiplier(),session_->frame().tactical_resume_speed());}}
    }
    const auto layout=NativeUiLayout::for_viewport(width,height,session_->developer_mode());
    for(const auto &event:input.events){
      if(battle_workspace_.visible()&&!menu_){
        const auto command=battle_workspace_.handle(event,width,height);
        execute_battle(command);
        if(command.captured)continue;
      }
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
           else if(command.kind==SystemWorkspaceCommandKind::open_construction){research_workspace_.close();shipyard_workspace_.close();diplomacy_workspace_.close();construction_workspace_.open();refresh_construction(true);if(!command.project_id.empty())construction_workspace_.select_project(command.project_id);}
          refresh_colony_entry(false);
          continue;
        }
      }
      if(menu_&&audio_settings_.visible()){
        const auto audio_result=audio_settings_.handle(event,width,height);
        if(audio_result.command==native_audio_settings::AudioSettingsCommand::Apply)
          audio_mixer_.apply_volumes(audio_result.values.master,audio_result.values.music,audio_result.values.sfx);
        else if(audio_result.command==native_audio_settings::AudioSettingsCommand::Close){
          audio_settings_.close();
          audio_mixer_.set_volumes(audio_result.values.master,audio_result.values.music,audio_result.values.sfx);
        }
        continue;
      }
      if(menu_&&voice_settings_view_.visible()){
        const auto voice_result=voice_settings_view_.handle(event,width,height);
        using VoiceCommand=native_voice_settings::VoiceSettingsCommand;
        if(voice_result.command==VoiceCommand::Apply)apply_voice_settings(voice_result.values);
        else if(voice_result.command==VoiceCommand::Replay){if(voice_playback_)voice_playback_->replay_last();}
        else if(voice_result.command==VoiceCommand::Stop){if(voice_playback_)voice_playback_->stop();}
        else if(voice_result.command==VoiceCommand::Close){apply_voice_settings(voice_result.values);voice_settings_view_.close();}
        continue;
      }
      if(menu_&&video_settings_view_.visible()){
        const auto video_result=video_settings_view_.handle(event,width,height);
        using VideoCommand=native_video_settings::VideoSettingsCommand;
        if(video_result.command==VideoCommand::Apply)begin_video_preview(video_result.values);
        else if(video_result.command==VideoCommand::Keep)keep_video_settings();
        else if(video_result.command==VideoCommand::Revert)revert_video_preview();
        else if(video_result.command==VideoCommand::Cancel){if(video_rollback_)revert_video_preview();video_settings_view_.close();}
        continue;
      }
      if(notification_view_.visible()&&!menu_){
        const auto result=notification_view_.handle(
            event,session_->notifications().items(),width,height);
        if(result.kind==native_notifications::NotificationViewCommandKind::OpenDiplomaticContact){
          research_workspace_.close();shipyard_workspace_.close();
          construction_workspace_.close();
          diplomacy_workspace_.open();
          refresh_diplomacy(true);
          diplomacy_workspace_.select_contact_civilization(result.civilization_id);
          refresh_diplomacy(true);
        }
        if(result.captured)continue;
      }
      if(menu_&&development_menu_.visible()){
        const auto command=development_menu_.handle(event,width,height);
        using DevelopmentCommand=
            native_development::DevelopmentMenuCommandKind;
        if(command.kind==DevelopmentCommand::OpenDeveloper){
          if(session_->developer_mode())session_->request_load();
          else request_developer_switch();
        }
        else if(command.kind==DevelopmentCommand::NewDeveloperCampaign)
          request_new_developer_campaign(command.seed);
        else if(command.kind==DevelopmentCommand::OpenTools){
          development_menu_.close();
          if(menu_)toggle_menu();
          if(session_->developer_mode())developer_tools_.open();
        }
        if(command.captured)continue;
      }
      if(developer_tools_.visible()&&!menu_){
        const auto command=developer_tools_.handle(event,width,height);
        if(command.kind==native_developer::DeveloperToolsCommandKind::Run)
          run_developer_command(command.command_id);
        if(command.captured)continue;
      }
      if(logistics_view_.visible()&&!menu_){
        const auto &campaign=session_->frame().runtime().world().campaign();
        if(logistics_view_.handle(
               event,native_logistics::build_home_logistics(
                        campaign,campaign.player_civilization_id),
               width,height))
          continue;
      }
      if(economy_view_.visible()&&!menu_){
        auto &runtime=session_->frame().runtime();
        const auto &campaign=runtime.world().campaign();
        const auto view=native_economy::build_economy_view(
            campaign,&runtime.research(),last_industry_allocation_);
        const auto command=
            economy_view_.handle(event,view,width,height);
        if(command.kind==native_economy::EconomyCommandKind::Close)
          economy_view_.close();
        else if(command.kind==native_economy::EconomyCommandKind::
                    SetIndustryPriority){
          // Reference UiSetIndustryPriority (Main.Economy.cs).
          const auto outcome=set_industry_priority(
              runtime.world().campaign().economies,
              campaign.player_civilization_id,campaign.player_civilization_id,
              command.priority);
          if(outcome.accepted){
            last_industry_allocation_.reset();
            session_->publish_notification("Economy",outcome.message);
          }
          session_->publish_status(outcome.message);
        }
        if(command.captured)continue;
      }
      if(missions_view_.visible()&&!menu_){
        const auto &campaign=session_->frame().runtime().world().campaign();
        const auto site_fleets=settlement_controller_.build(
            session_->frame(),session_->cache().generation);
        const auto colony_rows=
            native_missions::build_owned_colony_rows(campaign);
        const auto command=missions_view_.handle(
            event,native_missions::build_mission_board(campaign),site_fleets,
            colony_rows,width,height);
        if(command.kind==native_missions::MissionViewCommandKind::Close)
          missions_view_.close();
        else if(command.kind==native_missions::MissionViewCommandKind::
                    FocusFleet){
          const auto selected=fleet_controller_.select(
              session_->frame(),session_->cache().generation,
              command.fleet_id);
          if(selected.accepted){
            missions_view_.close();
            const auto &world=campaign;
            if(const auto fleet=std::ranges::find(world.fleets,command.fleet_id,
                                                  &FleetState::id);
               fleet!=world.fleets.end())
              camera_.center={fleet->position.x,fleet->position.y};
            session_->publish_status(selected.message);
          }
        }
        else if(command.kind==native_missions::MissionViewCommandKind::
                    OpenColony){
          missions_view_.close();
          open_overview_colony(command.colony_id,width,height);
        }
        else if(command.kind==native_missions::MissionViewCommandKind::
                    LandColony){
          // Reference UiOpenOwnedColony(colonyId, land:true).
          missions_view_.close();
          open_overview_colony(command.colony_id,width,height);
          open_surface(width,height);
        }
        else if(command.kind==native_missions::MissionViewCommandKind::
                    CollectOutpostFreight){
          // Reference UiRequestOutpostFreight: rescan for an idle freighter,
          // then issue the collection order through the coordinator.
          const auto row=std::ranges::find(
              colony_rows,command.colony_id,
              &native_missions::NativeMissionColonyRow::colony_id);
          if(row!=colony_rows.end()&&!row->can_request_freight)
            session_->publish_status(row->freight_reason.empty()
                ?"That outpost cannot receive a freight run."
                :row->freight_reason);
          else{
            auto &runtime=session_->frame().runtime();
            auto &simulation=runtime.world();
            const auto *freighter=native_missions::find_available_freighter(
                simulation.campaign());
            if(!freighter)
              session_->publish_status(
                  "No idle Interstellar Bulk Freighter is stationed at one "
                  "of your developed colonies.");
            else{
              const auto outcome=runtime.core().issue_freight_collection_order(
                  &simulation,simulation.campaign().player_civilization_id,
                  freighter->id,command.colony_id);
              session_->publish_status(outcome.message);
              if(outcome.accepted)refresh_fleets(true);
            }
          }
        }
        if(command.captured)continue;
      }

      if(event.type==InputEventType::EscapePressed){
        if(developer_tools_.visible())developer_tools_.close();
        else if(diplomacy_workspace_.modal_open())diplomacy_workspace_.dismiss_modal();
        else if(diplomacy_workspace_.visible())diplomacy_workspace_.close();
        else if(surface_workspace_.visible())surface_workspace_.close();
        else if(colony_workspace_.visible())colony_workspace_.close();
        else if(construction_workspace_.visible())construction_workspace_.close();
        else if(shipyard_workspace_.visible())shipyard_workspace_.close();
        else if(research_workspace_.visible())research_workspace_.close();
        else toggle_menu();
        continue;
      }
      // Reference keyboard shortcuts (Main.cs): Space pauses, 1-4 select a
      // strategic speed, F6 saves. Suppressed while the menu, surface view, or
      // diplomacy blocks gameplay input, or a text field owns the keyboard.
      if(event.type==InputEventType::KeyPressed&&!menu_&&
         !surface_workspace_.visible()&&!diplomacy_workspace_.visible()&&
         !wants_text_input()){
        auto &clock=session_->frame().clock();
        bool handled=true;
        switch(event.key){
          case ' ':
            if(clock.speed()==StrategicSpeed::Paused)clock.resume();
            else clock.set_speed(StrategicSpeed::Paused);
            break;
          case '1':clock.set_speed(StrategicSpeed::Normal);break;
          case '2':clock.set_speed(StrategicSpeed::Fast);break;
          case '3':clock.set_speed(StrategicSpeed::VeryFast);break;
          case '4':clock.set_speed(StrategicSpeed::Maximum);break;
          // Reference UiResumeAtSpeed: the Demo rate is Developer-only;
          // Player mode coerces the request to Normal.
          case '5':clock.set_speed(session_->developer_mode()?StrategicSpeed::Demo:StrategicSpeed::Normal);break;
          case 't':case 'T':cycle_research_candidate();break;
          case 'r':case 'R':start_research_candidate();break;
          case 'c':case 'C':cycle_construction_candidate();break;
          case 'b':case 'B':start_construction_candidate();break;
          case 'n':case 'N':request_new_game();break;
          case 0x4000003fu:session_->request_save();break; // SDLK_F6
          case 0x40000041u:export_support_bundle();break; // SDLK_F8
          default:handled=false;break;
        }
        if(handled)continue;
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
        if(fleet_command.kind==FleetWorkspaceCommandKind::OpenColony)
          open_overview_colony(fleet_command.colony_id,width,height);
        else handle_fleet_command(fleet_command);
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
        else if(action==UiAction::NewGame)request_new_game();
        else if(action==UiAction::Development)development_menu_.open();
        else if(action==UiAction::PlayerMode)request_player_switch();
        else if(action==UiAction::Audio)audio_settings_.open(audio_mixer_.settings());
        else if(action==UiAction::Voice){audio_settings_.close();voice_settings_view_.open(voice_settings_);}
        else if(action==UiAction::Video){audio_settings_.close();voice_settings_view_.close();video_settings_view_.open(video_settings_);}
        else if(action==UiAction::Support)export_support_bundle();
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
        else if(action==UiAction::Notifications){
          notification_view_.toggle(session_->notifications().latest_sequence());
          captured=true;
        }
        else if(action==UiAction::Logistics){
          logistics_view_.toggle();
          captured=true;
        }
        else if(action==UiAction::Missions){
          missions_view_.toggle();
          captured=true;
        }
        else if(action==UiAction::Economy){
          economy_view_.toggle();
          captured=true;
        }
        if(research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible()||diplomacy_workspace_.visible()||colony_workspace_.visible()||surface_workspace_.visible())captured=true;
        if(action!=UiAction::None)audio_mixer_.play(native_audio::NativeSfx::ui_confirm);
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.allows_world_drag())camera_.pan_pixels(event.delta.x,event.delta.y);gesture_.move(event.delta);update_fleet_hover_preview(event.position,width,height);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&!gesture_.captured_by_ui())camera_.zoom_at(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_||surface_workspace_.visible()||colony_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible())(void)gesture_.release_as_world_click();}
    }
    if(const auto request=surface_workspace_.take_preview_request())execute_surface(*request);
    if(research_workspace_.take_refresh_request())refresh_research(true);
    if(advance_simulation){
      const auto frame_result=session_->advance(menu_?0.:elapsed,timestamp);
      {
        const auto player=session_->frame()
                              .runtime().world().campaign()
                              .player_civilization_id;
        for(const auto &step:frame_result.strategic_results)
          for(const auto &allocation:step.core.industry_allocations)
            if(allocation.civilization_id==player)
              last_industry_allocation_=allocation;
      }
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
      if(voice_bridge_)
        voice_bridge_->observe(frame_result,session_->frame().runtime(),
                               session_->frame().clock().simulation_days(),
                               voice_settings_.effective_frequency());
      if(smoke_save_pending_){smoke_save_pending_=false;session_->request_save();}
    }
    // The opening line fires once per campaign once the menu is closed
    // (reference Main.Voice.cs TryEmitOpening).
    if(!menu_&&voice_bridge_)
      voice_bridge_->observe_opening(session_->frame().runtime(),
                                     session_->frame().clock().simulation_days(),
                                     voice_settings_.effective_frequency());
    if(voice_playback_){
      voice_playback_->update(elapsed);
      audio_mixer_.set_voice_ducking(voice_playback_->ducking());
    }
    // Reference RefreshNotifications: each newly published item plays its
    // category event sound.
    for(const auto &item:session_->notifications().items())
      if(item.sequence>last_played_notification_){
        last_played_notification_=item.sequence;
        audio_mixer_.play_event(item.category);
      }
    refresh_knowledge();
    return true;
  }

  [[nodiscard]] DrawList scene(int width,int height){
    const auto screen_height=static_cast<float>(height);
    DrawList out; std::optional<std::size_t> galaxy_marker_begin; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    if(system_workspace_.visible())system_workspace_.render(out,width,height);else{
    galaxy_backdrop_.append(out,{cache.generation,width,height,camera_,fitted_pixels_per_world_,true});
    if(territory_check_counter_--<=0){territory_check_counter_=30;const auto diplomacy_view=ObserverDiplomacyCommandService(session_->frame().runtime().diplomacy()).build_view(world.player_civilization_id);territory_overlay_.update(world,world.player_civilization_id,diplomacy_view.claims);}
    territory_overlay_.append(out,camera_,width,height,static_cast<float>(fitted_pixels_per_world_));
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    galaxy_marker_begin=out.world.size();
    for(const auto &system:world.systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);if(p.x<-14||p.y<-14||p.x>width+14||p.y>height+14)continue;const bool known=known_.contains(system.id),selected=selected_id_&&*selected_id_==system.id;NativeGalaxyStarAppearance appearance;const auto survey=world.knowledge.system_survey_level(world.player_civilization_id,system.id);if(survey==SystemSurveyLevel::fully_surveyed){if(system.primary)appearance.primary=galaxy_star_visual(*system.primary);if(system.secondary)appearance.secondary=galaxy_star_visual(*system.secondary);if(system.tertiary)appearance.tertiary=galaxy_star_visual(*system.tertiary);}galaxy_star_markers_.append(out,p,selected?4.2f:2.f,appearance,selected,UiRect{0,0,static_cast<float>(width),static_cast<float>(height)},known?1.f:.28f);if(selected||(known&&camera_.pixels_per_world>7.f))out.text.push_back({{p.x+8,p.y-4},known?system.name:"Unknown",{205,222,245,235}});}
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
    const auto layout = NativeUiLayout::for_viewport(width, height, session_->developer_mode());
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
    // Notification toggle with the unread badge (reference PlayerControls).
    fill(out, layout.notifications,
         notification_view_.visible()
             ? selected
             : layout.notifications.contains(pointer_) ? hover : button);
    stroke(out, layout.notifications,
           notification_view_.visible() ? Color{154, 225, 188, 255} : border);
    const auto unread =
        session_->notifications().unread_count(notification_view_.last_read());
    label(out, layout.notifications,
          unread > 99 ? "99+" : std::to_string(unread),
          unread > 0 ? Color{240, 197, 106, 255} : Color{225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.logistics,
         logistics_view_.visible()
             ? selected
             : layout.logistics.contains(pointer_) ? hover : button);
    stroke(out, layout.logistics,
           logistics_view_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.logistics, "SUPPLY", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.missions,
         missions_view_.visible()
             ? selected
             : layout.missions.contains(pointer_) ? hover : button);
    stroke(out, layout.missions,
           missions_view_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.missions, "MISSIONS", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    fill(out, layout.economy,
         economy_view_.visible()
             ? selected
             : layout.economy.contains(pointer_) ? hover : button);
    stroke(out, layout.economy,
           economy_view_.visible() ? Color{154, 225, 188, 255} : border);
    label(out, layout.economy, "ECONOMY", {225, 238, 250, 255},
          layout.control_font_pixels, layout.scale);
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y + 2.f * layout.scale},
        "Day " + std::to_string(static_cast<int>(
                     session_->frame().clock().simulation_days())),
        {154, 181, 211, 235}, layout.metric_font_pixels,
        layout.day_text.width, layout.day_text});
    if(selected_id_){const auto &campaign=session_->frame().runtime().world().campaign();const auto inspection=native_inspection::build_system_inspection(campaign,*selected_id_);inspection_card_bounds_=native_inspection::append_inspection_card(out,inspection,{14.f,static_cast<float>(screen_height)-88.f},std::clamp(std::min(width/1024.f,screen_height/600.f),.75f,1.25f));}else inspection_card_bounds_=std::nullopt;
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
      if (audio_settings_.visible()) {
        audio_settings_.render(out, width, height);
      } else if (voice_settings_view_.visible()) {
        voice_settings_view_.render(out, width, height);
      } else if (video_settings_view_.visible()) {
        video_settings_view_.render(out, width, height,
                                    video_rollback_remaining());
      } else if (development_menu_.visible()) {
        // Source: MainMenuLayer DevelopmentPanel — the menu body is replaced
        // by the Development submenu rather than floating above it.
        development_menu_.render(
            out,
            {session_->developer_mode(), developer_save_exists(),
             session_->notice().kind == SessionNoticeKind::Failure
                 ? std::string(session_->notice().message)
                 : std::string{}},
            width, height, &pointer_);
      } else {
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
      draw_button(layout.new_game_button, "NEW GAME");
      if (layout.developer_menu) {
        draw_button(layout.development_button, "DEVELOPMENT");
        draw_button(layout.player_button,
                    player_save_exists() ? "RESUME PLAYER" : "START PLAYER");
      } else {
        draw_button(layout.developer_button, "DEVELOPMENT");
      }
      draw_button(layout.audio_button, "AUDIO");
      draw_button(layout.voice_button, "VOICE");
      draw_button(layout.video_button, "VIDEO");
      draw_button(layout.support_button, "SUPPORT BUNDLE");
      draw_button(layout.exit_button, "EXIT TO WINDOWS");
      // Source CampaignModeLabel: mode + tools marker under the menu buttons.
      label(out, layout.mode_text,
            session_->developer_mode()
                ? (session_->developer_tools_used()
                       ? "DEVELOPER MODE · TOOLS USED"
                       : "DEVELOPER MODE · TOOLS UNUSED")
                : "PLAYER MODE",
            session_->developer_mode() ? Color{245, 197, 106, 255}
                                       : Color{126, 231, 200, 255},
            layout.metric_font_pixels, layout.scale);
      }
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
    if(battle_workspace_.visible()&&!menu_)battle_workspace_.render(out,width,height);
    // The recent-events panel is a HUD overlay (reference NotificationCenter
    // lives under PlayerControls): it floats above every workspace.
    if(!menu_)
      notification_view_.render(out,session_->notifications().items(),width,
                              height);
    if(!menu_&&logistics_view_.visible()){
      const auto &campaign=session_->frame().runtime().world().campaign();
      logistics_view_.render(
          out,native_logistics::build_home_logistics(
                  campaign,campaign.player_civilization_id),
          width,height);
    }
    if(!menu_&&economy_view_.visible()){
      auto &runtime=session_->frame().runtime();
      economy_view_.render(
          out,native_economy::build_economy_view(
                  runtime.world().campaign(),&runtime.research(),
                  last_industry_allocation_),
          width,height);
    }
    // Reference DeveloperToolsLayer: floating panel, closes when the campaign
    // menu opens or the session leaves Developer mode.
    if(!menu_&&developer_tools_.visible()){
      if(!session_->developer_mode())developer_tools_.close();
      else
        developer_tools_.render(
            out,{true,session_->developer_tools_used(),developer_result_,
                 developer_result_accepted_},
            width,height,&pointer_);
    }
    if(!menu_&&missions_view_.visible()){
      const auto &campaign=session_->frame().runtime().world().campaign();
      const auto site_fleets=settlement_controller_.build(
          session_->frame(),session_->cache().generation);
      missions_view_.render(
          out,native_missions::build_mission_board(campaign),site_fleets,
          native_missions::build_owned_colony_rows(campaign),width,height);
    }
    // Voice captions (reference VoiceCaptionDock): hidden while the menu or
    // diplomacy surface is open.
    if(voice_playback_&&voice_playback_->has_active_subtitle()&&!menu_&&
       !diplomacy_workspace_.visible()){
      const auto &voice=voice_playback_->settings();
      const auto scale=std::min(width/1280.f,height/720.f);
      const auto pixels=static_cast<int>(voice.subtitle_size*scale);
      const auto bar_width=std::min(720.f*scale,width-96.f*scale);
      const auto bar_height=68.f*scale;
      const UiRect bar{(width-bar_width)/2.f,height-bar_height-28.f*scale,
                       bar_width,bar_height};
      fill(out,bar,{8,13,22,static_cast<std::uint8_t>(190*voice.opacity)});
      stroke(out,bar,{116,174,225,160});
      auto caption_y=bar.y+8.f*scale;
      if(const auto speaker=voice_playback_->active_speaker_name();!speaker.empty()){
        out.overlay.emplace_back(Text{{bar.x+16.f*scale,caption_y},speaker,
                                      {240,197,106,255},pixels,bar_width-32.f*scale});
        caption_y+=(pixels+6.f)*scale;
      }
      out.overlay.emplace_back(Text{{bar.x+16.f*scale,caption_y},
                                    voice_playback_->active_subtitle(),
                                    {225,238,250,255},pixels,bar_width-32.f*scale});
    }
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
    if(command.kind==SurfaceWorkspaceCommandKind::UpgradeBuilding||
       command.kind==SurfaceWorkspaceCommandKind::RepairBuilding||
       command.kind==SurfaceWorkspaceCommandKind::SetBuildingEnabled||
       command.kind==SurfaceWorkspaceCommandKind::SetBuildingPriority||
       command.kind==SurfaceWorkspaceCommandKind::UpgradeHub){
      const auto &view=*surface_workspace_.view();
      NativeSurfaceCommandOutcome order;
      switch(command.kind){
      case SurfaceWorkspaceCommandKind::UpgradeBuilding:
        order=surface_controller_.upgrade_building(session_->frame(),generation,view,command.building_id);break;
      case SurfaceWorkspaceCommandKind::RepairBuilding:
        order=surface_controller_.repair_building(session_->frame(),generation,view,command.building_id);break;
      case SurfaceWorkspaceCommandKind::SetBuildingEnabled:
        order=surface_controller_.set_building_enabled(session_->frame(),generation,view,command.building_id,command.flag);break;
      case SurfaceWorkspaceCommandKind::SetBuildingPriority:
        order=surface_controller_.set_building_priority(session_->frame(),generation,view,command.building_id,command.flag);break;
      default:
        order=surface_controller_.upgrade_hub(session_->frame(),generation,view);break;
      }
      surface_workspace_.complete_command(visible_notice(order.message));
      refresh_surface(true);
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
    if(outcome.accepted){
      smoke_research_node_=command.node_id;
      session_->publish_notification("Research",outcome.message);
    }
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
    if(outcome.accepted)
      session_->publish_notification("Ships",outcome.message);
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
    if(outcome.accepted)
      session_->publish_notification("Construction",outcome.message);
    refresh_construction(true);
  }

  // T/R/C/B keyboard parity with the Godot presentation layer: candidate
  // cycling plus one-key start for research and construction. A candidate is a
  // project the controller reports as currently startable; the filtered list
  // ordering follows each view's own ordering rather than the reference's
  // plan-ranked ordering.
  void export_support_bundle(){
    if(!support_log_)return;
    const auto bundle=
        support_log_->export_bundle(session_->save_path());
    session_->publish_status("Support bundle exported: "+utf8_path(bundle));
  }
  [[nodiscard]] std::vector<const NativeResearchNode *>
  research_candidates(const NativeResearchWindow &view){
    std::vector<const NativeResearchNode *> candidates;
    for(const auto &node:view.nodes)
      if(node.primary_action.enabled&&
         node.primary_action.intent==NativeResearchIntent::Start)
        candidates.push_back(&node);
    return candidates;
  }
  void cycle_research_candidate(){
    const auto view=research_controller_.build(
        session_->frame(),session_->cache().generation,{});
    const auto candidates=research_candidates(view);
    if(candidates.empty()){
      research_candidate_index_=0;
      session_->publish_status(
          "No research choices are currently available. A construction "
          "prerequisite may be missing.");
      return;
    }
    research_candidate_index_=
        (research_candidate_index_+1)%
        static_cast<int>(candidates.size());
    session_->publish_status("Research candidate: "+
        candidates[research_candidate_index_]->display_name);
  }
  void start_research_candidate(){
    const auto view=research_controller_.build(
        session_->frame(),session_->cache().generation,{});
    const auto candidates=research_candidates(view);
    if(candidates.empty()){
      research_candidate_index_=0;
      session_->publish_status(
          "No available research project selected. Check construction "
          "prerequisites.");
      return;
    }
    research_candidate_index_=std::clamp(research_candidate_index_,0,
        static_cast<int>(candidates.size())-1);
    const auto *candidate=candidates[research_candidate_index_];
    const auto outcome=research_controller_.execute(
        session_->frame(),session_->cache().generation,
        view.research_revision,view.funding_revision,
        NativeResearchIntent::Start,candidate->id);
    session_->publish_status(outcome.message);
    if(outcome.accepted){
      session_->publish_notification("Research",outcome.message);
      research_candidate_index_=0;
    }
    if(research_workspace_.visible())refresh_research(true);
  }
  [[nodiscard]] std::vector<const NativeConstructionProject *>
  construction_candidates(const NativeConstructionView &view){
    std::vector<const NativeConstructionProject *> candidates;
    for(const auto &project:view.projects)
      if(project.start.enabled)candidates.push_back(&project);
    return candidates;
  }
  [[nodiscard]] bool construction_project_active(
      const NativeConstructionView &view){
    for(const auto &project:view.projects)
      if(project.active)return true;
    return false;
  }
  void cycle_construction_candidate(){
    const auto view=construction_controller_.build(
        session_->frame(),session_->cache().generation);
    if(construction_project_active(view)){
      construction_candidate_index_=0;
      session_->publish_status(
          "Complete the current construction project before selecting "
          "another.");
      return;
    }
    const auto candidates=construction_candidates(view);
    if(candidates.empty()){
      construction_candidate_index_=0;
      session_->publish_status(
          "No construction choices are currently available. Research may be "
          "required.");
      return;
    }
    construction_candidate_index_=
        (construction_candidate_index_+1)%
        static_cast<int>(candidates.size());
    session_->publish_status("Construction candidate: "+
        candidates[construction_candidate_index_]->name);
  }
  void start_construction_candidate(){
    const auto view=construction_controller_.build(
        session_->frame(),session_->cache().generation);
    if(construction_project_active(view)){
      construction_candidate_index_=0;
      session_->publish_status(
          "No available construction project selected.");
      return;
    }
    const auto candidates=construction_candidates(view);
    if(candidates.empty()){
      construction_candidate_index_=0;
      session_->publish_status(
          "No available construction project selected.");
      return;
    }
    construction_candidate_index_=std::clamp(construction_candidate_index_,0,
        static_cast<int>(candidates.size())-1);
    const auto *candidate=candidates[construction_candidate_index_];
    const auto outcome=construction_controller_.start(
        session_->frame(),session_->cache().generation,
        view.construction_revision,candidate->id);
    session_->publish_status(outcome.message);
    if(outcome.accepted){
      session_->publish_notification("Construction",outcome.message);
      construction_candidate_index_=0;
    }
    if(construction_workspace_.visible())refresh_construction(true);
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
        session_->frame(),session_->cache().generation,view->diplomacy_revision,
        command.action,command.target_civilization_id,command.proposal_id);
    diplomacy_workspace_.set_notice(outcome.message,outcome.accepted);
    last_diplomacy_command_accepted_=outcome.accepted;
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

  // Reference UiOpenOwnedColony(colonyId, land:false): enter the owning
  // system's orbital view already focused on the colony world.
  void open_overview_colony(int colony_id,int width,int height){
    const auto &campaign=session_->frame().runtime().world().campaign();
    const auto colony=std::ranges::find(campaign.colonies,colony_id,
                                        &Colony::id);
    if(colony==campaign.colonies.end()||!colony->planetary_body_id)return;
    notification_view_.close();
    if(!enter_system(colony->system_id,width,height)||
       !system_workspace_.select_body(*colony->planetary_body_id)){
      session_->publish_status(
          "The colony world is not available in the current orbital survey.");
      return;
    }
    refresh_colony_entry(true);
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
    fleet_workspace_.set_overview(native_overview::build_empire_overview(
        session_->frame().runtime().world().campaign(),selected_id_));
    fleet_refresh_elapsed_=0.;
  }

  // Reference UiFleetDestinationPreview: while an owned fleet is selected,
  // hovering a star continuously previews its route in the detail panel. The
  // hover preview is display-only — command_available stays false so CONFIRM
  // is never armed, and an armed right-click preview takes precedence.
  void update_fleet_hover_preview(Point pointer,int width,int height){
    // An armed right-click preview owns the panel; hover must not touch it.
    if(pending_fleet_preview_){
      hover_preview_target_.reset();hover_preview_shown_=false;return;
    }
    const bool blocked=menu_||surface_workspace_.visible()||
        colony_workspace_.visible()||research_workspace_.visible()||
        shipyard_workspace_.visible()||construction_workspace_.visible()||
        diplomacy_workspace_.visible()||notification_view_.visible()||
        logistics_view_.visible()||economy_view_.visible()||
        gesture_.captured_by_ui()||gesture_.allows_world_drag()||
        !fleet_workspace_.selected_fleet_id();
    const auto target=!blocked?system_hit(pointer,width,height):std::nullopt;
    if(target==hover_preview_target_){
      // refresh_fleets drops preview_ when the selection or order revision
      // changes; recompute instead of staying dark on the same target.
      if(target&&hover_preview_shown_&&!fleet_workspace_.preview())
        hover_preview_shown_=false;
      else return;
    }
    hover_preview_target_=target;
    if(!target||FleetWorkspaceLayout::for_viewport(width,height)
                  .panel.contains(pointer)){
      if(hover_preview_shown_){fleet_workspace_.clear_preview();hover_preview_shown_=false;}
      return;
    }
    auto preview=fleet_controller_.preview_selected_route(
        session_->frame(),session_->cache().generation,*target);
    preview.message=observer_safe_fleet_message(preview.message,
                                                observed_system_names());
    preview.command_available=false;
    fleet_workspace_.set_preview(std::move(preview),
                                 system_display_name(*target));
    hover_preview_shown_=true;
  }

  void handle_fleet_command(const FleetWorkspaceCommand &command){
    if(command.kind==FleetWorkspaceCommandKind::None)return;
    hover_preview_target_.reset();hover_preview_shown_=false;
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
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::HoldResume){
      const auto outcome=fleet_controller_.toggle_selected_civilian_hold(
          session_->frame(),session_->cache().generation);
      fleet_workspace_.set_notice(observer_safe_fleet_message(
                                      outcome.message,observed_system_names()),
                                  outcome.accepted);
      last_fleet_command_accepted_=outcome.accepted;
      refresh_fleets(true);
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::ReturnToBase){
      const auto outcome=fleet_controller_.request_selected_civilian_return(
          session_->frame(),session_->cache().generation,
          fleet_workspace_.civilian_return_pending());
      fleet_workspace_.set_civilian_return_pending(
          !outcome.accepted&&outcome.requires_confirmation);
      fleet_workspace_.set_notice(observer_safe_fleet_message(
                                      outcome.message,observed_system_names()),
                                  outcome.accepted||outcome.requires_confirmation);
      last_fleet_command_accepted_=outcome.accepted;
      refresh_fleets(true);
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Engage){
      const auto outcome=session_->frame().begin_tactical(command.fleet_id);
      fleet_workspace_.set_notice(observer_safe_fleet_message(
                                      outcome.message,observed_system_names()),
                                  outcome.accepted);
      if(outcome.accepted)
        session_->publish_notification("Combat",outcome.message);
      refresh_fleets(true);
    }
    // Reference UiIssueMilitaryOrder (non-tactical branch).
    if(command.kind==FleetWorkspaceCommandKind::MilitaryHold||
       command.kind==FleetWorkspaceCommandKind::MilitaryDefend||
       command.kind==FleetWorkspaceCommandKind::MilitaryRetreat){
      const auto type=command.kind==FleetWorkspaceCommandKind::MilitaryDefend
          ?MilitaryOrderType::Defend
          :command.kind==FleetWorkspaceCommandKind::MilitaryRetreat
               ?MilitaryOrderType::Retreat
               :MilitaryOrderType::Hold;
      const auto outcome=fleet_controller_.issue_selected_military_order(
          session_->frame(),session_->cache().generation,type);
      fleet_workspace_.set_notice(observer_safe_fleet_message(
                                      outcome.message,observed_system_names()),
                                  outcome.accepted);
      refresh_fleets(true);
      return;
    }
    // Reference UiFocusOwnedFleet: select + center the strategic map on the
    // fleet's authoritative position.
    if(command.kind==FleetWorkspaceCommandKind::Locate){
      const auto &campaign=session_->frame().runtime().world().campaign();
      if(const auto fleet=std::ranges::find(campaign.fleets,command.fleet_id,
                                            &FleetState::id);
         fleet!=campaign.fleets.end()){
        camera_.center={fleet->position.x,fleet->position.y};
        session_->publish_status(fleet->name+
            " selected. Right-click a destination to set its course.");
      }
      refresh_fleets(true);
      return;
    }
  }

  void execute_battle(const native_battle_ui::BattleWorkspaceCommand &command){
    using native_battle_ui::BattleWorkspaceCommandKind;
    auto &frame=session_->frame();
    switch(command.kind){
    case BattleWorkspaceCommandKind::IssueOrder:{
      if(command.order.formation_id<0){battle_workspace_.set_status("Select a friendly formation before issuing an order.",true);return;}
      const auto result=frame.issue_tactical_order(command.order);
      last_battle_order_accepted_=result.accepted;
      if(result.accepted)
        session_->publish_notification("Combat",result.message);
      battle_workspace_.set_status(result.message,!result.accepted);
      return;}
    case BattleWorkspaceCommandKind::TogglePause:
      frame.set_tactical_speed(frame.tactical_clock().speed_multiplier()>0.?0.:frame.tactical_resume_speed());
      battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());
      return;
    case BattleWorkspaceCommandKind::CycleSpeed:{
      const auto current=frame.tactical_resume_speed();
      double next=4.;
      for(const auto speed:MassiveCombatClock::allowed_speeds())if(speed>current+1e-9){next=speed;break;}
      if(next<=current+1e-9)next=.25;
      frame.set_tactical_speed(next);
      battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());
      return;}
    case BattleWorkspaceCommandKind::SetTacticalSpeed:
      frame.set_tactical_speed(command.speed);
      battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());
      return;
    case BattleWorkspaceCommandKind::Fit:return;
    case BattleWorkspaceCommandKind::Menu:toggle_menu();return;
    case BattleWorkspaceCommandKind::None:return;
    }
  }

  void fit_camera(int width,int height){if(galaxy_backdrop_.artwork_frame()){camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;return;}const auto &systems=session_->frame().runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void toggle_menu(){menu_=!menu_;auto &frame=session_->frame();frame.set_menu_open(menu_);audio_settings_.close();voice_settings_view_.close();video_settings_view_.close();notification_view_.close();development_menu_.close();developer_tools_.close();if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame.clock().speed();frame.clock().set_speed(StrategicSpeed::Paused);frame.pause_tactical_for_menu();}else{frame.resume_tactical_after_menu();frame.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=session_->frame().runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());if(galaxy_backdrop_.artwork_frame())galaxy_backdrop_.set_galactic_core_discovered(session_->cache().generation,world.knowledge.is_galactic_core_discovered(world.player_civilization_id));}
  void bind_galaxy_backdrop(int width,int height){const auto &world=session_->frame().runtime().world().campaign();GalaxyBackdropCatalog view;view.campaign_generation=session_->cache().generation;view.campaign_seed=world.seed;view.system_positions.reserve(world.systems.size());for(const auto &system:world.systems)view.system_positions.push_back({system.position.x,system.position.y});if(world.core){view.galactic_core=WorldPoint{world.core->position.x,world.core->position.y};view.galactic_core_exclusion_radius=world.core->exclusion_radius;view.galactic_core_discovered=world.knowledge.is_galactic_core_discovered(world.player_civilization_id);}galaxy_backdrop_.bind(std::move(view));camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;}
  // Reference UiResumeAtSpeed: Demo is reachable only in Developer mode;
  // Player mode coerces it to Normal and the cycle skips it entirely.
  void cycle_speed(){auto &clock=session_->frame().clock();const bool dev=session_->developer_mode();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;case StrategicSpeed::Maximum:next=dev?StrategicSpeed::Demo:StrategicSpeed::Normal;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  [[nodiscard]] std::string speed_text(){const auto &clock=session_->frame().clock();switch(clock.speed()==StrategicSpeed::Paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Fast:return "SPEED 2X";case StrategicSpeed::VeryFast:return "SPEED 3X";case StrategicSpeed::Maximum:return "SPEED 8X";default:return "SPEED 1X";}}
  void select(Point pointer,int width,int height){float best=10.f;std::optional<int> id;for(const auto &system:session_->frame().runtime().world().campaign().systems){const auto p=camera_.project({system.position.x,system.position.y},width,height);const auto d=std::hypot(p.x-pointer.x,p.y-pointer.y);if(d<best){best=d;id=system.id;}}selected_id_=id;}
  std::unique_ptr<NativeCampaignSession> session_;
  Window *window_{};
  Camera camera_;
  NativeGalaxyStarMarkerRenderer galaxy_star_markers_;
  NativeTerritoryOverlay territory_overlay_;
  int territory_check_counter_{};
  std::unordered_set<int> known_;
  std::optional<int> selected_id_;
  NativeResearchController research_controller_;
  NativeResearchWorkspace research_workspace_;
  NativeFleetController fleet_controller_;
  NativeFleetWorkspace fleet_workspace_;
  native_battle_ui::NativeBattleWorkspace battle_workspace_;
  double battle_refresh_elapsed_{};
  NativeShipyardController shipyard_controller_;
  NativeShipyardWorkspace shipyard_workspace_;
  NativeConstructionController construction_controller_;
  NativeConstructionWorkspace construction_workspace_;
  NativeDiplomacyController diplomacy_controller_;
  NativeDiplomacyWorkspace diplomacy_workspace_;
  std::filesystem::path asset_root_;
  native_audio::NativeAudioMixer audio_mixer_;
  native_audio::NativeAudioDevice audio_device_;
  // Native voice pipeline (VoicePlaybackController + GameplayVoiceEventBridge
  // port): profile registry → role resolver → event router → playback → the
  // mixer's dedicated dialogue voice. Optional members stay empty when the
  // packaged voice catalogue is absent, which disables voice entirely.
  native_voice::NativeVoiceSettings voice_settings_;
  std::filesystem::path voice_settings_path_;
  native_voice_settings::NativeVoiceSettingsView voice_settings_view_;
  // Live-applies a sanitized settings payload to playback, the dialogue gain
  // and the persisted voice-settings.json (the reference ApplySettings port).
  void apply_voice_settings(native_voice::NativeVoiceSettings values){
    voice_settings_=values.sanitized();
    if(voice_playback_)voice_playback_->apply_settings(voice_settings_);
    audio_mixer_.set_dialogue_volume(voice_settings_.volume);
    if(!voice_settings_path_.empty())voice_settings_.save(voice_settings_path_);
  }
  // VideoSettingsService port: applies a display payload to the window. The
  // reference's Apply → 15s CONFIRM DISPLAY → Keep/Revert rollback flow is
  // driven by begin_video_preview/keep_video_settings/revert_video_preview.
  native_video_settings::NativeVideoSettings video_settings_;
  std::filesystem::path video_settings_path_;
  native_video_settings::NativeVideoSettingsView video_settings_view_;
  struct VideoRollback {
    native_video_settings::NativeVideoSettings previous;
    std::chrono::steady_clock::time_point deadline;
  };
  std::optional<VideoRollback> video_rollback_;
  void apply_video_settings(native_video_settings::NativeVideoSettings values){
    video_settings_=values.sanitized();
    using native_video_settings::VideoDisplayMode;
    using native_video_settings::VideoFrameCap;
    using native_video_settings::VideoVsync;
    const int sdl_vsync=video_settings_.vsync==VideoVsync::Off?0
        :video_settings_.vsync==VideoVsync::Adaptive?SDL_RENDERER_VSYNC_ADAPTIVE:1;
    bool ok=true;
    if(window_){
      ok=window_->set_vsync(sdl_vsync)&&ok;
      ok=window_->set_fullscreen(video_settings_.display==VideoDisplayMode::Exclusive)&&ok;
      double cap=0.;
      switch(video_settings_.frame_cap){
        case VideoFrameCap::Fps60:cap=60.;break;
        case VideoFrameCap::Fps120:cap=120.;break;
        case VideoFrameCap::Fps144:cap=144.;break;
        default:break;
      }
      window_->set_frame_cap(cap);
    }
    if(!ok)video_settings_view_.set_error(
        "The display driver rejected part of these settings; the preview "
        "continues with what applied.");
  }
  void begin_video_preview(native_video_settings::NativeVideoSettings values){
    if(!video_rollback_)
      video_rollback_=VideoRollback{video_settings_,std::chrono::steady_clock::now()+std::chrono::seconds(15)};
    apply_video_settings(std::move(values));
    video_settings_view_.set_confirming(true);
  }
  void keep_video_settings(){
    video_rollback_.reset();
    video_settings_view_.set_confirming(false);
    if(!video_settings_path_.empty())video_settings_.save(video_settings_path_);
    video_settings_view_.close();
  }
  void revert_video_preview(){
    if(video_rollback_)apply_video_settings(video_rollback_->previous);
    video_rollback_.reset();
    video_settings_view_.set_confirming(false);
  }
  [[nodiscard]] double video_rollback_remaining()const{
    if(!video_rollback_)return 0.;
    return std::chrono::duration<double>(video_rollback_->deadline-std::chrono::steady_clock::now()).count();
  }
  native_voice::NativeVoiceProfileRegistry voice_profiles_;
  std::optional<UiRect> inspection_card_bounds_;
  std::optional<native_voice::NativeCharacterVoiceResolver> voice_resolver_;
  std::optional<native_voice::NativeVoiceRouter> voice_router_;
  std::optional<native_voice::NativeVoiceCache> voice_cache_;
  std::optional<native_voice::NativeVoicePlayback> voice_playback_;
  std::optional<native_voice::NativeGameplayVoiceBridge> voice_bridge_;
  native_audio_settings::NativeAudioSettingsView audio_settings_;
  native_notifications::NativeNotificationView notification_view_;
  native_logistics::NativeLogisticsView logistics_view_;
  native_economy::NativeEconomyPanel economy_view_;
  // Reference Main._lastPlayerIndustryAllocation: presentation-side cache of
  // the player's most recent industry allocation, cleared when a new
  // priority is chosen.
  std::optional<CivilizationIndustryAllocation> last_industry_allocation_;
  native_missions::NativeMissionView missions_view_;
  std::int64_t last_played_notification_{};
  std::unordered_map<std::string,std::shared_ptr<const RgbaImage>> diplomacy_portraits_;
  NativeDiplomacyWorkspace::PortraitProvider diplomacy_portrait_provider_ =
      [this](std::string_view relative){
        auto [entry,inserted]=diplomacy_portraits_.try_emplace(std::string(relative));
        if(inserted){
          try{entry->second=decode_rgba_image(asset_root_/entry->first);}
          catch(...){entry->second.reset();}
        }
        return entry->second;
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
  NativePlanetDiscAssets planet_discs_;
  NativeShipArtAssets ship_art_;
  NativeSystemWorkspace system_workspace_;
  std::vector<FleetMarkerOffset> fleet_marker_offsets_;
  std::optional<NativeFleetRoutePreview> pending_fleet_preview_;
  std::optional<int> hover_preview_target_;
  bool hover_preview_shown_{};
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
  int research_candidate_index_{};
  int construction_candidate_index_{};
  bool smoke_shortcut_{};
  int smoke_audio_support_{};
  int smoke_voice_settings_{};
  int smoke_video_settings_{};
  std::optional<native_support::NativeSupportLog> support_log_;
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
  bool last_diplomacy_command_accepted_{};
  bool last_battle_order_accepted_{};
  std::size_t smoke_battle_formations_{},smoke_battle_own_{},
      smoke_battle_foreign_{},smoke_battle_redacted_{},
      smoke_battle_vessels_hidden_{},smoke_battle_own_inexact_{},
      smoke_battle_selected_{},smoke_battle_events_{},smoke_battle_salvos_{};
  int smoke_battle_tokens_{};
  std::int64_t smoke_battle_tick_{};
  std::size_t smoke_diplomacy_contacts_{},smoke_diplomacy_identified_{},
      smoke_diplomacy_unidentified_{},smoke_diplomacy_redacted_{},
      smoke_diplomacy_channels_{},smoke_diplomacy_agreements_{},
      smoke_diplomacy_history_{},smoke_diplomacy_proposals_{},
      smoke_diplomacy_pending_after_{},smoke_diplomacy_incoming_{};
  int smoke_diplomacy_selected_civ_{-1},smoke_diplomacy_last_system_{-1};
  std::int64_t smoke_diplomacy_proposal_id_{-1};
  bool smoke_diplomacy_portrait_{};
  bool smoke_notification_panel_{};
  DeveloperSmokeEvidence developer_smoke_owned_{};
  DeveloperSmokeEvidence *developer_smoke_{&developer_smoke_owned_};
  bool smoke_logistics_panel_{},smoke_economy_panel_{},
      smoke_economy_toggled_{};
  int smoke_logistics_ready_{},smoke_logistics_nodes_{},
      smoke_logistics_corridors_{};
  int smoke_economy_ready_{},smoke_economy_cards_{},
      smoke_economy_flow_rows_{},smoke_economy_priority_{};
  double smoke_logistics_supply_{},smoke_logistics_demand_{},
      smoke_logistics_delivered_{},smoke_logistics_shortfall_{};
  int smoke_notification_items_{},smoke_notification_unread_{},
      smoke_notification_diplomacy_{},smoke_notification_contact_{-1},
      smoke_notification_focused_{-1};
  std::optional<int> smoke_fleet_id_;
  bool smoke_fleet_hover_preview_{},smoke_inspection_{},
       smoke_fleet_located_{},smoke_military_orders_{},
      smoke_civilian_recovery_{},smoke_overview_{};
  std::optional<int> smoke_military_fleet_id_;
  int smoke_missions_{},smoke_mission_count_{},smoke_mission_sites_{},
      smoke_mission_site_selection_{};
  std::optional<int> smoke_fleet_destination_;
  bool smoke_system_entered_{},smoke_system_hit_{},smoke_system_panned_{},smoke_system_zoomed_{},smoke_system_reset_{},smoke_system_back_{},smoke_system_pause_retained_{},smoke_system_speed_retained_{},smoke_system_gesture_cleared_{},smoke_system_focused_{};
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
  bool smoke_surface_mode_{},smoke_surface_reload_{},smoke_surface_palette_selected_{},smoke_surface_ghost_previewed_{},smoke_surface_placement_cancelled_{},smoke_surface_cancel_no_change_{},smoke_surface_placement_confirmed_{},smoke_surface_removal_previewed_{},smoke_surface_removal_confirmed_{},smoke_surface_refund_exact_{},smoke_surface_persisted_site_{},smoke_surface_managed_{};
  int smoke_surface_system_id_{},smoke_surface_body_id_{},smoke_surface_colony_id_{};std::optional<int> smoke_surface_site_id_;std::string smoke_surface_type_id_;std::size_t smoke_surface_site_count_before_{},smoke_surface_site_count_saved_{};float smoke_surface_x_{},smoke_surface_z_{},smoke_surface_rotation_{};double smoke_surface_authorization_{},smoke_surface_refund_{},smoke_surface_treasury_before_{},smoke_surface_treasury_after_place_{},smoke_surface_treasury_after_refund_{},smoke_surface_treasury_saved_{},smoke_surface_progress_{},smoke_surface_before_day_{},smoke_surface_saved_day_{};
  bool menu_{};bool new_game_requested_{};bool mode_switch_requested_{};bool smoke_save_pending_{};int smoke_audio_settings_{};Point pointer_{};PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
  std::optional<std::int64_t> new_developer_seed_{};
  native_development::NativeDevelopmentMenu development_menu_;
  native_developer::NativeDeveloperToolsPanel developer_tools_;
  std::string developer_result_{};bool developer_result_accepted_{};
  std::filesystem::path player_save_path_{};
  UiAction hovered_action_{UiAction::None};
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
    const StartupEntryConfig startup_config{{asset_root/"Data/research/v1",asset_root/"Data/astronomy/hyg-nearby-500-v1.json",options.save_path,STELLAR_GAME_VERSION},asset_root,utc_timestamp};
    std::unique_ptr<NativeCampaignSession> session;
    StartupEntryEvidence startup_evidence,restart_evidence;
    std::optional<std::filesystem::path> generated_save_path,setup_screenshot,loading_screenshot,restart_save_path;
    bool new_game_restart{};
    bool developer_switched{},developer_fresh{};
    DeveloperSmokeEvidence developer_evidence;
    if(options.new_game_restart_smoke&&!std::filesystem::is_regular_file(options.save_path))
      throw std::invalid_argument("--new-game-restart-smoke requires a preexisting save-path anchor.");
    if(options.new_game_smoke){
      if(!std::filesystem::is_regular_file(options.save_path))throw std::invalid_argument("--new-game-smoke requires a preexisting save-path anchor.");
      setup_screenshot=sidecar_path(*options.smoke_screenshot,L"-setup");
      loading_screenshot=sidecar_path(*options.smoke_screenshot,L"-loading");
      StartupEntryAutomation automation{std::to_string(options.seed),"pelagic_high_pressure",250,*setup_screenshot,*loading_screenshot};
      auto result=run_native_startup_entry(window,startup_config,&automation);
      if(result.exit_requested||!result.session)throw std::runtime_error("Automated new campaign startup did not activate a session.");
      startup_evidence=std::move(result.evidence);generated_save_path=result.session->save_path();
      if(*generated_save_path==options.save_path)throw std::runtime_error("New campaign startup overwrote the requested save anchor.");
      session=std::move(result.session);
    }else if(options.load||options.smoke_screenshot){session=make_session(options);}
    for(;;){
    if(!session){
      auto result=run_native_startup_entry(window,startup_config);
      if(result.exit_requested)return 0;
      if(!result.session)throw std::runtime_error("Startup ended without a campaign session.");
      session=std::move(result.session);
    }
    NativeCampaign campaign(std::move(session),window,window.drawable_width(),window.drawable_height(),options.asset_root,
                             [&window](const Text &label){return window.measure_text(label);},
                             options.save_path);
    campaign.adopt_developer_smoke_evidence(developer_evidence);
    campaign.enable_support_log({STELLAR_GAME_VERSION,SDL_GetPlatform(),
                                 window.gpu_driver(),
                                 SDL_GetNumLogicalCPUCores(),
                                 SDL_GetSystemRAM(),1});
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
      else if(options.ship_art_smoke)
        campaign.prepare_ship_art_smoke(window.drawable_width(),
                                        window.drawable_height());
      else if(options.diplomacy_smoke)
        campaign.prepare_diplomacy_smoke(window.drawable_width(),
                                         window.drawable_height());
      else if(options.notification_smoke)
        campaign.prepare_notification_smoke(window.drawable_width(),
                                            window.drawable_height());
      else if(options.logistics_smoke)
        campaign.prepare_logistics_smoke(window.drawable_width(),
                                         window.drawable_height());
      else if(options.economy_smoke)
        campaign.prepare_economy_smoke(window.drawable_width(),
                                       window.drawable_height());
      else if(options.battle_smoke)
        campaign.prepare_battle_smoke(window.drawable_width(),
                                      window.drawable_height());
      else if(options.audio_smoke)
        campaign.prepare_audio_smoke(window.drawable_width(),
                                     window.drawable_height());
      else if(options.developer_smoke){
        if(developer_fresh)
          campaign.verify_developer_fresh_smoke(window.drawable_width(),
                                              window.drawable_height());
        else if(developer_switched){
          campaign.prepare_developer_tools_smoke(window.drawable_width(),
                                                 window.drawable_height());
          campaign.prepare_developer_fresh_smoke(window.drawable_width(),
                                                 window.drawable_height());
        }else
          campaign.prepare_developer_smoke(window.drawable_width(),
                                           window.drawable_height());
      }
      else if(options.new_game_restart_smoke){/* the restart smoke drives N,
        the mid-session save, and the second campaign's manual save itself. */}
      else
        campaign.prepare_smoke_ui();
    }
    const auto startup_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-startup_begin).count();
    auto prior=std::chrono::steady_clock::now();int frames=0;std::vector<double> frame_ms;std::vector<double> cpu_ms;std::vector<double> draw_ms;bool discard_elapsed{};bool new_game=false;bool mode_switch=false;
    while(true){
      const auto now=std::chrono::steady_clock::now();
      const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();
      prior=now;
      const auto input=window.poll();
      if(!input.renderable()){
        discard_elapsed=true;
        if(!campaign.update(input,input.drawable_width,input.drawable_height,0.,false))break;
        if(campaign.new_game_ready()){new_game=true;break;}
        if(campaign.mode_switch_ready()){mode_switch=true;break;}
        window.set_text_input(campaign.wants_text_input());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }
      const auto elapsed=discard_elapsed?0.:measured_elapsed;
      if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);
      discard_elapsed=false;
      const auto cpu_begin=std::chrono::steady_clock::now();
      if(!campaign.update(input,input.drawable_width,input.drawable_height,
                          elapsed))break;
      if(campaign.new_game_ready()){new_game=true;break;}
      if(campaign.mode_switch_ready()){mode_switch=true;break;}
      if(options.new_game_restart_smoke){
        if(!new_game_restart&&frames==55&&!campaign.send_key('n',input.drawable_width,input.drawable_height))
          throw std::runtime_error("New Game restart smoke key input closed the campaign.");
        if(new_game_restart&&frames==60)campaign.request_smoke_save();
      }
      window.set_text_input(campaign.wants_text_input());
      if(options.smoke_screenshot){
        ++frames;
        if((options.research_smoke||options.fleet_smoke||options.shipyard_smoke||options.construction_smoke||options.system_smoke||options.system_travel_smoke||options.system_travel_reload_smoke||options.colony_smoke||options.colony_reload_smoke||options.settlement_smoke||options.settlement_reload_smoke||options.surface_smoke||options.surface_reload_smoke||options.galaxy_art_smoke||options.ship_art_smoke||options.diplomacy_smoke||options.battle_smoke||options.notification_smoke||options.logistics_smoke||options.economy_smoke||options.developer_smoke)&&frames==60)
          campaign.request_smoke_save();
      }
      std::optional<std::filesystem::path> screenshot;
      if(options.smoke_screenshot){
        if(options.galaxy_art_smoke){
          if(frames==120)screenshot=options.smoke_screenshot;
          else if(frames==121)screenshot=sidecar_path(*options.smoke_screenshot,L"-regional");
          else if(frames==122)screenshot=sidecar_path(*options.smoke_screenshot,L"-system");
        }else if(options.ship_art_smoke){
          if(frames==120)screenshot=options.smoke_screenshot;
          else if(frames==121)screenshot=sidecar_path(*options.smoke_screenshot,L"-map");
        }else if(options.diplomacy_smoke){
          if(frames==120)screenshot=options.smoke_screenshot;
          else if(frames==121)screenshot=sidecar_path(*options.smoke_screenshot,L"-proposals");
        }else if(options.notification_smoke){
          if(frames==120)screenshot=options.smoke_screenshot;
          else if(frames==121)screenshot=sidecar_path(*options.smoke_screenshot,L"-contact");
        }else if(frames>=120)screenshot=options.smoke_screenshot;
      }
      auto draw_list=campaign.scene(input.drawable_width,input.drawable_height);
      const auto cpu_end=std::chrono::steady_clock::now();
      window.draw(draw_list,screenshot);
      if(frames>0){
        cpu_ms.push_back(std::chrono::duration<double,std::milli>(cpu_end-cpu_begin).count());
        draw_ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_begin).count());
      }
      if(options.galaxy_art_smoke){
        if(frames==120){campaign.capture_galaxy_overview(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_regional(input.drawable_width,input.drawable_height);}
        else if(frames==121){campaign.capture_galaxy_regional(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_system(input.drawable_width,input.drawable_height);}
        else if(frames==122)campaign.capture_galaxy_system(input.drawable_width,input.drawable_height);
      }
      if(options.ship_art_smoke){
        if(frames==120)campaign.capture_ship_art_shipyard();
        else if(frames==121)campaign.capture_ship_art_map();
      }
      if(options.diplomacy_smoke){
        if(frames==120){campaign.capture_diplomacy_workspace();
          campaign.prepare_diplomacy_proposals(input.drawable_width,
                                               input.drawable_height);}
        else if(frames==121)campaign.capture_diplomacy_proposals();
      }
      if(options.notification_smoke){
        if(frames==120){campaign.capture_notification_panel();
          campaign.prepare_notification_contact(input.drawable_width,
                                                input.drawable_height);}
        else if(frames==121)campaign.capture_notification_contact();
      }
      if(options.logistics_smoke&&frames==120)
        campaign.capture_logistics_panel();
      if(options.economy_smoke&&frames==120)
        campaign.capture_economy_panel();
      if(options.battle_smoke&&frames==120)campaign.capture_battle_workspace();
      const bool capture=options.smoke_screenshot&&(options.galaxy_art_smoke?frames>=123:options.ship_art_smoke?frames>=122:options.diplomacy_smoke?frames>=122:options.notification_smoke?frames>=122:frames>=120);
      if(capture){
        if(!campaign.smoke_save_succeeded())
          throw std::runtime_error("Native session smoke did not complete its manual save.");
        std::ranges::sort(frame_ms);
        const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);
        const auto p95=frame_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(frame_ms.size())*.95))-1];
        std::ranges::sort(cpu_ms);
        const auto cpu_total=std::accumulate(cpu_ms.begin(),cpu_ms.end(),0.);
        const auto cpu_p95=cpu_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(cpu_ms.size())*.95))-1];
        std::ranges::sort(draw_ms);
        const auto draw_total=std::accumulate(draw_ms.begin(),draw_ms.end(),0.);
        const auto draw_p95=draw_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(draw_ms.size())*.95))-1];
        std::cout<<std::fixed<<std::setprecision(3)
                 <<"native-map smoke ok: gpu_driver="<<window.gpu_driver()
                 <<" presentation="<<window.presentation_mode()
                 <<" drawable="<<window.drawable_width()<<"x"
                 <<window.drawable_height()
                 <<" systems="<<campaign.system_count()<<" frames="<<frames
                 <<" startup_ms="<<startup_ms
                 <<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())
                 <<" frame_p95_ms="<<p95
                 <<" cpu_mean_ms="<<cpu_total/static_cast<double>(cpu_ms.size())
                 <<" cpu_p95_ms="<<cpu_p95
                 <<" draw_mean_ms="<<draw_total/static_cast<double>(draw_ms.size())
                 <<" draw_p95_ms="<<draw_p95
                 <<" image_uploads="<<window.image_upload_count()<<" save=ok screenshot="
                 <<utf8_path(*options.smoke_screenshot)
                 <<" territory="<<campaign.territory_smoke_status();
        if(options.research_smoke)
          std::cout<<" research="<<campaign.research_smoke_status()
                   <<" shortcut="<<(campaign.shortcut_smoke_succeeded()?1:0);
        if(options.fleet_smoke)
          std::cout<<" fleet="<<campaign.fleet_smoke_status();
        if(options.shipyard_smoke)
          std::cout<<" shipyard="<<campaign.shipyard_smoke_status();
        if(options.construction_smoke)
          std::cout<<" construction="<<campaign.construction_smoke_status()
                   <<" shortcut="<<(campaign.shortcut_smoke_succeeded()?1:0);
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
        if(options.diplomacy_smoke)
          std::cout<<" diplomacy="<<campaign.diplomacy_smoke_status();
        if(options.notification_smoke)
          std::cout<<" notifications="<<campaign.notification_smoke_status();
        if(options.logistics_smoke)
          std::cout<<" logistics="<<campaign.logistics_smoke_status();
        if(options.economy_smoke)
          std::cout<<" economy="<<campaign.economy_smoke_status();
        if(options.developer_smoke)
          std::cout<<" developer="<<campaign.developer_smoke_status();
        if(options.battle_smoke)
          std::cout<<" battle="<<campaign.battle_smoke_status();
        if(options.audio_smoke)
          std::cout<<" audio="<<campaign.audio_smoke_status();
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
        if(options.new_game_restart_smoke){
          std::cout<<" new_game_restart={\"saved_previous\":true,\"restarted\":"<<(new_game_restart?"true":"false")
            <<",\"entry_opened\":"<<(restart_evidence.entry_opened?"true":"false")
            <<",\"setup_opened\":"<<(restart_evidence.setup_opened?"true":"false")
            <<",\"species_selected\":"<<(restart_evidence.species_selected?"true":"false")
            <<",\"size_selected\":"<<(restart_evidence.size_selected?"true":"false")
            <<",\"seed_entered\":"<<(restart_evidence.seed_entered?"true":"false")
            <<",\"create_requested\":"<<(restart_evidence.create_requested?"true":"false")
            <<",\"activated\":true,\"system_count\":"<<campaign.system_count()
            <<",\"species_id\":"<<json_string(campaign.player_species_id())
            <<",\"seed\":"<<json_string(std::to_string(options.seed+1))
            <<",\"generated_save_path\":"<<json_string(utf8_path(restart_save_path.value_or({})))
            <<",\"previous_save_path\":"<<json_string(utf8_path(options.save_path))
            <<",\"unique_slot\":"<<((restart_save_path&&*restart_save_path!=options.save_path)?"true":"false")<<"}";
        }
        std::cout<<'\n';
        break;
      }
    }
    if(mode_switch){
      // Source UiSwitchToDeveloperMode/UiSwitchToPlayerMode: the outgoing
      // campaign already checkpointed; the target loads-or-creates against
      // its own slot, resumes at its mode speed, then checkpoints itself.
      // A pending world seed means NEW DEVELOPER CAMPAIGN: always switch to a
      // fresh Developer world, even when the current session is Developer.
      const auto fresh_seed=campaign.new_developer_seed();
      const bool to_developer=fresh_seed||!campaign.developer_mode();
      const auto player_path=campaign.player_save_path();
      auto previous=campaign.release_session();
      try{
        auto next=build_switched_session(
            to_developer,startup_config.host.research_root,
            startup_config.host.catalog_path,player_path,fresh_seed);
        next->frame().clock().set_speed(to_developer?StrategicSpeed::Demo
                                                    :StrategicSpeed::Normal);
        (void)next->checkpoint_now(utc_timestamp());
        session=std::move(next);
        if(to_developer){
          if(developer_switched)developer_fresh=true;
          developer_switched=true;
        }
      }catch(const std::exception &error){
        std::cerr<<"Stellar Continuum native client: mode switch failed: "
                 <<error.what()<<'\n';
        session=std::move(previous);
      }
      continue;
    }
    if(!new_game)return 0;
    // Reference RequestNewCampaign: the previous campaign is saved (gated
    // above), then sandbox setup runs; cancelling resumes the saved campaign.
    StartupEntryAutomation restart_automation;const StartupEntryAutomation *automation=nullptr;
    if(options.new_game_restart_smoke){
      const auto restart_setup=sidecar_path(*options.smoke_screenshot,L"-restart-setup");
      const auto restart_loading=sidecar_path(*options.smoke_screenshot,L"-restart-loading");
      restart_automation={std::to_string(options.seed+1),"pelagic_high_pressure",250,restart_setup,restart_loading};
      automation=&restart_automation;
    }
    auto restart_result=run_native_startup_entry(window,startup_config,automation);
    if(restart_result.session){
      new_game_restart=true;
      restart_evidence=restart_result.evidence;
      restart_save_path=restart_result.session->save_path();
      session=std::move(restart_result.session);
    }else{
      session=campaign.release_session();
    }
    }
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
