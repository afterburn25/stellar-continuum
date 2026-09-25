#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/spatial_index.hpp>
#include <stellar/engine/spatial_index3d.hpp>
#include "native_phenomena_debug.hpp"
#include "native_background_debug.hpp"
#include "map_camera.hpp"
#include "native_audio_director.hpp"
#include "native_voice_caption.hpp"
#include "native_campaign_feedback.hpp"
#include "native_audio_settings.hpp"
#include "native_settings_hub.hpp"
#include "native_voice.hpp"
#include "native_voice_bridge.hpp"
#include "native_voice_playback.hpp"
#include "native_voice_settings.hpp"
#include "native_general_settings.hpp"
#include "native_accessibility_bridge.hpp"
#include "native_video_controller.hpp"
#include "native_video_settings_smoke.hpp"
#include "native_audio_settings_smoke.hpp"
#include "map_interaction.hpp"
#include "native_audio_settings.hpp"
#include "native_battle_workspace.hpp"

#include "native_notifications.hpp"
#include "native_support.hpp"
#include "native_galaxy_star_markers.hpp"
#include "native_stellar_art.hpp"
#include "native_stellar_eruptions.hpp"
#include "native_stellar_activity_panel.hpp"
#include "native_stellar_observation.hpp"
#include "native_navigation_art.hpp"
#include "native_galaxy_labels.hpp"
#include "native_inspection.hpp"
#include "native_economy.hpp"
#include "native_logistics.hpp"
#include "native_missions.hpp"
#include "native_overview.hpp"
#include "native_campaign_session.hpp"
#include "native_developer_simulation_panel.hpp"
#include "native_developer_diagnostics.hpp"
#include "native_developer_empire_monitor.hpp"
#include "native_developer_fault_capture.hpp"
#include "native_developer_celestial_index.hpp"
#include "native_developer_planet_index.hpp"
#include "native_giant_test_panel.hpp"
#include "native_chronicle.hpp"
#include "native_notification_events.hpp"
#include "native_support_service.hpp"
#include "../developer_diagnostic_report.hpp"
#include "native_battle_workspace.hpp"
#include "native_campaign_calendar.hpp"
#include "native_colony_controller.hpp"
#include <stellar/core/campaign_observation.hpp>
#include "native_colony_workspace.hpp"
#include "native_colony_roster.hpp"
#include "native_controlled_assets.hpp"
#include "native_settlement_mission_controller.hpp"
#include "native_settlement_workspace.hpp"
#include "native_surface_construction_controller.hpp"
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
#include "native_small_body_assets.hpp"
#include <stellar/core/small_body_commands.hpp>
#include "native_planet_disc_assets.hpp"
#include "native_planet_surface_assets.hpp"
#include "native_fleet_route_effects.hpp"
#include "native_ship_art_assets.hpp"
#include "native_battle_art.hpp"
#include "native_battle_sprites.hpp"
#include "native_ui_layout.hpp"
#include "native_command_hud.hpp"
#include "native_ui_style.hpp"
#include "native_ui_theme.hpp"
#include "native_startup_entry.hpp"
#include "native_galaxy_backdrop.hpp"
#include "native_territory_overlay.hpp"
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/exploration_advance.hpp>
#include "native_video_settings.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/build_version.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_save.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/survey_operations.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/save_preview.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/engine/runtime_directory_lease.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/input_actions.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/platform_services.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/replay.hpp>
#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
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

// Ends the profiler frame opened at the top of ClientApp::scene.
struct ProfileFrameGuard {
  ~ProfileFrameGuard() {
    (void)stellar::engine::Profiler::instance().end_frame();
  }
};

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
namespace native_battle_ui = stellar::native_battle_ui;
namespace native_developer = stellar::native_developer;
namespace native_inspection = stellar::native_inspection;
namespace native_economy = stellar::native_economy;
namespace native_logistics = stellar::native_logistics;
namespace native_missions = stellar::native_missions;
namespace native_notifications = stellar::native_notifications;
namespace native_overview = stellar::native_overview;
namespace native_support = stellar::native_support;
namespace native_video_settings = stellar::native_video_settings;
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

