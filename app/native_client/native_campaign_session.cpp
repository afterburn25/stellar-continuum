#include "native_campaign_session.hpp"

#include "native_campaign_calendar.hpp"

#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/diplomacy_state.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace stellar::native_map {
namespace {
using namespace stellar::core;

[[nodiscard]] std::string exception_message(std::exception_ptr error) {
  try {
    if (error) {
      std::rethrow_exception(error);
    }
  } catch (const std::exception &caught) {
    return caught.what();
  } catch (...) {
    return "Unknown campaign operation failure.";
  }
  return "Unknown campaign operation failure.";
}

void validate_dependencies(const NativeCampaignSessionDependencies &dependencies) {
  if (!dependencies.save_writer) {
    throw std::invalid_argument("A native campaign save writer is required.");
  }
  if (!dependencies.loader) {
    throw std::invalid_argument("A native campaign loader is required.");
  }
}

} // namespace

struct NativeCampaignSession::LoadProgress final {
  void publish(const PlayerCampaignRestorationProgress &next) {
    std::scoped_lock lock(mutex);
    fraction = std::max(fraction, std::clamp(next.fraction, 0., 1.));
    status = next.status;
  }

  [[nodiscard]] PlayerCampaignRestorationProgress snapshot() const {
    std::scoped_lock lock(mutex);
    return {fraction, status};
  }

  mutable std::mutex mutex;
  double fraction{};
  std::string status{"Waiting to load campaign"};
};

struct NativeCampaignSession::PendingLoad final {
  std::shared_ptr<LoadProgress> progress;
  std::future<LoadedPlayerCampaignV17> result;
};

struct NativeCampaignSession::Live final {
  Live(IntegratedAdaptiveCampaignRuntime runtime, StrategicClock clock,
       const std::filesystem::path &path, std::uint64_t revision,
       bool recovered, PlayerCampaignPreparedWriter writer,
       std::uint64_t cache_generation)
      // std::move only casts; the trailing provenance read still observes the
      // valid runtime inside the delegated constructor call.
      : Live(std::move(runtime), std::move(clock), path, revision, recovered,
             std::move(writer), cache_generation,
             runtime.world().campaign().developer_provenance.has_value()) {}

  Live(IntegratedAdaptiveCampaignRuntime runtime, StrategicClock clock,
       const std::filesystem::path &path, std::uint64_t revision,
       bool recovered, PlayerCampaignPreparedWriter writer,
       std::uint64_t cache_generation, bool developer)
      : frame(std::move(runtime), std::move(clock),
              developer ? CampaignFramePolicy::Developer
                        : CampaignFramePolicy::Player),
        saves(developer ? CampaignAutosavePolicy{720., 48.}
                        : CampaignAutosavePolicy{},
              std::move(writer), developer) {
    cache = NativeCampaignSession::build_cache(frame, cache_generation);
    saves.configure(path, revision, frame.clock().simulation_days(), recovered);
  }

  CampaignFrame frame;
  NativeCampaignCache cache;
  PlayerCampaignSaveController saves;
};

NativeCampaignCache NativeCampaignSession::build_cache(CampaignFrame &frame,
                                                       std::uint64_t generation) {
  NativeCampaignCache result;
  result.generation = generation;
  const auto &world = frame.runtime().world().campaign();
  if (world.systems.empty()) {
    throw std::runtime_error("Loaded campaign contains no star systems.");
  }
  if (!std::ranges::any_of(world.civilizations, [player = world.player_civilization_id](
                                                     const Civilization &value) {
        return value.id == player && value.is_player;
      })) {
    throw std::runtime_error("Loaded campaign has no valid player civilization.");
  }
  result.systems_by_id.reserve(world.systems.size());
  for (const auto &system : world.systems) {
    if (!result.systems_by_id.emplace(system.id, &system).second) {
      throw std::runtime_error("Loaded campaign contains duplicate system identifiers.");
    }
  }

  const auto lane_view = frame.runtime().world().lanes().build();
  result.lanes.reserve(lane_view.size());
  for (const auto &lane : lane_view) {
    const auto first = std::min(lane.first_system_id, lane.second_system_id);
    const auto second = std::max(lane.first_system_id, lane.second_system_id);
    if (!result.systems_by_id.contains(first) ||
        !result.systems_by_id.contains(second)) {
      throw std::runtime_error("Loaded campaign lane references an unknown system.");
    }
    result.lanes.push_back({first, second, lane.length_light_years});
  }
  std::ranges::sort(result.lanes, {}, [](const InterstellarLane &lane) {
    return std::pair{lane.first_system_id, lane.second_system_id};
  });
  result.lanes.erase(
      std::unique(result.lanes.begin(), result.lanes.end(),
                  [](const InterstellarLane &left, const InterstellarLane &right) {
                    return left.first_system_id == right.first_system_id &&
                           left.second_system_id == right.second_system_id;
                  }),
      result.lanes.end());
  return result;
}

