#include "map_camera.hpp"
#include "native_audio_director.hpp"
#include "native_voice_caption.hpp"
#include "native_campaign_feedback.hpp"
#include "native_surface_art_assets.hpp"
#include "native_surface_status.hpp"
#include "native_audio_settings.hpp"
#include "native_settings_hub.hpp"
#include "native_voice_settings.hpp"
#include "native_general_settings.hpp"
#include "native_video_controller.hpp"
#include "native_video_settings_smoke.hpp"
#include "native_audio_settings_smoke.hpp"
#include "map_interaction.hpp"
#include "native_galaxy_star_markers.hpp"
#include "native_navigation_art.hpp"
#include "native_galaxy_labels.hpp"
#include "native_campaign_session.hpp"
#include "native_notification_events.hpp"
#include "native_support_service.hpp"
#include "native_battle_workspace.hpp"
#include "native_campaign_calendar.hpp"
#include "native_colony_controller.hpp"
#include "native_colony_workspace.hpp"
#include "native_colony_roster.hpp"
#include "native_settlement_mission_controller.hpp"
#include "native_settlement_workspace.hpp"
#include "native_surface_construction_controller.hpp"
#include "native_surface_workspace.hpp"
#include "native_surface_building_presentation.hpp"
#include "native_construction_controller.hpp"
#include "native_construction_workspace.hpp"
#include "native_diplomacy_controller.hpp"
#include "native_diplomacy_workspace.hpp"
#include "native_fleet_controller.hpp"
#include "native_fleet_presentation.hpp"
#include "native_fleet_workspace.hpp"
#include "native_research_controller.hpp"
#include "native_research_workspace.hpp"
#include "native_research_art.hpp"
#include "native_shipyard_controller.hpp"
#include "native_shipyard_workspace.hpp"
#include "native_system_view.hpp"
#include "native_inspection.hpp"
#include "native_logistics_workspace.hpp"
#include "native_economy_workspace.hpp"
#include "native_system_travel.hpp"
#include "native_system_workspace.hpp"
#include "native_planet_disc_assets.hpp"
#include "native_fleet_route_effects.hpp"
#include "native_ship_art_assets.hpp"
#include "native_battle_art.hpp"
#include "native_battle_sprites.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"
#include "native_startup_entry.hpp"
#include "native_galaxy_backdrop.hpp"
#include "native_territory_overlay.hpp"
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/exploration_advance.hpp>

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/build_version.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_save.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/survey_operations.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
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
namespace native_battle_art=stellar::native_battle_art;
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
using namespace stellar::native_territory;
using namespace stellar::native_ship_ui;
namespace native_battle_ui = stellar::native_battle_ui;

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

// First-rendered-frame evidence is retained only for an opt-in bounded profile.
struct SmokeColdProfile {
  struct Row {int frame{};double update{},scene{},render_present{};FrameTiming timing{};std::uint64_t uploads_before{},uploads_after{};};
  std::array<Row,10> rows{};std::size_t count{};
  void observe(int frame,double update,double scene,double render_present,const FrameTiming &timing,std::uint64_t uploads_before,std::uint64_t uploads_after){
    if(count>=rows.size())return;
    rows[count++]={frame,update,scene,render_present,timing,uploads_before,uploads_after};
  }
  void write(std::ostream &out)const{
    if(count!=rows.size())throw std::runtime_error("Cold profiling did not observe ten rendered frames.");
    out<<" cold_profile={\"rows\":[";
    for(std::size_t index=0;index<rows.size();++index){const auto &row=rows[index];if(index)out<<',';out<<"{\"frame\":"<<row.frame<<",\"update_ms\":"<<row.update<<",\"scene_ms\":"<<row.scene<<",\"submission_ms\":"<<row.timing.submission_ms<<",\"readback_ms\":"<<row.timing.readback_ms<<",\"throttle_ms\":"<<row.timing.throttle_ms<<",\"present_ms\":"<<row.timing.present_ms<<",\"render_present_ms\":"<<row.render_present<<",\"image_uploads_before\":"<<row.uploads_before<<",\"image_uploads_after\":"<<row.uploads_after<<'}';}
    out<<"]}";
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
  bool navigation_smoke{};
  bool support_check{},support_failure_check{};
  bool battle_smoke{},battle_reload_smoke{};
  bool fleet_smoke{};
  bool shipyard_smoke{};
  bool construction_smoke{};
  bool system_smoke{};
  bool system_travel_smoke{};
  bool system_travel_reload_smoke{};
  bool colony_smoke{};
  bool planetary_smoke{};
  bool colony_reload_smoke{};
  bool settlement_smoke{};
  bool settlement_reload_smoke{};
  bool settlement_preparation_smoke{};
  bool surface_smoke{};
  bool surface_reload_smoke{};
  enum class EarnedSurfaceMode { Resume, Paused };
  std::optional<EarnedSurfaceMode> earned_surface_mode;
  std::optional<EarnedSurfaceMode> earned_surface_expansion_mode;
  bool new_game_smoke{},restart_smoke{};
  StartupEntryAutomationAction restart_action{StartupEntryAutomationAction::Create};
  bool galaxy_art_smoke{};
  bool ship_art_smoke{};
  bool diplomacy_smoke{},diplomacy_reload_smoke{};
  bool fresh_progression_smoke{},fresh_progression_reload_smoke{};
  enum class FirstExplorationMode { Depart, Paused, Resume };
  std::optional<FirstExplorationMode> first_exploration_mode;
  enum class FirstSurveyMode { Depart, Paused, Resume };
  std::optional<FirstSurveyMode> first_survey_mode;
  enum class SettlementCompletionMode { Resume, Paused };
  std::optional<SettlementCompletionMode> settlement_completion_mode;
  bool campaign_profile{},menu_smoke{},audio_check{},audio_settings_check{},video_settings_check{},voice_check{},inspection_check{},logistics_check{},economy_check{},military_check{};
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
    else if(arg==L"--audio-check") result.audio_check=true;
    else if(arg==L"--support-check") result.support_check=true;
    else if((arg==L"--battle-smoke"||arg==L"--battle-reload-smoke")&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.battle_smoke=true;result.battle_reload_smoke=arg==L"--battle-reload-smoke";result.windowed=true;}
    else if(arg==L"--support-failure-check"){result.support_check=true;result.support_failure_check=true;}
    else if(arg==L"--voice-check") result.voice_check=true;
    else if(arg==L"--audio-settings-check") result.audio_settings_check=true;
    else if(arg==L"--video-settings-check") result.video_settings_check=true;
    else if(arg==L"--logistics-check") result.logistics_check=true;
    else if(arg==L"--economy-check") result.economy_check=true;
    else if(arg==L"--military-check") result.military_check=true;
    else if(arg==L"--inspection-check") result.inspection_check=true;
    else if(arg==L"--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg==L"--load") result.load=true;
    else if(arg==L"--width"&&i+1<argc) result.window_width=std::stoi(argv[++i]);
    else if(arg==L"--height"&&i+1<argc) result.window_height=std::stoi(argv[++i]);
    else if(arg==L"--profile-frames"&&i+1<argc) result.profile_frames=parse_profile_frames(std::wstring_view(argv[++i]));
    else if(arg==L"--campaign-profile"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.campaign_profile=true;result.windowed=true;}
    else if(arg==L"--smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.menu_smoke=true;result.windowed=true;}
    else if((arg==L"--restart-smoke"||arg==L"--restart-cancel-smoke"||arg==L"--restart-exit-smoke")&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.restart_smoke=true;result.windowed=true;result.load=true;result.restart_action=arg==L"--restart-cancel-smoke"?StartupEntryAutomationAction::ReturnToCampaign:arg==L"--restart-exit-smoke"?StartupEntryAutomationAction::Exit:StartupEntryAutomationAction::Create;}
    else if(arg==L"--new-game-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.windowed=true;}
    else if(arg==L"--research-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.research_smoke=true;result.windowed=true;}
    else if(arg==L"--navigation-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.navigation_smoke=true;result.windowed=true;}
    else if(arg==L"--fleet-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.fleet_smoke=true;result.windowed=true;}
    else if(arg==L"--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.shipyard_smoke=true;result.windowed=true;}
    else if(arg==L"--construction-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.construction_smoke=true;result.windowed=true;}
    else if(arg==L"--system-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_smoke=true;result.windowed=true;}
    else if(arg==L"--system-travel-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_travel_smoke=true;result.windowed=true;}
    else if(arg==L"--system-travel-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.system_travel_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--planetary-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_smoke=true;result.planetary_smoke=true;result.windowed=true;}
    else if(arg==L"--planetary-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_reload_smoke=true;result.planetary_smoke=true;result.windowed=true;}
    else if(arg==L"--colony-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_smoke=true;result.windowed=true;}
    else if(arg==L"--colony-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.colony_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--settlement-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_smoke=true;result.windowed=true;}
    else if(arg==L"--settlement-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_reload_smoke=true;result.windowed=true;}
    else if(arg==L"--surface-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.surface_smoke=true;result.windowed=true;}
    else if(arg==L"--surface-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.surface_reload_smoke=true;result.windowed=true;}
    else if((arg==L"--earned-surface-smoke"||arg==L"--earned-surface-paused-smoke")&&i+1<argc){if(result.earned_surface_mode)throw std::invalid_argument("Choose one earned surface mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.earned_surface_mode=arg==L"--earned-surface-smoke"?Options::EarnedSurfaceMode::Resume:Options::EarnedSurfaceMode::Paused;result.windowed=true;}
    else if((arg==L"--earned-surface-expansion-smoke"||arg==L"--earned-surface-expansion-paused-smoke")&&i+1<argc){if(result.earned_surface_expansion_mode)throw std::invalid_argument("Choose one earned surface expansion mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.earned_surface_expansion_mode=arg==L"--earned-surface-expansion-smoke"?Options::EarnedSurfaceMode::Resume:Options::EarnedSurfaceMode::Paused;result.windowed=true;}
    else if(arg==L"--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg==L"--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.ship_art_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_reload_smoke=true;result.windowed=true;}
    else if((arg==L"--fresh-progression-smoke"||arg==L"--fresh-progression-reload-smoke")&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.fresh_progression_smoke=arg==L"--fresh-progression-smoke";result.fresh_progression_reload_smoke=arg==L"--fresh-progression-reload-smoke";result.windowed=true;}
    else if((arg==L"--first-exploration-smoke"||arg==L"--first-exploration-paused-smoke"||arg==L"--first-exploration-resume-smoke")&&i+1<argc){if(result.first_exploration_mode)throw std::invalid_argument("Choose one first exploration mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.first_exploration_mode=arg==L"--first-exploration-smoke"?Options::FirstExplorationMode::Depart:arg==L"--first-exploration-paused-smoke"?Options::FirstExplorationMode::Paused:Options::FirstExplorationMode::Resume;result.windowed=true;}
    else if((arg==L"--settlement-completion-smoke"||arg==L"--settlement-founded-smoke")&&i+1<argc){if(result.settlement_completion_mode)throw std::invalid_argument("Choose one settlement completion mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_completion_mode=arg==L"--settlement-completion-smoke"?Options::SettlementCompletionMode::Resume:Options::SettlementCompletionMode::Paused;result.windowed=true;}
    else if(arg==L"--settlement-preparation-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_preparation_smoke=true;result.windowed=true;}
    else if((arg==L"--first-survey-smoke"||arg==L"--first-survey-paused-smoke"||arg==L"--first-survey-resume-smoke")&&i+1<argc){if(result.first_survey_mode)throw std::invalid_argument("Choose one first survey mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.first_survey_mode=arg==L"--first-survey-smoke"?Options::FirstSurveyMode::Depart:arg==L"--first-survey-paused-smoke"?Options::FirstSurveyMode::Paused:Options::FirstSurveyMode::Resume;result.windowed=true;}
#else
    const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--windowed") result.windowed=true;
    else if(arg=="--audio-check") result.audio_check=true;
    else if(arg=="--support-check") result.support_check=true;
    else if((arg=="--battle-smoke"||arg=="--battle-reload-smoke")&&i+1<argc){result.smoke_screenshot=argv[++i];result.battle_smoke=true;result.battle_reload_smoke=arg=="--battle-reload-smoke";result.windowed=true;}
    else if(arg=="--support-failure-check"){result.support_check=true;result.support_failure_check=true;}
    else if(arg=="--voice-check") result.voice_check=true;
    else if(arg=="--audio-settings-check") result.audio_settings_check=true;
    else if(arg=="--video-settings-check") result.video_settings_check=true;
    else if(arg=="--logistics-check") result.logistics_check=true;
    else if(arg=="--economy-check") result.economy_check=true;
    else if(arg=="--military-check") result.military_check=true;
    else if(arg=="--inspection-check") result.inspection_check=true;
    else if(arg=="--save-path"&&i+1<argc){result.save_path=argv[++i];result.save_path_overridden=true;}
    else if(arg=="--load") result.load=true;
    else if(arg=="--width"&&i+1<argc) result.window_width=std::stoi(argv[++i]);
    else if(arg=="--height"&&i+1<argc) result.window_height=std::stoi(argv[++i]);
    else if(arg=="--profile-frames"&&i+1<argc) result.profile_frames=parse_profile_frames(std::string_view(argv[++i]));
    else if(arg=="--campaign-profile"&&i+1<argc){result.smoke_screenshot=argv[++i];result.campaign_profile=true;result.windowed=true;}
    else if(arg=="--smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.menu_smoke=true;result.windowed=true;}
    else if((arg=="--restart-smoke"||arg=="--restart-cancel-smoke"||arg=="--restart-exit-smoke")&&i+1<argc){result.smoke_screenshot=argv[++i];result.restart_smoke=true;result.windowed=true;result.load=true;result.restart_action=arg=="--restart-cancel-smoke"?StartupEntryAutomationAction::ReturnToCampaign:arg=="--restart-exit-smoke"?StartupEntryAutomationAction::Exit:StartupEntryAutomationAction::Create;}
    else if(arg=="--new-game-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.windowed=true;}
    else if(arg=="--research-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.research_smoke=true;result.windowed=true;}
    else if(arg=="--navigation-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.navigation_smoke=true;result.windowed=true;}
    else if(arg=="--fleet-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.fleet_smoke=true;result.windowed=true;}
    else if(arg=="--shipyard-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.shipyard_smoke=true;result.windowed=true;}
    else if(arg=="--construction-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.construction_smoke=true;result.windowed=true;}
    else if(arg=="--system-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_smoke=true;result.windowed=true;}
    else if(arg=="--system-travel-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_travel_smoke=true;result.windowed=true;}
    else if(arg=="--system-travel-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.system_travel_reload_smoke=true;result.windowed=true;}
    else if(arg=="--planetary-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_smoke=true;result.planetary_smoke=true;result.windowed=true;}
    else if(arg=="--planetary-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_reload_smoke=true;result.planetary_smoke=true;result.windowed=true;}
    else if(arg=="--colony-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_smoke=true;result.windowed=true;}
    else if(arg=="--colony-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.colony_reload_smoke=true;result.windowed=true;}
    else if(arg=="--settlement-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_smoke=true;result.windowed=true;}
    else if(arg=="--settlement-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_reload_smoke=true;result.windowed=true;}
    else if(arg=="--surface-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.surface_smoke=true;result.windowed=true;}
    else if(arg=="--surface-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.surface_reload_smoke=true;result.windowed=true;}
    else if((arg=="--earned-surface-smoke"||arg=="--earned-surface-paused-smoke")&&i+1<argc){if(result.earned_surface_mode)throw std::invalid_argument("Choose one earned surface mode.");result.smoke_screenshot=argv[++i];result.earned_surface_mode=arg=="--earned-surface-smoke"?Options::EarnedSurfaceMode::Resume:Options::EarnedSurfaceMode::Paused;result.windowed=true;}
    else if((arg=="--earned-surface-expansion-smoke"||arg=="--earned-surface-expansion-paused-smoke")&&i+1<argc){if(result.earned_surface_expansion_mode)throw std::invalid_argument("Choose one earned surface expansion mode.");result.smoke_screenshot=argv[++i];result.earned_surface_expansion_mode=arg=="--earned-surface-expansion-smoke"?Options::EarnedSurfaceMode::Resume:Options::EarnedSurfaceMode::Paused;result.windowed=true;}
    else if(arg=="--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg=="--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.ship_art_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_reload_smoke=true;result.windowed=true;}
    else if((arg=="--fresh-progression-smoke"||arg=="--fresh-progression-reload-smoke")&&i+1<argc){result.smoke_screenshot=argv[++i];result.fresh_progression_smoke=arg=="--fresh-progression-smoke";result.fresh_progression_reload_smoke=arg=="--fresh-progression-reload-smoke";result.windowed=true;}
    else if((arg=="--first-exploration-smoke"||arg=="--first-exploration-paused-smoke"||arg=="--first-exploration-resume-smoke")&&i+1<argc){if(result.first_exploration_mode)throw std::invalid_argument("Choose one first exploration mode.");result.smoke_screenshot=argv[++i];result.first_exploration_mode=arg=="--first-exploration-smoke"?Options::FirstExplorationMode::Depart:arg=="--first-exploration-paused-smoke"?Options::FirstExplorationMode::Paused:Options::FirstExplorationMode::Resume;result.windowed=true;}
    else if((arg=="--settlement-completion-smoke"||arg=="--settlement-founded-smoke")&&i+1<argc){if(result.settlement_completion_mode)throw std::invalid_argument("Choose one settlement completion mode.");result.smoke_screenshot=argv[++i];result.settlement_completion_mode=arg=="--settlement-completion-smoke"?Options::SettlementCompletionMode::Resume:Options::SettlementCompletionMode::Paused;result.windowed=true;}
    else if(arg=="--settlement-preparation-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_preparation_smoke=true;result.windowed=true;}
    else if((arg=="--first-survey-smoke"||arg=="--first-survey-paused-smoke"||arg=="--first-survey-resume-smoke")&&i+1<argc){if(result.first_survey_mode)throw std::invalid_argument("Choose one first survey mode.");result.smoke_screenshot=argv[++i];result.first_survey_mode=arg=="--first-survey-smoke"?Options::FirstSurveyMode::Depart:arg=="--first-survey-paused-smoke"?Options::FirstSurveyMode::Paused:Options::FirstSurveyMode::Resume;result.windowed=true;}
#endif
    else throw std::invalid_argument("Unknown or incomplete native client option.");
  }
  if(result.smoke_screenshot&&!result.save_path_overridden)throw std::invalid_argument("--smoke requires an isolated --save-path.");
  if(result.support_check&&!result.menu_smoke)throw std::invalid_argument("--support-check requires an isolated --smoke invocation.");
  if(result.military_check&&(!result.menu_smoke||!result.load))throw std::invalid_argument("--military-check requires an isolated --load --smoke invocation.");
  if(result.economy_check&&!result.menu_smoke)throw std::invalid_argument("--economy-check requires an isolated --smoke invocation.");
  if(result.logistics_check&&!result.menu_smoke)throw std::invalid_argument("--logistics-check requires an isolated --smoke invocation.");
  if(result.inspection_check&&!result.menu_smoke)throw std::invalid_argument("--inspection-check requires an isolated --smoke invocation.");
  if(result.battle_smoke&&!result.load)throw std::invalid_argument("--battle-smoke requires an isolated active-encounter save and --load.");
  if(result.audio_check&&(!result.smoke_screenshot||(!result.new_game_smoke&&!result.restart_smoke&&!result.menu_smoke&&!result.system_travel_smoke&&!result.system_travel_reload_smoke)))throw std::invalid_argument("--audio-check requires an isolated new-game, reload or system-travel smoke invocation.");
  if(result.voice_check&&(!result.audio_check||(!result.system_travel_smoke&&!result.system_travel_reload_smoke)))throw std::invalid_argument("--voice-check requires --audio-check with an isolated system-travel smoke.");
  if(result.video_settings_check&&(!result.smoke_screenshot||(!result.new_game_smoke&&!result.menu_smoke)))throw std::invalid_argument("--video-settings-check requires an isolated new-game or reload smoke.");
  if(result.audio_settings_check&&!result.audio_check)throw std::invalid_argument("--audio-settings-check requires --audio-check and an isolated new-game or reload smoke.");
  if(result.profile_frames&&!result.system_smoke&&!result.galaxy_art_smoke&&!result.campaign_profile&&!result.surface_smoke&&!result.surface_reload_smoke)throw std::invalid_argument("--profile-frames requires a supported native profile smoke.");
  if(result.campaign_profile&&!result.profile_frames)throw std::invalid_argument("--campaign-profile requires --profile-frames.");
  if(result.campaign_profile&&result.menu_smoke)throw std::invalid_argument("--campaign-profile cannot be combined with --smoke.");
  if(static_cast<int>(result.research_smoke)+static_cast<int>(result.navigation_smoke)+static_cast<int>(result.fleet_smoke)+static_cast<int>(result.shipyard_smoke)+static_cast<int>(result.construction_smoke)+static_cast<int>(result.system_smoke)+static_cast<int>(result.system_travel_smoke)+static_cast<int>(result.system_travel_reload_smoke)+static_cast<int>(result.colony_smoke)+static_cast<int>(result.colony_reload_smoke)+static_cast<int>(result.settlement_smoke)+static_cast<int>(result.settlement_reload_smoke)+static_cast<int>(result.surface_smoke)+static_cast<int>(result.surface_reload_smoke)+static_cast<int>(result.earned_surface_mode.has_value())+static_cast<int>(result.earned_surface_expansion_mode.has_value())+static_cast<int>(result.new_game_smoke)+static_cast<int>(result.restart_smoke)+static_cast<int>(result.galaxy_art_smoke)+static_cast<int>(result.ship_art_smoke)+static_cast<int>(result.diplomacy_smoke)+static_cast<int>(result.diplomacy_reload_smoke)+static_cast<int>(result.fresh_progression_smoke)+static_cast<int>(result.fresh_progression_reload_smoke)+static_cast<int>(result.first_exploration_mode.has_value())+static_cast<int>(result.first_survey_mode.has_value())+static_cast<int>(result.settlement_preparation_smoke)+static_cast<int>(result.settlement_completion_mode.has_value())+static_cast<int>(result.campaign_profile)+static_cast<int>(result.battle_smoke)>1)throw std::invalid_argument("Choose one native graphical smoke mode.");
  if(result.fresh_progression_smoke&&result.load)throw std::invalid_argument("--fresh-progression-smoke cannot be combined with --load.");
  if(result.fresh_progression_reload_smoke&&!result.load)throw std::invalid_argument("--fresh-progression-reload-smoke requires --load.");
  if((result.fresh_progression_smoke||result.fresh_progression_reload_smoke)&&result.seed!=115501)throw std::invalid_argument("Fresh progression smoke requires --seed 115501.");
  if(result.first_exploration_mode&&!result.load)throw std::invalid_argument("First exploration smoke requires --load with the earned first-ships save.");
  if(result.first_exploration_mode&&result.seed!=115501)throw std::invalid_argument("First exploration smoke requires --seed 115501.");
  if(result.settlement_preparation_smoke&&(!result.load||result.seed!=115501))throw std::invalid_argument("Settlement preparation smoke requires --load with the earned full survey save and --seed 115501.");
  if(result.settlement_completion_mode&&!result.load)throw std::invalid_argument("Settlement completion smoke requires --load with an authorized or founded expedition save.");
  if(result.first_survey_mode&&!result.load)throw std::invalid_argument("First survey smoke requires --load with the completed scout save.");
  if(result.first_survey_mode&&result.seed!=115501)throw std::invalid_argument("First survey smoke requires --seed 115501.");
  if(result.new_game_smoke&&result.load)throw std::invalid_argument("--new-game-smoke cannot be combined with --load.");
  if(result.fleet_smoke&&!result.load)throw std::invalid_argument("--fleet-smoke requires --load with a player campaign fixture.");
  if(result.ship_art_smoke&&!result.load)throw std::invalid_argument("--ship-art-smoke requires --load with a player campaign fixture.");
  if((result.diplomacy_smoke||result.diplomacy_reload_smoke)&&!result.load)throw std::invalid_argument("Diplomacy smoke requires --load with an isolated diplomatic campaign fixture.");
  if(result.system_travel_smoke&&!result.load)throw std::invalid_argument("--system-travel-smoke requires --load with a routed player fleet fixture.");
  if(result.system_travel_reload_smoke&&!result.load)throw std::invalid_argument("--system-travel-reload-smoke requires --load with the paused system travel save.");
  if(result.colony_reload_smoke&&!result.load)throw std::invalid_argument("--colony-reload-smoke requires --load with the paused colony save.");
  if((result.settlement_smoke||result.settlement_reload_smoke)&&!result.load)throw std::invalid_argument("Settlement smoke requires --load with a test-authored funded populated settlement vessel.");
  if((result.surface_smoke||result.surface_reload_smoke)&&!result.load)throw std::invalid_argument("Surface smoke requires --load with the isolated native campaign save.");
  if(result.earned_surface_mode&&(!result.load||result.seed!=115501))throw std::invalid_argument("Earned surface smoke requires --load with the earned Xanthe colony save and --seed 115501.");
  if(result.earned_surface_expansion_mode&&(!result.load||result.seed!=115501))throw std::invalid_argument("Earned surface expansion smoke requires --load with the earned paused Xanthe save and --seed 115501.");
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
void control_label(DrawList &out, UiRect bounds, std::string value, Color color,
                   int preferred_size, float scale,
                   const SystemTextMeasurer &measure) {
  const float padding = std::max(6.f, 8.f * scale);
  const UiRect clip{bounds.x + padding, bounds.y + 2.f * scale,
                    std::max(1.f, bounds.width - 2.f * padding),
                    std::max(1.f, bounds.height - 4.f * scale)};
  int size = std::max(10, preferred_size);
  TextExtent extent{};
  // At most five cached measurements fit the control without wrapping.
  // The final extent must describe the same size submitted for drawing.
  for (int attempt = 0; attempt < 5; ++attempt) {
    const Text probe{{}, value, color, size, 0.f, std::nullopt,
                     TextAlign::Center, FontFace::Interface};
    extent = measure ? measure(probe)
                     : TextExtent{static_cast<int>(value.size() * size * .58f), size};
    if ((extent.width <= clip.width && extent.height <= clip.height) ||
        size == 10 || attempt == 4) break;
    size = std::max(10, size - 2);
  }
  out.overlay.emplace_back(Text{{bounds.x + bounds.width * .5f,
      bounds.y + (bounds.height - static_cast<float>(extent.height)) * .5f},
      std::move(value), color, size, 0.f, clip, TextAlign::Center,
      FontFace::Interface});
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
  NativeGalaxyLabelLayoutStats labels;
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
                 const std::filesystem::path &asset_root,SystemTextMeasurer text_measurer,
                 std::function<void()> audio_confirm={},
                 stellar::native_audio::NativeAudioSettings* audio_settings=nullptr,
                 stellar::native_audio::NativeAudioDirector* presentation_audio=nullptr,
                 stellar::native_video_settings::NativeVideoController* video_settings=nullptr,
                 stellar::native_general::NativeGeneralSettings* general_settings=nullptr,
                 stellar::native_settings::NativeSettingsHub* settings_hub=nullptr,
                 stellar::native_audio::NativeVoiceSettings* voice_settings=nullptr)
      : session_(std::move(session)),
        navigation_art_(std::filesystem::absolute(asset_root)),
        research_art_(std::filesystem::absolute(asset_root)),
        galaxy_assets_(std::filesystem::absolute(asset_root)),
        galaxy_backdrop_(galaxy_assets_),
        planet_discs_(std::filesystem::absolute(asset_root)/"assets/visual/sol"),
        ship_art_(std::filesystem::absolute(asset_root)),
        battle_sprites_(std::filesystem::absolute(asset_root)),
        surface_art_(std::filesystem::absolute(asset_root)),
        asset_root_(std::filesystem::absolute(asset_root)),
        text_measurer_(text_measurer),
        system_workspace_([this](const SystemBodyAppearance &appearance){return planet_discs_.request_image(appearance);},text_measurer) {
    research_workspace_.set_text_measurer(text_measurer_);
    research_workspace_.set_artwork_resolver([this](std::string_view id, bool portrait) {
      return research_art_.image(id, portrait);
    });
    inspection_card_.set_text_measurer(text_measurer_);
    supply_workspace_.set_text_measurer(text_measurer_);
    economy_workspace_.set_text_measurer(text_measurer_);
    notification_view_.set_text_measurer(text_measurer_);
    seed_notifications();
    galaxy_assets_.use_background_preparation(image_preparation_);
    planet_discs_.use_background_preparation(image_preparation_);
    colony_workspace_.set_text_measurer(text_measurer);
    colony_workspace_.use_planetary_screen();
    colony_workspace_.planetary().set_art([this](bool buildings){
      auto& cached=planetary_art_[buildings?1:0];
      if(!cached)cached=decode_rgba_image(asset_root_/"assets/visual/planetary"/(buildings?"building-portraits-v1.png":"colony-panorama-v1.png"));
      return cached;
    });
    surface_workspace_.set_text_measurer(std::move(text_measurer));
    system_workspace_.use_background_preparation(image_preparation_);
    surface_workspace_.use_relief_preparation(image_preparation_);
    surface_art_.use_background_preparation(image_preparation_);
    refresh_knowledge();
    fit_camera(width,height);
    bind_galaxy_backdrop(width,height);
    focus_home_map(width,height);
    refresh_fleets(true);
    audio_confirm_=std::move(audio_confirm);
    audio_settings_=audio_settings;
    video_settings_=video_settings;
    general_settings_=general_settings;settings_hub_=settings_hub;voice_settings_=voice_settings;
    presentation_audio_=presentation_audio;
    menu_hover_feedback_.set_callback([this]{if(presentation_audio_)presentation_audio_->hover();});
  }

  [[nodiscard]] bool new_game_ready()const{return session_->new_campaign_transition()==NewCampaignTransition::Ready;}
  [[nodiscard]] bool new_game_pending()const{return session_->new_campaign_pending();}
  [[nodiscard]] const std::filesystem::path& save_path()const{return session_->save_path();}
  [[nodiscard]] std::string restart_snapshot(){
    std::ostringstream out;out<<std::setprecision(17)<<camera_.center.x<<','<<camera_.center.y<<','<<camera_.pixels_per_world
      <<','<<selected_id_.value_or(-1)<<','<<session_->cache().generation<<','<<menu_
      <<','<<research_workspace_.visible()<<','<<system_workspace_.visible()<<'|';
    out<<encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),
      {session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2044-05-06T07:08:12Z"}));
    return out.str();
  }
  void cancel_new_game(){session_->cancel_new_campaign();gesture_.capture_for_ui();}

  void use_legacy_colony_probes(){colony_workspace_.use_planetary_screen(false);}
  void prepare_smoke_ui(){if(!menu_)toggle_menu();smoke_save_pending_=true;}
  [[nodiscard]] std::string military_smoke(int width,int height,
      const std::function<void(const DrawList&,std::string_view)>& draw) {
    if(!menu_||settings_visible()||session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Military proof requires the paused campaign menu.");
    const PlayerCampaignCaptureOptions capture_options{session_->frame().clock().simulation_days(),
      STELLAR_GAME_VERSION,"2044-05-06T07:08:12Z"};
    auto expected=capture_player_campaign_v17(session_->frame().runtime(),capture_options);
    const auto route=[&](std::vector<InputEvent> events){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);
      if(!update(input,width,height,0.,false))throw std::runtime_error("Military input exited the game.");
    };
    const auto click=[&](Point point){route({{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}});};
    const auto ui=NativeUiLayout::for_viewport(width,height);
    route({{InputEventType::EscapePressed}});
    const bool restore_running=session_->frame().clock().speed()!=StrategicSpeed::Paused;
    if(restore_running)click(center(ui.pause));
    const auto layout=FleetWorkspaceLayout::for_viewport(width,height);
    const auto& fleets=fleet_workspace_.view()->own_fleets;
    const auto military=std::ranges::find_if(fleets,[](const auto& fleet){
      return fleet.role==FleetRole::Military&&fleet.current_system_id&&
             fleet.combat_status&&fleet.combat_status->is_armed;});
    if(military==fleets.end())throw std::runtime_error("Military proof needs an owned armed fixture.");
    const int fleet_id=military->id;
    const auto index=static_cast<std::size_t>(military-fleets.begin());
    click({layout.list.x+12.f*layout.scale,layout.list.y+(static_cast<float>(index)*45.f+20.f)*layout.scale});
    if(fleet_controller_.selection()!=fleet_id)throw std::runtime_error("Military mouse selection failed.");
    const auto live=[&]()->const FleetState&{
      const auto& all=session_->frame().runtime().world().campaign().fleets;
      const auto found=std::ranges::find(all,fleet_id,&FleetState::id);
      if(found==all.end()||!found->combat)throw std::runtime_error("Military proof lost its ship.");
      return *found;
    };
    if(!expected.galaxy.fleets)throw std::runtime_error("Military proof has no fleet payload.");
    auto saved=std::ranges::find(*expected.galaxy.fleets,fleet_id,&FleetSaveDto::id);
    if(saved==expected.galaxy.fleets->end()||!saved->combat)
      throw std::runtime_error("Military proof has no combat payload.");
    saved->combat->order=MilitaryOrderType::Defend;
    saved->combat->defend_system_id=live().current_system_id;
    draw(scene(width,height),"ready");
    const auto order=[&](UiRect button,MilitaryOrderType type){
      click(center(button));
      if(!last_fleet_command_accepted_||live().combat->order!=type||
         live().combat->target_fleet_id||
         live().combat->defend_system_id!=(type==MilitaryOrderType::Defend?live().current_system_id:std::nullopt))
        throw std::runtime_error("Military mouse order did not reach canonical Core.");
    };
    order(layout.order_hold,MilitaryOrderType::Hold);
    order(layout.order_defend,MilitaryOrderType::Defend);
    order(layout.order_retreat,MilitaryOrderType::Retreat);
    draw(scene(width,height),"retreat");
    order(layout.order_hold,MilitaryOrderType::Hold);
    order(layout.order_defend,MilitaryOrderType::Defend);
    const auto before_locate=encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options));
    const auto zoom=camera_.pixels_per_world;
    click(center(layout.military_locate));
    if(!last_fleet_command_accepted_||fleet_controller_.selection()!=fleet_id||
       camera_.center.x!=live().position.x||camera_.center.y!=live().position.y||
       camera_.pixels_per_world!=zoom||
       encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options))!=before_locate)
      throw std::runtime_error("Military Locate changed gameplay, selection, or zoom.");
    draw(scene(width,height),"located");
    if(restore_running)click(center(ui.pause));
    route({{InputEventType::EscapePressed}});
    if(!menu_||session_->frame().clock().simulation_days()!=capture_options.simulation_days||
       encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options))!=
       encode_player_campaign_v17_json(expected))
      throw std::runtime_error("Military input changed campaign state beyond the authorized Defend order.");
    click(center(ui.save_button));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!smoke_save_succeeded()&&std::chrono::steady_clock::now()<deadline){
      route({});std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(!smoke_save_succeeded())throw std::runtime_error("Military order was not saved.");
    return "{\"selection\":true,\"hold\":true,\"defend\":true,\"retreat\":true,"
      "\"locate\":true,\"zoom_preserved\":true,\"only_order_changed\":true,\"order_saved\":true}";
  }
  [[nodiscard]] std::string economy_smoke(int width,int height,
      const std::function<void(const DrawList&,std::string_view)>& draw) {
    using namespace stellar::native_economy;
    if(!menu_||settings_visible()||session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Economy proof requires the paused campaign menu.");
    const PlayerCampaignCaptureOptions capture_options{session_->frame().clock().simulation_days(),
      STELLAR_GAME_VERSION,"2044-05-06T07:08:12Z"};
    auto expected=capture_player_campaign_v17(session_->frame().runtime(),capture_options);
    if(!expected.galaxy.economies)throw std::runtime_error("Economy proof has no saved economies.");
    const auto actor=expected.galaxy.player_civilization_id;
    auto own=std::ranges::find(*expected.galaxy.economies,actor,&EconomySaveDto::civilization_id);
    if(own==expected.galaxy.economies->end())throw std::runtime_error("Economy proof has no player economy.");
    own->industry_priority=IndustryPriority::ShipbuildingFirst;
    const auto camera_before=camera_;const auto selected_before=selected_id_;
    const auto route=[&](std::vector<InputEvent> events,double elapsed=0.){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);
      if(!update(input,width,height,elapsed,false))throw std::runtime_error("Economy input exited the game.");
    };
    const auto click=[&](Point point){route({{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}});};
    const auto ui=NativeUiLayout::for_viewport(width,height);
    route({{InputEventType::EscapePressed}});
    const bool restore_running=session_->frame().clock().speed()!=StrategicSpeed::Paused;
    if(restore_running)click(center(ui.pause));
    const auto before=economy_controller_.attempted_refresh_count();
    click(center(ui.economy));
    const auto& view=economy_controller_.view();
    if(!economy_workspace_.visible()||view.state!=EconomyState::Ready||
       economy_controller_.attempted_refresh_count()!=before+1)
      throw std::runtime_error("Economy navigation did not perform exactly one ready projection.");
    const auto& runtime=session_->frame().runtime();
    const auto reference=build_economy_view(runtime.world().campaign(),&runtime.research(),std::nullopt);
    if(view.cards!=reference.cards||view.income_rows!=reference.income_rows||view.cost_rows!=reference.cost_rows||
       view.income_rows.size()!=2||view.cost_rows.size()!=7)
      throw std::runtime_error("Economy displayed totals differ from Core.");
    route({{InputEventType::PointerMove,{static_cast<float>(width)*.5f,82.f}}});
    draw(scene(width,height),"ready");
    route({},2.);
    if(economy_controller_.attempted_refresh_count()!=before+1)
      throw std::runtime_error("Paused economy unnecessarily rebuilt its projection.");
    const auto layout=EconomyLayout::for_viewport(width,height);const auto inside=center(layout.body);
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    const auto end=economy_workspace_.scroll_offset();
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    if(economy_workspace_.scroll_offset()!=end)throw std::runtime_error("Economy scroll was unbounded.");
    auto end_scene=scene(width,height);bool research_visible=false;
    for(const auto& item:end_scene.overlay)if(const auto* label=std::get_if<Text>(&item))
      if(label->value=="RESEARCH PROGRAMS")research_visible=true;
    if(!research_visible)throw std::runtime_error("Research operating costs were unreachable.");
    draw(end_scene,"end");
    const Point outside{static_cast<float>(width)-2.f,static_cast<float>(height)-2.f};
    route({{InputEventType::LeftPressed,inside},{InputEventType::PointerMove,outside,{70.f,80.f}},
           {InputEventType::LeftReleased,outside},{InputEventType::PointerCancelled}});
    if(camera_.center.x!=camera_before.center.x||camera_.center.y!=camera_before.center.y||
       camera_.pixels_per_world!=camera_before.pixels_per_world||selected_id_!=selected_before)
      throw std::runtime_error("Economy input moved the underlying chart.");
    click(center(layout.refresh));
    if(economy_controller_.attempted_refresh_count()!=before+2)
      throw std::runtime_error("Economy refresh did not project exactly once.");
    for(const auto index:{1,0,2}){
      click(center(layout.priority_buttons[static_cast<std::size_t>(index)]));
      if(static_cast<int>(economy_controller_.view().industry_priority)!=index)
        throw std::runtime_error("Economy priority input did not reach Core.");
    }
    draw(scene(width,height),"priority");
    click(center(ui.supply));
    if(economy_workspace_.visible()||!supply_workspace_.visible())
      throw std::runtime_error("Economy and Supply were not exclusive.");
    click(center(ui.research));
    if(colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||!research_workspace_.visible())
      throw std::runtime_error("Economy and Research were not exclusive.");
    click(center(ui.economy));
    if(!economy_workspace_.visible()||research_workspace_.visible()||supply_workspace_.visible())
      throw std::runtime_error("Economy did not regain navigation ownership.");
    click(center(layout.close));
    if(economy_workspace_.visible())throw std::runtime_error("Economy close failed.");
    if(restore_running)click(center(ui.pause));
    route({{InputEventType::EscapePressed}});
    if(!menu_||session_->frame().clock().simulation_days()!=capture_options.simulation_days||
       encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options))!=
       encode_player_campaign_v17_json(expected))
      throw std::runtime_error("Economy changed campaign state beyond the authorized industry priority.");
    click(center(ui.save_button));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!smoke_save_succeeded()&&std::chrono::steady_clock::now()<deadline){
      route({});std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(!smoke_save_succeeded())throw std::runtime_error("Economy priority was not saved.");
    return "{\"navigation\":true,\"canonical_totals\":true,\"single_projection\":true,"
      "\"paused_cached\":true,\"scroll_bounded\":true,\"research_visible\":true,\"map_stationary\":true,"
      "\"refresh\":true,\"workspace_isolation\":true,\"priority_saved\":true,\"only_priority_changed\":true}";
  }
  [[nodiscard]] std::string supply_smoke(int width,int height,
      const std::function<void(const DrawList&,bool)>& draw) {
    using namespace stellar::native_logistics;
    if(!menu_||settings_visible()||session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Supply proof requires the paused campaign menu.");
    const auto initial=restart_snapshot();const auto camera_before=camera_;
    const auto route=[&](std::vector<InputEvent> events,double elapsed=0.){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);
      if(!update(input,width,height,elapsed,false))throw std::runtime_error("Supply input exited the game.");
    };
    const auto click=[&](Point point){route({{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}});};
    const auto ui=NativeUiLayout::for_viewport(width,height);
    route({{InputEventType::EscapePressed}});
    const bool restore_running=session_->frame().clock().speed()!=StrategicSpeed::Paused;
    if(restore_running)click(center(ui.pause));
    const auto before=supply_controller_.attempted_refresh_count();
    click(center(ui.supply));
    const auto& view=supply_controller_.view();
    if(!supply_workspace_.visible()||view.state!=LoadState::Ready||view.nodes.empty()||
       supply_controller_.attempted_refresh_count()!=before+1)
      throw std::runtime_error("Supply navigation did not perform exactly one ready projection.");
    const auto& world=session_->frame().runtime().world().campaign();
    const auto reference=build_home_logistics(world,world.player_civilization_id);
    if(view.supply_per_day!=reference.supply_per_day||view.demand_per_day!=reference.demand_per_day||
       view.delivered_per_day!=reference.delivered_per_day||view.shortfall_per_day!=reference.shortfall_per_day||
       view.nodes.size()!=reference.nodes.size())throw std::runtime_error("Supply totals differ from Core.");
    route({{InputEventType::PointerMove,{static_cast<float>(width)*.5f,82.f}}});
    draw(scene(width,height),false);
    route({},2.);
    if(supply_controller_.attempted_refresh_count()!=before+1)
      throw std::runtime_error("Paused supply view unnecessarily rebuilt its network.");
    const auto layout=SupplyLayout::for_viewport(width,height);const auto inside=center(layout.body);
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    const auto end=supply_workspace_.scroll_offset();
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    if(supply_workspace_.scroll_offset()!=end)throw std::runtime_error("Supply scroll was unbounded.");
    draw(scene(width,height),true);
    const Point outside{static_cast<float>(width)-2.f,static_cast<float>(height)-2.f};
    route({{InputEventType::LeftPressed,inside},{InputEventType::PointerMove,outside,{70.f,80.f}},
           {InputEventType::LeftReleased,outside},{InputEventType::PointerCancelled}});
    if(camera_.center.x!=camera_before.center.x||camera_.center.y!=camera_before.center.y||
       camera_.pixels_per_world!=camera_before.pixels_per_world)
      throw std::runtime_error("Supply input moved the underlying chart.");
    click(center(layout.refresh));
    if(supply_controller_.attempted_refresh_count()!=before+2)
      throw std::runtime_error("Supply refresh did not project exactly once.");
    click(center(ui.research));
    if(colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||!research_workspace_.visible())
      throw std::runtime_error("Supply/Research navigation was not exclusive.");
    click(center(ui.supply));
    if(!supply_workspace_.visible()||research_workspace_.visible())
      throw std::runtime_error("Supply did not regain navigation ownership.");
    click(center(layout.close));
    if(supply_workspace_.visible())throw std::runtime_error("Supply close failed.");
    if(restore_running)click(center(ui.pause));
    route({{InputEventType::EscapePressed}});
    if(restart_snapshot()!=initial)throw std::runtime_error("Supply inspection changed the campaign.");
    return "{\"navigation\":true,\"canonical_totals\":true,\"single_projection\":true,"
      "\"paused_cached\":true,\"scroll_bounded\":true,\"map_stationary\":true,"
      "\"refresh\":true,\"workspace_isolation\":true,\"campaign_unchanged\":true}";
  }

  [[nodiscard]] std::string system_inspection_smoke(int width,int height,
      const std::function<void(const DrawList&,std::string_view)>& draw) {
    using stellar::native_inspection::SystemInspectionCard;
    if(!menu_||settings_visible()||session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("System inspection proof requires the paused campaign menu.");
    const auto initial=restart_snapshot();
    const auto original_camera=camera_;
    const auto original_selected=selected_id_;
    const auto& world=session_->frame().runtime().world().campaign();
    const auto observer=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    if(observer==world.civilizations.end()||!observer->is_player)
      throw std::runtime_error("System inspection fixture has no validated player.");
    const auto home=std::ranges::find(world.systems,observer->home_system_id,&StellarSystem::id);
    const auto unknown=std::ranges::find_if(world.systems,[&](const auto& system){
      return world.knowledge.system_survey_level(observer->id,system.id)==SystemSurveyLevel::unknown;
    });
    if(home==world.systems.end()||unknown==world.systems.end()||
       world.knowledge.system_survey_level(observer->id,home->id)!=SystemSurveyLevel::fully_surveyed)
      throw std::runtime_error("System inspection fixture needs a surveyed home and unknown star.");
    const auto route=[&](std::vector<InputEvent> events){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);
      if(!update(input,width,height,0.,false))throw std::runtime_error("System inspection input exited the game.");
    };
    const auto click=[&](Point point){route({{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}});};
    const auto same_camera=[](const Camera& a,const Camera& b){return a.center.x==b.center.x&&a.center.y==b.center.y&&a.pixels_per_world==b.pixels_per_world;};
    const auto card_text=[&](const DrawList& draw,std::string_view value){
      const auto panel=inspection_bounds(width,height);
      return std::ranges::any_of(draw.overlay,[&](const auto& command){
        const auto* label=std::get_if<Text>(&command);
        return label&&label->value==value&&label->clip&&panel.contains(label->at);
      });
    };
    route({{InputEventType::EscapePressed}});
    if(menu_)throw std::runtime_error("Inspection could not leave the pause menu.");
    const bool restore_running=session_->frame().clock().speed()!=StrategicSpeed::Paused;
    if(restore_running)click(center(NativeUiLayout::for_viewport(width,height).pause));
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Inspection could not pause the simulation through its control.");
    const auto select_star=[&](const StellarSystem& system){
      // Place an actual chart marker in a free part of the viewport, then use
      // the same press/release hit testing as the player (no synthetic selection).
      camera_.pixels_per_world=10.;
      camera_.center={system.position.x-static_cast<double>(width)*.12/10.,system.position.y};
      click(camera_.project({system.position.x,system.position.y},width,height));
      if(selected_id_!=std::optional{system.id}||!inspection_visible())
        throw std::runtime_error("Actual star click did not open its inspection card.");
    };
    select_star(*home);
    auto known_scene=scene(width,height);
    if(!card_text(known_scene,home->name)||!card_text(known_scene,"Fully surveyed"))
      throw std::runtime_error("Surveyed star facts are missing from the visible card.");
    draw(known_scene,"known");
    const auto bounds=inspection_bounds(width,height);
    const auto inside=center(SystemInspectionCard::body_bounds(bounds));
    const Point outside{static_cast<float>(width)*.65f,static_cast<float>(height)*.7f};
    const auto before_camera=camera_;
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    const auto end_scroll=inspection_card_.scroll_offset();
    route({{InputEventType::Wheel,inside,{},-1000.f}});
    if(end_scroll!=inspection_card_.scroll_offset()||!same_camera(camera_,before_camera))
      throw std::runtime_error("Inspection wheel escaped its scroll bounds or zoomed the map.");
    draw(scene(width,height),"end");
    route({{InputEventType::LeftPressed,inside},{InputEventType::PointerMove,outside,{100.f,80.f}},
           {InputEventType::LeftReleased,outside}});
    if(!same_camera(camera_,before_camera)||selected_id_!=std::optional{home->id})
      throw std::runtime_error("Inspection drag panned or selected through the card.");
    route({{InputEventType::PointerCancelled}});
    route({{InputEventType::Wheel,inside,{},1000.f}});
    if(inspection_card_.scroll_offset()!=0.f)throw std::runtime_error("Inspection could not scroll back to its first row.");
    click(center(NativeUiLayout::for_viewport(width,height).research));
    if(!research_workspace_.visible()||inspection_visible())
      throw std::runtime_error("Research navigation failed to hide the map inspector.");
    route({{InputEventType::EscapePressed}});
    if(!inspection_visible())throw std::runtime_error("Map inspection was lost when closing Research.");
    click(center(SystemInspectionCard::close_bounds(bounds)));
    if(selected_id_||inspection_card_.visible())throw std::runtime_error("Inspection close retained its selected target.");
    select_star(*unknown);
    const auto unknown_scene=scene(width,height);
    if(!card_text(unknown_scene,"UNKNOWN")||card_text(unknown_scene,unknown->name))
      throw std::runtime_error("Unknown inspection failed its rendered name redaction.");
    draw(unknown_scene,"unknown");
    click(center(SystemInspectionCard::close_bounds(bounds)));
    camera_=original_camera;selected_id_=original_selected;refresh_inspection();
    if(restore_running)click(center(NativeUiLayout::for_viewport(width,height).pause));
    route({{InputEventType::EscapePressed}});
    if(restart_snapshot()!=initial)throw std::runtime_error("Read-only inspection changed campaign, clock, selection or camera.");
    return "{\"known_clicked\":true,\"unknown_clicked\":true,\"unknown_redacted\":true,"
      "\"wheel_bounded\":true,\"map_stationary\":true,\"drag_captured\":true,"
      "\"workspace_isolation\":true,\"close_clears_selection\":true,\"campaign_unchanged\":true}";
  }

  void repeat_unknown_lane_voice_input(int width,int height){
    if(!smoke_system_travel_unknown_denied_||!system_workspace_.travel_snapshot())
      throw std::runtime_error("Scientist check requires an observer-denied connected lane.");
    const auto& travel=*system_workspace_.travel_snapshot();
    for(const auto& geometry:system_workspace_.lane_geometry()){
      const auto lane=std::ranges::find(travel.lanes,geometry.destination_system_id,&NativeLocalLaneMarker::destination_system_id);
      if(lane==travel.lanes.end()||lane->known_label)continue;
      const auto before=system_workspace_.system_id();
      const auto day=session_->frame().clock().simulation_days();
      const auto speed=session_->frame().clock().speed();
      for(int repeat=0;repeat<4;++repeat){
        InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=geometry.center;
        input.events={{InputEventType::LeftPressed,geometry.center},{InputEventType::LeftReleased,geometry.center}};
        if(!update(input,width,height,0.,false))throw std::runtime_error("Scientist input unexpectedly exited campaign.");
      }
      if(system_workspace_.system_id()!=before||session_->frame().clock().simulation_days()!=day||
         session_->frame().clock().speed()!=speed||system_workspace_.notice().find("Telemetry unavailable")==std::string::npos)
        throw std::runtime_error("Repeated scientist guidance changed the paused observer state.");
      return;
    }
    throw std::runtime_error("Scientist check could not find a connected unknown lane.");
  }
  [[nodiscard]] bool paused_menu_visible()const{return menu_&&session_->frame().clock().speed()==StrategicSpeed::Paused;}
  void prepare_galaxy_art_smoke(int width,int height,bool reload){
    smoke_galaxy_mode_=true;smoke_galaxy_reload_=reload;
    if(camera_.pixels_per_world<fitted_pixels_per_world_*4.99)
      throw std::runtime_error("Campaign did not start in the home-system neighborhood.");
    smoke_galaxy_day_=session_->frame().clock().simulation_days();
    const auto click_pause=[&]{const auto layout=NativeUiLayout::for_viewport(width,height);const auto point=center(layout.pause);InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke pause input closed the campaign.");};
    if(session_->frame().clock().speed()==StrategicSpeed::Paused)click_pause();
    click_pause();
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)throw std::runtime_error("Galaxy artwork smoke did not pause through player input.");
    smoke_galaxy_paused_=true;fit_camera(width,height);smoke_galaxy_fitted_scale_=camera_.pixels_per_world;
    const auto overview=camera_;
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
    const Point middle{width*.5f,height*.5f};
    input.events={{InputEventType::LeftPressed,middle},
        {InputEventType::PointerMove,{middle.x+64,middle.y+32},{64,32}},
        {InputEventType::LeftReleased,{middle.x+64,middle.y+32}},
        {InputEventType::Wheel,middle,{},-12.f}};
    (void)update(input,width,height,0.,false);
    if(camera_.center.x!=overview.center.x||camera_.center.y!=overview.center.y||
        camera_.pixels_per_world!=overview.pixels_per_world)
      throw std::runtime_error("Overview drag/zoom moved or shrank the complete galaxy.");
  }
  [[nodiscard]] GalaxyArtSceneEvidence galaxy_scene_evidence(int width,int height){
    GalaxyArtSceneEvidence evidence;
    galaxy_backdrop_.clear_render_stats();
    const auto draw=scene(width,height);
    evidence.backdrop=galaxy_backdrop_.last_render_stats();
    evidence.labels=last_galaxy_label_stats_;
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
    const auto overview_radius=galaxy_star_core_radius(1.,height);
    const auto regional_radius=galaxy_star_core_radius(camera_.pixels_per_world/smoke_galaxy_fitted_scale_,height);
    if(regional_radius<overview_radius*2.)throw std::runtime_error("Regional zoom did not grow its star markers.");
    const auto before_pan=camera_;
    InputSnapshot drag;drag.drawable_width=width;drag.drawable_height=height;
    const Point middle{width*.5f,height*.5f};
    drag.events={{InputEventType::LeftPressed,middle},
        {InputEventType::PointerMove,{middle.x+24,middle.y+16},{24,16}},
        {InputEventType::LeftReleased,{middle.x+24,middle.y+16}}};
    (void)update(drag,width,height,0.,false);
    const auto expected=before_pan.unproject({width*.5f-24,height*.5f-16},width,height);
    if(std::hypot(camera_.center.x-expected.x,camera_.center.y-expected.y)>1e-6)
      throw std::runtime_error("Regional map drag did not pan the camera through space.");
    camera_=before_pan;
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
  [[nodiscard]] std::string territory_smoke_status()const {
    const auto *projection=territory_overlay_.projection();
    std::size_t contour_points=0,fill_runs=0;
    if(projection)for(const auto &region:projection->territories) {
      fill_runs+=region.fill_runs.size();
      for(const auto &contour:region.contours)contour_points+=contour.size();
    }
    std::ostringstream out;
    out<<"{\"valid\":"<<(projection?"true":"false")
       <<",\"regions\":"<<(projection?projection->territories.size():0)
       <<",\"claims\":"<<(projection?projection->claims.size():0)
       <<",\"fill_runs\":"<<fill_runs
       <<",\"contour_points\":"<<contour_points
       <<",\"fog_texels\":"<<(projection?projection->fog.alpha.size():0)
       <<",\"unexplored\":"<<(projection?projection->unexplored_system_ids.size():0)
       <<",\"fill_images\":"<<last_territory_draw_.fill_images
       <<",\"contour_segments\":"<<last_territory_draw_.contour_segments
       <<",\"claim_segments\":"<<last_territory_draw_.claim_segments
       <<",\"fog_images\":"<<last_territory_draw_.fog_images
       <<",\"cached_image_bytes\":"<<territory_overlay_.cached_image_bytes()
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
         <<",\"revealed_unknown_labels\":"<<evidence.revealed_unknown_labels
         <<",\"labels\":{\"candidates\":"<<evidence.labels.candidates
         <<",\"measured\":"<<evidence.labels.measured
         <<",\"placed\":"<<evidence.labels.placed
         <<",\"selected_requested\":"<<evidence.labels.selected_requested
         <<",\"selected_placed\":"<<evidence.labels.selected_placed
         <<",\"label_overlaps\":"<<evidence.labels.label_overlaps
         <<",\"obstacle_overlaps\":"<<evidence.labels.obstacle_overlaps
         <<",\"hud_overlaps\":"<<evidence.labels.hud_overlaps
         <<",\"star_overlaps\":"<<evidence.labels.star_overlaps
         <<",\"outside_viewport\":"<<evidence.labels.outside_viewport
         <<"}}";
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
    const auto focus_scale=system_workspace_.viewport()->scale;
    click({system_layout.focus_action.x+system_layout.focus_action.width*.5f,
           system_layout.focus_action.y+system_layout.focus_action.height*.5f});
    const auto focused=system_workspace_.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
    smoke_system_focused_=system_workspace_.viewport()->scale==focus_scale&&
      std::abs(focused.x-(system_layout.world_field.x+system_layout.world_field.width*.5f))<.1f&&
      std::abs(focused.y-(system_layout.world_field.y+system_layout.world_field.height*.5f))<.1f;
    if(!smoke_system_focused_)throw std::runtime_error("System smoke Focus Planet failed to center Earth or changed zoom.");
  }
  std::string body_inspection_smoke(int width,int height,const std::function<void(const DrawList&)>& capture){
    if(!system_workspace_.visible()||!system_workspace_.viewport())
      throw std::runtime_error("Body inspection smoke requires an open system view; it was closed during validation.");
    const auto layout=SystemWorkspaceLayout::for_viewport(width,height);
    const auto before=*system_workspace_.viewport();
    const auto wheel=[&](float amount){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer={layout.inspector.x+30.f,layout.inspector.y+220.f};
      input.events={{InputEventType::Wheel,input.pointer,{},amount}};
      (void)update(input,width,height,0.,false);
    };
    const auto contains=[&](const DrawList& draw,std::string_view value){
      return std::ranges::any_of(draw.overlay,[&](const auto& item){
        const auto* text=std::get_if<Text>(&item);
        return text&&text->value==value&&text->clip&&
          text->clip->x>=layout.inspector.x&&text->clip->y>=layout.inspector.y&&
          text->clip->x+text->clip->width<=layout.inspector.x+layout.inspector.width&&
          text->clip->y+text->clip->height<=layout.focus_action.y;
      });
    };
    wheel(10000.f);
    const auto initial=scene(width,height);
    const bool physical=contains(initial,"Physical")&&contains(initial,"6,371 km")&&
      contains(initial,"9.81 m/s²")&&contains(initial,"SURVEY COMPLETE");
    wheel(-10000.f);
    const auto end=scene(width,height);
    const bool environment=contains(end,"Environment")&&contains(end,"Oxygen / nitrogen")&&
      contains(end,"Satellites & signals")&&contains(end,"Known moons");
    const float end_scroll=system_workspace_.inspection_scroll();
    wheel(-10000.f);
    const bool bounded=system_workspace_.inspection_scroll()==end_scroll;
    capture(end);
    wheel(10000.f);
    const auto after=*system_workspace_.viewport();
    const bool camera_unchanged=before.center_x==after.center_x&&before.center_y==after.center_y&&before.scale==after.scale;
    if(!physical||!environment||!bounded||!camera_unchanged||system_workspace_.inspection_scroll()!=0.f)
      throw std::runtime_error("Planet inspector failed physical/environment rendering, bounded scrolling or camera isolation.");
    std::ostringstream out;out<<std::boolalpha<<"{\"physical\":"<<physical
      <<",\"environment\":"<<environment<<",\"bounded\":"<<bounded
      <<",\"camera_unchanged\":"<<camera_unchanged<<",\"focused\":"<<smoke_system_focused_
      <<",\"scroll_end\":"<<end_scroll<<",\"scroll_reset\":"<<system_workspace_.inspection_scroll()<<'}';
    return out.str();
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
    prepare_colony_roster_smoke(width,height);
  }

  void prepare_planetary_smoke(int width,int height,bool reload){
    auto& frame=session_->frame();
    auto& screen=colony_workspace_.planetary();
    const auto state=[&]{return encode_player_campaign_v17_json(capture_player_campaign_v17(frame.runtime(),{frame.clock().simulation_days(),STELLAR_GAME_VERSION,"2044-05-06T07:08:14Z"}));};
    const auto route=[&](std::vector<InputEvent> events){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);if(!update(input,width,height,0.,false))throw std::runtime_error("Planetary UI closed unexpectedly.");};
    const auto click=[&](Point p){route({{InputEventType::LeftPressed,p},{InputEventType::LeftReleased,p}});};
    auto initial=scene(width,height);
    if(!colony_workspace_.planetary_enabled()||surface_workspace_.visible())throw std::runtime_error("Planetary screen has not replaced the terrain workspace.");
    smoke_planetary_captures_.push_back(initial);
    if(reload){
      if(std::ranges::none_of(colony_workspace_.view()->construction_sites,[](const auto& b){return b.slot_index>=0&&!b.complete;}))throw std::runtime_error("Saved slot construction was not restored.");
      std::cout<<"planetary={\"mode\":\"reload\",\"slots_restored\":true}\n";return;
    }
    const auto click_label=[&](std::string_view title){
      for(int step=0;step<24;++step){
        const auto draw=scene(width,height);
        for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value==title){Point p{t->at.x+20,t->at.y+4};if(!t->clip||t->clip->contains(p)){click(p);return;}}
        route({{InputEventType::Wheel,center(PlanetaryLayout::make(width,height).details),{},-2.f}});
      }
      throw std::runtime_error("Planetary control is not reachable: "+std::string(title));
    };
    const auto before=state();click_label("Available slot");
    smoke_planetary_captures_.push_back(scene(width,height));
    click_label("Begin construction");
    if(!screen.modal()||!std::get<NativeSurfacePlacementQuote>(screen.pending()).accepted||state()!=before)throw std::runtime_error("Planetary review mutated state or rejected its empty slot.");
    smoke_planetary_captures_.push_back(scene(width,height));
    const auto layout=PlanetaryLayout::make(width,height);
    click(center(NativeUiLayout::for_viewport(width,height).research));
    if(!screen.modal()||research_workspace_.visible())throw std::runtime_error("Navigation escaped the planetary confirmation.");
    route({{InputEventType::EscapePressed}});
    if(screen.modal()||state()!=before)throw std::runtime_error("Cancelled planetary review changed the campaign.");
    click_label("Begin construction");(void)scene(width,height);
    const auto quote=std::get<NativeSurfacePlacementQuote>(screen.pending());
    click(center(layout.confirm));
    const auto& sites=colony_workspace_.view()->construction_sites;
    const auto built=std::ranges::find(sites,quote.prepared_building_id,&NativeSurfaceSite::building_id);
    if(screen.modal()||built==sites.end()||built->slot_index!=quote.slot_index||built->complete)throw std::runtime_error("Planetary confirm did not reserve an incomplete building slot.");
    smoke_planetary_captures_.push_back(scene(width,height));
    std::cout<<"planetary={\"mode\":\"fresh\",\"review_readonly\":true,\"cancel_readonly\":true,\"modal_isolated\":true,\"slot_reserved\":true,\"timed\":true}\n";
  }
  void capture_planetary_smoke(const std::function<void(const DrawList&,int)>& draw)const{
    for(std::size_t i=0;i<smoke_planetary_captures_.size();++i)draw(smoke_planetary_captures_[i],static_cast<int>(i));
  }
  void prepare_colony_roster_smoke(int width,int height){
    if(!colony_workspace_.view())throw std::runtime_error("Roster proof requires an owned colony.");
    const auto target=*colony_workspace_.view();
    auto& frame=session_->frame();
    const PlayerCampaignCaptureOptions capture{frame.clock().simulation_days(),STELLAR_GAME_VERSION,"2044-05-06T07:08:14Z"};
    const auto canonical=[&]{return encode_player_campaign_v17_json(capture_player_campaign_v17(frame.runtime(),capture));};
    const auto before=canonical();const auto camera_before=camera_;const auto fleet_before=fleet_controller_.selection();
    const auto route=[&](std::vector<InputEvent> events,double elapsed=0.){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=events.empty()?Point{}:events.back().position;input.events=std::move(events);
      if(!update(input,width,height,elapsed,false))throw std::runtime_error("Roster input closed the campaign.");
    };
    const auto click=[&](Point point){route({{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}});};
    const auto ui=NativeUiLayout::for_viewport(width,height);
    click(center(ui.colonies));
    if(!colony_roster_.visible()||!colony_roster_.view().available||inspection_visible())throw std::runtime_error("Colony roster did not open exclusively from navigation.");
    const auto row_count=colony_roster_.view().rows.size();
    const auto layout=stellar::native_colony_roster::RosterLayout::for_viewport(width,height);
    route({{InputEventType::Wheel,center(layout.list),{},-10000.f}});
    const float end=colony_roster_.scroll_offset();
    route({{InputEventType::Wheel,center(layout.list),{},-10000.f}});
    const bool scrolled=colony_roster_.scroll_offset()==end&&std::isfinite(end);
    route({{InputEventType::Wheel,center(layout.list),{},10000.f}});
    if(colony_roster_.scroll_offset()!=0.f)throw std::runtime_error("Roster scroll did not reset.");
    click(center(ui.research));
    bool exclusive=research_workspace_.visible()&&!colony_roster_.visible();
    click(center(ui.colonies));exclusive=exclusive&&colony_roster_.visible()&&!research_workspace_.visible();
    click(center(layout.refresh));
    const auto& rows=colony_roster_.view().rows;
    const auto found=std::ranges::find(rows,target.colony_id,&stellar::native_colony_roster::Row::colony_id);
    if(found==rows.end()||!found->can_open)throw std::runtime_error("Roster lost the owned target colony.");
    const int index=static_cast<int>(found-rows.begin());
    if(index>0)route({{InputEventType::Wheel,center(layout.list),{},-static_cast<float>(index)*(layout.row_height+5.f*layout.scale)/(48.f*layout.scale)}});
    smoke_colony_roster_capture_=scene(width,height);
    if(std::ranges::any_of(smoke_colony_roster_capture_->overlay,[](const auto& command){
      const auto* label=std::get_if<Text>(&command);
      return label&&label->value=="SURVEY FINDINGS";
    }))throw std::runtime_error("System inspector rendered over the colony roster.");
    const auto button=colony_roster_.row_button(index,width,height);
    if(!layout.list.contains(center(button)))throw std::runtime_error("Roster target button was not visible after scrolling.");
    route({{InputEventType::LeftReleased,center(button)}});
    if(!colony_roster_.visible())throw std::runtime_error("Roster opened without a matched mouse press.");
    click(center(button));
    const bool selected=colony_workspace_.visible()&&colony_workspace_.view()&&
        colony_workspace_.view()->colony_id==target.colony_id&&system_workspace_.selected_body_id()==target.body_id;
    const bool readonly=canonical()==before&&fleet_controller_.selection()==fleet_before&&
        camera_.center.x==camera_before.center.x&&camera_.center.y==camera_before.center.y&&camera_.pixels_per_world==camera_before.pixels_per_world;
    if(!exclusive||!selected||!readonly||!scrolled||colony_roster_.visible())throw std::runtime_error("Roster navigation changed state or failed input isolation.");
    std::ostringstream proof;proof<<"{\"player_id\":"<<target.player_civilization_id<<",\"colony_id\":"<<target.colony_id
      <<",\"rows\":"<<row_count<<",\"opened\":true,\"selected\":true,\"readonly\":true,\"exclusive\":true,\"scrolled\":true}";
    smoke_colony_roster_evidence_=proof.str();
  }
  void capture_colony_roster_smoke(const std::function<void(const DrawList&)>& draw)const{
    if(smoke_colony_roster_capture_)draw(*smoke_colony_roster_capture_);
    if(!smoke_colony_roster_evidence_.empty())std::cout<<"colony_roster="<<smoke_colony_roster_evidence_<<'\n';
  }
  void prepare_outpost_freight_smoke(int width,int height,bool reload){
    if(!colony_workspace_.view()||!colony_workspace_.view()->resource_outpost)return;
    const auto initial=*colony_workspace_.view();
    auto& frame=session_->frame();auto& world=frame.runtime().world().campaign();
    const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(),STELLAR_GAME_VERSION,"2044-05-06T07:08:12Z"};
    const auto state=[&]{return encode_player_campaign_v17_json(capture_player_campaign_v17(frame.runtime(),options));};
    const auto before=state();
    const auto click=[&](UiRect bounds){const auto point=center(bounds);InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Freight input closed the campaign.");};
    const auto layout=ColonyWorkspaceLayout::for_viewport(width,height,true);
    NativeOutpostFreightPreview quote;bool reviewed=false,cancelled=false;
    if(!reload){
      click(layout.collect_freight);
      if(!colony_workspace_.freight_preview()||!colony_workspace_.freight_preview()->accepted)
        throw std::runtime_error("Freight review unavailable: "+(colony_workspace_.freight_preview()?colony_workspace_.freight_preview()->message:"no review"));
      reviewed=state()==before;quote=*colony_workspace_.freight_preview();
      smoke_freight_review_capture_=scene(width,height);
      click(layout.freight_cancel);cancelled=!colony_workspace_.freight_preview()&&state()==before;
      click(layout.collect_freight);
      if(!colony_workspace_.freight_preview()||colony_workspace_.freight_preview()->fleet_id!=quote.fleet_id)
        throw std::runtime_error("Freight rereview changed its selected idle ship.");
      click(layout.freight_confirm);
    }else{
      const auto ship=std::ranges::find_if(world.fleets,[&](const auto& f){return f.civilization_id==initial.player_civilization_id&&f.freight_target_outpost_id==initial.colony_id;});
      if(ship==world.fleets.end()||!ship->freight_home_colony_id)throw std::runtime_error("Freight reload lost its dispatch.");
      quote.fleet_id=ship->id;quote.home_colony_id=*ship->freight_home_colony_id;
      if(state()!=before)throw std::runtime_error("Freight reload changed paused state.");
      colony_workspace_.set_freight_notice("Freight run restored. Unpause to continue travel, loading and delivery.");
    }
    const auto ship=std::ranges::find(world.fleets,quote.fleet_id,&FleetState::id);
    const bool dispatched=ship!=world.fleets.end()&&ship->freight_target_outpost_id==initial.colony_id&&ship->freight_home_colony_id==quote.home_colony_id&&ship->cargo_materials==0.;
    if(!dispatched||(!reload&&(!reviewed||!cancelled))||frame.clock().speed()!=StrategicSpeed::Paused||frame.clock().simulation_days()!=options.simulation_days)
      throw std::runtime_error("Freight input failed readonly review/cancel, paused dispatch or retained cargo.");
    std::ostringstream out;out<<std::boolalpha<<"{\"mode\":\""<<(reload?"reload":"dispatch")<<"\",\"player_id\":"<<initial.player_civilization_id
      <<",\"colony_id\":"<<initial.colony_id<<",\"body_id\":"<<initial.body_id<<",\"system_id\":"<<initial.system_id
      <<",\"fleet_id\":"<<quote.fleet_id<<",\"home_colony_id\":"<<quote.home_colony_id<<",\"reviewed\":"<<reviewed
      <<",\"cancelled\":"<<cancelled<<",\"dispatched\":"<<dispatched<<",\"paused\":true,\"day_unchanged\":true}";
    smoke_freight_evidence_=out.str();
  }
  void capture_outpost_freight_smoke(const std::function<void(const DrawList&)>& draw)const{
    if(smoke_freight_review_capture_)draw(*smoke_freight_review_capture_);
    if(!smoke_freight_evidence_.empty())std::cout<<"outpost_freight="<<smoke_freight_evidence_<<'\n';
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
    for(float z=-70.f;z<=70.f&&!available;z+=35.f)for(float x=-70.f;x<=70.f;x+=7.f){auto quote=surface_controller_.preview_placement(session_->frame(),session_->cache().generation,*surface_workspace_.view(),option.type_id,x,z,0.f);if(quote.accepted){available=quote;break;}}
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
        if(world.knowledge.system_survey_level(world.player_civilization_id,body.system_id)!=SystemSurveyLevel::fully_surveyed)continue;
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
  void prepare_navigation_smoke(int width,int height){
    const auto click=[&](UiRect bounds){
      const auto point=center(bounds);
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},
                    {InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Navigation smoke input closed the campaign.");
    };
    const auto layout=NativeUiLayout::for_viewport(width,height);
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)
      click(layout.pause);
    // Manual saves use the existing completed strategic-frame boundary. A
    // paused zero-duration frame establishes it without advancing gameplay.
    InputSnapshot ready;
    ready.drawable_width=width;ready.drawable_height=height;
    if(!update(ready,width,height,0.,true))
      throw std::runtime_error("Navigation replay could not establish a save boundary.");
    const auto& before=session_->frame().runtime().world().campaign();
    const auto economy=std::ranges::find(before.economies,
        before.player_civilization_id,&CivilizationEconomy::civilization_id);
    if(economy==before.economies.end())
      throw std::runtime_error("Navigation smoke lost the player treasury.");
    smoke_navigation_credits_before_=economy->credits;
    smoke_navigation_day_=session_->frame().clock().simulation_days();
    const PlayerCampaignCaptureOptions capture_options{
        smoke_navigation_day_,STELLAR_GAME_VERSION,utc_timestamp()};
    const auto canonical_before=encode_player_campaign_v17_json(
        capture_player_campaign_v17(session_->frame().runtime(),capture_options));
    smoke_keyboard_commands_=0;
    const auto send=[&](InputEvent event){
      InputSnapshot input;
      input.drawable_width=width;input.drawable_height=height;
      input.events.push_back(std::move(event));
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Keyboard replay closed the campaign.");
    };
    const auto key=[&](std::uint32_t value){
      InputEvent event{InputEventType::KeyPressed};event.key=value;send(event);
    };
    const auto playback=[&]{
      auto& clock=session_->frame().clock();
      const auto accepted=*smoke_keyboard_commands_;
      for(std::uint32_t value='1';value<='4';++value){
        const auto expected=static_cast<StrategicSpeed>(value-'0');
        key(value);
        if(clock.speed()!=expected)
          throw std::runtime_error("Numeric shortcut did not select canonical speed.");
        key(' ');
        if(clock.speed()!=StrategicSpeed::Paused||clock.resume_speed()!=expected)
          throw std::runtime_error("Space did not pause and retain selected speed.");
        key(' ');
        if(clock.speed()!=expected)
          throw std::runtime_error("Space did not restore selected speed.");
      }
      key('1'); // Restore the normal resume speed expected by neighboring fixtures.
      key(' ');
      key('x'); // An unbound key must not dispatch a gameplay command.
      if(*smoke_keyboard_commands_!=accepted+14||
         clock.speed()!=StrategicSpeed::Paused)
        throw std::runtime_error("Keyboard replay did not finish paused.");
    };
    const auto blocked_keys=[&]{
      const auto accepted=*smoke_keyboard_commands_;
      const auto speed=session_->frame().clock().speed();
      const auto resume=session_->frame().clock().resume_speed();
      for(const auto value:std::array<std::uint32_t,6>{' ','1','2','3','4',0x4000003fu})
        key(value);
      if(*smoke_keyboard_commands_!=accepted||
         session_->frame().clock().speed()!=speed||
         session_->frame().clock().resume_speed()!=resume)
        throw std::runtime_error("A blocking view leaked strategic keyboard commands.");
      ++smoke_keyboard_blocked_contexts_;
    };
    playback();
    smoke_keyboard_playback_=true;
    const auto sol=std::ranges::find(before.systems,std::string("Sol"),&StellarSystem::name);
    if(sol==before.systems.end()||!enter_system(sol->id,width,height))
      throw std::runtime_error("Keyboard replay could not enter the home system.");
    playback();
    smoke_keyboard_system_=true;
    system_workspace_.close();
    const auto accepted_before_save=*smoke_keyboard_commands_;
    key(0x4000003fu);
    smoke_keyboard_save_=*smoke_keyboard_commands_==accepted_before_save+1;
    if(!smoke_keyboard_save_)
      throw std::runtime_error("F6 did not request a campaign save.");
    InputSnapshot escape;
    escape.drawable_width=width;
    escape.drawable_height=height;
    escape.events={{InputEventType::EscapePressed}};
    if(!update(escape,width,height,0.,false)||!menu_)
      throw std::runtime_error("Navigation smoke could not open the pause menu.");
    blocked_keys();
    click(layout.research);
    smoke_navigation_menu_blocked_=menu_&&!research_workspace_.visible();
    if(!smoke_navigation_menu_blocked_||!update(escape,width,height,0.,false)||menu_)
      throw std::runtime_error("Pause menu did not block or release navigation.");
    prepare_colony_smoke(width,height,false);
    click(ColonyWorkspaceLayout::for_viewport(width,height).open_surface);
    if(!surface_workspace_.visible()||!surface_workspace_.view()||
       surface_workspace_.view()->available_buildings.empty())
      throw std::runtime_error("Navigation smoke could not open an actionable surface.");
    const auto surface_layout=SurfaceWorkspaceLayout::for_viewport(width,height);
    const auto type_id=surface_workspace_.view()->available_buildings.front().type_id;
    click({surface_layout.palette_rows.x+14.f*surface_layout.scale,
           surface_layout.palette_rows.y+20.f*surface_layout.scale});
    std::optional<NativeSurfacePlacementQuote> available;
    for(float z=-70.f;z<=70.f&&!available;z+=35.f)
      for(float x=-70.f;x<=70.f;x+=7.f){
        auto quote=surface_controller_.preview_placement(
            session_->frame(),session_->cache().generation,
            *surface_workspace_.view(),type_id,x,z,0.f);
        if(quote.accepted){available=quote;break;}
      }
    if(!available)
      throw std::runtime_error("Navigation smoke found no surface modal fixture.");
    (void)surface_controller_.cancel_quote(session_->cache().generation,
                                           available->quote_revision);
    const auto surface_point=surface_workspace_.viewport().world_to_screen(
        available->x,available->z,surface_layout.terrain);
    InputSnapshot move;
    move.drawable_width=width;
    move.drawable_height=height;
    move.pointer=surface_point;
    move.events={{InputEventType::PointerMove,surface_point,{}}};
    if(!update(move,width,height,0.,false))
      throw std::runtime_error("Navigation smoke surface preview closed the campaign.");
    click({surface_point.x,surface_point.y});
    if(!(surface_workspace_.modal_open()||colony_workspace_.planetary_modal()))
      throw std::runtime_error("Navigation smoke did not open a surface confirmation.");
    blocked_keys();
    click(layout.research);
    smoke_navigation_modal_blocked_=surface_workspace_.visible()&&
        (surface_workspace_.modal_open()||colony_workspace_.planetary_modal())&&!research_workspace_.visible();
    if(!smoke_navigation_modal_blocked_)
      throw std::runtime_error("Surface confirmation did not block navigation.");
    click(surface_layout.cancel);
    if((surface_workspace_.modal_open()||colony_workspace_.planetary_modal()))
      throw std::runtime_error("Navigation smoke could not dismiss its surface confirmation.");
    const auto verify_only=[&](bool expected,std::string_view name){
      const auto visible_count=static_cast<int>(research_workspace_.visible())+
          static_cast<int>(shipyard_workspace_.visible())+
          static_cast<int>(construction_workspace_.visible())+
          static_cast<int>(diplomacy_workspace_.visible());
      if(!expected||visible_count!=1)
        throw std::runtime_error("Navigation smoke did not exclusively open "+
                                 std::string(name)+".");
      ++smoke_navigation_switches_;
    };
    click(layout.research);
    verify_only(research_workspace_.visible(),"Research");
    const auto research_layout=ResearchWorkspaceLayout::for_viewport(
        width,height,research_workspace_.window()->domain_tabs.size());
    click(research_layout.search);
    if(!wants_text_input())
      throw std::runtime_error("Research search did not acquire keyboard ownership.");
    blocked_keys();
    const auto original_search=research_workspace_.query().search;
    send({InputEventType::TextEntered,{}, {},0.f,"1 "});
    if(research_workspace_.query().search!=original_search+"1 ")
      throw std::runtime_error("Gameplay shortcuts consumed research text.");
    send({InputEventType::BackspacePressed});
    send({InputEventType::BackspacePressed});
    smoke_keyboard_text_=research_workspace_.query().search==original_search;
    if(!smoke_keyboard_text_)
      throw std::runtime_error("Research text could not be corrected.");
    click(layout.shipyard);
    verify_only(shipyard_workspace_.visible(),"Shipyard");
    click(layout.construction);
    verify_only(construction_workspace_.visible(),"Construction");
    click(layout.diplomacy);
    verify_only(diplomacy_workspace_.visible(),"Relations");
    blocked_keys();
    const auto& after=session_->frame().runtime().world().campaign();
    const auto after_economy=std::ranges::find(after.economies,
        after.player_civilization_id,&CivilizationEconomy::civilization_id);
    if(after_economy==after.economies.end())
      throw std::runtime_error("Navigation smoke lost the player treasury after replay.");
    smoke_navigation_credits_after_=after_economy->credits;
    smoke_navigation_canonical_unchanged_=canonical_before==
        encode_player_campaign_v17_json(capture_player_campaign_v17(
            session_->frame().runtime(),capture_options));
    smoke_navigation_pause_retained_=
        session_->frame().clock().speed()==StrategicSpeed::Paused&&
        session_->frame().clock().simulation_days()==smoke_navigation_day_;
    smoke_navigation_no_charge_=
        smoke_navigation_credits_before_==smoke_navigation_credits_after_;
    if(!smoke_navigation_canonical_unchanged_||!smoke_navigation_pause_retained_||
       !smoke_navigation_no_charge_)
      throw std::runtime_error("Navigation smoke changed canonical campaign state.");
  }
  [[nodiscard]] DrawList research_inspector_end_smoke(int width,int height){
    if(!research_workspace_.visible()||!research_workspace_.window()||
       !research_workspace_.selected_id()||!text_measurer_)
      throw std::runtime_error("Research inspector replay requires a selected program and the native font renderer.");
    const auto selected=*research_workspace_.selected_id();
    const auto layout=ResearchWorkspaceLayout::for_viewport(
        width,height,research_workspace_.window()->domain_tabs.size());
    // Establish the real wrapped-text extent before sending a wheel event.
    (void)scene(width,height);
    const auto card_before=research_workspace_.card_bounds(selected,width,height);
    const auto wheel=[&](float amount){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer={layout.inspector.x+layout.inspector.width*.5f,
                     layout.inspector.y+layout.inspector.height*.5f};
      input.events={{InputEventType::Wheel,input.pointer,{},amount}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Research inspector wheel input closed the campaign.");
    };
    wheel(-1000.f);
    auto bottom=scene(width,height);
    const auto card_after=research_workspace_.card_bounds(selected,width,height);
    if(!card_before||!card_after||card_before->x!=card_after->x||card_before->y!=card_after->y)
      throw std::runtime_error("Research inspector wheel moved the graph.");
    const Text *last_detail=nullptr;
    for(const auto&command:bottom.overlay){
      const auto*label=std::get_if<Text>(&command);
      if(label&&(label->value.starts_with("COST & TIME\n")||
                 label->value.starts_with("KNOWN CAPABILITIES\n")||
                 label->value.starts_with("REQUIREMENTS / STATUS\n")||
                 label->value.starts_with("PROGRAM NOTICE\n")||
                 label->value.starts_with("ACTION STATUS\n")))
        last_detail=label;
    }
    if(!last_detail||!last_detail->clip)
      throw std::runtime_error("Research inspector replay found no final detail block.");
    const auto final_extent=text_measurer_(*last_detail);
    if(last_detail->at.y+static_cast<float>(final_extent.height)>
           last_detail->clip->y+last_detail->clip->height+1.f||
       last_detail->clip->y+last_detail->clip->height>layout.action.y)
      throw std::runtime_error("Research inspector final line is clipped or covers its action.");
    wheel(1.f);
    (void)scene(width,height);
    wheel(-1000.f);
    return scene(width,height);
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
    // Exercise an independent civilian through actual UI input while preserving
    // the routed ship and the paused-reload payload exactly.
    const auto &recovery_fleets=fleet_workspace_.view()->own_fleets;
    const auto civilian=std::ranges::find_if(recovery_fleets,[&](const auto &f){
      return is_civilian_role(f.role)&&f.id!=selected_fleet_id;});
    if(civilian==recovery_fleets.end())throw std::runtime_error("Fleet smoke lacks a second civilian ship.");
    const int recovery_id=civilian->id;
    const auto recovery_index=static_cast<std::size_t>(civilian-recovery_fleets.begin());
    click({layout.list.x+12.f*layout.scale,layout.list.y+(static_cast<float>(recovery_index)*45.f+20.f)*layout.scale});
    const auto state=[&]()->const FleetState &{
      const auto &live=session_->frame().runtime().world().campaign().fleets;
      const auto found=std::ranges::find(live,recovery_id,&FleetState::id);
      if(found==live.end())throw std::runtime_error("Recovery smoke ship vanished.");
      return *found;
    };
    if(fleet_controller_.selection()!=recovery_id)throw std::runtime_error("Recovery smoke selection failed.");
    const auto before=state();
    click(center(layout.recovery_left));
    if(!last_fleet_command_accepted_||state().hold_requested==before.hold_requested)
      throw std::runtime_error("Recovery smoke hold/resume failed.");
    click(center(layout.recovery_left));
    if(!last_fleet_command_accepted_||state().hold_requested!=before.hold_requested||
       state().mission_order_revision!=before.mission_order_revision||
       state().destination_system_id!=before.destination_system_id||
       state().planned_route_system_ids!=before.planned_route_system_ids)
      throw std::runtime_error("Recovery smoke changed the original mission.");
    smoke_civilian_recovery_=true;
    const PlayerCampaignCaptureOptions capture_options{session_->frame().clock().simulation_days(),
      STELLAR_GAME_VERSION,"2044-05-06T07:08:12Z"};
    const auto payload=encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options));
    const auto zoom=camera_.pixels_per_world;
    click(center(layout.civilian_locate));
    if(!last_fleet_command_accepted_||fleet_controller_.selection()!=recovery_id||
       camera_.center.x!=state().position.x||camera_.center.y!=state().position.y||
       camera_.pixels_per_world!=zoom||
       encode_player_campaign_v17_json(capture_player_campaign_v17(session_->frame().runtime(),capture_options))!=payload)
      throw std::runtime_error("Civilian Locate changed gameplay, selection, or zoom.");
    smoke_fleet_located_=true;
    click({layout.list.x+12.f*layout.scale,layout.list.y+(static_cast<float>(index)*45.f+20.f)*layout.scale});
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
  void prepare_diplomacy_map_capture(int width,int height){
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
    input.events={{InputEventType::EscapePressed}};
    if(!update(input,width,height,0.,false)||diplomacy_workspace_.visible()||menu_)
      throw std::runtime_error("Diplomacy capture could not return to the galaxy map.");
    const auto &world=session_->frame().runtime().world().campaign();
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,
                                       &Civilization::id);
    if(player==world.civilizations.end())throw std::runtime_error("Territory capture needs the player's home.");
    const auto home=session_->cache().systems_by_id.find(player->home_system_id);
    if(home==session_->cache().systems_by_id.end())throw std::runtime_error("Territory capture home is missing.");
    // Frame the player's own holding, then exercise the real wheel route. This
    // exposes claim dashes culled below five pixels in the overview, without
    // modifying ownership, survey state, or the paused simulation.
    camera_.center={home->second->position.x,home->second->position.y};
    input.pointer={static_cast<float>(width)*.5f,static_cast<float>(height)*.5f};
    const double target=std::clamp(fitted_pixels_per_world_*8.,3.,100.);
    for(int wheel=0;wheel<64&&camera_.pixels_per_world<target;++wheel){
      input.events={{InputEventType::Wheel,input.pointer,{},1.f}};
      if(!update(input,width,height,0.,false))throw std::runtime_error("Territory capture zoom closed the game.");
    }
    if(camera_.pixels_per_world<target||session_->frame().clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Territory capture failed its paused regional framing.");
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
    smoke_diplomacy_other_=other->contact_id;
    smoke_diplomacy_target_=target;
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
  void configure_support(std::string backend,std::string presentation){
    support_environment_="GameVersion="+std::string(STELLAR_GAME_VERSION)+
        "\nRuntime=native-c++23\nRendererBackend="+backend+"\nPresentation="+presentation+"\n";
#ifdef _WIN32
    support_environment_+="Platform=Windows\n";
#endif
    support_.record("session",utc_timestamp()+" Native campaign admitted.");
  }
  void prepare_battle_smoke(int width,int height,bool reload){
    smoke_battle_reload_=reload;
    const auto send=[&](InputEvent event,double delta=0.){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=event.position;input.events.push_back(std::move(event));
      if(!update(input,width,height,delta,true))throw std::runtime_error("Battle replay closed the campaign.");
    };
    refresh_battle(width,height,0.);
    if(!battle_workspace_.visible()||!battle_workspace_.snapshot())
      throw std::runtime_error("Battle replay needs an unreconciled encounter.");
    auto& frame=session_->frame();
    smoke_battle_day_=frame.clock().simulation_days();smoke_battle_utc_=utc_timestamp();
    const auto canonical=[&]{return encode_player_campaign_v17_json(capture_player_campaign_v17(
        frame.runtime(),{smoke_battle_day_,STELLAR_GAME_VERSION,smoke_battle_utc_}));};
    const auto before=canonical();
    const auto own=std::ranges::find_if(battle_workspace_.snapshot()->formations,[&](const auto& f){
      return f.civilization_id==frame.runtime().world().campaign().player_civilization_id;});
    if(own==battle_workspace_.snapshot()->formations.end())throw std::runtime_error("Battle replay lacks an owned formation.");
    diplomacy_smoke_click(battle_workspace_.project(own->position,width,height),width,height);
    if(battle_workspace_.selection().empty())throw std::runtime_error("Battle replay did not select the owned formation.");
    const auto layout=native_battle_ui::BattleWorkspaceLayout::for_viewport(width,height);
    diplomacy_smoke_click(center(layout.speed),width,height);
    send({InputEventType::PointerMove,center(layout.speed)},.25);
    smoke_battle_paused_speed_=frame.tactical_clock().speed_multiplier()==0.&&canonical()==before;
    diplomacy_smoke_click(center(layout.menu),width,height);
    if(!menu_)throw std::runtime_error("Battle menu did not open.");
    send({InputEventType::PointerMove,{}},.25);
    smoke_battle_menu_pause_=frame.tactical_clock().speed_multiplier()==0.&&canonical()==before;
    diplomacy_smoke_click(center(NativeUiLayout::for_viewport(width,height).continue_button),width,height);
    if(menu_||!smoke_battle_paused_speed_||!smoke_battle_menu_pause_)
      throw std::runtime_error("Battle speed/menu controls changed the paused campaign.");
    if(!reload){
      diplomacy_smoke_click(center(layout.order_buttons.front()),width,height);
      if(!last_battle_order_accepted_)throw std::runtime_error("Battle order was rejected by Core.");
      diplomacy_smoke_click(center(layout.play),width,height);
      for(int i=0;i<16;++i)send({InputEventType::PointerMove,{}},.25);
      diplomacy_smoke_click(center(layout.play),width,height);
      if(frame.tactical_clock().speed_multiplier()!=0.)throw std::runtime_error("Battle replay could not pause after advancement.");
    }
    refresh_battle(width,height,.1);
    (void)scene(width,height); // Bind hit geometry from the actual drawn ship.
    if(battle_art_plan_.size()!=1)throw std::runtime_error("Battle replay lacks its bound corvette.");
    const auto ship_point=battle_art_plan_.front().center;
    const auto ship_formation=battle_art_plan_.front().formation_id;
    diplomacy_smoke_click({width*.45f,height*.72f},width,height);
    if(!battle_workspace_.selection().empty())throw std::runtime_error("Battle sprite test did not clear formation selection.");
    diplomacy_smoke_click(ship_point,width,height);
    smoke_battle_ship_selected_=battle_workspace_.selection().contains(ship_formation);
    if(!smoke_battle_ship_selected_)throw std::runtime_error("Clicking the drawn corvette did not select its formation.");
    smoke_battle_canonical_=canonical();
    if(reload&&smoke_battle_canonical_!=before)throw std::runtime_error("Paused tactical reload changed canonical state.");
    // Save through the real tactical F6 route; the frame loop has no fallback.
    send({InputEventType::KeyPressed,{},{},0.f,{},0,0x4000003fu});
  }
  [[nodiscard]] std::string battle_smoke_status(int width,int height)const{
    const auto* snapshot=battle_workspace_.snapshot();
    if(!snapshot)throw std::runtime_error("Battle capture lost its observer snapshot.");
    auto& frame=session_->frame();const auto& world=frame.runtime().world().campaign();
    std::size_t own=0,foreign=0,foreign_exact=0;Point sample{};
    for(const auto& formation:snapshot->formations){
      if(formation.civilization_id==world.player_civilization_id){
        if(own++==0)sample=battle_workspace_.project(formation.position,width,height);
        if(!formation.is_exact)throw std::runtime_error("Own battle formation lost exact information.");
      }else{++foreign;if(formation.is_exact)++foreign_exact;}
    }
    const auto canonical=encode_player_campaign_v17_json(capture_player_campaign_v17(
        frame.runtime(),{smoke_battle_day_,STELLAR_GAME_VERSION,smoke_battle_utc_}));
    const bool unchanged=canonical==smoke_battle_canonical_;
    const bool paused=frame.tactical_clock().speed_multiplier()==0.;
    const bool day=frame.clock().simulation_days()==smoke_battle_day_;
    if(!unchanged||!paused||!day)throw std::runtime_error("Battle changed while paused before capture.");
    std::ostringstream out;out<<"{\"reload\":"<<(smoke_battle_reload_?"true":"false")
        <<",\"paused_speed\":"<<(smoke_battle_paused_speed_?"true":"false")
        <<",\"menu_pause\":"<<(smoke_battle_menu_pause_?"true":"false")
        <<",\"canonical_unchanged\":"<<(unchanged?"true":"false")
        <<",\"day_unchanged\":"<<(day?"true":"false")
        <<",\"order_accepted\":"<<(last_battle_order_accepted_?"true":"false")
        <<",\"own\":"<<own<<",\"foreign\":"<<foreign<<",\"foreign_exact\":"<<foreign_exact
        <<",\"hidden_formations\":"<<(world.active_combat_encounter->battle.formations.size()-snapshot->formations.size())
        <<",\"tick\":"<<snapshot->tick<<",\"tokens\":"<<battle_workspace_.rendered_tokens()
        <<",\"sample_x\":"<<sample.x<<",\"sample_y\":"<<sample.y<<"}";
    return out.str();
  }
  void battle_art_smoke(int width,int height,const std::function<void(const DrawList&)>& draw_without){
    const auto before=battle_smoke_status(width,height);
    battle_art_suppressed_=true;
    try {draw_without(scene(width,height));}
    catch(...){battle_art_suppressed_=false;throw;}
    battle_art_suppressed_=false;
    if(battle_smoke_status(width,height)!=before)throw std::runtime_error("Battle artwork changed paused canonical state.");
    if(battle_art_plan_.size()!=1||!battle_sprites_.resource())
      throw std::runtime_error("Battle artwork smoke requires one bound corvette sprite.");
    const auto& sprite=battle_art_plan_.front();const auto* resource=battle_sprites_.resource();
    const auto& observed=battle_workspace_.snapshot()->formations;
    const auto observer=session_->frame().runtime().world().campaign().player_civilization_id;
    const auto foreign=std::ranges::count_if(battle_art_plan_,[&](const auto& planned){
      const auto formation=std::ranges::find(observed,planned.formation_id,&MassiveObservedFormation::formation_id);
      return formation==observed.end()||formation->civilization_id!=observer;
    });
    std::cout<<"battle_art={\"sprites\":"<<battle_art_plan_.size()<<",\"foreign_sprites\":"<<foreign
      <<",\"source_width\":"<<resource->width()<<",\"source_height\":"<<resource->height()
      <<",\"source_bytes\":"<<resource->byte_size()<<",\"alpha_pixels\":"<<battle_sprites_.transparent_pixels()
      <<",\"center_x\":"<<sprite.center.x<<",\"center_y\":"<<sprite.center.y
      <<",\"size\":"<<sprite.size.x<<",\"heading_degrees\":"<<sprite.heading_degrees
      <<",\"moving\":"<<(sprite.moving?"true":"false")<<",\"ship_selected\":"<<(smoke_battle_ship_selected_?"true":"false")
      <<",\"paused_unchanged\":true}\n";
  }
  void support_smoke(int width,int height,bool expect_failure,
      const std::function<void(const DrawList&,bool)>& draw){
    using stellar::native_support::SupportExportState;
    if(!menu_||!smoke_save_succeeded())
      throw std::runtime_error("Support replay requires a paused saved campaign.");
    const auto day=session_->frame().clock().simulation_days();
    const PlayerCampaignCaptureOptions options{day,STELLAR_GAME_VERSION,utc_timestamp()};
    const auto canonical=[&]{return encode_player_campaign_v17_json(
        capture_player_campaign_v17(session_->frame().runtime(),options));};
    const auto before=canonical();
    const auto saved_bytes=[&]{
      const auto& path=session_->save_path();
      const auto size=std::filesystem::file_size(path);
      if(size>64u*1024u*1024u)throw std::runtime_error("Support replay save exceeds its read limit.");
      std::ifstream input(path,std::ios::binary);
      std::string bytes(static_cast<std::size_t>(size),'\0');
      if(!input||!input.read(bytes.data(),static_cast<std::streamsize>(size)))
        throw std::runtime_error("Support replay could not read the completed save.");
      return bytes;
    };
    const auto save_before=saved_bytes();
    const auto layout=NativeUiLayout::for_viewport(width,height);
    const auto send=[&](InputEvent event){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.events.push_back(std::move(event));
      if(!update(input,width,height,0.,false))throw std::runtime_error("Support replay closed the campaign.");
    };
    if(!audio_settings_)throw std::runtime_error("Support replay lacks the settings guard.");
    diplomacy_smoke_click(center(layout.settings_button),width,height);
    if(!settings_visible())throw std::runtime_error("Support replay did not open settings.");
    send({InputEventType::KeyPressed,{},{},0.f,{},0,0x40000041u});
    const bool blocked=support_.state()==SupportExportState::Idle;
    send({InputEventType::EscapePressed});
    if(!blocked||settings_visible()||!menu_)
      throw std::runtime_error("F8 escaped settings ownership.");
    const auto finish=[&]{
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
      while(support_.busy()&&std::chrono::steady_clock::now()<deadline){
        draw(scene(width,height),false);
        InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
        if(!update(input,width,height,0.,false))throw std::runtime_error("Support export exited the campaign.");
        std::this_thread::yield();
      }
      const auto expected=expect_failure?SupportExportState::Failed:SupportExportState::Succeeded;
      if(support_.state()!=expected)throw std::runtime_error("Support export timed out or returned an unexpected result: "+support_.error());
    };
    // Two presses while the immutable request is running must not queue jobs.
    InputSnapshot repeated;repeated.drawable_width=width;repeated.drawable_height=height;
    repeated.pointer=center(layout.support_button);
    repeated.events={{InputEventType::LeftPressed,repeated.pointer},
        {InputEventType::LeftReleased,repeated.pointer},
        {InputEventType::LeftPressed,repeated.pointer},
        {InputEventType::LeftReleased,repeated.pointer}};
    if(!update(repeated,width,height,0.,false))throw std::runtime_error("Support replay closed the campaign.");
    const bool menu_started=support_.busy();
    finish();
    const auto first=support_.result();
    diplomacy_smoke_click(center(layout.continue_button),width,height);
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)
      diplomacy_smoke_click(center(layout.pause),width,height);
    send({InputEventType::KeyPressed,{},{},0.f,{},0,0x40000041u});
    const bool key_started=support_.busy();
    finish();
    const auto second=support_.result();
    send({InputEventType::EscapePressed});
    const bool paused=menu_&&session_->frame().clock().speed()==StrategicSpeed::Paused&&
        session_->frame().clock().simulation_days()==day;
    const bool unchanged=canonical()==before;
    const bool save_unchanged=saved_bytes()==save_before;
    const bool distinct=expect_failure?(first.empty()&&second.empty()):(!first.empty()&&first!=second);
    if(!menu_started||!key_started||!paused||!unchanged||!save_unchanged||!distinct)
      throw std::runtime_error("Support replay did not preserve its UI, unique destination or campaign contract.");
    draw(scene(width,height),true);
    std::cout<<"support={\"mode\":\""<<(expect_failure?"failure":"success")
      <<"\",\"menu_started\":"<<(menu_started?"true":"false")
      <<",\"key_started\":"<<(key_started?"true":"false")
      <<",\"settings_blocked\":"<<(blocked?"true":"false")
      <<",\"canonical_unchanged\":"<<(unchanged?"true":"false")
      <<",\"save_unchanged\":"<<(save_unchanged?"true":"false")
      <<",\"paused\":"<<(paused?"true":"false")
      <<",\"first\":"<<json_string(utf8_path(first))
      <<",\"second\":"<<json_string(utf8_path(second))
      <<",\"error\":"<<json_string(support_.error())<<"}\n";
  }
  void notification_smoke(int width,int height,bool reload,
      const std::function<void(const DrawList&,bool)>& capture){
    const auto day=session_->frame().clock().simulation_days();
    const PlayerCampaignCaptureOptions options{day,STELLAR_GAME_VERSION,utc_timestamp()};
    const auto canonical=[&]{return encode_player_campaign_v17_json(
        capture_player_campaign_v17(session_->frame().runtime(),options));};
    const auto before=canonical();
    const auto main_layout=NativeUiLayout::for_viewport(width,height);
    // Start on a different identified contact, so following an event must
    // change selection rather than merely reopen an already selected empire.
    diplomacy_smoke_click(center(main_layout.diplomacy),width,height);
    if(!diplomacy_workspace_.visible())
      throw std::runtime_error("Notification replay could not open Relations.");
    diplomacy_smoke_select(smoke_diplomacy_other_,width,height);
    const auto items=notifications_.items().size();
    const auto unread_before=notifications_.unread_count(notification_view_.last_read());
    if(items!=(reload?0u:2u)||unread_before!=(reload?0:2))
      throw std::runtime_error("Notifications replayed retained history or lost new agreement reports.");
    diplomacy_smoke_click(center(main_layout.notifications),width,height);
    const bool opened=notification_view_.visible();
    const auto unread_after=notifications_.unread_count(notification_view_.last_read());
    if(!opened||unread_after!=0)
      throw std::runtime_error("Events button did not open and acknowledge the retained feed.");
    capture(scene(width,height),false);
    const auto layout=stellar::native_notifications::notification_layout_for(
        notifications_.items(),width,height,text_measurer_,notification_view_.scroll_offset());
    int focused_target=-1;
    if(reload){
      diplomacy_smoke_click(center(layout.close_button),width,height);
    }else{
      const auto entry=std::ranges::find_if(layout.entries,[&](const auto& item){
        return item.contact_button&&
            layout.list_viewport.contains(center(*item.contact_button));
      });
      if(entry==layout.entries.end()||
          notifications_.items()[entry->item_index].diplomatic_contact_id!=smoke_diplomacy_target_)
        throw std::runtime_error("New agreement report lacks its identified contact link.");
      diplomacy_smoke_click(center(*entry->contact_button),width,height);
      if(!diplomacy_workspace_.visible()||!diplomacy_workspace_.view())
        throw std::runtime_error("Event contact link did not open Relations.");
      focused_target=diplomacy_workspace_.view()->selected.target_civilization_id.value_or(-1);
      if(focused_target!=smoke_diplomacy_target_)
        throw std::runtime_error("Event contact link opened the wrong empire.");
    }
    const bool closed=!notification_view_.visible();
    const bool unchanged=before==canonical();
    const bool paused=session_->frame().clock().speed()==StrategicSpeed::Paused&&
        session_->frame().clock().simulation_days()==day;
    if(!closed||!unchanged||!paused)
      throw std::runtime_error("Reading notifications changed the campaign or failed to dismiss the panel.");
    capture(scene(width,height),true);
    std::cout<<"notifications={\"mode\":\""<<(reload?"paused_reload":"progress")
      <<"\",\"opened\":"<<(opened?"true":"false")
      <<",\"closed\":"<<(closed?"true":"false")<<",\"items\":"<<items
      <<",\"unread_before\":"<<unread_before<<",\"unread_after\":"<<unread_after
      <<",\"focused_target\":"<<focused_target
      <<",\"canonical_unchanged\":"<<(unchanged?"true":"false")
      <<",\"paused\":"<<(paused?"true":"false")<<"}\n";
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
  void prepare_fresh_progression_smoke(int width, int height, bool reload,
                                       const std::function<void()> &pump) {
    constexpr double maximum_days = 10'958.;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(600);
    auto &frame = session_->frame();
    auto &world = frame.runtime().world().campaign();
    fresh_progression_reload_ = reload;
    fresh_progression_before_days_ = frame.clock().simulation_days();
    const PlayerCampaignCaptureOptions fixed_capture{
        fresh_progression_before_days_, STELLAR_GAME_VERSION,
        "2044-05-06T07:08:09Z"};
    const auto captured = [&](const PlayerCampaignCaptureOptions &options) {
      return encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(frame.runtime(), options)
              .payload());
    };
    const auto before_payload = captured(fixed_capture);
    const auto route = [&](std::vector<InputEvent> events) {
      bool search_changed{};
      for (auto &event : events)
        if (event.type == InputEventType::TextEntered) {
          std::ranges::replace(event.text, '_', ' ');
          search_changed = true;
        } else if (event.type == InputEventType::BackspacePressed)
          search_changed = true;
      InputSnapshot input;
      input.drawable_width = width;
      input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error(
            "Fresh progression UI input closed the campaign.");
      if (search_changed && research_workspace_.visible())
        refresh_research(true);
    };
    const auto click = [&](Point point) {
      route({{InputEventType::LeftPressed, point},
             {InputEventType::LeftReleased, point}});
    };
    const auto ui = NativeUiLayout::for_viewport(width, height);
    const auto pause = [&] {
      if (frame.clock().speed() != StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Paused)
        throw std::runtime_error("Fresh progression pause input was rejected.");
    };
    const auto resume_normal = [&] {
      if (frame.clock().speed() == StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Normal)
        route({{InputEventType::KeyPressed, {}, {}, 0.f, {}, 0, '1'}});
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().effective_multiplier() != 1.)
        throw std::runtime_error(
            "Fresh progression requires the ordinary Normal clock.");
    };
    const auto owned_fleets = [&] {
      return std::ranges::count(world.fleets, world.player_civilization_id,
                                &FleetState::civilization_id);
    };
    const auto maturity = [&](std::string_view id) {
      const auto &state = frame.runtime().research().get_civilization(
          world.player_civilization_id);
      const auto *node = state.try_get_node_state(id);
      return node ? node->maturity : ResearchMaturity::rumored;
    };
    const auto service = [&] {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error(
            "Fresh progression exceeded its 600-second wall bound.");
      pump();
    };
    const auto advance_quarter = [&] {
      const auto before = frame.clock().simulation_days();
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().backlog_days() != 0.)
        throw std::runtime_error("Fresh progression accumulated clock backlog "
                                 "or left Normal speed before an offline step.");
      const auto result = frame.advance(.25);
      const auto after = frame.clock().simulation_days();
      if (result.route != CampaignFrameRoute::Strategic ||
          result.completed_substeps.size() != 1 ||
          result.completed_substeps.front() != .25 || after - before != .25 ||
          frame.clock().backlog_days() != 0.)
        throw std::runtime_error(
            "Fresh progression left single strategic quarter-day stepping.");
      ++fresh_progression_quarter_steps_;
      if (fresh_progression_quarter_steps_ % 128 == 0) {
        if (research_workspace_.visible())
          refresh_research(true);
        if (construction_workspace_.visible())
          refresh_construction(true);
        if (shipyard_workspace_.visible())
          refresh_shipyard(true);
        refresh_fleets(true);
        service();
      }
    };
    const auto advance_until = [&](const std::function<bool()> &done,
                                   std::string_view label) {
      while (!done()) {
        if (frame.clock().simulation_days() >= maximum_days)
          throw std::runtime_error(
              "Fresh progression exceeded the day bound at " +
              std::string(label) + ".");
        advance_quarter();
      }
      service();
    };
    const auto roundtrip = [&] {
      const PlayerCampaignCaptureOptions options{
          frame.clock().simulation_days(), STELLAR_GAME_VERSION,
          "2044-05-06T07:08:09Z"};
      const auto bytes = captured(options);
      auto restored = restore_player_campaign_v17_json(
          load_adaptive_research_strategic_runtime(asset_root_ /
                                                   "Data/research/v1"),
          bytes);
      if (restored.simulation_days() != options.simulation_days)
        throw std::runtime_error(
            "Fresh progression Player17 restore changed the clock.");
      auto resumed = std::move(restored).activate();
      const auto recaptured = encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(resumed, options).payload());
      const auto first = nlohmann::json::parse(bytes),
                 second = nlohmann::json::parse(recaptured);
      if (first != second)
        throw std::runtime_error(
            "Fresh progression Player17 roundtrip changed the campaign: " +
            nlohmann::json::diff(first, second).dump().substr(0, 4000));
      fresh_progression_roundtrip_ = true;
    };
    const auto open_construction = [&] {
      if (!construction_workspace_.visible())
        click(center(ui.construction));
      if (!construction_workspace_.visible() || !construction_workspace_.view())
        throw std::runtime_error(
            "Fresh progression could not open Construction.");
    };
    const auto open_shipyard = [&] {
      if (!shipyard_workspace_.visible())
        click(center(ui.shipyard));
      if (!shipyard_workspace_.visible() || !shipyard_workspace_.view())
        throw std::runtime_error("Fresh progression could not open Shipyard.");
    };
    const auto select_project = [&](std::string_view id) {
      const auto layout =
          ConstructionWorkspaceLayout::for_viewport(width, height);
      for (int attempt = 0;
           attempt < 128 &&
           !construction_workspace_.project_bounds(id, width, height);
           ++attempt)
        route({{InputEventType::Wheel, center(layout.projects), {}, -1.f}});
      const auto bounds =
          construction_workspace_.project_bounds(id, width, height);
      if (!bounds)
        throw std::runtime_error("Construction project is not reachable "
                                 "through its scrolled list: " +
                                 std::string(id));
      click(center(*bounds));
      if (construction_workspace_.selected_project_id() != id)
        throw std::runtime_error("Construction row selection failed: " +
                                 std::string(id));
    };
    const auto authorize_project = [&](std::string_view id) {
      open_construction();
      refresh_construction(true);
      select_project(id);
      const auto project =
          std::ranges::find(construction_workspace_.view()->projects, id,
                            &NativeConstructionProject::id);
      if (project == construction_workspace_.view()->projects.end())
        throw std::runtime_error("Construction project disappeared: " +
                                 std::string(id));
      const auto layout =
          ConstructionWorkspaceLayout::for_viewport(width, height);
      if (project->start.enabled)
        click(center(layout.primary_action));
      else if (project->queue.enabled)
        click(center(layout.secondary_action));
      else
        throw std::runtime_error("Construction project cannot be authorized: " +
                                 std::string(id));
      refresh_construction(true);
      const auto actual =
          std::ranges::find(construction_workspace_.view()->projects, id,
                            &NativeConstructionProject::id);
      if (actual == construction_workspace_.view()->projects.end() ||
          (!actual->active && !actual->queued && !actual->complete))
        throw std::runtime_error(
            "Construction UI did not change canonical state: " +
            std::string(id));
      ++fresh_progression_construction_actions_;
      service();
    };
    const auto open_research = [&] {
      if (!research_workspace_.visible())
        click(center(ui.research));
      if (!research_workspace_.visible() || !research_workspace_.window())
        throw std::runtime_error("Fresh progression could not open Research.");
    };
    const auto research_to = [&](std::string_view id, ResearchMaturity target) {
      open_research();
      const auto layout = ResearchWorkspaceLayout::for_viewport(
          width, height, research_workspace_.window()->domain_tabs.size());
      click(center(layout.search));
      while (!research_workspace_.query().search.empty())
        route({{InputEventType::BackspacePressed}});
      const auto target_node = std::ranges::find(
          research_workspace_.window()->nodes, id, &NativeResearchNode::id);
      if (target_node == research_workspace_.window()->nodes.end())
        throw std::runtime_error("Required research is not known: " +
                                 std::string(id));
      route({{InputEventType::TextEntered, {}, {}, 0.f,
              target_node->display_name}});
      auto bounds = research_workspace_.card_bounds(id, width, height);
      if (!bounds)
        throw std::runtime_error("Required research is not visible: " +
                                 std::string(id));
      const Point graph_center = center(layout.graph);
      Point drag_start{layout.graph.x + layout.graph.width - 4.f,
                       layout.graph.y + layout.graph.height - 4.f};
      if (bounds->contains(drag_start))
        drag_start = {layout.graph.x + 4.f,
                      layout.graph.y + layout.graph.height - 4.f};
      const Point delta{graph_center.x - center(*bounds).x,
                        graph_center.y - center(*bounds).y};
      route({{InputEventType::LeftPressed, drag_start},
             {InputEventType::PointerMove,
              {drag_start.x + delta.x, drag_start.y + delta.y},
              delta},
             {InputEventType::LeftReleased,
              {drag_start.x + delta.x, drag_start.y + delta.y}}});
      bounds = research_workspace_.card_bounds(id, width, height);
      if (!bounds || !layout.graph.contains(center(*bounds)))
        throw std::runtime_error("Research card panning did not expose " +
                                 std::string(id));
      click(center(*bounds));
      if (research_workspace_.selected_id() != id)
        throw std::runtime_error("Research card selection failed: " +
                                 std::string(id));
      click(center(layout.action));
      const auto current = maturity(id);
      const auto active = std::ranges::find(research_workspace_.window()->nodes,
                                            id, &NativeResearchNode::id);
      const bool reached_target =
          static_cast<int>(current) >= static_cast<int>(target);
      const bool active_after_action =
          active != research_workspace_.window()->nodes.end() && active->active;
      const bool archived = current == ResearchMaturity::archived;
      if (archived || (!reached_target && !active_after_action))
        throw std::runtime_error("Research UI did not start canonical work: " +
                                 std::string(id));
      ++fresh_progression_research_actions_;
      advance_until(
          [&] {
            const auto value = maturity(id);
            if (value == ResearchMaturity::archived)
              throw std::runtime_error("Required research archived: " +
                                       std::string(id));
            return static_cast<int>(value) >= static_cast<int>(target);
          },
          id);
      refresh_research(true);
    };
    const auto select_design = [&](std::string_view id) {
      const auto layout = ShipyardWorkspaceLayout::for_viewport(width, height);
      for (int attempt = 0; attempt < 128 && !shipyard_workspace_.design_bounds(
                                                 id, width, height);
           ++attempt)
        route({{InputEventType::Wheel, center(layout.designs), {}, -1.f}});
      const auto bounds = shipyard_workspace_.design_bounds(id, width, height);
      if (!bounds)
        throw std::runtime_error(
            "Ship design is not reachable through its scrolled list: " +
            std::string(id));
      click(center(*bounds));
      if (shipyard_workspace_.selected_design_id() != id)
        throw std::runtime_error("Ship design selection failed: " +
                                 std::string(id));
    };
    const auto authorize_ship = [&](std::string_view id) {
      open_shipyard();
      refresh_shipyard(true);
      select_design(id);
      click(
          center(ShipyardWorkspaceLayout::for_viewport(width, height).action));
      refresh_shipyard(true);
      if (std::ranges::none_of(
              shipyard_workspace_.view()->orders,
              [&](const auto &order) { return order.design_id == id; }))
        throw std::runtime_error(
            "Shipyard UI did not create the canonical order: " +
            std::string(id));
      ++fresh_progression_ship_actions_;
      service();
    };
    if (reload) {
      pause();
      fresh_progression_no_instant_ships_ = true;
      open_shipyard();
      route({{InputEventType::EscapePressed}});
      refresh_fleets(true);
      if (owned_fleets() < 2)
        throw std::runtime_error(
            "Fresh progression reload lost its completed ships.");
      open_shipyard();
      if (captured(fixed_capture) != before_payload)
        throw std::runtime_error(
            "Paused reload browsing changed the full Player17 payload.");
      for (const auto &fleet : world.fleets)
        if (fleet.civilization_id == world.player_civilization_id &&
            fleet.design_id == "warp_scout")
          fresh_progression_scout_id_ = fleet.id;
        else if (fleet.civilization_id == world.player_civilization_id &&
                 fleet.design_id == "science_vessel")
          fresh_progression_science_id_ = fleet.id;
      if (fresh_progression_scout_id_ < 0 || fresh_progression_science_id_ < 0)
        throw std::runtime_error(
            "Paused reload could not identify both canonical ships.");
      roundtrip();
      fresh_progression_after_days_ = frame.clock().simulation_days();
      InputSnapshot ready;
      ready.drawable_width = width;
      ready.drawable_height = height;
      (void)update(ready, width, height, 0., true);
      session_->request_save();
      return;
    }
    if (fresh_progression_before_days_ != 0. || owned_fleets() != 0)
      throw std::runtime_error(
          "Fresh progression did not start at day zero without fleets.");
    resume_normal();
    authorize_project("orbital_launch_complex");
    for (const auto &id :
         {"in_space_assembly", "asteroid_prospecting", "asteroid_mining",
          "vacuum_refining", "orbital_manufacturing", "orbital_shipyard"})
      research_to(id, ResearchMaturity::mature);
    authorize_project("orbital_shipyard");
    for (const auto &id : {"gravitational_physics", "field_theory",
                           "warp_metric_theory", "exotic_energy_coupling",
                           "micro_field_distortion", "warp_field_control"})
      research_to(id, ResearchMaturity::mature);
    authorize_project("warp_test_facility");
    advance_until(
        [&] {
          refresh_construction(true);
          const auto found =
              std::ranges::find(construction_workspace_.view()->projects,
                                std::string("warp_test_facility"),
                                &NativeConstructionProject::id);
          return found != construction_workspace_.view()->projects.end() &&
                 found->complete;
        },
        "warp_test_facility");
    for (int index = 0; index < 4; ++index)
      advance_quarter();
    research_to("prototype_warp_drive", ResearchMaturity::demonstrated);
    open_construction();
    advance_until(
        [&] {
          refresh_construction(true);
          const auto found = std::ranges::find(
              construction_workspace_.view()->projects,
              std::string("orbital_shipyard"), &NativeConstructionProject::id);
          return found != construction_workspace_.view()->projects.end() &&
                 found->complete;
        },
        "orbital_shipyard");
    authorize_ship("warp_scout");
    authorize_ship("science_vessel");
    fresh_progression_no_instant_ships_ = owned_fleets() == 0;
    if (!fresh_progression_no_instant_ships_)
      throw std::runtime_error("Ship authorization created an instant fleet.");
    advance_until([&] { return owned_fleets() >= 2; }, "first ships");
    refresh_shipyard(true);
    refresh_fleets(true);
    for (const auto &fleet : world.fleets)
      if (fleet.civilization_id == world.player_civilization_id &&
          fleet.design_id == "warp_scout")
        fresh_progression_scout_id_ = fleet.id;
      else if (fleet.civilization_id == world.player_civilization_id &&
               fleet.design_id == "science_vessel")
        fresh_progression_science_id_ = fleet.id;
    if (fresh_progression_scout_id_ < 0 || fresh_progression_science_id_ < 0)
      throw std::runtime_error(
          "Fresh progression could not identify both completed ships.");
    pause();
    open_shipyard();
    roundtrip();
    fresh_progression_after_days_ = frame.clock().simulation_days();
    InputSnapshot ready;
    ready.drawable_width = width;
    ready.drawable_height = height;
    if (!update(ready, width, height, 0., true))
      throw std::runtime_error(
          "Fresh progression could not establish its save boundary.");
    session_->request_save();
  }
  [[nodiscard]] std::string fresh_progression_smoke_status() const {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << std::boolalpha << "{\"mode\":\""
        << (fresh_progression_reload_ ? "paused_reload" : "fresh")
        << "\",\"seed\":115501,\"player_id\":"
        << session_->frame().runtime().world().campaign().player_civilization_id
        << ",\"before_days\":" << fresh_progression_before_days_
        << ",\"after_days\":" << fresh_progression_after_days_
        << ",\"scout_id\":" << fresh_progression_scout_id_
        << ",\"science_id\":" << fresh_progression_science_id_
        << ",\"research_actions\":" << fresh_progression_research_actions_
        << ",\"construction_actions\":"
        << fresh_progression_construction_actions_
        << ",\"ship_actions\":" << fresh_progression_ship_actions_
        << ",\"no_instant_ships\":" << fresh_progression_no_instant_ships_
        << ",\"offline_quarter_day_steps\":" << fresh_progression_quarter_steps_
        << ",\"ui_input\":true,\"paused\":"
        << (session_->frame().clock().speed() == StrategicSpeed::Paused)
        << ",\"save_roundtrip\":" << fresh_progression_roundtrip_ << '}';
    return out.str();
  }
  void prepare_first_exploration_smoke(
      int width, int height, Options::FirstExplorationMode mode,
      const std::function<void()> &pump,
      const std::function<void(std::string_view)> &capture) {
    constexpr double step_days = 1. / 64.;
    constexpr std::uint64_t maximum_steps = 256u * 64u;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(300);
    auto &frame = session_->frame();
    auto &world = frame.runtime().world().campaign();
    first_exploration_mode_ = mode;
    first_exploration_player_id_ = world.player_civilization_id;
    const auto player = std::ranges::find(
        world.civilizations, world.player_civilization_id, &Civilization::id);
    if (player == world.civilizations.end())
      throw std::runtime_error(
          "First exploration could not identify the player civilization.");
    first_exploration_origin_id_ = player->home_system_id;

    std::vector<const FleetState *> owned;
    for (const auto &fleet : world.fleets)
      if (fleet.civilization_id == world.player_civilization_id)
        owned.push_back(&fleet);
    if (owned.size() != 2 ||
        std::ranges::count_if(
            owned, [](const FleetState *fleet) { return fleet->is_active; }) !=
            2 ||
        std::ranges::count_if(owned,
                              [](const FleetState *fleet) {
                                return fleet->design_id ==
                                       std::optional<std::string>{"warp_scout"};
                              }) != 1 ||
        std::ranges::count_if(owned, [](const FleetState *fleet) {
          return fleet->design_id ==
                 std::optional<std::string>{"science_vessel"};
        }) != 1)
      throw std::runtime_error("First exploration requires exactly the two "
                               "active earned first ships.");
    const auto scout_source =
        std::ranges::find_if(owned, [](const FleetState *fleet) {
          return fleet->design_id == std::optional<std::string>{"warp_scout"} &&
                 fleet->role == FleetRole::Scout;
        });
    if (scout_source == owned.end())
      throw std::runtime_error(
          "First exploration could not identify the earned warp scout.");
    first_exploration_fleet_id_ = (*scout_source)->id;
    const auto live_scout = [&]() -> FleetState & {
      const auto found = std::ranges::find(
          world.fleets, first_exploration_fleet_id_, &FleetState::id);
      if (found == world.fleets.end() || !found->is_active ||
          found->civilization_id != world.player_civilization_id ||
          found->role != FleetRole::Scout ||
          found->design_id != std::optional<std::string>{"warp_scout"})
        throw std::runtime_error(
            "First exploration lost the selected earned scout.");
      return *found;
    };
    auto &initial_scout = live_scout();
    if (mode == Options::FirstExplorationMode::Depart &&
        (initial_scout.current_system_id != first_exploration_origin_id_ ||
         initial_scout.destination_system_id ||
         initial_scout.transit_phase != FleetTransitPhase::None))
      throw std::runtime_error("First exploration departure requires the idle "
                               "scout at its home system.");
    if (mode == Options::FirstExplorationMode::Resume &&
        (!initial_scout.destination_system_id ||
         initial_scout.transit_phase != FleetTransitPhase::InterstellarWarp ||
         initial_scout.transit_progress <= 0.))
      throw std::runtime_error(
          "First exploration continuation requires the saved partial warp.");
    if (mode == Options::FirstExplorationMode::Paused &&
        !((initial_scout.destination_system_id &&
           initial_scout.transit_phase == FleetTransitPhase::InterstellarWarp &&
           initial_scout.transit_progress > 0.) ||
          (!initial_scout.destination_system_id &&
           initial_scout.transit_phase == FleetTransitPhase::None &&
           initial_scout.current_system_id != first_exploration_origin_id_)))
      throw std::runtime_error(
          "First exploration paused browsing requires the saved warp or its "
          "completed scout destination.");

    const PlayerCampaignCaptureOptions before_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:15Z"};
    const auto captured = [&](const PlayerCampaignCaptureOptions &options) {
      return encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(frame.runtime(), options)
              .payload());
    };
    const auto before_selection = captured(before_capture);
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot input;
      input.drawable_width = width;
      input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error(
            "First exploration UI input closed the campaign.");
    };
    const auto click = [&](Point point, InputEventType pressed =
                                            InputEventType::LeftPressed) {
      route({{pressed, point},
             {pressed == InputEventType::LeftPressed
                  ? InputEventType::LeftReleased
                  : InputEventType::RightReleased,
              point}});
    };
    refresh_fleets(true);
    if (!fleet_workspace_.view())
      throw std::runtime_error(
          "First exploration did not receive its owned fleet view.");
    const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
    const auto scout_row =
        std::ranges::find(fleet_workspace_.view()->own_fleets,
                          first_exploration_fleet_id_, &NativeOwnFleet::id);
    if (scout_row == fleet_workspace_.view()->own_fleets.end())
      throw std::runtime_error(
          "First exploration scout was absent from the outliner.");
    const auto scout_index = static_cast<std::size_t>(
        scout_row - fleet_workspace_.view()->own_fleets.begin());
    click({layout.list.x + 12.f * layout.scale,
           layout.list.y +
               (static_cast<float>(scout_index) * 45.f + 20.f) * layout.scale});
    first_exploration_selected_ =
        fleet_controller_.selection() == first_exploration_fleet_id_;
    first_exploration_selection_read_only_ =
        captured(before_capture) == before_selection;
    if (!first_exploration_selected_ || !first_exploration_selection_read_only_)
      throw std::runtime_error("First exploration outliner selection was "
                               "rejected or changed Player17.");

    // Locate, wheel zoom and drag are all routed through ordinary input. They
    // make the tight near-Sol pair independently hittable without changing the
    // camera directly.
    click(center(layout.civilian_locate));
    if (!last_fleet_command_accepted_)
      throw std::runtime_error("First exploration Locate input was rejected.");
    Point zoom_anchor{static_cast<float>(width) * .5f,
                      static_cast<float>(height) * .5f};
    const auto before_zoom = camera_.pixels_per_world;
    for (int index = 0; index < 96 && camera_.pixels_per_world < 70.; ++index)
      route({{InputEventType::Wheel, zoom_anchor, {}, 1.f}});
    if (camera_.pixels_per_world <= before_zoom ||
        camera_.pixels_per_world < 70. || system_workspace_.visible())
      throw std::runtime_error(
          "First exploration map zoom remained captured after Locate.");
    const Point drag_start{static_cast<float>(width) * .34f,
                           static_cast<float>(height) * .72f};
    const Point drag_delta{40.f, -24.f};
    const Point drag_end{drag_start.x + drag_delta.x,
                         drag_start.y + drag_delta.y};
    route({{InputEventType::LeftPressed, drag_start},
           {InputEventType::PointerMove, drag_end, drag_delta},
           {InputEventType::LeftReleased, drag_end}});

    if (mode == Options::FirstExplorationMode::Depart) {
      std::vector<int> neighbors;
      for (const auto &lane :
           session_->frame().runtime().world().lanes().build())
        if (lane.connects(first_exploration_origin_id_))
          neighbors.push_back(lane.other(first_exploration_origin_id_));
      std::ranges::sort(neighbors);
      neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                      neighbors.end());
      for (const int candidate : neighbors) {
        if (world.knowledge.system_survey_level(world.player_civilization_id,
                                                candidate) >=
            SystemSurveyLevel::partially_surveyed)
          continue;
        const auto preview = fleet_controller_.preview_selected_route(
            frame, session_->cache().generation, candidate);
        if (preview.command_available && preview.route_supported &&
            preview.route_authoritative &&
            preview.route_system_ids ==
                std::vector<int>{first_exploration_origin_id_, candidate}) {
          first_exploration_target_id_ = candidate;
          break;
        }
      }
      if (first_exploration_target_id_ < 0)
        throw std::runtime_error(
            "First exploration found no direct unsurveyed authoritative lane.");
    } else {
      first_exploration_target_id_ =
          initial_scout.destination_system_id.value_or(
              initial_scout.current_system_id.value_or(-1));
      if (first_exploration_target_id_ < 0)
        throw std::runtime_error(
            "First exploration saved warp has no destination.");
    }
    first_exploration_lane_connected_ = std::ranges::any_of(
        session_->frame().runtime().world().lanes().build(),
        [&](const InterstellarLane &lane) {
          return lane.connects(first_exploration_origin_id_) &&
                 lane.other(first_exploration_origin_id_) ==
                     first_exploration_target_id_;
        });
    if (!first_exploration_lane_connected_)
      throw std::runtime_error(
          "First exploration target is not the direct home-system neighbor.");

    first_exploration_before_days_ = frame.clock().simulation_days();
    first_exploration_revision_before_ = initial_scout.mission_order_revision;
    first_exploration_phase_before_ = initial_scout.transit_phase;
    first_exploration_survey_before_ =
        static_cast<int>(world.knowledge.system_survey_level(
            world.player_civilization_id, first_exploration_target_id_));
    first_exploration_survey_progress_before_ =
        world.knowledge.system_survey_progress(world.player_civilization_id,
                                               first_exploration_target_id_);
    first_exploration_transit_progress_before_ = initial_scout.transit_progress;
    if (mode == Options::FirstExplorationMode::Paused &&
        !initial_scout.destination_system_id &&
        (first_exploration_survey_before_ <
             static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
         first_exploration_survey_progress_before_ + 1e-9 <
             ExplorationSimulation::scout_reconnaissance_progress))
      throw std::runtime_error(
          "First exploration completed paused browse lacks natural scout "
          "reconnaissance.");
    const auto observe_phase = [&] {
      const auto value = static_cast<int>(live_scout().transit_phase);
      if (std::ranges::find(first_exploration_seen_phases_, value) ==
          first_exploration_seen_phases_.end())
        first_exploration_seen_phases_.push_back(value);
    };
    observe_phase();

    const auto ui = NativeUiLayout::for_viewport(width, height);
    const auto pause = [&] {
      if (frame.clock().speed() != StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Paused)
        throw std::runtime_error("First exploration pause input was rejected.");
    };
    const auto resume_normal = [&] {
      if (frame.clock().speed() == StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Normal)
        route({{InputEventType::KeyPressed, {}, {}, 0.f, {}, 0, '1'}});
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().effective_multiplier() != 1.)
        throw std::runtime_error(
            "First exploration requires the ordinary Normal clock.");
    };
    const auto capture_phase = [&](std::string_view tag) {
      pause();
      refresh_fleets(true);
      refresh_system_travel(true);
      for (int frame_index = 0; frame_index < 4; ++frame_index)
        pump();
      capture(tag);
      resume_normal();
    };
    const auto step = [&] {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error(
            "First exploration exceeded its 300-second wall bound.");
      if (first_exploration_steps_ >= maximum_steps)
        throw std::runtime_error(
            "First exploration exceeded its 256-day step bound.");
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().backlog_days() != 0.)
        throw std::runtime_error(
            "First exploration left Normal speed or accumulated backlog.");
      const auto before = frame.clock().simulation_days();
      const auto result = frame.advance(step_days);
      const auto after = frame.clock().simulation_days();
      if (result.route != CampaignFrameRoute::Strategic ||
          result.completed_substeps.size() != 1 ||
          result.completed_substeps.front() != step_days ||
          after - before != step_days || frame.clock().backlog_days() != 0.)
        throw std::runtime_error(
            "First exploration left exact single-frame 1/64-day stepping.");
      ++first_exploration_steps_;
      observe_phase();
      if (first_exploration_steps_ % 64 == 0) {
        refresh_fleets(true);
        refresh_system_travel(true);
        pump();
      }
    };

    if (mode == Options::FirstExplorationMode::Depart) {
      const auto system = std::ranges::find(
          world.systems, first_exploration_target_id_, &StellarSystem::id);
      if (system == world.systems.end())
        throw std::runtime_error(
            "First exploration target disappeared before preview.");
      const auto preview_before = captured(before_capture);
      const auto target_point = camera_.project(
          {system->position.x, system->position.y}, width, height);
      if (layout.panel.contains(target_point) || target_point.x < 0.f ||
          target_point.y < 0.f || target_point.x >= width ||
          target_point.y >= height)
        throw std::runtime_error(
            "First exploration camera input did not expose the destination.");
      click(target_point, InputEventType::RightPressed);
      const auto &preview = fleet_workspace_.preview();
      first_exploration_preview_read_only_ =
          captured(before_capture) == preview_before;
      if (!preview || preview->fleet_id != first_exploration_fleet_id_ ||
          preview->target_system_id != first_exploration_target_id_ ||
          preview->expected_mission_order_revision !=
              first_exploration_revision_before_ ||
          !preview->command_available || !preview->route_supported ||
          !preview->route_authoritative ||
          preview->route_system_ids !=
              std::vector<int>{first_exploration_origin_id_,
                               first_exploration_target_id_} ||
          !first_exploration_preview_read_only_)
        throw std::runtime_error("First exploration right-click preview lost "
                                 "identity or authority.");
      click(center(layout.confirm));
      ++first_exploration_input_orders_;
      if (!last_fleet_command_accepted_ ||
          live_scout().mission_order_revision !=
              first_exploration_revision_before_ + 1 ||
          live_scout().destination_system_id != first_exploration_target_id_ ||
          live_scout().transit_phase != FleetTransitPhase::None ||
          static_cast<int>(world.knowledge.system_survey_level(
              world.player_civilization_id, first_exploration_target_id_)) !=
              first_exploration_survey_before_)
        throw std::runtime_error("First exploration confirmation did not "
                                 "create only the pending travel order.");
      // Core assigns the route on confirmation and starts local departure on
      // the next simulation step. An accepted order is not instant movement.
      resume_normal();
      while (live_scout().transit_phase == FleetTransitPhase::None)
        step();
      if (live_scout().transit_phase != FleetTransitPhase::LocalDeparture)
        throw std::runtime_error(
            "First exploration did not observe timed local departure.");
      observe_phase();
      capture_phase("departure");
      while (live_scout().transit_phase !=
                 FleetTransitPhase::InterstellarWarp ||
             live_scout().transit_progress <= 0.)
        step();
    } else if (mode == Options::FirstExplorationMode::Paused) {
      // Browse the exact target after real camera gestures. Selection and map
      // inspection are client state and must leave the complete payload fixed.
      const auto system = std::ranges::find(
          world.systems, first_exploration_target_id_, &StellarSystem::id);
      if (system == world.systems.end())
        throw std::runtime_error(
            "First exploration paused browse lost its destination.");
      click(camera_.project({system->position.x, system->position.y}, width,
                            height));
      // Map hits can cycle overlapping own vessels. Keep the exact earned
      // scout selected for the final mission-status capture.
      click({layout.list.x + 12.f * layout.scale,
             layout.list.y +
                 (static_cast<float>(scout_index) * 45.f + 20.f) * layout.scale});
      pause();
      if (captured(before_capture) != before_selection)
        throw std::runtime_error("First exploration paused browsing changed "
                                 "the full Player17 payload.");
    } else {
      resume_normal();
      bool arrival_captured{};
      while (true) {
        const auto &scout = live_scout();
        const auto level = world.knowledge.system_survey_level(
            world.player_civilization_id, first_exploration_target_id_);
        const auto progress = world.knowledge.system_survey_progress(
            world.player_civilization_id, first_exploration_target_id_);
        if (scout.transit_phase == FleetTransitPhase::None &&
            !scout.destination_system_id &&
            scout.current_system_id == first_exploration_target_id_ &&
            scout.reconnaissance_system_id == first_exploration_target_id_ &&
            scout.reconnaissance_days_completed + 1e-9 >=
                ExplorationSimulation::scout_reconnaissance_days &&
            level >= SystemSurveyLevel::partially_surveyed &&
            progress + 1e-9 >=
                ExplorationSimulation::scout_reconnaissance_progress)
          break;
        step();
        if (!arrival_captured &&
            live_scout().transit_phase == FleetTransitPhase::LocalArrival) {
          capture_phase("arrival");
          arrival_captured = true;
        }
      }
      if (!arrival_captured)
        throw std::runtime_error(
            "First exploration did not observe the local-arrival phase.");
      refresh_fleets(true);
      click(center(layout.civilian_locate));
      const auto target = std::ranges::find(
          world.systems, first_exploration_target_id_, &StellarSystem::id);
      if (target == world.systems.end())
        throw std::runtime_error(
            "First exploration surveyed destination disappeared.");
      const auto target_point = camera_.project(
          {target->position.x, target->position.y}, width, height);
      route({{InputEventType::LeftPressed, target_point, {}, 0.f, {}, 2},
             {InputEventType::LeftReleased, target_point}});
      if (!system_workspace_.visible() ||
          system_workspace_.system_id() != first_exploration_target_id_)
        throw std::runtime_error("First exploration could not enter the "
                                 "surveyed destination through UI input.");
      pause();
    }

    first_exploration_after_days_ = frame.clock().simulation_days();
    if (fleet_controller_.selection() != first_exploration_fleet_id_)
      throw std::runtime_error("First exploration lost the scout selection.");
    first_exploration_revision_after_ = live_scout().mission_order_revision;
    first_exploration_phase_after_ = live_scout().transit_phase;
    first_exploration_survey_after_ =
        static_cast<int>(world.knowledge.system_survey_level(
            world.player_civilization_id, first_exploration_target_id_));
    first_exploration_survey_progress_after_ =
        world.knowledge.system_survey_progress(world.player_civilization_id,
                                               first_exploration_target_id_);
    first_exploration_transit_progress_after_ = live_scout().transit_progress;
    if (first_exploration_after_days_ !=
            first_exploration_before_days_ +
                static_cast<double>(first_exploration_steps_) * step_days ||
        frame.clock().backlog_days() != 0.)
      throw std::runtime_error(
          "First exploration final time did not match its exact step count.");
    if (mode == Options::FirstExplorationMode::Depart &&
        (first_exploration_revision_after_ !=
             first_exploration_revision_before_ + 1 ||
         first_exploration_phase_after_ !=
             FleetTransitPhase::InterstellarWarp ||
         first_exploration_transit_progress_after_ <= 0. ||
         first_exploration_survey_after_ != first_exploration_survey_before_ ||
         first_exploration_survey_progress_after_ !=
             first_exploration_survey_progress_before_))
      throw std::runtime_error(
          "First exploration departure evidence is not a partial warp.");
    if (mode != Options::FirstExplorationMode::Depart &&
        first_exploration_revision_after_ != first_exploration_revision_before_)
      throw std::runtime_error("First exploration continuation changed the "
                               "persisted mission revision.");
    if (mode == Options::FirstExplorationMode::Paused &&
        (first_exploration_steps_ != 0 ||
         first_exploration_input_orders_ != 0 ||
         first_exploration_after_days_ != first_exploration_before_days_))
      throw std::runtime_error(
          "First exploration paused mode advanced or issued an order.");
    if (mode == Options::FirstExplorationMode::Resume &&
        (first_exploration_phase_after_ != FleetTransitPhase::None ||
         first_exploration_survey_after_ <
             static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
         first_exploration_survey_progress_after_ + 1e-9 <
             ExplorationSimulation::scout_reconnaissance_progress))
      throw std::runtime_error("First exploration resume did not finish "
                               "natural scout reconnaissance.");

    pause();
    const PlayerCampaignCaptureOptions final_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:15Z"};
    const auto bytes = captured(final_capture);
    auto restored = restore_player_campaign_v17_json(
        load_adaptive_research_strategic_runtime(asset_root_ /
                                                 "Data/research/v1"),
        bytes);
    if (restored.simulation_days() != final_capture.simulation_days)
      throw std::runtime_error(
          "First exploration Player17 restore changed the clock.");
    auto resumed = std::move(restored).activate();
    const auto recaptured = encode_player_campaign_v17_json(
        PreparedPlayerCampaignSave::capture(resumed, final_capture).payload());
    const auto first = nlohmann::json::parse(bytes);
    const auto second = nlohmann::json::parse(recaptured);
    if (first != second)
      throw std::runtime_error(
          "First exploration Player17 roundtrip changed the campaign: " +
          nlohmann::json::diff(first, second).dump().substr(0, 4000));
    first_exploration_roundtrip_ = true;
    InputSnapshot ready;
    ready.drawable_width = width;
    ready.drawable_height = height;
    if (!update(ready, width, height, 0., true))
      throw std::runtime_error(
          "First exploration could not establish its save boundary.");
    session_->request_save();
  }
  [[nodiscard]] std::string first_exploration_smoke_status() const {
    const auto mode =
        first_exploration_mode_ == Options::FirstExplorationMode::Depart
            ? "depart"
        : first_exploration_mode_ == Options::FirstExplorationMode::Paused
            ? "paused"
            : "resume";
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << std::boolalpha << "{\"mode\":\"" << mode
        << "\",\"seed\":115501,\"player_id\":" << first_exploration_player_id_
        << ",\"fleet_id\":" << first_exploration_fleet_id_
        << ",\"origin_id\":" << first_exploration_origin_id_
        << ",\"target_id\":" << first_exploration_target_id_
        << ",\"before_days\":" << first_exploration_before_days_
        << ",\"after_days\":" << first_exploration_after_days_
        << ",\"input_orders\":" << first_exploration_input_orders_
        << ",\"steps\":" << first_exploration_steps_
        << ",\"step_days\":" << (1. / 64.)
        << ",\"revision_before\":" << first_exploration_revision_before_
        << ",\"revision_after\":" << first_exploration_revision_after_
        << ",\"phase_before\":"
        << static_cast<int>(first_exploration_phase_before_)
        << ",\"phase_after\":"
        << static_cast<int>(first_exploration_phase_after_)
        << ",\"survey_before\":" << first_exploration_survey_before_
        << ",\"survey_after\":" << first_exploration_survey_after_
        << ",\"survey_progress_before\":"
        << first_exploration_survey_progress_before_
        << ",\"survey_progress_after\":"
        << first_exploration_survey_progress_after_
        << ",\"transit_progress_before\":"
        << first_exploration_transit_progress_before_
        << ",\"transit_progress_after\":"
        << first_exploration_transit_progress_after_ << ",\"seen_phases\":[";
    for (std::size_t index = 0; index < first_exploration_seen_phases_.size();
         ++index) {
      if (index)
        out << ',';
      out << first_exploration_seen_phases_[index];
    }
    out << "],\"selected\":" << first_exploration_selected_
        << ",\"selection_read_only\":" << first_exploration_selection_read_only_
        << ",\"preview_read_only\":" << first_exploration_preview_read_only_
        << ",\"lane_connected\":" << first_exploration_lane_connected_
        << ",\"paused\":"
        << (session_->frame().clock().speed() == StrategicSpeed::Paused)
        << ",\"save_roundtrip\":" << first_exploration_roundtrip_ << '}';
    return out.str();
  }
  void prepare_settlement_completion_smoke(int width, int height,
      Options::SettlementCompletionMode mode, const std::function<void()> &pump,
      const std::function<void()> &capture_progress) {
    auto &frame = session_->frame();
    const auto &world = frame.runtime().world().campaign();
    const int player_id = world.player_civilization_id;
    const bool paused = mode == Options::SettlementCompletionMode::Paused;
    if (frame.clock().speed() != StrategicSpeed::Paused)
      throw std::runtime_error("Settlement completion requires a paused source.");
    const auto snapshot = [&] {
      return nlohmann::json::parse(encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(frame.runtime(),
              {frame.clock().simulation_days(), STELLAR_GAME_VERSION,
               "2044-05-06T07:08:21Z"}).payload()));
    };
    const auto original = snapshot();
    const double before_days = frame.clock().simulation_days();
    const auto colonies_before = world.colonies.size();
    std::vector<NativeSettlementMissionView> active;
    for (auto &view : settlement_controller_.build(frame, session_->cache().generation))
      if (has_active_settlement_target(view)) active.push_back(std::move(view));
    int fleet_id = -1, system_id = -1, body_id = -1, colony_id = -1, revision = -1;
    SettlementKind kind = SettlementKind::Colony;
    if (!paused) {
      if (active.size() != 1)
        throw std::runtime_error("Settlement completion requires one authorized active expedition.");
      const auto &mission = active.front();
      fleet_id = mission.fleet_id;
      revision = mission.mission_order_revision;
      body_id = mission.settlement_body_id.value_or(mission.destination_body_id.value_or(-1));
      const auto vessel = std::ranges::find(world.fleets, fleet_id, &FleetState::id);
      if (vessel == world.fleets.end()) throw std::runtime_error("Settlement vessel missing.");
      system_id = mission.destination_system_id.value_or(vessel->current_system_id.value_or(-1));
      kind = mission.kind == NativeSettlementMissionKind::Colony
          ? SettlementKind::Colony : SettlementKind::ResourceOutpost;
      if (mission.settlement_days_completed >= mission.establishment_days - 5.)
        throw std::runtime_error("Settlement completion needs at least five workdays remaining.");
    } else {
      const auto player = std::ranges::find(world.civilizations, player_id, &Civilization::id);
      if (player == world.civilizations.end() || !active.empty())
        throw std::runtime_error("Founded reload requires no active expedition.");
      for (const auto &colony : world.colonies) {
        if (colony.civilization_id != player_id || colony.system_id == player->home_system_id)
          continue;
        if (colony_id >= 0) throw std::runtime_error("Founded reload target is ambiguous.");
        colony_id = colony.id; system_id = colony.system_id;
        body_id = colony.planetary_body_id.value_or(-1); kind = colony.kind;
      }
      for (const auto &fleet : world.fleets) {
        if (fleet.civilization_id != player_id || fleet.role != FleetRole::Colony || fleet.is_active)
          continue;
        if (fleet_id >= 0) throw std::runtime_error("Founded reload vessel is ambiguous.");
        fleet_id = fleet.id; revision = fleet.mission_order_revision;
      }
    }
    if (fleet_id < 0 || system_id < 0 || body_id < 0 ||
        world.knowledge.system_survey_level(player_id, system_id) != SystemSurveyLevel::fully_surveyed)
      throw std::runtime_error("Settlement completion lacks a surveyed, exact target.");
    const auto find_colony = [&]() -> const Colony * {
      const auto found = std::ranges::find_if(world.colonies, [&](const Colony &colony) {
        return colony.civilization_id == player_id && colony.system_id == system_id &&
               colony.planetary_body_id == body_id;
      });
      return found == world.colonies.end() ? nullptr : &*found;
    };
    if (!paused && find_colony()) throw std::runtime_error("Expedition target was already settled.");
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot input; input.drawable_width = width; input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error("Settlement completion input closed the game.");
    };
    const auto click = [&](Point at) {
      route({{InputEventType::LeftPressed, at}, {InputEventType::LeftReleased, at}});
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    const auto wait_art = [&] {
      do {
        if (std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error("Settlement completion artwork timed out.");
        pump();
      } while (!artwork_ready());
    };
    if (!paused) {
      refresh_fleets(true);
      const auto &fleets = fleet_workspace_.view()->own_fleets;
      const auto fleet = std::ranges::find(fleets, fleet_id, &NativeOwnFleet::id);
      if (fleet == fleets.end()) throw std::runtime_error("Expedition missing from outliner.");
      const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
      const auto index = static_cast<float>(fleet - fleets.begin());
      click({layout.list.x + 12.f * layout.scale,
             layout.list.y + (index * 45.f + 20.f) * layout.scale});
      if (fleet_controller_.selection() != fleet_id)
        throw std::runtime_error("Expedition outliner selection failed.");
    }
    if (!enter_system(system_id, width, height))
      throw std::runtime_error("Settlement target system could not open.");
    const auto select_body = [&] {
      const auto spatial = project_system(*system_workspace_.snapshot());
      const auto marker = std::ranges::find(spatial.bodies, body_id, &SystemSpatialBodyMarker::body_id);
      if (marker == spatial.bodies.end()) throw std::runtime_error("Settlement body is not visible.");
      const auto at = system_workspace_.viewport()->world_to_screen(marker->offset_x, marker->offset_y);
      click({at.x, at.y});
      if (system_workspace_.selected_body_id() != body_id)
        throw std::runtime_error("Settlement body click missed its target.");
    };
    select_body();
    constexpr double step_days = 1. / 64.;
    std::uint64_t steps{};
    bool progress_captured{};
    using stellar::native_campaign_feedback::FeedbackKind;
    const auto feedback_before = feedback_.counts().count(FeedbackKind::ColonyFounded);
    if (!paused) {
      click(center(NativeUiLayout::for_viewport(width, height).pause));
      while (!find_colony()) {
        if (++steps > 1024 * 64 || std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error("Settlement did not finish within 1024 days / 300 seconds.");
        if (frame.clock().speed() != StrategicSpeed::Normal || frame.clock().backlog_days() != 0.)
          throw std::runtime_error("Settlement completion left Normal speed.");
        const auto before = frame.clock().simulation_days();
        const auto result = frame.advance(step_days);
        if (result.route != CampaignFrameRoute::Strategic || result.completed_substeps != std::vector<double>{step_days} ||
            frame.clock().simulation_days() - before != step_days || frame.clock().backlog_days() != 0.)
          throw std::runtime_error("Settlement completion left exact 1/64-day stepping.");
        publish_feedback(result);
        const auto vessel = std::ranges::find(world.fleets, fleet_id, &FleetState::id);
        // Core clears the consumed vessel's route once, incrementing revision.
        if (vessel == world.fleets.end() ||
            vessel->mission_order_revision != revision + (find_colony() ? 1 : 0))
          throw std::runtime_error("Settlement completion lost or reordered the expedition.");
        if (!progress_captured && !find_colony() && vessel->settlement_days_completed >= 5.) {
          click(center(NativeUiLayout::for_viewport(width, height).pause));
          refresh_system(true); refresh_fleets(true); refresh_settlement_status();
          wait_art(); capture_progress(); progress_captured = true;
          click(center(NativeUiLayout::for_viewport(width, height).pause));
        }
        if (steps % 64 == 0) { refresh_system(true); refresh_fleets(true); pump(); }
      }
      click(center(NativeUiLayout::for_viewport(width, height).pause));
      if (!progress_captured || world.colonies.size() != colonies_before + 1 ||
          feedback_.counts().count(FeedbackKind::ColonyFounded) != feedback_before + 1)
        throw std::runtime_error("Settlement lacks timed work, one founded colony or normal completion feedback.");
    }
    const auto *colony = find_colony();
    const auto consumed = std::ranges::find(world.fleets, fleet_id, &FleetState::id);
    if (!colony || colony->kind != kind || colony->population_millions <= 0. ||
        consumed == world.fleets.end() || consumed->is_active || consumed->embarked_population_millions != 0. ||
        consumed->mission_order_revision != revision + (paused ? 0 : 1))
      throw std::runtime_error("Founding did not preserve population or consume the authorized vessel.");
    colony_id = colony->id;
    const auto before_inspection = snapshot();
    refresh_system(true); select_body(); refresh_colony_entry(true);
    click(center(SystemWorkspaceLayout::for_viewport(width, height).colony_action));
    if (!colony_workspace_.visible() || !colony_workspace_.view() ||
        colony_workspace_.view()->colony_id != colony_id)
      throw std::runtime_error("Founded colony could not open through its normal planet action.");
    wait_art();
    if (snapshot() != before_inspection || (paused && snapshot() != original))
      throw std::runtime_error("Colony inspection or paused reload changed Player17.");
    const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(), STELLAR_GAME_VERSION,
                                               "2044-05-06T07:08:21Z"};
    auto restored = restore_player_campaign_v17_json(
        load_adaptive_research_strategic_runtime(asset_root_ / "Data/research/v1"),
        encode_player_campaign_v17_json(PreparedPlayerCampaignSave::capture(frame.runtime(), options).payload()));
    auto resumed = std::move(restored).activate();
    if (nlohmann::json::parse(encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(resumed, options).payload())) != snapshot())
      throw std::runtime_error("Founded colony changed during full Player17 restoration.");
    settlement_completion_proof_ = nlohmann::json{
        {"mode", paused ? "paused" : "resume"}, {"player_id", player_id}, {"fleet_id", fleet_id},
        {"system_id", system_id}, {"body_id", body_id}, {"colony_id", colony_id},
        {"kind", kind == SettlementKind::Colony ? "colony" : "outpost"}, {"revision", consumed->mission_order_revision},
        {"before_days", before_days}, {"after_days", frame.clock().simulation_days()},
        {"steps", steps}, {"step_days", step_days}, {"colonies_before", colonies_before},
        {"colonies_after", world.colonies.size()}, {"consumed", true}, {"opened_colony", true},
        {"read_only", true}, {"feedback", !paused}, {"roundtrip", true}}.dump();
    InputSnapshot ready; ready.drawable_width = width; ready.drawable_height = height;
    if (!update(ready, width, height, 0., true)) throw std::runtime_error("Founded save boundary failed.");
    session_->request_save();
  }
  [[nodiscard]] const std::string &settlement_completion_smoke_status() const { return settlement_completion_proof_; }

  void prepare_earned_surface_smoke(
      int width, int height, Options::EarnedSurfaceMode mode,
      const std::function<void()> &pump,
      const std::function<void(std::string_view)> &capture) {
    auto &frame = session_->frame();
    const auto &world = frame.runtime().world().campaign();
    constexpr int expected_player = 0, expected_system = 8,
                  expected_body = 8004, expected_colony = 9;
    const bool paused = mode == Options::EarnedSurfaceMode::Paused;
    if (frame.clock().speed() != StrategicSpeed::Paused)
      throw std::runtime_error("Earned surface requires a paused source.");
    const auto saved = [&] {
      return nlohmann::json::parse(encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(
              frame.runtime(), {frame.clock().simulation_days(),
                                STELLAR_GAME_VERSION, "2044-05-06T07:08:21Z"})
              .payload()));
    };
    const auto original = saved();
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot in;
      in.drawable_width = width;
      in.drawable_height = height;
      in.pointer = events.empty() ? Point{} : events.back().position;
      in.events = std::move(events);
      if (!update(in, width, height, 0., false))
        throw std::runtime_error("Earned surface input closed the campaign.");
    };
    const auto click = [&](Point p) {
      route({{InputEventType::LeftPressed, p},
             {InputEventType::LeftReleased, p}});
    };
    if (world.player_civilization_id != expected_player)
      throw std::runtime_error("Earned surface source has the wrong player.");
    const auto colony =
        std::ranges::find(world.colonies, expected_colony, &Colony::id);
    if (colony == world.colonies.end() ||
        colony->system_id != expected_system ||
        colony->planetary_body_id != expected_body ||
        colony->population_millions < 250.)
      throw std::runtime_error("Earned surface requires Xanthe.");
    if ((!paused && !colony->surface_buildings.empty()) ||
        (paused && colony->surface_buildings.size() != 1))
      throw std::runtime_error(
          "Earned surface source has the wrong building state.");
    if (!enter_system(expected_system, width, height))
      throw std::runtime_error("Earned surface could not enter Xanthe system.");
    const auto spatial = project_system(*system_workspace_.snapshot());
    const auto marker = std::ranges::find(spatial.bodies, expected_body,
                                          &SystemSpatialBodyMarker::body_id);
    if (marker == spatial.bodies.end())
      throw std::runtime_error("Earned surface body is absent.");
    const auto body_point = system_workspace_.viewport()->world_to_screen(
        marker->offset_x, marker->offset_y);
    click({body_point.x, body_point.y});
    if (system_workspace_.selected_body_id() != expected_body)
      throw std::runtime_error("Earned surface body selection failed.");
    refresh_colony_entry(true);
    click(center(
        SystemWorkspaceLayout::for_viewport(width, height).colony_action));
    if (!colony_workspace_.visible() || !colony_workspace_.view() ||
        colony_workspace_.view()->colony_id != expected_colony)
      throw std::runtime_error("Earned surface colony action failed.");
    const auto colony_readiness_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (!artwork_ready()) {
      if (std::chrono::steady_clock::now() > colony_readiness_deadline)
        throw std::runtime_error("Earned colony artwork timed out.");
      pump();
    }
    if (!paused)
      capture("colony");
    click(center(
        ColonyWorkspaceLayout::for_viewport(width, height).open_surface));
    if (!surface_workspace_.visible() || !surface_workspace_.view())
      throw std::runtime_error(
          "Earned surface did not open the actual colony surface.");
    const auto readiness_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (!artwork_ready()) {
      if (std::chrono::steady_clock::now() > readiness_deadline)
        throw std::runtime_error("Earned surface artwork timed out.");
      pump();
    }
    const auto initial = *surface_workspace_.view();
    const double before_days = frame.clock().simulation_days();
    const double industry_before = initial.industry_per_day;
    const auto option = std::ranges::find(initial.available_buildings,
                                          std::string("fabricator"),
                                          &NativeSurfaceBuildOption::type_id);
    if (option == initial.available_buildings.end() ||
        option->authorization_budget_units != 50. ||
        option->industry_cost != 450. || option->power_demand != 2. ||
        option->workforce_required_millions != .04)
      throw std::runtime_error(
          "Earned surface fabricator is not unlocked with canonical costs.");
    const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
    const auto inspect_site = [&](NativeSurfaceSite inspected) {
      const auto before_selection = saved();
      if (surface_workspace_.selected_type_id())
        route({{InputEventType::EscapePressed}});
      const auto at = surface_workspace_.viewport().world_to_screen(
          inspected.x, inspected.z, layout.terrain);
      click(at);
      if (surface_workspace_.selected_building_id() !=
          std::optional<int>{inspected.building_id})
        throw std::runtime_error(
            "Earned surface could not select its fabricator.");
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(300);
      // Selection changes the scene's requested building imagery; make one real
      // draw before consulting readiness so a previously-ready frame cannot
      // hide a late asset.
      pump();
      while (!artwork_ready()) {
        if (std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error(
              "Earned surface building artwork timed out.");
        pump();
      }
      (void)scene(width, height);
      const auto art = surface_buildings_.stats();
      if (art.ready < 2 || art.failed || art.pending || art.deferred ||
          surface_workspace_.scene_diagnostics().replaced_structures < 2)
        throw std::runtime_error(
            "Earned surface did not draw its prepared hub and fabricator.");
      if (saved() != before_selection)
        throw std::runtime_error(
            "Earned surface building inspection changed the paused campaign.");
    };
    if (paused) {
      const auto persisted = std::ranges::find_if(
          initial.construction_sites, [](const NativeSurfaceSite &s) {
            return s.type_id == "fabricator" && s.complete;
          });
      if (persisted == initial.construction_sites.end() ||
          persisted->industry_cost != 450. ||
          persisted->industry_progress != 450. || !persisted->powered ||
          !persisted->staffed || !persisted->enabled ||
          persisted->efficiency <= 0. || initial.industry_per_day <= 0.)
        throw std::runtime_error(
            "Paused earned surface lacks its completed powered fabricator.");
      inspect_site(*persisted);
      InputSnapshot tick;
      tick.drawable_width = width;
      tick.drawable_height = height;
      (void)update(tick, width, height, 1., true);
      if (saved() != original || frame.clock().simulation_days() != before_days)
        throw std::runtime_error(
            "Paused earned surface reload changed Player17.");
      earned_surface_proof_ = nlohmann::json{
          {"mode", "paused"},
          {"player_id", expected_player},
          {"system_id", expected_system},
          {"body_id", expected_body},
          {"colony_id", expected_colony},
          {"building_id", persisted->building_id},
          {"type_id", persisted->type_id},
          {"x", persisted->x},
          {"z", persisted->z},
          {"rotation", persisted->rotation_degrees},
          {"before_days", before_days},
          {"after_days", before_days},
          {"steps", 0},
          {"step_days", 1. / 64.},
          {"authorization", 0},
          {"treasury_before", initial.treasury_budget_units},
          {"treasury_after", initial.treasury_budget_units},
          {"industry_cost", persisted->industry_cost},
          {"industry_progress", persisted->industry_progress},
          {"complete", persisted->complete},
          {"powered", persisted->powered},
          {"staffed", persisted->staffed},
          {"enabled", persisted->enabled},
          {"efficiency", persisted->efficiency},
          {"industry_before", industry_before},
          {"industry_after", initial.industry_per_day},
          {"cancel_unchanged", true},
          {"opened_surface", true},
          {"roundtrip", true}}.dump();
      session_->request_save();
      return;
    }
    const auto option_index =
        static_cast<std::size_t>(option - initial.available_buildings.begin());
    click(
        {layout.palette_rows.x + 14.f * layout.scale,
         layout.palette_rows.y +
             (static_cast<float>(option_index) * 78.f + 20.f) * layout.scale});
    if (surface_workspace_.selected_type_id() !=
        std::optional<std::string>{"fabricator"})
      throw std::runtime_error(
          "Earned surface palette did not select fabricator.");
    std::optional<NativeSurfacePlacementQuote> quote;
    for (float z = -70.f; z <= 70.f && !quote; z += 35.f)
      for (float x = -70.f; x <= 70.f; x += 7.f) {
        auto candidate = surface_controller_.preview_placement(
            frame, session_->cache().generation, *surface_workspace_.view(),
            "fabricator", x, z, 0.f);
        if (candidate.accepted) {
          quote = candidate;
          break;
        }
      }
    if (!quote)
      throw std::runtime_error("Earned surface found no fabricator position.");
    (void)surface_controller_.cancel_quote(session_->cache().generation,
                                           quote->quote_revision);
    const auto point = surface_workspace_.viewport().world_to_screen(
        quote->x, quote->z, layout.terrain);
    click(point);
    if (!surface_workspace_.placement_quote() ||
        !surface_workspace_.placement_quote()->accepted ||
        surface_workspace_.placement_quote()->authorization_budget_units != 50.)
      throw std::runtime_error(
          "Earned surface quote failed canonical authorization.");
    capture("review");
    click(center(layout.cancel));
    refresh_surface(true);
    const bool cancelled =
        surface_workspace_.view()->construction_sites.empty() &&
        surface_workspace_.view()->treasury_budget_units ==
            initial.treasury_budget_units &&
        saved() == original;
    if (!cancelled)
      throw std::runtime_error(
          "Earned surface cancel changed the paused colony.");
    click(point);
    if (!surface_workspace_.placement_quote() ||
        !surface_workspace_.placement_quote()->accepted ||
        surface_workspace_.placement_quote()->authorization_budget_units != 50.)
      throw std::runtime_error("Earned surface quote reopen failed.");
    click(center(layout.confirm));
    auto site = std::ranges::find_if(
        surface_workspace_.view()->construction_sites,
        [](const auto &s) { return s.type_id == "fabricator"; });
    if (site == surface_workspace_.view()->construction_sites.end() ||
        site->complete || site->industry_cost != 450. ||
        site->industry_progress != 0. ||
        std::abs(surface_workspace_.view()->treasury_budget_units -
                 (initial.treasury_budget_units - 50.)) > 1e-9)
      throw std::runtime_error("Earned surface did not create the exact paid "
                               "unfinished fabricator.");
    const int id = site->building_id;
    const float x = site->x, z = site->z, rotation = site->rotation_degrees;
    const double treasury_after =
        surface_workspace_.view()->treasury_budget_units;
    constexpr double step_days = 1. / 64.;
    std::uint64_t steps{};
    bool captured{};
    frame.clock().set_speed(StrategicSpeed::Normal);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (!site->complete) {
      if (++steps > 1024 * 64 || std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error(
            "Earned surface did not finish within 1024 days / 300 seconds.");
      const auto before = frame.clock().simulation_days();
      const auto r = frame.advance(step_days);
      if (r.completed_substeps != std::vector<double>{step_days} ||
          frame.clock().simulation_days() - before != step_days ||
          frame.clock().backlog_days() != 0.)
        throw std::runtime_error("Earned surface lost exact stepping.");
      publish_feedback(r);
      refresh_surface(true);
      site = std::ranges::find(surface_workspace_.view()->construction_sites,
                               id, &NativeSurfaceSite::building_id);
      if (site == surface_workspace_.view()->construction_sites.end())
        throw std::runtime_error("Earned surface lost its building.");
      if (!captured && site->industry_progress >= site->industry_cost * .1 &&
          !site->complete) {
        frame.clock().set_speed(StrategicSpeed::Paused);
        inspect_site(*site);
        capture("construction");
        captured = true;
        refresh_surface(true);
        site = std::ranges::find(surface_workspace_.view()->construction_sites,
                                 id, &NativeSurfaceSite::building_id);
        if (site == surface_workspace_.view()->construction_sites.end())
          throw std::runtime_error(
              "Earned surface lost its building after capture.");
        frame.clock().set_speed(StrategicSpeed::Normal);
      }
    }
    frame.clock().set_speed(StrategicSpeed::Paused);
    refresh_surface(true);
    site = std::ranges::find(surface_workspace_.view()->construction_sites, id,
                             &NativeSurfaceSite::building_id);
    if (!captured ||
        site == surface_workspace_.view()->construction_sites.end() ||
        !site->complete || !site->powered || !site->staffed || !site->enabled ||
        surface_workspace_.view()->industry_per_day <= industry_before)
      throw std::runtime_error(
          "Earned fabricator was not a staffed powered industry output.");
    inspect_site(*site);
    site = std::ranges::find(surface_workspace_.view()->construction_sites, id,
                            &NativeSurfaceSite::building_id);
    if (site == surface_workspace_.view()->construction_sites.end())
      throw std::runtime_error("Earned surface lost its inspected fabricator.");
    const PlayerCampaignCaptureOptions opts{frame.clock().simulation_days(),
                                            STELLAR_GAME_VERSION,
                                            "2044-05-06T07:08:21Z"};
    auto restored = restore_player_campaign_v17_json(
        load_adaptive_research_strategic_runtime(asset_root_ /
                                                 "Data/research/v1"),
        encode_player_campaign_v17_json(
            PreparedPlayerCampaignSave::capture(frame.runtime(), opts)
                .payload()));
    auto resumed = std::move(restored).activate();
    const bool roundtrip =
        nlohmann::json::parse(encode_player_campaign_v17_json(
            PreparedPlayerCampaignSave::capture(resumed, opts).payload())) ==
        saved();
    if (!roundtrip)
      throw std::runtime_error("Earned surface Player17 roundtrip failed.");
    earned_surface_proof_ = nlohmann::json{
        {"mode", "resume"},
        {"player_id", expected_player},
        {"system_id", expected_system},
        {"body_id", expected_body},
        {"colony_id", expected_colony},
        {"building_id", id},
        {"type_id", site->type_id},
        {"x", x},
        {"z", z},
        {"rotation", rotation},
        {"before_days", before_days},
        {"after_days", frame.clock().simulation_days()},
        {"steps", steps},
        {"step_days", step_days},
        {"authorization", option->authorization_budget_units},
        {"treasury_before", initial.treasury_budget_units},
        {"treasury_after", treasury_after},
        {"industry_cost", site->industry_cost},
        {"industry_progress", site->industry_progress},
        {"complete", site->complete},
        {"powered", site->powered},
        {"staffed", site->staffed},
        {"enabled", site->enabled},
        {"efficiency", site->efficiency},
        {"industry_before", industry_before},
        {"industry_after", surface_workspace_.view()->industry_per_day},
        {"cancel_unchanged", cancelled},
        {"opened_surface", true},
        {"roundtrip",
         roundtrip}}.dump();
    session_->request_save();
  }
  [[nodiscard]] const std::string &earned_surface_smoke_status() const {
    return earned_surface_proof_;
  }

  void prepare_earned_surface_expansion_smoke(
      int width, int height, Options::EarnedSurfaceMode mode,
      const std::function<void()> &pump,
      const std::function<void(std::string_view)> &capture) {
    auto &frame = session_->frame();
    const auto &world = frame.runtime().world().campaign();
    const bool paused = mode == Options::EarnedSurfaceMode::Paused;
    const auto snapshot = [&] {
      return nlohmann::json::parse(encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(
              frame.runtime(), {frame.clock().simulation_days(),
                                STELLAR_GAME_VERSION, "2044-05-06T07:08:21Z"})
              .payload()));
    };
    const auto original = snapshot();
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot input;
      input.drawable_width = width;
      input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error(
            "Earned surface expansion input closed the campaign.");
    };
    const auto click = [&](Point point) {
      route({{InputEventType::LeftPressed, point},
             {InputEventType::LeftReleased, point}});
    };
    if (frame.clock().speed() != StrategicSpeed::Paused ||
        world.player_civilization_id != 0)
      throw std::runtime_error(
          "Earned surface expansion requires paused player 0.");
    const auto colony = std::ranges::find(world.colonies, 9, &Colony::id);
    if (colony == world.colonies.end() || colony->system_id != 8 ||
        colony->planetary_body_id != 8004 ||
        colony->surface_buildings.size() != (paused ? 3u : 1u))
      throw std::runtime_error("Earned surface expansion requires Xanthe's "
                               "expected building state.");
    if (!enter_system(8, width, height))
      throw std::runtime_error(
          "Earned surface expansion could not enter Xanthe.");
    const auto spatial = project_system(*system_workspace_.snapshot());
    const auto marker = std::ranges::find(spatial.bodies, 8004,
                                          &SystemSpatialBodyMarker::body_id);
    if (marker == spatial.bodies.end())
      throw std::runtime_error("Earned surface expansion body is absent.");
    const auto body = system_workspace_.viewport()->world_to_screen(
        marker->offset_x, marker->offset_y);
    click({body.x, body.y});
    refresh_colony_entry(true);
    click(center(
        SystemWorkspaceLayout::for_viewport(width, height).colony_action));
    if (!colony_workspace_.visible() || !colony_workspace_.view() ||
        colony_workspace_.view()->colony_id != 9)
      throw std::runtime_error("Earned surface expansion colony entry failed.");
    click(center(
        ColonyWorkspaceLayout::for_viewport(width, height).open_surface));
    if (!surface_workspace_.visible() || !surface_workspace_.view())
      throw std::runtime_error(
          "Earned surface expansion did not open surface.");
    const auto wait_art = [&] {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(300);
      pump();
      while (!artwork_ready()) {
        if (std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error(
              "Earned surface expansion artwork timed out.");
        pump();
      }
    };
    wait_art();
    const auto initial = *surface_workspace_.view();
    const double before_days = frame.clock().simulation_days(),
                 power_before = initial.power_supply,
                 science_before = initial.science_per_day;
    const double research_labs_before = current_research_stamp().total_labs;
    const auto verify_lab = [&](int building_id) {
      const auto &state = frame.runtime().research().get_civilization(0);
      const auto institutions = state.expertise().institutions();
      const auto key = "construction:surface:9:" + std::to_string(building_id);
      const auto institution = std::ranges::find(
          institutions, key,
          &ResearchInstitutionRuntimeState::institution_instance_id);
      if (institution == institutions.end() ||
          institution->institution_archetype_id !=
              "surface_science_laboratory" ||
          institution->context_id != std::optional<std::string>{"colony:9"} ||
          institution->total_count != 1 || institution->active_count != 1)
        throw std::runtime_error("Earned science lab did not register its "
                                 "active research institution.");
      return *institution;
    };
    const auto verify_roundtrip = [&] {
      const PlayerCampaignCaptureOptions options{
          frame.clock().simulation_days(), STELLAR_GAME_VERSION,
          "2044-05-06T07:08:21Z"};
      auto restored = restore_player_campaign_v17_json(
          load_adaptive_research_strategic_runtime(asset_root_ /
                                                   "Data/research/v1"),
          encode_player_campaign_v17_json(
              PreparedPlayerCampaignSave::capture(frame.runtime(), options)
                  .payload()));
      auto resumed = std::move(restored).activate();
      if (nlohmann::json::parse(encode_player_campaign_v17_json(
              PreparedPlayerCampaignSave::capture(resumed, options)
                  .payload())) != snapshot())
        throw std::runtime_error(
            "Earned surface expansion Player17 roundtrip failed.");
      return true;
    };
    const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
    const auto select = [&](NativeSurfaceSite site) {
      if (surface_workspace_.selected_type_id())
        route({{InputEventType::EscapePressed}});
      click(surface_workspace_.viewport().world_to_screen(site.x, site.z,
                                                          layout.terrain));
      if (surface_workspace_.selected_building_id() !=
          std::optional<int>{site.building_id})
        throw std::runtime_error("Earned expansion could not select site.");
      wait_art();
    };
    const auto fabricator =
        std::ranges::find_if(initial.construction_sites, [](const auto &s) {
          return s.type_id == "fabricator" && s.complete && s.powered &&
                 s.staffed && s.enabled;
        });
    if (fabricator == initial.construction_sites.end())
      throw std::runtime_error(
          "Earned expansion lacks operational fabricator.");
    if (paused) {
      const auto generator =
          std::ranges::find_if(initial.construction_sites, [](const auto &s) {
            return s.type_id == "power_generator" && s.complete && s.powered &&
                   s.staffed && s.enabled;
          });
      const auto lab =
          std::ranges::find_if(initial.construction_sites, [](const auto &s) {
            return s.type_id == "science_lab" && s.complete && s.powered &&
                   s.staffed && s.enabled;
          });
      if (generator == initial.construction_sites.end() ||
          lab == initial.construction_sites.end() ||
          std::abs(initial.power_supply - 6.) > 1e-9 ||
          std::abs(initial.power_demand - 4.) > 1e-9 ||
          initial.science_per_day <= 0.)
        throw std::runtime_error(
            "Paused expansion lacks completed operational structures.");
      select(*lab);
      InputSnapshot tick;
      tick.drawable_width = width;
      tick.drawable_height = height;
      (void)update(tick, width, height, 1., true);
      if (snapshot() != original)
        throw std::runtime_error("Paused expansion changed Player17.");
      const auto institution = verify_lab(lab->building_id);
      const bool roundtrip = verify_roundtrip();
      if (surface_workspace_.scene_diagnostics().replaced_structures < 4)
        throw std::runtime_error(
            "Paused expansion did not render all prepared structures.");
      nlohmann::json persisted_stages = nlohmann::json::array();
      for (const auto &site : initial.construction_sites)
        persisted_stages.push_back(
            {{"type_id", site.type_id},
             {"building_id", site.building_id},
             {"x", site.x},
             {"z", site.z},
             {"rotation", site.rotation_degrees},
             {"authorization", 0},
             {"treasury_before", initial.treasury_budget_units},
             {"treasury_after", initial.treasury_budget_units},
             {"industry_cost", site.industry_cost},
             {"industry_progress", site.industry_progress},
             {"complete", site.complete},
             {"powered", site.powered},
             {"staffed", site.staffed},
             {"enabled", site.enabled},
             {"efficiency", site.efficiency},
             {"steps", 0},
             {"step_days", 1. / 64.},
             {"cancel_unchanged", true}});
      earned_surface_expansion_proof_ = nlohmann::json{
          {"mode", "paused"},
          {"player_id", 0},
          {"system_id", 8},
          {"body_id", 8004},
          {"colony_id", 9},
          {"before_days", before_days},
          {"after_days", before_days},
          {"power_supply_before", initial.power_supply},
          {"power_supply_after", initial.power_supply},
          {"science_before", science_before},
          {"science_after", initial.science_per_day},
          {"research_instance_id", institution.institution_instance_id},
          {"research_lab_active_count", institution.active_count},
          {"research_labs_before", research_labs_before},
          {"research_labs_after", current_research_stamp().total_labs},
          {"fabricator_operational", true},
          {"opened_surface", true},
          {"roundtrip", roundtrip},
          {"stages", persisted_stages}}.dump();
      session_->request_save();
      return;
    }
    nlohmann::json stages = nlohmann::json::array();
    const auto build = [&](std::string_view type, double authorization,
                           double cost, std::string_view prefix) {
      const auto view = *surface_workspace_.view();
      const auto stage_original = snapshot();
      const auto option =
          std::ranges::find(view.available_buildings, std::string(type),
                            &NativeSurfaceBuildOption::type_id);
      if (option == view.available_buildings.end() ||
          option->authorization_budget_units != authorization ||
          option->industry_cost != cost)
        throw std::runtime_error("Earned expansion catalog mismatch.");
      const auto index =
          static_cast<std::size_t>(option - view.available_buildings.begin());
      click({layout.palette_rows.x + 14.f * layout.scale,
             layout.palette_rows.y +
                 (static_cast<float>(index) * 78.f + 20.f) * layout.scale});
      std::optional<NativeSurfacePlacementQuote> quote;
      for (float z = -70; z <= 70 && !quote; z += 35)
        for (float x = -70; x <= 70; x += 7) {
          auto q = surface_controller_.preview_placement(
              frame, session_->cache().generation, *surface_workspace_.view(),
              std::string(type), x, z, 0);
          if (q.accepted) {
            quote = q;
            break;
          }
        }
      if (!quote)
        throw std::runtime_error("Earned expansion found no position.");
      (void)surface_controller_.cancel_quote(session_->cache().generation,
                                             quote->quote_revision);
      const auto point = surface_workspace_.viewport().world_to_screen(
          quote->x, quote->z, layout.terrain);
      click(point);
      if (!surface_workspace_.placement_quote() ||
          !surface_workspace_.placement_quote()->accepted ||
          surface_workspace_.placement_quote()->authorization_budget_units !=
              authorization)
        throw std::runtime_error("Earned expansion review failed.");
      capture(std::string(prefix) + "-review");
      click(center(layout.cancel));
      refresh_surface(true);
      const bool cancelled = surface_workspace_.view()->treasury_budget_units ==
                                 view.treasury_budget_units &&
                             snapshot() == stage_original;
      if (!cancelled)
        throw std::runtime_error("Earned expansion cancel changed Player17.");
      click(point);
      click(center(layout.confirm));
      auto site = std::ranges::find_if(
          surface_workspace_.view()->construction_sites,
          [&](const auto &s) { return s.type_id == type && !s.complete; });
      if (site == surface_workspace_.view()->construction_sites.end() ||
          site->industry_progress != 0. ||
          std::abs(surface_workspace_.view()->treasury_budget_units -
                   (view.treasury_budget_units - authorization)) > 1e-9)
        throw std::runtime_error("Earned expansion did not create exact site.");
      const int id = site->building_id;
      const double treasury_after_authorization =
          surface_workspace_.view()->treasury_budget_units;
      std::uint64_t steps{};
      bool partial{};
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(300);
      frame.clock().set_speed(StrategicSpeed::Normal);
      while (!site->complete) {
        if (++steps > 1024 * 64 || std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error(
              "Earned expansion exceeded its day or wall-clock bound.");
        const auto before = frame.clock().simulation_days();
        const auto result = frame.advance(1. / 64.);
        if (result.completed_substeps != std::vector<double>{1. / 64.} ||
            frame.clock().simulation_days() - before != 1. / 64. ||
            frame.clock().backlog_days() != 0.)
          throw std::runtime_error("Earned expansion lost exact stepping.");
        publish_feedback(result);
        if (steps % 64 == 0)
          pump();
        refresh_surface(true);
        site = std::ranges::find(surface_workspace_.view()->construction_sites,
                                 id, &NativeSurfaceSite::building_id);
        if (site == surface_workspace_.view()->construction_sites.end())
          throw std::runtime_error("Earned expansion lost site.");
        if (!partial && site->progress_fraction >= .10 && !site->complete) {
          frame.clock().set_speed(StrategicSpeed::Paused);
          select(*site);
          capture(std::string(prefix) + "-construction");
          partial = true;
          refresh_surface(true);
          site =
              std::ranges::find(surface_workspace_.view()->construction_sites,
                                id, &NativeSurfaceSite::building_id);
          frame.clock().set_speed(StrategicSpeed::Normal);
        }
      }
      frame.clock().set_speed(StrategicSpeed::Paused);
      refresh_surface(true);
      site = std::ranges::find(surface_workspace_.view()->construction_sites,
                               id, &NativeSurfaceSite::building_id);
      if (!partial ||
          site == surface_workspace_.view()->construction_sites.end() ||
          !site->complete || !site->powered || !site->staffed || !site->enabled)
        throw std::runtime_error("Earned expansion site did not operate.");
      const auto completed_site = *site;
      select(completed_site);
      capture(std::string(prefix) + "-complete");
      site = std::ranges::find(surface_workspace_.view()->construction_sites,
                               id, &NativeSurfaceSite::building_id);
      if (site == surface_workspace_.view()->construction_sites.end())
        throw std::runtime_error(
            "Earned expansion lost the inspected completed site.");
      stages.push_back({{"type_id", site->type_id},
                        {"building_id", id},
                        {"x", site->x},
                        {"z", site->z},
                        {"rotation", site->rotation_degrees},
                        {"authorization", authorization},
                        {"treasury_before", view.treasury_budget_units},
                        {"treasury_after", treasury_after_authorization},
                        {"industry_cost", site->industry_cost},
                        {"industry_progress", site->industry_progress},
                        {"complete", site->complete},
                        {"powered", site->powered},
                        {"staffed", site->staffed},
                        {"enabled", site->enabled},
                        {"efficiency", site->efficiency},
                        {"steps", steps},
                        {"step_days", 1. / 64.},
                        {"cancel_unchanged", cancelled}});
    };
    build("power_generator", 25., 300., "power");
    const double power_after = surface_workspace_.view()->power_supply;
    if (std::abs(power_after - power_before - 4.) > 1e-9)
      throw std::runtime_error("Generator did not raise power supply by four.");
    build("science_lab", 40., 400., "science");
    const auto final = *surface_workspace_.view();
    if (std::abs(final.science_per_day - science_before - 1.) > 1e-9)
      throw std::runtime_error(
          "Science lab did not update the surface science projection.");
    const double research_labs_after = current_research_stamp().total_labs;
    if (std::abs(research_labs_after - research_labs_before - 1.) > 1e-9)
      throw std::runtime_error(
          "Science lab did not add one effective research lab.");
    const auto institution =
        verify_lab(stages.at(1).at("building_id").get<int>());
    const bool roundtrip = verify_roundtrip();
    const auto final_fabricator =
        std::ranges::find(final.construction_sites, fabricator->building_id,
                          &NativeSurfaceSite::building_id);
    if (final_fabricator == final.construction_sites.end() ||
        !final_fabricator->complete || !final_fabricator->enabled ||
        !final_fabricator->powered || !final_fabricator->staffed ||
        final.industry_per_day < initial.industry_per_day ||
        surface_workspace_.scene_diagnostics().replaced_structures < 4)
      throw std::runtime_error(
          "Expansion lost operating industry or prepared structure artwork.");
    earned_surface_expansion_proof_ = nlohmann::json{
        {"mode", "expansion"},
        {"player_id", 0},
        {"system_id", 8},
        {"body_id", 8004},
        {"colony_id", 9},
        {"before_days", before_days},
        {"after_days", frame.clock().simulation_days()},
        {"power_supply_before", power_before},
        {"power_supply_after", final.power_supply},
        {"science_before", science_before},
        {"science_after", final.science_per_day},
        {"research_instance_id", institution.institution_instance_id},
        {"research_lab_active_count", institution.active_count},
        {"research_labs_before", research_labs_before},
        {"research_labs_after", research_labs_after},
        {"fabricator_operational", true},
        {"opened_surface", true},
        {"roundtrip", roundtrip},
        {"stages", stages}}.dump();
    session_->request_save();
  }
  [[nodiscard]] const std::string &
  earned_surface_expansion_smoke_status() const {
    return earned_surface_expansion_proof_;
  }

  void prepare_settlement_preparation_smoke(
      int width, int height, const std::function<void()> &pump,
      const std::function<void(std::string_view)> &capture) {
    auto &frame = session_->frame();
    const auto &world = frame.runtime().world().campaign();
    if (frame.clock().speed() != StrategicSpeed::Paused)
      throw std::runtime_error(
          "Settlement preparation requires a paused earned campaign.");
    const auto initial =
        stellar::native_settlement_preparation::build_settlement_preparation(
            frame, session_->cache().generation, 1, 1001);
    if (!initial || initial->site_can_found_current_colony ||
        !initial->solid_surface || initial->rare_resource)
      throw std::runtime_error("Settlement preparation requires the actual "
                               "surveyed, unsuitable Ilyra.");
    const PlayerCampaignCaptureOptions fixed_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:19Z"};
    const auto payload = [&] {
      return nlohmann::json::parse(encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(frame.runtime(), fixed_capture)
              .payload()));
    };
    const auto before = payload();
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot input;
      input.drawable_width = width;
      input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error(
            "Settlement preparation UI closed unexpectedly.");
    };
    const auto click = [&](Point at) {
      route({{InputEventType::LeftPressed, at},
             {InputEventType::LeftReleased, at}});
    };
    const auto stable = [&] {
      if (payload() != before)
        throw std::runtime_error(
            "Settlement preparation changed the Player17 campaign.");
    };
    const auto wait_art = [&] {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(30);
      do {
        pump();
        if (std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error("Settlement artwork did not become ready.");
      } while (!artwork_ready());
    };
    if (!enter_system(1, width, height))
      throw std::runtime_error("Surveyed system did not open.");
    const auto spatial = project_system(*system_workspace_.snapshot());
    const auto marker = std::ranges::find(spatial.bodies, 1001,
                                          &SystemSpatialBodyMarker::body_id);
    if (marker == spatial.bodies.end())
      throw std::runtime_error("Ilyra absent from the observed system.");
    const auto point = system_workspace_.viewport()->world_to_screen(
        marker->offset_x, marker->offset_y);
    const auto layout = SystemWorkspaceLayout::for_viewport(width, height);
    if (!layout.world_field.contains({point.x, point.y}) ||
        system_workspace_.viewport()->hit_body(spatial, point.x, point.y) !=
            1001)
      throw std::runtime_error("Ilyra is not independently clickable.");
    click({point.x,point.y});
    if(system_workspace_.selected_body_id()!=1001||!system_workspace_.settlement_preparation())
      throw std::runtime_error("Planet click did not publish settlement preparation.");
    wait_art();stable();capture("assessment");
    bool colony_cost{},outpost_cost{},guidance{},shipyard_button{};
    const auto observe=[&]{const auto draw=scene(width,height);for(const auto &item:draw.overlay)
      if(const auto *label=std::get_if<Text>(&item)){
        colony_cost=colony_cost||label->value==initial->colony_ship.formatted_ship_cost;
        outpost_cost=outpost_cost||label->value==initial->resource_outpost.formatted_ship_cost;
        guidance=guidance||label->value=="Explore other worlds";
        shipyard_button=shipyard_button||label->value=="VIEW SHIPYARD";
      }};
    observe();
    int cost_capture_at=-1;bool cost_capture{};
    for(int n=0;n<100;++n){
      route({{InputEventType::Wheel,center(layout.inspector),{},-1.f}});observe();
      if(outpost_cost&&cost_capture_at<0)cost_capture_at=n+4;
      if(n==cost_capture_at){capture("costs");cost_capture=true;}
    }
    if(!colony_cost||!outpost_cost||!guidance||!shipyard_button)
      throw std::runtime_error("Scrolling did not expose both vessel costs and truthful settlement guidance.");
    if(!cost_capture)throw std::runtime_error("Settlement cost capture was not reached.");
    stable();
    click(center(layout.colony_action));
    if(!shipyard_workspace_.visible()||system_workspace_.visible()||!shipyard_workspace_.view())
      throw std::runtime_error("Settlement footer did not open the ordinary shipyard.");
    const auto shipyard=ShipyardWorkspaceLayout::for_viewport(width,height);
    for(const auto *option:{&initial->colony_ship,&initial->resource_outpost}){
      for(int n=0;n<100&&!shipyard_workspace_.design_bounds(option->design_id,width,height);++n)
        route({{InputEventType::Wheel,center(shipyard.designs),{},-1.f}});
      const auto bounds=shipyard_workspace_.design_bounds(option->design_id,width,height);
      if(!bounds)throw std::runtime_error("Earned settlement vessel design missing from shipyard.");
      click(center(*bounds));
      const auto &designs=shipyard_workspace_.view()->available_designs;
      const auto design=std::ranges::find(designs,option->design_id,&NativeShipDesign::id);
      if(shipyard_workspace_.selected_design_id()!=option->design_id||design==designs.end()||
         design->formatted_credit_cost!=option->formatted_ship_cost||design->population_cost_millions!=option->population_reservation_millions)
        throw std::runtime_error("Shipyard disagrees with preparation cost or population reservation.");
      stable();
    }
    wait_art();capture("shipyard");
    click(center(shipyard.close));
    if(shipyard_workspace_.visible())throw std::runtime_error("Shipyard Back did not dismiss review.");
    stable();
    preparation_evidence_=nlohmann::json{{"system_id",1},{"body_id",1001},{"player_id",world.player_civilization_id},
      {"read_only",true},{"unsuitable_site",true},{"colony_cost_visible",colony_cost},{"outpost_cost_visible",outpost_cost},
      {"shipyard_opened",true},{"review_closed",true},{"days",frame.clock().simulation_days()}}.dump();
    // Return to the planet for the final capture, through the same UI hit path.
    if(!enter_system(1,width,height))throw std::runtime_error("Surveyed system disappeared after review.");
    const auto restored_point=system_workspace_.viewport()->world_to_screen(marker->offset_x,marker->offset_y);
    click({restored_point.x,restored_point.y});wait_art();stable();
    InputSnapshot ready;ready.drawable_width=width;ready.drawable_height=height;
    if(!update(ready,width,height,0.,true))throw std::runtime_error("Settlement review save boundary failed.");
    session_->request_save();
  }
  [[nodiscard]] const std::string &settlement_preparation_smoke_status()const{return preparation_evidence_;}

  void prepare_first_survey_smoke(
      int width, int height, Options::FirstSurveyMode mode,
      const std::function<void()> &pump,
      const std::function<void(std::string_view)> &capture) {
    constexpr double step_days = 1. / 64.;
    constexpr std::uint64_t maximum_steps = 256u * 64u;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(300);
    auto &frame = session_->frame();
    auto &world = frame.runtime().world().campaign();
    first_survey_mode_ = mode;
    first_survey_seen_phases_.clear();
    first_survey_steps_ = 0;
    first_survey_input_orders_ = 0;
    first_survey_body_id_ = -1;
    first_survey_preview_read_only_ = false;
    first_survey_inspection_read_only_ = false;
    first_survey_facts_visible_ = false;
    first_survey_player_id_ = world.player_civilization_id;
    const auto player = std::ranges::find(
        world.civilizations, first_survey_player_id_, &Civilization::id);
    if (player == world.civilizations.end())
      throw std::runtime_error(
          "First survey could not identify the player civilization.");
    first_survey_origin_id_ = player->home_system_id;

    std::vector<const FleetState *> owned;
    for (const auto &fleet : world.fleets)
      if (fleet.civilization_id == first_survey_player_id_)
        owned.push_back(&fleet);
    if (owned.size() != 2 ||
        std::ranges::count_if(
            owned, [](const FleetState *fleet) { return fleet->is_active; }) !=
            2 ||
        std::ranges::count_if(owned,
                              [](const FleetState *fleet) {
                                return fleet->design_id ==
                                       std::optional<std::string>{"warp_scout"};
                              }) != 1 ||
        std::ranges::count_if(owned, [](const FleetState *fleet) {
          return fleet->design_id ==
                 std::optional<std::string>{"science_vessel"};
        }) != 1)
      throw std::runtime_error(
          "First survey requires exactly the two active earned first ships.");
    const auto science_source =
        std::ranges::find_if(owned, [](const FleetState *fleet) {
          return fleet->design_id ==
                     std::optional<std::string>{"science_vessel"} &&
                 fleet->role == FleetRole::Science;
        });
    const auto scout_source =
        std::ranges::find_if(owned, [](const FleetState *fleet) {
          return fleet->design_id == std::optional<std::string>{"warp_scout"} &&
                 fleet->role == FleetRole::Scout;
        });
    if (science_source == owned.end() || scout_source == owned.end())
      throw std::runtime_error("First survey could not identify the earned "
                               "science vessel and scout.");
    first_survey_fleet_id_ = (*science_source)->id;
    first_survey_scout_id_ = (*scout_source)->id;
    const auto live_science = [&]() -> FleetState & {
      const auto found = std::ranges::find(world.fleets, first_survey_fleet_id_,
                                           &FleetState::id);
      if (found == world.fleets.end() || !found->is_active ||
          found->civilization_id != first_survey_player_id_ ||
          found->role != FleetRole::Science ||
          found->design_id != std::optional<std::string>{"science_vessel"})
        throw std::runtime_error(
            "First survey lost the selected earned science vessel.");
      return *found;
    };
    const auto live_scout = [&]() -> const FleetState & {
      const auto found = std::ranges::find(world.fleets, first_survey_scout_id_,
                                           &FleetState::id);
      if (found == world.fleets.end() || !found->is_active ||
          found->civilization_id != first_survey_player_id_ ||
          found->role != FleetRole::Scout ||
          found->design_id != std::optional<std::string>{"warp_scout"})
        throw std::runtime_error("First survey lost the earned scout.");
      return *found;
    };
    auto &initial_science = live_science();
    const auto &initial_scout = live_scout();
    first_survey_target_id_ = initial_scout.current_system_id.value_or(-1);
    if (first_survey_target_id_ < 0 ||
        first_survey_target_id_ == first_survey_origin_id_ ||
        initial_scout.destination_system_id ||
        initial_scout.transit_phase != FleetTransitPhase::None ||
        initial_scout.reconnaissance_system_id != first_survey_target_id_ ||
        initial_scout.reconnaissance_days_completed + 1e-9 <
            ExplorationSimulation::scout_reconnaissance_days)
      throw std::runtime_error(
          "First survey requires the naturally completed scout at its target.");
    if (mode == Options::FirstSurveyMode::Depart &&
        (initial_science.current_system_id != first_survey_origin_id_ ||
         initial_science.destination_system_id ||
         initial_science.transit_phase != FleetTransitPhase::None))
      throw std::runtime_error(
          "First survey departure requires the idle science vessel at home.");
    if (mode != Options::FirstSurveyMode::Depart &&
        (initial_science.current_system_id != first_survey_target_id_ ||
         initial_science.destination_system_id ||
         initial_science.transit_phase != FleetTransitPhase::None))
      throw std::runtime_error("First survey continuation requires the science "
                               "vessel at the scout target.");

    first_survey_lane_connected_ = std::ranges::any_of(
        frame.runtime().world().lanes().build(),
        [&](const InterstellarLane &lane) {
          return lane.connects(first_survey_origin_id_) &&
                 lane.other(first_survey_origin_id_) == first_survey_target_id_;
        });
    if (!first_survey_lane_connected_)
      throw std::runtime_error(
          "First survey target is not the direct home-system neighbor.");
    const auto operations = SurveyOperationsProfiler{}.build(
        world.systems, world.bodies, first_survey_target_id_);
    if (operations.system_id != first_survey_target_id_ ||
        operations.estimated_science_survey_days <
            SurveyOperationsProfiler::minimum_survey_days ||
        operations.estimated_science_survey_days >
            SurveyOperationsProfiler::maximum_survey_days ||
        operations.progress_per_day() <= 0.)
      throw std::runtime_error(
          "First survey target lacks a canonical operations profile.");

    const PlayerCampaignCaptureOptions before_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:15Z"};
    const auto captured = [&](const PlayerCampaignCaptureOptions &options) {
      return encode_player_campaign_v17_json(
          PreparedPlayerCampaignSave::capture(frame.runtime(), options)
              .payload());
    };
    const auto fleet_object = [&](std::string_view payload, int fleet_id) {
      const auto parsed = nlohmann::json::parse(payload);
      const auto &fleets = parsed.at("Galaxy").at("Fleets");
      const auto found =
          std::ranges::find_if(fleets, [&](const nlohmann::json &fleet) {
            return fleet.at("Id").get<int>() == fleet_id;
          });
      if (found == fleets.end())
        throw std::runtime_error(
            "First survey could not capture its invariant scout.");
      return *found;
    };
    const auto before_selection = captured(before_capture);
    const auto scout_before =
        fleet_object(before_selection, first_survey_scout_id_);
    const auto route = [&](std::vector<InputEvent> events) {
      InputSnapshot input;
      input.drawable_width = width;
      input.drawable_height = height;
      input.pointer = events.empty() ? Point{} : events.back().position;
      input.events = std::move(events);
      if (!update(input, width, height, 0., false))
        throw std::runtime_error("First survey UI input closed the campaign.");
    };
    const auto click = [&](Point point, InputEventType pressed =
                                            InputEventType::LeftPressed) {
      route({{pressed, point},
             {pressed == InputEventType::LeftPressed
                  ? InputEventType::LeftReleased
                  : InputEventType::RightReleased,
              point}});
    };
    refresh_fleets(true);
    if (!fleet_workspace_.view())
      throw std::runtime_error(
          "First survey did not receive its owned fleet view.");
    const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
    const auto science_row =
        std::ranges::find(fleet_workspace_.view()->own_fleets,
                          first_survey_fleet_id_, &NativeOwnFleet::id);
    if (science_row == fleet_workspace_.view()->own_fleets.end())
      throw std::runtime_error(
          "First survey science vessel was absent from the outliner.");
    const auto science_index = static_cast<std::size_t>(
        science_row - fleet_workspace_.view()->own_fleets.begin());
    const auto select_science = [&] {
      click({layout.list.x + 12.f * layout.scale,
             layout.list.y + (static_cast<float>(science_index) * 45.f + 20.f) *
                                 layout.scale});
    };
    select_science();
    first_survey_selected_ =
        fleet_controller_.selection() == first_survey_fleet_id_;
    first_survey_selection_read_only_ =
        captured(before_capture) == before_selection;
    if (!first_survey_selected_ || !first_survey_selection_read_only_)
      throw std::runtime_error(
          "First survey outliner selection was rejected or changed Player17.");

    click(center(layout.civilian_locate));
    if (!last_fleet_command_accepted_)
      throw std::runtime_error("First survey Locate input was rejected.");
    Point zoom_anchor{static_cast<float>(width) * .5f,
                      static_cast<float>(height) * .5f};
    const auto before_zoom = camera_.pixels_per_world;
    for (int index = 0; index < 96 && camera_.pixels_per_world < 70.; ++index)
      route({{InputEventType::Wheel, zoom_anchor, {}, 1.f}});
    if (camera_.pixels_per_world <= before_zoom ||
        camera_.pixels_per_world < 70. || system_workspace_.visible())
      throw std::runtime_error(
          "First survey map zoom remained captured after Locate.");
    const Point drag_start{static_cast<float>(width) * .34f,
                           static_cast<float>(height) * .72f};
    const Point drag_delta{40.f, -24.f};
    const Point drag_end{drag_start.x + drag_delta.x,
                         drag_start.y + drag_delta.y};
    route({{InputEventType::LeftPressed, drag_start},
           {InputEventType::PointerMove, drag_end, drag_delta},
           {InputEventType::LeftReleased, drag_end}});

    first_survey_before_days_ = frame.clock().simulation_days();
    first_survey_revision_before_ = initial_science.mission_order_revision;
    first_survey_phase_before_ = initial_science.transit_phase;
    first_survey_survey_before_ =
        static_cast<int>(world.knowledge.system_survey_level(
            first_survey_player_id_, first_survey_target_id_));
    first_survey_survey_progress_before_ =
        world.knowledge.system_survey_progress(first_survey_player_id_,
                                               first_survey_target_id_);
    first_survey_transit_progress_before_ = initial_science.transit_progress;
    if (first_survey_survey_before_ <
            static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
        first_survey_survey_progress_before_ + 1e-9 <
            ExplorationSimulation::scout_reconnaissance_progress)
      throw std::runtime_error(
          "First survey source lacks the natural scout reconnaissance.");
    if (mode == Options::FirstSurveyMode::Depart &&
        (first_survey_survey_before_ !=
             static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
         std::abs(first_survey_survey_progress_before_ -
                  ExplorationSimulation::scout_reconnaissance_progress) > 1e-9))
      throw std::runtime_error("First survey departure requires the untouched "
                               "partial scout survey.");
    if (mode == Options::FirstSurveyMode::Resume &&
        (first_survey_survey_before_ !=
             static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
         first_survey_survey_progress_before_ <=
             ExplorationSimulation::scout_reconnaissance_progress ||
         first_survey_survey_progress_before_ >= 1.))
      throw std::runtime_error(
          "First survey resume requires the saved ongoing science survey.");

    const auto observe_phase = [&] {
      const auto value = static_cast<int>(live_science().transit_phase);
      if (std::ranges::find(first_survey_seen_phases_, value) ==
          first_survey_seen_phases_.end())
        first_survey_seen_phases_.push_back(value);
    };
    observe_phase();
    const auto ui = NativeUiLayout::for_viewport(width, height);
    const auto pause = [&] {
      if (frame.clock().speed() != StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Paused)
        throw std::runtime_error("First survey pause input was rejected.");
    };
    const auto resume_normal = [&] {
      if (frame.clock().speed() == StrategicSpeed::Paused)
        click(center(ui.pause));
      if (frame.clock().speed() != StrategicSpeed::Normal)
        route({{InputEventType::KeyPressed, {}, {}, 0.f, {}, 0, '1'}});
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().effective_multiplier() != 1.)
        throw std::runtime_error(
            "First survey requires the ordinary Normal clock.");
    };
    const auto capture_phase = [&](std::string_view tag) {
      pause();
      refresh_fleets(true);
      refresh_system_travel(true);
      for (int frame_index = 0; frame_index < 4; ++frame_index)
        pump();
      capture(tag);
      resume_normal();
    };
    const auto step = [&] {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error(
            "First survey exceeded its 300-second wall bound.");
      if (first_survey_steps_ >= maximum_steps)
        throw std::runtime_error(
            "First survey exceeded its 256-day step bound.");
      if (frame.clock().speed() != StrategicSpeed::Normal ||
          frame.clock().backlog_days() != 0.)
        throw std::runtime_error(
            "First survey left Normal speed or accumulated backlog.");
      const auto before = frame.clock().simulation_days();
      const auto result = frame.advance(step_days);
      const auto after = frame.clock().simulation_days();
      if (result.route != CampaignFrameRoute::Strategic ||
          result.completed_substeps.size() != 1 ||
          result.completed_substeps.front() != step_days ||
          after - before != step_days || frame.clock().backlog_days() != 0.)
        throw std::runtime_error(
            "First survey left exact single-frame 1/64-day stepping.");
      publish_feedback(result);
      ++first_survey_steps_;
      observe_phase();
      if (first_survey_steps_ % 64 == 0) {
        refresh_fleets(true);
        refresh_system_travel(true);
        pump();
      }
    };

    const auto target = std::ranges::find(
        world.systems, first_survey_target_id_, &StellarSystem::id);
    if (target == world.systems.end())
      throw std::runtime_error("First survey target disappeared.");
    const auto target_point = [&] {
      return camera_.project({target->position.x, target->position.y}, width,
                             height);
    };
    if (mode == Options::FirstSurveyMode::Depart) {
      const auto preview_before = captured(before_capture);
      const auto point = target_point();
      if (layout.panel.contains(point) || point.x < 0.f || point.y < 0.f ||
          point.x >= width || point.y >= height)
        throw std::runtime_error(
            "First survey camera input did not expose the destination.");
      click(point, InputEventType::RightPressed);
      const auto &preview = fleet_workspace_.preview();
      first_survey_preview_read_only_ =
          captured(before_capture) == preview_before;
      if (!preview || preview->fleet_id != first_survey_fleet_id_ ||
          preview->target_system_id != first_survey_target_id_ ||
          preview->expected_mission_order_revision !=
              first_survey_revision_before_ ||
          !preview->command_available || !preview->route_supported ||
          !preview->route_authoritative ||
          preview->route_system_ids !=
              std::vector<int>{first_survey_origin_id_,
                               first_survey_target_id_} ||
          !first_survey_preview_read_only_)
        throw std::runtime_error(
            "First survey right-click preview lost identity or authority.");
      click(center(layout.confirm));
      ++first_survey_input_orders_;
      if (!last_fleet_command_accepted_ ||
          live_science().mission_order_revision !=
              first_survey_revision_before_ + 1 ||
          live_science().destination_system_id != first_survey_target_id_ ||
          live_science().transit_phase != FleetTransitPhase::None)
        throw std::runtime_error(
            "First survey confirmation did not create only the pending route.");
      resume_normal();
      while (live_science().transit_phase == FleetTransitPhase::None)
        step();
      if (live_science().transit_phase != FleetTransitPhase::LocalDeparture)
        throw std::runtime_error(
            "First survey did not observe timed local departure.");
      capture_phase("departure");
      while (live_science().transit_phase != FleetTransitPhase::None ||
             live_science().destination_system_id ||
             live_science().current_system_id != first_survey_target_id_)
        step();
      const auto arrival_progress = world.knowledge.system_survey_progress(
          first_survey_player_id_, first_survey_target_id_);
      step();
      const auto ongoing_progress = world.knowledge.system_survey_progress(
          first_survey_player_id_, first_survey_target_id_);
      if (ongoing_progress <= arrival_progress ||
          ongoing_progress <=
              ExplorationSimulation::scout_reconnaissance_progress ||
          ongoing_progress >= 1. ||
          world.knowledge.system_survey_level(first_survey_player_id_,
                                              first_survey_target_id_) !=
              SystemSurveyLevel::partially_surveyed)
        throw std::runtime_error(
            "First survey did not stop during the natural local survey.");
    } else if (mode == Options::FirstSurveyMode::Resume) {
      using stellar::native_campaign_feedback::FeedbackKind;
      const auto feedback_before =
          feedback_.counts().count(FeedbackKind::SurveyComplete);
      resume_normal();
      while (world.knowledge.system_survey_level(first_survey_player_id_,
                                                 first_survey_target_id_) !=
                 SystemSurveyLevel::fully_surveyed ||
             world.knowledge.system_survey_progress(
                 first_survey_player_id_, first_survey_target_id_) < 1.)
        step();
      const auto feedback_after =
          feedback_.counts().count(FeedbackKind::SurveyComplete);
      const auto recent = feedback_.recent();
      if (feedback_after != feedback_before + 1 ||
          std::ranges::find(recent, FeedbackKind::SurveyComplete,
                            &stellar::native_campaign_feedback::
                                CampaignFeedbackNotice::kind) == recent.end())
        throw std::runtime_error(
            "First survey completion did not reach normal campaign feedback.");
    }

    pause();
    const PlayerCampaignCaptureOptions inspection_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:15Z"};
    const auto before_inspection = captured(inspection_capture);
    refresh_fleets(true);
    select_science();
    click(center(layout.civilian_locate));
    const auto point = target_point();
    route({{InputEventType::LeftPressed, point, {}, 0.f, {}, 2},
           {InputEventType::LeftReleased, point}});
    if (!system_workspace_.visible() ||
        system_workspace_.system_id() != first_survey_target_id_ ||
        !system_workspace_.snapshot() || !system_workspace_.viewport())
      throw std::runtime_error(
          "First survey could not enter the target through UI input.");
    const auto spatial = project_system(*system_workspace_.snapshot());
    const auto system_layout =
        SystemWorkspaceLayout::for_viewport(width, height);
    std::vector<int> planet_ids;
    for (const auto &body : system_workspace_.snapshot()->bodies)
      if (body.kind == PlanetaryBodyKind::Planet)
        planet_ids.push_back(body.id);
    std::ranges::sort(planet_ids);
    if (planet_ids.empty())
      throw std::runtime_error(
          "First survey target has no real non-moon planet.");
    first_survey_body_id_ = planet_ids.front();
    const auto marker = std::ranges::find(spatial.bodies, first_survey_body_id_,
                                          &SystemSpatialBodyMarker::body_id);
    if (marker == spatial.bodies.end())
      throw std::runtime_error(
          "First survey target planet is absent from the spatial snapshot.");
    const auto screen = system_workspace_.viewport()->world_to_screen(
        marker->offset_x, marker->offset_y);
    const Point body_point{screen.x, screen.y};
    if (!system_layout.world_field.contains(body_point) ||
        system_workspace_.viewport()->hit_body(spatial, screen.x, screen.y) !=
            first_survey_body_id_)
      throw std::runtime_error(
          "First survey target planet is not independently hittable.");
    click(body_point);
    if (system_workspace_.selected_body_id() != first_survey_body_id_)
      throw std::runtime_error(
          "First survey could not select the target planet through UI input.");
    const auto inspection = build_body_inspection(*system_workspace_.snapshot(),
                                                  first_survey_body_id_);
    if (!inspection)
      throw std::runtime_error(
          "First survey selected planet has no observer-safe inspection.");
    const auto physical = std::ranges::find(
        inspection->sections, std::string{"Physical"}, &BodySection::heading);
    const auto environment =
        std::ranges::find(inspection->sections, std::string{"Environment"},
                          &BodySection::heading);
    const bool fully_surveyed = system_workspace_.snapshot()->survey_level ==
                                SystemSurveyLevel::fully_surveyed;
    first_survey_facts_visible_ =
        inspection->confirmed && physical != inspection->sections.end() &&
        environment != inspection->sections.end() && !physical->facts.empty() &&
        !environment->facts.empty() &&
        std::ranges::none_of(
            physical->facts,
            [](const BodyFact &fact) { return fact.value == "Unconfirmed"; }) &&
        std::ranges::none_of(environment->facts, [](const BodyFact &fact) {
          return fact.value == "Unconfirmed";
        });
    const bool partial_facts_private =
        !inspection->confirmed && physical != inspection->sections.end() &&
        environment != inspection->sections.end() &&
        std::ranges::all_of(physical->facts,
                            [](const BodyFact &fact) {
                              // Reconnaissance identifies the broad body type;
                              // measured physical values remain hidden until
                              // the science survey completes.
                              return fact.label == "Type" ||
                                     fact.value == "Unconfirmed";
                            }) &&
        std::ranges::all_of(environment->facts, [](const BodyFact &fact) {
          return fact.value == "Unconfirmed";
        });
    if (first_survey_facts_visible_ != fully_surveyed)
      throw std::runtime_error(
          "First survey body facts did not match canonical survey knowledge.");
    if (!fully_surveyed && !partial_facts_private)
      throw std::runtime_error("First survey body inspection leaked "
                               "measurements before full survey.");
    route({{InputEventType::Wheel, center(system_layout.inspector), {}, -1.f},
           {InputEventType::Wheel, center(system_layout.inspector), {}, -1.f}});
    for (int frame_index = 0; frame_index < 4; ++frame_index)
      pump();
    first_survey_inspection_read_only_ =
        captured(inspection_capture) == before_inspection;
    if (!first_survey_inspection_read_only_)
      throw std::runtime_error(
          "First survey body inspection changed the full Player17 payload.");
    if (mode == Options::FirstSurveyMode::Resume) {
      for (int input_count = 0;
           system_workspace_.inspection_scroll() > 0.f && input_count < 128;
           ++input_count)
        route({{InputEventType::Wheel,
                center(system_layout.inspector),
                {},
                1.f}});
      if (system_workspace_.inspection_scroll() != 0.f)
        throw std::runtime_error("First survey could not return the completed "
                                 "facts to the Physical heading.");
      std::uint32_t artwork_frames{};
      while (!artwork_ready()) {
        if (std::chrono::steady_clock::now() > deadline ||
            ++artwork_frames > 600)
          throw std::runtime_error("First survey system imagery did not become "
                                   "ready before capture.");
        pump();
      }
      first_survey_inspection_read_only_ =
          first_survey_inspection_read_only_ &&
          captured(inspection_capture) == before_inspection;
      if (!first_survey_inspection_read_only_)
        throw std::runtime_error(
            "First survey inspection reset changed the full Player17 payload.");
      capture("inspection");
    }
    if (mode != Options::FirstSurveyMode::Paused) {
      click(center(system_layout.back));
      refresh_fleets(true);
      select_science();
      pause();
    }

    first_survey_after_days_ = frame.clock().simulation_days();
    first_survey_revision_after_ = live_science().mission_order_revision;
    first_survey_phase_after_ = live_science().transit_phase;
    first_survey_survey_after_ =
        static_cast<int>(world.knowledge.system_survey_level(
            first_survey_player_id_, first_survey_target_id_));
    first_survey_survey_progress_after_ =
        world.knowledge.system_survey_progress(first_survey_player_id_,
                                               first_survey_target_id_);
    first_survey_transit_progress_after_ = live_science().transit_progress;
    if (first_survey_after_days_ !=
            first_survey_before_days_ +
                static_cast<double>(first_survey_steps_) * step_days ||
        frame.clock().backlog_days() != 0.)
      throw std::runtime_error(
          "First survey final time did not match its exact step count.");
    if (fleet_controller_.selection() != first_survey_fleet_id_)
      throw std::runtime_error(
          "First survey lost the science-vessel selection.");
    if (mode == Options::FirstSurveyMode::Depart &&
        (first_survey_revision_after_ != first_survey_revision_before_ + 1 ||
         first_survey_phase_after_ != FleetTransitPhase::None ||
         first_survey_survey_after_ !=
             static_cast<int>(SystemSurveyLevel::partially_surveyed) ||
         first_survey_survey_progress_after_ <=
             first_survey_survey_progress_before_ ||
         first_survey_survey_progress_after_ >= 1. ||
         first_survey_seen_phases_ != std::vector<int>{0, 1, 2, 3}))
      throw std::runtime_error(
          "First survey departure evidence is not an ongoing local survey.");
    if (mode != Options::FirstSurveyMode::Depart &&
        first_survey_revision_after_ != first_survey_revision_before_)
      throw std::runtime_error(
          "First survey continuation changed the persisted mission revision.");
    if (mode == Options::FirstSurveyMode::Paused &&
        (first_survey_steps_ != 0 || first_survey_input_orders_ != 0 ||
         first_survey_after_days_ != first_survey_before_days_ ||
         captured(inspection_capture) != before_inspection))
      throw std::runtime_error(
          "First survey paused mode advanced, ordered or changed Player17.");
    if (mode == Options::FirstSurveyMode::Resume &&
        (first_survey_phase_after_ != FleetTransitPhase::None ||
         first_survey_survey_after_ !=
             static_cast<int>(SystemSurveyLevel::fully_surveyed) ||
         first_survey_survey_progress_after_ != 1. ||
         first_survey_seen_phases_ != std::vector<int>{0} ||
         !first_survey_facts_visible_))
      throw std::runtime_error(
          "First survey resume did not finish the natural science survey.");
    if (mode == Options::FirstSurveyMode::Depart && first_survey_facts_visible_)
      throw std::runtime_error(
          "First survey exposed confirmed facts before full survey.");

    pause();
    const PlayerCampaignCaptureOptions final_capture{
        frame.clock().simulation_days(), STELLAR_GAME_VERSION,
        "2044-05-06T07:08:15Z"};
    const auto bytes = captured(final_capture);
    if (fleet_object(bytes, first_survey_scout_id_) != scout_before)
      throw std::runtime_error(
          "First survey changed the completed scout fleet object.");
    auto restored = restore_player_campaign_v17_json(
        load_adaptive_research_strategic_runtime(asset_root_ /
                                                 "Data/research/v1"),
        bytes);
    if (restored.simulation_days() != final_capture.simulation_days)
      throw std::runtime_error(
          "First survey Player17 restore changed the clock.");
    auto resumed = std::move(restored).activate();
    const auto recaptured = encode_player_campaign_v17_json(
        PreparedPlayerCampaignSave::capture(resumed, final_capture).payload());
    const auto first = nlohmann::json::parse(bytes);
    const auto second = nlohmann::json::parse(recaptured);
    if (first != second)
      throw std::runtime_error(
          "First survey Player17 roundtrip changed the campaign: " +
          nlohmann::json::diff(first, second).dump().substr(0, 4000));
    first_survey_roundtrip_ = true;
    InputSnapshot ready;
    ready.drawable_width = width;
    ready.drawable_height = height;
    if (!update(ready, width, height, 0., true))
      throw std::runtime_error(
          "First survey could not establish its save boundary.");
    session_->request_save();
  }
  [[nodiscard]] std::string first_survey_smoke_status() const {
    const auto mode =
        first_survey_mode_ == Options::FirstSurveyMode::Depart   ? "depart"
        : first_survey_mode_ == Options::FirstSurveyMode::Paused ? "paused"
                                                                 : "resume";
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << std::boolalpha << "{\"mode\":\"" << mode
        << "\",\"seed\":115501,\"player_id\":" << first_survey_player_id_
        << ",\"fleet_id\":" << first_survey_fleet_id_
        << ",\"scout_id\":" << first_survey_scout_id_
        << ",\"body_id\":" << first_survey_body_id_
        << ",\"origin_id\":" << first_survey_origin_id_
        << ",\"target_id\":" << first_survey_target_id_
        << ",\"before_days\":" << first_survey_before_days_
        << ",\"after_days\":" << first_survey_after_days_
        << ",\"input_orders\":" << first_survey_input_orders_
        << ",\"steps\":" << first_survey_steps_
        << ",\"step_days\":" << (1. / 64.)
        << ",\"revision_before\":" << first_survey_revision_before_
        << ",\"revision_after\":" << first_survey_revision_after_
        << ",\"phase_before\":" << static_cast<int>(first_survey_phase_before_)
        << ",\"phase_after\":" << static_cast<int>(first_survey_phase_after_)
        << ",\"survey_before\":" << first_survey_survey_before_
        << ",\"survey_after\":" << first_survey_survey_after_
        << ",\"survey_progress_before\":"
        << first_survey_survey_progress_before_
        << ",\"survey_progress_after\":" << first_survey_survey_progress_after_
        << ",\"transit_progress_before\":"
        << first_survey_transit_progress_before_
        << ",\"transit_progress_after\":"
        << first_survey_transit_progress_after_ << ",\"seen_phases\":[";
    for (std::size_t index = 0; index < first_survey_seen_phases_.size();
         ++index) {
      if (index)
        out << ',';
      out << first_survey_seen_phases_[index];
    }
    out << "],\"selected\":" << first_survey_selected_
        << ",\"selection_read_only\":" << first_survey_selection_read_only_
        << ",\"preview_read_only\":" << first_survey_preview_read_only_
        << ",\"lane_connected\":" << first_survey_lane_connected_
        << ",\"inspection_read_only\":" << first_survey_inspection_read_only_
        << ",\"facts_visible\":" << first_survey_facts_visible_
        << ",\"paused\":"
        << (session_->frame().clock().speed() == StrategicSpeed::Paused)
        << ",\"save_roundtrip\":" << first_survey_roundtrip_ << '}';
    return out.str();
  }
  void capture_surface_smoke_state() {
    if (!smoke_surface_site_id_)
      return;
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    refresh_surface(true);
    if (!surface_workspace_.view())
      throw std::runtime_error(
          "Surface smoke lost its observer-safe colony view before save.");
    const auto &view = *surface_workspace_.view();
    const auto site =
        std::ranges::find(view.construction_sites, *smoke_surface_site_id_,
                          &NativeSurfaceSite::building_id);
    if (site == view.construction_sites.end() || site->complete)
      throw std::runtime_error(
          "Surface smoke lost its unfinished canonical site before save.");
    smoke_surface_x_ = site->x;
    smoke_surface_z_ = site->z;
    smoke_surface_rotation_ = site->rotation_degrees;
    smoke_surface_progress_ = site->industry_progress;
    smoke_surface_site_count_saved_ = view.construction_sites.size();
    smoke_surface_treasury_saved_ = view.treasury_budget_units;
    smoke_surface_saved_day_ = session_->frame().clock().simulation_days();
    smoke_surface_persisted_site_ = true;
  }
  void request_smoke_save() {
    if (smoke_settlement_mode_) {
      session_->frame().clock().set_speed(StrategicSpeed::Paused);
      capture_settlement_smoke_state();
    }
    if (smoke_surface_mode_)
      capture_surface_smoke_state();
    session_->request_save();
  }
  [[nodiscard]] double campaign_profile_days()const{return session_->frame().clock().simulation_days();}
  [[nodiscard]] StrategicSpeed campaign_profile_speed()const{return session_->frame().clock().speed();}
  [[nodiscard]] SessionNoticeKind campaign_profile_notice()const{return session_->notice().kind;}
  void campaign_profile_request_save(){session_->request_save();}
  void prepare_campaign_profile(int width,int height){
    const auto click=[&](UiRect bounds){const auto point=center(bounds);InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Campaign profile UI input closed the campaign.");};
    const auto layout=NativeUiLayout::for_viewport(width,height);
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)click(layout.pause);
    for(int index=0;index<4&&session_->frame().clock().resume_speed()!=StrategicSpeed::Maximum;++index)click(layout.speed);
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused||session_->frame().clock().resume_speed()!=StrategicSpeed::Maximum)throw std::runtime_error("Campaign profile did not select 8X through UI input.");
  }
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
  [[nodiscard]] std::string navigation_smoke_status()const{
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(6)<<std::boolalpha
       <<"{\"switches\":"<<smoke_navigation_switches_
       <<",\"final_workspace\":\"relations\""
       <<",\"credits_before\":"<<smoke_navigation_credits_before_
       <<",\"credits_after\":"<<smoke_navigation_credits_after_
       <<",\"no_charge\":"<<smoke_navigation_no_charge_
       <<",\"canonical_payload_unchanged\":"
       <<smoke_navigation_canonical_unchanged_
       <<",\"menu_blocked\":"<<smoke_navigation_menu_blocked_
       <<",\"modal_blocked\":"<<smoke_navigation_modal_blocked_
       <<",\"pause_retained\":"<<smoke_navigation_pause_retained_
       <<",\"keyboard_galaxy_playback\":"<<smoke_keyboard_playback_
       <<",\"keyboard_system_playback\":"<<smoke_keyboard_system_
       <<",\"keyboard_save_requested\":"<<smoke_keyboard_save_
       <<",\"keyboard_text_preserved\":"<<smoke_keyboard_text_
       <<",\"keyboard_blocked_contexts\":"<<smoke_keyboard_blocked_contexts_
       <<",\"day_unchanged\":"
       <<(session_->frame().clock().simulation_days()==smoke_navigation_day_)
       <<'}';
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
       <<" civilian_recovery="<<(smoke_civilian_recovery_?1:0)
       <<" fleet_located="<<(smoke_fleet_located_?1:0);
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
      <<":gesture_cleared="<<smoke_system_gesture_cleared_<<":focused="<<smoke_system_focused_
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
  [[nodiscard]] std::string surface_smoke_status()const{const auto render=surface_workspace_.scene_diagnostics();std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<std::boolalpha<<"{\"mode\":\""<<(smoke_surface_reload_?"paused_reload":"ordered")<<"\",\"system_id\":"<<smoke_surface_system_id_<<",\"body_id\":"<<smoke_surface_body_id_<<",\"colony_id\":"<<smoke_surface_colony_id_<<",\"type_id\":\""<<smoke_surface_type_id_<<"\",\"site_id\":"<<smoke_surface_site_id_.value_or(-1)<<",\"x\":"<<smoke_surface_x_<<",\"z\":"<<smoke_surface_z_<<",\"rotation\":"<<smoke_surface_rotation_<<",\"authorization\":"<<smoke_surface_authorization_<<",\"refund\":"<<smoke_surface_refund_<<",\"treasury_before\":"<<smoke_surface_treasury_before_<<",\"treasury_after_cancel\":"<<smoke_surface_treasury_before_<<",\"treasury_after_place\":"<<smoke_surface_treasury_after_place_<<",\"treasury_after_refund\":"<<smoke_surface_treasury_after_refund_<<",\"treasury_saved\":"<<smoke_surface_treasury_saved_<<",\"site_count_before\":"<<smoke_surface_site_count_before_<<",\"site_count_saved\":"<<smoke_surface_site_count_saved_<<",\"progress\":"<<smoke_surface_progress_<<",\"before_days\":"<<smoke_surface_before_day_<<",\"saved_days\":"<<smoke_surface_saved_day_<<",\"palette_selected\":"<<smoke_surface_palette_selected_<<",\"ghost_previewed\":"<<smoke_surface_ghost_previewed_<<",\"placement_cancelled\":"<<smoke_surface_placement_cancelled_<<",\"cancel_no_change\":"<<smoke_surface_cancel_no_change_<<",\"placement_confirmed\":"<<smoke_surface_placement_confirmed_<<",\"removal_previewed\":"<<smoke_surface_removal_previewed_<<",\"removal_confirmed\":"<<smoke_surface_removal_confirmed_<<",\"refund_exact\":"<<smoke_surface_refund_exact_<<",\"persisted_site\":"<<smoke_surface_persisted_site_<<",\"paused\":"<<(session_->frame().clock().speed()==StrategicSpeed::Paused)<<",\"render\":{\"sites\":"<<render.sites<<",\"meshes\":"<<render.meshes<<",\"triangles\":"<<render.triangles<<",\"road_segments\":"<<render.road_segments<<"}}";return out.str();}
  [[nodiscard]] std::string system_travel_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(9)<<std::boolalpha<<"{\"mode\":\""<<(smoke_system_travel_reload_?"paused_reload":"progress")<<"\",\"fleet_id\":"<<smoke_system_travel_fleet_id_.value_or(-1)<<",\"system_id\":"<<smoke_system_travel_system_id_.value_or(-1)<<",\"destination_id\":"<<smoke_system_travel_destination_id_.value_or(-1)<<",\"order_revision\":"<<smoke_system_travel_mission_revision_<<",\"lane_count\":"<<smoke_system_travel_lane_count_<<",\"before_x\":"<<smoke_system_travel_before_x_<<",\"before_y\":"<<smoke_system_travel_before_y_<<",\"after_x\":"<<smoke_system_travel_after_x_<<",\"after_y\":"<<smoke_system_travel_after_y_<<",\"before_days\":"<<smoke_system_travel_before_day_<<",\"after_days\":"<<smoke_system_travel_after_day_<<",\"selected\":"<<smoke_system_travel_selected_<<",\"canonical_moved\":"<<smoke_system_travel_canonical_moved_<<",\"rendered_moved\":"<<smoke_system_travel_rendered_moved_<<",\"paused_stable\":"<<smoke_system_travel_paused_stable_<<",\"pause_retained\":"<<smoke_system_travel_pause_retained_<<",\"known_arrow\":"<<smoke_system_travel_known_opened_<<",\"unknown_denied\":"<<smoke_system_travel_unknown_denied_<<",\"knowledge_unchanged\":"<<smoke_system_travel_knowledge_unchanged_<<",\"lanes_connected\":"<<smoke_system_travel_lanes_connected_<<'}';return out.str();}
  [[nodiscard]] bool wants_text_input() const noexcept {
    return !menu_ && !battle_workspace_.visible() && research_workspace_.wants_text_input() &&
           !shipyard_workspace_.visible() &&
           !construction_workspace_.visible();
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    if(video_settings_)video_settings_->service(input.focused,input.renderable());
    resize_galaxy_camera(width,height);
    support_notice_seconds_=std::max(0.,support_notice_seconds_-std::max(0.,elapsed));
    if(support_.poll())support_notice_seconds_=20.;
    feedback_.advance(elapsed);
    if(surface_workspace_.visible())surface_workspace_.set_terrain_image(surface_art_.request_image());
    pointer_=input.focused&&input.renderable()?input.pointer:Point{-1.f,-1.f};
    const auto timestamp=utc_timestamp();
    if(session_->service(timestamp,menu_)){
      support_.record("session",timestamp+" Campaign activated.");
      territory_overlay_.clear();
      last_territory_draw_={};
      territory_refresh_elapsed_=.5;
      feedback_.reset();
      notifications_.clear();
      notification_view_.close();
      seed_notifications();
      last_event_sound_={};
      if(presentation_audio_)presentation_audio_->stop_voice();
      selected_id_.reset();
      inspection_card_.clear();
      supply_workspace_.close();supply_controller_.clear();supply_day_.reset();supply_generation_.reset();
      economy_workspace_.clear();economy_controller_.clear();economy_day_.reset();economy_generation_.reset();last_industry_allocation_.reset();
      pre_menu_speed_=StrategicSpeed::Normal;
      fit_camera(width,height);
      galaxy_backdrop_.discard_campaign();
      bind_galaxy_backdrop(width,height);
      focus_home_map(width,height);
      refresh_knowledge();
      research_workspace_.discard_campaign();
      projected_research_stamp_.reset();
      fleet_workspace_.discard_campaign();
      shipyard_workspace_.discard_campaign();
      construction_workspace_.discard_campaign();
      diplomacy_workspace_.discard_campaign();
      diplomacy_portraits_.clear();
      colony_workspace_.discard_campaign();
      colony_roster_.discard_campaign();roster_day_.reset();roster_refresh_elapsed_=0.;
      surface_workspace_.discard_campaign();
      surface_buildings_.clear();surface_buildings_active_=false;
      battle_workspace_.discard_campaign();battle_refresh_elapsed_=0.;battle_art_bindings_.clear();battle_art_plan_.clear();
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
    const auto& session_notice=session_->notice();
    if(session_notice.kind!=last_support_notice_kind_||session_notice.message!=last_support_notice_){
      last_support_notice_kind_=session_notice.kind;last_support_notice_=session_notice.message;
      if(!session_notice.message.empty())support_.record("session",timestamp+" "+session_notice.message);
    }
    if(input.quit_requested)session_->request_exit();
    if(session_->new_campaign_pending()){
      const auto pending_layout=NativeUiLayout::for_viewport(width,height);
      for(const auto& event:input.events)
        if((event.type==InputEventType::EscapePressed)||
           (event.type==InputEventType::LeftPressed&&pending_layout.continue_button.contains(event.position))){
          cancel_new_game();if(audio_confirm_)audio_confirm_();break;
        }
      return true;
    }
    refresh_battle(width,height,elapsed);
    refresh_inspection();
    refresh_economy(false,false);
    refresh_roster(false);
    const auto layout=NativeUiLayout::for_viewport(width,height);
    const auto route_navigation=[&](UiAction action){
      const auto close_navigation_workspaces=[&]{
        research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
        diplomacy_workspace_.close();colony_roster_.close();economy_workspace_.close();
        supply_workspace_.close();system_workspace_.close();colony_workspace_.close();
        surface_workspace_.close();notification_view_.close();
      };
      if(action==UiAction::Map){
        close_navigation_workspaces();selected_id_.reset();refresh_inspection();return;
      }
      if(action==UiAction::Home){
        close_navigation_workspaces();
        focus_home_map(width,height,true);refresh_inspection();return;
      }
      if(action==UiAction::Inspect){
        if(system_workspace_.visible()){
          system_workspace_.set_notice("Select a body in this system to inspect its details.");return;
        }
        if(!selected_id_){
          const auto &world=session_->frame().runtime().world().campaign();const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
          if(player!=world.civilizations.end()){
            const auto home=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
            if(home!=world.systems.end())selected_id_=home->id;
          }
        }
        close_navigation_workspaces();refresh_inspection();return;
      }
      if(action==UiAction::ZoomIn||action==UiAction::ZoomOut){
        const auto direction=action==UiAction::ZoomIn?1.f:-1.f;
        if(system_workspace_.visible()){
          (void)system_workspace_.handle({InputEventType::Wheel,{static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},{},direction},width,height);
        }else if(!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!surface_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
          zoom_galaxy_camera(direction,{static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},width,height);
        }
        return;
      }
      if(action==UiAction::Explore){
        close_navigation_workspaces();refresh_fleets(true);
        bool selected_scout{};
        if(fleet_workspace_.view())for(const auto &fleet:fleet_workspace_.view()->own_fleets)if(fleet.role==FleetRole::Scout||fleet.role==FleetRole::Science){(void)fleet_controller_.select(session_->frame(),session_->cache().generation,fleet.id);selected_scout=true;break;}
        if(!selected_scout)fleet_workspace_.set_notice("No scout or science vessel is available. Select a fleet on the map to plan exploration.",false);
        return;
      }
      if(action==UiAction::Menu){toggle_menu();return;}
      if(action!=UiAction::Supply)supply_workspace_.close();
      if(action!=UiAction::Colonies)colony_roster_.close();
      if(action!=UiAction::Economy)economy_workspace_.close();
      fleet_workspace_.cancel_recovery();
      notification_view_.close();
      system_workspace_.close();
      colony_workspace_.close();
      surface_workspace_.close();
      if(action==UiAction::Colonies){
        if(colony_roster_.visible())colony_roster_.close();
        else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();colony_roster_.open();refresh_roster(true);}
      }else if(action==UiAction::Economy){
        if(economy_workspace_.visible())economy_workspace_.close();
        else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();economy_workspace_.open();refresh_economy(true,false);}
      }else if(action==UiAction::Supply){
        if(supply_workspace_.visible())supply_workspace_.close();
        else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();supply_workspace_.open();refresh_supply(true,false);}
      }else if(action==UiAction::Research){
        if(research_workspace_.visible())research_workspace_.close();
        else{shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();research_workspace_.open();refresh_research(true);}
      }else if(action==UiAction::Shipyard){
        if(shipyard_workspace_.visible())shipyard_workspace_.close();
        else{research_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();shipyard_workspace_.open();refresh_shipyard(true);}
      }else if(action==UiAction::Construction){
        if(construction_workspace_.visible())construction_workspace_.close();
        else{research_workspace_.close();shipyard_workspace_.close();diplomacy_workspace_.close();construction_workspace_.open();refresh_construction(true);}
      }else if(action==UiAction::Diplomacy){
        if(diplomacy_workspace_.visible())diplomacy_workspace_.close();
        else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.open();refresh_diplomacy(true);}
      }
    };
    if(!input.focused||!input.renderable())menu_hover_feedback_.reset();
    for(const auto &event:input.events){
      const bool hover_blocked=settings_visible()||(!menu_&&(battle_workspace_.visible()||settlement_workspace_.visible()||surface_workspace_.modal_open()||colony_workspace_.planetary_modal()||diplomacy_workspace_.modal_open()||shipyard_workspace_.confirmation_open()||construction_workspace_.confirmation_open()||fleet_workspace_.preview()));
      const auto hover_action=(!hover_blocked&&input.focused&&input.renderable())?layout.hit(event.position,menu_):UiAction::None;
      menu_hover_feedback_.update(event,static_cast<std::uint64_t>(hover_action));
      if(session_->new_campaign_pending()) break;
      if(event.type==InputEventType::PointerCancelled){settlement_workspace_.cancel_pending_input();(void)colony_roster_.handle(event,width,height);fleet_workspace_.cancel_recovery();colony_workspace_.cancel_freight();outpost_freight_controller_.clear();}
      if(voice_settings_&&voice_settings_->visible()){(void)voice_settings_->handle(event,width,height);gesture_.capture_for_ui();continue;}
      if(settings_hub_&&settings_hub_->handle(event,width,height)){gesture_.capture_for_ui();continue;}
      if(general_settings_&&general_settings_->visible()){
        notification_view_.close();
        (void)general_settings_->handle(event,width,height);
        gesture_.capture_for_ui();
        continue;
      }
      if(video_settings_&&video_settings_->visible()){
        notification_view_.close();
        (void)video_settings_->handle(event,width,height);
        gesture_.capture_for_ui();
        continue;
      }
      if(audio_settings_&&audio_settings_->visible()){
        notification_view_.close();
        (void)audio_settings_->handle(event,width,height);
        gesture_.capture_for_ui();
        continue;
      }
      if(battle_workspace_.visible()&&!menu_){
        if(event.type==InputEventType::KeyPressed&&event.key==0x4000003fu&&
           input.focused&&input.renderable())session_->request_save();
        else execute_battle(battle_workspace_.handle(event,width,height));
        gesture_.capture_for_ui();continue;
      }
      if(event.type==InputEventType::KeyPressed&&event.key==0x40000041u&&
         input.focused&&input.renderable()&&!wants_text_input()&&
         !settlement_workspace_.visible()&&!(surface_workspace_.modal_open()||colony_workspace_.planetary_modal())&&
         !diplomacy_workspace_.modal_open()&&!shipyard_workspace_.confirmation_open()&&
         !construction_workspace_.confirmation_open()&&!fleet_workspace_.preview()){
        request_support(width,height);
        continue;
      }
      const bool can_notify=notifications_available();
      if(!can_notify)notification_view_.close();
      if(can_notify&&notification_view_.visible()){
        const auto command=notification_view_.handle(event,notifications_.items(),width,height);
        if(command.kind==stellar::native_notifications::NotificationViewCommandKind::OpenDiplomaticContact){
          system_workspace_.close();colony_workspace_.close();surface_workspace_.close();
          research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
          colony_roster_.close();economy_workspace_.close();supply_workspace_.close();diplomacy_workspace_.open();refresh_diplomacy(true);
          if(!diplomacy_workspace_.select_contact_civilization(command.civilization_id))
            diplomacy_workspace_.set_notice("This contact is no longer identified. Review the contact list.",false);
          refresh_diplomacy(true);
        }
        if(command.captured){gesture_.capture_for_ui();continue;}
      }
      if(can_notify&&event.type==InputEventType::LeftPressed&&
         layout.notifications.contains(event.position)){
        notification_view_.toggle(notifications_.latest_sequence());
        if(audio_confirm_)audio_confirm_();
        gesture_.begin(true);continue;
      }
      // Strategic shortcuts precede system-view capture, but never take keys
      // from text entry, a confirmation or a legacy gameplay-blocking view.
      if(event.type==InputEventType::KeyPressed&&input.focused&&input.renderable()&&
         !menu_&&!surface_workspace_.visible()&&!diplomacy_workspace_.visible()&&
         !settlement_workspace_.visible()&&!wants_text_input()&&
         !shipyard_workspace_.confirmation_open()&&
         !construction_workspace_.confirmation_open()&&!fleet_workspace_.preview()){
        auto& clock=session_->frame().clock();
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
        case 0x4000003fu:session_->request_save();break; // SDLK_F6
        default:handled=false;break;
        }
        if(handled){
          if(smoke_keyboard_commands_)++*smoke_keyboard_commands_;
          if(audio_confirm_)audio_confirm_();
          continue;
        }
      }
      const auto modal_blocks_navigation=!native_navigation_available(
          menu_,settlement_workspace_.visible(),diplomacy_workspace_.modal_open(),
          (surface_workspace_.modal_open()||colony_workspace_.planetary_modal()),
          settings_visible());
      if(event.type==InputEventType::LeftPressed&&!modal_blocks_navigation){
        const auto action=layout.hit(event.position,false);
        const auto navigation=action==UiAction::Map||action==UiAction::Home||action==UiAction::Inspect||action==UiAction::ZoomIn||action==UiAction::ZoomOut||action==UiAction::Explore||action==UiAction::Menu||action==UiAction::Research||
                              action==UiAction::Shipyard||
                              action==UiAction::Construction||
                              action==UiAction::Diplomacy||action==UiAction::Supply||action==UiAction::Economy||action==UiAction::Colonies;
        if(navigation){
          if(audio_confirm_)audio_confirm_();
          route_navigation(action);
          gesture_.begin(true);
          continue;
        }
      }
      if(colony_roster_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=colony_roster_.handle(event,width,height);
          if(command.refresh)refresh_roster(true);
          else if(command.open_colony_id)open_roster_colony(command,width,height);
          if(command.captured){gesture_.cancel();continue;}
        }
      }
      if(economy_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          using stellar::native_economy::EconomyCommandKind;
          const auto command=economy_workspace_.handle(event,economy_controller_.view(),width,height);
          if(command.kind==EconomyCommandKind::Refresh){refresh_economy(true,true);if(audio_confirm_)audio_confirm_();}
          else if(command.kind==EconomyCommandKind::SetIndustryPriority){
            const auto outcome=economy_controller_.change_priority(session_->frame(),session_->cache().generation,command.view_revision,command.priority);
            if(outcome.accepted){last_industry_allocation_.reset();publish_notification("Economy",outcome.message);if(audio_confirm_)audio_confirm_();}
            economy_workspace_.set_notice(outcome.message);
            refresh_economy(true,false);
          }
          if(command.captured){gesture_.cancel();continue;}
        }
      }
      if(supply_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=supply_workspace_.handle(event,supply_controller_.view(),width,height);
          if(command.refresh){refresh_supply(true,true);if(audio_confirm_)audio_confirm_();}
          if(command.captured){gesture_.cancel();continue;}
        }
      }
      if(surface_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
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
        if(colony_workspace_.freight_preview()||(top_action!=UiAction::Pause&&top_action!=UiAction::Speed)){
          const auto command=colony_workspace_.handle(event,width,height);
          if(command.kind==ColonyWorkspaceCommandKind::Close)gesture_.cancel();
          else if(command.kind==ColonyWorkspaceCommandKind::Planetary)execute_planetary(command.planetary);
          else if(command.kind==ColonyWorkspaceCommandKind::OpenSurface){outpost_freight_controller_.clear();open_surface(width,height);}
          else if(command.kind==ColonyWorkspaceCommandKind::ReviewFreight || command.kind==ColonyWorkspaceCommandKind::ConfirmFreight || command.kind==ColonyWorkspaceCommandKind::CancelFreight)
            execute_colony_freight(command);
          if(command.captured)continue;
        }
      }
      if(system_workspace_.visible()&&!colony_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const auto command=system_workspace_.handle(event,width,height);
          if(command.kind==SystemWorkspaceCommandKind::close){system_workspace_.close();gesture_.cancel();}
          else if(command.kind==SystemWorkspaceCommandKind::select_fleet){const auto selected=fleet_controller_.select_next_hit(session_->frame(),session_->cache().generation,command.hit_fleet_ids);system_workspace_.set_notice(selected.message);refresh_fleets(true);refresh_system_travel(true);}
          else if(command.kind==SystemWorkspaceCommandKind::open_destination){if(!enter_system(command.target_id,width,height))system_workspace_.set_notice("Destination details are not available to this observer.");}
          else if(command.kind==SystemWorkspaceCommandKind::reconnaissance_required){scientist_voice(stellar::native_audio::VoiceCue::ReconnaissanceRequired);}
           else if(command.kind==SystemWorkspaceCommandKind::open_colony){open_colony_from_system(command.target_id);}
           else if(command.kind==SystemWorkspaceCommandKind::settlement_target){preview_settlement(command.target_id,width,height);}
           else if(command.kind==SystemWorkspaceCommandKind::open_shipyard){
             refresh_colony_entry(true);
             if(system_workspace_.settlement_preparation()&&system_workspace_.selected_body_id()==command.target_id)
               route_navigation(UiAction::Shipyard);
           }
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
        gesture_.cancel();
        continue;
      }
      if(event.type==InputEventType::PointerCancelled){
        gesture_.cancel();
        (void)inspection_card_.handle(event,inspection_bounds(width,height));
        (void)supply_workspace_.handle(event,supply_controller_.view(),width,height);
        (void)economy_workspace_.handle(event,economy_controller_.view(),width,height);
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
        if(command.kind==DiplomacyWorkspaceCommandKind::Close){
          diplomacy_workspace_.close();
          gesture_.cancel();
        }else if(command.kind==DiplomacyWorkspaceCommandKind::SelectContact)
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
      if(inspection_visible()){
        const auto result=inspection_card_.handle(event,inspection_bounds(width,height));
        if(result.closed)selected_id_.reset();
        if(result.captured){gesture_.cancel();continue;}
      }
      if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
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
      if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
        const auto markers=fleet_markers(width,height);
        const auto target=event.type==InputEventType::RightPressed
                              ?system_hit(event.position,width,height)
                              :std::nullopt;
        const auto fleet_command=fleet_workspace_.handle(
            event,width,height,markers,target);
        handle_fleet_command(fleet_command);
        if(fleet_command.captured){
          if(event.type==InputEventType::LeftPressed)gesture_.begin(true);
          else if(event.type==InputEventType::LeftReleased)gesture_.cancel();
          continue;
        }
      }
      if(event.type==InputEventType::LeftPressed){
        const auto action=layout.hit(event.position,menu_);bool captured=menu_||action!=UiAction::None;
        if(action!=UiAction::None&&audio_confirm_)audio_confirm_();
        if(action==UiAction::Continue)toggle_menu();
        else if(action==UiAction::NewGame){gesture_.capture_for_ui();(void)session_->request_new_campaign();}
        else if(action==UiAction::Save)session_->request_save();
        else if(action==UiAction::Load)session_->request_load();
        else if(action==UiAction::Settings){if(settings_hub_)settings_hub_->open();else if(audio_settings_)audio_settings_->open();}
        else if(action==UiAction::Support)request_support(width,height);
        else if(action==UiAction::Exit)session_->request_exit();
        else if(action==UiAction::Pause){if(session_->frame().clock().speed()==StrategicSpeed::Paused)session_->frame().clock().resume();else session_->frame().clock().set_speed(StrategicSpeed::Paused);}
        else if(action==UiAction::Speed)cycle_speed();
        if(colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible()||diplomacy_workspace_.visible()||colony_workspace_.visible()||surface_workspace_.visible())captured=true;
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.allows_world_drag())pan_galaxy_camera(event.delta,width,height);gesture_.move(event.delta);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&!gesture_.captured_by_ui())zoom_galaxy_camera(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_||colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||surface_workspace_.visible()||colony_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible())(void)gesture_.release_as_world_click();}
    }
    if(const auto request=surface_workspace_.take_preview_request())execute_surface(*request);
    if(research_workspace_.take_refresh_request())refresh_research(true);
    if(advance_simulation){
      const auto frame_result=session_->advance(menu_?0.:elapsed,timestamp);
      publish_feedback(frame_result);
      const auto player=session_->frame().runtime().world().campaign().player_civilization_id;
      for(const auto& step:frame_result.strategic_results)
        for(const auto& allocation:step.core.industry_allocations)
          if(allocation.civilization_id==player)last_industry_allocation_=allocation;
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
    supply_refresh_elapsed_+=std::max(0.,elapsed);
    roster_refresh_elapsed_+=std::max(0.,elapsed);
    refresh_supply(false,false);
    economy_refresh_elapsed_+=std::max(0.,elapsed);
    refresh_economy(false,false);
    notification_refresh_elapsed_+=std::max(0.,elapsed);
    if(notification_refresh_elapsed_>=1.)refresh_notifications();
    refresh_knowledge();
    refresh_inspection();
    if(!inspection_visible())
      (void)inspection_card_.handle({InputEventType::PointerCancelled},inspection_bounds(width,height));
    territory_overlay_.poll();
    territory_refresh_elapsed_+=std::max(0.,elapsed);
    if(!system_workspace_.visible() &&
       ((!territory_overlay_.valid()&&!territory_overlay_.pending()) ||
        territory_refresh_elapsed_>=.5)) {
      const auto &territory_world=session_->frame().runtime().world().campaign();
      const auto diplomacy_view=stellar::core::ObserverDiplomacyCommandService(
          session_->frame().runtime().diplomacy()).build_view(
              territory_world.player_civilization_id);
      territory_overlay_.request_update(territory_world,
          territory_world.player_civilization_id,diplomacy_view.claims,
          session_->cache().generation);
      territory_refresh_elapsed_=0.;
    }
    update_surface_buildings(width,height);
    return true;
  }

  void update_surface_buildings(int width,int height){
    if(!surface_workspace_.visible()||!surface_workspace_.view()){
      surface_relief_failure_logged_=false;
      if(surface_buildings_active_){
        surface_workspace_.set_building_images({});
        surface_buildings_.clear();surface_buildings_active_=false;
      }
      return;
    }
    surface_buildings_active_=true;
    surface_buildings_.update(*surface_workspace_.view(),surface_workspace_.viewport(),
        SurfaceWorkspaceLayout::for_viewport(width,height).terrain,
        surface_workspace_.placement_quote());
    surface_workspace_.set_building_images(surface_buildings_.provider(),surface_buildings_.preview());
    if(surface_buildings_.stats().failed)
      surface_workspace_.set_artwork_notice("Detailed buildings unavailable. Reopen the surface to retry.");
    else if(surface_workspace_.relief_stats().failed){
      surface_workspace_.set_artwork_notice("Terrain shading unavailable. Reopen the surface to retry.");
      if(!surface_relief_failure_logged_){
        std::cerr<<"Native terrain shading unavailable: "<<surface_workspace_.relief_error()<<'\n';
        surface_relief_failure_logged_=true;
      }
    }
  }

  void surface_building_smoke(int width,int height,const std::function<void(const DrawList&)>&draw){
    if(!surface_workspace_.visible()||!surface_buildings_.ready())
      throw std::runtime_error("Surface building capture requires ready owned-colony artwork.");
    surface_workspace_.set_building_images({});
    draw(scene(width,height));
    surface_workspace_.set_building_images(surface_buildings_.provider(),surface_buildings_.preview());
    (void)scene(width,height);
  }

  std::string surface_relief_smoke(int width,int height,const std::function<void(const DrawList&,bool)>&draw){
    const auto before=surface_workspace_.relief_stats();
    if(!surface_workspace_.visible()||!before.ready||before.pending||before.deferred||before.failed||
       before.cache_bytes!=stellar::native_surface::NativeSurfaceRelief::image_bytes||before.generated!=1)
      throw std::runtime_error("Surface relief did not prepare and reuse one bounded world-space image: "+surface_workspace_.relief_error());
    draw(scene(width,height),true);
    surface_workspace_.set_relief_suppressed(true);
    try{draw(scene(width,height),false);}catch(...){surface_workspace_.set_relief_suppressed(false);throw;}
    surface_workspace_.set_relief_suppressed(false);
    const auto after=surface_workspace_.relief_stats();
    if(after.generated!=before.generated||after.cache_bytes!=before.cache_bytes)
      throw std::runtime_error("Surface relief comparison regenerated or discarded its image.");
    const auto terrain=SurfaceWorkspaceLayout::for_viewport(width,height).terrain;
    std::ostringstream out;
    out<<std::boolalpha<<"{\"requested\":"<<after.requested<<",\"ready\":"<<after.ready
       <<",\"pending\":"<<after.pending<<",\"deferred\":"<<after.deferred<<",\"failed\":"<<after.failed
       <<",\"cache_bytes\":"<<after.cache_bytes<<",\"reserved_bytes\":"<<after.reserved_bytes
       <<",\"generated\":"<<after.generated<<",\"resolution\":"<<stellar::native_surface::NativeSurfaceRelief::image_resolution
       <<",\"terrain\":["<<terrain.x<<','<<terrain.y<<','<<terrain.width<<','<<terrain.height<<"]}";
    return out.str();
  }

  // Runtime-only evidence uses the same input routing and observer-owned view
  // as a player. It neither advances Core nor authors operating state.
  [[nodiscard]] std::string surface_inspection_smoke(int width,int height,
      const std::function<void(const DrawList&,bool)>& draw){
    if(!surface_workspace_.visible()||!surface_workspace_.view())
      throw std::runtime_error("Surface inspection requires an owned colony.");
    const auto initial=*surface_workspace_.view();
    if(initial.construction_sites.empty()||initial.construction_sites.size()>16)
      throw std::runtime_error("Surface inspection requires a bounded populated fixture.");
    const auto layout=SurfaceWorkspaceLayout::for_viewport(width,height);
    const auto original=surface_workspace_.selected_building_id();
    const auto click=[&](Point point){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
      input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Surface inspection unexpectedly closed the campaign.");
    };
    const auto select=[&](const NativeSurfaceSite& site){
      click(surface_workspace_.viewport().world_to_screen(site.x,site.z,layout.terrain));
      if(surface_workspace_.selected_building_id()!=std::optional{site.building_id})
        throw std::runtime_error("Surface inspection could not pick the visible building.");
    };
    // Steady profiling intentionally pans at different zooms. Establish the
    // player's Overview command as this proof's baseline, not that manual pan.
    click(center(layout.overview));
    const auto &world=session_->frame().runtime().world().campaign();
    const auto colony=std::ranges::find(world.colonies,initial.colony_id,&Colony::id);
    if(colony==world.colonies.end()||colony->civilization_id!=initial.player_civilization_id)
      throw std::runtime_error("Surface inspection lost its ownership binding.");
    const auto output=surface_colony_output(*colony);
    std::ostringstream evidence;evidence<<std::boolalpha<<"{\"sites\":[";
    bool first=true;
    for(const auto &site:initial.construction_sites){
      if(site.powered!=std::ranges::contains(output.powered_building_ids,site.building_id)||
         site.staffed!=std::ranges::contains(output.staffed_building_ids,site.building_id))
        throw std::runtime_error("Surface operating telemetry disagrees with Core allocation.");
      select(site);
      const auto rendered=scene(width,height);
      const auto status=stellar::native_colony_ui::surface_site_status(site);
      const bool visible=std::ranges::any_of(rendered.overlay,[&](const auto& command){
        const auto* label=std::get_if<Text>(&command);
        return label&&label->value==status.label&&label->clip&&layout.inspector.contains(label->at);
      });
      if(!visible)throw std::runtime_error("Selected facility status is missing from its inspector.");
      if(!first)evidence<<',';first=false;
      evidence<<"{\"id\":"<<site.building_id<<",\"complete\":"<<site.complete
        <<",\"enabled\":"<<site.enabled<<",\"condition\":"<<site.condition
        <<",\"powered\":"<<site.powered<<",\"staffed\":"<<site.staffed
        <<",\"status\":\""<<status.label<<"\",\"label_visible\":"<<visible<<'}';
    }
    const auto target=std::ranges::find_if(initial.construction_sites,[](const auto& site){
      return site.complete&&site.enabled&&(!site.powered||!site.staffed);
    });
    const auto &focused_site=target==initial.construction_sites.end()?initial.construction_sites.front():*target;
    select(focused_site);
    const auto overview=surface_workspace_.viewport();
    click(center(layout.focus));
    const auto focused=surface_workspace_.viewport();
    if(focused.center_x!=focused_site.x||focused.center_z!=focused_site.z||
       focused.pixels_per_unit<=overview.pixels_per_unit)
      throw std::runtime_error("Focus Selected did not center and enlarge the chosen building.");
    refresh_surface(true);
    const auto same_camera=[](const auto& a,const auto& b){
      return a.center_x==b.center_x&&a.center_z==b.center_z&&a.pixels_per_unit==b.pixels_per_unit;
    };
    if(!same_camera(focused,surface_workspace_.viewport()))
      throw std::runtime_error("Surface refresh discarded the focused camera.");
    const auto ready=[&]{
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
      do{
        update_surface_buildings(width,height);
        if(surface_buildings_.ready())return;
        draw(scene(width,height),false);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }while(std::chrono::steady_clock::now()<deadline);
      throw std::runtime_error("Focused surface artwork did not become ready within its bounded wait.");
    };
    ready();draw(scene(width,height),true);
    click(center(layout.overview));
    if(!same_camera(overview,surface_workspace_.viewport())){
      const auto restored=surface_workspace_.viewport();
      std::ostringstream error;error<<"Colony Overview failed to restore the overview camera: before "
        <<overview.center_x<<','<<overview.center_z<<','<<overview.pixels_per_unit
        <<" after "<<restored.center_x<<','<<restored.center_z<<','<<restored.pixels_per_unit;
      throw std::runtime_error(error.str());
    }
    if(original){
      const auto anchor=std::ranges::find(initial.construction_sites,*original,&NativeSurfaceSite::building_id);
      if(anchor!=initial.construction_sites.end())select(*anchor);
    }
    ready();(void)scene(width,height);
    evidence<<"],\"focused_id\":"<<focused_site.building_id
      <<",\"overview_zoom\":"<<overview.pixels_per_unit<<",\"focus_zoom\":"<<focused.pixels_per_unit
      <<",\"refresh_preserved\":true,\"overview_restored\":true}";
    return evidence.str();
  }

  std::string surface_management_smoke(int width,int height,const std::function<void(const DrawList&,bool)>& capture){
    if(!surface_workspace_.view())throw std::runtime_error("Management proof requires an owned surface.");
    const auto initial=*surface_workspace_.view();
    const auto found=std::ranges::find_if(initial.construction_sites,[](const auto& site){return site.complete;});
    if(found==initial.construction_sites.end())return "{\"available\":false}";
    const auto target=*found;
    const auto original=surface_workspace_.selected_building_id();
    const auto layout=SurfaceWorkspaceLayout::for_viewport(width,height);
    const auto camera=surface_workspace_.viewport();
    const auto click=[&](Point point){
      InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;
      input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
      if(!update(input,width,height,0.,false))throw std::runtime_error("Management proof closed the game.");
    };
    click(surface_workspace_.viewport().world_to_screen(target.x,target.z,layout.terrain));
    const auto current=[&](){
      const auto& sites=surface_workspace_.view()->construction_sites;
      const auto site=std::ranges::find(sites,target.building_id,&NativeSurfaceSite::building_id);
      if(site==sites.end())throw std::runtime_error("Management proof lost its owned building.");
      return *site;
    };
    const auto review=[&](UiRect button){
      click(center(button));
      if(!(surface_workspace_.modal_open()||colony_workspace_.planetary_modal())||!surface_workspace_.management_quote()||!surface_workspace_.management_quote()->accepted)
        throw std::runtime_error("Management button did not open an accepted cost review.");
      const auto& quote=*surface_workspace_.management_quote();
      const auto frame=scene(width,height);
      const bool cost=std::ranges::any_of(frame.overlay,[&](const auto& item){
        const auto* label=std::get_if<Text>(&item);
        return label&&label->value=="Authorization  "+quote.formatted_authorization&&label->clip&&layout.confirmation.contains(label->at);
      });
      if(!cost)throw std::runtime_error("Management confirmation concealed its authorization.");
      return frame;
    };
    const auto cancelled=review(layout.toggle_operation);
    capture(cancelled,true);
    click(center(layout.cancel));
    if((surface_workspace_.modal_open()||colony_workspace_.planetary_modal())||current().enabled!=target.enabled||current().prioritized!=target.prioritized)
      throw std::runtime_error("Cancelling a management review changed the building.");
    (void)review(layout.toggle_operation);click(center(layout.confirm));
    if(current().enabled==target.enabled)throw std::runtime_error("Confirmed operation toggle did not reach Core.");
    capture(scene(width,height),false);
    (void)review(layout.toggle_operation);click(center(layout.confirm));
    if(current().enabled!=target.enabled)throw std::runtime_error("Operation toggle could not be reversed.");
    (void)review(layout.priority);click(center(layout.confirm));
    if(current().prioritized==target.prioritized)throw std::runtime_error("Confirmed priority did not reach Core.");
    (void)review(layout.priority);click(center(layout.confirm));
    if(current().prioritized!=target.prioritized)throw std::runtime_error("Priority could not be reversed.");
    const auto restored=surface_workspace_.viewport();
    if(restored.center_x!=camera.center_x||restored.center_z!=camera.center_z||restored.pixels_per_unit!=camera.pixels_per_unit)
      throw std::runtime_error("Management input moved the camera.");
    if(original){const auto item=std::ranges::find(initial.construction_sites,*original,&NativeSurfaceSite::building_id);
      if(item!=initial.construction_sites.end())click(restored.world_to_screen(item->x,item->z,layout.terrain));}
    std::ostringstream out;out<<"{\"available\":true,\"site_id\":"<<target.building_id
      <<",\"cost_visible\":true,\"cancel_no_change\":true,\"operation_changed\":true,\"priority_changed\":true,\"restored\":true,\"camera_unchanged\":true}";
    return out.str();
  }

  [[nodiscard]] std::string surface_building_status(int width,int height)const{
    const auto stats=surface_buildings_.stats();
    std::ostringstream out;
    out<<"{\"requested\":"<<stats.requested<<",\"ready\":"<<stats.ready
       <<",\"pending\":"<<stats.pending<<",\"deferred\":"<<stats.deferred
       <<",\"failed\":"<<stats.failed<<",\"replaced\":"<<surface_workspace_.scene_diagnostics().replaced_structures
       <<",\"entries\":"<<stats.cache.entries<<",\"cache_bytes\":"<<stats.cache.cached_bytes
       <<",\"reserved_bytes\":"<<stats.cache.reserved_bytes<<",\"admitted\":"<<stats.cache.admitted
       <<",\"completed\":"<<stats.cache.completed<<",\"canceled\":"<<stats.cache.canceled;
    const auto terrain=SurfaceWorkspaceLayout::for_viewport(width,height).terrain;
    out<<",\"terrain\":["<<terrain.x<<','<<terrain.y<<','<<terrain.width<<','<<terrain.height<<"]}";
    return out.str();
  }

  [[nodiscard]] bool artwork_ready()const noexcept{
    if(surface_workspace_.visible()){
      const auto relief=surface_workspace_.relief_stats();
      return surface_art_.cache_bytes()>0&&surface_buildings_.ready()&&(relief.ready||relief.failed);
    }
    return system_workspace_.visible()?system_workspace_.artwork_ready():galaxy_backdrop_.artwork_ready()&&territory_overlay_.valid()&&!territory_overlay_.pending();
  }

  [[nodiscard]] DrawList scene(int width,int height){
    if(battle_workspace_.visible()&&!menu_){
      DrawList tactical;
      battle_art_plan_.clear();
      battle_workspace_.render(tactical,width,height,[&](DrawList& layer,const UiRect& field,float zoom,float scale){
        battle_art_plan_=native_battle_art::prepare_battle_art(battle_art_bindings_,
            [&](MassivePoint point){return battle_workspace_.project(point,width,height);},field,zoom,scale);
        std::vector<native_battle_ui::BattleShipTarget> targets;
        targets.reserve(battle_art_plan_.size());
        for(const auto& sprite:battle_art_plan_)
          targets.push_back({sprite.formation_id,sprite.center,sprite.size.x,sprite.heading_degrees});
        battle_workspace_.set_ship_targets(std::move(targets),width,height);
        if(!battle_art_suppressed_)battle_sprites_.append(layer,battle_art_plan_);
      });
      return tactical;
    }
    const auto screen_height=static_cast<float>(height);
    last_galaxy_label_stats_ = {};
    DrawList out; std::optional<std::size_t> galaxy_marker_begin; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    if(system_workspace_.visible())system_workspace_.render(out,width,height);else{
    galaxy_backdrop_.append(out,{cache.generation,width,height,camera_,fitted_pixels_per_world_,true});
    last_territory_draw_=territory_overlay_.append(out,camera_,width,height,
        static_cast<float>(fitted_pixels_per_world_), {false});
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    galaxy_marker_begin=out.world.size();
    std::vector<NativeGalaxyLabelCandidate> label_candidates;
    std::vector<NativeGalaxyLabelObstacle> label_obstacles;
    for (const auto &system : world.systems) {
      const auto point = camera_.project(
          {system.position.x, system.position.y}, width, height);
      const bool known = known_.contains(system.id);
      const bool selected_system = selected_id_ && *selected_id_ == system.id;
      NativeGalaxyStarAppearance appearance;
      const auto survey = world.knowledge.system_survey_level(
          world.player_civilization_id, system.id);
      if (survey == SystemSurveyLevel::fully_surveyed) {
        if (system.primary)
          appearance.primary = galaxy_star_visual(*system.primary);
        if (system.secondary)
          appearance.secondary = galaxy_star_visual(*system.secondary);
        if (system.tertiary)
          appearance.tertiary = galaxy_star_visual(*system.tertiary);
      }
      const float core_radius = galaxy_star_core_radius(
          camera_.pixels_per_world/fitted_pixels_per_world_,height,appearance.primary);
      const float extent=core_radius*4.5f;
      if(point.x < -extent || point.y < -extent || point.x > width+extent || point.y > height+extent)continue;
      galaxy_star_markers_.append(
          out, point, core_radius, appearance, selected_system,
          UiRect{0, 0, static_cast<float>(width), static_cast<float>(height)},
          known ? 1.f : .86f);

      // Names may cross the faint corona, but never the bright stellar core.
      // Reserving the full transparent sprite hid every nearby label at zoom.
      float marker_left = -core_radius * 1.1f;
      float marker_right = core_radius * 1.1f;
      float marker_top = marker_left;
      float marker_bottom = marker_right;
      const auto include_component = [&](Point offset, float scale) {
        const float extent = core_radius * 1.1f * scale;
        marker_left = std::min(marker_left, offset.x - extent);
        marker_right = std::max(marker_right, offset.x + extent);
        marker_top = std::min(marker_top, offset.y - extent);
        marker_bottom = std::max(marker_bottom, offset.y + extent);
      };
      if (appearance.secondary)
        include_component({core_radius * .88f, -core_radius * .48f}, .70f);
      if (appearance.tertiary)
        include_component({-core_radius * .82f, core_radius * .54f}, .58f);
      const float marker_radius = std::max(
          {std::abs(marker_left), std::abs(marker_right),
           std::abs(marker_top), std::abs(marker_bottom)});
      label_obstacles.push_back(
          {{point.x + marker_left, point.y + marker_top,
            marker_right - marker_left, marker_bottom - marker_top},
           NativeGalaxyLabelObstacleKind::star});
      if (selected_system || (known && camera_.pixels_per_world/fitted_pixels_per_world_ > 2.)) {
        const double dx = point.x - static_cast<float>(width) * .5f;
        const double dy = point.y - static_cast<float>(height) * .5f;
        label_candidates.push_back(
            {NativeGalaxyLabelKind::system, system.id, point, marker_radius,
             Text{{}, known ? system.name : "Unknown", {205, 222, 245, 235},
                  13},
             selected_system, -(dx * dx + dy * dy)});
      }
    }

    if (const auto *projection = territory_overlay_.projection()) {
      const float overview = native_territory_overview_blend(
          static_cast<float>(camera_.pixels_per_world) /
              NativeTerritoryOverlay::coordinate_scale,
          static_cast<float>(fitted_pixels_per_world_) /
              NativeTerritoryOverlay::coordinate_scale);
      const float detail = native_territory_detail(overview);
      for (const auto &region : projection->territories) {
        if (region.anchors.empty() ||
            (overview > .82f && region.anchors.size() < 2))
          continue;
        std::string name = region.civilization_name;
        for (auto &character : name)
          character = static_cast<char>(std::toupper(
              static_cast<unsigned char>(character)));
        const auto point = camera_.project(
            {region.label_position.x / NativeTerritoryOverlay::coordinate_scale,
             region.label_position.y / NativeTerritoryOverlay::coordinate_scale},
            width, height);
        if (point.x < 4.f || point.y < 4.f || point.x >= width - 4.f ||
            point.y >= height - 4.f)
          continue;
        auto empire_color = native_territory_color(
            region.civilization_id, world.player_civilization_id);
        empire_color.a = static_cast<std::uint8_t>(std::clamp(
            std::lround(static_cast<float>(empire_color.a) * .82f * detail),
            0l, 255l));
        label_candidates.push_back(
            {NativeGalaxyLabelKind::empire, region.civilization_id, point, 0.f,
             Text{{}, std::move(name), empire_color, 13},
             false, static_cast<double>(region.anchors.size())});
      }
    }

    const auto ui_layout = NativeUiLayout::for_viewport(width, height);
    const auto reserve_hud = [&](UiRect bounds) {
      if (bounds.width > 0.f && bounds.height > 0.f)
        label_obstacles.push_back(
            {bounds, NativeGalaxyLabelObstacleKind::hud});
    };
    reserve_hud(ui_layout.pause);
    reserve_hud(ui_layout.speed);
    reserve_hud(ui_layout.notifications);
    reserve_hud(ui_layout.map);reserve_hud(ui_layout.home);reserve_hud(ui_layout.inspect);reserve_hud(ui_layout.zoom_in);reserve_hud(ui_layout.zoom_out);
    reserve_hud(ui_layout.research);
    reserve_hud(ui_layout.shipyard);
    reserve_hud(ui_layout.construction);
    reserve_hud(ui_layout.diplomacy);
    reserve_hud(ui_layout.explore);reserve_hud(ui_layout.menu);
    reserve_hud(ui_layout.day_text);
    reserve_hud(ui_layout.status_text);
    reserve_hud(ui_layout.zoom_text);
    if (menu_) reserve_hud(ui_layout.menu_panel);
    if (selected_id_)
      reserve_hud({14.f, screen_height - 88.f, 320.f, 74.f});
    if (fleet_workspace_.view())
      reserve_hud(FleetWorkspaceLayout::for_viewport(width, height).panel);

    const UiRect label_viewport{4.f, 4.f,
                                std::max(1.f, static_cast<float>(width) - 8.f),
                                std::max(1.f, static_cast<float>(height) - 8.f)};
    auto label_layout = layout_native_galaxy_labels(
        std::move(label_candidates), label_viewport, label_obstacles,
        text_measurer_);
    last_galaxy_label_stats_ = label_layout.stats;
    for (auto &placement : label_layout.placements) {
      if (placement.kind == NativeGalaxyLabelKind::empire) {
        const Point nearest{
            std::clamp(placement.anchor.x, placement.bounds.x,
                       placement.bounds.x + placement.bounds.width),
            std::clamp(placement.anchor.y, placement.bounds.y,
                       placement.bounds.y + placement.bounds.height)};
        if (std::hypot(nearest.x - placement.anchor.x,
                       nearest.y - placement.anchor.y) > 32.f) {
          auto leader_color = placement.label.color;
          leader_color.a = std::min<std::uint8_t>(leader_color.a, 90);
          out.lines.push_back({placement.anchor, nearest, leader_color});
        }
        auto shadow = placement.label;
        shadow.at.x += 1.f;
        shadow.at.y += 1.f;
        shadow.color = {5, 11, 18, 230};
        out.text.push_back(std::move(shadow));
      }
      out.text.push_back(std::move(placement.label));
    }
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
    if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!system_workspace_.visible()&&!surface_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible())
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
    if(!menu_)colony_roster_.render(out,width,height);
    if(!menu_)supply_workspace_.render(out,supply_controller_.view(),width,height);
    if(!menu_)economy_workspace_.render(out,economy_controller_.view(),width,height);
    if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&
       !construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&
       !colony_workspace_.visible()&&!surface_workspace_.visible()&&!settlement_workspace_.visible())
      feedback_.render(out,width,height);
    const auto layout = NativeUiLayout::for_viewport(width, height);
    using stellar::native_ui_style::panel;
    panel(out, layout.pause, layout.pause.contains(pointer_), false);
    const auto paused=session_->frame().clock().speed()==StrategicSpeed::Paused;
    const auto icon_color=Color{225,238,250,255};
    if(paused){
      const auto center=Point{layout.pause.x+layout.pause.width*.5f,layout.pause.y+layout.pause.height*.5f};
      TriangleMesh play;play.color=icon_color;play.clip=layout.pause;
      play.vertices={{center.x-5.f*layout.scale,center.y-8.f*layout.scale},
                     {center.x-5.f*layout.scale,center.y+8.f*layout.scale},
                     {center.x+8.f*layout.scale,center.y}};
      play.indices={0,1,2};out.overlay.emplace_back(std::move(play));
    }else{
      const float bar_width=4.f*layout.scale,bar_height=15.f*layout.scale;
      const float center_x=layout.pause.x+layout.pause.width*.5f,center_y=layout.pause.y+layout.pause.height*.5f;
      fill(out,{center_x-bar_width-2.f*layout.scale,center_y-bar_height*.5f,bar_width,bar_height},icon_color);
      fill(out,{center_x+2.f*layout.scale,center_y-bar_height*.5f,bar_width,bar_height},icon_color);
    }
    panel(out, layout.speed, layout.speed.contains(pointer_), false);
    const auto active_speed=paused?session_->frame().clock().resume_speed():session_->frame().clock().speed();
    const int chevrons=active_speed==StrategicSpeed::Maximum?4:active_speed==StrategicSpeed::VeryFast?3:active_speed==StrategicSpeed::Fast?2:1;
    const std::string rate=active_speed==StrategicSpeed::Maximum?"8×":active_speed==StrategicSpeed::VeryFast?"3×":active_speed==StrategicSpeed::Fast?"2×":"1×";
    for(int index=0;index<chevrons;++index){
      const float x=layout.speed.x+12.f*layout.scale+index*7.f*layout.scale;
      const float y=layout.speed.y+layout.speed.height*.5f;
      out.overlay.emplace_back(Line{{x-3.f*layout.scale,y-5.f*layout.scale},{x+2.f*layout.scale,y},icon_color});
      out.overlay.emplace_back(Line{{x+2.f*layout.scale,y},{x-3.f*layout.scale,y+5.f*layout.scale},icon_color});
    }
    out.overlay.emplace_back(Text{{layout.speed.x+47.f*layout.scale,layout.speed.y+8.f*layout.scale},rate,
        icon_color,layout.metric_font_pixels,layout.speed.width-51.f*layout.scale,layout.speed,TextAlign::Left,FontFace::Heading});
    if(notifications_available()){
      panel(out,layout.notifications,layout.notifications.contains(pointer_),notification_view_.visible());
      const auto unread=notifications_.unread_count(notification_view_.last_read());
      control_label(out,layout.notifications,"EVENTS "+std::to_string(unread),
          unread?Color{240,197,106,255}:Color{225,238,250,255},
          layout.control_font_pixels,layout.scale,text_measurer_);
    }
    const auto draw_navigation=[&](UiRect bounds,UiAction action,bool active,std::string_view tip){
      panel(out,bounds,bounds.contains(pointer_),active);
      if(const auto image=navigation_art_.image(action))out.overlay.emplace_back(Image{image,{bounds.x+3.f*layout.scale,bounds.y+3.f*layout.scale,bounds.width-6.f*layout.scale,bounds.height-6.f*layout.scale}});
      if(action==UiAction::ZoomIn||action==UiAction::ZoomOut){const auto glyph=action==UiAction::ZoomIn?"+":"−";out.overlay.emplace_back(Text{{bounds.x+bounds.width*.5f,bounds.y+bounds.height*.5f-9.f*layout.scale},glyph,{245,250,255,255},static_cast<int>(18.f*layout.scale),0,bounds,TextAlign::Center,FontFace::Heading});}
      if(!bounds.contains(pointer_))return;
      const Color tooltip_text{235,244,255,255};
      const Text probe{{},std::string(tip),tooltip_text,layout.metric_font_pixels};
      const auto extent=text_measurer_?text_measurer_(probe):TextExtent{static_cast<int>(tip.size()*7),layout.metric_font_pixels+4};
      const UiRect tooltip{std::min(bounds.x+bounds.width+8.f,static_cast<float>(width-extent.width-12)),bounds.y,std::max(1.f,static_cast<float>(extent.width+12)),static_cast<float>(extent.height+8)};
      fill(out,tooltip,{4,14,27,245});stroke(out,tooltip,{82,155,194,230});
      out.overlay.emplace_back(Text{{tooltip.x+6.f,tooltip.y+4.f},std::string(tip),tooltip_text,layout.metric_font_pixels,tooltip.width-12.f,tooltip});
    };
    const auto navigation_visible=native_navigation_available(
        menu_,settlement_workspace_.visible(),diplomacy_workspace_.modal_open(),
        (surface_workspace_.modal_open()||colony_workspace_.planetary_modal()),settings_visible());
    if(navigation_visible){
      draw_navigation(layout.map,UiAction::Map,false,"Map");
      draw_navigation(layout.home,UiAction::Home,false,"Home");
      draw_navigation(layout.inspect,UiAction::Inspect,inspection_visible(),"Inspect");
      draw_navigation(layout.zoom_in,UiAction::ZoomIn,false,"Zoom in");
      draw_navigation(layout.zoom_out,UiAction::ZoomOut,false,"Zoom out");
      draw_navigation(layout.economy,UiAction::Economy,economy_workspace_.visible(),"Economy and industry");
      draw_navigation(layout.research,UiAction::Research,research_workspace_.visible(),"Research");
      draw_navigation(layout.construction,UiAction::Construction,construction_workspace_.visible(),"Construction");
      draw_navigation(layout.shipyard,UiAction::Shipyard,shipyard_workspace_.visible(),"Ships");
      draw_navigation(layout.explore,UiAction::Explore,fleet_controller_.selection().has_value(),"Explore");
      draw_navigation(layout.colonies,UiAction::Colonies,colony_roster_.visible(),"Colonies and outposts");
      draw_navigation(layout.supply,UiAction::Supply,supply_workspace_.visible(),"Supply network");
      draw_navigation(layout.diplomacy,UiAction::Diplomacy,diplomacy_workspace_.visible(),"Relations");
      draw_navigation(layout.menu,UiAction::Menu,false,"Menu");
    }
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y + 2.f * layout.scale},
        "Day " + std::to_string(static_cast<int>(
                     session_->frame().clock().simulation_days())),
        {154, 181, 211, 235}, layout.metric_font_pixels,
        layout.day_text.width, layout.day_text});
    if(!system_workspace_.visible()&&!research_workspace_.visible()&&
       !colony_workspace_.visible()&&!diplomacy_workspace_.visible()&&
       !shipyard_workspace_.visible()&&!construction_workspace_.visible()&&
       !economy_workspace_.visible()&&!supply_workspace_.visible()&&!menu_){
      std::ostringstream zoom;
      zoom << "Map zoom " << std::fixed << std::setprecision(1)
           << camera_.pixels_per_world / fitted_pixels_per_world_ << "x";
      out.overlay.emplace_back(Text{
          {layout.zoom_text.x, layout.zoom_text.y + 2.f * layout.scale},
          zoom.str(), {184, 223, 239, 255}, layout.metric_font_pixels,
          layout.zoom_text.width, layout.zoom_text});
    }
    if(inspection_visible())inspection_card_.render(out,inspection_bounds(width,height));
    const auto &notice = session_->notice();
    const bool preparing_galaxy=!system_workspace_.visible()&&
        (!galaxy_backdrop_.artwork_ready()||!territory_overlay_.valid()||territory_overlay_.pending());
    const bool support_notice=(support_.busy()||support_notice_seconds_>0.)&&
        notice.kind!=SessionNoticeKind::Failure&&notice.kind!=SessionNoticeKind::Loading&&
        notice.kind!=SessionNoticeKind::Saving;
    if (notice.kind != SessionNoticeKind::None || preparing_galaxy || support_notice) {
      auto message = notice.message;
      if(preparing_galaxy&&(notice.kind==SessionNoticeKind::None||notice.kind==SessionNoticeKind::Saved||notice.kind==SessionNoticeKind::Loaded))
        message="Updating star chart...";
      if (notice.kind == SessionNoticeKind::Loading) {
        message += " " +
                   std::to_string(static_cast<int>(notice.progress * 100.)) +
                   "%";
      }
      if(support_notice)message=support_.busy()?"Preparing local diagnostics...":
          support_.state()==stellar::native_support::SupportExportState::Succeeded?
          "Diagnostics exported. See the location in the pause menu.":
          "Diagnostic export failed. Open the pause menu for details.";
      out.overlay.emplace_back(Text{
          {layout.status_text.x,
           layout.status_text.y + 2.f * layout.scale},
          visible_notice(std::move(message)),
          (notice.kind == SessionNoticeKind::Failure||
           (support_notice&&support_.state()==stellar::native_support::SupportExportState::Failed))
              ? Color{255, 133, 123, 255}
              : Color{154, 211, 183, 255},
          layout.metric_font_pixels, layout.status_text.width,
          layout.status_text});
    }
    if (menu_) {
      stellar::native_ui_style::menu_panel(out, layout.menu_panel);
      label(out, layout.menu_heading, session_->new_campaign_pending()?"SAVING CAMPAIGN":"PAUSED", {238, 244, 255, 255},
            layout.heading_font_pixels, layout.scale, FontFace::Heading);
      const auto draw_button = [&](UiRect bounds, std::string text) {
        panel(out, bounds, bounds.contains(pointer_), false);
        control_label(out, bounds, std::move(text), {238, 244, 255, 255},
              layout.control_font_pixels, layout.scale,text_measurer_);
      };
      draw_button(layout.continue_button, session_->new_campaign_pending()?"CANCEL NEW GAME":"CONTINUE");
      if(!session_->new_campaign_pending()){
      draw_button(layout.save_button, "SAVE");
      draw_button(layout.load_button, "LOAD");
      draw_button(layout.settings_button, "SETTINGS");
      draw_button(layout.support_button, support_.busy()?"EXPORTING...":"EXPORT DIAGNOSTICS");
      draw_button(layout.new_game_button, "NEW GAME");
      draw_button(layout.exit_button, "EXIT TO WINDOWS");
      }
      const float footer_y=layout.menu_panel.y+layout.menu_panel.height+8.f*layout.scale;
      const UiRect footer{36.f*layout.scale,footer_y,
          static_cast<float>(width)-72.f*layout.scale,
          std::max(0.f,static_cast<float>(height)-footer_y-12.f*layout.scale)};
      if(footer.height>=32.f*layout.scale){
        std::string detail="F12 saves a PNG screenshot. Export diagnostics (F8) includes your last completed save and recent session reports.";
        if(support_.state()==stellar::native_support::SupportExportState::Succeeded)
          detail="Diagnostics saved: "+utf8_path(support_.result());
        else if(support_.state()==stellar::native_support::SupportExportState::Failed)
          detail="Could not export diagnostics: "+support_.error()+" Your campaign is still open.";
        out.overlay.emplace_back(Text{{footer.x,footer.y},std::move(detail),
            {194,218,238,255},layout.metric_font_pixels,footer.width,footer});
      }
    }
    if(notifications_available())notification_view_.render(out,notifications_.items(),width,height);
    stellar::native_audio::render_voice_caption(out,presentation_audio_,width,height,text_measurer_);
    if(audio_settings_)audio_settings_->render(out,width,height);
    if(general_settings_)general_settings_->render(out,width,height);
    if(video_settings_)video_settings_->render(out,width,height);
    if(voice_settings_)voice_settings_->render(out,width,height);
    if(settings_hub_)settings_hub_->render(out,width,height);
    return out;
  }
 private:
  void refresh_battle(int width,int height,double elapsed){
    auto& frame=session_->frame();
    const auto& world=frame.runtime().world().campaign();
    const bool active=world.active_combat_encounter&&!world.active_combat_encounter->reconciled;
    if(!active){if(battle_workspace_.visible())battle_workspace_.close();battle_refresh_elapsed_=0.;battle_art_bindings_.clear();return;}
    bool observed_changed=false;
    if(!battle_workspace_.visible()){
      colony_roster_.close();economy_workspace_.close();supply_workspace_.close();notification_view_.close();research_workspace_.close();shipyard_workspace_.close();
      construction_workspace_.close();diplomacy_workspace_.close();system_workspace_.close();
      colony_workspace_.close();surface_workspace_.close();
      battle_workspace_.open(frame.tactical_snapshot(),world.player_civilization_id,width,height);
      observed_changed=true;
      battle_refresh_elapsed_=0.;
    }else{
      battle_refresh_elapsed_+=std::max(0.,elapsed);
      if(battle_refresh_elapsed_>=.1){
        battle_workspace_.set_snapshot(frame.tactical_snapshot(),battle_refresh_elapsed_);
        observed_changed=true;
        battle_refresh_elapsed_=0.;
      }
    }
    if(observed_changed){
      const auto fleets=fleet_controller_.build(frame,session_->cache().generation);
      battle_art_bindings_.clear();
      if(player_species_id()=="terran_baseline")
        battle_art_bindings_=native_battle_art::bind_owned_battle_art(*battle_workspace_.snapshot(),
            *world.active_combat_encounter,world.player_civilization_id,fleets);
    }
    battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());
  }
  void execute_battle(const native_battle_ui::BattleWorkspaceCommand& command){
    using native_battle_ui::BattleWorkspaceCommandKind;
    auto& frame=session_->frame();
    const auto change_speed=[&](double speed){
      if(frame.tactical_clock().speed_multiplier()>0.)frame.set_tactical_speed(speed);
      else frame.set_tactical_resume_speed(speed);
      battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());
    };
    switch(command.kind){
    case BattleWorkspaceCommandKind::IssueOrder:{
      std::size_t accepted=0;
      std::string message="Select a friendly formation before issuing an order.";
      for(const auto& order:command.orders){
        const auto result=frame.issue_tactical_order(order);
        accepted+=result.accepted?1u:0u;message=result.message;
      }
      if(command.orders.size()>1)message="Orders accepted for "+std::to_string(accepted)+" of "+std::to_string(command.orders.size())+" formations.";
      last_battle_order_accepted_=accepted>0&&accepted==command.orders.size();
      battle_workspace_.set_status(message,!last_battle_order_accepted_);
      support_.record("combat",utc_timestamp()+" "+message);
      if(presentation_audio_){if(last_battle_order_accepted_)presentation_audio_->confirm();else presentation_audio_->play_event(stellar::native_audio::Cue::Alert);}
      battle_workspace_.set_snapshot(frame.tactical_snapshot(),0.);
      return;
    }
    case BattleWorkspaceCommandKind::TogglePause:
      frame.set_tactical_speed(frame.tactical_clock().speed_multiplier()>0.?0.:frame.tactical_resume_speed());
      battle_workspace_.set_tactical_speed(frame.tactical_clock().speed_multiplier(),frame.tactical_resume_speed());return;
    case BattleWorkspaceCommandKind::CycleSpeed:{
      const auto current=frame.tactical_resume_speed();double next=.25;
      for(const auto speed:MassiveCombatClock::allowed_speeds())if(speed>current+1e-9){next=speed;break;}
      change_speed(next);return;
    }
    case BattleWorkspaceCommandKind::SetTacticalSpeed:change_speed(command.speed);return;
    case BattleWorkspaceCommandKind::Menu:toggle_menu();return;
    case BattleWorkspaceCommandKind::Fit:case BattleWorkspaceCommandKind::None:return;
    }
  }
  void request_support(int width,int height){
    if(support_.busy())return;
    support_.record("support",utc_timestamp()+" Local diagnostic export requested.");
    std::ostringstream info;
    info<<support_environment_<<"Viewport="<<width<<'x'<<height<<'\n'
        <<"Systems="<<system_count()<<"\nSimulationDays="
        <<session_->frame().clock().simulation_days()<<'\n'
        <<"SaveFormat=Player17\nSavePolicy=Last completed save; no save requested by export\n";
    auto root=session_->save_path().parent_path();
    if(root.empty())root=".";
    (void)support_.request({std::move(root),session_->save_path(),info.str(),{}});
    support_notice_seconds_=20.;
  }
  [[nodiscard]] bool notifications_available()const noexcept{
    return !battle_workspace_.visible()&&native_navigation_available(menu_,settlement_workspace_.visible(),
        diplomacy_workspace_.modal_open(),(surface_workspace_.modal_open()||colony_workspace_.planetary_modal()),
        settings_visible())&&
        !shipyard_workspace_.confirmation_open()&&!construction_workspace_.confirmation_open()&&
        !fleet_workspace_.preview();
  }
  void seed_notifications(){
    auto& runtime=session_->frame().runtime();
    diplomatic_notifications_.seed(runtime.diplomacy().build_view_for(
        runtime.world().campaign().player_civilization_id));
    notification_refresh_elapsed_=0.;
  }
  void refresh_notifications(){
    auto& runtime=session_->frame().runtime();
    diplomatic_notifications_.harvest(notifications_,runtime.diplomacy().build_view_for(
        runtime.world().campaign().player_civilization_id));
    notification_refresh_elapsed_=0.;
  }
  void publish_notification(std::string category,std::string message){
    support_.record(category,utc_timestamp()+" "+message);
    notifications_.publish(std::move(category),stellar::native_campaign::format_campaign_date(
        session_->frame().clock().simulation_days()),std::move(message));
  }
  void scientist_voice(stellar::native_audio::VoiceCue cue){
    // Human casting must not silently replace another species' advisor.
    if(presentation_audio_&&player_species_id()=="terran_baseline")presentation_audio_->speak(cue);
  }
  void publish_feedback(const CampaignFrameResult& frame){
    using namespace stellar::native_campaign_feedback;
    using stellar::native_audio::Cue;
    using stellar::native_audio::VoiceCue;
    const auto observer=session_->frame().runtime().world().campaign().player_civilization_id;
    const auto summary=collect_campaign_feedback(frame,observer);
    if(summary.empty())return;
    feedback_.publish(summary);
    stellar::native_notifications::publish_campaign_notifications(
        notifications_,summary,session_->frame().clock().simulation_days());
    if(summary.count(FeedbackKind::ResearchReport))scientist_voice(VoiceCue::ResearchReport);
    if(summary.count(FeedbackKind::SurveyComplete))scientist_voice(VoiceCue::SurveyComplete);
    // Coalesce accelerated simulation bursts into a single highest-priority cue.
    // Notices retain all categories/counts; no event history is re-read on load.
    if(!presentation_audio_)return;
    const auto now=std::chrono::steady_clock::now();
    if(now-last_event_sound_<std::chrono::seconds(2))return;
    Cue cue=Cue::Discovery;
    if(summary.count(FeedbackKind::CombatAlert))cue=Cue::Alert;
    else if(summary.count(FeedbackKind::ShipComplete))cue=Cue::Ship;
    else if(summary.count(FeedbackKind::ConstructionComplete)||summary.count(FeedbackKind::ColonyFounded))cue=Cue::Construction;
    presentation_audio_->play_event(cue);
    last_event_sound_=now;
  }
  [[nodiscard]] bool enter_system(int system_id,int width,int height){
    auto built=system_controller_.build(session_->frame(),session_->cache().generation,system_id);
    if(!built.snapshot)return false;
    auto travel=system_travel_controller_.build(session_->frame(),session_->cache().generation,*built.snapshot);
    colony_roster_.close();economy_workspace_.close();supply_workspace_.close();gesture_.cancel();research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();colony_workspace_.close();surface_workspace_.close();settlement_workspace_.clear();colony_entry_view_.reset();
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
      colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);system_workspace_.set_settlement_preparation(std::nullopt);return;
    }
    if(!force&&system_workspace_.settlement_preparation())return;
    if(!force&&colony_entry_view_&&colony_entry_view_->campaign_generation==session_->cache().generation&&colony_entry_view_->system_id==*system_workspace_.system_id()&&colony_entry_view_->body_id==*system_workspace_.selected_body_id())return;
    auto built=colony_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.snapshot(),*system_workspace_.selected_body_id());
    if(built.view){colony_entry_view_=std::move(*built.view);system_workspace_.set_colony_body(colony_entry_view_->body_id);}
    else{colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);}
    system_workspace_.set_settlement_preparation(colony_entry_view_?std::nullopt:
        stellar::native_settlement_preparation::build_settlement_preparation(session_->frame(),session_->cache().generation,
            *system_workspace_.system_id(),*system_workspace_.selected_body_id()));
  }

  void open_colony_from_system(int body_id){
    refresh_colony_entry(true);
    if(!colony_entry_view_||colony_entry_view_->body_id!=body_id){system_workspace_.set_notice("Colony operations are unavailable for this body.");return;}
    colony_workspace_.open(*colony_entry_view_);refresh_planetary_portrait();gesture_.capture_for_ui();colony_refresh_elapsed_=0.;
  }

  void refresh_colony(bool force){
    if(!colony_workspace_.visible()||!system_workspace_.snapshot()||!system_workspace_.selected_body_id())return;
    if(!force&&colony_refresh_elapsed_<.2)return;
    auto built=colony_controller_.build(session_->frame(),session_->cache().generation,*system_workspace_.snapshot(),*system_workspace_.selected_body_id());
    if(!built.view){surface_workspace_.close();colony_workspace_.close();colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);return;}
    colony_entry_view_=*built.view;if(surface_workspace_.visible())surface_workspace_.set_view(*built.view);colony_workspace_.set_view(std::move(*built.view));refresh_planetary_portrait();system_workspace_.set_colony_body(colony_entry_view_->body_id);colony_refresh_elapsed_=0.;
  }

  void execute_colony_freight(const ColonyWorkspaceCommand& command){
    if(command.kind==ColonyWorkspaceCommandKind::CancelFreight){
      outpost_freight_controller_.clear();colony_workspace_.cancel_freight();return;
    }
    if(!colony_workspace_.visible()||!colony_workspace_.view())return;
    if(command.kind==ColonyWorkspaceCommandKind::ReviewFreight){
      session_->frame().clock().set_speed(StrategicSpeed::Paused);
      colony_workspace_.set_freight_preview(outpost_freight_controller_.preview(
          session_->frame(),session_->cache().generation,*colony_workspace_.view()));
      gesture_.capture_for_ui();return;
    }
    if(!colony_workspace_.freight_preview()||
       colony_workspace_.freight_preview()->revision!=command.quote_revision ||
       session_->frame().clock().speed()!=StrategicSpeed::Paused){
      outpost_freight_controller_.clear();colony_workspace_.cancel_freight();return;
    }
    const auto outcome=outpost_freight_controller_.issue(session_->frame(),session_->cache().generation,command.quote_revision);
    colony_workspace_.cancel_freight();colony_workspace_.set_freight_notice(outcome.message);
    if(outcome.accepted){publish_notification("Freight",outcome.message);if(audio_confirm_)audio_confirm_();refresh_fleets(true);refresh_system_travel(true);}
    refresh_colony(true);
  }


  void refresh_planetary_portrait(){
    if(!colony_workspace_.planetary_enabled()||!colony_workspace_.view())return;
    const auto& v=*colony_workspace_.view();
    colony_workspace_.planetary().set_portrait(planet_discs_.request_image({v.campaign_generation,v.body_id,true,v.planet.visual_class,v.planet.sol_texture_key,static_cast<std::uint32_t>(v.body_id),-.4f}));
  }
  void execute_planetary(const PlanetaryCommand& command){
    if(!colony_workspace_.view())return;
    auto& screen=colony_workspace_.planetary();const auto& view=*colony_workspace_.view();
    const auto generation=session_->cache().generation;
    if(command.action==PlanetaryAction::None)return;
    if(command.action==PlanetaryAction::Save){session_->request_save();return;}
    if(command.action==PlanetaryAction::Cancel){
      std::visit([&](const auto& quote){using T=std::decay_t<decltype(quote)>;if constexpr(!std::is_same_v<T,std::monostate>)(void)surface_controller_.cancel_quote(generation,quote.quote_revision);},screen.pending());
      screen.complete("Order cancelled. No resources spent.");return;
    }
    if(command.action==PlanetaryAction::Confirm){
      NativeSurfaceCommandOutcome outcome{false,"Review the order again."};
      std::visit([&](const auto& quote){using T=std::decay_t<decltype(quote)>;
        if constexpr(std::is_same_v<T,NativeSurfacePlacementQuote>)outcome=surface_controller_.confirm_placement(session_->frame(),generation,quote);
        else if constexpr(std::is_same_v<T,NativeSurfaceManagementQuote>)outcome=surface_controller_.confirm_management(session_->frame(),generation,quote);
        else if constexpr(std::is_same_v<T,NativeSurfaceRemovalQuote>)outcome=surface_controller_.confirm_removal(session_->frame(),generation,quote);
      },screen.pending());
      screen.complete(outcome.message);refresh_colony(true);if(outcome.accepted&&audio_confirm_)audio_confirm_();return;
    }
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    if(command.action==PlanetaryAction::Build){screen.set_confirmation(surface_controller_.preview_placement(session_->frame(),generation,view,command.type,0,0,0,command.slot));return;}
    if(command.action==PlanetaryAction::Remove){screen.set_confirmation(surface_controller_.preview_removal(session_->frame(),generation,view,command.building_id));return;}
    NativeSurfaceManagementAction action;
    switch(command.action){
      case PlanetaryAction::CommandCenter:action=NativeSurfaceManagementAction::UpgradeHub;break;
      case PlanetaryAction::Upgrade:action=NativeSurfaceManagementAction::UpgradeBuilding;break;
      case PlanetaryAction::Repair:action=NativeSurfaceManagementAction::RepairBuilding;break;
      case PlanetaryAction::Enable:action=NativeSurfaceManagementAction::SetEnabled;break;
      case PlanetaryAction::Priority:action=NativeSurfaceManagementAction::SetPriority;break;
      default:return;
    }
    screen.set_confirmation(surface_controller_.preview_management(session_->frame(),generation,view,action,command.building_id,command.value));
  }

  void open_surface(int width,int height){
    if(!colony_workspace_.view()||!colony_workspace_.view()->solid_surface){
      return;
    }
    surface_workspace_.open(*colony_workspace_.view(),width,height);
    surface_workspace_.set_terrain_image(surface_art_.request_image());
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
    if(command.kind==SurfaceWorkspaceCommandKind::PreviewManagement){
      session_->frame().clock().set_speed(StrategicSpeed::Paused);
      auto quote=surface_controller_.preview_management(session_->frame(),generation,
          *surface_workspace_.view(),command.management_action,command.building_id,command.value);
      surface_workspace_.set_management_quote(std::move(quote));
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
    }else if(command.kind==SurfaceWorkspaceCommandKind::ConfirmManagement){
      if(!surface_workspace_.management_quote()||surface_workspace_.management_quote()->quote_revision!=command.quote_revision)return;
      outcome=surface_controller_.confirm_management(session_->frame(),generation,*surface_workspace_.management_quote());
      surface_workspace_.complete_management(visible_notice(outcome.message));
      refresh_surface(true);
      return;
    }else return;
    surface_workspace_.complete_command(visible_notice(outcome.message), outcome.accepted);
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
    if(outcome.accepted){smoke_research_node_=command.node_id;publish_notification("Research",outcome.message);}
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
    last_shipyard_command_accepted_=outcome.accepted;
    if(outcome.accepted)publish_notification("Ships",outcome.message);
    refresh_shipyard(true);
    shipyard_workspace_.set_notice(outcome.message,outcome.accepted);
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
    if(outcome.accepted)publish_notification("Construction",outcome.message);
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
    if(outcome.accepted)refresh_notifications();
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
    float best=std::max(10.f,galaxy_star_core_radius(camera_.pixels_per_world/fitted_pixels_per_world_,height)*1.8f);
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
    const auto names=observed_system_names();
    for(auto &fleet:view.own_fleets)
      fleet.recovery_message=observer_safe_fleet_message(fleet.recovery_message,names);
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
    if(command.kind==FleetWorkspaceCommandKind::Recovery){
      if(!command.recovery_quote)return;
      auto outcome=fleet_controller_.issue_civilian_recovery(
          session_->frame(),*command.recovery_quote,command.recovery_action,
          command.confirm_abandon);
      outcome.message=observer_safe_fleet_message(outcome.message,observed_system_names());
      pending_fleet_preview_.reset();
      fleet_workspace_.clear_preview();
      fleet_workspace_.set_recovery_result(*command.recovery_quote,outcome);
      last_fleet_command_accepted_=outcome.accepted;
      refresh_fleets(true);
      refresh_system_travel(true);
      return;
    }
    if(command.kind==FleetWorkspaceCommandKind::MilitaryOrder){
      if(!command.military_order_quote)return;
      const auto outcome=fleet_controller_.issue_selected_military_order(
          session_->frame(),*command.military_order_quote,command.military_order);
      const auto message=observer_safe_fleet_message(outcome.message,observed_system_names());
      last_fleet_command_accepted_=outcome.accepted;
      if(outcome.accepted){
        pending_fleet_preview_.reset();fleet_workspace_.clear_preview();
        publish_notification("Fleet",message);if(audio_confirm_)audio_confirm_();
      }
      fleet_workspace_.set_notice(message,outcome.accepted);
      refresh_fleets(true);refresh_system_travel(true);return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Locate){
      if(!command.locate_quote)return;
      const auto outcome=fleet_controller_.locate_selected(session_->frame(),*command.locate_quote);
      last_fleet_command_accepted_=outcome.accepted;
      if(outcome.accepted){
        camera_.center={outcome.position.x,outcome.position.y};
        if(audio_confirm_)audio_confirm_();
      }
      fleet_workspace_.set_notice(observer_safe_fleet_message(outcome.message,observed_system_names()),outcome.accepted);
      refresh_fleets(true);return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Engage){
      const auto outcome=session_->frame().begin_tactical(command.fleet_id);
      fleet_workspace_.set_notice(observer_safe_fleet_message(outcome.message,observed_system_names()),outcome.accepted);
      if(outcome.accepted)publish_notification("Combat",outcome.message);
      refresh_fleets(true);return;
    }
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

  void constrain_galaxy_camera(int width,int height){
    if(!galaxy_backdrop_.artwork_frame())return;
    const auto overview=galaxy_backdrop_.fit_camera(width,height);
    fitted_pixels_per_world_=overview.pixels_per_world;
    camera_.constrain_to_overview(overview,width,height);
  }
  void resize_galaxy_camera(int width,int height){
    if(width<=0||height<=0||!galaxy_backdrop_.artwork_frame())return;
    if(galaxy_view_width_==width&&galaxy_view_height_==height)return;
    const auto fit=galaxy_backdrop_.fit_camera(width,height);
    camera_.pixels_per_world*=fit.pixels_per_world/fitted_pixels_per_world_;
    fitted_pixels_per_world_=fit.pixels_per_world;
    galaxy_view_width_=width;galaxy_view_height_=height;
    constrain_galaxy_camera(width,height);
  }
  void pan_galaxy_camera(Point delta,int width,int height){
    camera_.pan_pixels(delta.x,delta.y);constrain_galaxy_camera(width,height);
  }
  void zoom_galaxy_camera(float wheel,Point pointer,int width,int height){
    camera_.zoom_at(wheel,pointer,width,height);constrain_galaxy_camera(width,height);
  }
  void focus_home_map(int width,int height,bool inspect=false){
    const auto& world=session_->frame().runtime().world().campaign();
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    if(player==world.civilizations.end())return;
    const auto home=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
    if(home==world.systems.end())return;
    std::vector<double> nearby;
    for(const auto& star:world.systems)if(star.id!=home->id)
      nearby.push_back(std::hypot(star.position.x-home->position.x,star.position.y-home->position.y));
    std::ranges::sort(nearby);
    const double radius=nearby.empty()?1.:std::max(1.,nearby[std::min<std::size_t>(7,nearby.size()-1)]);
    camera_.center={home->position.x,home->position.y};
    camera_.pixels_per_world=std::clamp(std::min(width,height)*.34/radius,
        fitted_pixels_per_world_*5.,std::max(100.,fitted_pixels_per_world_*5.));
    selected_id_=inspect?std::optional<int>{home->id}:std::nullopt;
    constrain_galaxy_camera(width,height);
  }
  void fit_camera(int width,int height){if(galaxy_backdrop_.artwork_frame()){camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;return;}const auto &systems=session_->frame().runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void toggle_menu(){settlement_workspace_.cancel_pending_input();colony_roster_.cancel_pending_input();colony_workspace_.cancel_freight();outpost_freight_controller_.clear();fleet_workspace_.cancel_recovery();notification_view_.close();menu_=!menu_;auto &frame=session_->frame();frame.set_menu_open(menu_);if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame.clock().speed();frame.clock().set_speed(StrategicSpeed::Paused);frame.pause_tactical_for_menu();}else{frame.resume_tactical_after_menu();frame.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=session_->frame().runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());if(galaxy_backdrop_.artwork_frame())galaxy_backdrop_.set_galactic_core_discovered(session_->cache().generation,world.knowledge.is_galactic_core_discovered(world.player_civilization_id));}
  [[nodiscard]] bool inspection_visible()const noexcept {
    return inspection_card_.visible()&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!menu_&&!system_workspace_.visible()&&
        !surface_workspace_.visible()&&!colony_workspace_.visible()&&
        !settlement_workspace_.visible()&&!research_workspace_.visible()&&
        !shipyard_workspace_.visible()&&!construction_workspace_.visible()&&
        !diplomacy_workspace_.visible()&&!battle_workspace_.visible()&&
        !notification_view_.visible()&&!settings_visible();
  }
  [[nodiscard]] static UiRect inspection_bounds(int width,int height) {
    const auto scale=NativeUiLayout::for_viewport(width,height).scale;
    const float x=80.f*scale,top=90.f*scale,bottom=static_cast<float>(height)-88.f*scale;
    const float panel_height=std::max(100.f,std::min(500.f*scale,bottom-top));
    return {x,bottom-panel_height,std::max(120.f,std::min(360.f*scale,static_cast<float>(width)-x-18.f*scale)),panel_height};
  }
  void refresh_economy(bool force,bool retry) {
    if(!economy_workspace_.visible()||menu_||battle_workspace_.visible())return;
    auto& frame=session_->frame();const auto& world=frame.runtime().world().campaign();
    const auto generation=session_->cache().generation;const auto day=frame.clock().simulation_days();
    const bool changed=economy_generation_!=generation||economy_observer_!=world.player_civilization_id;
    if(changed)last_industry_allocation_.reset();
    if(!force&&!changed&&(economy_refresh_elapsed_<1.||economy_day_==day))return;
    economy_refresh_elapsed_=0.;economy_day_=day;economy_generation_=generation;economy_observer_=world.player_civilization_id;
    const bool attempted=economy_controller_.refresh(frame,generation,last_industry_allocation_,retry);
    if(attempted&&!economy_controller_.view().diagnostic.empty())
      support_.record("economy",utc_timestamp()+" "+economy_controller_.view().diagnostic);
  }
  void refresh_roster(bool force){
    if(!colony_roster_.visible())return;
    const auto& world=session_->frame().runtime().world().campaign();
    const auto generation=session_->cache().generation;
    const auto day=session_->frame().clock().simulation_days();
    const auto& current=colony_roster_.view();
    const bool changed=current.generation!=generation||current.player_id!=world.player_civilization_id;
    if(!force&&!changed&&(!current.available|| (roster_day_&&(*roster_day_==day||roster_refresh_elapsed_<1.))))return;
    colony_roster_.set_view(stellar::native_colony_roster::build(world,generation));
    roster_day_=day;roster_refresh_elapsed_=0.;
  }
  void open_roster_colony(const stellar::native_colony_roster::RosterCommand& command,int width,int height){
    const auto generation=session_->cache().generation;
    const auto& world=session_->frame().runtime().world().campaign();
    if(command.generation!=generation||command.player_id!=world.player_civilization_id){
      refresh_roster(true);colony_roster_.set_notice("That colony selection changed. Select it again.");return;
    }
    const auto live=stellar::native_colony_roster::build(world,generation);
    const auto row=std::ranges::find(live.rows,*command.open_colony_id,&stellar::native_colony_roster::Row::colony_id);
    if(!live.available||row==live.rows.end()||!row->can_open||row->system_id!=command.system_id||row->body_id!=command.body_id){
      refresh_roster(true);colony_roster_.set_notice("That colony is no longer available to open.");return;
    }
    auto built=system_controller_.build(session_->frame(),generation,row->system_id);
    if(!built.snapshot){colony_roster_.set_notice(built.denial);return;}
    auto colony=colony_controller_.build(session_->frame(),generation,*built.snapshot,row->body_id);
    if(!colony.view||colony.view->colony_id!=row->colony_id){colony_roster_.set_notice("Colony operations are unavailable. Refresh the list.");return;}
    if(!enter_system(row->system_id,width,height))return;
    if(!system_workspace_.select_body(row->body_id))return;
    open_colony_from_system(row->body_id);
    if(audio_confirm_)audio_confirm_();
  }

  void refresh_supply(bool force,bool retry) {
    if(!supply_workspace_.visible()||menu_||battle_workspace_.visible())return;
    auto& frame=session_->frame();
    const auto& world=frame.runtime().world().campaign();
    const auto generation=session_->cache().generation;
    const auto day=frame.clock().simulation_days();
    const bool identity_changed=supply_generation_!=generation||supply_observer_!=world.player_civilization_id;
    if(!force&&!identity_changed&&(supply_refresh_elapsed_<1.||supply_day_==day))return;
    supply_refresh_elapsed_=0.;supply_day_=day;supply_generation_=generation;supply_observer_=world.player_civilization_id;
    const bool attempted=supply_controller_.refresh(world,world.player_civilization_id,generation,retry);
    if(attempted&&supply_controller_.view().state==stellar::native_logistics::LoadState::Failed)
      support_.record("supply",utc_timestamp()+" "+supply_controller_.view().diagnostic);
  }
  void refresh_inspection() {
    if(!selected_id_){inspection_card_.clear();return;}
    inspection_card_.set_inspection(stellar::native_inspection::build_system_inspection(
        session_->frame().runtime().world().campaign(),*selected_id_));
  }
  void bind_galaxy_backdrop(int width,int height){const auto &world=session_->frame().runtime().world().campaign();GalaxyBackdropCatalog view;view.campaign_generation=session_->cache().generation;view.campaign_seed=world.seed;view.system_positions.reserve(world.systems.size());for(const auto &system:world.systems)view.system_positions.push_back({system.position.x,system.position.y});if(world.core){view.galactic_core=WorldPoint{world.core->position.x,world.core->position.y};view.galactic_core_exclusion_radius=world.core->exclusion_radius;view.galactic_core_discovered=world.knowledge.is_galactic_core_discovered(world.player_civilization_id);}galaxy_backdrop_.bind(std::move(view));camera_=galaxy_backdrop_.fit_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void cycle_speed(){auto &clock=session_->frame().clock();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  [[nodiscard]] std::string speed_text(){const auto &clock=session_->frame().clock();switch(clock.speed()==StrategicSpeed::Paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Fast:return "SPEED 2X";case StrategicSpeed::VeryFast:return "SPEED 3X";case StrategicSpeed::Maximum:return "SPEED 8X";default:return "SPEED 1X";}}
  void select(Point pointer,int width,int height){selected_id_=system_hit(pointer,width,height);refresh_inspection();}
  std::unique_ptr<NativeCampaignSession> session_;
  Camera camera_;
  int galaxy_view_width_{},galaxy_view_height_{};
  NativeGalaxyStarMarkerRenderer galaxy_star_markers_;
  stellar::native_navigation::NativeNavigationArt navigation_art_;
  NativeResearchArt research_art_;
  NativeTerritoryOverlay territory_overlay_;
  NativeTerritoryDrawStats last_territory_draw_;
  NativeGalaxyLabelLayoutStats last_galaxy_label_stats_;
  double territory_refresh_elapsed_{.5};
  std::unordered_set<int> known_;
  std::optional<int> selected_id_;
  stellar::native_inspection::SystemInspectionCard inspection_card_;
  stellar::native_economy::NativeEconomyController economy_controller_;
  stellar::native_economy::NativeEconomyWorkspace economy_workspace_;
  std::optional<CivilizationIndustryAllocation> last_industry_allocation_;
  double economy_refresh_elapsed_{};
  std::optional<double> economy_day_;
  std::optional<std::uint64_t> economy_generation_;
  int economy_observer_{};
  stellar::native_logistics::HomeLogisticsController supply_controller_;
  stellar::native_logistics::SupplyWorkspace supply_workspace_;
  stellar::native_colony_roster::RosterWorkspace colony_roster_;
  std::optional<DrawList> smoke_colony_roster_capture_;
  std::string smoke_colony_roster_evidence_;
  std::optional<double> roster_day_;
  double roster_refresh_elapsed_{};
  double supply_refresh_elapsed_{};
  std::optional<double> supply_day_;
  std::optional<std::uint64_t> supply_generation_;
  int supply_observer_{};
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
  std::string smoke_diplomacy_other_;
  int smoke_diplomacy_target_{-1};
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
  NativeOutpostFreightController outpost_freight_controller_;
  std::optional<DrawList> smoke_freight_review_capture_;
  std::string smoke_freight_evidence_;
  NativeSurfaceConstructionController surface_controller_;
  NativeSurfaceWorkspace surface_workspace_;
  std::vector<DrawList> smoke_planetary_captures_;
  std::array<std::shared_ptr<const RgbaImage>,2> planetary_art_;
  bool surface_relief_failure_logged_{};
  NativeSettlementMissionController settlement_controller_;
  NativeSettlementWorkspace settlement_workspace_;
  std::optional<NativeColonyView> colony_entry_view_;
  NativeSystemViewController system_controller_;
  NativeSystemTravelController system_travel_controller_;
  NativeGalaxyBackdropAssets galaxy_assets_;
  NativeGalaxyBackdrop galaxy_backdrop_;
  double fitted_pixels_per_world_{.01};
  std::shared_ptr<ImagePreparationQueue> image_preparation_{std::make_shared<ImagePreparationQueue>()};
  NativeSurfaceBuildingPresentation surface_buildings_{image_preparation_};
  bool surface_buildings_active_{};
  NativePlanetDiscAssets planet_discs_;
  NativeShipArtAssets ship_art_;
  native_battle_art::NativeBattleSprites battle_sprites_;
  std::vector<native_battle_art::BattleArtBinding> battle_art_bindings_;
  std::vector<native_battle_art::BattleArtSprite> battle_art_plan_;
  bool battle_art_suppressed_{};
  bool smoke_battle_ship_selected_{};
  stellar::native_surface_ui::NativeSurfaceArtAssets surface_art_;
  SystemTextMeasurer text_measurer_;
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
  bool fresh_progression_reload_{},fresh_progression_no_instant_ships_{},fresh_progression_roundtrip_{};
  int fresh_progression_scout_id_{-1},fresh_progression_science_id_{-1},fresh_progression_research_actions_{},fresh_progression_construction_actions_{},fresh_progression_ship_actions_{},fresh_progression_quarter_steps_{};
  double fresh_progression_before_days_{},fresh_progression_after_days_{};
  Options::FirstExplorationMode first_exploration_mode_{Options::FirstExplorationMode::Depart};
  int first_exploration_player_id_{-1},first_exploration_fleet_id_{-1},first_exploration_origin_id_{-1},first_exploration_target_id_{-1};
  int first_exploration_input_orders_{},first_exploration_revision_before_{},first_exploration_revision_after_{};
  int first_exploration_survey_before_{},first_exploration_survey_after_{};
  std::uint64_t first_exploration_steps_{};
  FleetTransitPhase first_exploration_phase_before_{FleetTransitPhase::None},first_exploration_phase_after_{FleetTransitPhase::None};
  double first_exploration_before_days_{},first_exploration_after_days_{},first_exploration_survey_progress_before_{},first_exploration_survey_progress_after_{},first_exploration_transit_progress_before_{},first_exploration_transit_progress_after_{};
  std::vector<int> first_exploration_seen_phases_;
  bool first_exploration_selected_{},first_exploration_selection_read_only_{},first_exploration_preview_read_only_{},first_exploration_lane_connected_{},first_exploration_roundtrip_{};
  Options::FirstSurveyMode first_survey_mode_{Options::FirstSurveyMode::Depart};
  int first_survey_player_id_{-1},first_survey_fleet_id_{-1},first_survey_scout_id_{-1},first_survey_body_id_{-1},first_survey_origin_id_{-1},first_survey_target_id_{-1};
  int first_survey_input_orders_{},first_survey_revision_before_{},first_survey_revision_after_{},first_survey_survey_before_{},first_survey_survey_after_{};
  std::uint64_t first_survey_steps_{};
  FleetTransitPhase first_survey_phase_before_{FleetTransitPhase::None},first_survey_phase_after_{FleetTransitPhase::None};
  double first_survey_before_days_{},first_survey_after_days_{},first_survey_survey_progress_before_{},first_survey_survey_progress_after_{},first_survey_transit_progress_before_{},first_survey_transit_progress_after_{};
  std::vector<int> first_survey_seen_phases_;
  std::string preparation_evidence_;
  bool first_survey_selected_{},first_survey_selection_read_only_{},first_survey_preview_read_only_{},first_survey_lane_connected_{},first_survey_inspection_read_only_{},first_survey_facts_visible_{},first_survey_roundtrip_{};
  std::optional<std::string> smoke_research_node_;
  int smoke_navigation_switches_{};
  double smoke_navigation_credits_before_{},smoke_navigation_credits_after_{};
  double smoke_navigation_day_{};
  bool smoke_navigation_no_charge_{},smoke_navigation_canonical_unchanged_{};
  bool smoke_navigation_menu_blocked_{},smoke_navigation_modal_blocked_{};
  bool smoke_navigation_pause_retained_{};
  std::optional<unsigned> smoke_keyboard_commands_;
  bool smoke_keyboard_playback_{},smoke_keyboard_system_{};
  bool smoke_keyboard_save_{},smoke_keyboard_text_{};
  unsigned smoke_keyboard_blocked_contexts_{};
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
  bool smoke_civilian_recovery_{};
  bool smoke_fleet_located_{};
  std::optional<int> smoke_fleet_destination_;
  bool smoke_system_entered_{},smoke_system_hit_{},smoke_system_panned_{},smoke_system_zoomed_{},smoke_system_reset_{},smoke_system_back_{},smoke_system_pause_retained_{},smoke_system_speed_retained_{},smoke_system_gesture_cleared_{},smoke_system_focused_{};
  double smoke_system_day_{};
  std::optional<int> smoke_system_travel_fleet_id_,smoke_system_travel_system_id_,smoke_system_travel_destination_id_;
  int smoke_system_travel_mission_revision_{};std::size_t smoke_system_travel_lane_count_{};
  float smoke_system_travel_before_x_{},smoke_system_travel_before_y_{},smoke_system_travel_after_x_{},smoke_system_travel_after_y_{};double smoke_system_travel_before_day_{},smoke_system_travel_after_day_{};
  bool smoke_system_travel_reload_{},smoke_system_travel_selected_{},smoke_system_travel_canonical_moved_{},smoke_system_travel_rendered_moved_{},smoke_system_travel_paused_stable_{},smoke_system_travel_pause_retained_{},smoke_system_travel_known_opened_{},smoke_system_travel_unknown_denied_{},smoke_system_travel_knowledge_unchanged_{},smoke_system_travel_lanes_connected_{};
  bool smoke_colony_reload_{},smoke_colony_selected_{},smoke_colony_opened_{},smoke_colony_back_{},smoke_colony_pause_retained_{},smoke_colony_speed_retained_{};
  double smoke_colony_day_{};
  std::string settlement_completion_proof_;
  std::string earned_surface_proof_;
  std::string earned_surface_expansion_proof_;
  bool smoke_settlement_mode_{},smoke_settlement_reload_{},smoke_settlement_selected_{},smoke_settlement_previewed_{},smoke_settlement_accepted_{};
  bool smoke_settlement_cancelled_{},smoke_settlement_cancel_no_charge_{},smoke_settlement_requires_authorization_{},smoke_settlement_no_instant_colony_{};
  std::optional<int> smoke_settlement_fleet_id_,smoke_settlement_system_id_,smoke_settlement_body_id_;
  NativeSettlementMissionKind smoke_settlement_kind_{NativeSettlementMissionKind::Colony};
  std::size_t smoke_settlement_colonies_before_{};
  int smoke_settlement_revision_{};double smoke_settlement_before_day_{},smoke_settlement_saved_day_{},smoke_settlement_progress_{},smoke_settlement_authorization_{},smoke_settlement_treasury_before_{},smoke_settlement_treasury_after_{};
  bool smoke_surface_mode_{},smoke_surface_reload_{},smoke_surface_palette_selected_{},smoke_surface_ghost_previewed_{},smoke_surface_placement_cancelled_{},smoke_surface_cancel_no_change_{},smoke_surface_placement_confirmed_{},smoke_surface_removal_previewed_{},smoke_surface_removal_confirmed_{},smoke_surface_refund_exact_{},smoke_surface_persisted_site_{};
  int smoke_surface_system_id_{},smoke_surface_body_id_{},smoke_surface_colony_id_{};std::optional<int> smoke_surface_site_id_;std::string smoke_surface_type_id_;std::size_t smoke_surface_site_count_before_{},smoke_surface_site_count_saved_{};float smoke_surface_x_{},smoke_surface_z_{},smoke_surface_rotation_{};double smoke_surface_authorization_{},smoke_surface_refund_{},smoke_surface_treasury_before_{},smoke_surface_treasury_after_place_{},smoke_surface_treasury_after_refund_{},smoke_surface_treasury_saved_{},smoke_surface_progress_{},smoke_surface_before_day_{},smoke_surface_saved_day_{};
  std::function<void()> audio_confirm_;
  stellar::native_campaign_feedback::NativeCampaignFeedback feedback_;
  stellar::native_notifications::NativeNotificationFeed notifications_;
  stellar::native_support::NativeSupportService support_;
  native_battle_ui::NativeBattleWorkspace battle_workspace_;
  double battle_refresh_elapsed_{};
  bool last_battle_order_accepted_{};
  std::string smoke_battle_canonical_,smoke_battle_utc_;
  double smoke_battle_day_{};
  bool smoke_battle_reload_{},smoke_battle_paused_speed_{},smoke_battle_menu_pause_{};
  std::string support_environment_,last_support_notice_;
  SessionNoticeKind last_support_notice_kind_{};
  double support_notice_seconds_{};
  stellar::native_notifications::NativeNotificationView notification_view_;
  stellar::native_notifications::NativeDiplomaticNotifications diplomatic_notifications_;
  double notification_refresh_elapsed_{};
  stellar::native_audio::NativeAudioDirector* presentation_audio_{};
  stellar::native_menu_audio::HoverFeedback menu_hover_feedback_;
  std::chrono::steady_clock::time_point last_event_sound_{};
  bool settings_visible() const { return (settings_hub_&&settings_hub_->visible()) || (voice_settings_&&voice_settings_->visible()) || (general_settings_&&general_settings_->visible()) || (audio_settings_&&audio_settings_->visible()) || (video_settings_&&video_settings_->visible()); }
  stellar::native_general::NativeGeneralSettings* general_settings_{};
  stellar::native_settings::NativeSettingsHub* settings_hub_{};
  stellar::native_audio::NativeVoiceSettings* voice_settings_{};
  stellar::native_video_settings::NativeVideoController* video_settings_{};
  stellar::native_audio::NativeAudioSettings* audio_settings_{};
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
    const auto settings_path=options.save_path.parent_path()/"audio-settings.json";
    const auto video_settings_path=settings_path.parent_path()/"video-settings.json";
    const auto general_settings_path=settings_path.parent_path()/"general-settings.json";
    const auto initial_video=stellar::native_video_settings::NativeVideoSettings::load(video_settings_path);
    auto launch_video=initial_video;
    // Capture dimensions belong to the test viewport, not the player's saved
    // preferences. Normal --windowed launches still expose their actual mode.
    if(options.windowed&&!options.smoke_screenshot){launch_video.display=stellar::native_video_settings::VideoDisplayMode::Windowed;
      launch_video.width=options.window_width;launch_video.height=options.window_height;launch_video.refresh_hz=0.f;}
    Window window("Stellar Continuum - Native Galaxy",options.window_width,
                  options.window_height,!options.windowed&&initial_video.display!=stellar::native_video_settings::VideoDisplayMode::Windowed,
                  asset_root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");
    stellar::native_general::NativeGeneralSettings general_settings(general_settings_path);
    window.set_screenshot_directory(general_settings.saved().screenshot_directory);
    general_settings.set_text_measurer([&](const Text& text){return window.measure_text(text);});
    general_settings.set_apply([&](const auto& value){window.set_screenshot_directory(value.screenshot_directory);});
    try{general_settings.set_default_directory(Window::default_screenshot_directory());}
    catch(const std::exception& error){std::cerr<<"Default screenshot folder unavailable: "<<error.what()<<'\n';}
    general_settings.set_browse([&](auto id,const auto& path){return window.request_folder_dialog(id,path);});
    const auto service_general=[&]{if(auto result=window.take_folder_dialog_result())general_settings.accept_browse_result(std::move(*result));};
    // Declared after Window: audio closes its streams/device before SDL teardown.
    stellar::native_audio::NativeAudioDirector audio(asset_root,!options.smoke_screenshot||options.audio_check);
    stellar::native_audio::NativeAudioSettings audio_settings(settings_path,
      [&audio](const stellar::native_audio::AudioPreferences& value){audio.set_volumes(value.muted?0.f:value.master,value.music,value.effects);},
      [&audio]{audio.confirm();});
    stellar::native_video_settings::NativeVideoController video_settings(video_settings_path,
      [&](const stellar::native_video_settings::NativeVideoSettings& value){
        using namespace stellar::native_video_settings;
        // Explicit windowed smoke captures retain their requested viewport, but
        // normal settings changes apply all three supported display modes.
        if(!(options.smoke_screenshot&&options.windowed))window.set_display_mode(
          value.display==VideoDisplayMode::Windowed?WindowDisplayMode::Windowed:
          value.display==VideoDisplayMode::Exclusive?WindowDisplayMode::ExclusiveFullscreen:
          WindowDisplayMode::Borderless,value.width,value.height,value.refresh_hz);
        window.set_scene_quality(value.scene_resolution_percent,value.scene_samples);
        window.set_vsync(value.vsync==VideoVsync::Off?0:value.vsync==VideoVsync::On?1:-1);
        if(value.frame_cap==VideoFrameCap::Automatic)window.set_auto_frame_cap();
        else window.set_frame_cap(value.frame_cap==VideoFrameCap::Fps60?60.:value.frame_cap==VideoFrameCap::Fps120?120.:
          value.frame_cap==VideoFrameCap::Fps144?144.:0.);
      },stellar::native_video_settings::NativeVideoController::Clock::now,{},launch_video);
    const auto open_video=[&]{
      std::vector<stellar::native_video_settings::VideoDisplayChoice> modes;
      try{for(const auto& mode:window.display_modes())modes.push_back({mode.width,mode.height,mode.refresh_hz});}
      catch(const std::exception& error){std::cerr<<"Display choices unavailable: "<<error.what()<<'\n';}
      std::vector<stellar::native_video_settings::VideoDisplayChoice> windowed_modes;
      try{for(const auto& mode:window.windowed_display_modes())windowed_modes.push_back({mode.width,mode.height,mode.refresh_hz});}
      catch(const std::exception& error){std::cerr<<"Windowed display choices unavailable: "<<error.what()<<'\n';}
      auto desktop=stellar::native_map::DisplayMode{window.drawable_width(),window.drawable_height(),window.display_refresh_hz()};
      try{desktop=window.desktop_display_mode();}
      catch(const std::exception& error){std::cerr<<"Desktop display mode unavailable: "<<error.what()<<'\n';}
      video_settings.set_display_choices(std::move(modes),std::to_string(desktop.width)+" x "+
          std::to_string(desktop.height)+" @ "+std::to_string(static_cast<int>(std::lround(desktop.refresh_hz)))+" Hz");
      video_settings.set_windowed_display_choices(std::move(windowed_modes));
      video_settings.set_adapter(window.graphics_adapter(),window.has_nvidia_control_panel()?std::function<void()>{[&]{window.open_nvidia_control_panel();}}:std::function<void()>{});
      video_settings.open();
    };
    stellar::native_audio::NativeVoiceSettings voice_settings(settings_path.parent_path()/"voice-settings.json",
      [&](const auto& value){audio.set_voice_preferences(value);},[&]{(void)audio.replay_last_voice();},[&]{audio.stop_voice();});
    stellar::native_settings::NativeSettingsHub settings_hub;
    settings_hub.set_callbacks([&](stellar::native_settings::Category category){
      audio.confirm();
      switch(category){
      case stellar::native_settings::Category::General:general_settings.open();break;
      case stellar::native_settings::Category::Audio:audio_settings.open();break;
      case stellar::native_settings::Category::Video:open_video();break;
      case stellar::native_settings::Category::Voice:voice_settings.open();break;
      default:break;}
    },[&]{return general_settings.visible()||audio_settings.visible()||video_settings.visible()||voice_settings.visible();});
    audio_settings.set_video_navigation(open_video);
    audio_settings.set_general_navigation([&]{general_settings.open();});
    general_settings.set_navigation([&]{audio.confirm();audio_settings.open();},[&]{audio.confirm();open_video();});
    const auto menu_hover=[&]{audio.hover();};
    audio_settings.set_hover_callback(menu_hover);video_settings.set_hover_callback(menu_hover);
    general_settings.set_hover_callback(menu_hover);voice_settings.set_hover_callback(menu_hover);
    settings_hub.set_hover_callback(menu_hover);
    bool audio_menu_ready{};std::size_t audio_boot_services{};
    const StartupAudioHooks audio_hooks{
      [&]{audio.service();service_general();audio_settings.set_device_status(audio.failure_message());if(!audio_menu_ready){++audio_boot_services;if(audio.stats().music_started)throw std::runtime_error("Music started before the startup menu was ready.");}},
      [&]{audio_menu_ready=true;audio.menu_ready();},
      [&]{audio.confirm();},[&]{return audio.assets_ready();},[&]{audio.hover();}};
    const auto startup_config=[&]{
      StartupEntryConfig config{{asset_root/"Data/research/v1",asset_root/"Data/astronomy/hyg-nearby-500-v1.json",options.save_path,STELLAR_GAME_VERSION},asset_root,utc_timestamp};
      config.audio=audio_hooks;config.audio_settings=&audio_settings;config.video_settings=&video_settings;config.general_settings=&general_settings;config.settings_hub=&settings_hub;config.voice_settings=&voice_settings;config.caption=[&](DrawList& draw,int w,int h){stellar::native_audio::render_voice_caption(draw,&audio,w,h,[&](const Text& t){return window.measure_text(t);});};return config;
    };
    std::unique_ptr<NativeCampaignSession> session;
    StartupEntryEvidence startup_evidence;
    std::optional<std::filesystem::path> generated_save_path,setup_screenshot,loading_screenshot;
    if(options.new_game_smoke){
      if(!std::filesystem::is_regular_file(options.save_path))throw std::invalid_argument("--new-game-smoke requires a preexisting save-path anchor.");
      setup_screenshot=sidecar_path(*options.smoke_screenshot,L"-setup");
      loading_screenshot=sidecar_path(*options.smoke_screenshot,L"-loading");
      StartupEntryAutomation automation{std::to_string(options.seed),"pelagic_high_pressure",250,*setup_screenshot,*loading_screenshot};
      if(options.audio_settings_check){
        automation.audio_settings_path=settings_path;
        automation.audio_settings_screenshot=sidecar_path(*options.smoke_screenshot,L"-audio-settings");
      }
      if(options.video_settings_check){
        automation.video_settings_path=video_settings_path;
        automation.video_settings_screenshot=sidecar_path(*options.smoke_screenshot,L"-video-settings");
        automation.video_confirm_screenshot=sidecar_path(*options.smoke_screenshot,L"-video-confirm");
      }
      auto result=run_native_startup_entry(window,startup_config(),&automation);
      if(result.exit_requested||!result.session)throw std::runtime_error("Automated new campaign startup did not activate a session.");
      startup_evidence=std::move(result.evidence);generated_save_path=result.session->save_path();
      if(*generated_save_path==options.save_path)throw std::runtime_error("New campaign startup overwrote the requested save anchor.");
      session=std::move(result.session);
    }else if(options.load||options.smoke_screenshot){session=make_session(options);}
    else{
      auto result=run_native_startup_entry(window,startup_config());
      if(result.exit_requested)return 0;
      if(!result.session)throw std::runtime_error("Startup ended without a campaign session.");
      session=std::move(result.session);
    }
    audio_menu_ready=true;audio.menu_ready();
    bool restart_completed{};std::string restart_before;
    const auto restart_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(session){
    NativeCampaign campaign(std::move(session),window.drawable_width(),window.drawable_height(),options.asset_root,
                             [&window](const Text &label){return window.measure_text(label);},[&]{audio.confirm();},&audio_settings,&audio,&video_settings,&general_settings,&settings_hub,&voice_settings);
    campaign.configure_support(window.gpu_driver(),window.presentation_mode());
    if(options.smoke_screenshot){
      if(!options.planetary_smoke)campaign.use_legacy_colony_probes();
      if(options.research_smoke)
        campaign.prepare_research_smoke(window.drawable_width(),
                                        window.drawable_height(),!options.load);
      else if(options.navigation_smoke)
        campaign.prepare_navigation_smoke(window.drawable_width(),
                                          window.drawable_height());
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
      else if(options.campaign_profile)
        campaign.prepare_campaign_profile(window.drawable_width(),window.drawable_height());
      else if(options.system_travel_smoke||options.system_travel_reload_smoke){
        if(!options.voice_check)campaign.prepare_system_travel_smoke(window.drawable_width(),
                                             window.drawable_height(),options.system_travel_reload_smoke);
      }
      else if(options.colony_smoke||options.colony_reload_smoke){
        campaign.prepare_colony_smoke(window.drawable_width(),window.drawable_height(),options.colony_reload_smoke);
        if(options.planetary_smoke)campaign.prepare_planetary_smoke(window.drawable_width(),window.drawable_height(),options.colony_reload_smoke);
        else campaign.prepare_outpost_freight_smoke(window.drawable_width(),window.drawable_height(),options.colony_reload_smoke);
      }
      else if(options.settlement_smoke||options.settlement_reload_smoke)
        campaign.prepare_settlement_smoke(window.drawable_width(),
                                           window.drawable_height(),options.settlement_reload_smoke);
      else if(options.surface_smoke||options.surface_reload_smoke)
        campaign.prepare_surface_smoke(window.drawable_width(),
                                       window.drawable_height(),options.surface_reload_smoke);
      else if(options.earned_surface_mode)
        campaign.prepare_earned_surface_smoke(window.drawable_width(),window.drawable_height(),*options.earned_surface_mode,
          [&]{audio.service();auto progress_input=window.poll();if(!campaign.update(progress_input,progress_input.drawable_width,progress_input.drawable_height,0.,false))throw std::runtime_error("Earned surface window closed before completion.");if(progress_input.renderable())window.draw(campaign.scene(progress_input.drawable_width,progress_input.drawable_height));},
          [&](std::string_view tag){window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,tag=="review"?L"-review":tag=="colony"?L"-colony":L"-construction"));});
      else if(options.earned_surface_expansion_mode)
        campaign.prepare_earned_surface_expansion_smoke(window.drawable_width(),window.drawable_height(),*options.earned_surface_expansion_mode,
          [&]{audio.service();auto progress_input=window.poll();if(!campaign.update(progress_input,progress_input.drawable_width,progress_input.drawable_height,0.,false))throw std::runtime_error("Earned expansion window closed before completion.");if(progress_input.renderable())window.draw(campaign.scene(progress_input.drawable_width,progress_input.drawable_height));},
          [&](std::string_view tag){const auto suffix=std::wstring(L"-")+std::wstring(tag.begin(),tag.end());window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,suffix.c_str()));});
      else if(options.galaxy_art_smoke)
        campaign.prepare_galaxy_art_smoke(window.drawable_width(),
                                          window.drawable_height(),options.load);
      else if(options.diplomacy_smoke||options.diplomacy_reload_smoke)
        campaign.prepare_diplomacy_smoke(window.drawable_width(),window.drawable_height(),options.diplomacy_reload_smoke);
      else if(options.battle_smoke)
        campaign.prepare_battle_smoke(window.drawable_width(),window.drawable_height(),options.battle_reload_smoke);
      else if(options.ship_art_smoke)
        campaign.prepare_ship_art_smoke(window.drawable_width(),
                                        window.drawable_height());
      else if(options.first_exploration_mode)
        campaign.prepare_first_exploration_smoke(
            window.drawable_width(),window.drawable_height(),
            *options.first_exploration_mode,
            [&]{audio.service();audio_settings.set_device_status(audio.failure_message());auto progress_input=window.poll();if(!campaign.update(progress_input,progress_input.drawable_width,progress_input.drawable_height,0.,false))throw std::runtime_error("First exploration window closed before completion.");if(progress_input.renderable())window.draw(campaign.scene(progress_input.drawable_width,progress_input.drawable_height));},
            [&](std::string_view tag){window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,tag=="departure"?L"-departure":L"-arrival"));});
      else if(options.settlement_completion_mode)
        campaign.prepare_settlement_completion_smoke(window.drawable_width(),window.drawable_height(),*options.settlement_completion_mode,
            [&]{audio.service();auto progress=window.poll();if(!campaign.update(progress,progress.drawable_width,progress.drawable_height,0.,false))throw std::runtime_error("Settlement completion window closed.");if(progress.renderable())window.draw(campaign.scene(progress.drawable_width,progress.drawable_height));},
            [&]{window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,L"-establishment"));});
      else if(options.settlement_preparation_smoke)
        campaign.prepare_settlement_preparation_smoke(window.drawable_width(),window.drawable_height(),
            [&]{audio.service();auto progress=window.poll();if(!campaign.update(progress,progress.drawable_width,progress.drawable_height,0.,false))throw std::runtime_error("Settlement review window closed.");if(progress.renderable())window.draw(campaign.scene(progress.drawable_width,progress.drawable_height));},
            [&](std::string_view tag){window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,tag=="assessment"?L"-assessment":tag=="costs"?L"-costs":L"-shipyard"));});
      else if(options.first_survey_mode)
        campaign.prepare_first_survey_smoke(
            window.drawable_width(),window.drawable_height(),
            *options.first_survey_mode,
            [&]{audio.service();audio_settings.set_device_status(audio.failure_message());auto progress_input=window.poll();if(!campaign.update(progress_input,progress_input.drawable_width,progress_input.drawable_height,0.,false))throw std::runtime_error("First survey window closed before completion.");if(progress_input.renderable())window.draw(campaign.scene(progress_input.drawable_width,progress_input.drawable_height));},
            [&](std::string_view tag){window.draw(campaign.scene(window.drawable_width(),window.drawable_height()),sidecar_path(*options.smoke_screenshot,tag=="departure"?L"-departure":L"-inspection"));});
      else if(options.fresh_progression_smoke||options.fresh_progression_reload_smoke)
        campaign.prepare_fresh_progression_smoke(window.drawable_width(),window.drawable_height(),options.fresh_progression_reload_smoke,[&]{audio.service();audio_settings.set_device_status(audio.failure_message());auto progress_input=window.poll();if(!campaign.update(progress_input,progress_input.drawable_width,progress_input.drawable_height,0.,false))throw std::runtime_error("Fresh progression window closed before completion.");if(progress_input.renderable())window.draw(campaign.scene(progress_input.drawable_width,progress_input.drawable_height));});
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
    std::unique_ptr<SmokeColdProfile> cold_profile;
    if(options.profile_frames)cold_profile=std::make_unique<SmokeColdProfile>();
    FrameTiming draw_timing;
    bool voice_prepared{},voice_repeated{},voice_queue_bounded=true;
    std::uint64_t voice_first_count{};
    const auto voice_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    const int campaign_active_first=120;
    const int campaign_active_last=119+options.profile_frames.value_or(0);
    bool campaign_started{},campaign_pause_requested{},campaign_final_requested{},campaign_mid_requested{},campaign_mid_saved{},campaign_advanced_after_mid{};int campaign_wait_frames{};
    double campaign_before_days{},campaign_after_days{},campaign_mid_save_day{},campaign_mid_save_completed_day{},campaign_frozen_days{};
    while(true){
      const auto now=std::chrono::steady_clock::now();
      const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();
      prior=now;
      auto input=window.poll();
      // Automated captures drive their own player-input sequences below. Keep
      // window lifecycle events, but don't let typing in another app while a
      // test window opens accidentally toggle pause or order a fleet.
      if(options.smoke_screenshot)
        std::erase_if(input.events,[](const InputEvent& event){
          return event.type!=InputEventType::PointerCancelled;
        });
      if(options.restart_smoke&&!restart_completed){
        if(std::chrono::steady_clock::now()>restart_deadline)throw std::runtime_error("New Game lifecycle smoke timed out.");
        if(frames>=2&&!campaign.new_game_pending()&&restart_before.empty()&&
           (!options.audio_check||audio.assets_ready())){
          restart_before=campaign.restart_snapshot();
          const auto point=center(NativeUiLayout::for_viewport(input.drawable_width,input.drawable_height).new_game_button);
          input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
        }
        if(!restart_before.empty()&&campaign.campaign_profile_notice()==SessionNoticeKind::Failure)
          throw std::runtime_error("New Game save failed during lifecycle smoke.");
      }
      if(options.profile_frames&&(options.surface_smoke||options.surface_reload_smoke)){
        const auto terrain=SurfaceWorkspaceLayout::for_viewport(input.drawable_width,input.drawable_height).terrain;
        const auto point=center(terrain);
        if(frames==150||frames==180){
          input.pointer=point;
          input.events={{InputEventType::Wheel,point,{},frames==150?1.f:-1.f}};
        }else if(frames==160||frames==190){
          const Point delta{frames==160?40.f:-40.f,0.f};
          const Point moved{point.x+delta.x,point.y};
          input.pointer=moved;
          input.events={{InputEventType::LeftPressed,point},
              {InputEventType::PointerMove,moved,delta},
              {InputEventType::LeftReleased,moved}};
        }
      }
      audio.service();
      service_general();
      audio_settings.set_device_status(audio.failure_message());
      if(options.voice_check){
        const auto state=audio.stats();
        if(state.failed||std::chrono::steady_clock::now()>voice_deadline)
          throw std::runtime_error("Scientist playback check failed or timed out: "+audio.failure_message());
        if(input.renderable()&&state.assets_loaded&&!voice_prepared){
          if(!state.voice_available)throw std::runtime_error("Packaged British scientist cues did not load.");
          campaign.prepare_system_travel_smoke(input.drawable_width,input.drawable_height,options.system_travel_reload_smoke);
          voice_prepared=true;capture_frame=frames+120;
        }
        if(state.voice_active&&!voice_repeated){
          voice_first_count=state.voice_play_count;
          campaign.repeat_unknown_lane_voice_input(input.drawable_width,input.drawable_height);
          voice_repeated=true;
        }
        voice_queue_bounded=voice_queue_bounded&&state.queued_voice_bytes<=288000;
        if(!voice_prepared||!voice_repeated)capture_frame=frames+120;
      }
      if(!input.renderable()){
        discard_elapsed=true;
        if(!campaign.update(input,input.drawable_width,input.drawable_height,0.,false))break;
        window.set_text_input(campaign.wants_text_input());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }
      const auto elapsed=discard_elapsed?0.:measured_elapsed;
      const bool valid_interval=!discard_elapsed;
      if(options.campaign_profile&&(frames==119||(campaign_pause_requested&&!campaign_final_requested))){
        const auto bounds=NativeUiLayout::for_viewport(input.drawable_width,input.drawable_height).pause;const auto point=center(bounds);input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
        if(frames==119){campaign_before_days=campaign.campaign_profile_days();campaign_started=true;}
      }
      if(frames>0&&!discard_elapsed)frame_ms.push_back(elapsed*1000.);
      discard_elapsed=false;
      const auto update_begin=std::chrono::steady_clock::now();
      if(!campaign.update(input,input.drawable_width,input.drawable_height,
                          elapsed,!options.voice_check||voice_prepared))break;
      if(campaign.new_game_ready()){
        auto config=startup_config();config.return_to_campaign_available=true;
        config.host.default_save_path=campaign.save_path();
        audio.stop_voice();window.set_text_input(false);
        std::optional<StartupEntryAutomation> automation;
        if(options.restart_smoke){
          window.draw(campaign.scene(input.drawable_width,input.drawable_height),sidecar_path(*options.smoke_screenshot,L"-saved"));
          automation=StartupEntryAutomation{std::to_string(options.seed),"pelagic_high_pressure",250,
              sidecar_path(*options.smoke_screenshot,L"-setup"),sidecar_path(*options.smoke_screenshot,L"-loading")};
          automation->action=options.restart_action;
        }
        auto restart=run_native_startup_entry(window,std::move(config),automation?&*automation:nullptr);
        if(options.restart_smoke){
          std::cout<<"restart_renderer="<<window.gpu_driver()<<'\n';
          const auto& evidence=restart.evidence;
          if(evidence.boot_presented||evidence.menu_ready_called||!evidence.setup_opened)
            throw std::runtime_error("Live New Game replayed boot/music or skipped setup.");
          if(options.audio_check&&(audio.stats().failed||audio.stats().music_start_count!=1))
            throw std::runtime_error("Live New Game interrupted or restarted main-menu music.");
          std::cout<<"restart={\"setup_opened\":true,\"boot_replayed\":false,\"menu_ready_recalled\":false,\"exit\":"
              <<(restart.exit_requested?"true":"false")<<",\"returned\":"<<(restart.return_to_campaign?"true":"false")
              <<",\"created\":"<<(restart.session?"true":"false")<<",\"music_start_count\":"<<audio.stats().music_start_count<<"}\n";
        }
        if(restart.exit_requested)return 0;
        if(restart.return_to_campaign){
          campaign.cancel_new_game();prior=std::chrono::steady_clock::now();
          if(options.restart_smoke){
            if(campaign.restart_snapshot()!=restart_before)throw std::runtime_error("Cancel changed the original world, menu, camera or selection.");
            window.draw(campaign.scene(input.drawable_width,input.drawable_height),*options.smoke_screenshot);
            std::cout<<"restart_cancel=preserved_same_campaign\n";return 0;
          }
          discard_elapsed=true;continue;
        }
        if(!restart.session)throw std::runtime_error("New Game ended without a campaign or return outcome.");
        if(options.restart_smoke){
          if(restart.session->save_path()==campaign.save_path())throw std::runtime_error("New Game reused the original save slot.");
          generated_save_path=restart.session->save_path();restart_completed=true;
          std::cout<<"restart_new_save="<<utf8_path(*generated_save_path)<<'\n';
        }
        session=std::move(restart.session);break;
      }
      const auto update_end=std::chrono::steady_clock::now();
      if(options.campaign_profile&&campaign.campaign_profile_notice()==SessionNoticeKind::Failure)throw std::runtime_error("Campaign profile encountered a session/save failure.");
      if(options.campaign_profile&&campaign_started&&frames==119&&campaign.campaign_profile_speed()!=StrategicSpeed::Maximum)throw std::runtime_error("Campaign profile resume UI input did not select 8X.");
      if(options.campaign_profile&&campaign_pause_requested&&!campaign_final_requested){
        if(campaign.campaign_profile_speed()!=StrategicSpeed::Paused)throw std::runtime_error("Campaign profile pause UI input did not pause the clock.");campaign_after_days=campaign.campaign_profile_days();campaign_frozen_days=campaign_after_days;campaign.campaign_profile_request_save();campaign_final_requested=true;
      }
      window.set_text_input(campaign.wants_text_input());
      if(options.smoke_screenshot){
        ++frames;
        // Navigation replay saves through its actual F6 input, with no fallback.
        if((options.research_smoke||options.fleet_smoke||options.shipyard_smoke||options.construction_smoke||options.system_smoke||options.system_travel_smoke||options.system_travel_reload_smoke||options.colony_smoke||options.colony_reload_smoke||options.settlement_smoke||options.settlement_reload_smoke||options.surface_smoke||options.surface_reload_smoke||options.galaxy_art_smoke||options.ship_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)&&((!options.voice_check&&frames==60)||(options.voice_check&&voice_prepared&&frames==capture_frame-60)))
          campaign.request_smoke_save();
      }
      if(options.campaign_profile&&frames>=campaign_active_first&&frames<=campaign_active_last){
        if(campaign.campaign_profile_speed()!=StrategicSpeed::Maximum)throw std::runtime_error("Campaign profile clock left 8X during active measurement.");
        if(!campaign_mid_requested&&frames==campaign_active_first+*options.profile_frames/2){campaign_mid_save_day=campaign.campaign_profile_days();campaign.campaign_profile_request_save();campaign_mid_requested=true;}
        if(campaign_mid_requested&&!campaign_mid_saved&&campaign.campaign_profile_notice()==SessionNoticeKind::Saved){campaign_mid_saved=true;campaign_mid_save_completed_day=campaign.campaign_profile_days();}
        if(campaign_mid_saved&&campaign.campaign_profile_days()>campaign_mid_save_completed_day)campaign_advanced_after_mid=true;
        if(frames==campaign_active_last)campaign_pause_requested=true;
      }
      std::optional<std::filesystem::path> screenshot;
      if(options.smoke_screenshot){
        if(options.campaign_profile){
          if(campaign_final_requested&&campaign.campaign_profile_notice()==SessionNoticeKind::Failure)throw std::runtime_error("Campaign profile final save failed.");
          if(campaign_final_requested&&campaign.campaign_profile_days()!=campaign_frozen_days)throw std::runtime_error("Campaign profile clock advanced after its final UI pause.");
          if(campaign_final_requested&&campaign.campaign_profile_notice()==SessionNoticeKind::Saved&&campaign.artwork_ready())screenshot=options.smoke_screenshot;
          else if(campaign_final_requested&&++campaign_wait_frames>600)throw std::runtime_error("Campaign profile final save or artwork did not complete.");
        }else if(options.galaxy_art_smoke){
          if(frames==capture_frame)screenshot=options.smoke_screenshot;
          else if(frames==capture_frame+1)screenshot=sidecar_path(*options.smoke_screenshot,L"-regional");
          else if(frames==capture_frame+2)screenshot=sidecar_path(*options.smoke_screenshot,L"-system");
        }else if(options.diplomacy_smoke||options.diplomacy_reload_smoke){
          if(frames==capture_frame)screenshot=options.smoke_screenshot;
          else if(frames==capture_frame+1)screenshot=sidecar_path(*options.smoke_screenshot,L"-unknown");
          else if(frames==capture_frame+2)screenshot=sidecar_path(*options.smoke_screenshot,L"-map");
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
      const auto uploads_before=cold_profile?window.image_upload_count():0;
      window.draw(scene,screenshot,(steady_profile||cold_profile)?&draw_timing:nullptr);
      const auto render_end=std::chrono::steady_clock::now();
      if(cold_profile)cold_profile->observe(frames,
        std::chrono::duration<double,std::milli>(update_end-update_begin).count(),
        std::chrono::duration<double,std::milli>(scene_end-scene_begin).count(),
        std::chrono::duration<double,std::milli>(render_end-scene_end).count(),draw_timing,
        uploads_before,window.image_upload_count());
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
      if(!waiting_for_artwork&&(options.diplomacy_smoke||options.diplomacy_reload_smoke)&&frames==capture_frame+1)
        campaign.prepare_diplomacy_map_capture(input.drawable_width,input.drawable_height);
      const bool capture=!waiting_for_artwork&&options.smoke_screenshot&&(options.campaign_profile?screenshot.has_value():((options.galaxy_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)?frames>=capture_frame+3:options.ship_art_smoke?frames>=capture_frame+2:frames>=capture_frame));
      if(capture){
        if(options.colony_smoke||options.colony_reload_smoke){
          if(options.planetary_smoke)campaign.capture_planetary_smoke([&](const DrawList& draw,int i){const auto suffix=L"-planetary-"+std::to_wstring(i);window.draw(draw,sidecar_path(*options.smoke_screenshot,suffix.c_str()));});
          campaign.capture_colony_roster_smoke([&](const DrawList& draw){window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-colony-roster"));});
          campaign.capture_outpost_freight_smoke([&](const DrawList& draw){window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-freight-review"));});
        }
        if(options.surface_smoke||options.surface_reload_smoke){
          const auto evidence=campaign.surface_relief_smoke(window.drawable_width(),window.drawable_height(),
              [&](const DrawList& draw,bool enabled){window.draw(draw,sidecar_path(*options.smoke_screenshot,
                  enabled?L"-relief":L"-without-relief"));});
          std::cout<<"surface_relief="<<evidence<<'\n';
        }
        if(options.system_smoke){
          const auto evidence=campaign.body_inspection_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-body-details"));
              });
          std::cout<<"body_inspection="<<evidence<<'\n';
        }
        if(options.military_check){
          const auto evidence=campaign.military_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,std::string_view stage){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,stage=="ready"?L"-military-ready":stage=="retreat"?L"-military-retreat":L"-military-located"));
              });
          std::cout<<"military_inspection="<<evidence<<'\n';
        }
        if(options.economy_check){
          const auto evidence=campaign.economy_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,std::string_view stage){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,stage=="ready"?L"-economy-ready":stage=="end"?L"-economy-end":L"-economy-priority"));
              });
          std::cout<<"economy_inspection="<<evidence<<'\n';
        }
        if(options.logistics_check){
          const auto evidence=campaign.supply_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,bool end){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,end?L"-supply-end":L"-supply-ready"));
              });
          std::cout<<"supply_inspection="<<evidence<<'\n';
        }
        if(options.inspection_check){
          const auto evidence=campaign.system_inspection_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,std::string_view stage){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,stage=="known"?L"-inspection-known":stage=="end"?L"-inspection-end":L"-inspection-unknown"));
              });
          std::cout<<"system_inspection="<<evidence<<'\n';
        }
        if(options.surface_reload_smoke){
          const auto evidence=campaign.surface_inspection_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,bool capture_detail){
                window.draw(draw,capture_detail?std::optional{sidecar_path(*options.smoke_screenshot,L"-focus")}:std::nullopt);
              });
          std::cout<<"surface_inspection="<<evidence<<'\n';
        }
        if(options.surface_smoke||options.surface_reload_smoke)
          campaign.surface_building_smoke(window.drawable_width(),window.drawable_height(),[&](const DrawList& draw){
            window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-without-buildings"));
          });
        if(options.surface_reload_smoke){
          const auto evidence=campaign.surface_management_smoke(
              window.drawable_width(),window.drawable_height(),[&](const DrawList& draw,bool review){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,review?L"-management-review":L"-management-result"));
              });
          std::cout<<"surface_management="<<evidence<<'\n';
        }
        if(options.battle_smoke)
          campaign.battle_art_smoke(window.drawable_width(),window.drawable_height(),[&](const DrawList& draw){
            window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-without-ships"));
          });
        if(options.support_check)
          campaign.support_smoke(window.drawable_width(),window.drawable_height(),
              options.support_failure_check,[&](const DrawList& draw,bool capture_result){
                window.draw(draw,capture_result?
                    std::optional{sidecar_path(*options.smoke_screenshot,L"-support")}:std::nullopt);
              });
        if(options.diplomacy_smoke||options.diplomacy_reload_smoke){
          campaign.notification_smoke(window.drawable_width(),window.drawable_height(),
              options.diplomacy_reload_smoke,[&](const DrawList& draw,bool contact){
                window.draw(draw,sidecar_path(*options.smoke_screenshot,
                    contact?L"-events-contact":L"-events"));
              });
        }
        if(options.research_smoke){
          window.draw(campaign.research_inspector_end_smoke(
              window.drawable_width(),window.drawable_height()),
              sidecar_path(*options.smoke_screenshot,L"-inspector-end"));
          std::cout<<"research_inspector={\"final_line_visible\":true,\"graph_stationary\":true}\n";
        }
        if(options.audio_settings_check&&!options.new_game_smoke){
          const int width=window.drawable_width(),height=window.drawable_height();
          const auto route=[&](const InputEvent& event){
            InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
            input.events.push_back(event);input.pointer=event.position;
            if(!campaign.update(input,width,height,0.,false))throw std::runtime_error("Audio settings unexpectedly exited the campaign.");
            if(!campaign.paused_menu_visible())throw std::runtime_error("Audio settings closed the parent menu or resumed the campaign.");
          };
          stellar::native_audio::check_audio_settings(audio_settings,settings_path,width,height,"pause",
            [&]{settings_hub.close();const auto bounds=NativeUiLayout::for_viewport(width,height).settings_button;route({InputEventType::LeftPressed,center(bounds)});route({InputEventType::LeftPressed,center(stellar::native_settings::HubLayout::for_viewport(width,height).categories[1])});},
            route,[&]{window.draw(campaign.scene(width,height),sidecar_path(*options.smoke_screenshot,L"-audio-settings"));});
          settings_hub.close();
        }
        if(options.video_settings_check&&!options.new_game_smoke){
          const int width=window.drawable_width(),height=window.drawable_height();
          const auto route=[&](const InputEvent& event){
            InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
            input.events.push_back(event);input.pointer=event.position;
            if(!campaign.update(input,width,height,0.,false)||!campaign.paused_menu_visible())
              throw std::runtime_error("Video settings escaped the paused campaign menu.");
          };
          stellar::native_video_settings::check_video_settings(video_settings,video_settings_path,width,height,"pause",
            [&]{settings_hub.close();route({InputEventType::LeftPressed,center(NativeUiLayout::for_viewport(width,height).settings_button)});
                route({InputEventType::LeftPressed,center(stellar::native_settings::HubLayout::for_viewport(width,height).categories[2])});},
            route,[&](bool confirming){window.draw(campaign.scene(width,height),sidecar_path(*options.smoke_screenshot,confirming?L"-video-confirm":L"-video-settings"));});
          settings_hub.close();
        }
        if(options.audio_check){
          const auto state=audio.stats();
          if(!state.assets_loaded||state.failed||!state.music_started||state.music_start_count!=1||state.queued_music_bytes==0||(options.new_game_smoke&&(audio_boot_services==0||state.confirm_count==0||state.hover_count==0)))
            throw std::runtime_error("Native audio startup/playback proof failed: "+audio.failure_message());
          audio.stop();const auto stopped=audio.stats();
          if(!stopped.stopped||stopped.music_started||stopped.queued_music_bytes!=0)throw std::runtime_error("Native audio did not stop before window teardown.");
          if(options.voice_check){
            if(!voice_repeated||!voice_queue_bounded||voice_first_count!=1||state.voice_play_count!=voice_first_count||stopped.voice_active||stopped.queued_voice_bytes!=0)
              throw std::runtime_error("Scientist guidance did not coalesce, bound its queue, or stop cleanly.");
            std::cout<<"voice_check={\"available\":true,\"played\":"<<state.voice_play_count<<",\"unknown_denied\":true,\"overlap_prevented\":true,\"queue_bounded\":true,\"stopped\":true}\n";
          }
          std::cout<<"audio_hover_check={\"played\":"<<state.hover_count<<"}\n";
          std::cout<<"audio_check={\"assets_loaded\":true,\"music_starts\":"<<state.music_start_count<<",\"confirm_count\":"<<state.confirm_count<<",\"queued_music_bytes\":"<<state.queued_music_bytes<<",\"boot_services\":"<<audio_boot_services<<",\"stopped\":true}\n";
        }
        if(options.campaign_profile&&(!campaign_started||!campaign_mid_requested||!campaign_mid_saved||!campaign_advanced_after_mid||!campaign_final_requested||campaign.campaign_profile_notice()!=SessionNoticeKind::Saved||campaign.campaign_profile_speed()!=StrategicSpeed::Paused))throw std::runtime_error("Campaign profile did not complete active simulation, saves, and final UI pause.");
        if(!campaign.smoke_save_succeeded())
          throw std::runtime_error("Native session smoke did not complete its manual save.");
        if(options.fresh_progression_smoke||options.fresh_progression_reload_smoke)
          std::cout<<"fresh_progression="<<campaign.fresh_progression_smoke_status()<<'\n';
        if(options.first_exploration_mode)
          std::cout<<"first_exploration="<<campaign.first_exploration_smoke_status()<<'\n';
        if(options.settlement_completion_mode)
          std::cout<<"settlement_completion="<<campaign.settlement_completion_smoke_status()<<'\n';
        if(options.settlement_preparation_smoke)
          std::cout<<"settlement_preparation="<<campaign.settlement_preparation_smoke_status()<<'\n';
        if(options.earned_surface_mode)
          std::cout<<"earned_surface="<<campaign.earned_surface_smoke_status()<<'\n';
        if(options.earned_surface_expansion_mode)
          std::cout<<"earned_surface_expansion="<<campaign.earned_surface_expansion_smoke_status()<<'\n';
        if(options.first_survey_mode)
          std::cout<<"first_survey="<<campaign.first_survey_smoke_status()<<'\n';
        std::ranges::sort(frame_ms);
        const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);
        const auto p95=frame_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(frame_ms.size())*.95))-1];
        std::cout<<std::fixed<<std::setprecision(3)
                 <<"native-map smoke ok: gpu_driver="<<window.gpu_driver()
                 <<" presentation="<<window.presentation_mode()
                 <<" drawable="<<window.drawable_width()<<'x'<<window.drawable_height()
                 <<" systems="<<campaign.system_count()<<" frames="<<frames
                 <<" startup_ms="<<startup_ms
                 <<" artwork_pending_frames="<<artwork_pending_frames<<" artwork_prepare_max_ms="<<artwork_prepare_max_ms
                 <<" artwork_capture_wait_frames="<<artwork_wait_frames
                 <<" frame_mean_ms="<<total/static_cast<double>(frame_ms.size())
                 <<" frame_p95_ms="<<p95<<" image_uploads="<<window.image_upload_count()<<" save=ok screenshot="
                 <<utf8_path(*options.smoke_screenshot)<<" territory="<<campaign.territory_smoke_status();
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
        if(cold_profile)cold_profile->write(std::cout);
        if(options.campaign_profile)std::cout<<" campaign_profile={\"samples\":"<<*options.profile_frames<<",\"speed_multiplier\":8,\"before_days\":"<<std::setprecision(std::numeric_limits<double>::max_digits10)<<campaign_before_days<<",\"after_days\":"<<campaign_after_days<<",\"mid_save_day\":"<<campaign_mid_save_day<<",\"mid_save_completed_day\":"<<campaign_mid_save_completed_day<<",\"mid_save_completed\":"<<(campaign_mid_saved?"true":"false")<<",\"advanced_after_mid_save\":"<<(campaign_advanced_after_mid?"true":"false")<<",\"speed_input\":true,\"resume_input\":true,\"pause_input\":true,\"final_saved\":true}";
        if(options.research_smoke)
          std::cout<<" research="<<campaign.research_smoke_status();
        if(options.navigation_smoke)
          std::cout<<" navigation="<<campaign.navigation_smoke_status();
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
        if(options.surface_smoke||options.surface_reload_smoke)
          std::cout<<"\nsurface_art="<<campaign.surface_building_status(input.drawable_width,input.drawable_height);
        if(options.galaxy_art_smoke)
          std::cout<<" galaxy_art="<<campaign.galaxy_art_smoke_status();
        if(options.ship_art_smoke)
          std::cout<<" ship_art="<<campaign.ship_art_smoke_status();
        if(options.diplomacy_smoke||options.diplomacy_reload_smoke)
          std::cout<<" diplomacy="<<campaign.diplomacy_smoke_status();
        if(options.battle_smoke)std::cout<<" battle="<<campaign.battle_smoke_status(input.drawable_width,input.drawable_height);
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
    } // A successful New Game replaces the campaign only after activation.
    return 0;
  }catch(const std::exception &error){std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}
