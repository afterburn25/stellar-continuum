#include "native_campaign_session.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <ranges>
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
  std::future<void> status;
};

struct NativeCampaignSession::Live final {
  Live(IntegratedAdaptiveCampaignRuntime runtime, StrategicClock clock,
       const std::filesystem::path &path, std::uint64_t revision,
       bool recovered, PlayerCampaignPreparedWriter writer,
       std::uint64_t cache_generation,bool developer)
      : frame(std::move(runtime), std::move(clock), developer?CampaignFramePolicy::Developer:CampaignFramePolicy::Player),
        saves({}, std::move(writer),developer?CampaignSaveKind::Developer:CampaignSaveKind::Player) {
    if(frame.runtime().world().campaign().developer_provenance.has_value()!=developer)
      throw std::invalid_argument("Campaign provenance does not match the requested session mode.");
    frame.clock().set_days_per_second(1./24.);
    if(developer)frame.set_developer_speed(frame.runtime().world().campaign().developer_provenance->simulation.speed);
    auto& campaign=frame.runtime().world().campaign();
    initialize_small_body_fields(campaign.seed,campaign.systems,campaign.bodies);
    initialize_stellar_orbits(campaign.seed,campaign.systems,campaign.bodies);
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
      dependencies_(std::move(dependencies)) {}

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
      dependencies.save_writer, 1,dependencies.developer_session);
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
  auto loaded = dependencies.developer_session?load_existing_developer_campaign(save_path,make_runtime,progress)
      :dependencies.loader(save_path, make_runtime, progress);
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
      recovered, dependencies.save_writer, 1,dependencies.developer_session);
  if (live->frame.clock().simulation_days() != day) {
    throw std::runtime_error("Loaded campaign clock does not match its saved day.");
  }
  if (dependencies.validate_candidate) {
    dependencies.validate_candidate(live->frame);
  }
  auto session = std::unique_ptr<NativeCampaignSession>(new NativeCampaignSession(
      std::move(live), std::move(research_root), std::move(save_path),
      std::move(game_version), std::move(dependencies)));
  session->notice_ = {recovered ? SessionNoticeKind::Recovered
                                : SessionNoticeKind::Loaded,
                      recovered
                          ? (loaded.origin == PlayerCampaignLoadOrigin::History
                                 ? "Recovered campaign from an older autosave"
                                 : "Recovered campaign from backup")
                          : "Loaded campaign",
                      1.,
                      recovered ? (loaded.origin ==
                                           PlayerCampaignLoadOrigin::History
                                       ? "SESSION_NOTICE_RECOVERED_HISTORY"
                                       : "SESSION_NOTICE_RECOVERED_BACKUP")
                                : "SESSION_NOTICE_LOADED"};
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

NewCampaignTransition NativeCampaignSession::new_campaign_transition() const {
  require_owner();
  return new_campaign_transition_;
}
bool NativeCampaignSession::new_campaign_pending() const {
  require_owner();
  return new_campaign_transition_ == NewCampaignTransition::Waiting ||
         new_campaign_transition_ == NewCampaignTransition::Saving ||
         new_campaign_transition_ == NewCampaignTransition::Ready;
}
bool NativeCampaignSession::request_new_campaign() {
  require_owner();
  if (new_campaign_pending()) return false;
  if (pending_load_ || exit_requested_ || exit_ready_ || !manual_capture_ready_) {
    publish_failure("New Game is unavailable until the current campaign operation finishes",
                    "SESSION_NOTICE_NEW_GAME_UNAVAILABLE");
    return false;
  }
  save_requested_ = false;
  new_campaign_transition_ = NewCampaignTransition::Waiting;
  notice_ = {SessionNoticeKind::Saving, "Saving this campaign before New Game", 0.,
             "SESSION_NOTICE_SAVING_NEW_GAME"};
  return true;
}
void NativeCampaignSession::cancel_new_campaign() {
  require_owner();
  if (!new_campaign_pending() && new_campaign_transition_ != NewCampaignTransition::Failed)
    return;
  // A writer in flight may finish, but cannot authorize any later request.
  new_campaign_transition_ = NewCampaignTransition::Inactive;
  notice_ = {SessionNoticeKind::None, "Current campaign retained", 0.,
             "SESSION_NOTICE_CAMPAIGN_RETAINED"};
}

PlayerCampaignRuntimeFactory NativeCampaignSession::runtime_factory() const {
  return [root = research_root_] {
    return load_adaptive_research_strategic_runtime(root);
  };
}

void NativeCampaignSession::publish_save_result(
    const PlayerCampaignSaveResult &result, std::string success_message,
    std::string_view success_key) {
  if (result.succeeded) {
    notice_ = {SessionNoticeKind::Saved, std::move(success_message), 1.,
               std::string(success_key)};
  } else {
    publish_failure("Save failed: " +
                    (result.error_message.empty() ? result.error_type
                                                  : result.error_message));
  }
}