NativeCampaignSession::NativeCampaignSession(
    std::unique_ptr<Live> live, std::filesystem::path research_root,
    std::filesystem::path save_path, std::string game_version,
    NativeCampaignSessionDependencies dependencies)
    : live_(std::move(live)), research_root_(std::move(research_root)),
      save_path_(std::move(save_path)), game_version_(std::move(game_version)),
      dependencies_(std::move(dependencies)) {
  seed_notification_history();
}

std::unique_ptr<NativeCampaignSession> NativeCampaignSession::create_fresh(
    IntegratedAdaptiveCampaignRuntime runtime, std::filesystem::path research_root,
    std::filesystem::path save_path, std::string game_version,
    NativeCampaignSessionDependencies dependencies) {
  if (save_path.empty()) {
    throw std::invalid_argument("A native campaign save path is required.");
  }
  validate_dependencies(dependencies);
  auto live = std::make_unique<Live>(
      std::move(runtime), StrategicClock{}, save_path, 1, false,
      dependencies.save_writer, 1);
  return std::unique_ptr<NativeCampaignSession>(new NativeCampaignSession(
      std::move(live), std::move(research_root), std::move(save_path),
      std::move(game_version), std::move(dependencies)));
}

std::unique_ptr<NativeCampaignSession> NativeCampaignSession::load_startup(
    std::filesystem::path research_root, std::filesystem::path save_path,
    std::string game_version,
    const std::function<void(const PlayerCampaignRestorationProgress &)> &progress,
    NativeCampaignSessionDependencies dependencies) {
  if (save_path.empty()) {
    throw std::invalid_argument("A native campaign save path is required.");
  }
  validate_dependencies(dependencies);
  const auto make_runtime = [root = research_root] {
    return load_adaptive_research_strategic_runtime(root);
  };
  auto loaded = dependencies.loader(save_path, make_runtime, progress);
  return create_loaded(std::move(loaded), std::move(research_root),
                       std::move(save_path), std::move(game_version),
                       std::move(dependencies));
}

std::unique_ptr<NativeCampaignSession> NativeCampaignSession::create_loaded(
    LoadedPlayerCampaignV17 loaded, std::filesystem::path research_root,
    std::filesystem::path save_path, std::string game_version,
    NativeCampaignSessionDependencies dependencies) {
  if (save_path.empty()) {
    throw std::invalid_argument("A native campaign save path is required.");
  }
  validate_dependencies(dependencies);
  const bool recovered = loaded.origin != PlayerCampaignLoadOrigin::Primary;
  const auto day = loaded.campaign.simulation_days();
  if (!std::isfinite(day) || day < 0.) {
    throw std::runtime_error("Loaded campaign has an invalid simulation day.");
  }
  StrategicClock clock;
  clock.restore(day);
  clock.set_speed(StrategicSpeed::Normal);
  clock.set_speed(StrategicSpeed::Paused);
  auto live = std::make_unique<Live>(
      std::move(loaded.campaign).activate(), std::move(clock), save_path, 1,
      recovered, dependencies.save_writer, 1);
  if (live->frame.clock().simulation_days() != day) {
    throw std::runtime_error("Loaded campaign clock does not match its saved day.");
  }
  if (dependencies.validate_candidate) {
    dependencies.validate_candidate(live->frame);
  }
  auto session = std::unique_ptr<NativeCampaignSession>(new NativeCampaignSession(
      std::move(live), std::move(research_root), std::move(save_path),
      std::move(game_version), std::move(dependencies)));
  const auto recovered_message =
      loaded.origin == PlayerCampaignLoadOrigin::History
          ? "Recovered campaign from an older autosave"
          : "Recovered campaign from backup";
  session->notice_ = {recovered ? SessionNoticeKind::Recovered
                                : SessionNoticeKind::Loaded,
                      recovered ? recovered_message : "Loaded campaign",
                      1.};
  return session;
}