// UiRect → announcement bounds (UiRect converts to the optional implicitly,
// so call sites pass either).
[[nodiscard]] std::optional<stellar::engine::AnnouncementBounds>
announcement_bounds(std::optional<UiRect> rect) {
  if (!rect) return std::nullopt;
  return stellar::engine::AnnouncementBounds{rect->x, rect->y, rect->width,
                                             rect->height};
}

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
  int smoke_galaxy_card{};
  int smoke_system_count{250};
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
  bool new_game_smoke{},restart_smoke{},developer_smoke{},eruption_smoke{};
  bool new_game_restart_smoke{};
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
  bool save_path_overridden{},devtools{},dev_game{},smoke_full_exploration{};
  std::optional<int> profile_frames;
  // Deterministic replay: --record captures the GALAXY command stream and a
  // canonical-state hash at every save capture; --replay re-feeds the commands
  // under a fixed 60 Hz step and verifies the checkpoint hashes in order.
  std::filesystem::path record_path;
  std::filesystem::path replay_path;
  std::filesystem::path replay_info_path;
  std::optional<std::uint64_t> replay_until_tick;
  // --replay-exit: a scripted verification run exits once the recording
  // verifies (replay_verified, exit 0) instead of continuing the session.
  bool replay_exit{};
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
    else if(arg==L"--smoke-galaxy-card"&&i+1<argc) result.smoke_galaxy_card=std::stoi(argv[++i]);
    else if(arg==L"--smoke-system-count"&&i+1<argc) result.smoke_system_count=std::stoi(argv[++i]);
    else if(arg==L"--devtools") result.devtools=true;
    else if(arg==L"--dev-game"){result.devtools=true;result.dev_game=true;}
    else if(arg==L"--smoke-full-exploration")result.smoke_full_exploration=true;
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
    else if(arg==L"--developer-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.developer_smoke=true;result.windowed=true;}
    else if(arg==L"--eruption-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.developer_smoke=true;result.eruption_smoke=true;result.windowed=true;}
    else if(arg==L"--new-game-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_smoke=true;result.windowed=true;}
    else if(arg==L"--new-game-restart-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.new_game_restart_smoke=true;result.windowed=true;}
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
    else if(arg==L"--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg==L"--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.ship_art_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg==L"--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.diplomacy_reload_smoke=true;result.windowed=true;}
    else if((arg==L"--fresh-progression-smoke"||arg==L"--fresh-progression-reload-smoke")&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.fresh_progression_smoke=arg==L"--fresh-progression-smoke";result.fresh_progression_reload_smoke=arg==L"--fresh-progression-reload-smoke";result.windowed=true;}
    else if((arg==L"--first-exploration-smoke"||arg==L"--first-exploration-paused-smoke"||arg==L"--first-exploration-resume-smoke")&&i+1<argc){if(result.first_exploration_mode)throw std::invalid_argument("Choose one first exploration mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.first_exploration_mode=arg==L"--first-exploration-smoke"?Options::FirstExplorationMode::Depart:arg==L"--first-exploration-paused-smoke"?Options::FirstExplorationMode::Paused:Options::FirstExplorationMode::Resume;result.windowed=true;}
    else if((arg==L"--settlement-completion-smoke"||arg==L"--settlement-founded-smoke")&&i+1<argc){if(result.settlement_completion_mode)throw std::invalid_argument("Choose one settlement completion mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_completion_mode=arg==L"--settlement-completion-smoke"?Options::SettlementCompletionMode::Resume:Options::SettlementCompletionMode::Paused;result.windowed=true;}
    else if(arg==L"--settlement-preparation-smoke"&&i+1<argc){result.smoke_screenshot=std::filesystem::path(argv[++i]);result.settlement_preparation_smoke=true;result.windowed=true;}
    else if((arg==L"--first-survey-smoke"||arg==L"--first-survey-paused-smoke"||arg==L"--first-survey-resume-smoke")&&i+1<argc){if(result.first_survey_mode)throw std::invalid_argument("Choose one first survey mode.");result.smoke_screenshot=std::filesystem::path(argv[++i]);result.first_survey_mode=arg==L"--first-survey-smoke"?Options::FirstSurveyMode::Depart:arg==L"--first-survey-paused-smoke"?Options::FirstSurveyMode::Paused:Options::FirstSurveyMode::Resume;result.windowed=true;}
    else if(arg==L"--record"&&i+1<argc) result.record_path=argv[++i];
    else if(arg==L"--replay"&&i+1<argc) result.replay_path=argv[++i];
    else if(arg==L"--replay-until"&&i+1<argc) result.replay_until_tick=std::stoull(argv[++i]);
    else if(arg==L"--replay-info"&&i+1<argc) result.replay_info_path=argv[++i];
    else if(arg==L"--replay-exit") result.replay_exit=true;
#else
    const std::string arg=argv[i];
    if(arg=="--asset-root"&&i+1<argc) result.asset_root=argv[++i];
    else if(arg=="--seed"&&i+1<argc) result.seed=std::stoll(argv[++i]);
    else if(arg=="--smoke-galaxy-card"&&i+1<argc) result.smoke_galaxy_card=std::stoi(argv[++i]);
    else if(arg=="--smoke-system-count"&&i+1<argc) result.smoke_system_count=std::stoi(argv[++i]);
    else if(arg=="--devtools") result.devtools=true;
    else if(arg=="--dev-game"){result.devtools=true;result.dev_game=true;}
    else if(arg=="--smoke-full-exploration")result.smoke_full_exploration=true;
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
    else if(arg=="--developer-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.developer_smoke=true;result.windowed=true;}
    else if(arg=="--eruption-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.developer_smoke=true;result.eruption_smoke=true;result.windowed=true;}
    else if(arg=="--new-game-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_smoke=true;result.windowed=true;}
    else if(arg=="--new-game-restart-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.new_game_restart_smoke=true;result.windowed=true;}
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
    else if(arg=="--galaxy-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.galaxy_art_smoke=true;result.windowed=true;}
    else if(arg=="--ship-art-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.ship_art_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_smoke=true;result.windowed=true;}
    else if(arg=="--diplomacy-reload-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.diplomacy_reload_smoke=true;result.windowed=true;}
    else if((arg=="--fresh-progression-smoke"||arg=="--fresh-progression-reload-smoke")&&i+1<argc){result.smoke_screenshot=argv[++i];result.fresh_progression_smoke=arg=="--fresh-progression-smoke";result.fresh_progression_reload_smoke=arg=="--fresh-progression-reload-smoke";result.windowed=true;}
    else if((arg=="--first-exploration-smoke"||arg=="--first-exploration-paused-smoke"||arg=="--first-exploration-resume-smoke")&&i+1<argc){if(result.first_exploration_mode)throw std::invalid_argument("Choose one first exploration mode.");result.smoke_screenshot=argv[++i];result.first_exploration_mode=arg=="--first-exploration-smoke"?Options::FirstExplorationMode::Depart:arg=="--first-exploration-paused-smoke"?Options::FirstExplorationMode::Paused:Options::FirstExplorationMode::Resume;result.windowed=true;}
    else if((arg=="--settlement-completion-smoke"||arg=="--settlement-founded-smoke")&&i+1<argc){if(result.settlement_completion_mode)throw std::invalid_argument("Choose one settlement completion mode.");result.smoke_screenshot=argv[++i];result.settlement_completion_mode=arg=="--settlement-completion-smoke"?Options::SettlementCompletionMode::Resume:Options::SettlementCompletionMode::Paused;result.windowed=true;}
    else if(arg=="--settlement-preparation-smoke"&&i+1<argc){result.smoke_screenshot=argv[++i];result.settlement_preparation_smoke=true;result.windowed=true;}
    else if((arg=="--first-survey-smoke"||arg=="--first-survey-paused-smoke"||arg=="--first-survey-resume-smoke")&&i+1<argc){if(result.first_survey_mode)throw std::invalid_argument("Choose one first survey mode.");result.smoke_screenshot=argv[++i];result.first_survey_mode=arg=="--first-survey-smoke"?Options::FirstSurveyMode::Depart:arg=="--first-survey-paused-smoke"?Options::FirstSurveyMode::Paused:Options::FirstSurveyMode::Resume;result.windowed=true;}
    else if(arg=="--record"&&i+1<argc) result.record_path=argv[++i];
    else if(arg=="--replay"&&i+1<argc) result.replay_path=argv[++i];
    else if(arg=="--replay-until"&&i+1<argc) result.replay_until_tick=std::stoull(argv[++i]);
    else if(arg=="--replay-info"&&i+1<argc) result.replay_info_path=argv[++i];
    else if(arg=="--replay-exit") result.replay_exit=true;
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
  if(result.profile_frames&&!result.system_smoke&&!result.galaxy_art_smoke&&!result.campaign_profile)throw std::invalid_argument("--profile-frames requires a supported native profile smoke.");
  if(result.campaign_profile&&!result.profile_frames)throw std::invalid_argument("--campaign-profile requires --profile-frames.");
  if(result.campaign_profile&&result.menu_smoke)throw std::invalid_argument("--campaign-profile cannot be combined with --smoke.");
  if(static_cast<int>(result.research_smoke)+static_cast<int>(result.navigation_smoke)+static_cast<int>(result.fleet_smoke)+static_cast<int>(result.shipyard_smoke)+static_cast<int>(result.construction_smoke)+static_cast<int>(result.system_smoke)+static_cast<int>(result.system_travel_smoke)+static_cast<int>(result.system_travel_reload_smoke)+static_cast<int>(result.colony_smoke)+static_cast<int>(result.colony_reload_smoke)+static_cast<int>(result.settlement_smoke)+static_cast<int>(result.settlement_reload_smoke)+static_cast<int>(result.new_game_smoke)+static_cast<int>(result.restart_smoke)+static_cast<int>(result.galaxy_art_smoke)+static_cast<int>(result.ship_art_smoke)+static_cast<int>(result.diplomacy_smoke)+static_cast<int>(result.diplomacy_reload_smoke)+static_cast<int>(result.fresh_progression_smoke)+static_cast<int>(result.fresh_progression_reload_smoke)+static_cast<int>(result.first_exploration_mode.has_value())+static_cast<int>(result.first_survey_mode.has_value())+static_cast<int>(result.settlement_preparation_smoke)+static_cast<int>(result.settlement_completion_mode.has_value())+static_cast<int>(result.campaign_profile)+static_cast<int>(result.battle_smoke)>1)throw std::invalid_argument("Choose one native graphical smoke mode.");
  if(result.fresh_progression_smoke&&result.load)throw std::invalid_argument("--fresh-progression-smoke cannot be combined with --load.");
  if(result.fresh_progression_reload_smoke&&!result.load)throw std::invalid_argument("--fresh-progression-reload-smoke requires --load.");
  if((result.fresh_progression_smoke||result.fresh_progression_reload_smoke)&&result.seed!=115501)throw std::invalid_argument("Fresh progression smoke requires --seed 115501.");
  if(result.first_exploration_mode&&!result.load)throw std::invalid_argument("First exploration smoke requires --load with the earned first-ships save.");
  if(result.first_exploration_mode&&result.seed!=115501)throw std::invalid_argument("First exploration smoke requires --seed 115501.");
  if(result.settlement_preparation_smoke&&(!result.load||result.seed!=115501))throw std::invalid_argument("Settlement preparation smoke requires --load with the earned full survey save and --seed 115501.");
  if(result.settlement_completion_mode&&!result.load)throw std::invalid_argument("Settlement completion smoke requires --load with an authorized or founded expedition save.");
  if(result.first_survey_mode&&!result.load)throw std::invalid_argument("First survey smoke requires --load with the completed scout save.");
  if(result.first_survey_mode&&result.seed!=115501)throw std::invalid_argument("First survey smoke requires --seed 115501.");
  if(result.smoke_galaxy_card<0||result.smoke_galaxy_card>5)throw std::invalid_argument("Galaxy smoke card must be between zero and five.");
  if(!stellar::core::supported_full_galaxy_system_count(result.smoke_system_count))throw std::invalid_argument("Unsupported smoke galaxy size.");
  if(result.new_game_smoke&&result.load)throw std::invalid_argument("--new-game-smoke cannot be combined with --load.");
  if(result.fleet_smoke&&!result.load)throw std::invalid_argument("--fleet-smoke requires --load with a player campaign fixture.");
  if(result.ship_art_smoke&&!result.load)throw std::invalid_argument("--ship-art-smoke requires --load with a player campaign fixture.");
  if((result.diplomacy_smoke||result.diplomacy_reload_smoke)&&!result.load)throw std::invalid_argument("Diplomacy smoke requires --load with an isolated diplomatic campaign fixture.");
  if(result.system_travel_smoke&&!result.load)throw std::invalid_argument("--system-travel-smoke requires --load with a routed player fleet fixture.");
  if(result.system_travel_reload_smoke&&!result.load)throw std::invalid_argument("--system-travel-reload-smoke requires --load with the paused system travel save.");
  if(result.colony_reload_smoke&&!result.load)throw std::invalid_argument("--colony-reload-smoke requires --load with the paused colony save.");
  if((result.settlement_smoke||result.settlement_reload_smoke)&&!result.load)throw std::invalid_argument("Settlement smoke requires --load with a test-authored funded populated settlement vessel.");
  if(result.window_width<640||result.window_width>3840||result.window_height<360||result.window_height>2160)throw std::invalid_argument("Native window dimensions are out of range.");
  if(result.dev_game&&!result.save_path_overridden)
    result.save_path=default_native_campaign_save_path().parent_path()/"developer"/"campaign.dev17.json";
  if(!result.record_path.empty()&&!result.replay_path.empty())throw std::invalid_argument("--record and --replay are mutually exclusive.");
  if(result.replay_until_tick&&result.replay_path.empty())throw std::invalid_argument("--replay-until requires --replay.");
  if(result.replay_exit&&result.replay_path.empty())throw std::invalid_argument("--replay-exit requires --replay.");
  // Replay feeds commands only once a campaign session exists; without a
  // session source the run lands on the interactive startup screen and the
  // recorded stream can never consume — fail fast instead of idling.
  if(!result.replay_path.empty()&&!result.load&&!result.smoke_screenshot)
    throw std::invalid_argument("--replay requires --load or a smoke session; the interactive startup screen cannot host a deterministic replay.");
  if(!result.replay_info_path.empty()&&(!result.replay_path.empty()||!result.record_path.empty()))throw std::invalid_argument("--replay-info is a standalone inspection flag.");
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
    NativeCampaignSessionDependencies dependencies;
    dependencies.developer_session=options.dev_game;
    return NativeCampaignSession::load_startup(research_root,options.save_path,STELLAR_GAME_VERSION,
      [](const PlayerCampaignRestorationProgress &progress){std::cerr<<"Loading: "<<static_cast<int>(progress.fraction*100.)<<"% "<<progress.status<<'\n';},std::move(dependencies));
  }
  return NativeCampaignSession::create_fresh(
    IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      seed_persistable_fresh_campaign(options.seed,
        load_nearby_catalog(asset_root/"Data/astronomy/hyg-nearby-500-v1.json"),
        {utc_timestamp(),500,6,1,"terran_baseline",StellarPopulationOptions{}})),
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


// Deterministic replay state, owned by main() so one stream spans the
// per-session NativeCampaign rebuilds that mode switches and new games cause.
struct ReplayState {
  std::optional<stellar::engine::ReplayRecorder> recorder;
  std::optional<stellar::engine::ReplayRecorder> recording;
  std::size_t command_cursor{};
  std::size_t checkpoint_cursor{};
  std::uint64_t verified_checkpoints{};
  std::string divergence;
  // <recording>.expected/<tick>.json sidecars: record mode writes the
  // canonical capture per checkpoint so a replay divergence can leaf-diff
  // the expected document instead of stopping at the section hash.
  std::filesystem::path expected_directory;
  // --replay-until: once the simulated tick reaches the stop, dump the
  // canonical document so it can be leaf-diffed against an expected
  // sidecar — the bisect companion to checkpoint divergence.
  std::optional<std::uint64_t> stop_at_tick;
  std::filesystem::path stop_dump_path;
  // Stall detection for --replay-until: frames since the simulated tick
  // last advanced. A paused campaign or exhausted command stream can never
  // reach the stop — the update loop reports it instead of hanging.
  std::uint64_t last_tick{std::numeric_limits<std::uint64_t>::max()};
  std::uint32_t stalled_frames{};
  // One-shot "recording fully consumed and verified" report — a scripted
  // --replay run greps replay_verified={...} instead of watching for the
  // absence of a divergence over a timeout.
  bool completion_reported{};
  // --replay-exit: a scripted verification run exits once the recording
  // verifies (exit 0 after replay_verified) instead of continuing the
  // session, and a run that stalls with work pending reports it rather
  // than hanging. The trackers record the last tick/cursor positions so
  // "no progress" is measurable across frames.
  bool exit_on_completion{};
  std::uint64_t verification_last_tick{std::numeric_limits<std::uint64_t>::max()};
  std::size_t verification_last_command{}, verification_last_checkpoint{};
  std::uint32_t verification_stalled_frames{};
  // pointer_button commands dequeue into real InputEvents merged into the
  // next update's event stream (ahead of live events) so hit-testing and
  // dispatch see exactly what the recorded session saw — not a synthetic
  // binding. pointer_held bounds move journaling on the record side to
  // drags, where accumulated motion decides drag-vs-click.
  std::vector<InputEvent> injected_events;
  std::uint32_t pointer_held{};
};

// Writes the recording on scope exit, including early returns and failures.
struct ReplayFileFlush {
  const ReplayState *state{};
  std::filesystem::path path;
  ~ReplayFileFlush() {
    if (!state || !state->recorder || path.empty()) return;
    try {
      if (state->recorder->truncated())
        std::cerr << "Stellar Continuum native client: replay recording hit "
                     "its memory budget — the file holds a truncated prefix.\n";
      const auto bytes = state->recorder->serialize();
      stellar::engine::write_file_atomically(
          path, std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
    } catch (const std::exception &error) {
      std::cerr << "Stellar Continuum native client: replay recording write "
                   "failed: " << error.what() << '\n';
    }
  }
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
                 stellar::native_audio::NativeVoiceSettings* voice_settings=nullptr,
                 stellar::native_client::NativeAccessibilityBridge* accessibility_bridge=nullptr)
      : session_(std::move(session)),
        navigation_art_(std::filesystem::absolute(asset_root)),
        research_art_(std::filesystem::absolute(asset_root)),
        stellar_art_(std::filesystem::absolute(asset_root)),
        eruption_art_(std::filesystem::absolute(asset_root)),
        galaxy_assets_(std::filesystem::absolute(asset_root)),
        galaxy_backdrop_(galaxy_assets_),
        planet_discs_(std::filesystem::absolute(asset_root)/"assets/visual"),
        ship_art_(std::filesystem::absolute(asset_root)),
        battle_sprites_(std::filesystem::absolute(asset_root)),
        asset_root_(std::filesystem::absolute(asset_root)),
        text_measurer_(text_measurer),
        system_workspace_([this](const SystemBodyAppearance &appearance){return planet_discs_.request_image(appearance);},text_measurer) {
    session_->frame().clock().set_maximum_multiplier(4.);
    stellar_art_.use_queue(image_preparation_);
    eruption_art_.use_queue(image_preparation_);
    system_workspace_.set_stellar_activity([this](DrawList& out,Point p,float radius,int sid,int component,UiRect clip){
      const auto& cache=session_->cache();if(const auto s=cache.systems_by_id.find(sid);s!=cache.systems_by_id.end()){
        const auto physics=stellar_host_physics(*s->second,component);const auto art=stellar::native_stellar::observed_stellar_artwork(SystemSurveyLevel::fully_surveyed,physics,{});
        if(art)if(const auto surface=stellar_art_.photosphere(std::string(art->id),p,radius))eruption_art_.append(out,surface->center,surface->radius,*s->second,component,clip);
      }
    });
    system_workspace_.set_stellar_art([this](DrawList& out,Point p,float radius,const stellar::native_stellar::ObservedStellarArtwork& art,double seconds,UiRect clip){
      stellar_art_.append(out,p,radius,std::string(art.id),seconds,clip);
    });
    research_workspace_.set_text_measurer(text_measurer_);
    shipyard_workspace_.set_text_measurer(text_measurer_);
    shipyard_workspace_.bind_preferences(session_->save_path().parent_path()/"shipyard-favorites.txt");
    research_workspace_.set_artwork_resolver([this](std::string_view id, bool portrait) {
      return research_art_.image(id, portrait);
    });
    inspection_card_.set_text_measurer(text_measurer_);
    supply_workspace_.set_text_measurer(text_measurer_);
    economy_workspace_.set_text_measurer(text_measurer_);
    notification_view_.set_text_measurer(text_measurer_);
    chronicle_view_.set_text_measurer(text_measurer_);
    chronicle_view_.set_actor_name_resolver([this](std::uint64_t id){
      if(!session_)return std::string{};
      const auto& world=session_->frame().runtime().world().campaign();
      const auto it=std::ranges::find(world.civilizations,static_cast<int>(id),&Civilization::id);
      return it==world.civilizations.end()?std::string{}:it->name;
    });
    chronicle_view_.set_campaign_day_source([this]{
      return session_?session_->frame().clock().simulation_days():0.;
    });
    seed_notifications();
    galaxy_assets_.use_background_preparation(image_preparation_);
    phenomena_.use_queue(image_preparation_);
    phenomena_.use_assets(asset_root_);
    system_background_.configure(asset_root_,image_preparation_);
    planet_discs_.use_background_preparation(image_preparation_);
    small_body_assets_.configure(asset_root_,image_preparation_);
    system_workspace_.set_small_body_images([this](SmallBodyAssetPool pool,int variant){return small_body_assets_.image(pool,variant);});
    colony_workspace_.set_text_measurer(text_measurer);
    colony_workspace_.planetary().set_art([this](bool buildings){
      auto& cached=planetary_art_[buildings?1:0];
      if(!cached)cached=decode_rgba_image(asset_root_/"assets/visual/planetary"/(buildings?"building-portraits-v1.png":"colony-panorama-v1.png"));
      return cached;
    });
    colony_workspace_.planetary().set_action_art([this](int index){const std::array actions={UiAction::Construction,UiAction::Economy,UiAction::Inspect,UiAction::Research,UiAction::Diplomacy};return navigation_art_.image(actions[index]);});
    colony_workspace_.planetary().set_globe_maps([this](std::string_view key,int layer)->std::shared_ptr<const RgbaImage>{
      const auto index=stellar::native_system_ui::planet_surface_asset_index(key,layer);if(!index)return {};
      auto& image=planetary_globe_maps_[*index];if(image)return image;
      auto source=decode_rgba_image(asset_root_/"assets/visual/sol"/stellar::native_system_ui::planet_surface_assets[*index].filename);
      image=NativePlanetGlobe::prepare_map(std::move(source),layer);
      return image;
    });
    planet_material_cache_.set_root(asset_root_/"assets/visual");
    const stellar::native_planets::MaterialProvider planet_provider=[this](const stellar::core::PlanetAppearance& a,int width){return planet_material_cache_.request(a,width);};
    system_workspace_.set_planet_materials(planet_provider);
    colony_workspace_.planetary().globe().set_materials(planet_provider);
    system_workspace_.use_background_preparation(image_preparation_);

    refresh_knowledge();
    fit_camera(width,height);
    bind_galaxy_backdrop(width,height);
    focus_home_map(width,height);
    refresh_fleets(true);
    audio_confirm_=std::move(audio_confirm);
    audio_settings_=audio_settings;
    video_settings_=video_settings;
    general_settings_=general_settings;
    if(general_settings_){const auto& preferences=general_settings_->saved();assets_.set_preferences({preferences.asset_categories_collapsed,preferences.assets_hidden});
      assets_.set_persist([this](const stellar::native_assets::Preferences& p){auto prefs=general_settings_->saved();prefs.asset_categories_collapsed=p.collapsed;prefs.assets_hidden=p.hidden;return general_settings_->save(prefs);});}
    settings_hub_=settings_hub;voice_settings_=voice_settings;
    accessibility_bridge_=accessibility_bridge;
    presentation_audio_=presentation_audio;
    menu_hover_feedback_.set_callback([this]{if(presentation_audio_)presentation_audio_->hover();});
    configure_input_actions();
    configure_voice_pipeline();
  }

  // Gamepad camera axes are Axis1D actions in a separate context — the
  // Controls view lists them as rebindable axis rows (stick deflection or
  // wheel capture). Saves written before the context existed lack it, so
  // load_user_bindings re-registers the defaults when the loaded map does
  // not define it. SDL axis ids: 0=left X, 1=left Y, 3=right Y.
  static constexpr std::string_view kGalaxyPadContext=R"json({
    "contexts":[{"name":"GALAXY_PAD","exclusive":false,"actions":[
      {"name":"map_pan_x","type":"Axis1D","bindings":[{"kind":"GamepadAxis","code":0}]},
      {"name":"map_pan_y","type":"Axis1D","bindings":[{"kind":"GamepadAxis","code":1}]},
      {"name":"map_zoom","type":"Axis1D","bindings":[{"kind":"GamepadAxis","code":3}]}
    ]}]}
  )json";
  // Reference Main.cs keyboard shortcuts, expressed as a data-driven GALAXY
  // input context (engine InputActions): Space pauses, 1-5 select strategic
  // speeds (5 is the Developer-only Demo rate), T/R/C/B cycle and start the
  // research/construction candidates, N starts a new campaign and F6 saves.
  void configure_input_actions(){
    static constexpr std::string_view kGalaxyContext=R"json({
      "contexts":[{"name":"GALAXY","exclusive":false,"actions":[
        {"name":"toggle_pause","type":"Button","bindings":[{"kind":"KeyPress","code":32}]},
        {"name":"speed_normal","type":"Button","bindings":[{"kind":"KeyPress","code":49}]},
        {"name":"speed_fast","type":"Button","bindings":[{"kind":"KeyPress","code":50}]},
        {"name":"speed_very_fast","type":"Button","bindings":[{"kind":"KeyPress","code":51}]},
        {"name":"speed_maximum","type":"Button","bindings":[{"kind":"KeyPress","code":52}]},
        {"name":"speed_demo","type":"Button","bindings":[{"kind":"KeyPress","code":53}]},
        {"name":"cycle_research","type":"Button","bindings":[{"kind":"KeyPress","code":116},{"kind":"KeyPress","code":84}]},
        {"name":"start_research","type":"Button","bindings":[{"kind":"KeyPress","code":114},{"kind":"KeyPress","code":82}]},
        {"name":"cycle_construction","type":"Button","bindings":[{"kind":"KeyPress","code":99},{"kind":"KeyPress","code":67}]},
        {"name":"start_construction","type":"Button","bindings":[{"kind":"KeyPress","code":98},{"kind":"KeyPress","code":66}]},
        {"name":"new_campaign","type":"Button","bindings":[{"kind":"KeyPress","code":110},{"kind":"KeyPress","code":78}]},
        {"name":"quicksave","type":"Button","bindings":[{"kind":"KeyPress","code":1073741887}]}
      ]}]}
    )json";
    std::string error;
    if(input_mapper_.load_contexts(kGalaxyContext,&error)
        &&input_mapper_.load_contexts(kGalaxyPadContext,&error)){
      input_mapper_.push_context("GALAXY_PAD");
      input_mapper_.push_context("GALAXY");
    }
    else SDL_Log("GALAXY input context failed to load: %s",error.c_str());
  }

  // User-customized bindings overlay: replaces the registered GALAXY context
  // wholesale (the stack resolves by name, so no re-push needed). Returns
  // true when a saved map loaded; a rejected file keeps the defaults.
  bool load_user_bindings(const std::filesystem::path &path){
    std::error_code ec;
    if(!std::filesystem::exists(path,ec))return false;
    std::ifstream in(path);std::ostringstream contents;contents<<in.rdbuf();
    std::string error;
    if(input_mapper_.load_contexts(contents.str(),&error)){
      // Saved maps written before pad camera axes existed carry no
      // GALAXY_PAD context — inject the defaults; a saved map that defines
      // it already holds the user's own axis bindings.
      if(!input_mapper_.context("GALAXY_PAD")){
        std::string pad_error;
        (void)input_mapper_.load_contexts(kGalaxyPadContext,&pad_error);
      }
      return true;
    }
    SDL_Log("Saved input bindings rejected: %s",error.c_str());return false;
  }
  // Live mapper — the settings hub's Controls view binds against it.
  stellar::engine::InputMapper& input_mapper()noexcept{return input_mapper_;}

  // Maps the persisted UI voice preferences onto the playback controller's
  // settings record (stellar::native_voice::NativeVoiceSettings).
  [[nodiscard]] static stellar::native_voice::NativeVoiceSettings
  voice_pipeline_settings_for(const stellar::native_audio::VoicePreferences &preferences){
    stellar::native_voice::NativeVoiceSettings settings;
    settings.enable_voices=preferences.enabled;
    settings.volume=preferences.volume;
    settings.subtitles=preferences.subtitles;
    settings.subtitle_size=preferences.subtitle_size;
    settings.opacity=preferences.subtitle_background_opacity;
    settings.speaker_labels=preferences.speaker_labels;
    settings.no_interruptions=preferences.no_interruptions;
    settings.comms_intensity=1.f-preferences.communication_filter;
    settings.frequency=static_cast<stellar::native_voice::VoiceFrequency>(
        static_cast<int>(preferences.frequency));
    return settings.sanitized();
  }

  [[nodiscard]] stellar::native_voice::NativeVoiceSettings voice_pipeline_settings() const {
    return voice_pipeline_settings_for(
        voice_settings_?voice_settings_->saved_values():stellar::native_audio::VoicePreferences{});
  }

  // Wires the event-driven gameplay voice pipeline (bridge -> router ->
  // playback -> the director's voice channel). Every stage degrades silently:
  // missing catalogs or an unavailable speech backend leave the pipeline
  // subtitle-only or absent rather than failing the client.
  void configure_voice_pipeline(){
    namespace voice=stellar::native_voice;
    if(!presentation_audio_)return;
    const auto voice_root=asset_root_/"Data/voice_profiles";
    try{
      voice_profiles_=voice::NativeVoiceProfileRegistry::load(voice_root/"human.json");
      if(voice_profiles_.size()==0)return;
      voice_resolver_=voice::NativeCharacterVoiceResolver::load(&voice_profiles_,voice_root/"roles.json");
      voice_cache_.emplace(default_native_campaign_save_path().parent_path()/"voice-cache");
      voice_playback_.emplace(voice_pipeline_settings(),&voice_profiles_,&*voice_resolver_,&*voice_cache_);
      voice_playback_->attach_backend(voice::create_offline_speech_backend());
      voice_playback_->bind(
          [this](const std::filesystem::path& path)->voice::NativeVoicePlayback::Stream{
            const auto resolved=path.is_absolute()?path:asset_root_/path;
            auto decoded=stellar::native_audio::decode_audio_file(resolved);
            if(!decoded)return nullptr;
            return std::make_shared<const stellar::native_audio::PcmData>(std::move(*decoded));
          },
          [this](voice::NativeVoicePlayback::Stream stream,double){
            if(presentation_audio_)presentation_audio_->play_dialogue_pcm(std::move(stream));},
          [this]{if(presentation_audio_)presentation_audio_->stop_voice();});
      voice_router_=voice::NativeVoiceRouter::from_file(voice_root/"events.json",
          [this](voice::NativeSpeechRequest request){if(voice_playback_)voice_playback_->speak(std::move(request));},
          &*voice_resolver_);
      voice_bridge_.emplace(*voice_router_);
      voice_bridge_->reset(session_->frame().runtime());
      support_.record("voice","Gameplay voice pipeline armed: "+
          std::to_string(voice_profiles_.size())+" profiles, "+
          voice_playback_->backend_status());
    }catch(const std::exception& error){
      support_.record("voice",std::string{"Gameplay voice pipeline unavailable: "}+error.what());
      voice_bridge_.reset();voice_router_.reset();voice_playback_.reset();
      voice_resolver_.reset();voice_cache_.reset();
    }
  }

  [[nodiscard]] stellar::native_voice::NativeVoicePlayback* voice_playback(){
    return voice_playback_?&*voice_playback_:nullptr;
  }

  // Hands the live locale catalog to the campaign surface and the voice
  // pipeline: menu labels translate directly, minted localization_keys
  // resolve when a table ships translated cue text.
  void set_locale(const stellar::engine::LocalizationTable &table){
    locale_=&table;
    diplomacy_workspace_.set_localization(&table);
    research_workspace_.set_localization(&table);
    fleet_workspace_.set_localization(&table);
    construction_workspace_.set_localization(&table);
    inspection_card_.set_localization(&table);
    economy_workspace_.set_localization(&table);
    supply_workspace_.set_localization(&table);
    colony_roster_.set_localization(&table);
    colony_workspace_.set_localization(&table);
    system_workspace_.set_localization(&table);
    battle_workspace_.set_localization(&table);
    shipyard_workspace_.set_localization(&table);
    notification_view_.set_localization(&table);
    chronicle_view_.set_localization(&table);
    assets_.set_localization(&table);
    settlement_workspace_.set_localization(&table);
    economy_controller_.set_localization(&table);
    research_controller_.set_localization(&table);
    phenomena_.set_localization(&table);
    mission_view_.set_localization(&table);
    feedback_.set_localization(&table);
    supply_controller_.set_localization(&table);
    system_travel_controller_.set_localization(&table);
    system_controller_.set_localization(&table);
    fleet_controller_.set_localization(&table);
    outpost_freight_controller_.set_localization(&table);
    surface_controller_.set_localization(&table);
    shipyard_controller_.set_localization(&table);
    diplomacy_controller_.set_localization(&table);
    settlement_controller_.set_localization(&table);
    construction_controller_.set_localization(&table);
    colony_controller_.set_localization(&table);
    if(voice_playback_)voice_playback_->set_localization(&table);
  }

  // Deterministic replay (engine ReplayRecorder adoption): --record stores
  // each dispatched GALAXY keypress and a canonical-state hash at every save
  // capture; --replay re-feeds the keypresses under the fixed step that main()
  // applies and verifies the recorded hashes in order. Verification failures
  // surface as a thrown divergence after advance(), never inside the writer.
  void attach_replay(ReplayState *state){
    replay_=state;
    install_replay_observer();
  }

  [[nodiscard]] std::uint64_t replay_tick()const{
    return static_cast<std::uint64_t>(std::llround(
        session_->frame().clock().simulation_days()*1000.));
  }

  void install_replay_observer(){
    if(!replay_||!session_||(!replay_->recorder&&!replay_->recording))return;
    session_->set_save_capture_observer(
        [this](double day,const PlayerCampaignPayloadV17Dto &payload){
          if(!replay_||!replay_->divergence.empty())return;
          const auto tick=static_cast<std::uint64_t>(std::llround(day*1000.));
          // The canonical payload carries two wall-clock provenance fields —
          // SavedAtUtc and the campaign's CreatedAtUtc generation stamp. Strip
          // both so checkpoint hashes compare simulation state only.
          auto document=encode_player_campaign_v17_document(payload);
          document.erase("SavedAtUtc");
          if(const auto galaxy=document.find("Galaxy");galaxy!=document.end())
            if(const auto meta=galaxy->find("GenerationMetadata");meta!=galaxy->end())
              meta->erase("CreatedAtUtc");
          // Per-section checkpoints: a divergence names the subsystem
          // ("save:World.Fleets"), not just "state differs at tick N".
          const auto actual=stellar::engine::document_section_checkpoints(
              tick,document,"save");
          if(replay_->recorder){
            for(const auto &checkpoint:actual)
              replay_->recorder->checkpoint(checkpoint.tick,checkpoint.hash,
                                            checkpoint.label);
            // Retain the expected-side document so a later replay can
            // leaf-diff the divergence instead of needing a hand capture.
            if(!replay_->expected_directory.empty()){
              const auto expected_path=replay_->expected_directory/
                  (std::to_string(tick)+".json");
              std::error_code ec;
              std::filesystem::create_directories(expected_path.parent_path(),ec);
              if(std::ofstream out{expected_path,std::ios::binary|std::ios::trunc};out)
                out<<document.dump(2);
            }
            return;
          }
          auto cursor=replay_->checkpoint_cursor;
          const auto result=stellar::engine::verify_checkpoint_sequence(
              replay_->recording->checkpoints(),cursor,actual);
          replay_->checkpoint_cursor=cursor;
          replay_->verified_checkpoints+=result.verified;
          if(!result.divergence.empty()){
            replay_->divergence=result.divergence;
            // Dump the diverging canonical document next to the save —
            // diffing it against the original capture names the leaf.
            const auto dump_path=session_->save_path().parent_path()/
                ("replay-divergence-"+std::to_string(tick)+".json");
            if(std::ofstream out{dump_path,std::ios::binary|std::ios::trunc};
               out){
              out<<document.dump(2);
              replay_->divergence+=" (actual state dumped to "+
                  dump_path.generic_string()+")";
            }
            // Leaf-diff against the recorded capture when it was retained.
            const auto expected_path=replay_->expected_directory/
                (std::to_string(tick)+".json");
            if(std::ifstream in{expected_path,std::ios::binary};in){
              std::ostringstream contents;contents<<in.rdbuf();
              try{
                const auto expected_doc=
                    nlohmann::ordered_json::parse(contents.str());
                const auto leaves=stellar::engine::document_leaf_diff(
                    expected_doc,document);
                if(!leaves.empty()){
                  const auto diff_path=session_->save_path().parent_path()/
                      ("replay-divergence-"+std::to_string(tick)+".diff.txt");
                  std::ostringstream report;
                  for(const auto&leaf:leaves)
                    report<<leaf.path<<"\n  expected: "<<leaf.expected
                          <<"\n  actual:   "<<leaf.actual<<'\n';
                  if(std::ofstream out{diff_path,std::ios::binary|std::ios::trunc};out){
                    out<<report.str();
                    replay_->divergence+=" (first leaf: "+leaves.front().path+
                        "; full diff in "+diff_path.generic_string()+")";
                  }
                }
              }catch(const std::exception&){
                // A corrupt expected sidecar still leaves the section name.
              }
            }
          }
        });
  }

  [[nodiscard]] std::string_view first_galaxy_action_pressed()const{
    static constexpr std::string_view actions[]={
        "toggle_pause","speed_normal","speed_fast","speed_very_fast",
        "speed_maximum","speed_demo","cycle_research","start_research",
        "cycle_construction","start_construction","new_campaign","quicksave"};
    for(const auto name:actions)
      if(input_mapper_.just_pressed(name))return name;
    return {};
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
          tr("STATUS_NO_RESEARCH_CHOICES",
             "No research choices are currently available. A construction "
             "prerequisite may be missing."));
      return;
    }
    research_candidate_index_=
        (research_candidate_index_+1)%
        static_cast<int>(candidates.size());
    session_->publish_status(
        trf("STATUS_RESEARCH_CANDIDATE",
            {candidates[research_candidate_index_]->display_name},
            "Research candidate: {0}"));
  }
  void start_research_candidate(){
    const auto view=research_controller_.build(
        session_->frame(),session_->cache().generation,{});
    const auto candidates=research_candidates(view);
    if(candidates.empty()){
      research_candidate_index_=0;
      session_->publish_status(
          tr("STATUS_NO_RESEARCH_SELECTED",
             "No available research project selected. Check construction "
             "prerequisites."));
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
      publish_notification("Research",outcome.message);
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
          tr("STATUS_CONSTRUCTION_IN_PROGRESS",
             "Complete the current construction project before selecting "
             "another."));
      return;
    }
    const auto candidates=construction_candidates(view);
    if(candidates.empty()){
      construction_candidate_index_=0;
      session_->publish_status(
          tr("STATUS_NO_CONSTRUCTION_CHOICES",
             "No construction choices are currently available. Research may be "
             "required."));
      return;
    }
    construction_candidate_index_=
        (construction_candidate_index_+1)%
        static_cast<int>(candidates.size());
    session_->publish_status(
        trf("STATUS_CONSTRUCTION_CANDIDATE",
            {candidates[construction_candidate_index_]->name},
            "Construction candidate: {0}"));
  }
  void start_construction_candidate(){
    const auto view=construction_controller_.build(
        session_->frame(),session_->cache().generation);
    if(construction_project_active(view)){
      construction_candidate_index_=0;
      session_->publish_status(
          tr("STATUS_NO_CONSTRUCTION_SELECTED",
             "No available construction project selected."));
      return;
    }
    const auto candidates=construction_candidates(view);
    if(candidates.empty()){
      construction_candidate_index_=0;
      session_->publish_status(
          tr("STATUS_NO_CONSTRUCTION_SELECTED",
             "No available construction project selected."));
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
      publish_notification("Construction",outcome.message);
      construction_candidate_index_=0;
    }
    if(construction_workspace_.visible())refresh_construction(true);
  }

  void dispatch_galaxy_action(std::string_view action,int width,int height){
    auto &clock=session_->frame().clock();
    if(action=="toggle_pause"){
      if(clock.speed()==StrategicSpeed::Paused)clock.resume();
      else clock.set_speed(StrategicSpeed::Paused);
    }
    else if(action=="speed_normal")clock.set_speed(StrategicSpeed::Normal);
    else if(action=="speed_fast")clock.set_speed(StrategicSpeed::Fast);
    else if(action=="speed_very_fast")clock.set_speed(StrategicSpeed::VeryFast);
    else if(action=="speed_maximum")clock.set_speed(StrategicSpeed::Maximum);
    // Reference UiResumeAtSpeed: the Demo rate is Developer-only; Player mode
    // coerces the request to Normal.
    else if(action=="speed_demo")clock.set_speed(developer_session()?StrategicSpeed::Demo:StrategicSpeed::Normal);
    else if(action=="cycle_research")cycle_research_candidate();
    else if(action=="start_research")start_research_candidate();
    else if(action=="cycle_construction")cycle_construction_candidate();
    else if(action=="start_construction")start_construction_candidate();
    else if(action=="new_campaign"){(void)session_->request_new_campaign();}
    else if(action=="quicksave")session_->request_save();
    (void)width;(void)height;
  }

  [[nodiscard]] bool developer_session(){return session_->frame().runtime().world().campaign().developer_provenance.has_value();}
  void developer_shortcut(){
    gesture_.capture_for_ui();
    if(developer_empires_.visible()){developer_empires_.close();return;}
    if(developer_diagnostics_.visible()){developer_diagnostics_.close();return;}
    if(stellar_activity_panel_.visible()){stellar_activity_panel_.close();return;}
    if(giant_test_panel_.visible()){giant_test_panel_.close();return;}
    if(developer_planet_index_.visible()){developer_planet_index_.close();return;}
    if(developer_index_.visible()){developer_index_.close();return;}
    if(developer_session())developer_panel_.toggle();else (void)session_->request_new_campaign();
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
  void cancel_new_game(){session_->cancel_new_campaign();menu_focus_=-1;gesture_.capture_for_ui();}


  void prepare_developer_smoke(){
    prepare_smoke_ui();if(menu_)toggle_menu();developer_panel_.toggle();session_->frame().set_developer_speed(25);
  }
  void focus_stellar_activity(int width,int height){
    const auto id=stellar_activity_panel_.take_focus();if(!id)return;
    if(menu_)toggle_menu();
    colony_roster_.close();economy_workspace_.close();supply_workspace_.close();research_workspace_.close();
    shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();colony_workspace_.close();
    settlement_workspace_.clear();colony_entry_view_.reset();system_workspace_.close();
    if(const auto it=session_->cache().systems_by_id.find(*id);it!=session_->cache().systems_by_id.end()){
      camera_.center={it->second->position.x,it->second->position.y};camera_.pixels_per_world=16000.;
      selected_id_=*id;constrain_galaxy_camera(width,height);refresh_inspection();
    }
  }
  void capture_developer_index_smoke(int width,int height,const std::function<void(const DrawList&,std::wstring_view)> &draw){
    const auto find=[&](std::string_view label){
      for(const auto &command:scene(width,height).overlay)if(const auto *text=std::get_if<Text>(&command);text&&text->value.starts_with(label)&&text->clip){
        const auto &r=*text->clip;return Point{r.x+r.width*.5f,r.y+std::min(10.f,r.height*.5f)};
      }
      throw std::runtime_error("Developer capture could not find "+std::string(label));
    };
    const auto route=[&](std::vector<InputEvent> events){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.events=std::move(events);input.pointer=input.events.back().position;(void)update(input,width,height,0.,false);};
    const auto click=[&](Point p){route({{InputEventType::LeftPressed,p},{InputEventType::LeftReleased,p}});};
    const auto before=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    click(find("EXPORT DIAGNOSTIC BUNDLE"));
    const auto export_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(support_.busy()&&std::chrono::steady_clock::now()<export_deadline){
      (void)support_.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(support_.state()!=stellar::native_support::SupportExportState::Succeeded||!std::filesystem::is_regular_file(support_.result()))
      throw std::runtime_error("Developer button export failed: "+support_.error());
    std::cout<<"developer_diagnostic_bundle="<<support_.result().string()<<'\n';
    click(find("PERFORMANCE & DIAGNOSTICS"));draw(scene(width,height),L"-performance");
    click(find("RECENT EVENTS"));draw(scene(width,height),L"-events");
    click(find("CLOSE"));developer_panel_.toggle();
    click(find("EMPIRE MONITOR"));draw(scene(width,height),L"-empires");
    click(find("SHOW HOME SYSTEM"));
    if(!selected_id_||system_workspace_.visible())throw std::runtime_error("Empire monitor navigation failed.");
    developer_panel_.toggle();
    click(find("CELESTIAL INDEX"));draw(scene(width,height),L"-celestial-index");
    click(find("Search name"));
    if(!wants_text_input())throw std::runtime_error("Celestial search did not enable native text input.");
    route({{InputEventType::TextEntered,{}, {},0,"Galactic center"}});draw(scene(width,height),L"-central-index");
    click(find("CENTER GALAXY MAP"));
    const auto &core=session_->frame().runtime().world().campaign().galactic_core;
    if(!core||std::hypot(camera_.center.x-core->x,camera_.center.y-core->y)>1e-5||system_workspace_.visible())
      throw std::runtime_error("Celestial focus failed to center the galaxy map.");
    const auto after=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    if(before!=after)throw std::runtime_error("Celestial inspection changed canonical campaign state.");
    std::cout<<"developer_celestial_index=search_focus_and_discovery_preservation_passed\n";
    developer_panel_.toggle();
    const auto &exploration=session_->frame().runtime().world().campaign().developer_provenance;
    click(find(exploration->full_exploration?"ENTIRE GALAXY REVEALED":"REVEAL ENTIRE GALAXY"));
    click(find("CLOSE"));
    const auto &revealed=session_->frame().runtime().world().campaign();
    if(!stellar::native_stellar::observed_central_artwork(revealed,revealed.player_civilization_id)||known_.size()!=revealed.systems.size())
      throw std::runtime_error("Live reveal omitted the central black hole or a system.");
    const auto capture_core=[&](std::wstring_view suffix){
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);int settled=0;
      do {
        route({{InputEventType::PointerMove,{0,0}}});(void)scene(width,height);
        settled=artwork_ready()?settled+1:0;
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Revealed galaxy artwork failed to settle.");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }while(settled<40);
      const auto view=scene(width,height);const auto point=camera_.project({revealed.core->position.x,revealed.core->position.y},width,height);
      const bool image=std::ranges::any_of(view.world,[&](const auto &command){const auto *i=std::get_if<Image>(&command);
        const auto radius=stellar::native_stellar::central_black_hole_map_radius(revealed.core->exclusion_radius,camera_.pixels_per_world,width,height);
        return i&&i->tint.a==255&&std::abs(i->destination.width-radius*2.9f)<1&&std::abs(i->destination.x+i->destination.width*.5f-point.x)<1&&std::abs(i->destination.y+i->destination.height*.5f-point.y)<1;});
      if(!image||galaxy_backdrop_.last_render_stats().undisclosed_core_fog_images)throw std::runtime_error("Central black hole artwork is absent or still covered by core fog.");
      if(!territory_overlay_.projection()||!territory_overlay_.projection()->unexplored_system_ids.empty())throw std::runtime_error("Full exploration left unexplored map territory.");
      draw(view,suffix);
    };
    capture_core(L"-central-black-hole");fit_camera(width,height);capture_core(L"-explored-galaxy");
    std::cout<<"developer_empire_monitor=read_only_focus_passed\ndeveloper_live_reveal=all_systems_and_central_artwork_passed\n";
    // Exercise real foreign-world navigation and both planetary inspection tabs.
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    const auto inspection_before=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    const auto foreign=std::ranges::find_if(revealed.colonies,[&](const auto& c){return c.civilization_id!=revealed.player_civilization_id&&c.system_id!=sol_system_id&&c.planetary_body_id;});
    if(foreign==revealed.colonies.end())throw std::runtime_error("Developer inspection smoke requires an alien colony.");
    const auto roster=stellar::native_colony_roster::build(revealed,session_->cache().generation);
    const auto row=std::ranges::find(roster.rows,foreign->id,&stellar::native_colony_roster::Row::colony_id);
    if(row==roster.rows.end()||!row->can_open)throw std::runtime_error("Developer colony roster omitted the alien homeworld.");
    open_roster_colony({.open_colony_id=row->colony_id,.generation=roster.generation,.player_id=roster.player_id,.body_id=row->body_id,.system_id=row->system_id},width,height);
    const auto capture_planet=[&](std::wstring_view suffix){
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);int settled=0;
      do { route({{InputEventType::PointerMove,{0,0}}});(void)scene(width,height);settled=artwork_ready()?settled+1:0;
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Planetary inspection artwork failed to settle.");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }while(settled<40);
      const auto rendered=scene(width,height);
      for(const auto& command:rendered.overlay)if(const auto* t=std::get_if<Text>(&command);t&&t->value.find("not available to this observer")!=std::string::npos)
        throw std::runtime_error("Developer planetary screen still withholds colony data.");
      draw(rendered,suffix);
    };
    if(!colony_workspace_.view()||colony_workspace_.view()->observer_only||!colony_workspace_.view()->foreign_settlement||colony_workspace_.view()->population_millions!=foreign->population_millions)
      throw std::runtime_error("Foreign planetary screen lost live colony statistics.");
    capture_planet(L"-alien-colony");click(find("Economy"));capture_planet(L"-alien-economy");
    if(!enter_system(sol_system_id,width,height)||!system_workspace_.select_body(earth_body_id))throw std::runtime_error("Earth classification smoke navigation failed.");
    open_colony_from_system(earth_body_id);
    if(!colony_workspace_.view()||colony_workspace_.view()->planet.world_class!=PlanetaryWorldClass::Continental)throw std::runtime_error("Earth is missing Continental classification.");
    capture_planet(L"-earth-classification");colony_workspace_.close();system_workspace_.close();
    for(const auto key:{"mercury","venus","earth","moon","mars","jupiter","saturn","uranus","neptune"}){
      if(!enter_system(sol_system_id,width,height))throw std::runtime_error("Authored planet smoke could not enter Sol.");
      const auto& bodies=system_workspace_.snapshot()->bodies;
      const auto body=std::ranges::find_if(bodies,[&](const auto& b){return b.sol_texture_key==std::optional<std::string>{key};});
      if(body==bodies.end()||!system_workspace_.select_body(body->id))throw std::runtime_error("Authored test planet is absent from Sol.");
      const auto body_id=body->id;
      system_workspace_.focus_selected_body(width,height);
      const auto field=stellar::native_system_ui::SystemWorkspaceLayout::for_viewport(width,height).world_field;
      const Point focus{field.x+field.width*.5f,field.y+field.height*.5f};
      const auto radius=body_display_radius(body->radius_earth,body->kind)*system_workspace_.viewport()->scale;
      const auto steps=std::log(std::min(field.width,field.height)*.38f/std::max(.001f,radius))/std::log(1.16f);
      route({{.type=InputEventType::Wheel,.position=focus,.wheel_y=steps}});
      const std::wstring label=L"-"+std::wstring(key,key+std::char_traits<char>::length(key));
      capture_planet(label+L"-system");
      route({{.type=InputEventType::LeftPressed,.position=focus,.click_count=2},{.type=InputEventType::LeftReleased,.position=focus}});
      if(!colony_workspace_.view()||colony_workspace_.view()->body_id!=body_id||colony_workspace_.view()->planet.sol_texture_key!=std::optional<std::string>{key})throw std::runtime_error("Planetary navigation changed authored identity.");
      const auto grid_scene=scene(width,height);
      if(std::ranges::any_of(grid_scene.overlay,[](const auto& command){const auto* t=std::get_if<Text>(&command);return t&&t->value.starts_with("●  Geographic provinces");}))
        click(find("●  Geographic provinces")); // Layer choice survives planet switching.
      capture_planet(label+L"-globe-front");
      const auto rendered=scene(width,height);
      // The globe binds the canonical material at its 2048 LOD; compare the
      // submitted texture against that same cache entry. Albedo width is
      // source-limited (resize_map never upscales), so assert it exceeds the
      // 256 LOD tier rather than a fixed size.
      const auto canonical=planet_material_cache_.request(*colony_workspace_.view()->planet.appearance,2048);
      bool authored=false;for(const auto& command:rendered.overlay)if(const auto* s=std::get_if<Scene3DView>(&command);s&&s->scene&&!s->scene->instances().empty())
        authored|=canonical&&s->scene->instances()[0].material.texture==canonical->albedo;
      if(!authored||!canonical||!canonical->albedo||canonical->albedo->width()<256)throw std::runtime_error("3D screen did not submit the supplied planet map.");
      const auto area=stellar::native_colony_ui::PlanetaryLayout::make(width,height).globe;
      const auto center=colony_workspace_.planetary().globe().center(area);
      const auto start_rotation=colony_workspace_.planetary().globe().rotation();
      route({{InputEventType::LeftPressed,center},{InputEventType::PointerMove,{center.x-524,center.y}},{InputEventType::LeftReleased,{center.x-524,center.y}}});
      if(std::abs(colony_workspace_.planetary().globe().rotation()-start_rotation)<3.f)throw std::runtime_error("Planetary mouse drag failed to reveal the far side.");
      capture_planet(label+L"-globe-back");colony_workspace_.close();system_workspace_.close();
    }
    std::cout<<"authored_planets=all_nine_sol_bodies_system_and_rotating_globes_passed\n";
    const auto inspection_after=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    if(inspection_before!=inspection_after)throw std::runtime_error("Developer inspection modified the saved campaign.");
    std::cout<<"developer_inspection=alien_colony_economy_and_earth_classification_read_only_passed\n";
    for(const auto count:{1u,2u}){
      const auto multiple=std::ranges::find_if(revealed.systems,[&](const auto& s){return s.stellar_orbits&&s.stellar_orbits->companions.size()==count;});
      if(multiple==revealed.systems.end()||!enter_system(multiple->id,width,height))throw std::runtime_error("Multiple-star smoke needs a surveyed binary and triple.");
      capture_planet(count==1?L"-binary-system":L"-triple-system");
      const auto rendered=scene(width,height);
      for(unsigned component=0;component<=count;++component){const auto label=stellar_host_name(static_cast<int>(component));
        if(std::ranges::none_of(rendered.world,[&](const auto& command){const auto* text=std::get_if<Text>(&command);return text&&text->value.ends_with(label)&&text->align==TextAlign::Center;}))
          throw std::runtime_error("System chart omitted a component star.");
      }
      system_workspace_.close();
    }
    std::cout<<"multiple_stars=binary_and_triple_components_paths_and_artwork_submitted_passed\n";
    if(!enter_system(sol_system_id,width,height))throw std::runtime_error("Small-body smoke cannot enter Sol.");
    capture_planet(L"-belts-sol-overview");
    click(find("BELTS & DEBRIS"));capture_planet(L"-belts-inspector");
    click(find("Show orbital bands"));capture_planet(L"-belts-debug");
    click(find("Focus body"));capture_planet(L"-belts-rock-close");
    if(system_workspace_.small_body_statistics().solid_bodies==0)throw std::runtime_error("Asteroid close-up did not submit solid 3D geometry.");
    click(find("BELTS & DEBRIS"));click(find("Next large"));click(find("Focus body"));capture_planet(L"-belts-rock-huge");
    click(find("BELTS & DEBRIS"));click(find("Next field"));click(find("Focus body"));capture_planet(L"-belts-ice-close");
    click(find("BELTS & DEBRIS"));click(find("Next large"));click(find("Focus body"));capture_planet(L"-belts-ice-huge");
    if(system_workspace_.small_body_statistics().solid_bodies==0)throw std::runtime_error("Ice close-up did not submit solid 3D geometry.");
    std::cout<<"small_body_solids=rock_ice_and_large_body_depth_tested_meshes_passed\n";
    const auto inspected=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    if(inspected!=inspection_after)throw std::runtime_error("Small-body inspection modified campaign state.");
    // Replay both material families; only checking ice missed flat rocky overlays.
    const auto verify_belt_motion=[&](std::wstring prefix,bool icy){
    // Sky domes and planet globes submit their own Scene3DViews to the frame;
    // inspect the exact scene the small-body renderer submitted.
    const auto solid_scene=[&](){(void)scene(width,height);
      auto submitted=system_workspace_.small_body_scene();
      if(!submitted||submitted->instances().empty())throw std::runtime_error("Motion replay lost its solid scene.");
      return submitted;};
    const auto motion_before=solid_scene();const auto day_before=session_->frame().clock().simulation_days();
    if(icy&&std::ranges::none_of(motion_before->instances(),[](const auto& body){return body.material.dielectric.has_value();}))
      throw std::runtime_error("Icy solids did not use reflective/refractive materials.");
    const auto motion_field=system_workspace_.snapshot()->small_body_fields[icy?1:0];
    const auto reference=small_body_instance(motion_field,0);
    const auto orbit_before=stellar::engine::analytic_orbit_position(reference.orbit,day_before-motion_field.epoch_days);
    const auto previous_developer_speed=session_->frame().runtime().world().campaign().developer_provenance->simulation.speed;
    session_->frame().set_developer_speed(1);
    click(find("Paused / Resume"));
    InputSnapshot motion_input;motion_input.drawable_width=width;motion_input.drawable_height=height;
    for(int i=0;i<80;++i){if(!update(motion_input,width,height,.05,true))throw std::runtime_error("Motion replay exited.");
      system_workspace_.focus_small_body(width,height);
      if(i%10==0)draw(scene(width,height),prefix+std::to_wstring(i/10));}
    const auto motion_after=solid_scene();const auto& ma=motion_before->instances().front();const auto& mb=motion_after->instances().front();
    const auto orbit_after=stellar::engine::analytic_orbit_position(reference.orbit,session_->frame().clock().simulation_days()-motion_field.epoch_days);
    if(session_->frame().clock().simulation_days()<=day_before+.1||ma.mesh!=mb.mesh||
       std::abs(ma.rotation.x-mb.rotation.x)+std::abs(ma.rotation.y-mb.rotation.y)+std::abs(ma.rotation.z-mb.rotation.z)+std::abs(ma.rotation.w-mb.rotation.w)<.02f||
       std::hypot(orbit_before[0]-orbit_after[0],orbit_before[1]-orbit_after[1])<1e-7)
      throw std::runtime_error("Resume did not advance the same asteroid's orbit and full-axis spin.");
    click(find("Motion ON / Pause"));const auto paused_day=session_->frame().clock().simulation_days();
    (void)update(motion_input,width,height,1.,true);const auto paused_scene=solid_scene();
    if(session_->frame().clock().simulation_days()!=paused_day||paused_scene->instances().front().rotation.w!=mb.rotation.w)
      throw std::runtime_error("Pause did not freeze asteroid motion.");
    session_->frame().set_developer_speed(previous_developer_speed);
    capture_planet(prefix+L"paused");
    };
    // Ice belts legitimately contain rocky inclusions, so the largest focused
    // body may be non-dielectric. Advance to an icy body so the optics check
    // exercises reflective/refractive materials on an actual ice solid.
    for(int i=0;;++i){
      const auto focused=system_workspace_.focused_small_body();
      if(focused&&focused->material>=stellar::core::SmallBodyMaterial::WaterIce)break;
      if(i>=64)throw std::runtime_error("Ice field has no large icy body to focus.");
      click(find("BELTS & DEBRIS"));click(find("Next large"));click(find("Focus body"));}
    verify_belt_motion(L"-belts-motion-",true);
    click(find("BELTS & DEBRIS"));click(find("Previous field"));click(find("Next large"));click(find("Focus body"));
    verify_belt_motion(L"-belts-rock-motion-",false);
    std::cout<<"rocky_body_motion=solid_rock_resume_orbit_tumble_pause_passed\n";
    std::cout<<"small_body_optics_motion=dielectrics_resume_orbit_tumble_pause_passed\n";
    click(find("BELTS & DEBRIS"));
    const auto initial_fields=system_workspace_.snapshot()->small_body_fields.size();
    for(const auto name:{"+ Asteroid belt","+ Ice belt","+ Debris disk","+ Cracked debris"})click(find(name));
    if(system_workspace_.snapshot()->small_body_fields.size()!=initial_fields+4)throw std::runtime_error("Developer field spawn controls failed.");
    capture_planet(L"-belts-cracked-inspector");
    click(find("Focus body"));capture_planet(L"-belts-cracked-close");
    const auto field_save=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    if(field_save.find("SmallBodyFields")==std::string::npos||field_save.find("CrackedWorld")==std::string::npos)throw std::runtime_error("Spawned small bodies absent from saved payload.");
    session_->request_save();
    const auto save_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(session_->notice().kind!=SessionNoticeKind::Saved){
      route({{InputEventType::PointerMove,{0,0}}});
      if(session_->notice().kind==SessionNoticeKind::Failure||std::chrono::steady_clock::now()>save_deadline)throw std::runtime_error("Small-body manual save failed.");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::ifstream saved_fields(session_->save_path(),std::ios::binary);
    const std::string saved_field_json{std::istreambuf_iterator<char>(saved_fields),std::istreambuf_iterator<char>()};
    auto field_reload=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(asset_root_/"data/research/v1"),saved_field_json);
    const auto loaded_sol=std::ranges::find(field_reload.galaxy().systems,sol_system_id,&StellarSystem::id);
    if(loaded_sol==field_reload.galaxy().systems.end()||loaded_sol->small_body_fields!=std::optional{system_workspace_.snapshot()->small_body_fields}||field_reload.simulation_days()!=session_->frame().clock().simulation_days())throw std::runtime_error("Small-body disk save/load changed fields or their clock.");
    std::cout<<"small_body_fields=sol_overview_inspection_picking_four_spawn_commands_saved_records_passed\n";
    system_workspace_.close();
    // Inspect the imported canonical materials through both actual consumers.
    auto& planet_world=session_->frame().runtime().world().campaign();
    for(const auto& sample:std::array<std::pair<PlanetClass,std::string_view>,8>{{
      {PlanetClass::Temperate,"gaia-islands"},{PlanetClass::Frozen,"blue-glacier"},
      {PlanetClass::Volcanic,"magma-ocean"},{PlanetClass::GasGiant,"cream-band"},
      {PlanetClass::Greenhouse,"sulfuric-cloud"},{PlanetClass::Carbon,"diamond-rich"},
      {PlanetClass::Ocean,"archipelago"},{PlanetClass::Frozen,"icy-hotspots"}}}){
      const auto target=force_developer_planet_type(planet_world,sample.first,sample.second,-1,session_->frame().clock().simulation_days());
      if(!enter_system(target.first,width,height)||!system_workspace_.select_body(target.second))throw std::runtime_error("Imported planet navigation failed.");
      system_workspace_.focus_selected_body(width,height);
      const auto appearance=std::ranges::find(planet_world.bodies,target.second,&PlanetaryBody::id)->appearance;
      const std::wstring label=L"-import-"+std::wstring(sample.second.begin(),sample.second.end());
      const auto field=stellar::native_system_ui::SystemWorkspaceLayout::for_viewport(width,height).world_field;
      route({{.type=InputEventType::Wheel,.position={field.x+field.width*.5f,field.y+field.height*.5f},.wheel_y=18}});
      capture_planet(label+L"-system");
      const auto projected=stellar::native_system::project_system(*system_workspace_.snapshot());
      const auto marker=std::ranges::find(projected.bodies,target.second,&stellar::native_system::SystemSpatialBodyMarker::body_id);
      if(marker==projected.bodies.end())throw std::runtime_error("Imported planet has no chart marker.");
      const float close_radius=std::min(field.width,field.height)*.36f/static_cast<float>(appearance->rings.enabled?appearance->rings.outer_radius:1.);
      const float wanted_scale=std::clamp(close_radius/marker->display_radius,.01f,55.f);
      const float close_wheel=std::log(wanted_scale/system_workspace_.viewport()->scale)/std::log(1.16f);
      route({{.type=InputEventType::Wheel,.position={field.x+field.width*.5f,field.y+field.height*.5f},.wheel_y=close_wheel}});
      system_workspace_.focus_selected_body(width,height);capture_planet(label+L"-system-close");
      open_colony_from_system(target.second);capture_planet(label+L"-globe");
      if(colony_workspace_.view()->planet.appearance!=appearance)throw std::runtime_error("System/planetary view changed canonical imported appearance.");
      const auto pack=planet_material_cache_.request(*appearance,2048);bool submitted=false;
      for(const auto& command:scene(width,height).overlay)if(const auto* view=std::get_if<Scene3DView>(&command);view&&view->scene)
        for(const auto& mesh:view->scene->instances())submitted|=pack&&mesh.material.texture==pack->albedo;
      if(!submitted)throw std::runtime_error("Imported globe did not submit its canonical 3D material.");
      if(appearance->rings.enabled){
        bool ring_on_planet=false,planet_on_ring=false;
        for(const auto& command:scene(width,height).overlay)if(const auto* view=std::get_if<Scene3DView>(&command);view&&view->scene)
          for(const auto& mesh:view->scene->instances())if(mesh.material.shadow){const auto& shadow=*mesh.material.shadow;
            ring_on_planet|=mesh.material.texture==pack->albedo&&shadow.shape==AnalyticShadowShape3D::Annulus&&shadow.opacity_map==pack->rings;
            planet_on_ring|=mesh.material.texture==pack->rings&&shadow.shape==AnalyticShadowShape3D::Ellipsoid;}
        if(!ring_on_planet||!planet_on_ring)throw std::runtime_error("Imported ringed globe lost its mutual shadows.");
        std::cout<<"planet_ring_shadows=canonical_ring_opacity_and_solid_planet_blocker_passed\n";
      }
      const auto portrait=planet_material_cache_.portrait(*appearance);bool portrait_submitted=false;
      for(const auto& command:scene(width,height).overlay)if(const auto* image=std::get_if<Image>(&command))portrait_submitted|=portrait&&image->resource==portrait;
      if(!portrait_submitted)throw std::runtime_error("Planet portrait did not submit its canonical surface preview.");
      if(portrait->width()!=96||portrait->height()!=96)throw std::runtime_error("Canonical portrait escaped its icon-size budget.");
      colony_workspace_.close();system_workspace_.close();
    }
    developer_planet_index_.open(planet_world);click(find("Ancient Grey Crater"));draw(scene(width,height),L"-planet-index");
    click(find("VIEW RULES"));(void)find("Base class:");(void)find("Image pools:");draw(scene(width,height),L"-planet-rules");developer_planet_index_.close();
    if(!planet_material_cache_.errors().empty())throw std::runtime_error("Planet material streaming reported a failure.");
    const auto planet_save=capture_developer_campaign_json(session_->frame().runtime(),{session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    const auto planet_reload=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(asset_root_/"data/research/v1"),planet_save);
    const auto& reloaded_bodies=planet_reload.galaxy().bodies;
    if(reloaded_bodies.size()!=planet_world.bodies.size())throw std::runtime_error("Imported planet persistence changed body count.");
    for(std::size_t i=0;i<reloaded_bodies.size();++i)if(reloaded_bodies[i].appearance!=planet_world.bodies[i].appearance)throw std::runtime_error("Imported appearance rerolled during save/load.");
    std::cout<<"imported_planets=eight_classes_both_views_canonical_materials_developer_index_and_reload_passed\n";
    std::cout<<"planet_portraits=eight_classes_canonical_cached_portraits_submitted_passed\n";
    // Broad material assertion: the coverage world forces one body per
    // generation-enabled subclass, so sweeping bodies exercises every
    // subclass's canonical material through the real cache — admitted art
    // decodes or the procedural fallback synthesizes — not just the eight
    // navigated examples above.
    {
      std::unordered_set<std::string> pending_subclasses;
      for(const auto& def:planet_subclass_definitions())if(def.generation_enabled)pending_subclasses.insert(def.id);
      std::size_t resolved=0;
      for(const auto& body:planet_world.bodies){
        if(!body.appearance||!pending_subclasses.erase(body.appearance->subclass))continue;
        (void)planet_material_cache_.request(*body.appearance,128);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!planet_material_cache_.ready()&&std::chrono::steady_clock::now()<deadline){
          planet_material_cache_.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto pack=planet_material_cache_.request(*body.appearance,128);
        if(!pack||!pack->albedo)throw std::runtime_error("Canonical material did not resolve for subclass "+body.appearance->subclass);
        ++resolved;
      }
      if(!pending_subclasses.empty())throw std::runtime_error("Coverage world is missing subclass "+*pending_subclasses.begin());
      if(!planet_material_cache_.errors().empty())throw std::runtime_error("Broad planet material sweep reported a failure.");
      std::cout<<"broad_planet_materials="<<resolved<<"_subclasses_canonical_albedo_resolved_passed\n";
    }
    // Explicit smoke-only fault injection into this isolated developer world.
    // Runtime fault handling never repairs state; restore this fixture solely
    // so the rest of the smoke can finish without persisting test corruption.
    auto &frame=session_->frame();auto &economy=frame.runtime().world().campaign().economies.front();
    const auto healthy_credits=economy.credits;
    economy.credits=-1.;developer_monitor_.reset();frame.clock().set_speed(StrategicSpeed::Normal);
    developer_monitor_.observe(frame,{},stellar::engine::diagnostic_utc_now());
    respond_to_developer_fault(width,height);
    if(!developer_fault_capture_.latched()||frame.clock().speed()!=StrategicSpeed::Paused)
      throw std::runtime_error("Developer critical finding did not automatically pause.");
    const auto critical_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(developer_fault_capture_.busy()&&std::chrono::steady_clock::now()<critical_deadline){
      developer_fault_capture_.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(developer_fault_capture_.result().empty()||!std::filesystem::is_regular_file(developer_fault_capture_.result()))
      throw std::runtime_error("Developer critical capture failed: "+developer_fault_capture_.error());
    draw(scene(width,height),L"-critical");
    std::cout<<"developer_critical_capture="<<developer_fault_capture_.result().string()<<"\ncritical_pause=true\n";
    economy.credits=healthy_credits;
  }
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
    const auto layout=fleet_workspace_.layout(width,height);
    const auto& fleets=fleet_workspace_.view()->own_fleets;
    const auto military=std::ranges::find_if(fleets,[](const auto& fleet){
      return fleet.role==FleetRole::Military&&fleet.current_system_id&&
             fleet.combat_status&&fleet.combat_status->is_armed;});
    if(military==fleets.end())throw std::runtime_error("Military proof needs an owned armed fixture.");
    const int fleet_id=military->id;
    const auto index=static_cast<std::size_t>(military-fleets.begin());
    click(scroll_fleet_row_into_view(index,width,height));
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
  void verify_command_hud_smoke(int width,int height){
    const auto saved_camera=camera_;const auto saved_selection=selected_id_;
    const auto& world=session_->frame().runtime().world().campaign();
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    if(player==world.civilizations.end())throw std::runtime_error("HUD has no player identity");
    const auto home=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
    const auto route=[&](std::vector<InputEvent> events){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=events.back().position;input.events=std::move(events);if(!update(input,width,height,0.,false))throw std::runtime_error("HUD navigation closed the campaign");};
    const auto click=[&](UiRect r){auto p=center(r);route({{InputEventType::LeftPressed,p},{InputEventType::LeftReleased,p}});};
    camera_.center={home->position.x,home->position.y};
    const Point p{width*.5f,height*.5f};
    route({{InputEventType::Wheel,p,{},1000.f}});
    if(system_workspace_.visible()||camera_.pixels_per_world!=16000.||galaxy_star_core_radius(camera_.pixels_per_world/fitted_pixels_per_world_,height)<80.f)
        throw std::runtime_error("Deep star-map zoom check: system="+std::to_string(system_workspace_.visible())+" scale="+std::to_string(camera_.pixels_per_world)+" fitted="+std::to_string(fitted_pixels_per_world_));
    route({{InputEventType::LeftPressed,p,{},0,{},2},{InputEventType::LeftReleased,p}});
    if(system_workspace_.system_id()!=home->id)throw std::runtime_error("Explored star double-click did not enter its system");
    const auto hud=CommandHudLayout::make(width,height);
    click(hud.switch_view);
    if(system_workspace_.visible()||selected_id_!=home->id)throw std::runtime_error("Galaxy button lost the focused system");
    click(hud.switch_view);
    if(system_workspace_.system_id()!=home->id)throw std::runtime_error("System button did not reopen the focused system");
    // The view-switch button joins the HUD keyboard ring only while it is
    // actionable — Return replays the same activate_hud_switch dispatch.
    {
      const auto ring_items=[&]{return hud_ring_items(NativeUiLayout::for_viewport(width,height),width,height);};
      const auto items=ring_items();
      if(items.empty()||items.back().second!=UiAction::SwitchView)throw std::runtime_error("Switch view missing from the HUD focus ring");
      map_focus_group_=2;hud_focus_=static_cast<int>(items.size())-1;
      const auto key=[&](std::uint32_t k){InputEvent e{InputEventType::KeyPressed};e.key=k;route({e});};
      key(13u);
      if(system_workspace_.visible()||selected_id_!=home->id)throw std::runtime_error("Keyboard switch view did not return to the galaxy");
      const auto galaxy_items=ring_items();
      if(galaxy_items.empty()||galaxy_items.back().second!=UiAction::SwitchView)throw std::runtime_error("Switch view dropped out of the ring with a live selection");
      hud_focus_=static_cast<int>(galaxy_items.size())-1;
      key(13u);
      if(system_workspace_.system_id()!=home->id)throw std::runtime_error("Keyboard switch view did not reopen the focused system");
      map_focus_group_=-1;hud_focus_=-1;
    }
    if(!colony_roster_.view().rows.empty()){
      const auto row=colony_roster_.view().rows.front();
      const auto asset=std::ranges::find_if(assets_.view().rows,[&](const auto& r){return r.body_id==row.body_id;});
      if(asset==assets_.view().rows.end())throw std::runtime_error("Owned planet missing from Controlled Assets");
      assets_.set_selection(asset->key,true);DrawList navigator_draw;assets_.render(navigator_draw,width,height,{});
      const auto bounds=assets_.row_bounds(asset->key,width,height);
      if(!bounds)throw std::runtime_error("Owned planet navigator row inaccessible");
      const auto target=center(*bounds);
      route({{InputEventType::LeftPressed,target,{},0,{},2},{InputEventType::LeftReleased,target}});
      if(!colony_workspace_.visible()||!colony_workspace_.view()||colony_workspace_.view()->body_id!=row.body_id)
        throw std::runtime_error("Planet outliner failed to open the planetary management screen");
      const auto vp=system_workspace_.viewport();const auto sp=project_system(*system_workspace_.snapshot());
      const auto marker=std::ranges::find(sp.bodies,row.body_id,&SystemSpatialBodyMarker::body_id);
      const auto pos=vp->world_to_screen(marker->offset_x,marker->offset_y);
      const auto field=SystemWorkspaceLayout::for_viewport(width,height).world_field;
      if(std::hypot(pos.x-field.x-field.width*.5f,pos.y-field.y-field.height*.5f)>.1f)
        throw std::runtime_error("Planet outliner did not center the selected world");
      colony_workspace_.close();
    }
    system_workspace_.close();camera_=saved_camera;selected_id_=saved_selection;gesture_.cancel();refresh_inspection();
  }
  void prepare_galaxy_art_smoke(int width,int height,bool reload){
    smoke_galaxy_mode_=true;smoke_galaxy_reload_=reload;
    if(camera_.pixels_per_world<fitted_pixels_per_world_*4.99)
      throw std::runtime_error("Campaign did not start in the home-system neighborhood.");
    smoke_galaxy_day_=session_->frame().clock().simulation_days();
    const auto click_pause=[&]{const auto layout=NativeUiLayout::for_viewport(width,height);const auto point=center(layout.pause);InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke pause input closed the campaign.");};
    if(session_->frame().clock().speed()==StrategicSpeed::Paused)click_pause();
    click_pause();
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused)throw std::runtime_error("Galaxy artwork smoke did not pause through player input.");
    smoke_galaxy_paused_=true;verify_command_hud_smoke(width,height);fit_camera(width,height);smoke_galaxy_fitted_scale_=camera_.pixels_per_world;
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
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    const auto sol=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
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
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    const auto sol=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
    if(sol==world.systems.end())throw std::runtime_error("Galaxy artwork smoke could not locate Sol in the catalog.");
    const auto anchor=camera_.project({sol->position.x,sol->position.y},width,height);
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=anchor;input.events={{InputEventType::LeftPressed,anchor,{},0,{},2},{InputEventType::LeftReleased,anchor}};if(!update(input,width,height,0.,false))throw std::runtime_error("Galaxy artwork smoke system entry input closed the campaign.");
    if(!system_workspace_.visible())throw std::runtime_error("Galaxy artwork smoke double click did not open the observed system.");
    smoke_galaxy_system_entry_=true;
  }
  void capture_galaxy_system(int width,int height){smoke_galaxy_system_=galaxy_scene_evidence(width,height);}
  void capture_eruption_smoke(int width,int height,const std::function<void(const DrawList&,const wchar_t*)>& render){
    auto& frame=session_->frame();auto& world=frame.runtime().world().campaign();
    if(!world.developer_provenance)throw std::runtime_error("Eruption replay requires an isolated developer campaign");
    // The focused replay also runs without the full developer-index replay,
    // whose reveal command previously supplied this observer prerequisite.
    fully_explore_developer_galaxy(world);refresh_knowledge();
    developer_panel_.close();developer_index_.close();developer_planet_index_.close();
    developer_empires_.close();developer_diagnostics_.close();stellar_activity_panel_.close();
    colony_workspace_.close();settlement_workspace_.clear();system_workspace_.close();
    if(menu_)toggle_menu();frame.clock().set_speed(StrategicSpeed::Paused);
    auto sol=std::ranges::find(world.systems,sol_system_id,&StellarSystem::id);
    if(sol==world.systems.end())throw std::runtime_error("Eruption replay requires Sol");
    const auto route=[&](std::vector<InputEvent> events){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=events.back().position;input.events=std::move(events);(void)update(input,width,height,0.,false);};
    const auto capture=[&](const wchar_t* suffix){
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);int settled=0;
      do{auto image=scene(width,height);render(image,nullptr);settled=artwork_ready()?settled+1:0;
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Stellar eruption assets did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }while(settled<35);
      auto image=scene(width,height);render(image,suffix);
    };
    const auto force=[&](int sid,int kind,int variant){
      StellarActivityCommand cmd;cmd.system_id=sid;cmd.action=StellarActivityAction::ClearForced;
      (void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);
      cmd.action=StellarActivityAction::Force;cmd.type=static_cast<StellarEruptionType>(kind);cmd.variant=variant;cmd.longitude=1.30;cmd.orientation=1.57;cmd.magnitude=1.5;
      const auto id=apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);
      cmd.action=StellarActivityAction::Scrub;cmd.event_id=id;cmd.fraction=.35;
      (void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),cmd);return cmd;
    };
    auto command=force(sol->id,2,3);const auto id=command.event_id;
    camera_.center={sol->position.x,sol->position.y};camera_.pixels_per_world=16000.;selected_id_=sol->id;refresh_inspection();
    capture(L"-eruption-map");
    const auto find_record=[&](){for(const auto& record:eruption_art_.records())if(record.id==id)return record;throw std::runtime_error("Authoritative eruption absent from live view");};
    const auto map_record=find_record();
    if(!enter_system(sol->id,width,height))throw std::runtime_error("Eruption system entry failed");
    auto viewport=system_workspace_.viewport();Point center{viewport->center_x,viewport->center_y};
    route({{InputEventType::Wheel,center,{},std::log(150.f/star_screen_radius(viewport->scale))/std::log(1.16f)}});
    capture(L"-eruption-system");const auto system_record=find_record();
    if(map_record.progress!=system_record.progress||map_record.variant!=system_record.variant||map_record.stage!=system_record.stage)throw std::runtime_error("Changing live views restarted eruption");
    command.action=StellarActivityAction::TogglePause;(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),command);
    frame.set_developer_speed(1);frame.clock().resume();const double start_day=frame.clock().simulation_days();
    // Cross at least one complete one-hour simulation tick at 1x; subsecond
    // sampling alone can mistake floating-point reconstruction noise for motion.
    for(int i=0;i<90;++i){(void)frame.advance(.016);render(scene(width,height),nullptr);std::this_thread::sleep_for(std::chrono::milliseconds(16));}
    const auto live_system=find_record();system_workspace_.close();camera_.center={sol->position.x,sol->position.y};camera_.pixels_per_world=16000.;
    render(scene(width,height),L"-eruption-live-map");const auto live_map=find_record();
    if(frame.clock().simulation_days()<=start_day||live_system.progress-system_record.progress<.005||live_map.progress<live_system.progress||live_map.progress-live_system.progress>.02)throw std::runtime_error("Live eruption progression does not survive view switch");
    frame.clock().set_speed(StrategicSpeed::Paused);
    const auto encoded=capture_developer_campaign_json(frame.runtime(),{frame.clock().simulation_days(),STELLAR_GAME_VERSION,"2050-03-21T00:00:00Z"});
    if(encoded.find("StellarActivity")==std::string::npos)throw std::runtime_error("Saved campaign lost stellar activity payload");
    // Exercise the actual developer control entry and take a readable capture.
    stellar_activity_panel_.open(frame,sol->id);focus_stellar_activity(width,height);render(scene(width,height),L"-eruption-controls");stellar_activity_panel_.close();
    command.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),command);
    const auto a=std::ranges::find_if(world.systems,[](const auto& s){return s.stellar_activity&&s.stellar_activity->front().profile.spectral==EruptionSpectralClass::A;});
    if(a!=world.systems.end()){
      command=force(a->id,1,0);camera_.center={a->position.x,a->position.y};selected_id_=a->id;camera_.pixels_per_world=16000.;refresh_inspection();
      // Put the new A-class rising image at the centre of its own stage.
      const auto& event=a->stellar_activity->front().events.back();command.fraction=(event.stage_days[0]+event.stage_days[1]*.5)/stellar_eruption_duration(event);
      (void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),command);capture(L"-eruption-a-rising");
      command.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,frame.runtime().stellar_activity(),frame.runtime().stellar_activity_day(),command);
    }
    std::cout<<"stellar_eruptions=live_map_system_same_id_variant_position_timeline_continuity_and_campaign_payload_passed id="<<id<<" map="<<map_record.progress<<" system="<<system_record.progress<<" live_system="<<live_system.progress<<" live_map="<<live_map.progress<<'\n';
    fit_camera(width,height);selected_id_.reset();
  }

  void capture_stellar_art_smoke(int width,int height,
      const std::function<void(const DrawList&,const wchar_t*)>& render){
    const auto& world=session_->frame().runtime().world().campaign();
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    const auto home=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
    const auto route=[&](std::vector<InputEvent> events){InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=events.back().position;input.events=std::move(events);if(!update(input,width,height,0.,false))throw std::runtime_error("Stellar artwork navigation closed the campaign");};
    const auto capture=[&](const wchar_t* suffix,Point at,float radius){
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
      int settled=0;
      do{
        const auto draw=scene(width,height);render(draw,nullptr);
        settled=artwork_ready()?settled+1:0;
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Close stellar artwork did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }while(settled<40);
      const auto draw=scene(width,height);
      const bool supplied_detail=std::ranges::any_of(draw.world,[&](const auto& command){
        const auto* image=std::get_if<Image>(&command);
        return image&&image->resource->width()==1024&&image->tint.a==255&&
            std::abs(image->destination.x+image->destination.width*.5f-at.x)<1.f&&
            std::abs(image->destination.y+image->destination.height*.5f-at.y)<1.f&&
            std::abs(image->destination.width-radius*2.9f)<1.f;
      });
      if(!supplied_detail)throw std::runtime_error("Close stellar view still renders a distance marker instead of supplied artwork");
      render(draw,suffix);
    };
    system_workspace_.close();camera_.center={home->position.x,home->position.y};
    const Point middle{width*.5f,height*.5f};
    route({{InputEventType::Wheel,middle,{},1000.f}});
    const auto art=stellar::native_stellar::observed_stellar_artwork(SystemSurveyLevel::fully_surveyed,home->stellar_object,home->primary);
    if(!art)throw std::runtime_error("Home star has no observed artwork");
    capture(L"-star-map-close",middle,galaxy_star_core_radius(camera_.pixels_per_world/fitted_pixels_per_world_,height)*art->scale);
    route({{InputEventType::LeftPressed,middle,{},0,{},2},{InputEventType::LeftReleased,middle}});
    if(system_workspace_.system_id()!=home->id)throw std::runtime_error("Close stellar artwork did not enter the home system");
    const auto* viewport=system_workspace_.viewport();
    const Point star{viewport->center_x,viewport->center_y};
    const auto wheel=std::log(160.f/star_screen_radius(viewport->scale))/std::log(1.16f);
    route({{InputEventType::Wheel,star,{},wheel}});
    capture(L"-star-system-close",star,star_screen_radius(system_workspace_.viewport()->scale,art->scale));
    route({{InputEventType::Wheel,star,{},1000.f}});
    capture(L"-star-system-maximum",star,star_screen_radius(system_workspace_.viewport()->scale,art->scale));
    std::cout<<"stellar_closeup={\"artwork\":\""<<art->id<<"\",\"map_detail\":true,\"system_detail\":true,\"maximum_zoom_detail\":true}\n";
    system_workspace_.reset_fit(width,height);
  }
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
         <<",\"star_background\":"<<evidence.backdrop.star_background_images
         <<",\"regional_nebula\":"<<evidence.backdrop.regional_nebula_images
         <<",\"regional_points\":"<<evidence.backdrop.regional_points
         <<",\"undisclosed_core_fog\":"<<evidence.backdrop.undisclosed_core_fog_images
         <<",\"background_images\":"<<(system?evidence.backdrop.deep_field_images+evidence.backdrop.star_background_images+evidence.backdrop.galaxy_layer_images+evidence.backdrop.regional_nebula_images+evidence.backdrop.undisclosed_core_fog_images:0)
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
    const auto resumed_developer_speed=developer_session()?session_->frame().runtime().world().campaign().developer_provenance->simulation.speed:0;
    smoke_system_pause_retained_=system_workspace_.visible()&&resumed_speed!=StrategicSpeed::Paused;
    click({main_layout.speed.x+main_layout.speed.width*.5f,main_layout.speed.y+main_layout.speed.height*.5f});
    smoke_system_speed_retained_=system_workspace_.visible()&&(developer_session()?session_->frame().runtime().world().campaign().developer_provenance->simulation.speed!=resumed_developer_speed:session_->frame().clock().speed()!=resumed_speed);
    click({main_layout.pause.x+main_layout.pause.width*.5f,main_layout.pause.y+main_layout.pause.height*.5f});
    if(session_->frame().clock().speed()!=StrategicSpeed::Paused||!smoke_system_pause_retained_||!smoke_system_speed_retained_)throw std::runtime_error("System smoke global clock controls closed the workspace or failed to route: resumed="+std::to_string(static_cast<int>(resumed_speed))+", pause="+std::to_string(smoke_system_pause_retained_)+", speed="+std::to_string(smoke_system_speed_retained_)+", critical="+std::to_string(developer_fault_capture_.latched())+", menu="+std::to_string(menu_));
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
    if(!colony_workspace_.visible())throw std::runtime_error("Planetary screen is not visible.");
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
    const auto before=state();click_label("Build");click_label("Available slot");
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
    const auto layout=ColonyWorkspaceLayout::for_viewport(width,height);
    const auto click_label=[&](std::string_view label){
      for(int attempt=0;attempt<24;++attempt){
        const auto draw=scene(width,height);
        for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value==label){
          Point p{t->at.x+20,t->at.y+4};if(!t->clip||t->clip->contains(p)){click({p.x,p.y,0,0});return;}
        }
        InputSnapshot scroll;scroll.drawable_width=width;scroll.drawable_height=height;
        scroll.events={{InputEventType::Wheel,center(PlanetaryLayout::make(width,height).details),{},-2.f}};
        (void)update(scroll,width,height,0.,false);
      }
      throw std::runtime_error("Planetary freight control not reachable: "+std::string(label));
    };
    click_label("Economy");
    NativeOutpostFreightPreview quote;bool reviewed=false,cancelled=false;
    if(!reload){
      click_label("Collect materials");
      if(!colony_workspace_.freight_preview()||!colony_workspace_.freight_preview()->accepted)
        throw std::runtime_error("Freight review unavailable: "+(colony_workspace_.freight_preview()?colony_workspace_.freight_preview()->message:"no review"));
      reviewed=state()==before;quote=*colony_workspace_.freight_preview();
      smoke_freight_review_capture_=scene(width,height);
      click(layout.freight_cancel);cancelled=!colony_workspace_.freight_preview()&&state()==before;
      click_label("Collect materials");
      if(!colony_workspace_.freight_preview()||colony_workspace_.freight_preview()->fleet_id!=quote.fleet_id)
        throw std::runtime_error("Freight rereview changed its selected idle ship.");
      click(layout.freight_confirm);
    }else{
      const auto ship=std::ranges::find_if(world.fleets,[&](const auto& f){return f.civilization_id==initial.player_civilization_id&&f.freight_target_outpost_id==initial.colony_id;});
      if(ship==world.fleets.end()||!ship->freight_home_colony_id)throw std::runtime_error("Freight reload lost its dispatch.");
      quote.fleet_id=ship->id;quote.home_colony_id=*ship->freight_home_colony_id;
      if(state()!=before)throw std::runtime_error("Freight reload changed paused state.");
      colony_workspace_.set_freight_notice(tr("COLONY_FREIGHT_RESTORED","Freight run restored. Unpause to continue travel, loading and delivery."));
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
  [[nodiscard]] bool smoke_map_point_exposed(Point point,int width,int height)const{
    const auto ui=NativeUiLayout::for_viewport(width,height);
    const auto hud=CommandHudLayout::make(width,height);
    const auto assets_layout=stellar::native_assets::Layout::make(width,height);
    return point.x>=0&&point.y>=0&&point.x<width&&point.y<height&&
        !fleet_workspace_.layout(width,height).panel.contains(point)&&
        !ui.navigation_bar.contains(point)&&!hud.resource_strip.contains(point)&&
        !hud.context.contains(point)&&
        !(assets_.preferences().hidden?assets_layout.restore:assets_layout.panel).contains(point);
  }
  Point scroll_fleet_row_into_view(std::size_t index,int width,int height){
    const auto count=fleet_workspace_.view()?fleet_workspace_.view()->own_fleets.size():0;
    if(index>=count)throw std::runtime_error("Fleet input replay requested an absent row.");
    refresh_assets();
    const stellar::native_assets::Key key{stellar::native_assets::Category::Fleets,
        fleet_workspace_.view()->own_fleets[index].id};
    assets_.set_selection(key,true);
    DrawList draw;assets_.render(draw,width,height,{});
    const auto row=assets_.row_bounds(key,width,height);
    if(!row)throw std::runtime_error("Fleet input replay could not reveal its Controlled Assets row.");
    // Replays need the fleet's strategic commands. A row-body click now focuses
    // its local system; use the same visible Manage arrow as a player instead.
    const auto layout=stellar::native_assets::Layout::make(width,height);
    return {layout.list.x+layout.list.width-16.f*layout.scale,
            row->y+row->height*.5f};
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
    click(scroll_fleet_row_into_view(fleet_index,width,height));
    smoke_settlement_selected_=fleet_controller_.selection()==smoke_settlement_fleet_id_;
    if(!smoke_settlement_selected_)throw std::runtime_error("Settlement input could not select its visible fleet row.");
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
        auto candidate=settlement_controller_.preview_exact(session_->frame(),session_->cache().generation,chosen->fleet_id,body.system_id,body.id);
        if(candidate.accepted&&candidate.candidate){smoke_settlement_system_id_=body.system_id;smoke_settlement_body_id_=body.id;found=true;break;}
      }
      if(!found)throw std::runtime_error("Settlement smoke found no observer-admitted exact target for the authored vessel.");
    }
    const auto system=session_->cache().systems_by_id.find(*smoke_settlement_system_id_);
    if(system==session_->cache().systems_by_id.end())throw std::runtime_error("Settlement target system is absent from the campaign cache.");
    // A legitimately earned viable world can be outside the initial home view.
    // Center its chart marker before exercising the normal double-click path;
    // never alter survey knowledge, vessel state or the settlement quote.
    const auto target_point=camera_.project({system->second->position.x,system->second->position.y},width,height);
    if(!smoke_map_point_exposed(target_point,width,height))
      camera_.center={system->second->position.x-static_cast<double>(width)*.12/camera_.pixels_per_world,system->second->position.y};
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
    event.type=InputEventType::KeyReleased;
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
  [[nodiscard]] DrawList research_cancellation_smoke(int width,int height,bool restart){
    if(!smoke_research_node_)throw std::runtime_error("Cancellation smoke has no selected program.");
    refresh_research(true);
    auto &research=session_->frame().runtime().research();
    const int player=session_->frame().runtime().world().campaign().player_civilization_id;
    const auto &state=research.get_civilization(player);
    const std::string node=*smoke_research_node_;
    const auto *before=state.try_get_node_state(node);
    if(!before)throw std::runtime_error("Cancellation smoke lost its research node.");
    const double work=before->total_research_points;
    (void)scene(width,height); // Materialize the same hit targets as the visible frame.
    const auto layout=ResearchWorkspaceLayout::for_viewport(width,height,research_workspace_.window()->domain_tabs.size());
    const Point point=restart?center(layout.action):Point{layout.tree_focus.x+layout.tree_focus.width*.75f,layout.tree_focus.y+layout.tree_focus.height*.5f};
    InputSnapshot input;input.drawable_width=width;input.drawable_height=height;input.pointer=point;
    input.events={{InputEventType::LeftPressed,point},{InputEventType::LeftReleased,point}};
    if(!update(input,width,height,0.,false)||!last_research_command_accepted_)
      throw std::runtime_error("Cancellation/restart smoke UI command was rejected.");
    const bool cancelled=state.cancelled_project(node)!=nullptr;
    if(cancelled==restart||state.try_get_node_state(node)->total_research_points!=work)
      throw std::runtime_error("Cancellation/restart smoke lost work or did not change program state.");
    std::cout<<(restart?"research_restart_preserved_work=true\n":"research_cancel_preserved_work=true\n");
    return scene(width,height);
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
    // Pointer replay injection: a recorded pointer_button command must
    // dequeue into a real InputEvent and dispatch through the same click
    // path as live input — drive a recorded click on the HUD menu button,
    // verify the menu opened, then a recorded click on Continue closes it.
    {
      ReplayState pointer_replay;
      auto *const outer_replay=replay_;
      const auto journal_click=[&](const UiRect &cell){
        pointer_replay.recording.emplace();
        const auto point=center(cell);
        char payload[96];
        std::snprintf(payload,sizeof payload,"1,%.9g,%.9g,0,0",
            static_cast<double>(point.x),static_cast<double>(point.y));
        pointer_replay.recording->record(0,"pointer_button",payload);
        payload[0]='2';
        pointer_replay.recording->record(0,"pointer_button",payload);
      };
      InputSnapshot idle;idle.drawable_width=width;idle.drawable_height=height;
      journal_click(layout.menu);
      replay_=&pointer_replay;
      const auto opened=update(idle,width,height,0.,false)&&menu_&&
                        pointer_replay.command_cursor==2;
      pointer_replay=ReplayState{};
      if(opened){
        journal_click(layout.continue_button);
        const auto closed=update(idle,width,height,0.,false)&&!menu_&&
                          pointer_replay.command_cursor==2;
        replay_=outer_replay;
        if(!closed)
          throw std::runtime_error("Replayed menu click did not close the menu.");
      }else{
        replay_=outer_replay;
        throw std::runtime_error("Replayed pointer click did not open the menu.");
      }
    }
    const auto send=[&](InputEvent event){
      InputSnapshot input;
      input.drawable_width=width;input.drawable_height=height;
      input.events.push_back(std::move(event));
      if(!update(input,width,height,0.,false))
        throw std::runtime_error("Keyboard replay closed the campaign.");
    };
    const auto key=[&](std::uint32_t value){
      // The input mapper treats a press without a matching release as "held",
      // so replay realistic press+release pairs like physical input delivers.
      InputEvent event{InputEventType::KeyPressed};event.key=value;send(event);
      InputEvent release{InputEventType::KeyReleased};release.key=value;send(release);
    };
    const auto pad_mouse=[&]{
      auto& clock=session_->frame().clock();
      // Bindings captured in the Controls view must reach the live mapper:
      // rebind pause to pad SOUTH (SDL button 0), verify press+release
      // toggles, then bind speed 1 to a right-click and verify — restoring
      // the keyboard defaults afterwards.
      if(input_mapper_.rebind("toggle_pause",
             {{stellar::engine::RawInputEvent::Kind::GamepadButton,0}})!=1)
        throw std::runtime_error("Pause rebind to gamepad button failed.");
      InputEvent press{InputEventType::GamepadPressed};press.gamepad_button=0;send(press);
      InputEvent release{InputEventType::GamepadReleased};release.gamepad_button=0;send(release);
      const auto entry_speed=clock.speed();
      send(press);send(release);
      if(clock.speed()==entry_speed)
        throw std::runtime_error("Gamepad button did not toggle pause.");
      send(press);send(release);
      if(clock.speed()!=entry_speed)
        throw std::runtime_error("Gamepad button did not toggle pause back.");
      if(input_mapper_.rebind("toggle_pause",
             {{stellar::engine::RawInputEvent::Kind::KeyPress,32}})!=1)
        throw std::runtime_error("Pause keyboard binding restore failed.");
      if(input_mapper_.rebind("speed_normal",
             {{stellar::engine::RawInputEvent::Kind::MouseButton,3}})!=1)
        throw std::runtime_error("Speed rebind to right click failed.");
      InputEvent right{InputEventType::RightPressed};send(right);
      InputEvent right_release{InputEventType::RightReleased};send(right_release);
      if(clock.speed()!=StrategicSpeed::Normal)
        throw std::runtime_error("Right-click binding did not select normal speed.");
      if(input_mapper_.rebind("speed_normal",
             {{stellar::engine::RawInputEvent::Kind::KeyPress,49}})!=1)
        throw std::runtime_error("Speed keyboard binding restore failed.");
      // Axis bindings (GALAXY_PAD) — the mapper holds the last axis value,
      // so a deflection persists until a centered event clears it. Zoom in
      // first so the overview clamp leaves room for the camera to move.
      const auto send_axis=[&](std::uint32_t axis,float value){
        InputEvent stick{InputEventType::GamepadAxis};stick.gamepad_axis=static_cast<std::uint8_t>(axis);stick.gamepad_axis_value=value;
        InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
        input.events.push_back(stick);
        if(!update(input,width,height,.1,false))
          throw std::runtime_error("Pad-axis replay closed the campaign.");
      };
      camera_.pixels_per_world=std::clamp(
          fitted_pixels_per_world_>0.?fitted_pixels_per_world_*4.:400.,.01,16000.);
      camera_.center=galaxy_overview_camera(width,height).center;
      const auto pan_before=camera_.center;
      send_axis(0,1.f);
      if(!system_workspace_.visible()){
        if(camera_.center.x==pan_before.x)
          throw std::runtime_error("Left stick did not pan the galaxy camera.");
        const auto zoom_before=camera_.pixels_per_world;
        send_axis(3,1.f);
        if(camera_.pixels_per_world==zoom_before)
          throw std::runtime_error("Right stick did not zoom the galaxy camera.");
        send_axis(3,0.f);
      }else if(camera_.center.x!=pan_before.x)
        throw std::runtime_error("System view leaked a galaxy-camera pan.");
      send_axis(0,0.f);
      camera_.center=galaxy_overview_camera(width,height).center;
      if(fitted_pixels_per_world_>0.)
        camera_.pixels_per_world=fitted_pixels_per_world_;
      key(' '); // Return to the paused state the caller sequence expects.
      if(clock.speed()!=StrategicSpeed::Paused)
        throw std::runtime_error("Could not restore paused state after pad/mouse check.");
    };
    const auto playback=[&]{
      auto& clock=session_->frame().clock();
      pad_mouse();
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
    // Escape unwinds the topmost layer: an open inspection card consumes the
    // first press, the pause menu takes the second.
    if(inspection_card_.visible()){
      if(!update(escape,width,height,0.,false)||inspection_card_.visible())
        throw std::runtime_error("Navigation smoke could not close the inspection card.");
    }
    if(!update(escape,width,height,0.,false)||!menu_)
      throw std::runtime_error("Navigation smoke could not open the pause menu.");
    blocked_keys();
    click(layout.research);
    smoke_navigation_menu_blocked_=menu_&&!research_workspace_.visible();
    if(!smoke_navigation_menu_blocked_||!update(escape,width,height,0.,false)||menu_)
      throw std::runtime_error("Pause menu did not block or release navigation.");
    prepare_colony_smoke(width,height,false);
    const auto& colony=*colony_workspace_.view();
    if(colony.available_buildings.empty())throw std::runtime_error("No planetary construction option.");
    std::optional<NativeSurfacePlacementQuote> available;
    for(int slot=0;slot<colony.building_capacity;++slot){
      auto quote=surface_controller_.preview_placement(session_->frame(),session_->cache().generation,
          colony,colony.available_buildings.front().type_id,0,0,0,slot);
      if(quote.accepted){available=std::move(quote);break;}
    }
    if(!available)throw std::runtime_error("No planetary confirmation fixture.");
    colony_workspace_.planetary().set_confirmation(*available);
    (void)scene(width,height);
    blocked_keys();click(layout.research);
    smoke_navigation_modal_blocked_=colony_workspace_.visible()&&colony_workspace_.planetary_modal()&&!research_workspace_.visible();
    if(!smoke_navigation_modal_blocked_)throw std::runtime_error("Planetary confirmation leaked navigation.");
    click(PlanetaryLayout::make(width,height).cancel);
    if(colony_workspace_.planetary_modal())throw std::runtime_error("Planetary confirmation did not cancel.");
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
    verify_only(research_workspace_.visible(),tr("NAV_RESEARCH","Research"));
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
    verify_only(shipyard_workspace_.visible(),tr("NAV_SHIPYARD","Shipyard"));
    click(layout.construction);
    verify_only(construction_workspace_.visible(),tr("NAV_CONSTRUCTION","Construction"));
    click(layout.diplomacy);
    verify_only(diplomacy_workspace_.visible(),"Relations");
    blocked_keys();
    // Missions rail affordance: opens exclusively over the workspace class,
    // re-click toggles closed, and Escape releases the panel without
    // reaching the pause menu.
    click(layout.missions);
    if(!mission_view_.visible()||diplomacy_workspace_.visible())
      throw std::runtime_error("Missions rail button did not open exclusively.");
    click(layout.missions);
    if(mission_view_.visible())
      throw std::runtime_error("Missions rail button did not close the board.");
    click(layout.missions);
    send({InputEventType::EscapePressed});
    if(mission_view_.visible()||menu_)
      throw std::runtime_error("Escape did not close the missions board before the menu.");
    // Focus ring: Tab arms the first control, Escape releases the ring
    // while the panel stays open, and a second Escape closes it.
    click(layout.missions);
    key(9);
    if(mission_view_.focus()<0)
      throw std::runtime_error("Tab did not arm the missions focus ring.");
    if(!wants_keyboard_focus())
      throw std::runtime_error("The armed missions ring did not claim keyboard focus.");
    send({InputEventType::EscapePressed});
    if(!mission_view_.visible()||mission_view_.focus()>=0)
      throw std::runtime_error("Escape did not release the missions ring first.");
    send({InputEventType::EscapePressed});
    if(mission_view_.visible()||menu_)
      throw std::runtime_error("Escape did not close the missions board after ring release.");
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
    if(card_before.has_value()!=card_after.has_value()||(card_before&&(card_before->x!=card_after->x||card_before->y!=card_after->y)))
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
    const auto click=[&](Point point,InputEventType press=InputEventType::LeftPressed,std::uint8_t count=1){
      InputSnapshot input;
      input.drawable_width=width;
      input.drawable_height=height;
      input.pointer=point;
      input.events={{press,point,{},0,{},count},
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
    const auto layout=fleet_workspace_.layout(width,height);
    click(scroll_fleet_row_into_view(index,width,height),InputEventType::LeftPressed,2);
    if(fleet_controller_.selection()!=std::optional<int>{selected_fleet_id})
      throw std::runtime_error("Fleet smoke mouse selection failed.");
    smoke_fleet_id_=selected_fleet_id;
    if(!selected_destination){
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
           *candidate.estimated_transit_days>longest_eta&&smoke_map_point_exposed(point,width,height)&&
           system_hit(point,width,height)==system.id){
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
    click(scroll_fleet_row_into_view(recovery_index,width,height),InputEventType::LeftPressed,2);
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
    click(scroll_fleet_row_into_view(index,width,height),InputEventType::LeftPressed,2);
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
      const auto id=shipyard_workspace_.view()->available_designs[*index].id;
      auto design_bounds=shipyard_workspace_.design_bounds(id,width,height);
      for(int attempt=0;!design_bounds&&attempt<32;++attempt){
        InputSnapshot input;input.drawable_width=width;input.drawable_height=height;
        input.events={{InputEventType::Wheel,center(layout.designs),{},-1.f}};
        if(!update(input,width,height,0.,false))throw std::runtime_error("Shipyard scroll closed campaign");
        design_bounds=shipyard_workspace_.design_bounds(id,width,height);
      }
      if(!design_bounds)throw std::runtime_error("Known ship design is inaccessible");
      click(center(*design_bounds));
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
    {const auto status=platform_services_.status();
     support_environment_+="PlatformServices="+status.backend_name+
         (status.available?"(available)":"(unavailable)")+"\n";}
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
    // C/B keyboard parity evidence: the active-project and no-candidate
    // guards keep these presses status-only on both smoke launches.
    smoke_shortcut_=send_key('c',width,height)&&
                    send_key('b',width,height)&&
                    shortcut_status_reported();
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
      const auto result = frame.advance(.25 / frame.clock().days_per_second());
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
    const auto layout = fleet_workspace_.layout(width, height);
    const auto scout_row =
        std::ranges::find(fleet_workspace_.view()->own_fleets,
                          first_exploration_fleet_id_, &NativeOwnFleet::id);
    if (scout_row == fleet_workspace_.view()->own_fleets.end())
      throw std::runtime_error(
          "First exploration scout was absent from the outliner.");
    const auto scout_index = static_cast<std::size_t>(
        scout_row - fleet_workspace_.view()->own_fleets.begin());
    click(scroll_fleet_row_into_view(scout_index,width,height));
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
      click(scroll_fleet_row_into_view(scout_index,width,height));
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
      const auto index = static_cast<std::size_t>(fleet - fleets.begin());
      click(scroll_fleet_row_into_view(index,width,height));
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
    const auto layout = fleet_workspace_.layout(width, height);
    const auto science_row =
        std::ranges::find(fleet_workspace_.view()->own_fleets,
                          first_survey_fleet_id_, &NativeOwnFleet::id);
    if (science_row == fleet_workspace_.view()->own_fleets.end())
      throw std::runtime_error(
          "First survey science vessel was absent from the outliner.");
    const auto science_index = static_cast<std::size_t>(
        science_row - fleet_workspace_.view()->own_fleets.begin());
    const auto select_science = [&] {
      click(scroll_fleet_row_into_view(science_index,width,height));
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

  void request_smoke_save() {
    if (smoke_settlement_mode_) {
      session_->frame().clock().set_speed(StrategicSpeed::Paused);
      capture_settlement_smoke_state();
    }
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
  [[nodiscard]] bool shortcut_smoke_succeeded()const noexcept{return smoke_shortcut_;}
  // Mid-session setup cancellation hands the (already saved) session back so
  // the campaign can resume, matching the reference mode-select cancel.
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
       <<" hover="<<(smoke_fleet_hover_preview_?1:0)
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

  [[nodiscard]] std::string system_travel_smoke_status()const{std::ostringstream out;out<<std::fixed<<std::setprecision(9)<<std::boolalpha<<"{\"mode\":\""<<(smoke_system_travel_reload_?"paused_reload":"progress")<<"\",\"fleet_id\":"<<smoke_system_travel_fleet_id_.value_or(-1)<<",\"system_id\":"<<smoke_system_travel_system_id_.value_or(-1)<<",\"destination_id\":"<<smoke_system_travel_destination_id_.value_or(-1)<<",\"order_revision\":"<<smoke_system_travel_mission_revision_<<",\"lane_count\":"<<smoke_system_travel_lane_count_<<",\"before_x\":"<<smoke_system_travel_before_x_<<",\"before_y\":"<<smoke_system_travel_before_y_<<",\"after_x\":"<<smoke_system_travel_after_x_<<",\"after_y\":"<<smoke_system_travel_after_y_<<",\"before_days\":"<<smoke_system_travel_before_day_<<",\"after_days\":"<<smoke_system_travel_after_day_<<",\"selected\":"<<smoke_system_travel_selected_<<",\"canonical_moved\":"<<smoke_system_travel_canonical_moved_<<",\"rendered_moved\":"<<smoke_system_travel_rendered_moved_<<",\"paused_stable\":"<<smoke_system_travel_paused_stable_<<",\"pause_retained\":"<<smoke_system_travel_pause_retained_<<",\"known_arrow\":"<<smoke_system_travel_known_opened_<<",\"unknown_denied\":"<<smoke_system_travel_unknown_denied_<<",\"knowledge_unchanged\":"<<smoke_system_travel_knowledge_unchanged_<<",\"lanes_connected\":"<<smoke_system_travel_lanes_connected_<<'}';return out.str();}
  [[nodiscard]] bool wants_text_input() const noexcept {
    if(developer_planet_index_.visible()||giant_test_panel_.visible())return false;
    if(developer_index_.visible())return developer_index_.wants_text_input();
    return !menu_ && !battle_workspace_.visible() && (research_workspace_.wants_text_input() || shipyard_workspace_.wants_text_input() || chronicle_view_.wants_text_input() || colony_roster_.wants_text_input() || developer_diagnostics_.wants_text_input() || (map_hud_visible()&&assets_.wants_text_input())) &&
           !construction_workspace_.visible();
  }
  // true while a focus-ring surface owns keyboard activation, so bound galaxy
  // actions (Space -> toggle_pause) do not preempt Return/Space activation.
  [[nodiscard]] bool wants_keyboard_focus() const noexcept {
    return (research_workspace_.visible()&&research_workspace_.focus()>=0)||
           (shipyard_workspace_.visible()&&shipyard_workspace_.focus()>=0)||
           (economy_workspace_.visible()&&economy_workspace_.focus()>=0)||
           (supply_workspace_.visible()&&supply_workspace_.focus()>=0)||
           fleet_workspace_.focus()>=0||
           (construction_workspace_.visible()&&construction_workspace_.focus()>=0)||
           (chronicle_view_.visible()&&chronicle_view_.focus()>=0)||
           (notification_view_.visible()&&notification_view_.focus()>=0)||
           (colony_roster_.visible()&&colony_roster_.focus()>=0)||
           (colony_workspace_.visible()&&colony_workspace_.focus()>=0)||
           (mission_view_.visible()&&mission_view_.focus()>=0)||
           (map_hud_visible()&&assets_.focus()>=0)||
           (developer_index_.visible()&&developer_index_.focus()>=0)||
           (developer_planet_index_.visible()&&developer_planet_index_.focus()>=0)||
           (phenomena_debug_.visible&&phenomena_debug_.focus()>=0)||
           (background_debug_.visible()&&background_debug_.focus()>=0)||
           (giant_test_panel_.visible()&&giant_test_panel_.focus()>=0)||
           (stellar_activity_panel_.visible()&&stellar_activity_panel_.focus()>=0)||
           (developer_empires_.visible()&&developer_empires_.focus()>=0)||
           (developer_panel_.visible()&&developer_panel_.focus()>=0)||
           (developer_diagnostics_.visible()&&developer_diagnostics_.focus()>=0)||
           hud_focus_>=0||
           system_workspace_.small_body_keyboard_focus()||
           inspection_card_.focus()>=0;
  }

  bool update(const InputSnapshot &input,int width,int height,double elapsed,bool advance_simulation=true){
    input_mapper_.begin_frame();
    const auto update_scope=stellar::engine::Profiler::instance().span("update","client");
    // Keep the planet material queue draining even when no visible view is
    // requesting materials (e.g. pause menu over the map); otherwise a pending
    // decode stalls readiness until the next request.
    planet_material_cache_.poll();
    if(planet_material_memory_==stellar::engine::MemoryTracker::invalid_subsystem)
      planet_material_memory_=stellar::engine::MemoryTracker::instance().register_subsystem("planet-materials");
    stellar::engine::MemoryTracker::instance().report(planet_material_memory_,planet_material_cache_.resident_bytes(),stellar::native_planets::MaterialCache::budget);
    if(territory_overlay_memory_==stellar::engine::MemoryTracker::invalid_subsystem)
      territory_overlay_memory_=stellar::engine::MemoryTracker::instance().register_subsystem("territory-overlay");
    stellar::engine::MemoryTracker::instance().report(territory_overlay_memory_,territory_overlay_.cached_image_bytes(),NativeTerritoryOverlay::maximum_cached_image_bytes);
    if(image_preparation_memory_==stellar::engine::MemoryTracker::invalid_subsystem)
      image_preparation_memory_=stellar::engine::MemoryTracker::instance().register_subsystem("image-preparation");
    stellar::engine::MemoryTracker::instance().report(image_preparation_memory_,image_preparation_->reserved_bytes(),ImagePreparationQueue::default_max_reserved_output_bytes);
    // Recording sessions accumulate command payloads unboundedly — the
    // recorder's occupancy joins the census while one is active.
    if(replay_&&replay_->recorder){
      if(replay_recorder_memory_==stellar::engine::MemoryTracker::invalid_subsystem)
        replay_recorder_memory_=stellar::engine::MemoryTracker::instance().register_subsystem("replay-recorder");
      stellar::engine::MemoryTracker::instance().report(replay_recorder_memory_,replay_->recorder->estimated_memory_bytes());
    }
    if(video_settings_)video_settings_->service(input.focused,input.renderable());
    resize_galaxy_camera(width,height);
    assets_refresh_elapsed_+=std::max(0.,elapsed);
    support_notice_seconds_=std::max(0.,support_notice_seconds_-std::max(0.,elapsed));
    if(support_.poll())support_notice_seconds_=20.;
    developer_fault_capture_.poll();
    developer_panel_.set_export_busy(support_.busy());
    session_->frame().set_profiling_enabled(developer_session());
    stellar::engine::Profiler::instance().set_enabled(developer_session());
    feedback_.advance(elapsed);
    if(voice_playback_)voice_playback_->update(std::max(0.,elapsed));
    pointer_=input.focused&&input.renderable()?input.pointer:Point{-1.f,-1.f};
    const auto timestamp=utc_timestamp();
    if(session_->service(timestamp,menu_)){
      developer_panel_.close();developer_index_.close();developer_planet_index_.close();giant_test_panel_.close();stellar_activity_panel_.close();developer_diagnostics_.close();developer_empires_.close();developer_monitor_.reset();developer_fault_capture_.reset();
      support_.record("session",timestamp+" Campaign activated.");
      territory_overlay_.clear();
      last_territory_draw_={};
      territory_refresh_elapsed_=.5;
      feedback_.reset();
      notifications_.clear();
      notification_view_.close();chronicle_view_.close();
      seed_notifications();
      last_event_sound_={};
      if(presentation_audio_)presentation_audio_->stop_voice();
      if(voice_playback_)voice_playback_->reset_campaign();
      if(voice_router_)voice_router_->reset();
      if(voice_bridge_)voice_bridge_->reset(session_->frame().runtime());
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
      colony_roster_.discard_campaign();hud_planet_appearances_.clear();hud_switch_pressed_=false;roster_day_.reset();roster_refresh_elapsed_=0.;

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
      install_replay_observer();
    }
    session_->frame().clock().set_maximum_multiplier(4.);
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
           (event.type==InputEventType::KeyPressed&&(event.key==13u||event.key==32u))||
           (event.type==InputEventType::LeftPressed&&pending_layout.continue_button.contains(event.position))){
          cancel_new_game();if(audio_confirm_)audio_confirm_();break;
        }
      return true;
    }
    refresh_battle(width,height,elapsed);
    refresh_inspection();
    refresh_economy(false,false);
    refresh_roster(false);
    refresh_missions(false);
    const auto layout=NativeUiLayout::for_viewport(width,height);
    const auto route_navigation=[&](UiAction action){
      const auto close_navigation_workspaces=[&]{
        research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
        diplomacy_workspace_.close();colony_roster_.close();economy_workspace_.close();
        supply_workspace_.close();system_workspace_.close();colony_workspace_.close();
        notification_view_.close();chronicle_view_.close();mission_view_.close();
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
          system_workspace_.set_notice(tr("SYSTEM_NOTICE_SELECT","Select a body in this system to inspect its details."));return;
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
        }else if(!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
          zoom_galaxy_camera(direction,{static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},width,height);
        }
        return;
      }
      if(action==UiAction::Explore){
        close_navigation_workspaces();refresh_fleets(true);
        bool selected_scout{};
        if(fleet_workspace_.view())for(const auto &fleet:fleet_workspace_.view()->own_fleets)if(fleet.role==FleetRole::Scout||fleet.role==FleetRole::Science){(void)fleet_controller_.select(session_->frame(),session_->cache().generation,fleet.id);selected_scout=true;break;}
        if(!selected_scout)fleet_workspace_.set_notice(tr("FLEET_NOTICE_NO_SCOUT","No scout or science vessel is available. Select a fleet on the map to plan exploration."),false);
        return;
      }
      if(action==UiAction::Menu){toggle_menu();return;}
      if(action!=UiAction::Supply)supply_workspace_.close();
      if(action!=UiAction::Colonies)colony_roster_.close();
      if(action!=UiAction::Economy)economy_workspace_.close();
      if(action!=UiAction::Missions)mission_view_.close();
      fleet_workspace_.cancel_recovery();
      notification_view_.close();chronicle_view_.close();
      system_workspace_.close();
      colony_workspace_.close();

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
      }else if(action==UiAction::Missions){
        if(mission_view_.visible())mission_view_.close();
        else{research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();mission_view_.open();refresh_missions(true);}
      }
    };
    if(!input.focused||!input.renderable())menu_hover_feedback_.reset();
    // Replay: feed recorded commands whose ticks have arrived through the
    // same dispatch paths as live input. pointer_button commands dequeue
    // into real InputEvents merged into the event stream below — pointer
    // dispatch doesn't need the menu/text gate because the handlers gate on
    // UI state exactly as they did during recording. Key/pad/binding
    // commands resolve through GALAXY under the same input gates — a
    // suppressed tick replays when the gate opens, and in-order replay
    // holds later pointer commands behind a gated binding command.
    if(replay_&&replay_->recording&&session_){
      const auto tick=replay_tick();
      const auto &commands=replay_->recording->commands();
      const bool input_gate_open=
          !menu_&&!diplomacy_workspace_.visible()&&!wants_text_input();
      while(replay_->command_cursor<commands.size()&&
            commands[replay_->command_cursor].tick<=tick){
        const auto &command=commands[replay_->command_cursor];
        if(command.name=="pointer_button"){
          ++replay_->command_cursor;
          // "type,x,y[,dx,dy]" — replayed through the real event stream so
          // hit-testing and dispatch see live-shaped input.
          int type=0;float x=0.f,y=0.f,dx=0.f,dy=0.f;
          const char *const begin=command.payload.data();
          const char *const end=begin+command.payload.size();
          const auto parsed_type=std::from_chars(begin,end,type);
          if(parsed_type.ec!=std::errc{}||parsed_type.ptr>=end||*parsed_type.ptr!=',')continue;
          const auto parsed_x=std::from_chars(parsed_type.ptr+1,end,x);
          if(parsed_x.ec!=std::errc{}||parsed_x.ptr>=end||*parsed_x.ptr!=',')continue;
          const auto parsed_y=std::from_chars(parsed_x.ptr+1,end,y);
          if(parsed_y.ec!=std::errc{})continue;
          if(parsed_y.ptr<end&&*parsed_y.ptr==','){
            const auto parsed_dx=std::from_chars(parsed_y.ptr+1,end,dx);
            if(parsed_dx.ec==std::errc{}&&parsed_dx.ptr<end&&*parsed_dx.ptr==',')
              (void)std::from_chars(parsed_dx.ptr+1,end,dy);
          }
          replay_->injected_events.push_back(
              InputEvent{static_cast<InputEventType>(type),{x,y},{dx,dy}});
          continue;
        }
        if(!input_gate_open)break;
        ++replay_->command_cursor;
        stellar::engine::RawInputEvent raw;
        if(command.name=="key_press")
          raw.kind=stellar::engine::RawInputEvent::Kind::KeyPress;
        else if(command.name=="gamepad_button")
          raw.kind=stellar::engine::RawInputEvent::Kind::GamepadButton;
        else if(command.name=="mouse_button")
          raw.kind=stellar::engine::RawInputEvent::Kind::MouseButton;
        else continue;
        int code=0;
        const auto parsed=std::from_chars(command.payload.data(),
            command.payload.data()+command.payload.size(),code);
        if(parsed.ec!=std::errc{})continue;
        raw.code=code;
        (void)input_mapper_.feed(raw);
        const auto action=first_galaxy_action_pressed();
        if(!action.empty())dispatch_galaxy_action(action,width,height);
        // Replayed presses are edge-triggered: release immediately so the next
        // recorded press of the same input fires like real input does.
        if(command.name=="key_press")
          raw.kind=stellar::engine::RawInputEvent::Kind::KeyRelease;
        else raw.pressed=false;
        (void)input_mapper_.feed(raw);
      }
      // Completeness and completion are cursor bookkeeping, not dispatch —
      // they must run even while the input gate is closed: a pointer command
      // consumed behind the gate (e.g. a menu click) can finish the stream
      // with the gate held shut, and gating these checks would hang a
      // scripted --replay-exit run with nothing left pending.
      if(replay_->divergence.empty()&&
         replay_->command_cursor>=commands.size()){
        const auto &expected=replay_->recording->checkpoints();
        if(replay_->checkpoint_cursor<expected.size()&&
           expected[replay_->checkpoint_cursor].tick<tick)
          replay_->divergence=
              "replay skipped recorded checkpoint at tick "+
              std::to_string(expected[replay_->checkpoint_cursor].tick)+
              (expected[replay_->checkpoint_cursor].label.empty()?"":
               " ("+expected[replay_->checkpoint_cursor].label+")");
      }
      // Completion: the whole recorded stream consumed, every recorded
      // checkpoint verified, no divergence — report it once so a scripted
      // run has a success signal instead of only an absence of failure.
      if(replay_->divergence.empty()&&!replay_->completion_reported&&
         replay_->command_cursor>=commands.size()&&
         replay_->checkpoint_cursor>=replay_->recording->checkpoints().size()){
        replay_->completion_reported=true;
        // Flush: scripted --replay watchers poll for this line — a buffered
        // stream would hold it hostage until the process exits.
        std::cout<<"replay_verified={\"commands\":"<<commands.size()
                 <<",\"checkpoints\":"<<replay_->verified_checkpoints<<"}\n"
                 <<std::flush;
        // --replay-exit: a scripted verification run ends here — exit 0
        // with the verified line, rather than continuing the session.
        if(replay_->exit_on_completion)return false;
      }
    }
    // --replay-exit stall detection, outside the input gate: pending
    // commands or checkpoints whose tick can never arrive (frozen
    // simulation, held-closed input gate) mean the verification cannot
    // complete — report it instead of hanging. Same 600-frame idiom as
    // the --replay-until stall guard.
    if(replay_&&replay_->recording&&session_&&replay_->exit_on_completion&&
       !replay_->completion_reported&&replay_->divergence.empty()){
      const auto&recording=*replay_->recording;
      const auto tick=replay_tick();
      const bool pending=
          replay_->command_cursor<recording.commands().size()||
          replay_->checkpoint_cursor<recording.checkpoints().size();
      const bool progressed=
          tick!=replay_->verification_last_tick||
          replay_->command_cursor!=replay_->verification_last_command||
          replay_->checkpoint_cursor!=replay_->verification_last_checkpoint;
      replay_->verification_last_tick=tick;
      replay_->verification_last_command=replay_->command_cursor;
      replay_->verification_last_checkpoint=replay_->checkpoint_cursor;
      if(!pending||progressed){
        replay_->verification_stalled_frames=0;
      }else if(++replay_->verification_stalled_frames>=600){
        throw std::runtime_error(
            "Replay verification stalled: "+
            std::to_string(recording.commands().size()-replay_->command_cursor)+
            " command(s) and "+
            std::to_string(recording.checkpoints().size()-replay_->checkpoint_cursor)+
            " checkpoint(s) pending with no progress for 600 frames — the "
            "recording cannot finish verifying.");
      }
    }
    // --replay-until: once the simulated tick reaches the requested stop,
    // dump the canonical document next to the recording. The dump is
    // canonicalized exactly like checkpoint captures so a leaf-diff against
    // <recording>.expected/<tick>.json names the diverging leaf — this is
    // the bisect companion to checkpoint divergence. Runs outside the input
    // gate so a modal that pauses command feeding cannot suppress the dump.
    if(replay_&&replay_->recording&&session_&&replay_->stop_at_tick){
      if(const auto current_tick=replay_tick();current_tick<*replay_->stop_at_tick){
        // Stall detection: a paused campaign or an exhausted command stream
        // can never reach the stop — report it instead of hanging.
        if(current_tick!=replay_->last_tick){replay_->last_tick=current_tick;replay_->stalled_frames=0;}
        else if(++replay_->stalled_frames>600){
          std::cout<<"replay-until: simulated tick stalled at "<<current_tick
                   <<" below target "<<*replay_->stop_at_tick
                   <<" for 600 frames — the recording cannot reach it\n";
          replay_->stop_at_tick.reset();
          session_->request_exit();
        }
      }else{
      const auto stop=*replay_->stop_at_tick;
      const PlayerCampaignCaptureOptions capture_options{
          session_->frame().clock().simulation_days(),STELLAR_GAME_VERSION,""};
      auto document=encode_player_campaign_v17_document(
          capture_player_campaign_v17(session_->frame().runtime(),
                                      capture_options));
      document.erase("SavedAtUtc");
      if(const auto galaxy=document.find("Galaxy");galaxy!=document.end())
        if(const auto meta=galaxy->find("GenerationMetadata");meta!=galaxy->end())
          meta->erase("CreatedAtUtc");
      std::string message="replay-until: canonical state at tick "+
          std::to_string(stop)+" dumped to ";
      if(std::ofstream out{replay_->stop_dump_path,
                           std::ios::binary|std::ios::trunc};out){
        out<<document.dump(2);
        message+=replay_->stop_dump_path.generic_string();
      }else
        message+="(write failed: "+replay_->stop_dump_path.generic_string()+")";
      // Bisect aid: when an expected sidecar exists for the stop tick, leaf-diff
      // immediately so the dump arrives with its first divergence named.
      const auto expected_path=replay_->expected_directory/
          (std::to_string(stop)+".json");
      if(std::ifstream in{expected_path,std::ios::binary};in){
        std::ostringstream contents;contents<<in.rdbuf();
        try{
          const auto leaves=stellar::engine::document_leaf_diff(
              nlohmann::ordered_json::parse(contents.str()),document);
          if(!leaves.empty()){
            const auto diff_path=replay_->expected_directory.parent_path()/
                ("replay-until-"+std::to_string(stop)+".diff.txt");
            std::ostringstream report;
            for(const auto&leaf:leaves)
              report<<leaf.path<<"\n  expected: "<<leaf.expected
                    <<"\n  actual:   "<<leaf.actual<<'\n';
            if(std::ofstream out{diff_path,std::ios::binary|std::ios::trunc};out)
              message+=" (first leaf: "+leaves.front().path+
                  "; full diff in "+diff_path.generic_string()+")";
          }else
            message+=" (matches expected sidecar)";
        }catch(const std::exception&){
          // A corrupt expected sidecar still leaves the plain dump.
        }
      }
      std::cout<<message<<'\n';
      replay_->stop_at_tick.reset();
      session_->request_exit();
      }
    }
    // Replayed pointer commands merge ahead of this frame's live events —
    // they dispatch through the identical handlers in recorded order.
    std::vector<InputEvent> frame_events;
    const std::vector<InputEvent> *frame_event_stream=&input.events;
    if(replay_&&!replay_->injected_events.empty()){
      frame_events.reserve(replay_->injected_events.size()+input.events.size());
      frame_events.insert(frame_events.end(),replay_->injected_events.begin(),
                          replay_->injected_events.end());
      replay_->injected_events.clear();
      frame_events.insert(frame_events.end(),input.events.begin(),input.events.end());
      frame_event_stream=&frame_events;
    }
    for(const auto &event:*frame_event_stream){
      // Journal pointer input: positions (and drag deltas) are what dispatch
      // consumes, so the recording stores the raw event and replay re-runs
      // hit-testing against reproduced state instead of trusting a resolved
      // action name. Moves record only while a button is held — accumulated
      // drag motion decides drag-vs-click at release; hovers are cosmetic.
      if(replay_&&replay_->recorder){
        bool journal=false;
        switch(event.type){
        case InputEventType::LeftPressed:case InputEventType::RightPressed:
          ++replay_->pointer_held;journal=true;break;
        case InputEventType::LeftReleased:case InputEventType::RightReleased:
          if(replay_->pointer_held>0)--replay_->pointer_held;
          journal=true;break;
        case InputEventType::PointerCancelled:
          replay_->pointer_held=0;journal=true;break;
        case InputEventType::PointerMove:
          journal=replay_->pointer_held>0;break;
        default:break;
        }
        if(journal){
          char payload[96];
          const auto written=std::snprintf(payload,sizeof payload,
              "%d,%.9g,%.9g,%.9g,%.9g",static_cast<int>(event.type),
              static_cast<double>(event.position.x),static_cast<double>(event.position.y),
              static_cast<double>(event.delta.x),static_cast<double>(event.delta.y));
          if(written>0)
            replay_->recorder->record(replay_tick(),"pointer_button",
                std::string(payload,static_cast<std::size_t>(written)));
        }
      }
      if(developer_session()){
        const int stellar_activity_focus_before=stellar_activity_panel_.focus();
        if(stellar_activity_panel_.handle(event,width,height,session_->frame())){
          focus_stellar_activity(width,height);
          if(stellar_activity_panel_.focus()!=stellar_activity_focus_before)
            announcer_.announce_focus(stellar_activity_panel_.focused_label(width,height),
              announcement_bounds(stellar_activity_panel_.focused_bounds(width,height)),
              std::nullopt,stellar_activity_panel_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int developer_empires_focus_before=developer_empires_.focus();
        if(developer_empires_.handle(event,width,height,session_->frame())){
          if(const auto id=developer_empires_.take_focus_request()){
            system_workspace_.close();colony_workspace_.close();settlement_workspace_.clear();colony_entry_view_.reset();
            if(const auto it=session_->cache().systems_by_id.find(*id);it!=session_->cache().systems_by_id.end()){
              camera_.center={it->second->position.x,it->second->position.y};
              camera_.pixels_per_world=std::max(camera_.pixels_per_world,fitted_pixels_per_world_*8.);
              selected_id_=*id;constrain_galaxy_camera(width,height);
            }
            if(menu_)toggle_menu();
          }
          if(developer_empires_.focus()!=developer_empires_focus_before)
            announcer_.announce_focus(developer_empires_.focused_label(width,height),
              announcement_bounds(developer_empires_.focused_bounds(width,height)),
              std::nullopt,developer_empires_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int developer_diagnostics_focus_before=developer_diagnostics_.focus();
        if(developer_diagnostics_.handle(event,width,height,developer_monitor_)){
          if(developer_diagnostics_.focus()!=developer_diagnostics_focus_before)
            announcer_.announce_focus(developer_diagnostics_.focused_label(width,height),
              announcement_bounds(developer_diagnostics_.focused_bounds(width,height)),
              std::nullopt,developer_diagnostics_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int giant_test_focus_before=giant_test_panel_.focus();
        if(giant_test_panel_.handle(event,width,height,session_->frame())){
          if(giant_test_panel_.focus()!=giant_test_focus_before)
            announcer_.announce_focus(giant_test_panel_.focused_label(width,height),
              announcement_bounds(giant_test_panel_.focused_bounds(width,height)),
              std::nullopt,giant_test_panel_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int developer_planet_index_focus_before=developer_planet_index_.focus();
        if(developer_planet_index_.handle(event,width,height,session_->frame())){
          if(developer_planet_index_.take_giant_request()){giant_test_panel_.open(session_->frame());refresh_knowledge();}
          if(const auto target=developer_planet_index_.take_focus_request()){
            if(menu_)toggle_menu();
            if(enter_system(target->first,width,height)&&system_workspace_.select_body(target->second)){
              system_workspace_.focus_selected_body(width,height);refresh_colony_entry(true);open_colony_from_system(target->second);
            }
          }
          if(developer_planet_index_.focus()!=developer_planet_index_focus_before)
            announcer_.announce_focus(developer_planet_index_.focused_label(width,height),
              announcement_bounds(developer_planet_index_.focused_bounds(width,height)),
              std::nullopt,developer_planet_index_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int developer_index_focus_before=developer_index_.focus();
        if(developer_index_.handle(event,width,height,session_->frame())){
          if(const auto target=developer_index_.take_focus_request()){
            colony_roster_.close();economy_workspace_.close();supply_workspace_.close();
            research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
            diplomacy_workspace_.close();colony_workspace_.close();
            settlement_workspace_.clear();colony_entry_view_.reset();system_workspace_.close();
            camera_.center={target->x,target->y};
            camera_.pixels_per_world=std::max(camera_.pixels_per_world,fitted_pixels_per_world_*8.);
            selected_id_=target->system_id;constrain_galaxy_camera(width,height);
            if(menu_)toggle_menu();
          }
          if(developer_index_.focus()!=developer_index_focus_before)
            announcer_.announce_focus(developer_index_.focused_label(width,height),
              announcement_bounds(developer_index_.focused_bounds(width,height)),
              std::nullopt,developer_index_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()){
        const int developer_panel_focus_before=developer_panel_.focus();
        if(developer_panel_.handle(event,width,height,session_->frame())){
          if(developer_panel_.take_stellar_request()){
            stellar_activity_panel_.open(session_->frame(),selected_id_);focus_stellar_activity(width,height);
          }
          if(developer_panel_.take_empires_request())developer_empires_.open(session_->frame());
          if(developer_panel_.take_reveal_request()){
            fully_explore_developer_galaxy(session_->frame().runtime().world().campaign());
            refresh_knowledge();territory_refresh_elapsed_=.5;refresh_diplomacy(true);
          }
          if(developer_panel_.take_diagnostics_request())developer_diagnostics_.open(developer_monitor_);
          if(developer_panel_.take_export_request())request_support(width,height);
          if(developer_panel_.take_planet_index_request())developer_planet_index_.open(session_->frame().runtime().world().campaign());
          if(developer_panel_.take_index_request())developer_index_.open(session_->frame().runtime().world().campaign());
          if(developer_panel_.focus()!=developer_panel_focus_before)
            announcer_.announce_focus(developer_panel_.focused_label(width,height,session_->frame()),
              announcement_bounds(developer_panel_.focused_bounds(width,height,session_->frame())),
              std::nullopt,developer_panel_.focused_control(width,height,session_->frame()));
          gesture_.capture_for_ui();continue;
        }
      }
      const bool hover_blocked=settings_visible()||(!menu_&&(battle_workspace_.visible()||settlement_workspace_.visible()||colony_workspace_.planetary_modal()||diplomacy_workspace_.modal_open()||shipyard_workspace_.confirmation_open()||construction_workspace_.confirmation_open()||fleet_workspace_.preview()));
      const auto hover_action=(!hover_blocked&&input.focused&&input.renderable())?layout.hit(event.position,menu_):UiAction::None;
      menu_hover_feedback_.update(event,static_cast<std::uint64_t>(hover_action));
      if(session_->new_campaign_pending()) break;
      if(event.type==InputEventType::PointerCancelled){settlement_workspace_.cancel_pending_input();(void)colony_roster_.handle(event,width,height);fleet_workspace_.cancel_recovery();colony_workspace_.cancel_freight();outpost_freight_controller_.clear();}
      if(voice_settings_&&voice_settings_->visible()){
        const int focus_before=voice_settings_->focused();
        (void)voice_settings_->handle(event,width,height);
        if(voice_settings_->focused()!=focus_before)announcer_.announce_focus(voice_settings_->focused_label(),announcement_bounds(voice_settings_->focused_bounds(width,height)),voice_settings_->focused_range(),voice_settings_->focused_control());
        gesture_.capture_for_ui();continue;
      }
      if(settings_hub_){
        const int focus_before=settings_hub_->focused();
        if(settings_hub_->handle(event,width,height)){
          if(settings_hub_->focused()!=focus_before)announcer_.announce_focus(settings_hub_->focused_label(),announcement_bounds(settings_hub_->focused_bounds(width,height)));
          if(auto notice=settings_hub_->take_notice();!notice.empty())announcer_.announce(std::move(notice));
          gesture_.capture_for_ui();continue;
        }
      }
      if(general_settings_&&general_settings_->visible()){
        notification_view_.close();chronicle_view_.close();
        const int focus_before=general_settings_->focused();
        (void)general_settings_->handle(event,width,height);
        if(general_settings_->focused()!=focus_before)announcer_.announce_focus(general_settings_->focused_label(),announcement_bounds(general_settings_->focused_bounds(width,height)));
        gesture_.capture_for_ui();
        continue;
      }
      if(video_settings_&&video_settings_->visible()){
        notification_view_.close();chronicle_view_.close();
        const int focus_before=video_settings_->focused();
        (void)video_settings_->handle(event,width,height);
        if(video_settings_->focused()!=focus_before)announcer_.announce_focus(video_settings_->focused_label(width,height),announcement_bounds(video_settings_->focused_bounds(width,height)));
        gesture_.capture_for_ui();
        continue;
      }
      if(audio_settings_&&audio_settings_->visible()){
        notification_view_.close();chronicle_view_.close();
        const int focus_before=audio_settings_->focused();
        (void)audio_settings_->handle(event,width,height);
        if(audio_settings_->focused()!=focus_before)announcer_.announce_focus(audio_settings_->focused_label(),announcement_bounds(audio_settings_->focused_bounds(width,height)),audio_settings_->focused_range(),audio_settings_->focused_control());
        gesture_.capture_for_ui();
        continue;
      }
      if(menu_){
        if(event.type==InputEventType::LeftPressed||event.type==InputEventType::PointerCancelled)menu_focus_=-1;
        if(event.type==InputEventType::KeyPressed&&event.key){
          constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u,kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u,kHome=0x4000004au,kEnd=0x4000004du;
          const bool backward=event.key==kLeft||event.key==kUp||(event.key==kTab&&event.shift);
          if(event.key==kTab||event.key==kRight||event.key==kDown||backward){
            menu_focus_=menu_focus_<0?0:(backward?menu_focus_+menu_action_count-1:menu_focus_+1)%menu_action_count;
            menu_hover_feedback_.cue(static_cast<std::uint64_t>(menu_actions_[menu_focus_]));
            announce_menu_focus(width,height);
            gesture_.capture_for_ui();continue;
          }
          if(event.key==kHome||event.key==kEnd){
            menu_focus_=event.key==kHome?0:menu_action_count-1;
            menu_hover_feedback_.cue(static_cast<std::uint64_t>(menu_actions_[menu_focus_]));
            announce_menu_focus(width,height);
            gesture_.capture_for_ui();continue;
          }
          if((event.key==kReturn||event.key==kSpace)&&menu_focus_>=0&&menu_focus_<menu_action_count){
            if(audio_confirm_)audio_confirm_();
            activate_menu_action(menu_actions_[menu_focus_],width,height);
            gesture_.capture_for_ui();continue;
          }
          gesture_.capture_for_ui();continue;
        }
      }
      if(!map_hud_visible()){hud_switch_pressed_=false;assets_.cancel_input();map_focus_group_=-1;hud_focus_=-1;}
      if(map_hud_visible()){
        if(event.type==InputEventType::LeftPressed||event.type==InputEventType::PointerCancelled){map_focus_group_=-1;hud_focus_=-1;}
        if(event.type==InputEventType::KeyPressed&&event.key){
          constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u,kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u,kHome=0x4000004au,kEnd=0x4000004du;
          const bool bwd=event.key==kLeft||event.key==kUp||(event.key==kTab&&event.shift);
          const bool nav=event.key==kTab||event.key==kRight||event.key==kDown||bwd||event.key==kHome||event.key==kEnd;
          const bool activate=event.key==kReturn||event.key==kSpace;
          if(nav||activate){
            // Always-on map focus groups: assets navigator -> fleet workspace
            // -> HUD chrome. A nav key that would wrap a group's boundary
            // releases the ring so the same key can land in the next group;
            // activation keys only reach the group currently holding focus.
            if(map_focus_group_>=0&&(map_focus_group_==0?assets_.focus():map_focus_group_==1?fleet_workspace_.focus():hud_focus_)<0)map_focus_group_=-1;
            const auto hud_items=hud_ring_items(layout,width,height);
            const auto send_map_key=[&](int g)->bool{
              if(g==0){
                const auto command=assets_.handle(event,width,height);
                if(command.captured&&nav&&!activate)announcer_.announce_focus(assets_.focused_label(width,height),announcement_bounds(assets_.focused_bounds(width,height)),std::nullopt,assets_.focused_control(width,height));
                return command.captured;
              }
              if(g==1){
                const auto markers=fleet_markers(width,height);
                const auto fleet_command=fleet_workspace_.handle(event,width,height,markers,std::nullopt);
                if(fleet_command.kind==FleetWorkspaceCommandKind::OpenColony)open_overview_colony(fleet_command.colony_id,width,height);
                else handle_fleet_command(fleet_command);
                if(fleet_command.captured&&nav&&!activate){const auto fl=fleet_workspace_.layout(width,height);announcer_.announce_focus(fleet_workspace_.focused_label(fl),announcement_bounds(fleet_workspace_.focused_bounds(fl)));}
                return fleet_command.captured;
              }
              const int count=static_cast<int>(hud_items.size());
              if(event.key==kHome||event.key==kEnd)hud_focus_=event.key==kHome?0:count-1;
              else if(nav&&!activate){
                if(hud_focus_<0||hud_focus_>=count)hud_focus_=bwd?count-1:0;
                else{const int next=hud_focus_+(bwd?-1:1);if(next<0||next>=count){hud_focus_=-1;return false;}hud_focus_=next;}
              }
              else if(activate&&hud_focus_>=0&&hud_focus_<count){
                const auto action=hud_items[static_cast<std::size_t>(hud_focus_)].second;
                if(audio_confirm_)audio_confirm_();
                if(action==UiAction::Pause){if(session_->frame().clock().speed()==StrategicSpeed::Paused)session_->frame().clock().resume();else session_->frame().clock().set_speed(StrategicSpeed::Paused);}
                else if(action==UiAction::Speed)cycle_speed();
                else if(action==UiAction::Notifications){if(notifications_available())notification_view_.toggle(notifications_.latest_sequence());}
                else if(action==UiAction::Menu)toggle_menu();
                else if(action==UiAction::SwitchView)activate_hud_switch(width,height);
                else route_navigation(action);
                return true;
              }
              else return false;
              menu_hover_feedback_.cue(static_cast<std::uint64_t>(hud_items[static_cast<std::size_t>(hud_focus_)].second));
              announcer_.announce_focus(hud_action_label(hud_items[static_cast<std::size_t>(hud_focus_)].second),announcement_bounds(hud_items[static_cast<std::size_t>(hud_focus_)].first),std::nullopt,stellar::engine::AnnouncementControl::Button);
              return true;
            };
            if(activate){
              if(map_focus_group_>=0&&send_map_key(map_focus_group_)){gesture_.capture_for_ui();continue;}
            }else{
              int g=map_focus_group_>=0?map_focus_group_:(bwd?2:0);
              bool claimed=false;
              for(int tries=0;tries<3;++tries){
                if(send_map_key(g)){map_focus_group_=g;claimed=true;break;}
                g=(g+(bwd?2:1))%3;
              }
              if(!claimed)map_focus_group_=-1;
              // Nav keys on the clean map belong to the rings — never fall
              // through to the raw handlers, claimed or not.
              gesture_.capture_for_ui();continue;
            }
          }
        }
        const auto asset_command=assets_.handle(event,width,height);
        if(asset_command.captured){if(asset_command.key)execute_asset(asset_command,width,height);gesture_.capture_for_ui();continue;}
        const auto hud=CommandHudLayout::make(width,height);
        if(event.type==InputEventType::PointerCancelled)hud_switch_pressed_=false;
        if(event.type==InputEventType::LeftReleased&&hud_switch_pressed_){
          hud_switch_pressed_=false;
          if(hud.switch_view.contains(event.position)){
            activate_hud_switch(width,height);
            if(audio_confirm_)audio_confirm_();
          }gesture_.cancel();continue;
        }
        if(hud.resource_strip.contains(event.position)||hud.context.contains(event.position)){
          if(event.type==InputEventType::LeftPressed&&layout.hit(event.position,false)==UiAction::None){hud_switch_pressed_=hud.switch_view.contains(event.position);gesture_.capture_for_ui();continue;}
          if(event.type==InputEventType::Wheel||event.type==InputEventType::RightPressed||event.type==InputEventType::PointerMove){gesture_.capture_for_ui();continue;}
        }
      }
      if(battle_workspace_.visible()&&!menu_){
        if(event.type==InputEventType::KeyPressed&&event.key==0x4000003fu&&
           input.focused&&input.renderable())session_->request_save();
        else{
          const int focus_before=battle_workspace_.focus();
          execute_battle(battle_workspace_.handle(event,width,height));
          if(battle_workspace_.focus()!=focus_before)
            {const auto battle_layout=stellar::native_battle_ui::BattleWorkspaceLayout::for_viewport(width,height);
            announcer_.announce_focus(battle_workspace_.focused_label(battle_layout),announcement_bounds(battle_workspace_.focused_bounds(battle_layout)));}
        }
        gesture_.capture_for_ui();continue;
      }
      if(event.type==InputEventType::KeyPressed&&event.key==0x40000041u&&
         input.focused&&input.renderable()&&!wants_text_input()&&
         !settlement_workspace_.visible()&&!(colony_workspace_.planetary_modal())&&
         !diplomacy_workspace_.modal_open()&&!shipyard_workspace_.confirmation_open()&&
         !construction_workspace_.confirmation_open()&&!fleet_workspace_.preview()){
        request_support(width,height);
        continue;
      }
      const bool can_notify=notifications_available();
      if(!can_notify){notification_view_.close();chronicle_view_.close();}
      if(can_notify&&notification_view_.visible()){
        const int focus_before=notification_view_.focus();
        const auto command=notification_view_.handle(event,notifications_.items(),width,height);
        if(command.captured&&notification_view_.focus()!=focus_before)
          announcer_.announce_focus(notification_view_.focused_label(notifications_.items(),width,height),announcement_bounds(notification_view_.focused_bounds(notifications_.items(),width,height)));
        if(command.kind==stellar::native_notifications::NotificationViewCommandKind::OpenDiplomaticContact){
          system_workspace_.close();colony_workspace_.close();
          research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
          colony_roster_.close();economy_workspace_.close();supply_workspace_.close();diplomacy_workspace_.open();refresh_diplomacy(true);
          if(!diplomacy_workspace_.select_contact_civilization(command.civilization_id))
            diplomacy_workspace_.set_notice(tr("DIPLOMACY_NOTICE_UNIDENTIFIED","This contact is no longer identified. Review the contact list."),false);
          refresh_diplomacy(true);
        }
        if(command.kind==stellar::native_notifications::NotificationViewCommandKind::OpenSystem){
          (void)enter_system(command.system_id,width,height);
        }
        if(command.kind==stellar::native_notifications::NotificationViewCommandKind::OpenChronicle){
          notification_view_.close();
          chronicle_view_.open(session_->frame().runtime().history(),
              session_->frame().runtime().world().campaign().player_civilization_id);
          if(audio_confirm_)audio_confirm_();
          gesture_.capture_for_ui();continue;
        }
        if(command.captured){gesture_.capture_for_ui();continue;}
      }
      if(can_notify&&chronicle_view_.visible()){
        const int focus_before=chronicle_view_.focus();
        if(chronicle_view_.handle(event,width,height)){
          if(chronicle_view_.focus()!=focus_before)
            announcer_.announce_focus(chronicle_view_.focused_label(width,height),announcement_bounds(chronicle_view_.focused_bounds(width,height)),std::nullopt,chronicle_view_.focused_control(width,height));
          if(const auto nav=chronicle_view_.navigation()){
            chronicle_view_.close();
            (void)enter_system(static_cast<int>(*nav),width,height);
          }
          if(const auto contact=chronicle_view_.contact_navigation()){
            chronicle_view_.close();
            system_workspace_.close();colony_workspace_.close();
            research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();
            colony_roster_.close();economy_workspace_.close();supply_workspace_.close();diplomacy_workspace_.open();refresh_diplomacy(true);
            if(!diplomacy_workspace_.select_contact_civilization(static_cast<int>(*contact)))
              diplomacy_workspace_.set_notice(tr("DIPLOMACY_NOTICE_UNIDENTIFIED","This contact is no longer identified. Review the contact list."),false);
            refresh_diplomacy(true);
          }
          gesture_.capture_for_ui();continue;
        }
      }
      if(can_notify&&event.type==InputEventType::LeftPressed&&
         layout.notifications.contains(event.position)){
        notification_view_.toggle(notifications_.latest_sequence());
        if(audio_confirm_)audio_confirm_();
        gesture_.begin(true);continue;
      }
      // Strategic shortcuts precede system-view capture, but never take keys
      // from text entry, a confirmation or a gameplay-blocking view.
      if(developer_session()&&event.type==InputEventType::KeyPressed&&event.control&&event.alt&&event.key=='b'){background_debug_.toggle(system_background_);gesture_.capture_for_ui();continue;}
      if(developer_session()){
        const int background_debug_focus_before=background_debug_.focus();
        if(background_debug_.handle(event,width,height,system_background_)){
          if(auto id=background_debug_.navigation())(void)enter_system(*id,width,height);
          if(background_debug_.focus()!=background_debug_focus_before)
            announcer_.announce_focus(background_debug_.focused_label(width,height,system_background_),
              announcement_bounds(background_debug_.focused_bounds(width,height)),
              std::nullopt,background_debug_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      if(developer_session()&&event.type==InputEventType::KeyPressed&&event.control&&event.alt&&event.key=='n'){phenomena_debug_.toggle();gesture_.capture_for_ui();continue;}
      if(developer_session()){
        const int phenomena_debug_focus_before=phenomena_debug_.focus();
        if(phenomena_debug_.handle(event,width,height)){
          if(const auto* field=phenomena_.field())if(auto index=phenomena_debug_.take_navigation(field->regions.size())){
            const auto& r=field->regions[*index];system_workspace_.close();camera_.center={r.shape.x,r.shape.y};camera_.pixels_per_world=std::max(galaxy_overview_camera(width,height).pixels_per_world,std::min(width,height)/(5*std::max(r.shape.extent_x,r.shape.extent_y)));selected_id_.reset();refresh_inspection();
          }
          if(phenomena_debug_.focus()!=phenomena_debug_focus_before)
            announcer_.announce_focus(phenomena_debug_.focused_label(width,height),
              announcement_bounds(phenomena_debug_.focused_bounds(width,height)),
              std::nullopt,phenomena_debug_.focused_control(width,height));
          gesture_.capture_for_ui();continue;
        }
      }
      // Key releases always reach the input mapper so held state clears even
      // when a menu or workspace suppressed the matching press.
      if(event.type==InputEventType::KeyReleased){
        stellar::engine::RawInputEvent raw;
        raw.kind=stellar::engine::RawInputEvent::Kind::KeyRelease;
        raw.code=event.key;
        (void)input_mapper_.feed(raw);
        continue;
      }
      // Reference keyboard shortcuts (Main.cs): resolved through the GALAXY
      // input context. Suppressed while the menu, surface view, or diplomacy
      // blocks gameplay input, or a text field owns the keyboard.
      if(event.type==InputEventType::KeyPressed&&input.focused&&input.renderable()&&
         !menu_&&!diplomacy_workspace_.visible()&&
         !colony_workspace_.planetary_modal()&&
         !settlement_workspace_.visible()&&!wants_text_input()&&
         !wants_keyboard_focus()&&
         !shipyard_workspace_.confirmation_open()&&
         !construction_workspace_.confirmation_open()&&!fleet_workspace_.preview()){
        stellar::engine::RawInputEvent raw;
        raw.kind=stellar::engine::RawInputEvent::Kind::KeyPress;
        raw.code=event.key;
        bool handled=input_mapper_.feed(raw);
        if(handled){
          const auto action=first_galaxy_action_pressed();
          if(action.empty())handled=false;
          else{
            if(replay_&&replay_->recorder)
              replay_->recorder->record(replay_tick(),"key_press",
                                        std::to_string(event.key));
            dispatch_galaxy_action(action,width,height);
          }
        }
        if(handled){
          if(smoke_keyboard_commands_)++*smoke_keyboard_commands_;
          if(audio_confirm_)audio_confirm_();
          continue;
        }
      }
      // Pad releases and axis state always reach the mapper — like key
      // releases — so held bindings clear and stick values stay live even
      // when a menu suppressed the matching press.
      if(event.type==InputEventType::GamepadReleased){
        stellar::engine::RawInputEvent raw;
        raw.kind=stellar::engine::RawInputEvent::Kind::GamepadButton;
        raw.code=event.gamepad_button;raw.device=event.gamepad_device;raw.pressed=false;
        (void)input_mapper_.feed(raw);continue;
      }
      if(event.type==InputEventType::GamepadAxis){
        stellar::engine::RawInputEvent raw;
        raw.kind=stellar::engine::RawInputEvent::Kind::GamepadAxis;
        raw.code=event.gamepad_axis;raw.device=event.gamepad_device;raw.value=event.gamepad_axis_value;
        (void)input_mapper_.feed(raw);continue;
      }
      if(event.type==InputEventType::RightReleased){
        // Feed but never consume — map right-gesture release handling still
        // needs the event; this only clears mapper held-state.
        stellar::engine::RawInputEvent raw;
        raw.kind=stellar::engine::RawInputEvent::Kind::MouseButton;
        raw.code=3;raw.pressed=false;
        (void)input_mapper_.feed(raw);
      }
      // Pad presses and right-clicks resolve through GALAXY under the same
      // gameplay gate as keys, so bindings captured in the Controls view
      // actually fire. Left click stays the universal UI gesture — the
      // rebind UI never captures it.
      if((event.type==InputEventType::GamepadPressed||
          event.type==InputEventType::RightPressed)&&
         input.focused&&input.renderable()&&
         !menu_&&!diplomacy_workspace_.visible()&&
         !colony_workspace_.planetary_modal()&&
         !settlement_workspace_.visible()&&!wants_text_input()&&
         !wants_keyboard_focus()&&
         !shipyard_workspace_.confirmation_open()&&
         !construction_workspace_.confirmation_open()&&!fleet_workspace_.preview()){
        stellar::engine::RawInputEvent raw;
        const char *record_name="mouse_button";
        if(event.type==InputEventType::GamepadPressed){
          raw.kind=stellar::engine::RawInputEvent::Kind::GamepadButton;
          raw.code=event.gamepad_button;raw.device=event.gamepad_device;
          record_name="gamepad_button";
        }else{
          raw.kind=stellar::engine::RawInputEvent::Kind::MouseButton;
          raw.code=3;
        }
        bool handled=input_mapper_.feed(raw);
        if(handled){
          const auto action=first_galaxy_action_pressed();
          if(action.empty())handled=false;
          else{
            // Right-clicks already journal as pointer_button events at the
            // top of the loop — recording mouse_button too would dispatch
            // the binding twice on replay (the injected event reaches this
            // same feed). Pad buttons carry no position, so they keep the
            // binding-level record.
            if(replay_&&replay_->recorder&&event.type==InputEventType::GamepadPressed)
              replay_->recorder->record(replay_tick(),record_name,
                                        std::to_string(raw.code));
            dispatch_galaxy_action(action,width,height);
          }
        }
        if(handled){
          if(smoke_keyboard_commands_)++*smoke_keyboard_commands_;
          if(audio_confirm_)audio_confirm_();
          continue;
        }
      }
      // The missions board is a floating panel — it owns its pointer input
      // and routes its commands to the authoritative paths (fleet focus,
      // colony open, planetary surface entry, outpost freight review).
      // Keyboard events reach the view first: a live focus ring consumes
      // Escape to release itself; otherwise the panel closes.
      if(mission_view_.visible()&&!menu_){
        const int focus_before=mission_view_.focus();
        const auto command=mission_view_.handle(event,mission_board_,mission_fleets_,mission_colonies_,width,height);
        if(command.captured){
          if(mission_view_.focus()!=focus_before)
            announcer_.announce_focus(mission_view_.focused_label(mission_board_,mission_fleets_,mission_colonies_,width,height),announcement_bounds(mission_view_.focused_bounds(mission_board_,mission_fleets_,mission_colonies_,width,height)),std::nullopt,stellar::engine::AnnouncementControl::Button);
          if(command.kind==native_missions::MissionViewCommandKind::Close)mission_view_.close();
          else if(command.kind==native_missions::MissionViewCommandKind::FocusFleet)focus_mission_fleet(command.fleet_id);
          else if(command.kind==native_missions::MissionViewCommandKind::OpenColony){mission_view_.close();open_overview_colony(command.colony_id,width,height);}
          else if(command.kind==native_missions::MissionViewCommandKind::LandColony){mission_view_.close();open_mission_colony(command.colony_id,width,height,false);}
          else if(command.kind==native_missions::MissionViewCommandKind::CollectOutpostFreight){mission_view_.close();open_mission_colony(command.colony_id,width,height,true);}
          gesture_.cancel();continue;
        }
        if(event.type==InputEventType::EscapePressed||event.type==InputEventType::PointerCancelled){mission_view_.close();continue;}
      }
      const auto modal_blocks_navigation=!native_navigation_available(
          menu_,settlement_workspace_.visible(),diplomacy_workspace_.modal_open(),
          (colony_workspace_.planetary_modal()),
          settings_visible());
      if(event.type==InputEventType::LeftPressed&&!modal_blocks_navigation){
        const auto action=layout.hit(event.position,false);
        const auto navigation=action==UiAction::Map||action==UiAction::Home||action==UiAction::Inspect||action==UiAction::ZoomIn||action==UiAction::ZoomOut||action==UiAction::Explore||action==UiAction::Menu||action==UiAction::Research||
                              action==UiAction::Shipyard||
                              action==UiAction::Construction||
                              action==UiAction::Diplomacy||action==UiAction::Supply||action==UiAction::Economy||action==UiAction::Colonies||action==UiAction::Missions;
        if(navigation){
          if(audio_confirm_)audio_confirm_();
          route_navigation(action);
          gesture_.begin(true);
          continue;
        }
      }
      if(!menu_&&layout.navigation_bar.contains(event.position)&&
         (event.type==InputEventType::LeftPressed||event.type==InputEventType::LeftReleased||
          event.type==InputEventType::RightPressed||event.type==InputEventType::RightReleased||
          event.type==InputEventType::PointerMove||event.type==InputEventType::Wheel)){
        gesture_.capture_for_ui();continue;
      }
      if(colony_roster_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const int focus_before=colony_roster_.focus();
          const auto command=colony_roster_.handle(event,width,height);
          if(command.captured&&colony_roster_.focus()!=focus_before)
            announcer_.announce_focus(colony_roster_.focused_label(width,height),announcement_bounds(colony_roster_.focused_bounds(width,height)),std::nullopt,colony_roster_.focused_control(width,height));
          if(command.refresh)refresh_roster(true);
          else if(command.open_colony_id)open_roster_colony(command,width,height);
          if(command.captured){gesture_.cancel();continue;}
        }
      }
      if(economy_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          using stellar::native_economy::EconomyCommandKind;
          const int focus_before=economy_workspace_.focus();
          const auto command=economy_workspace_.handle(event,economy_controller_.view(),width,height);
          if(command.captured&&economy_workspace_.focus()!=focus_before)
            announcer_.announce_focus(economy_workspace_.focused_label(economy_controller_.view()),announcement_bounds(economy_workspace_.focused_bounds(width,height)));
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
          const int focus_before=supply_workspace_.focus();
          const auto command=supply_workspace_.handle(event,supply_controller_.view(),width,height);
          if(command.captured&&supply_workspace_.focus()!=focus_before)
            announcer_.announce_focus(supply_workspace_.focused_label(supply_controller_.view()),announcement_bounds(supply_workspace_.focused_bounds(width,height)));
          if(command.refresh){refresh_supply(true,true);if(audio_confirm_)audio_confirm_();}
          if(command.captured){gesture_.cancel();continue;}
        }
      }

      if(settlement_workspace_.visible()&&!menu_){
        const int focus_before=settlement_workspace_.focus();
        const auto command=settlement_workspace_.handle(event,width,height);
        if(command.captured&&settlement_workspace_.focus()!=focus_before)
          announcer_.announce_focus(settlement_workspace_.focused_label(),announcement_bounds(settlement_workspace_.focused_bounds(width,height)));
        if(command.kind==SettlementWorkspaceCommandKind::Confirm)
          execute_settlement();
        if(command.captured)continue;
      }
      if(colony_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        if(colony_workspace_.freight_preview()||(top_action!=UiAction::Pause&&top_action!=UiAction::Speed)){
          const int focus_before=colony_workspace_.focus();
          const auto command=colony_workspace_.handle(event,width,height);
          if(command.captured&&colony_workspace_.focus()!=focus_before)
            announcer_.announce_focus(colony_workspace_.focused_label(),announcement_bounds(colony_workspace_.focused_bounds(width,height)),std::nullopt,colony_workspace_.focused_control());
          if(command.kind==ColonyWorkspaceCommandKind::Close)gesture_.cancel();
          else if(command.kind==ColonyWorkspaceCommandKind::Planetary)execute_planetary(command.planetary);
          else if(command.kind==ColonyWorkspaceCommandKind::ReviewFreight || command.kind==ColonyWorkspaceCommandKind::ConfirmFreight || command.kind==ColonyWorkspaceCommandKind::CancelFreight)
            execute_colony_freight(command);
          if(command.captured)continue;
        }
      }
      if(system_workspace_.visible()&&!colony_workspace_.visible()&&!shipyard_workspace_.visible()&&!menu_){
        const auto top_action=event.type==InputEventType::LeftPressed
                                  ?layout.hit(event.position,false):UiAction::None;
        if(top_action!=UiAction::Pause&&top_action!=UiAction::Speed){
          const int focus_before=system_workspace_.focused();
          const auto command=system_workspace_.handle(event,width,height);
          if(command.captured&&system_workspace_.focused()!=focus_before)
            announcer_.announce_focus(system_workspace_.focused_label(width,height),announcement_bounds(system_workspace_.focused_bounds(width,height)));
          if(command.kind==SystemWorkspaceCommandKind::close){system_workspace_.close();gesture_.cancel();}
          else if(command.kind==SystemWorkspaceCommandKind::toggle_motion){
            auto& clock=session_->frame().clock();if(clock.speed()==StrategicSpeed::Paused)clock.resume();else clock.set_speed(StrategicSpeed::Paused);
          }
          else if(command.kind==SystemWorkspaceCommandKind::spawn_small_body_field){
            try{
              auto& world=session_->frame().runtime().world().campaign();
              const auto id=force_developer_small_body_field(world,*system_workspace_.system_id(),static_cast<SmallBodyFieldType>(command.target_id),session_->frame().clock().simulation_days(),system_workspace_.selected_body_id().value_or(-1));
              refresh_system(true);
              const auto& fields=system_workspace_.snapshot()->small_body_fields;
              const auto found=std::ranges::find(fields,id,&SmallBodyField::id);
              if(found!=fields.end())system_workspace_.inspect_small_body(static_cast<std::size_t>(found-fields.begin()));
              system_workspace_.set_notice(tr("SYSTEM_NOTICE_FIELD","Field added to this developer campaign."));
            }catch(const std::exception& error){system_workspace_.set_notice(error.what());}
          }
          else if(command.kind==SystemWorkspaceCommandKind::select_fleet){const auto selected=fleet_controller_.select_next_hit(session_->frame(),session_->cache().generation,command.hit_fleet_ids);system_workspace_.set_notice(selected.message);refresh_fleets(true);refresh_system_travel(true);}
          else if(command.kind==SystemWorkspaceCommandKind::open_destination){if(!enter_system(command.target_id,width,height))system_workspace_.set_notice(tr("SYSTEM_NOTICE_DESTINATION","Destination details are not available to this observer."));}
          else if(command.kind==SystemWorkspaceCommandKind::reconnaissance_required){scientist_voice(stellar::native_audio::VoiceCue::ReconnaissanceRequired);}
           else if(command.kind==SystemWorkspaceCommandKind::open_colony){open_colony_from_system(command.target_id);}
           else if(command.kind==SystemWorkspaceCommandKind::settlement_target){preview_settlement(command.target_id,width,height);}
           else if(command.kind==SystemWorkspaceCommandKind::open_orbital_shipyard){
             refresh_assets();const auto key=stellar::native_assets::Key{stellar::native_assets::Category::Shipyards,command.target_id};
             if(std::ranges::any_of(assets_.view().rows,[&](const auto& r){return r.key==key&&r.system_id==system_workspace_.system_id();})){assets_.set_selection(key,false);shipyard_workspace_.open();refresh_shipyard(true);}
           }
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
        if(map_focus_group_==0){assets_.cancel_input();map_focus_group_=-1;}
        else if(map_focus_group_==1){fleet_workspace_.reset_focus();map_focus_group_=-1;}
        else if(map_focus_group_==2){hud_focus_=-1;map_focus_group_=-1;}
        else if(diplomacy_workspace_.modal_open())diplomacy_workspace_.dismiss_modal();
        else if(diplomacy_workspace_.visible())diplomacy_workspace_.close();
        else if(colony_workspace_.visible())colony_workspace_.close();
        else if(construction_workspace_.visible())construction_workspace_.close();
        else if(shipyard_workspace_.visible()){if(shipyard_workspace_.popover_open())(void)shipyard_workspace_.handle(event,width,height);else shipyard_workspace_.close();}
        else if(research_workspace_.visible()){if(research_workspace_.popover_open())(void)research_workspace_.handle(event,width,height);else research_workspace_.close();}
        else if(inspection_card_.visible()){inspection_card_.clear();selected_id_.reset();}
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
                tr("DIPLOMACY_NOTICE_UNSURVEYED","The last observation is outside surveyed space."),false);
        }
        if(command.captured&&event.type==InputEventType::KeyPressed&&
           diplomacy_workspace_.focus()>=0)
          announcer_.announce_focus(diplomacy_workspace_.focused_label(width,height),announcement_bounds(diplomacy_workspace_.focused_bounds(width,height)));
        if(command.captured)continue;
      }
      if(construction_workspace_.visible()){
        const int focus_before=construction_workspace_.focus();
        const auto command=construction_workspace_.handle(event,width,height);
        if(command.captured&&construction_workspace_.focus()!=focus_before)
          {const auto cl=ConstructionWorkspaceLayout::for_viewport(width,height);
          announcer_.announce_focus(construction_workspace_.focused_label(cl),announcement_bounds(construction_workspace_.focused_bounds(cl)));}
        if(command.kind!=ConstructionWorkspaceCommandKind::None)
          execute_construction(command);
        if(command.captured)continue;
      }
      if(shipyard_workspace_.visible()){
        const int focus_before=shipyard_workspace_.focus();
        const auto command=shipyard_workspace_.handle(event,width,height);
        if(command.captured&&shipyard_workspace_.focus()!=focus_before)
          {const auto sl=ShipyardWorkspaceLayout::for_viewport(width,height);
          announcer_.announce_focus(shipyard_workspace_.focused_label(sl),announcement_bounds(shipyard_workspace_.focused_bounds(sl)),std::nullopt,shipyard_workspace_.focused_control(sl));}
        if(command.kind!=ShipyardWorkspaceCommandKind::None)
          execute_shipyard(command);
        if(command.captured)continue;
      }
      if(research_workspace_.visible()){
        const int focus_before=research_workspace_.focus();
        const auto command=research_workspace_.handle(event,width,height);
        if(command.captured&&research_workspace_.focus()!=focus_before)
          announcer_.announce_focus(
              research_workspace_.focused_label(width,height),announcement_bounds(research_workspace_.focused_bounds(width,height)),std::nullopt,research_workspace_.focused_control(width,height));
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
        const int focus_before=inspection_card_.focus();
        const auto result=inspection_card_.handle(event,inspection_bounds(width,height));
        if(result.captured&&inspection_card_.focus()!=focus_before)
          announcer_.announce_focus(inspection_card_.focused_label(),announcement_bounds(inspection_card_.focused_bounds(inspection_bounds(width,height))));
        if(result.closed)selected_id_.reset();
        if(result.captured){gesture_.cancel();continue;}
      }
      if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
        if(event.type==InputEventType::LeftPressed&&event.click_count>=2){
          if(const auto target=system_hit(event.position,width,height);target&&enter_system(*target,width,height)){
            gesture_.capture_for_ui();continue;
          }
        }
      }
      if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()){
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
          else if(event.type==InputEventType::LeftReleased)gesture_.cancel();
          continue;
        }
      }
      if(event.type==InputEventType::LeftPressed){
        const auto action=layout.hit(event.position,menu_);bool captured=menu_||action!=UiAction::None;
        if(action!=UiAction::None&&audio_confirm_)audio_confirm_();
        if(action==UiAction::Pause){if(session_->frame().clock().speed()==StrategicSpeed::Paused)session_->frame().clock().resume();else session_->frame().clock().set_speed(StrategicSpeed::Paused);}
        else if(action==UiAction::Speed)cycle_speed();
        else if(action==UiAction::Menu)toggle_menu();
        else activate_menu_action(action,width,height);
        if(colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible()||diplomacy_workspace_.visible()||colony_workspace_.visible())captured=true;
        gesture_.begin(captured);continue;
      }
      if(event.type==InputEventType::PointerMove){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.allows_world_drag())pan_galaxy_camera(event.delta,width,height);gesture_.move(event.delta);update_fleet_hover_preview(event.position,width,height);continue;}
      if(event.type==InputEventType::Wheel){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&!gesture_.captured_by_ui())zoom_galaxy_camera(event.wheel_y,event.position,width,height);continue;}
      if(event.type==InputEventType::LeftReleased){if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&gesture_.release_as_world_click())select(event.position,width,height);else if(menu_||colony_roster_.visible()||economy_workspace_.visible()||supply_workspace_.visible()||colony_workspace_.visible()||research_workspace_.visible()||shipyard_workspace_.visible()||construction_workspace_.visible())(void)gesture_.release_as_world_click();}
    }
    if(research_workspace_.take_refresh_request())refresh_research(true);
    if(advance_simulation){
      const auto& tumble_world=session_->frame().runtime().world().campaign();
      system_workspace_.advance_tumble(elapsed,!menu_&&session_->frame().clock().speed()!=StrategicSpeed::Paused&&
          (!general_settings_||!general_settings_->saved().accessibility.reduce_motion)&&
          (!tumble_world.active_combat_encounter||tumble_world.active_combat_encounter->reconciled));
      const bool single_step=developer_panel_.take_step_request()&&session_->frame().can_step_developer();
      const auto frame_result=[&]{
        const auto advance_scope=stellar::engine::Profiler::instance().span("simulation","client");
        try{return session_->advance(menu_?0.:elapsed,timestamp,single_step);}
        catch(...){
          // A step that throws mid-frame in a developer session latches the
          // fault path (pause + diagnostic capture) instead of crashing; the
          // failure record lives on the frame. Player sessions still throw.
          if(!developer_session()||!session_->frame().last_advance_failure())throw;
          developer_monitor_.observe_advance_failure(session_->frame(),timestamp);
          respond_to_developer_fault(width,height);
          return stellar::core::CampaignFrameResult{};
        }
      }();
      if(developer_session()){
        developer_monitor_.observe(session_->frame(),frame_result,timestamp);
        respond_to_developer_fault(width,height);
      }
      publish_feedback(frame_result);
      if(voice_bridge_){
        const auto frequency=voice_pipeline_settings().frequency;
        voice_bridge_->observe(frame_result,session_->frame().runtime(),
            session_->frame().clock().simulation_days(),frequency);
        if(!menu_)
          voice_bridge_->observe_opening(session_->frame().runtime(),
              session_->frame().clock().simulation_days(),frequency);
      }
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
      if(replay_&&!replay_->divergence.empty())
        throw std::runtime_error("Replay divergence: "+replay_->divergence);
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
      const auto territory_claims=stellar::core::campaign_territorial_claims(
          session_->frame().runtime(),territory_world.player_civilization_id);
      territory_overlay_.request_update(territory_world,
          territory_world.player_civilization_id,territory_claims,
          session_->cache().generation);
      territory_refresh_elapsed_=0.;
    }
    // Gamepad camera input — Axis1D bindings in the GALAXY_PAD context read
    // the held stick values the mapper keeps live between axis events. Pan
    // moves the view in the stick direction (opposite the drag gesture's
    // sign); zoom acts on the viewport center like a centered wheel.
    if(!system_workspace_.visible()&&map_hud_visible()){
      const auto dead=[](float v){return std::abs(v)<.18f?0.f:v;};
      const float dt=static_cast<float>(std::max(0.,elapsed));
      const float pan_x=dead(input_mapper_.axis("map_pan_x"));
      const float pan_y=dead(input_mapper_.axis("map_pan_y"));
      if(pan_x!=0.f||pan_y!=0.f)
        pan_galaxy_camera({-pan_x*1000.f*dt,-pan_y*1000.f*dt},width,height);
      if(const float zoom=dead(input_mapper_.axis("map_zoom"));zoom!=0.f)
        zoom_galaxy_camera(-zoom*3.f*dt,
            {static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},
            width,height);
    }
    return true;
  }







  // Runtime-only evidence uses the same input routing and observer-owned view
  // as a player. It neither advances Core nor authors operating state.






  [[nodiscard]] bool artwork_ready()const noexcept{

    return planet_material_cache_.ready()&&(!system_workspace_.visible()||system_background_.ready())&&phenomena_.ready()&&(stellar_art_.pending_count()+eruption_art_.pending_count())==0&&(system_workspace_.visible()?system_workspace_.artwork_ready():galaxy_backdrop_.artwork_ready()&&territory_overlay_.valid()&&!territory_overlay_.pending());
  }

  [[nodiscard]] std::string artwork_status()const{
    return "planet="+std::to_string(planet_material_cache_.ready())+", sky="+std::to_string(system_background_.ready())+", phenomena="+std::to_string(phenomena_.ready())+", stellar="+std::to_string(stellar_art_.pending_count())+", eruptions="+std::to_string(eruption_art_.pending_count())+", system-visible="+std::to_string(system_workspace_.visible())+", system="+std::to_string(system_workspace_.artwork_ready())+", galaxy="+std::to_string(galaxy_backdrop_.artwork_ready())+", territory="+std::to_string(territory_overlay_.valid())+"/"+std::to_string(territory_overlay_.pending());
  }

  [[nodiscard]] DrawList scene(int width,int height){
    // One profiler frame per rendered scene: begin_frame drains the update
    // spans recorded since the last render into this frame's capture.
    stellar::engine::Profiler::instance().begin_frame();
    const ProfileFrameGuard profile_guard{};
    const auto scene_scope=stellar::engine::Profiler::instance().span("scene","client");
    system_background_.poll();
    phenomena_.poll();
    auto out=scene_content(width,height);if(developer_session()){phenomena_debug_.data(phenomena_debug_text());phenomena_debug_.render(out,width,height);if(background_debug_.visible()){const auto id=system_workspace_.system_id().value_or(selected_id_.value_or(session_->frame().runtime().world().campaign().systems.front().id));background_debug_.data(system_background_,id);background_debug_.render(out,width,height,system_background_);}}developer_panel_.render(out,width,height,session_->frame());developer_index_.render(out,width,height,session_->frame());developer_planet_index_.render(out,width,height);giant_test_panel_.render(out,width,height,session_->frame(),[this](const auto& a,int lod){return planet_material_cache_.request(a,lod);});stellar_activity_panel_.render(out,width,height,session_->frame());developer_diagnostics_.render(out,width,height,session_->frame(),developer_monitor_);developer_empires_.render(out,width,height,session_->frame());
    if(developer_session()&&developer_fault_capture_.latched()){
      const float s=std::clamp(static_cast<float>(height)/1080.f,.7f,1.5f);
      const UiRect banner{12.f*s,static_cast<float>(height)-70.f*s,static_cast<float>(width)-24.f*s,58.f*s};
      stellar::native_menu_style::panel(out,banner,s);
      stellar::native_menu_style::text(out,{banner.x+10*s,banner.y+6*s,banner.width-20*s,banner.height-12*s},
          developer_fault_capture_.notice(),std::max(12,static_cast<int>(16*s)),{255,180,110,255});
    }
    if(general_settings_&&general_settings_->saved().accessibility.high_contrast)
      stellar::native_ui::apply_high_contrast(out);
    if(general_settings_&&general_settings_->saved().accessibility.color_blind!=stellar::engine::ColorBlindMode::None)
      stellar::native_ui::apply_color_blind(out,general_settings_->saved().accessibility.color_blind);
    return out;
  }
  [[nodiscard]] DrawList scene_content(int width,int height){
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
      const auto& battle_world=session_->frame().runtime().world().campaign();
      if(battle_world.active_combat_encounter){const auto sid=battle_world.active_combat_encounter->system_id;const auto star=std::ranges::find(battle_world.systems,sid,&StellarSystem::id);
        if(star!=battle_world.systems.end()){DrawList environment;system_background_.append(environment,sid,width,height,starfield_quality(),starfield_density());phenomena_.append_system(environment,sid,star->position.x,star->position.y,width,height,1.,phenomena_options(sid),true);
          std::vector<UiOverlayCommand> commands;for(const auto& command:environment.world)std::visit([&](const auto& c){using T=std::decay_t<decltype(c)>;if constexpr(std::is_same_v<T,Image>||std::is_same_v<T,Scene3DView>)commands.emplace_back(c);},command);
          tactical.overlay.insert(tactical.overlay.begin()+std::min<std::size_t>(1,tactical.overlay.size()),commands.begin(),commands.end());
          if(!system_background_.ready()||!phenomena_.ready()){tactical={};tactical.overlay.emplace_back(Text{{static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},tr("MAP_ENVIRONMENT_LOADING","Loading system environment…"),{170,207,227,255},20,500,std::nullopt,TextAlign::Center});}
        }
      }
      return tactical;
    }
    char crash_context[512]{};
    std::snprintf(crash_context,sizeof(crash_context),"view=%s selected=%d system=%d map_zoom=%.6f viewport=%dx%d day=%.6f",
        system_workspace_.visible()?"solar-system":"star-map",selected_id_.value_or(-1),system_workspace_.system_id().value_or(-1),
        camera_.pixels_per_world/fitted_pixels_per_world_,width,height,session_->frame().clock().simulation_days());
    stellar::engine::RuntimeDiagnostics::context(crash_context);
    const auto screen_height=static_cast<float>(height);
    last_galaxy_label_stats_ = {};
    stellar_art_.set_reduce_flashing(general_settings_&&general_settings_->saved().accessibility.reduce_flashing);
    stellar_art_.begin_frame();
    eruption_art_.begin_frame(session_->cache().generation,session_->frame().runtime().stellar_activity_day(),std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),session_->frame().clock().speed()!=StrategicSpeed::Paused&&!menu_&&(!general_settings_||!general_settings_->saved().accessibility.reduce_motion),general_settings_?general_settings_->saved().eruption_quality:2);
    DrawList out; std::optional<std::size_t> galaxy_marker_begin; const auto &world=session_->frame().runtime().world().campaign();const auto &cache=session_->cache(); const Color lane{49,74,108,125};
    if(system_workspace_.visible()){
      const auto sid=*system_workspace_.system_id();const auto system=std::ranges::find(world.systems,sid,&StellarSystem::id);
      system_background_.append(out,sid,width,height,starfield_quality(),starfield_density());
      if(system!=world.systems.end())phenomena_.append_system(out,sid,system->position.x,system->position.y,width,height,system_workspace_.viewport()?system_workspace_.viewport()->scale:1.,phenomena_options(sid));
      system_workspace_.set_simulation_days(session_->frame().clock().simulation_days());
      system_workspace_.set_motion_running(session_->frame().clock().speed()!=StrategicSpeed::Paused&&!menu_&&(!world.active_combat_encounter||world.active_combat_encounter->reconciled));
      system_workspace_.render(out,width,height,false);
      render_system_environment(out,width,height);
      if(!system_background_.ready()||!phenomena_.ready()){out={};out.overlay.emplace_back(Text{{static_cast<float>(width)*.5f,static_cast<float>(height)*.5f},tr("MAP_ENVIRONMENT_LOADING","Loading system environment…"),{170,207,227,255},20,500,std::nullopt,TextAlign::Center});}
    }else{
    galaxy_backdrop_.append(out,{cache.generation,width,height,camera_,fitted_pixels_per_world_,true});
    phenomena_.append_map(out,camera_,width,height,phenomena_options(),surveyed_phenomena());
    last_territory_draw_=territory_overlay_.append(out,camera_,width,height,
        static_cast<float>(fitted_pixels_per_world_), {false,world.developer_provenance&&world.developer_provenance->full_exploration});
    for(const auto &edge:cache.lanes){ if(!known_.contains(edge.first_system_id)||!known_.contains(edge.second_system_id))continue; const auto a=cache.systems_by_id.find(edge.first_system_id),b=cache.systems_by_id.find(edge.second_system_id); if(a==cache.systems_by_id.end()||b==cache.systems_by_id.end())continue; const auto p1=camera_.project({a->second->position.x,a->second->position.y},width,height),p2=camera_.project({b->second->position.x,b->second->position.y},width,height); out.lines.push_back({p1,p2,lane}); }
    galaxy_marker_begin=out.world.size();
    std::vector<NativeGalaxyLabelCandidate> label_candidates;
    std::vector<NativeGalaxyLabelObstacle> label_obstacles;
    if(const auto central_art=stellar::native_stellar::observed_central_artwork(world,world.player_civilization_id)) {
      const auto point=camera_.project({world.core->position.x,world.core->position.y},width,height);
      const auto radius=stellar::native_stellar::central_black_hole_map_radius(world.core->exclusion_radius,camera_.pixels_per_world,width,height);
      stellar_art_.append(out,point,radius,std::string(*central_art),std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),UiRect{0,0,static_cast<float>(width),static_cast<float>(height)});
      label_obstacles.push_back({{point.x-radius,point.y-radius,radius*2,radius*2},NativeGalaxyLabelObstacleKind::star});
      label_candidates.push_back({NativeGalaxyLabelKind::central_object,0,point,radius,
          Text{{},tr("GALAXY_CENTRAL_OBJECT","Supermassive black hole"),{205,222,245,255},13},false,1e12});
    }
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
      if(survey!=SystemSurveyLevel::fully_surveyed&&colored_frontier_.contains(system.id)) {
        if(system.stellar_object){const auto rgb=stellar_object_definition(system.stellar_object->type).color;appearance.observed_color=Color{static_cast<std::uint8_t>(rgb[0]),static_cast<std::uint8_t>(rgb[1]),static_cast<std::uint8_t>(rgb[2]),255};}
        else if(system.primary)appearance.observed_color=spectral_color(system.primary);
      }
      const double relative_zoom=camera_.pixels_per_world/fitted_pixels_per_world_;
      const bool compact_marker=world.systems.size()>10000&&!known&&!selected_system&&
          appearance.primary==GalaxyStarVisualClass::unknown&&relative_zoom<16.;
      const float full_core_radius=galaxy_star_core_radius(relative_zoom,height,appearance.primary);
      const float core_radius=compact_marker?std::clamp(full_core_radius*.3f,.45f,1.25f):full_core_radius;
      const auto artwork=stellar::native_stellar::observed_stellar_artwork(survey,system.stellar_object,system.primary);
      const bool observed_physics=artwork.has_value();
      const float primary_scale=artwork?artwork->scale:1.f;
      const float separation=observed_physics?std::max(1.f,primary_scale):1.f;
      const Point secondary_offset=observed_physics?Point{core_radius*2.4f*separation,-core_radius*.85f}:Point{core_radius*.88f,-core_radius*.48f};
      const Point tertiary_offset=observed_physics?Point{-core_radius*2.2f*separation,core_radius*.95f}:Point{-core_radius*.82f,core_radius*.54f};
      const float extent=core_radius*4.5f*separation;
      if(point.x < -extent || point.y < -extent || point.x > width+extent || point.y > height+extent)continue;
      if(artwork) {
        stellar_art_.append(out,point,core_radius*artwork->scale,std::string(artwork->id),
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),UiRect{0,0,static_cast<float>(width),static_cast<float>(height)},static_cast<std::uint64_t>(system.id)*4);
        if(const auto surface=stellar_art_.photosphere(std::string(artwork->id),point,core_radius*artwork->scale))eruption_art_.append(out,surface->center,surface->radius,system,0,UiRect{0,0,static_cast<float>(width),static_cast<float>(height)});
        const auto companion=[&](std::optional<GalaxyStarVisualClass> visual,std::optional<StellarClass> spectral,Point offset,float scale,std::uint64_t component_key){
          if(!visual)return;
          if(const auto art=stellar::native_stellar::observed_stellar_artwork(survey,std::nullopt,spectral)){
            stellar_art_.append(out,{point.x+offset.x,point.y+offset.y},core_radius*scale,std::string(art->id),
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),UiRect{0,0,static_cast<float>(width),static_cast<float>(height)},static_cast<std::uint64_t>(system.id)*4+component_key);
            if(const auto surface=stellar_art_.photosphere(std::string(art->id),{point.x+offset.x,point.y+offset.y},core_radius*scale))eruption_art_.append(out,surface->center,surface->radius,system,static_cast<int>(component_key),UiRect{0,0,static_cast<float>(width),static_cast<float>(height)});
            return;
          }
          NativeGalaxyStarAppearance component;component.primary=*visual;
          galaxy_star_markers_.append(out,{point.x+offset.x,point.y+offset.y},core_radius*scale,component,false,UiRect{0,0,static_cast<float>(width),static_cast<float>(height)});
        };
        companion(appearance.secondary,system.secondary,secondary_offset,.70f,1);
        companion(appearance.tertiary,system.tertiary,tertiary_offset,.58f,2);
        if(selected_system)stellar::native_stellar::append_selection(out,point,core_radius*primary_scale);
      } else if(appearance.primary==GalaxyStarVisualClass::unknown&&!appearance.secondary&&!appearance.tertiary)
        galaxy_star_markers_.append_neutral_batch(out,point,core_radius,selected_system,
          UiRect{0,0,static_cast<float>(width),static_cast<float>(height)},compact_marker?.55f:known?1.f:.86f,appearance.observed_color,compact_marker);
      else galaxy_star_markers_.append(
          out, point, core_radius, appearance, selected_system,
          UiRect{0, 0, static_cast<float>(width), static_cast<float>(height)},
          known ? 1.f : .86f);

      // Names may cross the faint corona, but never the bright stellar core.
      // Reserving the full transparent sprite hid every nearby label at zoom.
      float marker_left = -core_radius * primary_scale * 1.1f;
      float marker_right = core_radius * primary_scale * 1.1f;
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
        include_component(secondary_offset, .70f);
      if (appearance.tertiary)
        include_component(tertiary_offset, .58f);
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
             Text{{}, known ? system.name : tr("SYSTEM_UNKNOWN","Unknown"), {205, 222, 245, 235},
                  15},
             selected_system, -(dx * dx + dy * dy)});
      }
    }

    if (const auto *projection = territory_overlay_.projection()) {
      const float overview = native_territory_overview_blend(
          static_cast<float>(camera_.pixels_per_world) /
              NativeTerritoryOverlay::coordinate_scale,
          static_cast<float>(fitted_pixels_per_world_) /
              NativeTerritoryOverlay::coordinate_scale);
      const float detail = std::max(native_territory_detail(overview),world.developer_provenance&&world.developer_provenance->full_exploration?.9f:0.f);
      for (const auto &region : projection->territories) {
        if (region.anchors.empty() ||
            (overview > .82f && region.anchors.size() < 2 && !(world.developer_provenance&&world.developer_provenance->full_exploration)))
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
    const auto command_hud=CommandHudLayout::make(width,height);
    reserve_hud(command_hud.resource_strip);reserve_hud(command_hud.context);
    const auto asset_layout=stellar::native_assets::Layout::make(width,height);
    reserve_hud(assets_.preferences().hidden?asset_layout.restore:asset_layout.panel);
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
    reserve_hud(map_zoom_bounds(width,height));
    if (menu_) reserve_hud(ui_layout.menu_panel);
    if (selected_id_)
      reserve_hud({14.f, screen_height - 88.f, 320.f, 74.f});
    if (const auto fleet_panel=fleet_workspace_.panel_bounds(width,height))
      reserve_hud(*fleet_panel);

    const UiRect label_viewport{4.f, 4.f,
                                std::max(1.f, static_cast<float>(width) - 8.f),
                                std::max(1.f, static_cast<float>(height) - 8.f)};
    auto label_layout = layout_native_galaxy_labels(
        std::move(label_candidates), label_viewport, label_obstacles,
        text_measurer_);
    last_galaxy_label_stats_ = label_layout.stats;
    for (auto &placement : label_layout.placements) {
      if (placement.kind != NativeGalaxyLabelKind::system) {
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
    if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!system_workspace_.visible()&&!colony_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&!diplomacy_workspace_.visible())
      fleet_workspace_.render(out,width,height,fleet_markers(width,height),&ship_art_,&overview_portrait_provider_);
    if(galaxy_marker_begin)
      promote_legacy_galaxy_foreground(out,*galaxy_marker_begin);
    if(!menu_&&!system_workspace_.visible()&&!settings_visible()){
      const auto cursor=pinned_phenomenon_?camera_.project(*pinned_phenomenon_,width,height):pointer_;
      phenomena_.inspect(out,camera_,cursor,width,height,surveyed_phenomena(),developer_session(),pinned_phenomenon_.has_value());
    }
    research_workspace_.render(out, width, height);
    shipyard_workspace_.render(out, width, height, &ship_art_);
    construction_workspace_.render(out, width, height);
    diplomacy_workspace_.render(out, width, height, &diplomacy_portrait_provider_);
    colony_workspace_.planetary().globe().set_simulation_days(session_->frame().clock().simulation_days());
    colony_workspace_.planetary().globe().set_visual_seconds(system_workspace_.visual_seconds());
    if(colony_workspace_.visible())refresh_planetary_portrait();
    colony_workspace_.render(out, width, height);
    settlement_workspace_.render(out,width,height);
    if(!menu_)colony_roster_.render(out,width,height);
    if(!menu_)mission_view_.render(out,mission_board_,mission_fleets_,mission_colonies_,width,height);
    if(!menu_)supply_workspace_.render(out,supply_controller_.view(),width,height);
    if(!menu_)economy_workspace_.render(out,economy_controller_.view(),width,height);
    if(!menu_&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&
       !construction_workspace_.visible()&&!diplomacy_workspace_.visible()&&
       !colony_workspace_.visible()&&!settlement_workspace_.visible()&&!mission_view_.visible())
      feedback_.render(out,width,height);
    const auto layout = NativeUiLayout::for_viewport(width, height);
    using stellar::native_ui_style::panel;
    render_command_hud(out,width,height);
    stellar::engine::ui_skin::gradient(out,layout.navigation_bar,{3,17,29,255},{1,8,16,255},0);
    out.overlay.emplace_back(Line{{0,layout.navigation_bar.y+layout.navigation_bar.height},
        {static_cast<float>(width),layout.navigation_bar.y+layout.navigation_bar.height},{42,111,147,245}});
    if(!hud_crest_)hud_crest_=decode_rgba_image(asset_root_/"assets/visual/branding/stellar-continuum-icon-v1.png");
    if(hud_crest_)out.overlay.emplace_back(Image{hud_crest_,{layout.brand.x,layout.brand.y+3*layout.scale,40*layout.scale,40*layout.scale}});
    out.overlay.emplace_back(Text{{layout.brand.x+50*layout.scale,layout.brand.y+3*layout.scale},"S T E L L A R",{205,239,255,255},static_cast<int>(17*layout.scale),layout.brand.width-50*layout.scale,layout.brand,TextAlign::Left,FontFace::Heading});
    out.overlay.emplace_back(Text{{layout.brand.x+50*layout.scale,layout.brand.y+27*layout.scale},"C O N T I N U U M",{150,202,229,255},static_cast<int>(12*layout.scale),layout.brand.width-50*layout.scale,layout.brand,TextAlign::Left,FontFace::Interface});
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
    const std::string rate=developer_session()?std::to_string(session_->frame().runtime().world().campaign().developer_provenance->simulation.speed)+"×":active_speed==StrategicSpeed::Maximum?"4×":active_speed==StrategicSpeed::VeryFast?"3×":active_speed==StrategicSpeed::Fast?"2×":"1×";
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
      control_label(out,layout.notifications,trf("HUD_EVENTS",{std::to_string(unread)},"EVENTS {0}"),
          unread?Color{240,197,106,255}:Color{225,238,250,255},
          layout.control_font_pixels,layout.scale,text_measurer_);
    }
    const auto draw_navigation=[&](UiRect bounds,UiAction action,bool active,std::string_view tip){
      const bool tab=bounds.width>bounds.height+4.f*layout.scale;
      if(active||bounds.contains(pointer_)||!tab)panel(out,bounds,bounds.contains(pointer_),active);
      const float icon=tab?27.f*layout.scale:bounds.width-8.f*layout.scale;
      if(const auto image=navigation_art_.image(action))out.overlay.emplace_back(Image{image,{bounds.x+(bounds.width-icon)*.5f,bounds.y+(tab?5.f*layout.scale:4.f*layout.scale),icon,icon}});
      if(tab)out.overlay.emplace_back(Text{{bounds.x+bounds.width*.5f,bounds.y+36.f*layout.scale},std::string(tip),
          active?Color{137,229,255,255}:Color{194,219,238,255},static_cast<int>(13.f*layout.scale),bounds.width-4.f*layout.scale,bounds,TextAlign::Center,FontFace::Interface});
      if(action==UiAction::ZoomIn||action==UiAction::ZoomOut){const auto glyph=action==UiAction::ZoomIn?"+":"−";out.overlay.emplace_back(Text{{bounds.x+bounds.width*.5f,bounds.y+bounds.height*.5f-9.f*layout.scale},glyph,{245,250,255,255},static_cast<int>(18.f*layout.scale),0,bounds,TextAlign::Center,FontFace::Heading});}
      if(tab||!bounds.contains(pointer_))return;
      const Color tooltip_text{235,244,255,255};
      const Text probe{{},std::string(tip),tooltip_text,layout.metric_font_pixels};
      const auto extent=text_measurer_?text_measurer_(probe):TextExtent{static_cast<int>(tip.size()*7),layout.metric_font_pixels+4};
      const UiRect tooltip{std::min(bounds.x+bounds.width+8.f,static_cast<float>(width-extent.width-12)),bounds.y,std::max(1.f,static_cast<float>(extent.width+12)),static_cast<float>(extent.height+8)};
      fill(out,tooltip,{4,14,27,245});stroke(out,tooltip,{82,155,194,230});
      out.overlay.emplace_back(Text{{tooltip.x+6.f,tooltip.y+4.f},std::string(tip),tooltip_text,layout.metric_font_pixels,tooltip.width-12.f,tooltip});
    };
    const auto navigation_visible=native_navigation_available(
        menu_,settlement_workspace_.visible(),diplomacy_workspace_.modal_open(),
        (colony_workspace_.planetary_modal()),settings_visible());
    if(navigation_visible){
      draw_navigation(layout.map,UiAction::Map,!system_workspace_.visible()&&!research_workspace_.visible()&&!shipyard_workspace_.visible()&&!colony_workspace_.visible()&&!economy_workspace_.visible()&&!diplomacy_workspace_.visible()&&!colony_roster_.visible()&&!supply_workspace_.visible()&&!construction_workspace_.visible() ,tr("NAV_GALAXY","Galaxy"));
      draw_navigation(layout.home,UiAction::Home,system_workspace_.visible()&&!colony_workspace_.visible(),tr("NAV_SYSTEM","System"));
      draw_navigation(layout.inspect,UiAction::Inspect,inspection_visible(),tr("NAV_INSPECT","Inspect"));
      draw_navigation(layout.zoom_in,UiAction::ZoomIn,false,tr("NAV_ZOOM_IN","Zoom in"));
      draw_navigation(layout.zoom_out,UiAction::ZoomOut,false,tr("NAV_ZOOM_OUT","Zoom out"));
      draw_navigation(layout.economy,UiAction::Economy,economy_workspace_.visible(),tr("NAV_ECONOMY","Economy"));
      draw_navigation(layout.research,UiAction::Research,research_workspace_.visible(),tr("NAV_RESEARCH","Research"));
      draw_navigation(layout.construction,UiAction::Construction,construction_workspace_.visible(),tr("NAV_CONSTRUCTION","Construction"));
      draw_navigation(layout.shipyard,UiAction::Shipyard,shipyard_workspace_.visible(),tr("NAV_SHIPYARD","Shipyard"));
      draw_navigation(layout.explore,UiAction::Explore,fleet_controller_.selection().has_value(),tr("NAV_EXPLORE","Explore"));
      draw_navigation(layout.missions,UiAction::Missions,mission_view_.visible(),tr("NAV_MISSIONS","Missions"));
      {const UiRect icon_rect{layout.missions.x+6.f*layout.scale,layout.missions.y+4.f*layout.scale,layout.missions.width-12.f*layout.scale,layout.missions.height-8.f*layout.scale};out.overlay.emplace_back(Text{{icon_rect.x+icon_rect.width*.5f,icon_rect.y+icon_rect.height*.5f-8.f*layout.scale},"M",{245,250,255,255},static_cast<int>(16.f*layout.scale),0,icon_rect,TextAlign::Center,FontFace::Heading});}
      draw_navigation(layout.colonies,UiAction::Colonies,colony_roster_.visible()||colony_workspace_.visible(),tr("NAV_PLANETS","Planets"));
      draw_navigation(layout.supply,UiAction::Supply,supply_workspace_.visible(),tr("NAV_LOGISTICS","Logistics"));
      draw_navigation(layout.diplomacy,UiAction::Diplomacy,diplomacy_workspace_.visible(),tr("NAV_DIPLOMACY","Diplomacy"));
      draw_navigation(layout.menu,UiAction::Menu,false,tr("NAV_MENU","Menu"));
      if(map_hud_visible()&&hud_focus_>=0){
        const auto items=hud_ring_items(layout,width,height);
        if(hud_focus_<static_cast<int>(items.size()))
          out.overlay.emplace_back(StrokedRectangle{items[static_cast<std::size_t>(hud_focus_)].first,{164,221,237,255}});
      }
    }
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y},
        stellar::native_campaign::format_campaign_date_short(
                     session_->frame().clock().simulation_days()),
        {210, 230, 244, 255}, static_cast<int>(13.f * layout.scale),
        layout.day_text.width, layout.day_text});
    out.overlay.emplace_back(Text{
        {layout.day_text.x, layout.day_text.y + 16.f * layout.scale},
        stellar::native_campaign::format_campaign_time(
            session_->frame().clock().simulation_days()) + tr("HUD_TIME_RATE","  |  1x = 1 hour/sec"),
        {139, 174, 194, 255}, static_cast<int>(10.f * layout.scale),
        layout.day_text.width, layout.day_text});
    if(!system_workspace_.visible()&&!research_workspace_.visible()&&
       !colony_workspace_.visible()&&!diplomacy_workspace_.visible()&&
       !shipyard_workspace_.visible()&&!construction_workspace_.visible()&&
       !economy_workspace_.visible()&&!supply_workspace_.visible()&&!menu_){
      std::ostringstream zoom_factor;
      zoom_factor << std::fixed << std::setprecision(1)
                  << camera_.pixels_per_world / fitted_pixels_per_world_;
      const auto zoom_bounds=map_zoom_bounds(width,height);
      out.overlay.emplace_back(Text{
          {zoom_bounds.x, zoom_bounds.y + 2.f * layout.scale},
          trf("HUD_MAP_ZOOM", {zoom_factor.str()}, "Map zoom {0}x"),
          {184, 223, 239, 255}, layout.metric_font_pixels,
          zoom_bounds.width, zoom_bounds});
    }
    if(inspection_visible())inspection_card_.render(out,inspection_bounds(width,height));
    const auto &notice = session_->notice();
    const bool preparing_galaxy=!system_workspace_.visible()&&
        (!galaxy_backdrop_.artwork_ready()||!territory_overlay_.valid()||territory_overlay_.pending());
    const bool support_notice=(support_.busy()||support_notice_seconds_>0.)&&
        notice.kind!=SessionNoticeKind::Failure&&notice.kind!=SessionNoticeKind::Loading&&
        notice.kind!=SessionNoticeKind::Saving;
    const bool quiet_workspace_notice=(research_workspace_.visible()||colony_workspace_.visible()) && !support_notice &&
        (notice.kind==SessionNoticeKind::Saved||notice.kind==SessionNoticeKind::Loaded);
    if (!quiet_workspace_notice && (notice.kind != SessionNoticeKind::None || preparing_galaxy || support_notice)) {
      auto message = notice.message_key.empty()
                         ? notice.message
                         : tr(notice.message_key, notice.message);
      if(preparing_galaxy&&(notice.kind==SessionNoticeKind::None||notice.kind==SessionNoticeKind::Saved||notice.kind==SessionNoticeKind::Loaded))
        message=tr("HUD_UPDATING_CHART","Updating star chart...");
      if (notice.kind == SessionNoticeKind::Loading) {
        message += " " +
                   std::to_string(static_cast<int>(notice.progress * 100.)) +
                   "%";
      }
      if(support_notice)message=support_.busy()?tr("SUPPORT_PREPARING","Preparing local diagnostics..."):
          support_.state()==stellar::native_support::SupportExportState::Succeeded?
          tr("SUPPORT_EXPORTED","Diagnostics exported. See the location in the pause menu."):
          tr("SUPPORT_FAILED","Diagnostic export failed. Open the pause menu for details.");
      const auto status_bounds=colony_workspace_.visible()?PlanetaryLayout::make(width,height).notice:layout.status_text;
      if(colony_workspace_.visible())fill(out,status_bounds,{3,12,18,255});
      out.overlay.emplace_back(Text{
          {status_bounds.x,
           status_bounds.y + 2.f * layout.scale},
          visible_notice(std::move(message)),
          (notice.kind == SessionNoticeKind::Failure||
           (support_notice&&support_.state()==stellar::native_support::SupportExportState::Failed))
              ? Color{255, 133, 123, 255}
              : Color{154, 211, 183, 255},
          layout.metric_font_pixels, status_bounds.width,
          status_bounds});
    }
    if (menu_) {
      stellar::native_ui_style::menu_panel(out, layout.menu_panel);
      label(out, layout.menu_heading, tr(session_->new_campaign_pending()?"MENU_SAVING":"MENU_PAUSED",session_->new_campaign_pending()?"SAVING CAMPAIGN":"PAUSED"), {238, 244, 255, 255},
            layout.heading_font_pixels, layout.scale, FontFace::Heading);
      const auto draw_button = [&](UiRect bounds, std::string text) {
        panel(out, bounds, bounds.contains(pointer_), false);
        control_label(out, bounds, std::move(text), {238, 244, 255, 255},
              layout.control_font_pixels, layout.scale,text_measurer_);
      };
      draw_button(layout.continue_button, tr(session_->new_campaign_pending()?"MENU_CANCEL_NEW_GAME":"MENU_CONTINUE",session_->new_campaign_pending()?"CANCEL NEW GAME":"CONTINUE"));
      if(!session_->new_campaign_pending()){
      draw_button(layout.save_button, tr("MENU_SAVE","SAVE"));
      draw_button(layout.load_button, tr("MENU_LOAD","LOAD"));
      draw_button(layout.settings_button, tr("MENU_SETTINGS","SETTINGS"));
      draw_button(layout.support_button, tr(support_.busy()?"MENU_EXPORTING":"MENU_EXPORT",support_.busy()?"EXPORTING...":"EXPORT DIAGNOSTICS"));
      draw_button(layout.new_game_button, tr("MENU_NEW_GAME","NEW GAME"));
      draw_button(layout.exit_button, tr("MENU_EXIT","EXIT TO WINDOWS"));
      }
      if(menu_focus_>=0&&menu_focus_<menu_action_count&&(!session_->new_campaign_pending()||menu_focus_==0)){
        const std::array<UiRect,7> focus_rects{layout.continue_button,layout.save_button,layout.load_button,layout.settings_button,layout.support_button,layout.new_game_button,layout.exit_button};
        out.overlay.emplace_back(StrokedRectangle{focus_rects[menu_focus_],{164,221,237,255}});
      }
      const float footer_y=layout.menu_panel.y+layout.menu_panel.height+8.f*layout.scale;
      const UiRect footer{36.f*layout.scale,footer_y,
          static_cast<float>(width)-72.f*layout.scale,
          std::max(0.f,static_cast<float>(height)-footer_y-12.f*layout.scale)};
      if(footer.height>=32.f*layout.scale){
        std::string detail=tr(developer_session()?"MENU_FOOTER_DEV":"MENU_FOOTER",developer_session()?"F12 saves a PNG screenshot. Export diagnostics (F8) includes the current isolated developer checkpoint and diagnostics.":"F12 saves a PNG screenshot. Export diagnostics (F8) includes your last completed save and recent session reports.");
        if(support_.state()==stellar::native_support::SupportExportState::Succeeded)
          detail="Diagnostics saved: "+utf8_path(support_.result());
        else if(support_.state()==stellar::native_support::SupportExportState::Failed)
          detail="Could not export diagnostics: "+support_.error()+" Your campaign is still open.";
        if(developer_session()&&developer_fault_capture_.latched()){
          if(!developer_fault_capture_.result().empty())detail="Critical diagnostics: "+utf8_path(developer_fault_capture_.result());
          else detail=developer_fault_capture_.notice();
        }
        out.overlay.emplace_back(Text{{footer.x,footer.y},std::move(detail),
            {194,218,238,255},layout.metric_font_pixels,footer.width,footer});
      }
    }
    if(notifications_available())notification_view_.render(out,notifications_.items(),width,height);
    if(notifications_available())chronicle_view_.render(out,width,height);
    stellar::native_audio::render_voice_caption(out,presentation_audio_,width,height,text_measurer_,
        general_settings_?general_settings_->saved().effective():stellar::engine::AccessibilitySettings{},
        voice_playback_?&*voice_playback_:nullptr,announcement_caption());
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
      colony_roster_.close();economy_workspace_.close();supply_workspace_.close();notification_view_.close();chronicle_view_.close();research_workspace_.close();shipyard_workspace_.close();
      construction_workspace_.close();diplomacy_workspace_.close();system_workspace_.close();
      colony_workspace_.close();
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
      const auto current=frame.tactical_resume_speed();const auto choices=frame.tactical_speed_options();double next=choices[1];
      for(const auto speed:choices)if(speed>current+1e-9){next=speed;break;}
      change_speed(next);return;
    }
    case BattleWorkspaceCommandKind::SetTacticalSpeed:change_speed(command.speed);return;
    case BattleWorkspaceCommandKind::Menu:toggle_menu();return;
    case BattleWorkspaceCommandKind::Fit:case BattleWorkspaceCommandKind::None:return;
    }
  }
  stellar::native_support::SupportBundleRequest make_support_request(int width,int height){
    std::ostringstream info;
    info<<support_environment_<<"Viewport="<<width<<'x'<<height<<'\n'
        <<"Systems="<<system_count()<<"\nSimulationDays="
        <<session_->frame().clock().simulation_days()<<'\n'
        <<"SaveFormat=Player17\nSavePolicy=Last completed save; no save requested by export\n";
    auto root=session_->save_path().parent_path();
    if(root.empty())root=".";
      stellar::native_support::SupportBundleRequest request{root,session_->save_path(),info.str(),{}};
      if(developer_session()){
        const auto timestamp=stellar::engine::diagnostic_utc_now();
        if(diagnostic_executable_hash_.empty())diagnostic_executable_hash_=stellar::app_diagnostics::executable_fingerprint(stellar::engine::executable_path());
        request.save_path.clear();
        request.system_info=support_environment_+"Viewport="+std::to_string(width)+"x"+std::to_string(height)+
            "\nSaveFormat=Developer17\nSavePolicy=Current immutable developer snapshot; normal saves excluded\n";
        request.additional_entries=stellar::app_diagnostics::capture_developer_report(session_->frame(),
            {STELLAR_GAME_VERSION,STELLAR_ENGINE_VERSION,STELLAR_SOURCE_COMMIT,diagnostic_executable_hash_},timestamp,&developer_monitor_.history());
        request.archive_name=stellar::app_diagnostics::archive_name(timestamp,session_->frame().runtime().world().campaign().seed);
      }
    if(const auto registry=stellar::engine::mounted_asset_registry()){
      const auto stats=registry->diagnostics();std::ostringstream report;
      report<<"ManifestVersion=STMNF001\nPackageFormat=STPAK001\nSourceFallback=false\n"
          <<"Assets="<<stats.assets<<"\nPackages="<<stats.packages<<"\nReads="<<stats.reads
          <<"\nStoredBytesRead="<<stats.bytes_read<<"\nDecodedBytes="<<stats.decoded_bytes
          <<"\nFailures="<<stats.failures<<"\nPackageGenerations:\n";
      std::set<std::string> packages;
      for(const auto& asset:registry->records())for(const auto& chunk:asset.chunks)packages.insert(chunk.package);
      for(const auto& name:packages)report<<name<<'\n';
      report<<"RecentFailures:\n";for(const auto& error:stats.recent_failures)report<<error<<'\n';
      request.additional_entries.push_back({"cooked-assets.txt",report.str()});
    }
    return request;
  }
  void request_support(int width,int height){
    if(support_.busy())return;
    support_.record("support",utc_timestamp()+" Local diagnostic export requested.");
    try{(void)support_.request(make_support_request(width,height));}
    catch(const std::exception &error){support_.report_capture_failure(error.what());}
    support_notice_seconds_=20.;
  }
  void respond_to_developer_fault(int width,int height){
    if(!developer_fault_capture_.observe(session_->frame(),developer_monitor_,[&]{return make_support_request(width,height);}))return;
    // Closing a menu must not silently restore the pre-fault running speed.
    pre_menu_speed_=StrategicSpeed::Paused;
    auto &frame=session_->frame();frame.resume_tactical_after_menu();frame.set_tactical_speed(0.);
    developer_empires_.close();developer_panel_.close();developer_index_.close();developer_planet_index_.close();developer_diagnostics_.open(developer_monitor_);
    gesture_.capture_for_ui();
  }
  [[nodiscard]] bool notifications_available()const noexcept{
    return !battle_workspace_.visible()&&native_navigation_available(menu_,settlement_workspace_.visible(),
        diplomacy_workspace_.modal_open(),(colony_workspace_.planetary_modal()),
        settings_visible())&&
        !shipyard_workspace_.confirmation_open()&&!construction_workspace_.confirmation_open()&&
        !fleet_workspace_.preview();
  }
  void seed_notifications(){
    auto& runtime=session_->frame().runtime();
    diplomatic_notifications_.seed(runtime.diplomacy().build_view_for(
        runtime.world().campaign().player_civilization_id));
    native_notifications::seed_chronicle_notifications(notifications_,
        runtime.history(),runtime.world().campaign().player_civilization_id);
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
    announcer_.announce(message);
    notifications_.publish(std::move(category),stellar::native_campaign::format_campaign_date(
        session_->frame().clock().simulation_days()),std::move(message));
  }
  void scientist_voice(stellar::native_audio::VoiceCue cue){
    // Human casting must not silently replace another species' advisor.
    if(player_species_id()!="terran_baseline")return;
    if(voice_router_){
      stellar::native_voice::NativeGameplayVoiceEvent event;
      event.event_key=cue==stellar::native_audio::VoiceCue::ReconnaissanceRequired
          ?"exploration.system.reconnaissance_required"
          :cue==stellar::native_audio::VoiceCue::ResearchReport?"research.completed"
          :"exploration.survey.completed";
      const auto& world=session_->frame().runtime().world().campaign();
      const auto days=session_->frame().clock().simulation_days();
      event.source_civilization_id=world.player_civilization_id;
      event.source_species_id=player_species_id();
      event.simulation_tick=static_cast<std::int64_t>(std::llround(days*1000.));
      event.simulation_date=stellar::native_campaign::format_campaign_date(days);
      event.unique_event_id=event.event_key+":ui:"+std::to_string(++voice_ui_sequence_);
      stellar::native_voice::NativeVoiceRoutingContext context;
      context.player_civilization_id=world.player_civilization_id;
      context.frequency=voice_pipeline_settings().frequency;
      voice_router_->emit(event,context);
      return;
    }
    if(presentation_audio_)presentation_audio_->speak(cue);
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
    // Research/survey speech routes through the gameplay voice bridge's
    // frame-result observation (the legacy VoiceCue path only remains as the
    // fallback for UI-triggered reconnaissance when the pipeline is absent).
    if(!voice_router_){
      if(summary.count(FeedbackKind::ResearchReport))scientist_voice(VoiceCue::ResearchReport);
      if(summary.count(FeedbackKind::SurveyComplete))scientist_voice(VoiceCue::SurveyComplete);
    }
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
    colony_roster_.close();economy_workspace_.close();supply_workspace_.close();gesture_.cancel();research_workspace_.close();shipyard_workspace_.close();construction_workspace_.close();diplomacy_workspace_.close();colony_workspace_.close();settlement_workspace_.clear();colony_entry_view_.reset();mission_view_.close();
    system_workspace_.open(std::move(*built.snapshot),width,height);selected_id_=system_id;
    system_background_.preload(system_id,starfield_quality(),starfield_density());
    {const auto& world=session_->frame().runtime().world().campaign();const auto star=std::ranges::find(world.systems,system_id,&StellarSystem::id);if(star!=world.systems.end()){DrawList preload;phenomena_.append_system(preload,system_id,star->position.x,star->position.y,width,height,1,phenomena_options(system_id));}}
    if(travel.snapshot)system_workspace_.refresh_travel(std::move(*travel.snapshot),fleet_controller_.selection());else system_workspace_.set_notice(travel.denial);
    refresh_assets();system_refresh_elapsed_=0.;return true;
  }

  void refresh_system(bool force){
    if(!system_workspace_.visible()||!system_workspace_.system_id())return;
    const auto &world=session_->frame().runtime().world().campaign();
    const auto level=observation_survey_level(world,world.player_civilization_id,*system_workspace_.system_id());
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

  std::optional<NativeColonyView> planetary_view(int body_id){
    if(!system_workspace_.snapshot())return {};
    const auto& snapshot=*system_workspace_.snapshot();
    auto owned=colony_controller_.build(session_->frame(),session_->cache().generation,snapshot,body_id);
    if(owned.view)return owned.view;
    const auto body=std::ranges::find(snapshot.bodies,body_id,&NativeSystemBody::id);
    if(body==snapshot.bodies.end())return {};
    NativeColonyView view;view.simulation_days=session_->frame().clock().simulation_days();view.illumination_star=stellar_host_physics(stellar::native_system::snapshot_stellar_system(snapshot),body->stellar_host);
    view.stellar_lighting=stellar::native_planets::system_lighting(snapshot,*body);
    const auto projected=stellar::native_system::project_system(snapshot);
    for(const auto& marker:projected.bodies)if(marker.body_id==body_id){const auto host=projected.stellar_hosts[body->stellar_host];view.illumination_x=marker.offset_x-host.x;view.illumination_y=marker.offset_y-host.y;break;}
    view.campaign_generation=snapshot.campaign_generation;view.player_civilization_id=snapshot.observer_civilization_id;
    view.system_id=snapshot.system_id;view.body_id=body_id;view.colony_id=-body_id;view.body_display_name=body->name;view.system_name=snapshot.catalog_name;view.planet=*body;view.observer_only=true;view.developer_inspection=developer_observation(session_->frame().runtime().world().campaign(),snapshot.observer_civilization_id);view.solid_surface=body->details&&body->details->has_solid_surface;
    return view;
  }
  void open_colony_from_system(int body_id){
    auto view=planetary_view(body_id);if(!view)return;colony_workspace_.open(std::move(*view));
    refresh_planetary_portrait();gesture_.capture_for_ui();colony_refresh_elapsed_=0.;
  }
  void refresh_colony(bool force){
    if(!colony_workspace_.visible()||!system_workspace_.snapshot()||!system_workspace_.selected_body_id())return;
    if(!force&&colony_refresh_elapsed_<.2)return;
    auto view=planetary_view(*system_workspace_.selected_body_id());
    if(!view){colony_workspace_.close();colony_entry_view_.reset();system_workspace_.set_colony_body(std::nullopt);return;}
    if(!view->observer_only)colony_entry_view_=*view;
    colony_workspace_.set_view(std::move(*view));refresh_planetary_portrait();colony_refresh_elapsed_=0.;
  }

  void execute_colony_freight(const ColonyWorkspaceCommand& command){
    if(command.kind==ColonyWorkspaceCommandKind::CancelFreight){
      outpost_freight_controller_.clear();colony_workspace_.cancel_freight();return;
    }
    if(!colony_workspace_.visible()||!colony_workspace_.view()||colony_workspace_.view()->foreign_settlement)return;
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
    if(!colony_workspace_.view())return;
    const auto& v=*colony_workspace_.view();
    if(v.planet.appearance){colony_workspace_.planetary().set_portrait(planet_material_cache_.portrait(*v.planet.appearance));return;}
    colony_workspace_.planetary().set_portrait(planet_discs_.request_image({v.campaign_generation,v.body_id,v.planet.details.has_value(),v.planet.visual_class,v.planet.sol_texture_key,static_cast<std::uint32_t>(v.body_id),-.4f}));
  }
  void execute_planetary(const PlanetaryCommand& command){
    if(!colony_workspace_.view())return;
    auto& screen=colony_workspace_.planetary();const auto& view=*colony_workspace_.view();
    const auto generation=session_->cache().generation;
    if(command.action==PlanetaryAction::None)return;
    if(command.action==PlanetaryAction::Save){session_->request_save();return;}
    if(view.observer_only||view.foreign_settlement)return;
    if(command.action==PlanetaryAction::Cancel){
      std::visit([&](const auto& quote){using T=std::decay_t<decltype(quote)>;if constexpr(!std::is_same_v<T,std::monostate>)(void)surface_controller_.cancel_quote(generation,quote.quote_revision);},screen.pending());
      screen.complete(tr("PLANET_ORDER_CANCELLED","Order cancelled. No resources spent."));return;
    }
    if(command.action==PlanetaryAction::Confirm){
      NativeSurfaceCommandOutcome outcome{false,tr("PLANET_ORDER_REVIEW","Review the order again.")};
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







  void preview_settlement(int body_id,int width,int height){
    if(!system_workspace_.system_id()||!fleet_controller_.selection()){
      system_workspace_.set_notice(tr("SYSTEM_NOTICE_VESSEL","Select an owned colony or outpost vessel before choosing a settlement target."));
      return;
    }
    if(!settlement_controller_.live_status(session_->frame(),session_->cache().generation,*fleet_controller_.selection())){
      system_workspace_.set_notice(tr("SYSTEM_NOTICE_NOT_VESSEL","The selected fleet is not a populated settlement vessel."));
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
      research_workspace_.set_notice(tr("RESEARCH_NOTICE_LOADING","Research details are still loading."),false);
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
      shipyard_workspace_.set_notice(tr("SHIPYARD_NOTICE_LOADING","Shipyard details are still loading."),false);
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
            tr("SHIPYARD_NOTICE_QUOTE_CHANGED","The cancellation quote changed; review the refreshed order."),false);
      return;
    }
    if(command.kind==ShipyardWorkspaceCommandKind::Start)
      outcome=shipyard_controller_.start(session_->frame(),
          session_->cache().generation,view->shipyard_revision,command.id,command.quantity);
    else if(command.kind==ShipyardWorkspaceCommandKind::MoveUp || command.kind==ShipyardWorkspaceCommandKind::MoveDown)
      outcome=shipyard_controller_.reorder(session_->frame(),session_->cache().generation,
          view->shipyard_revision,command.id,command.kind==ShipyardWorkspaceCommandKind::MoveUp?-1:1);
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
          tr("CONSTRUCTION_NOTICE_LOADING","Construction details are still loading."),false);
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
      diplomacy_workspace_.set_notice(tr("DIPLOMACY_NOTICE_LOADING","Diplomatic channels are still loading."),false);
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
    if(!(camera_.pixels_per_world>0.))return std::nullopt;
    ensure_system_hit_grid();
    if(!system_hit_grid_)return std::nullopt;
    // The camera transform is a uniform scale, so nearest in world space is
    // nearest in screen space; convert the pixel pick radius once instead of
    // projecting every system per query.
    const auto world=camera_.unproject(pointer,width,height);
    const auto hit=system_hit_grid_->nearest(static_cast<float>(world.x),
        static_cast<float>(world.y),
        static_cast<float>(best/camera_.pixels_per_world));
    return hit?std::optional<int>{hit->first}:std::nullopt;
  }

  // Star positions are static within a campaign; the spatial index is rebuilt
  // lazily when the session cache generation changes (new/load campaign).
  void ensure_system_hit_grid()const{
    const auto generation=session_->cache().generation;
    if(system_hit_grid_&&system_hit_grid_generation_==generation)return;
    const auto &systems=session_->frame().runtime().world().campaign().systems;
    system_hit_grid_.reset();
    system_hit_grid_generation_=generation;
    if(systems.empty())return;
    float minx=std::numeric_limits<float>::max(),maxx=std::numeric_limits<float>::lowest(),
          miny=minx,maxy=maxx;
    for(const auto &s:systems){
      minx=std::min(minx,s.position.x);maxx=std::max(maxx,s.position.x);
      miny=std::min(miny,s.position.y);maxy=std::max(maxy,s.position.y);
    }
    const float extent=std::max({maxx-minx,maxy-miny,1e-3f});
    const float cell=std::max(extent/std::sqrt(static_cast<float>(systems.size())),1e-3f);
    system_hit_grid_.emplace(cell);
    for(const auto &s:systems)
      system_hit_grid_->insert(s.id,s.position.x,s.position.y);
  }

  [[nodiscard]] std::string system_display_name(int system_id)const{
    if(!known_.contains(system_id))return tr("SYSTEM_NAME_UNKNOWN","Unknown system");
    const auto found=session_->cache().systems_by_id.find(system_id);
    return found==session_->cache().systems_by_id.end()?tr("SYSTEM_NAME_UNKNOWN","Unknown system"):found->second->name;
  }

  [[nodiscard]] std::vector<ObservedSystemName> observed_system_names()const{
    const auto &systems=session_->frame().runtime().world().campaign().systems;
    std::vector<ObservedSystemName> result;
    result.reserve(systems.size());
    for(const auto &system:systems)
      result.push_back({system.id,system.name,known_.contains(system.id)});
    return result;
  }

  // Missions board feed: mission cards, active settlement fleets and the
  // owned-colony rows are rebuilt when the campaign generation turns over
  // while the panel is open.
  void refresh_missions(bool force){
    if(!mission_view_.visible()){mission_generation_.reset();return;}
    const auto generation=session_->cache().generation;
    if(!force&&mission_generation_==generation)return;
    mission_generation_=generation;
    auto&frame=session_->frame();
    const auto&campaign=frame.runtime().world().campaign();
    mission_board_=native_missions::build_mission_board(campaign,locale_);
    mission_fleets_.clear();
    for(auto&view:settlement_controller_.build(frame,generation))
      if(has_active_settlement_target(view))mission_fleets_.push_back(std::move(view));
    mission_colonies_=native_missions::build_owned_colony_rows(campaign,locale_);
  }
  // "Select ship on map": select the colony ship and center the galaxy map
  // on it — the settle order itself is issued from the destination system.
  void focus_mission_fleet(int fleet_id){
    const auto outcome=fleet_controller_.select(session_->frame(),session_->cache().generation,fleet_id);
    if(!outcome.accepted){publish_notification("Fleet",observer_safe_fleet_message(outcome.message,observed_system_names(),locale_));return;}
    refresh_fleets(true);
    if(fleet_workspace_.view()){
      const auto fleet=std::ranges::find(fleet_workspace_.view()->own_fleets,fleet_id,&NativeOwnFleet::id);
      if(fleet!=fleet_workspace_.view()->own_fleets.end()){
        mission_view_.close();
        camera_.center={fleet->position.x,fleet->position.y};
        if(audio_confirm_)audio_confirm_();
      }
    }else mission_view_.close();
  }
  // Owned-colony card actions: enter the colony's planetary management
  // screen (surface entry is the reference "Land"), optionally opening the
  // freight review for a resource outpost (the reference "Collect").
  void open_mission_colony(int colony_id,int width,int height,bool review_freight){
    const auto&world=session_->frame().runtime().world().campaign();
    const auto colony=std::ranges::find(world.colonies,colony_id,&Colony::id);
    if(colony==world.colonies.end()||!colony->planetary_body_id)return;
    if(!enter_system(colony->system_id,width,height)||
       !system_workspace_.select_body(*colony->planetary_body_id))return;
    open_colony_from_system(*colony->planetary_body_id);
    if(!review_freight||!colony_workspace_.visible()||!colony_workspace_.view())return;
    const auto&view=*colony_workspace_.view();
    if(view.observer_only||view.foreign_settlement)return;
    session_->frame().clock().set_speed(StrategicSpeed::Paused);
    colony_workspace_.set_freight_preview(outpost_freight_controller_.preview(
        session_->frame(),session_->cache().generation,view));
    gesture_.capture_for_ui();
  }
  // Reference UiOpenOwnedColony(colonyId, land:false): enter the owning
  // system's orbital view already focused on the colony world.
  void open_overview_colony(int colony_id,int width,int height){
    const auto &campaign=session_->frame().runtime().world().campaign();
    const auto colony=std::ranges::find(campaign.colonies,colony_id,
                                        &Colony::id);
    if(colony==campaign.colonies.end()||!colony->planetary_body_id)return;
    notification_view_.close();chronicle_view_.close();
    if(!enter_system(colony->system_id,width,height)||
       !system_workspace_.select_body(*colony->planetary_body_id)){
      session_->publish_status(
          tr("STATUS_COLONY_WORLD_UNAVAILABLE",
             "The colony world is not available in the current orbital survey."));
      return;
    }
    refresh_colony_entry(true);
  }

  void refresh_fleets(bool force){
    if(!force&&fleet_refresh_elapsed_<.1)return;
    auto view=fleet_controller_.build(session_->frame(),
                                      session_->cache().generation);
    const auto names=observed_system_names();
    for(auto &fleet:view.own_fleets)
      fleet.recovery_message=observer_safe_fleet_message(fleet.recovery_message,names,locale_);
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
        session_->frame().runtime().world().campaign(),selected_id_,locale_));
    if(force||assets_refresh_elapsed_>=.25)refresh_assets();
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
    const bool blocked=menu_||
        colony_workspace_.visible()||research_workspace_.visible()||
        shipyard_workspace_.visible()||construction_workspace_.visible()||
        diplomacy_workspace_.visible()||notification_view_.visible()||
        chronicle_view_.visible()||
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
                                                observed_system_names(),locale_);
    preview.command_available=false;
    fleet_workspace_.set_preview(std::move(preview),
                                 system_display_name(*target));
    hover_preview_shown_=true;
  }

  void handle_fleet_command(const FleetWorkspaceCommand &command){
    if(command.kind==FleetWorkspaceCommandKind::None)return;
    hover_preview_target_.reset();hover_preview_shown_=false;
    if(command.kind==FleetWorkspaceCommandKind::Recovery){
      if(!command.recovery_quote)return;
      auto outcome=fleet_controller_.issue_civilian_recovery(
          session_->frame(),*command.recovery_quote,command.recovery_action,
          command.confirm_abandon);
      outcome.message=observer_safe_fleet_message(outcome.message,observed_system_names(),locale_);
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
      const auto message=observer_safe_fleet_message(outcome.message,observed_system_names(),locale_);
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
      fleet_workspace_.set_notice(observer_safe_fleet_message(outcome.message,observed_system_names(),locale_),outcome.accepted);
      refresh_fleets(true);return;
    }
    if(command.kind==FleetWorkspaceCommandKind::Engage){
      const auto outcome=session_->frame().begin_tactical(command.fleet_id);
      fleet_workspace_.set_notice(observer_safe_fleet_message(outcome.message,observed_system_names(),locale_),outcome.accepted);
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
                                                  observed_system_names(),locale_);
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
                                      outcome.message,observed_system_names(),
                                      locale_),
                                  outcome.accepted);
      last_fleet_command_accepted_=outcome.accepted;
      refresh_fleets(true);
      return;
    }
  }


  [[nodiscard]] Camera galaxy_overview_camera(int width,int height)const{
    const auto scale=NativeUiLayout::for_viewport(width,height).scale;
    const auto right=stellar::native_assets::Layout::make(width,height).panel.x-12.f*scale;
    const UiRect content{78.f*scale,110.f*scale,std::max(1.f,right-78.f*scale),std::max(1.f,height-206.f*scale)};
    return galaxy_backdrop_.fit_camera(width,height,content);
  }
  void constrain_galaxy_camera(int width,int height){
    if(!galaxy_backdrop_.artwork_frame())return;
    const auto overview=galaxy_overview_camera(width,height);
    fitted_pixels_per_world_=overview.pixels_per_world;
    camera_.constrain_to_overview(overview,width,height);
  }
  void resize_galaxy_camera(int width,int height){
    if(width<=0||height<=0||!galaxy_backdrop_.artwork_frame())return;
    if(galaxy_view_width_==width&&galaxy_view_height_==height)return;
    hud_switch_pressed_=false;gesture_.cancel();
    const auto fit=galaxy_overview_camera(width,height);
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
  void fit_camera(int width,int height){if(galaxy_backdrop_.artwork_frame()){camera_=galaxy_overview_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;return;}const auto &systems=session_->frame().runtime().world().campaign().systems;double minx=std::numeric_limits<double>::max(),maxx=std::numeric_limits<double>::lowest(),miny=minx,maxy=maxx;for(const auto&s:systems){minx=std::min(minx,static_cast<double>(s.position.x));maxx=std::max(maxx,static_cast<double>(s.position.x));miny=std::min(miny,static_cast<double>(s.position.y));maxy=std::max(maxy,static_cast<double>(s.position.y));}camera_.center={(minx+maxx)*.5,(miny+maxy)*.5};camera_.pixels_per_world=std::max(.01,std::min(static_cast<double>(width)/std::max(1.,maxx-minx),static_cast<double>(height)/std::max(1.,maxy-miny))*.88);fitted_pixels_per_world_=camera_.pixels_per_world;}
  static constexpr std::array<UiAction,7> menu_actions_{UiAction::Continue,UiAction::Save,UiAction::Load,UiAction::Settings,UiAction::Support,UiAction::NewGame,UiAction::Exit};
  static constexpr int menu_action_count=static_cast<int>(menu_actions_.size());
  void activate_menu_action(UiAction action,int width,int height){
    if(action==UiAction::Continue)toggle_menu();
    else if(action==UiAction::NewGame){gesture_.capture_for_ui();menu_focus_=-1;(void)session_->request_new_campaign();}
    else if(action==UiAction::Save)session_->request_save();
    else if(action==UiAction::Load)session_->request_load();
    else if(action==UiAction::Settings){if(settings_hub_)settings_hub_->open();else if(audio_settings_)audio_settings_->open();}
    else if(action==UiAction::Support)request_support(width,height);
    else if(action==UiAction::Exit)session_->request_exit();
  }
  std::string menu_action_label(UiAction action)const{
    const bool pending=session_->new_campaign_pending();
    switch(action){
      case UiAction::Continue:return tr(pending?"MENU_CANCEL_NEW_GAME":"MENU_CONTINUE",pending?"CANCEL NEW GAME":"CONTINUE");
      case UiAction::Save:return tr("MENU_SAVE","SAVE");
      case UiAction::Load:return tr("MENU_LOAD","LOAD");
      case UiAction::Settings:return tr("MENU_SETTINGS","SETTINGS");
      case UiAction::Support:return tr(support_.busy()?"MENU_EXPORTING":"MENU_EXPORT",support_.busy()?"EXPORTING...":"EXPORT DIAGNOSTICS");
      case UiAction::NewGame:return tr("MENU_NEW_GAME","NEW GAME");
      case UiAction::Exit:return tr("MENU_EXIT","EXIT TO WINDOWS");
      default:return {};
    }
  }
  void announce_menu_focus(int width,int height){if(menu_focus_>=0&&menu_focus_<menu_action_count){const auto layout=NativeUiLayout::for_viewport(width,height);const std::array<UiRect,7> rects{layout.continue_button,layout.save_button,layout.load_button,layout.settings_button,layout.support_button,layout.new_game_button,layout.exit_button};announcer_.announce_focus(menu_action_label(menu_actions_[menu_focus_]),announcement_bounds(rects[static_cast<std::size_t>(menu_focus_)]),std::nullopt,stellar::engine::AnnouncementControl::Button);}}
  // HUD focus-ring items: the layout's chrome actions plus the command
  // plate's view-switch button — the latter only while it is actionable
  // (a system is open or one is selected), matching the pointer path's
  // dimmed-when-inert rendering.
  [[nodiscard]] std::vector<std::pair<UiRect,UiAction>> hud_ring_items(const NativeUiLayout &layout,int width,int height)const{
    std::vector<std::pair<UiRect,UiAction>> items;
    for(const auto &item:layout.hud_actions())
      if(item.second!=UiAction::Notifications||notifications_available())items.push_back(item);
    if(system_workspace_.visible()||selected_id_)
      items.emplace_back(CommandHudLayout::make(width,height).switch_view,UiAction::SwitchView);
    return items;
  }
  void activate_hud_switch(int width,int height){
    if(system_workspace_.visible()){selected_id_=system_workspace_.system_id();system_workspace_.close();refresh_inspection();}
    else if(selected_id_&&!enter_system(*selected_id_,width,height))scientist_voice(stellar::native_audio::VoiceCue::ReconnaissanceRequired);
  }
  std::string hud_action_label(UiAction action)const{
    switch(action){
      case UiAction::Notifications:return tr("NAV_EVENTS","Events");
      case UiAction::Pause:return tr("NAV_PAUSE","Pause");
      case UiAction::Speed:return tr("NAV_SPEED","Speed");
      case UiAction::Map:return tr("NAV_GALAXY","Galaxy");
      case UiAction::Home:return tr("NAV_SYSTEM","System");
      case UiAction::Colonies:return tr("NAV_PLANETS","Planets");
      case UiAction::Economy:return tr("NAV_ECONOMY","Economy");
      case UiAction::Research:return tr("NAV_RESEARCH","Research");
      case UiAction::Diplomacy:return tr("NAV_DIPLOMACY","Diplomacy");
      case UiAction::Supply:return tr("NAV_LOGISTICS","Logistics");
      case UiAction::Shipyard:return tr("NAV_SHIPYARD","Shipyard");
      case UiAction::Menu:return tr("NAV_MENU","Menu");
      case UiAction::Inspect:return tr("NAV_INSPECT","Inspect");
      case UiAction::ZoomIn:return tr("NAV_ZOOM_IN","Zoom in");
      case UiAction::ZoomOut:return tr("NAV_ZOOM_OUT","Zoom out");
      case UiAction::Construction:return tr("NAV_CONSTRUCTION","Construction");
      case UiAction::Explore:return tr("NAV_EXPLORE","Explore");
      case UiAction::Missions:return tr("NAV_MISSIONS","Missions");
      case UiAction::SwitchView:return tr("HUD_SWITCH_VIEW","Switch view");
      default:return {};
    }
  }
  std::optional<stellar::native_audio::VoiceCaption> announcement_caption(){
    while(auto item=announcer_.take()){
      if(accessibility_bridge_){
        if(item->kind==stellar::engine::AnnouncementKind::Focus)
          accessibility_bridge_->focus_changed(item->text, item->bounds,
                                               item->range, item->control);
        else
          accessibility_bridge_->announce(item->text);
      }
      // Empty focus items only signal the ring releasing to the bridge —
      // nothing to speak or caption.
      if(item->text.empty())continue;
      if(voice_playback_&&presentation_audio_&&
         presentation_audio_->voice_preferences().interface_announcements){
        stellar::native_voice::NativeSpeechRequest request;
        request.text=item->text;
        request.category="interface";
        request.priority=static_cast<int>(stellar::native_voice::VoicePriority::Important);
        request.queue_behavior=stellar::native_voice::SpeechQueueBehavior::ReplaceCategory;
        request.interruptible=true;
        request.dedupe_key="interface|"+std::to_string(++announcement_seq_);
        request.expires_at=std::chrono::system_clock::now()+std::chrono::seconds(10);
        voice_playback_->speak(std::move(request));
      }
      announcement_caption_=stellar::native_audio::VoiceCaption{"",std::move(item->text),
          std::chrono::steady_clock::now()+std::chrono::seconds(4)};
    }
    if(!announcement_caption_||std::chrono::steady_clock::now()>=announcement_caption_->expires_at)return std::nullopt;
    if(!presentation_audio_||!presentation_audio_->voice_preferences().subtitles)return std::nullopt;
    return announcement_caption_;
  }
  void toggle_menu(){menu_focus_=-1;settlement_workspace_.cancel_pending_input();colony_roster_.cancel_pending_input();colony_workspace_.cancel_freight();outpost_freight_controller_.clear();fleet_workspace_.cancel_recovery();notification_view_.close();chronicle_view_.close();mission_view_.close();menu_=!menu_;auto &frame=session_->frame();frame.set_menu_open(menu_);if(menu_){gesture_.capture_for_ui();pre_menu_speed_=frame.clock().speed();frame.clock().set_speed(StrategicSpeed::Paused);frame.pause_tactical_for_menu();}else{frame.resume_tactical_after_menu();frame.clock().set_speed(pre_menu_speed_);}}
  void refresh_knowledge(){const auto &world=session_->frame().runtime().world().campaign();const auto known=world.knowledge.known_systems(world.player_civilization_id);known_.clear();known_.insert(known.begin(),known.end());
    if(galaxy_backdrop_.artwork_frame())galaxy_backdrop_.set_galactic_core_discovered(session_->cache().generation,world.knowledge.is_galactic_core_discovered(world.player_civilization_id));
    std::unordered_set<int> owned;
    for(const auto& colony:world.colonies)if(colony.civilization_id==world.player_civilization_id)owned.insert(colony.system_id);
    std::vector<int> origins;
    for(const auto& system:world.systems)if(owned.contains(system.id)||world.knowledge.system_survey_level(world.player_civilization_id,system.id)>=SystemSurveyLevel::partially_surveyed)origins.push_back(system.id);
    const auto generation=session_->cache().generation;
    if(frontier_generation_==generation&&frontier_origins_==origins)return;
    frontier_generation_=generation;frontier_origins_=origins;colored_frontier_.clear();
    if(origins.size()==world.systems.size()){colored_frontier_.insert(origins.begin(),origins.end());return;}
    if(world.systems.size()>10000){
      std::vector<const StellarSystem*> ordered;ordered.reserve(world.systems.size());
      for(const auto& system:world.systems)ordered.push_back(&system);
      std::ranges::sort(ordered,{},[](auto* system){return system->id;});
      std::vector<stellar::engine::SpatialIndex3D::Point> positions;positions.reserve(ordered.size());
      std::unordered_map<int,std::size_t> indices;indices.reserve(ordered.size());
      for(std::size_t i=0;i<ordered.size();++i){const auto& p=ordered[i]->position;
        positions.push_back({p.x,p.y,p.depth_light_years.value_or(0.)});indices.emplace(ordered[i]->id,i);}
      const stellar::engine::SpatialIndex3D index(std::move(positions));
      for(int id:origins){colored_frontier_.insert(id);
        for(const auto& match:index.nearest(indices.at(id),3,false,[&](auto a,auto b){
              return squared_distance_light_years(ordered[a]->position,ordered[b]->position);}))
          colored_frontier_.insert(ordered[match.index]->id);
      }
      return;
    }
    for(int id:origins){
      const auto& origin=*session_->cache().systems_by_id.at(id);
      colored_frontier_.insert(id);
      std::vector<std::pair<double,int>> nearest;nearest.reserve(world.systems.size());
      for(const auto& candidate:world.systems)if(candidate.id!=id)nearest.emplace_back(squared_distance_light_years(origin.position,candidate.position),candidate.id);
      const auto count=std::min<std::size_t>(3,nearest.size());std::partial_sort(nearest.begin(),nearest.begin()+count,nearest.end());
      for(std::size_t i=0;i<count;++i)colored_frontier_.insert(nearest[i].second);
    }
  }
  [[nodiscard]] bool inspection_visible()const noexcept {
    return inspection_card_.visible()&&!colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&!menu_&&!system_workspace_.visible()&&
        !colony_workspace_.visible()&&
        !settlement_workspace_.visible()&&!research_workspace_.visible()&&
        !shipyard_workspace_.visible()&&!construction_workspace_.visible()&&
        !diplomacy_workspace_.visible()&&!battle_workspace_.visible()&&
        !notification_view_.visible()&&!chronicle_view_.visible()&&
        !settings_visible();
  }
  [[nodiscard]] bool map_hud_visible() const {
    return !menu_&&!settings_visible()&&!battle_workspace_.visible()&&!settlement_workspace_.visible()&&
        !colony_roster_.visible()&&!economy_workspace_.visible()&&!supply_workspace_.visible()&&
        !research_workspace_.visible()&&!shipyard_workspace_.visible()&&!construction_workspace_.visible()&&
        !diplomacy_workspace_.visible()&&!colony_workspace_.visible()&&
        !notification_view_.visible()&&!chronicle_view_.visible()&&
        !mission_view_.visible()&&!fleet_workspace_.preview();
  }
  void render_command_hud(DrawList& out,int width,int height){
    const auto l=CommandHudLayout::make(width,height);const float s=l.scale;
    fill(out,l.resource_strip,{5,18,26,250});
    out.overlay.emplace_back(Line{{0,l.resource_strip.height},{static_cast<float>(width),l.resource_strip.height},{53,109,119,220}});
    const auto& world=session_->frame().runtime().world().campaign();
    const auto economy=std::ranges::find(world.economies,world.player_civilization_id,&CivilizationEconomy::civilization_id);
    if(economy!=world.economies.end()){
      const float available=std::max(0.f,static_cast<float>(width)-356*s);
      const float cell=std::min(185*s,available/3.f);
      const std::array<std::pair<std::string,double>,3> resources{{{tr("HUD_CREDITS","CREDITS"),economy->credits},{tr("HUD_INDUSTRY","INDUSTRY"),economy->industry},{tr("HUD_SCIENCE","SCIENCE"),economy->science}}};
      const std::array<Color,3> colors{{{243,199,110,255},{233,164,124,255},{115,199,239,255}}};
      for(int i=0;i<3;++i){
        const float x=12*s+i*cell;
        hud_text(out,{x,4*s,cell-8*s,12*s},resources[i].first,static_cast<int>(9*s),colors[i]);
        hud_text(out,{x,17*s,cell-8*s,17*s},hud_amount(resources[i].second),static_cast<int>(13*s),{230,241,247,255});
      }
    }
    if(!map_hud_visible())return;
    if(!hud_crest_)hud_crest_=decode_rgba_image(asset_root_/"assets/visual/branding/stellar-continuum-icon-v1.png");
    if(!hud_galaxy_icon_)hud_galaxy_icon_=decode_rgba_image(asset_root_/"assets/visual/hud/galaxy-view.png");
    if(!hud_system_icon_)hud_system_icon_=decode_rgba_image(asset_root_/"assets/visual/hud/system-view.png");
    const auto player=std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);
    const bool in_system=system_workspace_.visible();
    std::string title=in_system?system_workspace_.snapshot()->catalog_name:player!=world.civilizations.end()?player->name:tr("HUD_EMPIRE","Empire");
    std::string subtitle=in_system?tr("HUD_SYSTEM_VIEW","SYSTEM VIEW"):player!=world.civilizations.end()?species_environment_profile(player->species_id).display_name:"";
    const bool paused=session_->frame().clock().speed()==StrategicSpeed::Paused;
    render_context_plate(out,l,title,subtitle,paused,pointer_,hud_crest_,in_system?hud_galaxy_icon_:hud_system_icon_,in_system||selected_id_.has_value(),locale_);
    const auto& view=colony_roster_.view();
    sync_asset_selection();
    assets_.render(out,width,height,[&](const stellar::native_assets::Row& asset){
      using stellar::native_assets::Category;
      if(asset.key.category==Category::Fleets)return navigation_art_.image(UiAction::Explore);
      if(asset.key.category==Category::Shipyards)return navigation_art_.image(UiAction::Shipyard);
      if(asset.key.category==Category::Stations)return navigation_art_.image(UiAction::Construction);
      const int body_id=asset.body_id;
      const auto row=std::ranges::find(view.rows,body_id,&stellar::native_colony_roster::Row::body_id);
      if(row==view.rows.end()||!row->can_open)return std::shared_ptr<const RgbaImage>{};
      auto found=hud_planet_appearances_.find(body_id);
      if(found==hud_planet_appearances_.end()){
        const auto built=system_controller_.build(session_->frame(),session_->cache().generation,row->system_id);
        if(!built.snapshot)return std::shared_ptr<const RgbaImage>{};
        const auto body=std::ranges::find(built.snapshot->bodies,body_id,&NativeSystemBody::id);
        if(body==built.snapshot->bodies.end())return std::shared_ptr<const RgbaImage>{};
        SystemBodyAppearance appearance;appearance.campaign_generation=session_->cache().generation;
        appearance.body_id=body_id;appearance.fully_surveyed=true;appearance.visual_class=body->visual_class;
        appearance.texture_key=body->sol_texture_key;appearance.deterministic_seed=static_cast<std::uint32_t>(body_id);
        appearance.canonical=body->appearance;
        found=hud_planet_appearances_.emplace(body_id,std::move(appearance)).first;
      }
      const auto& appearance=found->second;
      if(appearance.canonical)return planet_material_cache_.portrait(*appearance.canonical);
      return planet_discs_.request_image(appearance);
    });
    if(l.switch_view.contains(pointer_)){
      const UiRect tip{l.context.x,l.context.y-25*s,l.context.width,22*s};
      fill(out,tip,{5,18,26,240});hud_text(out,tip,in_system?tr("HUD_TIP_RETURN","Return to star map"):selected_id_?tr("HUD_TIP_OPEN","Open focused system"):tr("HUD_TIP_SELECT","Select an explored star"),static_cast<int>(12*s),{211,235,242,255},TextAlign::Center);
    }
  }
  [[nodiscard]] UiRect map_zoom_bounds(int width,int height) const {
    const auto layout=NativeUiLayout::for_viewport(width,height);
    auto bounds=layout.zoom_text;
    if(const auto panel=fleet_workspace_.panel_bounds(width,height))
      bounds.x=std::max(bounds.x,panel->x+panel->width+12.f*layout.scale);
    if(inspection_visible()) {
      const auto panel=inspection_bounds(width,height);
      bounds.x=std::max(bounds.x,panel.x+panel.width+12.f*layout.scale);
    }
    const auto assets=stellar::native_assets::Layout::make(width,height);
    const float right=assets_.preferences().hidden?width-12.f*layout.scale:assets.panel.x-12.f*layout.scale;
    bounds.width=std::min(bounds.width,std::max(0.f,right-bounds.x));
    return bounds;
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
  void refresh_assets(){
    if(!fleet_workspace_.view())return;
    refresh_roster(false);
    auto yard=shipyard_controller_.build(session_->frame(),session_->cache().generation);
    assets_.set_view(stellar::native_assets::build(session_->frame().runtime().world().campaign(),colony_roster_.view(),*fleet_workspace_.view(),&yard,[this](int id){return id<0?tr("SYSTEM_UNKNOWN","Unknown"):system_display_name(id);},locale_));
    assets_refresh_elapsed_=0.;
    if(system_workspace_.visible()){
      int body=-1;for(const auto& r:colony_roster_.view().rows)if(r.system_id==yard.home_system_id&&r.can_open){body=r.body_id;break;}
      system_workspace_.set_shipyard(yard.orbital_shipyard_complete&&system_workspace_.system_id()==yard.home_system_id?std::optional<int>(yard.player_civilization_id):std::nullopt,body,navigation_art_.image(UiAction::Shipyard));
    }
  }
  void sync_asset_selection(){
    using namespace stellar::native_assets;
    if(system_workspace_.visible()&&system_workspace_.selected_shipyard_owner()){assets_.set_selection(Key{Category::Shipyards,*system_workspace_.selected_shipyard_owner()});return;}
    if(system_workspace_.visible()&&system_workspace_.selected_body_id()){
      const int body=*system_workspace_.selected_body_id();
      const auto r=std::ranges::find_if(assets_.view().rows,[&](const Row& row){return (row.key.category==Category::Planets||row.key.category==Category::Outposts)&&row.body_id==body;});
      if(r!=assets_.view().rows.end()){assets_.set_selection(r->key);return;}
    }
    if(fleet_controller_.selection()){assets_.set_selection(Key{Category::Fleets,*fleet_controller_.selection()});return;}
    if(assets_.selection()&&assets_.selection()->category!=Category::Shipyards)assets_.set_selection({});
  }
  void execute_asset(const stellar::native_assets::Command& command,int width,int height){
    using namespace stellar::native_assets;
    if(!command.key||command.generation!=session_->cache().generation||command.observer!=session_->frame().runtime().world().campaign().player_civilization_id)return;
    refresh_assets();const auto it=std::ranges::find_if(assets_.view().rows,[&](const Row& r){return r.key==command.key;});
    if(it==assets_.view().rows.end()||!it->actionable)return;const auto row=*it;
    if(row.key.category==Category::Planets||row.key.category==Category::Outposts){
      fleet_controller_.clear_selection();refresh_fleets(true);
      if(!enter_system(row.system_id,width,height)||!system_workspace_.select_body(row.body_id))return;
      system_workspace_.focus_selected_body(width,height);
      if(command.manage)open_colony_from_system(row.body_id);
    }else if(row.key.category==Category::Fleets){
      const auto selected=fleet_controller_.select(session_->frame(),command.generation,row.key.id);if(!selected.accepted)return;
      refresh_fleets(true);const auto& fleets=fleet_workspace_.view()->own_fleets;const auto f=std::ranges::find(fleets,row.key.id,&NativeOwnFleet::id);if(f==fleets.end())return;
      // Entering a system refreshes the observer snapshot and invalidates f.
      const auto position=f->position;const auto current_system=f->current_system_id;
      if(current_system&&enter_system(*current_system,width,height)){refresh_system_travel(true);system_workspace_.focus_fleet(row.key.id,width,height);}
      else {system_workspace_.close();camera_.center={position.x,position.y};}
      if(command.manage){system_workspace_.close();camera_.center={position.x,position.y};selected_id_.reset();refresh_inspection();refresh_fleets(true);}
    }else if(row.key.category==Category::Shipyards){
      fleet_controller_.clear_selection();refresh_fleets(true);if(!enter_system(row.system_id,width,height))return;
      system_workspace_.focus_shipyard(width,height);
      if(command.manage){shipyard_workspace_.open();refresh_shipyard(true);}
    }
    assets_.set_selection(row.key,false);if(audio_confirm_)audio_confirm_();
  }
  void refresh_roster(bool force){
    const auto& world=session_->frame().runtime().world().campaign();
    const auto generation=session_->cache().generation;
    const auto day=session_->frame().clock().simulation_days();
    const auto& current=colony_roster_.view();
    const bool changed=current.generation!=generation||current.player_id!=world.player_civilization_id;
    if(!force&&!changed&&(!current.available|| (roster_day_&&(*roster_day_==day||roster_refresh_elapsed_<1.))))return;
    colony_roster_.set_view(stellar::native_colony_roster::build(world,generation,locale_));
    roster_day_=day;roster_refresh_elapsed_=0.;
  }
  void open_roster_colony(const stellar::native_colony_roster::RosterCommand& command,int width,int height){
    const auto generation=session_->cache().generation;
    const auto& world=session_->frame().runtime().world().campaign();
    if(command.generation!=generation||command.player_id!=world.player_civilization_id){
      refresh_roster(true);colony_roster_.set_notice(tr("ROSTER_NOTICE_CHANGED","That colony selection changed. Select it again."));return;
    }
    const auto live=stellar::native_colony_roster::build(world,generation,locale_);
    const auto row=std::ranges::find(live.rows,*command.open_colony_id,&stellar::native_colony_roster::Row::colony_id);
    if(!live.available||row==live.rows.end()||!row->can_open||row->system_id!=command.system_id||row->body_id!=command.body_id){
      refresh_roster(true);colony_roster_.set_notice(tr("ROSTER_NOTICE_GONE","That colony is no longer available to open."));return;
    }
    auto built=system_controller_.build(session_->frame(),generation,row->system_id);
    if(!built.snapshot){colony_roster_.set_notice(built.denial);return;}
    auto colony=colony_controller_.build(session_->frame(),generation,*built.snapshot,row->body_id);
    if(!colony.view||colony.view->colony_id!=row->colony_id){colony_roster_.set_notice(tr("ROSTER_NOTICE_UNAVAILABLE","Colony operations are unavailable. Refresh the list."));return;}
    if(!enter_system(row->system_id,width,height))return;
    if(!system_workspace_.select_body(row->body_id))return;
    system_workspace_.focus_selected_body(width,height);
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
  int starfield_quality()const{return video_settings_?video_settings_->active().starfield_quality:2;}
  int starfield_density()const{return video_settings_?video_settings_->active().starfield_density:1;}
  stellar::native_phenomena::VisualOptions phenomena_options(std::optional<int> system_id={}){
    auto options=developer_session()?phenomena_debug_.options:stellar::native_phenomena::VisualOptions{};
    if(system_id)options.local_nebula=system_background_.catalog.profile(*system_id).local_nebula;
    options.density=general_settings_?general_settings_->saved().nebula_density:1;options.background_stars=false;options.local_opacity=system_background_.options.nebula?system_background_.options.nebula_opacity:0;return options;
  }
  std::set<std::uint32_t> surveyed_phenomena(){
    std::set<std::uint32_t> result;const auto* field=phenomena_.field();if(!field)return result;
    const auto& world=session_->frame().runtime().world().campaign();
    for(const auto& r:field->regions)if(developer_session()||std::ranges::any_of(r.systems_contained,[&](int id){return world.knowledge.system_survey_level(world.player_civilization_id,id)>=SystemSurveyLevel::partially_surveyed;}))result.insert(r.id);
    return result;
  }
  std::string phenomena_debug_text(){
    const auto& world=session_->frame().runtime().world().campaign();const auto id=system_workspace_.visible()?system_workspace_.system_id():selected_id_;
    std::string text=phenomena_.field()?stellar::core::phenomenon_art_usage(*phenomena_.field()):phenomena_diagnostics(nullptr);
    if(world.generation_metadata&&world.generation_metadata->configuration)text="Galaxy seed "+std::to_string(world.seed)+" · "+world.generation_metadata->configuration->generator_version+"\n"+text;
    if(id){const auto system=std::ranges::find(world.systems,*id,&StellarSystem::id);if(system!=world.systems.end()){const auto& context=phenomena_.context(*id,system->position.x,system->position.y);text=system->name+" · system "+std::to_string(*id)+"\n"+phenomena_diagnostics(phenomena_.field(),&context)+"\n"+text;}}
    return text;
  }
  void render_system_environment(DrawList& out,int width,int height){
    const auto& world=session_->frame().runtime().world().campaign();const auto id=*system_workspace_.system_id();const auto system=std::ranges::find(world.systems,id,&StellarSystem::id);if(system==world.systems.end())return;
    const auto& context=phenomena_.context(id,system->position.x,system->position.y);if(!context.dominant)return;
    const auto& r=*std::ranges::find(phenomena_.field()->regions,*context.dominant,&GalaxyPhenomenon::id);
    const bool known=world.knowledge.system_survey_level(world.player_civilization_id,id)>=SystemSurveyLevel::partially_surveyed||developer_session();
    const float s=std::clamp(height/1080.f,.7f,1.7f);const auto layout=SystemWorkspaceLayout::for_viewport(width,height);const auto row=layout.controls_row;const UiRect box{row.x+row.width*.53f,row.y+4*s,row.width*.45f,row.height-8*s};
    std::ostringstream label;label<<(known?std::string(phenomenon_definition(r.type).name):tr("PHENOMENA_UNKNOWN","Uncharted interstellar cloud"));if(known)label<<" · "<<tr("PHENOMENA_SENSOR","Sensor range")<<" "<<static_cast<int>(context.effects.sensor*100)<<"% · "<<tr("PHENOMENA_SURVEY","Survey effort")<<" "<<std::fixed<<std::setprecision(2)<<context.effects.scanning<<"x";
    stellar::native_menu_style::text(out,box,label.str(),std::max(12,static_cast<int>(13*s)),{150,215,235,255});
  }
  void refresh_inspection() {
    if(!selected_id_){inspection_card_.clear();return;}
    inspection_card_.set_inspection(stellar::native_inspection::build_system_inspection(
        session_->frame().runtime().world().campaign(),*selected_id_,locale_));
  }
  void bind_galaxy_backdrop(int width,int height){const auto &world=session_->frame().runtime().world().campaign();GalaxyBackdropCatalog view;view.generated_phenomena=world.generation_metadata&&world.generation_metadata->phenomena.has_value();view.campaign_generation=session_->cache().generation;view.campaign_seed=world.seed;if(world.generation_metadata&&world.generation_metadata->configuration){const auto& config=*world.generation_metadata->configuration;const auto pair=galaxy_visual_pair(config.morphology,config.resolved_population);view.map_asset_path=pair.map_path;const auto frame=galaxy_footprint_frame(config.morphology,config.system_count,config.resolved_population);view.fixed_artwork_frame=GalaxyBackdropFrame{frame.left,frame.top,frame.width,frame.height};if(pair.fallback)stellar::engine::log("galaxy-assets",pair.diagnostic);}if(world.generation_metadata&&world.generation_metadata->stellar_population){const auto m=world.generation_metadata->stellar_population->morphology;view.use_spiral_artwork=m==GalaxyMorphology::Spiral||m==GalaxyMorphology::BarredSpiral;}view.system_positions.reserve(world.systems.size());for(const auto &system:world.systems)view.system_positions.push_back({system.position.x,system.position.y});if(world.core){view.galactic_core=WorldPoint{world.core->position.x,world.core->position.y};view.galactic_core_exclusion_radius=world.core->exclusion_radius;view.galactic_core_discovered=world.knowledge.is_galactic_core_discovered(world.player_civilization_id);}galaxy_backdrop_.bind(std::move(view));system_background_.bind(world);phenomena_.bind(world.generation_metadata&&world.generation_metadata->phenomena?&*world.generation_metadata->phenomena:nullptr);pinned_phenomenon_.reset();camera_=galaxy_overview_camera(width,height);fitted_pixels_per_world_=camera_.pixels_per_world;}
  void cycle_speed(){
    if(developer_session()){
      const auto current=session_->frame().runtime().world().campaign().developer_provenance->simulation.speed;
      const std::uint32_t next=current==1?2:current==2?5:current==5?10:current==10?25:1;
      session_->frame().set_developer_speed(next);return;
    }
    auto &clock=session_->frame().clock();const bool paused=clock.speed()==StrategicSpeed::Paused;StrategicSpeed next;switch(paused?clock.resume_speed():clock.speed()){case StrategicSpeed::Normal:next=StrategicSpeed::Fast;break;case StrategicSpeed::Fast:next=StrategicSpeed::VeryFast;break;case StrategicSpeed::VeryFast:next=StrategicSpeed::Maximum;break;default:next=StrategicSpeed::Normal;break;}if(paused)clock.select_resume_speed(next);else clock.set_speed(next);}
  void select(Point pointer,int width,int height){selected_id_=system_hit(pointer,width,height);pinned_phenomenon_=selected_id_?std::nullopt:std::optional{camera_.unproject(pointer,width,height)};refresh_inspection();}
  std::unique_ptr<NativeCampaignSession> session_;
  Camera camera_;
  mutable std::optional<stellar::engine::SpatialGrid<int>> system_hit_grid_;
  mutable std::uint64_t system_hit_grid_generation_{std::numeric_limits<std::uint64_t>::max()};
  int galaxy_view_width_{},galaxy_view_height_{};
  NativeGalaxyStarMarkerRenderer galaxy_star_markers_;
  stellar::native_stellar::Artwork stellar_art_;
  stellar::native_stellar::EruptionArtwork eruption_art_;
  std::unordered_set<int> colored_frontier_;
  std::vector<int> frontier_origins_;
  std::optional<std::uint64_t> frontier_generation_;
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
  native_missions::NativeMissionView mission_view_;
  native_missions::NativeMissionBoard mission_board_;
  std::vector<NativeSettlementMissionView> mission_fleets_;
  std::vector<native_missions::NativeMissionColonyRow> mission_colonies_;
  std::optional<std::uint64_t> mission_generation_;
  stellar::native_assets::Navigator assets_;
  NativeDeveloperSimulationPanel developer_panel_;
  stellar::app_diagnostics::CampaignDiagnosticMonitor developer_monitor_;
  stellar::native_developer::NativeDeveloperFaultCapture developer_fault_capture_;
  NativeDeveloperDiagnostics developer_diagnostics_;
  NativeDeveloperEmpireMonitor developer_empires_;
  NativeDeveloperCelestialIndex developer_index_;
  NativeDeveloperPlanetIndex developer_planet_index_;
  NativeGiantTestPanel giant_test_panel_;
  StellarActivityPanel stellar_activity_panel_;
  double assets_refresh_elapsed_{};
  std::shared_ptr<const RgbaImage> hud_crest_;
  std::shared_ptr<const RgbaImage> hud_galaxy_icon_,hud_system_icon_;
  std::unordered_map<int,SystemBodyAppearance> hud_planet_appearances_;
  bool hud_switch_pressed_{};
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
  NativeFleetWorkspace fleet_workspace_{FleetWorkspacePresentation::SelectedCommands};
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
  std::unordered_map<std::string,std::shared_ptr<const RgbaImage>>
      overview_portraits_;
  stellar::native_overview::OverviewImageProvider overview_portrait_provider_ =
      [this](std::string_view relative){
        auto [entry,inserted]=overview_portraits_.try_emplace(std::string(relative));
        if(inserted){
          try{entry->second=decode_rgba_image(asset_root_/entry->first);}
          catch(...){entry->second.reset();}
        }
        return entry->second;
      };
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
  std::vector<DrawList> smoke_planetary_captures_;
  std::array<std::shared_ptr<const RgbaImage>,2> planetary_art_;
  stellar::native_planets::MaterialCache planet_material_cache_;
  std::array<std::shared_ptr<const RgbaImage>,stellar::native_system_ui::planet_surface_assets.size()> planetary_globe_maps_;
  NativeSettlementMissionController settlement_controller_;
  NativeSettlementWorkspace settlement_workspace_;
  std::optional<NativeColonyView> colony_entry_view_;
  NativeSystemViewController system_controller_;
  NativeSystemTravelController system_travel_controller_;
  NativeGalaxyBackdropAssets galaxy_assets_;
  NativeGalaxyBackdrop galaxy_backdrop_;
  stellar::native_phenomena::NativePhenomena phenomena_;
  NativeSystemBackground system_background_;
  NativeBackgroundDebug background_debug_;
  stellar::native_phenomena::PhenomenaDebug phenomena_debug_;
  std::optional<WorldPoint> pinned_phenomenon_;
  double fitted_pixels_per_world_{.01};
  std::shared_ptr<ImagePreparationQueue> image_preparation_{std::make_shared<ImagePreparationQueue>()};
  NativePlanetDiscAssets planet_discs_;
  stellar::native_system_ui::NativeSmallBodyAssets small_body_assets_;
  NativeShipArtAssets ship_art_;
  native_battle_art::NativeBattleSprites battle_sprites_;
  std::vector<native_battle_art::BattleArtBinding> battle_art_bindings_;
  std::vector<native_battle_art::BattleArtSprite> battle_art_plan_;
  bool battle_art_suppressed_{};
  bool smoke_battle_ship_selected_{};
  SystemTextMeasurer text_measurer_;
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
  bool smoke_fleet_hover_preview_{};
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
  bool smoke_settlement_mode_{},smoke_settlement_reload_{},smoke_settlement_selected_{},smoke_settlement_previewed_{},smoke_settlement_accepted_{};
  bool smoke_settlement_cancelled_{},smoke_settlement_cancel_no_charge_{},smoke_settlement_requires_authorization_{},smoke_settlement_no_instant_colony_{};
  std::optional<int> smoke_settlement_fleet_id_,smoke_settlement_system_id_,smoke_settlement_body_id_;
  NativeSettlementMissionKind smoke_settlement_kind_{NativeSettlementMissionKind::Colony};
  std::size_t smoke_settlement_colonies_before_{};
  int smoke_settlement_revision_{};double smoke_settlement_before_day_{},smoke_settlement_saved_day_{},smoke_settlement_progress_{},smoke_settlement_authorization_{},smoke_settlement_treasury_before_{},smoke_settlement_treasury_after_{};
  std::function<void()> audio_confirm_;
  stellar::native_campaign_feedback::NativeCampaignFeedback feedback_;
  stellar::native_notifications::NativeNotificationFeed notifications_;
  stellar::engine::AccessibilityAnnouncer announcer_;
  std::optional<stellar::native_audio::VoiceCaption> announcement_caption_;
  std::uint64_t announcement_seq_{};
  stellar::native_support::NativeSupportService support_;
  native_battle_ui::NativeBattleWorkspace battle_workspace_;
  double battle_refresh_elapsed_{};
  bool last_battle_order_accepted_{};
  std::string smoke_battle_canonical_,smoke_battle_utc_;
  double smoke_battle_day_{};
  bool smoke_battle_reload_{},smoke_battle_paused_speed_{},smoke_battle_menu_pause_{};
  std::string diagnostic_executable_hash_;
  std::string support_environment_,last_support_notice_;
  // Optional distribution services (Steam-style). Standalone runs on the null
  // backend; attach a real backend here when one ships.
  stellar::engine::PlatformServices platform_services_;
  stellar::engine::MemoryTracker::SubsystemId planet_material_memory_{stellar::engine::MemoryTracker::invalid_subsystem};
  stellar::engine::MemoryTracker::SubsystemId territory_overlay_memory_{stellar::engine::MemoryTracker::invalid_subsystem};
  stellar::engine::MemoryTracker::SubsystemId image_preparation_memory_{stellar::engine::MemoryTracker::invalid_subsystem};
  stellar::engine::MemoryTracker::SubsystemId replay_recorder_memory_{stellar::engine::MemoryTracker::invalid_subsystem};
  SessionNoticeKind last_support_notice_kind_{};
  double support_notice_seconds_{};
  stellar::native_notifications::NativeNotificationView notification_view_;
  stellar::native_notifications::NativeDiplomaticNotifications diplomatic_notifications_;
  stellar::native_chronicle::NativeChronicleView chronicle_view_;
  double notification_refresh_elapsed_{};
  stellar::engine::InputMapper input_mapper_;
  int research_candidate_index_{};
  int construction_candidate_index_{};
  ReplayState *replay_{};
  stellar::native_audio::NativeAudioDirector* presentation_audio_{};
  // Event-driven gameplay voice pipeline (bridge -> router -> playback) bound
  // onto the director's voice channel. Destruction order matters: the bridge
  // references the router, which references the resolver and profiles.
  stellar::native_voice::NativeVoiceProfileRegistry voice_profiles_{};
  std::optional<stellar::native_voice::NativeCharacterVoiceResolver> voice_resolver_;
  std::optional<stellar::native_voice::NativeVoiceCache> voice_cache_;
  std::optional<stellar::native_voice::NativeVoicePlayback> voice_playback_;
  std::optional<stellar::native_voice::NativeVoiceRouter> voice_router_;
  std::optional<stellar::native_voice::NativeGameplayVoiceBridge> voice_bridge_;
  const stellar::engine::LocalizationTable *locale_{};
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const{
    if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
    return std::string(fallback);
  }
  [[nodiscard]] std::string trf(std::string_view key,std::initializer_list<std::string> args,
                                std::string_view fallback)const{
    if(locale_&&locale_->contains(key)){
      const std::vector<std::string> values(args.begin(),args.end());
      return locale_->format(key,std::span<const std::string>(values));
    }
    std::string out{fallback};
    std::size_t index=0;
    for(const auto& arg:args){
      const std::string marker="{"+std::to_string(index++)+"}";
      if(const auto at=out.find(marker);at!=std::string::npos)out.replace(at,marker.size(),arg);
    }
    return out;
  }
  int voice_ui_sequence_{};
  stellar::native_menu_audio::HoverFeedback menu_hover_feedback_;
  std::chrono::steady_clock::time_point last_event_sound_{};
  bool settings_visible() const { return (settings_hub_&&settings_hub_->visible()) || (voice_settings_&&voice_settings_->visible()) || (general_settings_&&general_settings_->visible()) || (audio_settings_&&audio_settings_->visible()) || (video_settings_&&video_settings_->visible()); }
  stellar::native_general::NativeGeneralSettings* general_settings_{};
  stellar::native_settings::NativeSettingsHub* settings_hub_{};
  stellar::native_audio::NativeVoiceSettings* voice_settings_{};
  stellar::native_client::NativeAccessibilityBridge* accessibility_bridge_{};
  stellar::native_video_settings::NativeVideoController* video_settings_{};
  stellar::native_audio::NativeAudioSettings* audio_settings_{};
  bool menu_{};int menu_focus_{-1};int map_focus_group_{-1};int hud_focus_{-1};bool smoke_save_pending_{};bool smoke_shortcut_{};Point pointer_{};PointerGesture gesture_; StrategicSpeed pre_menu_speed_{StrategicSpeed::Paused};
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
  stellar::engine::RuntimeDiagnostics diagnostics(STELLAR_GAME_VERSION,STELLAR_ENGINE_VERSION);
  try{
    const auto options=parse_options(argc,argv);
    // --replay-info: headless recording inventory — header, command-stream
    // summary, and per-tick checkpoint counts with expected-sidecar presence
    // (the ticks --replay-until bisects on). Exits before window/session
    // creation so it runs in scripts without a GPU or a valid install.
    if(!options.replay_info_path.empty()){
      std::ifstream in(options.replay_info_path,std::ios::binary);
      if(!in)
        throw std::invalid_argument("Cannot open replay recording: "+
            utf8_path(options.replay_info_path));
      std::ostringstream contents;contents<<in.rdbuf();
      std::string parse_error;
      const auto recording=stellar::engine::ReplayRecorder::parse(
          contents.str(),&parse_error);
      if(!recording)
        throw std::invalid_argument("Replay file is not a valid recording: "+
            parse_error);
      std::cout<<stellar::engine::replay_info_json(*recording,
          options.replay_info_path)<<'\n';
      return 0;
    }
    stellar::engine::RuntimeDiagnostics::context(options.dev_game?"startup developer game":"startup player game");
    const auto installation_root=stellar::engine::executable_directory();
    stellar::engine::RuntimeDirectoryLease maintenance_lease(installation_root);
    if(std::filesystem::exists(installation_root/".stellar-transaction"))
      throw std::runtime_error("An interrupted installation needs recovery. Run StellarContinuumSetup.exe before starting the game.");
#ifdef _WIN32
    char dev_buffer[8]{};std::size_t dev_length{};
    const char* dev_environment=getenv_s(&dev_length,dev_buffer,sizeof(dev_buffer),"STELLAR_CONTINUUM_DEVTOOLS")==0?dev_buffer:nullptr;
#else
    const char* dev_environment=std::getenv("STELLAR_CONTINUUM_DEVTOOLS");
#endif
    stellar::engine::DeveloperAccess developer_access(options.devtools||
      (dev_environment&&(std::string_view(dev_environment)=="1"||std::string_view(dev_environment)=="true")));
    if(options.dev_game)(void)developer_access.set_active(true);
    const auto asset_root=std::filesystem::absolute(options.asset_root);
    if(std::filesystem::is_regular_file(asset_root/"Content/runtime.stmanifest")||
       std::filesystem::is_regular_file(stellar::engine::executable_directory()/"cooked-only.marker"))
      stellar::engine::mount_asset_registry(asset_root,false);
    struct CookedReadReport {
      ~CookedReadReport(){if(auto registry=stellar::engine::mounted_asset_registry()){
        const auto d=registry->diagnostics();std::cout<<"cooked_assets={\"assets\":"<<d.assets<<",\"packages\":"<<d.packages<<",\"reads\":"<<d.reads<<",\"stored_bytes_read\":"<<d.bytes_read<<",\"decoded_bytes\":"<<d.decoded_bytes<<",\"failures\":"<<d.failures<<",\"source_fallback\":false}\n";
      }}
    } cooked_read_report;
    const auto startup_begin=std::chrono::steady_clock::now();
    const auto settings_path=options.save_path.parent_path()/"audio-settings.json";
    const auto video_settings_path=settings_path.parent_path()/"video-settings.json";
    const auto general_settings_path=settings_path.parent_path()/"general-settings.json";
    const auto initial_video=stellar::native_video_settings::NativeVideoSettings::load(video_settings_path);
    auto launch_video=initial_video.for_startup();
    // Capture dimensions belong to the test viewport, not the player's saved
    // preferences. Normal --windowed launches still expose their actual mode.
    if(options.windowed&&!options.smoke_screenshot){launch_video.display=stellar::native_video_settings::VideoDisplayMode::Windowed;
      launch_video.width=options.window_width;launch_video.height=options.window_height;launch_video.refresh_hz=0.f;}
    Window window("Stellar Continuum - Native Galaxy",options.window_width,
                  options.window_height,!options.windowed&&initial_video.display!=stellar::native_video_settings::VideoDisplayMode::Windowed,
                  asset_root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");
    stellar::native_client::NativeAccessibilityBridge accessibility_bridge;
    if(!accessibility_bridge.attach(window.native_window_handle()))
      SDL_Log("Accessibility bridge unavailable on this platform.");
    // English is the built-in baseline; a shipped Data/locale/<locale>.json
    // table overrides panel text through the engine localization service and
    // falls back to English for any key it does not cover.
    const auto load_locale=[&](stellar::engine::LocalizationTable& table,const std::string& id){
      if(auto stream=stellar::engine::resource_stream(asset_root/"Data/locale"/(id+".json"))){
        std::ostringstream contents;contents<<stream.rdbuf();
        std::string error;
        if(!table.load_json(contents.str(),&error))std::cerr<<"Locale catalog '"<<id<<"' rejected: "<<error<<'\n';
      }
    };
    std::vector<std::string> locale_ids{"en"};
    try {
      for(const auto& entry:std::filesystem::directory_iterator(asset_root/"Data/locale"))
        if(entry.is_regular_file()&&entry.path().extension()==".json"){
          const auto id=entry.path().stem().string();
          if(id!="en"&&std::find(locale_ids.begin(),locale_ids.end(),id)==locale_ids.end())locale_ids.push_back(id);
        }
      std::sort(locale_ids.begin()+1,locale_ids.end());
    } catch(const std::exception& error){std::cerr<<"Locale discovery failed: "<<error.what()<<'\n';}
    stellar::engine::LocalizationTable locale_table{"en","en"};
    load_locale(locale_table,"en");
    stellar::native_general::NativeGeneralSettings general_settings(general_settings_path);
    if(const auto& wanted=general_settings.saved().locale;
       wanted!="en"&&std::find(locale_ids.begin(),locale_ids.end(),wanted)!=locale_ids.end()){
      locale_table=stellar::engine::LocalizationTable{wanted,"en"};
      load_locale(locale_table,wanted);load_locale(locale_table,"en");
    }
    general_settings.set_locales(locale_ids);
    general_settings.set_localization(&locale_table);
    window.set_screenshot_directory(general_settings.saved().screenshot_directory);
    stellar::native_map::NativeUiLayout::set_user_scale(
      stellar::native_general::interface_scale_multiplier(general_settings.saved().interface_scale));
    stellar::native_map::NativeUiLayout::set_text_scale(
      general_settings.saved().effective().text_scale);
    general_settings.set_text_measurer([&](const Text& text){return window.measure_text(text);});
    general_settings.set_apply([&](const auto& value){
      window.set_screenshot_directory(value.screenshot_directory);
      stellar::native_map::NativeUiLayout::set_user_scale(
        stellar::native_general::interface_scale_multiplier(value.interface_scale));
      stellar::native_map::NativeUiLayout::set_text_scale(
        value.effective().text_scale);
      if(value.locale!=locale_table.locale()){
        locale_table=stellar::engine::LocalizationTable{value.locale,"en"};
        load_locale(locale_table,value.locale);load_locale(locale_table,"en");
      }});
    try{general_settings.set_default_directory(Window::default_screenshot_directory());}
    catch(const std::exception& error){std::cerr<<"Default screenshot folder unavailable: "<<error.what()<<'\n';}
    general_settings.set_browse([&](auto id,const auto& path){return window.request_folder_dialog(id,path);});
    const auto service_general=[&]{if(auto result=window.take_folder_dialog_result())general_settings.accept_browse_result(std::move(*result));};
    // Declared after Window: audio closes its streams/device before SDL teardown.
    stellar::native_audio::NativeAudioDirector audio(asset_root,!options.smoke_screenshot||options.audio_check);
    // Audio queue occupancy joins the memory census — the bounded music and
    // voice queues report their current fill against their combined limit.
    stellar::engine::MemoryTracker::SubsystemId audio_queue_memory_{
        stellar::engine::MemoryTracker::invalid_subsystem};
    stellar::native_audio::NativeAudioSettings audio_settings(settings_path,
      [&audio](const stellar::native_audio::AudioPreferences& value){audio.set_volumes(value.muted?0.f:value.master,value.music,value.effects);},
      [&audio]{audio.confirm();});
    audio_settings.set_localization(&locale_table);
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
    video_settings.set_localization(&locale_table);
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
    stellar::native_voice::NativeVoicePlayback* active_voice_playback{};
    stellar::native_audio::NativeVoiceSettings voice_settings(settings_path.parent_path()/"voice-settings.json",
      [&](const auto& value){audio.set_voice_preferences(value);if(active_voice_playback)active_voice_playback->apply_settings(NativeCampaign::voice_pipeline_settings_for(value));},
      [&]{(void)audio.replay_last_voice();if(active_voice_playback)active_voice_playback->replay_last();},
      [&]{audio.stop_voice();if(active_voice_playback)active_voice_playback->stop();});
    voice_settings.set_localization(&locale_table);
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
    settings_hub.set_localization(&locale_table);
    bool audio_menu_ready{};std::size_t audio_boot_services{};
    const StartupAudioHooks audio_hooks{
      [&]{audio.service();service_general();audio_settings.set_device_status(audio.failure_message());if(!audio_menu_ready){++audio_boot_services;if(audio.stats().music_started)throw std::runtime_error("Music started before the startup menu was ready.");}},
      [&]{audio_menu_ready=true;audio.menu_ready();},
      [&]{audio.confirm();},[&]{return audio.assets_ready();},[&]{audio.hover();}};
    stellar::engine::AccessibilityAnnouncer startup_announcer;
    std::optional<stellar::native_audio::VoiceCaption> startup_announcement;
    const auto startup_config=[&]{
      StartupEntryConfig config{{asset_root/"Data/research/v1",asset_root/"Data/astronomy/hyg-nearby-500-v1.json",options.save_path,STELLAR_GAME_VERSION},asset_root,utc_timestamp};
      config.developer_access=&developer_access;config.locale=&locale_table;
      config.audio=audio_hooks;config.audio_settings=&audio_settings;config.video_settings=&video_settings;config.general_settings=&general_settings;config.settings_hub=&settings_hub;config.voice_settings=&voice_settings;config.announcer=&startup_announcer;config.caption=[&](DrawList& draw,int w,int h){while(auto item=startup_announcer.take()){if(item->kind==stellar::engine::AnnouncementKind::Focus)accessibility_bridge.focus_changed(item->text,item->bounds,item->range,item->control);else accessibility_bridge.announce(item->text);if(!item->text.empty())startup_announcement={"",std::move(item->text),std::chrono::steady_clock::now()+std::chrono::seconds(4)};}std::optional<stellar::native_audio::VoiceCaption> ui;if(startup_announcement&&std::chrono::steady_clock::now()<startup_announcement->expires_at&&audio.voice_preferences().subtitles)ui=startup_announcement;stellar::native_audio::render_voice_caption(draw,&audio,w,h,[&](const Text& t){return window.measure_text(t);},general_settings.saved().effective(),nullptr,ui);};return config;
    };
    std::unique_ptr<NativeCampaignSession> session;
    StartupEntryEvidence startup_evidence,restart_evidence;
    std::optional<std::filesystem::path> generated_save_path,setup_screenshot,loading_screenshot,restart_save_path;
    bool new_game_restart{};
    ReplayState replay;
    if(!options.record_path.empty()){
      replay.recorder.emplace(stellar::engine::ReplayHeader{
          static_cast<std::uint64_t>(options.seed),STELLAR_SOURCE_COMMIT,
          STELLAR_GAME_VERSION,
          static_cast<std::uint32_t>(window.drawable_width()),
          static_cast<std::uint32_t>(window.drawable_height())});
      // Bounded recording: past the budget the recorder keeps an honest
      // prefix (no later commands or checkpoints claim fidelity) and
      // serializes a truncated flag rather than growing without limit.
      replay.recorder->set_memory_budget(128ull*1024*1024);
    }
    if(!options.replay_path.empty()){
      std::ifstream replay_file(options.replay_path,std::ios::binary);
      if(!replay_file)throw std::invalid_argument("Replay file cannot be opened.");
      std::ostringstream contents;contents<<replay_file.rdbuf();
      std::string parse_error;
      auto parsed=stellar::engine::ReplayRecorder::parse(contents.str(),&parse_error);
      if(!parsed)throw std::invalid_argument("Replay file is not a valid recording: "+parse_error);
      // Provenance check: a recording made under a different seed or build
      // cannot reproduce this session — flag it so a divergence is read as
      // a provenance mismatch, not a simulation defect. Advisory only:
      // cross-build replay is a legitimate compatibility probe.
      // build_id carries the source commit in new recordings; older ones
      // stamped the game version there — skip the commit check for those.
      const bool legacy_header=
          parsed->header().build_id==parsed->header().game_version;
      if(parsed->header().seed!=static_cast<std::uint64_t>(options.seed)||
         (!legacy_header&&parsed->header().build_id!=STELLAR_SOURCE_COMMIT)||
         parsed->header().game_version!=STELLAR_GAME_VERSION)
        std::cerr<<"Stellar Continuum native client: replay provenance differs "
                   "(recorded seed "<<parsed->header().seed<<" vs "<<options.seed
                 <<", recorded build "<<parsed->header().build_id
                 <<" vs "<<STELLAR_SOURCE_COMMIT
                 <<", recorded version "<<parsed->header().game_version
                 <<" vs "<<STELLAR_GAME_VERSION
                 <<") — divergence may reflect the mismatch.\n";
      // Pointer commands carry drawable-pixel positions — a replay under a
      // different drawable size (windowed vs scaled fullscreen) lands them
      // on different UI cells, so flag the mismatch like a provenance
      // difference.
      if(parsed->header().window_width!=0&&parsed->header().window_height!=0&&
         (parsed->header().window_width!=static_cast<std::uint32_t>(window.drawable_width())||
          parsed->header().window_height!=static_cast<std::uint32_t>(window.drawable_height())))
        std::cerr<<"Stellar Continuum native client: recording was made on a "
                 <<parsed->header().window_width<<"x"<<parsed->header().window_height
                 <<" drawable — replaying at "<<window.drawable_width()<<"x"
                 <<window.drawable_height()
                 <<" moves pointer hit-testing; divergence may reflect the mismatch.\n";
      // A truncated recording is an honest prefix: its command stream and
      // checkpoints end mid-session, so nothing past them is verified.
      if(parsed->truncated())
        std::cerr<<"Stellar Continuum native client: recording is truncated "
                   "(memory budget) — commands and checkpoints end "
                   "mid-session; nothing past the prefix is verified.\n";
      // The feed loop and cursor verifier assume non-decreasing ticks —
      // a hand-edited recording would silently drop commands.
      if(!std::ranges::is_sorted(parsed->commands(),{},
             &stellar::engine::ReplayCommand::tick)||
         !std::ranges::is_sorted(parsed->checkpoints(),{},
             &stellar::engine::ReplayCheckpoint::tick))
        std::cerr<<"Stellar Continuum native client: recording streams are "
                   "not in tick order — out-of-order entries never fire.\n";
      replay.recording=std::move(parsed);
    }
    // Expected-document sidecars live next to the recording: record writes
    // <path>.expected/<tick>.json, replay reads them for leaf-level diffs.
    if(!options.record_path.empty())
      replay.expected_directory=options.record_path.generic_string()+".expected";
    else if(!options.replay_path.empty())
      replay.expected_directory=options.replay_path.generic_string()+".expected";
    replay.exit_on_completion=options.replay_exit;
    if(options.replay_until_tick){
      replay.stop_at_tick=options.replay_until_tick;
      replay.stop_dump_path=options.replay_path.parent_path()/
          ("replay-until-"+std::to_string(*options.replay_until_tick)+".json");
    }
    // Fixed-step: record/replay share one deterministic advance quantum so
    // command ticks and checkpoint days reproduce exactly.
    constexpr double kReplayStepSeconds=1./60.;
    const bool fixed_step=!options.record_path.empty()||!options.replay_path.empty();
    const ReplayFileFlush replay_flush{&replay,options.record_path};
    if(options.new_game_restart_smoke&&!std::filesystem::is_regular_file(options.save_path))
      throw std::invalid_argument("--new-game-restart-smoke requires a preexisting save-path anchor.");
    if(options.new_game_smoke){
      if(!std::filesystem::is_regular_file(options.save_path))throw std::invalid_argument("--new-game-smoke requires a preexisting save-path anchor.");
      setup_screenshot=sidecar_path(*options.smoke_screenshot,L"-setup");
      loading_screenshot=sidecar_path(*options.smoke_screenshot,L"-loading");
      StartupEntryAutomation automation{std::to_string(options.seed),"pelagic_high_pressure",options.smoke_system_count,*setup_screenshot,*loading_screenshot};
      automation.galaxy_card=options.smoke_galaxy_card;
      automation.full_exploration=options.smoke_full_exploration;
      automation.developer_mode=options.developer_smoke;automation.complete_normal_research=options.developer_smoke;automation.full_celestial_coverage=options.developer_smoke;
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
      if(options.dev_game&&(!result.session->frame().runtime().world().campaign().developer_provenance||
          generated_save_path->parent_path().filename()!="developer"))
        throw std::runtime_error("Developer launcher did not create an isolated developer campaign.");
      session=std::move(result.session);
      if(options.smoke_full_exploration){
        const auto &world=session->frame().runtime().world().campaign();
        if(!world.developer_provenance||!world.developer_provenance->full_exploration)
          throw std::runtime_error("Full exploration provenance did not reach the developer campaign.");
        if(std::ranges::any_of(world.systems,[&](const auto &system){return !world.knowledge.is_system_fully_surveyed(world.player_civilization_id,system.id);}))
          throw std::runtime_error("Full exploration left unsurveyed systems.");
        if((world.core||world.galactic_core)&&!world.knowledge.is_galactic_core_discovered(world.player_civilization_id))
          throw std::runtime_error("Full exploration left the generated galactic core undiscovered.");
        std::cout<<"developer_full_exploration="<<world.systems.size()<<" systems fully surveyed; generated_core="<<(world.core?"yes":"no")<<"\n";
      }
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
                             [&window](const Text &label){return window.measure_text(label);},[&]{audio.confirm();},&audio_settings,&audio,&video_settings,&general_settings,&settings_hub,&voice_settings,&accessibility_bridge);
    campaign.attach_replay(&replay);
    campaign.set_locale(locale_table);
    // Rebindable galaxy controls: the hub's Controls view edits the live
    // mapper and persists the rebound map beside the other settings files.
    const auto controls_path=settings_path.parent_path()/"galaxy-controls.json";
    campaign.load_user_bindings(controls_path);
    settings_hub.set_input_mapper(&campaign.input_mapper(),"GALAXY","GALAXY_PAD");
    settings_hub.set_bindings_persist([&campaign,&controls_path]{
      const auto text=campaign.input_mapper().save_contexts();
      try {
        stellar::engine::write_file_atomically(controls_path,
          std::span{reinterpret_cast<const std::byte*>(text.data()),text.size()});
      } catch(const std::exception& error){std::cerr<<"Input bindings save failed: "<<error.what()<<'\n';}
    });
    active_voice_playback=campaign.voice_playback();
    campaign.configure_support(window.gpu_driver(),window.presentation_mode());
    std::cout<<"renderer="<<window.gpu_driver()<<" presentation="<<window.presentation_mode()<<" drawable="<<window.drawable_width()<<'x'<<window.drawable_height()<<'\n';
    if(options.smoke_screenshot){
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
      else if(options.developer_smoke)campaign.prepare_developer_smoke();
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
    std::optional<std::chrono::steady_clock::time_point> smoke_save_wait_since;
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
    bool campaign_started{},campaign_pause_requested{},campaign_final_requested{},campaign_mid_requested{},campaign_mid_saved{},campaign_advanced_after_mid{};int campaign_wait_frames{};bool new_game{};
    double campaign_before_days{},campaign_after_days{},campaign_mid_save_day{},campaign_mid_save_completed_day{},campaign_frozen_days{};
    while(true){
      const auto now=std::chrono::steady_clock::now();
      const auto measured_elapsed=std::chrono::duration<double>(now-prior).count();
      prior=now;
      auto input=window.poll();
      if(!options.smoke_screenshot)std::erase_if(input.events,[&](const InputEvent &event){
        if(!is_developer_shortcut(event))return false;
        if(developer_access.eligible()){
          (void)developer_access.set_active(true);campaign.developer_shortcut();
        }
        return true;
      });
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

      audio.service();
      if(audio_queue_memory_==stellar::engine::MemoryTracker::invalid_subsystem)
        audio_queue_memory_=stellar::engine::MemoryTracker::instance().register_subsystem("audio-queues");
      if(const auto audio_stats=audio.stats();audio_stats.music_queue_limit_bytes!=0)
        stellar::engine::MemoryTracker::instance().report(audio_queue_memory_,
            audio_stats.queued_music_bytes+audio_stats.queued_voice_bytes,
            audio_stats.music_queue_limit_bytes+audio_stats.voice_queue_limit_bytes);
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
        if(campaign.new_game_ready()){new_game=true;break;}
        window.set_text_input(campaign.wants_text_input());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }
      const auto elapsed=discard_elapsed?0.:(fixed_step?kReplayStepSeconds:measured_elapsed);
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
        if(!campaign.developer_session())config.host.default_save_path=campaign.save_path();
        audio.stop_voice();if(active_voice_playback)active_voice_playback->stop();window.set_text_input(false);
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
        if((options.research_smoke||options.fleet_smoke||options.shipyard_smoke||options.construction_smoke||options.system_smoke||options.system_travel_smoke||options.system_travel_reload_smoke||options.colony_smoke||options.colony_reload_smoke||options.settlement_smoke||options.settlement_reload_smoke||options.galaxy_art_smoke||options.ship_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)&&((!options.voice_check&&frames==60)||(options.voice_check&&voice_prepared&&frames==capture_frame-60)))
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
      // Save latency grows with the authoritative catalog. Keep pumping the UI
      // and async writer instead of treating a fixed frame count as completion.
      const bool waiting_for_save=options.smoke_screenshot&&!options.campaign_profile&&
          frames>=capture_frame&&!campaign.smoke_save_succeeded();
      if(waiting_for_save){
        if(campaign.campaign_profile_notice()==SessionNoticeKind::Failure)
          throw std::runtime_error("Native session smoke save failed.");
        if(!smoke_save_wait_since)smoke_save_wait_since=scene_end;
        if(scene_end-*smoke_save_wait_since>std::chrono::seconds(120))
          throw std::runtime_error("Native session smoke save did not complete within 120 seconds.");
        screenshot.reset();
      }
      const bool waiting_for_capture=waiting_for_artwork||waiting_for_save;
      if(options.smoke_screenshot){
        if(!artwork_ready){++artwork_pending_frames;if(!artwork_pending_since)artwork_pending_since=scene_begin;}
        else if(artwork_pending_since){artwork_prepare_max_ms=std::max(artwork_prepare_max_ms,std::chrono::duration<double,std::milli>(scene_end-*artwork_pending_since).count());artwork_pending_since.reset();}
      }
      if(waiting_for_artwork){screenshot.reset();if(++artwork_wait_frames>600){window.draw(scene,sidecar_path(*options.smoke_screenshot,L"-incomplete"));throw std::runtime_error("Map artwork did not finish preparation before capture: "+campaign.artwork_status());}}
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
      if(!waiting_for_capture&&options.galaxy_art_smoke){
        if(frames==capture_frame){campaign.capture_galaxy_overview(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_regional(input.drawable_width,input.drawable_height);}
        else if(frames==capture_frame+1){campaign.capture_galaxy_regional(input.drawable_width,input.drawable_height);campaign.prepare_galaxy_system(input.drawable_width,input.drawable_height);}
        else if(frames==capture_frame+2)campaign.capture_galaxy_system(input.drawable_width,input.drawable_height);
      }
      if(!waiting_for_capture&&options.ship_art_smoke){
        if(frames==capture_frame)campaign.capture_ship_art_shipyard();
        else if(frames==capture_frame+1)campaign.capture_ship_art_map();
      }
      if(!waiting_for_capture&&(options.diplomacy_smoke||options.diplomacy_reload_smoke)&&frames==capture_frame)
        campaign.capture_diplomacy_unknown(input.drawable_width,input.drawable_height);
      if(!waiting_for_capture&&(options.diplomacy_smoke||options.diplomacy_reload_smoke)&&frames==capture_frame+1)
        campaign.prepare_diplomacy_map_capture(input.drawable_width,input.drawable_height);
      const bool capture=!waiting_for_capture&&options.smoke_screenshot&&(options.campaign_profile?screenshot.has_value():((options.galaxy_art_smoke||options.diplomacy_smoke||options.diplomacy_reload_smoke)?frames>=capture_frame+3:options.ship_art_smoke?frames>=capture_frame+2:frames>=capture_frame));
      if(capture){
        if(options.galaxy_art_smoke)campaign.capture_stellar_art_smoke(input.drawable_width,input.drawable_height,
            [&](const DrawList& draw,const wchar_t* suffix){window.draw(draw,suffix?std::optional<std::filesystem::path>{sidecar_path(*options.smoke_screenshot,suffix)}:std::nullopt);});
        if(options.developer_smoke&&!options.eruption_smoke)campaign.capture_developer_index_smoke(window.drawable_width(),window.drawable_height(),
            [&](const DrawList &draw,std::wstring_view suffix){window.draw(draw,sidecar_path(*options.smoke_screenshot,std::wstring(suffix).c_str()));});
        if(options.developer_smoke)campaign.capture_eruption_smoke(window.drawable_width(),window.drawable_height(),
            [&](const DrawList& draw,const wchar_t* suffix){window.draw(draw,suffix?std::optional<std::filesystem::path>{sidecar_path(*options.smoke_screenshot,suffix)}:std::nullopt);});
        if(options.colony_smoke||options.colony_reload_smoke){
          if(options.planetary_smoke)campaign.capture_planetary_smoke([&](const DrawList& draw,int i){const auto suffix=L"-planetary-"+std::to_wstring(i);window.draw(draw,sidecar_path(*options.smoke_screenshot,suffix.c_str()));});
          campaign.capture_colony_roster_smoke([&](const DrawList& draw){window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-colony-roster"));});
          campaign.capture_outpost_freight_smoke([&](const DrawList& draw){window.draw(draw,sidecar_path(*options.smoke_screenshot,L"-freight-review"));});
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
          window.draw(campaign.research_cancellation_smoke(window.drawable_width(),window.drawable_height(),false),
              sidecar_path(*options.smoke_screenshot,L"-cancelled"));
          window.draw(campaign.research_cancellation_smoke(window.drawable_width(),window.drawable_height(),true),
              sidecar_path(*options.smoke_screenshot,L"-restarted"));
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
        if(options.first_survey_mode)
          std::cout<<"first_survey="<<campaign.first_survey_smoke_status()<<'\n';
        std::ranges::sort(frame_ms);
        const auto total=std::accumulate(frame_ms.begin(),frame_ms.end(),0.);
        const auto p95=frame_ms[static_cast<std::size_t>(
            std::ceil(static_cast<double>(frame_ms.size())*.95))-1];
        const auto gpu_residency=window.scene3d_statistics();
        std::cout<<"asset_residency={\"image_textures\":"<<window.image_cache_entries()
                 <<",\"image_texture_bytes\":"<<window.image_cache_resident_bytes()
                 <<",\"scene_textures\":"<<gpu_residency.texture_cache_entries
                 <<",\"scene_texture_bytes\":"<<gpu_residency.texture_cache_bytes
                 <<",\"scene_mesh_bytes\":"<<gpu_residency.mesh_cache_bytes
                 <<",\"scene_render_target_bytes\":"<<gpu_residency.target_bytes<<"}\n";
        { // The client's bounded caches must be registered in the memory census.
          const auto census=stellar::engine::MemoryTracker::instance().snapshot();
          const auto tracked=[&](std::string_view name){
            return std::find_if(census.subsystems.begin(),census.subsystems.end(),
                [&](const auto& s){return s.name==name;})!=census.subsystems.end();};
          if(!tracked("planet-materials")||!tracked("territory-overlay")||!tracked("image-preparation"))
            throw std::runtime_error("Memory census is missing the client's bounded cache subsystems.");
        }
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
        if(replay.recorder)
          std::cout<<" replay={\"commands\":"<<replay.recorder->commands().size()
                   <<",\"checkpoints\":"<<replay.recorder->checkpoints().size()
                   <<",\"file\":"<<json_string(utf8_path(options.record_path))<<"}";
        if(replay.recording)
          std::cout<<" replay={\"verified_checkpoints\":"
                   <<replay.verified_checkpoints<<",\"of\":"
                   <<replay.recording->checkpoints().size()
                   <<",\"commands_consumed\":"<<replay.command_cursor
                   <<",\"diverged\":false}";
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
      if(waiting_for_capture)++capture_frame;
    }
    active_voice_playback=nullptr;
    } // A successful New Game replaces the campaign only after activation.
    return 0;
  }catch(const std::exception &error){diagnostics.fatal(error.what());std::cerr<<"Stellar Continuum native client failed: "<<error.what()<<'\n';return 1;}catch(...){diagnostics.fatal("Unknown fatal error");std::cerr<<"Stellar Continuum native client failed: unknown fatal error\n";return 1;}
}