void NativeCampaignSession::publish_failure(std::string message,
                                            std::string_view key) {
  if (new_campaign_pending()) new_campaign_transition_ = NewCampaignTransition::Failed;
  std::cerr << "Stellar Continuum native session: " << message << '\n';
  notice_ = {SessionNoticeKind::Failure, std::move(message), 0.,
             std::string(key)};
}

void NativeCampaignSession::publish_background_save_result(
    const PlayerCampaignSaveResult &result) {
  if (result.succeeded && new_campaign_transition_ == NewCampaignTransition::Saving)
    new_campaign_transition_ = NewCampaignTransition::Ready;
  const bool manual = std::exchange(manual_save_pending_, false);
  if (!result.succeeded) {
    // A queued action must not conceal a failed write with an immediate retry.
    // The player can retry explicitly after seeing the original diagnostic.
    save_requested_ = false;
    exit_requested_ = false;
  }
  publish_save_result(result,
                      manual ? "Saved campaign" : "Autosaved campaign",
                      manual ? "SESSION_NOTICE_SAVED"
                             : "SESSION_NOTICE_AUTOSAVED");
}

bool NativeCampaignSession::drain_live_save() {
  const auto completed = live_->saves.complete(
      live_->frame.clock().simulation_days(), save_path_,
      live_->saves.revision(), true);
  if (completed) {
    manual_save_pending_ = false;
  }
  if (completed && !completed->succeeded) {
    publish_save_result(*completed, "Saved campaign", "SESSION_NOTICE_SAVED");
    return false;
  }
  return true;
}

CampaignFrameResult NativeCampaignSession::advance(
    double real_delta_seconds, const std::string &saved_at_utc, bool developer_single_step) {
  require_owner();
  // Freeze even tactical reconciliation and preserve the completed capture boundary.
  if (new_campaign_pending()) return {};
  manual_capture_ready_ = false;
  auto result = developer_single_step ? live_->frame.step_developer() : live_->frame.advance(real_delta_seconds);
  // A returned tactical frame has finished its owned advance/reconciliation.
  // Player17 already captures its pending time, orders and encounter state.
  // Exceptions leave this false, and autosaves remain strategic-day driven.
  manual_capture_ready_ = result.ready_for_save_capture ||
                          result.route == CampaignFrameRoute::Tactical;
  if (pending_load_) {
    return result;
  }
  try {
    if (const auto completed = live_->saves.after_frame(
            live_->frame, result, game_version_, saved_at_utc)) {
      publish_background_save_result(*completed);
    } else if (live_->saves.pending() && !pending_load_) {
      notice_ = {SessionNoticeKind::Saving, "Saving campaign", 0.,
                 "SESSION_NOTICE_SAVING"};
    }
  } catch (const std::exception &error) {
    publish_failure(std::string("Autosave failed: ") + error.what());
  }
  return result;
}

void NativeCampaignSession::request_save() {
  require_owner();
  if (new_campaign_pending()) return;
  if (pending_load_) {
    publish_failure("Wait for the current load before saving",
                    "SESSION_NOTICE_WAIT_LOAD_SAVE");
    return;
  }
  save_requested_ = true;
  notice_ = {SessionNoticeKind::Saving, "Saving campaign", 0.,
             "SESSION_NOTICE_SAVING"};
}

void NativeCampaignSession::set_save_capture_observer(
    stellar::core::PlayerCampaignCaptureObserver observer) {
  require_owner();
  live_->saves.set_capture_observer(std::move(observer));
}

void NativeCampaignSession::begin_load() {
  auto progress = std::make_shared<LoadProgress>();
  auto loader = dependencies_.developer_session?NativeCampaignLoader{load_existing_developer_campaign}:dependencies_.loader;
  auto path = save_path_;
  auto make_runtime = runtime_factory();
  auto promise =
      std::make_shared<std::promise<LoadedPlayerCampaignV17>>();
  auto task = promise->get_future();
  auto status = load_jobs_.submit(
      "campaign-load", stellar::engine::JobPriority::Normal,
      stellar::engine::JobCancelToken{},
      [progress, loader = std::move(loader), path = std::move(path),
       make_runtime = std::move(make_runtime), promise]() mutable {
        try {
          promise->set_value(loader(path, make_runtime,
                      [progress](const PlayerCampaignRestorationProgress &next) {
                        progress->publish(next);
                      }));
        } catch (...) {
          try { promise->set_exception(std::current_exception()); }
          catch (...) {}
        }
      });
  pending_load_ = std::make_unique<PendingLoad>(
      PendingLoad{std::move(progress), std::move(task), std::move(status)});
}