NativeCampaignSession::~NativeCampaignSession() {
  if (pending_load_) {
    try {
      pending_load_->result.wait();
      (void)pending_load_->result.get();
    } catch (...) {
    }
    pending_load_.reset();
  }
  if (live_) {
    try {
      (void)live_->saves.complete(live_->frame.clock().simulation_days(),
                                  save_path_, live_->saves.revision(), true);
    } catch (...) {
    }
  }
}

void NativeCampaignSession::require_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error(
        "Campaign sessions must be controlled by their simulation owner thread.");
  }
}

CampaignFrame &NativeCampaignSession::frame() {
  require_owner();
  return live_->frame;
}
const NativeCampaignCache &NativeCampaignSession::cache() const {
  require_owner();
  return live_->cache;
}
const std::filesystem::path &NativeCampaignSession::save_path() const {
  require_owner();
  return save_path_;
}
const SessionNotice &NativeCampaignSession::notice() const {
  require_owner();
  return notice_;
}
bool NativeCampaignSession::load_pending() const {
  require_owner();
  return pending_load_ != nullptr;
}
bool NativeCampaignSession::exit_ready() const {
  require_owner();
  return exit_ready_;
}
bool NativeCampaignSession::developer_mode() const {
  require_owner();
  return live_->frame.runtime().world().campaign()
      .developer_provenance.has_value();
}
bool NativeCampaignSession::developer_tools_used() const {
  require_owner();
  const auto &provenance =
      live_->frame.runtime().world().campaign().developer_provenance;
  return provenance && provenance->tools_used;
}

DeveloperCommandResult NativeCampaignSession::run_developer_command(
    const std::string_view command_id, const std::string &saved_at_utc) {
  require_owner();
  auto &frame = live_->frame;
  auto &campaign = frame.runtime().world().campaign();
  auto result = execute_developer_command(
      campaign, command_id, [&frame](const double days) {
        // Source Main.CoreIntegration.AdvanceDeveloperDays: ordinary
        // simulation steps of at most .25 days through the live runtime.
        for (auto remaining = days; remaining > 0.;) {
          const auto step = std::min(.25, remaining);
          frame.clock().restore(frame.clock().simulation_days() + step);
          (void)frame.runtime().advance(step,
                                        frame.clock().simulation_days());
          remaining -= step;
        }
      });
  if (!result.accepted) return result;
  if (command_id == "unlock_technology" || command_id == "unlock_research") {
    auto &research = frame.runtime().research();
    auto &state = detail::AdaptiveResearchCampaignStateAccess::get_civilization(
        research, campaign.player_civilization_id);
    for (const auto capability :
         {"orbital_industry", "spacecraft_construction",
          "experimental_interstellar_transit"})
      (void)research.runtime().authority().add_capability(state, capability);
  }
  // The command boundary writes one checkpoint after the entire action.
  if (!checkpoint_now(saved_at_utc))
    result.message += " Saving failed; retry Save before switching.";
  notice_ = {SessionNoticeKind::Status, result.message, 1.};
  return result;
}

bool NativeCampaignSession::checkpoint_now(const std::string &saved_at_utc) {
  require_owner();
  try {
    if (!drain_live_save()) return false;
    const auto result = live_->saves.save_manual(
        live_->frame.runtime(),
        {live_->frame.clock().simulation_days(), game_version_,
         saved_at_utc});
    publish_save_result(result, "Saved campaign");
    return result.succeeded;
  } catch (const std::exception &error) {
    publish_failure(std::string("Save failed: ") + error.what());
    return false;
  }
}

void NativeCampaignSession::set_save_capture_observer(
    stellar::core::PlayerCampaignCaptureObserver observer) {
  require_owner();
  if (live_) live_->saves.set_capture_observer(std::move(observer));
}

PlayerCampaignRuntimeFactory NativeCampaignSession::runtime_factory() const {
  return [root = research_root_] {
    return load_adaptive_research_strategic_runtime(root);
  };
}

void NativeCampaignSession::publish_save_result(
    const PlayerCampaignSaveResult &result, std::string success_message) {
  if (result.succeeded) {
    notice_ = {SessionNoticeKind::Saved, std::move(success_message), 1.};
  } else {
    publish_failure("Save failed: " +
                    (result.error_message.empty() ? result.error_type
                                                  : result.error_message));
  }
}

void NativeCampaignSession::publish_failure(std::string message) {
  std::cerr << "Stellar Continuum native session: " << message << '\n';
  notice_ = {SessionNoticeKind::Failure, std::move(message), 0.};
}

bool NativeCampaignSession::drain_live_save() {
  const auto completed = live_->saves.complete(
      live_->frame.clock().simulation_days(), save_path_,
      live_->saves.revision(), true);
  if (completed && !completed->succeeded) {
    publish_save_result(*completed, "Saved campaign");
    return false;
  }
  return true;
}

void NativeCampaignSession::publish_notification(
    std::string category, std::string message,
    std::optional<int> diplomatic_contact_id) {
  require_owner();
  notifications_.publish(
      std::move(category),
      native_campaign::format_campaign_date(
          live_->frame.clock().simulation_days()),
      std::move(message), diplomatic_contact_id);
}

void NativeCampaignSession::seed_notification_history() {
  seen_diplomatic_events_.clear();
  const auto view = live_->frame.runtime().diplomacy().build_view_for(
      live_->frame.runtime().world().campaign().player_civilization_id);
  for (const auto &event : view.recent_events)
    seen_diplomatic_events_.insert(event.event_id);
}