void NativeCampaignSession::request_load() {
  require_owner();
  if (new_campaign_pending()) return;
  if (pending_load_) {
    publish_failure("A campaign load is already in progress",
                    "SESSION_NOTICE_LOAD_IN_PROGRESS");
    return;
  }
  try {
    if (!drain_live_save()) {
      return;
    }
    save_requested_ = false;
    exit_requested_ = false;
    manual_capture_ready_ = false;
    notice_ = {SessionNoticeKind::Loading, "Loading campaign", 0.,
               "SESSION_NOTICE_LOADING"};
    begin_load();
  } catch (const std::exception &error) {
    publish_failure(std::string("Load failed: ") + error.what());
  }
}

void NativeCampaignSession::request_exit() {
  require_owner();
  cancel_new_campaign();
  if (pending_load_) {
    publish_failure("Wait for the current load before exiting",
                    "SESSION_NOTICE_WAIT_LOAD_EXIT");
    return;
  }
  exit_requested_ = true;
  save_requested_ = false;
  notice_ = {SessionNoticeKind::Saving, "Saving before exit", 0.,
             "SESSION_NOTICE_SAVING_EXIT"};
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
      loaded.origin == PlayerCampaignLoadOrigin::Backup,
      dependencies_.save_writer, cache_generation,dependencies_.developer_session);
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
        const bool recovered =
            loaded.origin != PlayerCampaignLoadOrigin::Primary;
        auto candidate = activate(std::move(loaded), menu_open,
                                  live_->saves.revision() + 1,
                                  live_->cache.generation + 1);
        live_.swap(candidate);
        manual_capture_ready_ = false;
        notice_ = {recovered ? SessionNoticeKind::Recovered
                             : SessionNoticeKind::Loaded,
                   recovered
                       ? (loaded.origin == PlayerCampaignLoadOrigin::History
                              ? "Recovered campaign from an older autosave"
                              : "Recovered campaign from backup")
                       : "Loaded campaign",
                   1.,
                   recovered ? (loaded.origin ==
                                        PlayerCampaignLoadOrigin::History
                                    ? "SESSION_NOTICE_RECOVERED_HISTORY"
                                    : "SESSION_NOTICE_RECOVERED_BACKUP")
                             : "SESSION_NOTICE_LOADED"};
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

  // Poll even when simulation advancement is suspended (for example while the
  // window is minimized). Ordinary manual saves encode and write on the worker.
  if (const auto completed = live_->saves.complete(
          live_->frame.clock().simulation_days(), save_path_,
          live_->saves.revision())) {
    publish_background_save_result(*completed);
    if (!completed->succeeded) {
      return replaced;
    }
  }

  if (new_campaign_transition_ == NewCampaignTransition::Waiting &&
      !live_->saves.pending()) {
    try {
      const PlayerCampaignCaptureOptions options{
          live_->frame.clock().simulation_days(), game_version_, saved_at_utc};
      const auto failed = live_->saves.begin_manual(live_->frame.runtime(), options);
      if (failed) {
        publish_save_result(*failed, "Saved campaign", "SESSION_NOTICE_SAVED");
      } else {
        manual_save_pending_ = true;
        new_campaign_transition_ = NewCampaignTransition::Saving;
        notice_ = {SessionNoticeKind::Saving, "Saving this campaign before New Game", 0.,
                   "SESSION_NOTICE_SAVING_NEW_GAME"};
      }
    } catch (const std::exception &error) {
      publish_failure(std::string("Save before New Game failed: ") + error.what());
    }
  }

  if (save_requested_ || exit_requested_) {
    const bool exiting = exit_requested_;
    // Coalesce clicks while one writer owns the destination. Explicit load and
    // exit still drain synchronously before replacement or durable shutdown.
    if (!exiting && live_->saves.pending()) {
      return replaced;
    }
    save_requested_ = false;
    exit_requested_ = false;
    if (!manual_capture_ready_) {
      publish_failure("Save is unavailable until a campaign frame completes",
                      "SESSION_NOTICE_SAVE_UNAVAILABLE");
      return replaced;
    }
    try {
      if (!drain_live_save()) {
        return replaced;
      }
      const PlayerCampaignCaptureOptions options{
          live_->frame.clock().simulation_days(), game_version_, saved_at_utc};
      if (exiting) {
        const auto result = live_->saves.save_manual(live_->frame.runtime(), options);
        publish_save_result(result, "Saved campaign; exiting",
                            "SESSION_NOTICE_SAVED_EXITING");
        exit_ready_ = result.succeeded;
      } else {
        const auto failed = live_->saves.begin_manual(live_->frame.runtime(), options);
        if (failed) {
          publish_save_result(*failed, "Saved campaign",
                              "SESSION_NOTICE_SAVED");
        } else {
          manual_save_pending_ = true;
          notice_ = {SessionNoticeKind::Saving, "Saving campaign", 0.,
                     "SESSION_NOTICE_SAVING"};
        }
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