// Player-visible event publication, mirroring Main.CoreIntegration /
// Main.cs handlers. Everything below is filtered to the player observer
// before entering the feed.
void NativeCampaignSession::harvest_notifications(
    const CampaignFrameResult &result, int player_id,
    double previous_funding) {
  const auto &campaign = live_->frame.runtime().world().campaign();
  const auto date = native_campaign::format_campaign_date(
      live_->frame.clock().simulation_days());
  for (const auto &step : result.strategic_results) {
    for (const auto &event : step.core.combat_events) {
      const bool involved = event.actor_civilization_id == player_id ||
                            event.target_civilization_id == player_id;
      if (!involved) continue;
      if (event.type == CombatEventType::EngagementStarted ||
          event.type == CombatEventType::FleetRetreatInitiated ||
          event.type == CombatEventType::FleetEscaped ||
          event.type == CombatEventType::FleetDestroyed ||
          event.type == CombatEventType::EngagementEnded)
        notifications_.publish("Combat", date, event.message);
    }
    for (const auto &event : step.research_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Research", date, event.message);
    for (const auto &event : step.core.research_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Research", date, event.message);
    for (const auto &event : step.core.construction_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Construction", date, event.message);
    for (const auto &event : step.core.shipbuilding_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Ships", date, event.message);
    for (const auto &event : step.core.exploration_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Exploration", date, event.message);
    for (const auto &event : step.core.colonization_events)
      if (event.civilization_id == player_id)
        notifications_.publish("Colony", date, event.message);
  }
  // PublishOperatingFundingTransition: crossing the funded threshold either
  // direction publishes the reference economy message.
  const auto economy = std::ranges::find_if(
      campaign.economies,
      [player_id](const auto &entry) {
        return entry.civilization_id == player_id;
      });
  if (economy != campaign.economies.end()) {
    const auto current = economy->last_base_operations_funding_fraction;
    if ((previous_funding >= 0.999999) != (current >= 0.999999)) {
      std::ostringstream message;
      if (current >= 0.999999)
        message << "Operating funding restored. Industrial production and "
                   "fleet missions have resumed at full capacity.";
      else
        message << "Operating shortfall: only "
                << static_cast<int>(std::lround(current * 100.))
                << "% of current services are funded. Production and fleet "
                   "missions are reduced until revenue recovers.";
      notifications_.publish("Economy", date, message.str());
    }
  }
  // Diplomatic bulletins: new observer-visible events of the reference kinds,
  // with the counterpart resolved only when it is an identified contact.
  const auto view =
      live_->frame.runtime().diplomacy().build_view_for(player_id);
  for (const auto &event : view.recent_events) {
    if (event.kind != DiplomaticEventKind::contact_established &&
        event.kind != DiplomaticEventKind::communication_available &&
        event.kind != DiplomaticEventKind::proposal_sent &&
        event.kind != DiplomaticEventKind::proposal_accepted &&
        event.kind != DiplomaticEventKind::proposal_rejected &&
        event.kind != DiplomaticEventKind::agreement_activated &&
        event.kind != DiplomaticEventKind::agreement_terminated &&
        event.kind != DiplomaticEventKind::war_declared)
      continue;
    if (!seen_diplomatic_events_.insert(event.event_id).second) continue;
    std::optional<int> counterpart;
    if (event.primary_civilization_id == player_id)
      counterpart = event.secondary_civilization_id;
    else if (event.secondary_civilization_id == player_id)
      counterpart = event.primary_civilization_id;
    const auto known = counterpart &&
                       std::ranges::any_of(view.contacts, [&](const auto &c) {
                         return c.target_civilization_id == *counterpart;
                       });
    notifications_.publish(
        "Diplomacy",
        native_campaign::format_campaign_date(
            static_cast<double>(event.tick) / 1000.),
        event.summary, known ? counterpart : std::nullopt);
  }
  std::erase_if(seen_diplomatic_events_, [&](std::int64_t id) {
    return std::ranges::none_of(
        view.recent_events,
        [id](const auto &event) { return event.event_id == id; });
  });
}

CampaignFrameResult NativeCampaignSession::advance(
    double real_delta_seconds, const std::string &saved_at_utc) {
  require_owner();
  manual_capture_ready_ = false;
  const auto &campaign = live_->frame.runtime().world().campaign();
  const auto player_id = campaign.player_civilization_id;
  const auto economy = std::ranges::find_if(
      campaign.economies,
      [player_id](const auto &entry) {
        return entry.civilization_id == player_id;
      });
  const double previous_funding =
      economy != campaign.economies.end()
          ? economy->last_base_operations_funding_fraction
          : 1.;
  auto result = live_->frame.advance(real_delta_seconds);
  harvest_notifications(result, player_id, previous_funding);
  manual_capture_ready_ = result.ready_for_save_capture ||
                          result.route == CampaignFrameRoute::Tactical;
  if (pending_load_) {
    return result;
  }
  try {
    if (const auto completed = live_->saves.after_frame(
            live_->frame, result, game_version_, saved_at_utc)) {
      publish_save_result(*completed, "Autosaved campaign");
    } else if (live_->saves.pending() && !pending_load_) {
      notice_ = {SessionNoticeKind::Saving, "Saving campaign", 0.};
    }
  } catch (const std::exception &error) {
    publish_failure(std::string("Autosave failed: ") + error.what());
  }
  return result;
}

void NativeCampaignSession::request_save() {
  require_owner();
  if (pending_load_) {
    publish_failure("Wait for the current load before saving");
    return;
  }
  save_requested_ = true;
  notice_ = {SessionNoticeKind::Saving, "Saving campaign", 0.};
}

void NativeCampaignSession::begin_load() {
  auto progress = std::make_shared<LoadProgress>();
  auto loader = dependencies_.loader;
  auto path = save_path_;
  auto make_runtime = runtime_factory();
  auto task = std::async(
      std::launch::async,
      [progress, loader = std::move(loader), path = std::move(path),
       make_runtime = std::move(make_runtime)]() mutable {
        return loader(path, make_runtime,
                      [progress](const PlayerCampaignRestorationProgress &next) {
                        progress->publish(next);
                      });
      });
  pending_load_ = std::make_unique<PendingLoad>(
      PendingLoad{std::move(progress), std::move(task)});
}

void NativeCampaignSession::request_load() {
  require_owner();
  if (pending_load_) {
    publish_failure("A campaign load is already in progress");
    return;
  }
  try {
    if (!drain_live_save()) {
      return;
    }
    save_requested_ = false;
    exit_requested_ = false;
    manual_capture_ready_ = false;
    notice_ = {SessionNoticeKind::Loading, "Loading campaign", 0.};
    begin_load();
  } catch (const std::exception &error) {
    publish_failure(std::string("Load failed: ") + error.what());
  }
}

void NativeCampaignSession::request_exit() {
  require_owner();
  if (pending_load_) {
    publish_failure("Wait for the current load before exiting");
    return;
  }
  exit_requested_ = true;
  save_requested_ = false;
  notice_ = {SessionNoticeKind::Saving, "Saving before exit", 0.};
}

std::unique_ptr<NativeCampaignSession::Live> NativeCampaignSession::activate(
    LoadedPlayerCampaignV17 loaded, bool menu_open, std::uint64_t revision,
    std::uint64_t cache_generation) {
  const auto day = loaded.campaign.simulation_days();
  if (!std::isfinite(day) || day < 0.) {
    throw std::runtime_error("Loaded campaign has an invalid simulation day.");
  }
  StrategicClock clock;
  clock.restore(day);
  clock.set_speed(StrategicSpeed::Normal);
  clock.set_speed(StrategicSpeed::Paused);
  auto candidate = std::make_unique<Live>(
      std::move(loaded.campaign).activate(), std::move(clock), save_path_, revision,
      loaded.origin != PlayerCampaignLoadOrigin::Primary,
      dependencies_.save_writer, cache_generation);
  if (candidate->frame.clock().simulation_days() != day) {
    throw std::runtime_error("Loaded campaign clock does not match its saved day.");
  }
  if (menu_open) {
    candidate->frame.set_menu_open(true);
    candidate->frame.pause_tactical_for_menu();
  }
  if (dependencies_.validate_candidate) {
    dependencies_.validate_candidate(candidate->frame);
  }
  return candidate;
}

bool NativeCampaignSession::service(const std::string &saved_at_utc,
                                    bool menu_open) {
  require_owner();
  bool replaced = false;
  if (pending_load_) {
    if (pending_load_->result.wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready) {
      try {
        auto loaded = pending_load_->result.get();
        const bool recovered = loaded.origin != PlayerCampaignLoadOrigin::Primary;
        auto candidate = activate(std::move(loaded), menu_open,
                                  live_->saves.revision() + 1,
                                  live_->cache.generation + 1);
        live_.swap(candidate);
        manual_capture_ready_ = false;
        notifications_.clear();
        seed_notification_history();
        const auto recovered_message =
            loaded.origin == PlayerCampaignLoadOrigin::History
                ? "Recovered campaign from an older autosave"
                : "Recovered campaign from backup";
        notice_ = {recovered ? SessionNoticeKind::Recovered
                             : SessionNoticeKind::Loaded,
                   recovered ? recovered_message : "Loaded campaign",
                   1.};
        replaced = true;
      } catch (...) {
        publish_failure("Load failed: " +
                        exception_message(std::current_exception()));
      }
      pending_load_.reset();
    } else {
      const auto progress = pending_load_->progress->snapshot();
      notice_ = {SessionNoticeKind::Loading, progress.status, progress.fraction};
    }
  }

  if (save_requested_ || exit_requested_) {
    const bool exiting = exit_requested_;
    save_requested_ = false;
    exit_requested_ = false;
    if (!manual_capture_ready_) {
      publish_failure("Save is unavailable until a strategic frame completes");
      return replaced;
    }
    try {
      if (!drain_live_save()) {
        return replaced;
      }
      const auto result = live_->saves.save_manual(
          live_->frame.runtime(),
          {live_->frame.clock().simulation_days(), game_version_, saved_at_utc});
      publish_save_result(result,
                          exiting ? "Saved campaign; exiting" : "Saved campaign");
      if (exiting && result.succeeded) {
        exit_ready_ = true;
      }
    } catch (const std::exception &error) {
      publish_failure(std::string("Save failed: ") + error.what());
    }
  }
  return replaced;
}

std::filesystem::path default_native_campaign_save_path() {
#ifdef _WIN32
  struct CoTaskMemory final {
    void operator()(wchar_t *value) const noexcept { CoTaskMemFree(value); }
  };
  PWSTR known_path = nullptr;
  const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                           &known_path);
  if (FAILED(status) || known_path == nullptr) {
    throw std::runtime_error(
        "Windows LocalAppData is unavailable for native campaign saves.");
  }
  const std::unique_ptr<wchar_t, CoTaskMemory> owner(known_path);
  const std::filesystem::path root(owner.get());
  return root / "Stellar Continuum" / "NativePreview" /
         "campaign.player17.json";
#else
  throw std::runtime_error(
      "The native preview save location is only available on Windows.");
#endif
}

} // namespace stellar::native_map
