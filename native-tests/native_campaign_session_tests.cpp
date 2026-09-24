#include "native_campaign_session.hpp"

#include <stellar/core/player_campaign_json.hpp>
#include <stellar/engine/save_history.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;
using namespace stellar::native_map;

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read test fixture.");
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

void write(const fs::path &path, const std::string &value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output) throw std::runtime_error("Cannot write test campaign.");
}

[[nodiscard]] std::string fixture_campaign(const fs::path &path) {
  const auto fixture = Json::parse(read(path));
  for (const auto &row : fixture.at("Rows")) {
    if (row.at("Name") == "valid-current17") {
      return row.at("InputJson").get<std::string>();
    }
  }
  throw std::runtime_error("Missing valid Player17 fixture row.");
}

[[nodiscard]] bool wait_for_load(NativeCampaignSession &session,
                                 bool menu_open = true) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (session.load_pending() && std::chrono::steady_clock::now() < deadline) {
    if (session.service("2044-05-06T07:08:10Z", menu_open)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

void wait_for_save(NativeCampaignSession &session) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (session.notice().kind == SessionNoticeKind::Saving &&
         std::chrono::steady_clock::now() < deadline) {
    // Service must consume completion without relying on simulation updates.
    (void)session.service("2044-05-06T07:08:09Z", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(session.notice().kind != SessionNoticeKind::Saving,
          "Background manual save did not finish.");
}

void save_load_and_transactional_failure(const fs::path &research_root,
                                         const std::string &source,
                                         const fs::path &directory) {
  const auto save_path = directory / "campaign.json";
  auto reject_candidate = std::make_shared<std::atomic_bool>(false);
  NativeCampaignSessionDependencies dependencies;
  dependencies.validate_candidate = [reject_candidate](CampaignFrame &) {
    if (reject_candidate->load()) {
      throw std::runtime_error("injected activation rejection");
    }
  };
  write(save_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, save_path, "0.1.7-alpha", {}, dependencies);
  require(session->frame().clock().speed() == StrategicSpeed::Paused &&
              session->frame().clock().resume_speed() == StrategicSpeed::Normal,
          "Startup load did not pause with a safe ordinary resume speed.");
  (void)session->advance(0., "2044-05-06T07:08:08Z");
  session->request_save();
  (void)session->service("2044-05-06T07:08:09Z", false);
  wait_for_save(*session);
  require(session->notice().kind == SessionNoticeKind::Saved &&
              fs::is_regular_file(save_path),
          "Manual session save did not succeed.");

  const auto initial_generation = session->cache().generation;
  const auto initial_system_id = session->cache().systems_by_id.begin()->first;
  const auto initial_system_name =
      session->cache().systems_by_id.at(initial_system_id)->name;
  const auto saved_seed = session->frame().runtime().world().campaign().seed;
  const auto saved_day = session->frame().clock().simulation_days();
  const auto saved_bytes = read(save_path);
  session->frame().runtime().world().campaign().seed += 77;
  session->request_load();
  require(wait_for_load(*session, false), "Valid campaign did not activate.");
  require(session->cache().generation == initial_generation + 1 &&
              session->cache().systems_by_id.at(initial_system_id)->name ==
                  initial_system_name,
          "Successful activation did not publish a new valid cache generation.");
  require(session->frame().runtime().world().campaign().seed == saved_seed &&
              session->frame().clock().simulation_days() == saved_day &&
              session->frame().clock().speed() == StrategicSpeed::Paused &&
              session->frame().clock().resume_speed() == StrategicSpeed::Normal,
          "Closed-menu load did not restore the saved world paused.");
  const auto recaptured = encode_player_campaign_v17_json(
      capture_player_campaign_v17(
          session->frame().runtime(),
          {saved_day, "0.1.7-alpha", "2044-05-06T07:08:09Z"}));
  require(recaptured == saved_bytes,
          "Loaded campaign full-state recapture differs from the saved state.");

  (void)session->advance(0., "2044-05-06T07:08:10Z");
  session->request_save();
  (void)session->service("2044-05-06T07:08:11Z", true);
  wait_for_save(*session);
  require(session->notice().kind == SessionNoticeKind::Saved,
          "Second manual save did not succeed.");
  const auto stable_generation = session->cache().generation;
  const auto stable_system = session->cache().systems_by_id.begin()->second;
  const auto stable_day = session->frame().clock().simulation_days();
  reject_candidate->store(true);
  session->request_load();
  require(!wait_for_load(*session) &&
              session->notice().kind == SessionNoticeKind::Failure,
          "Rejected activation was reported as a success.");
  require(session->cache().generation == stable_generation &&
              session->cache().systems_by_id.begin()->second == stable_system &&
              session->frame().clock().simulation_days() == stable_day,
          "Rejected activation changed the live campaign.");

  reject_candidate->store(false);
  { std::ofstream primary(save_path, std::ios::binary | std::ios::trunc);
    primary << "broken primary"; }
  { std::ofstream backup(fs::path(save_path).concat(".bak"),
                         std::ios::binary | std::ios::trunc);
    backup << "broken backup"; }
  // Two prior saves rotated a valid generation into .bak.2 — recovery must
  // reach the history slot rather than fail on the corrupted pair.
  session->request_load();
  require(wait_for_load(*session, false) &&
              session->notice().kind == SessionNoticeKind::Recovered,
          "History autosave was not recovered.");
  const auto history_generation = session->cache().generation;
  const auto history_system = session->cache().systems_by_id.begin()->second;
  const auto history_day = session->frame().clock().simulation_days();

  // Corrupt the entire chain — no valid save anywhere must fail cleanly.
  { std::ofstream primary(save_path, std::ios::binary | std::ios::trunc);
    primary << "broken primary"; }
  { std::ofstream backup(fs::path(save_path).concat(".bak"),
                         std::ios::binary | std::ios::trunc);
    backup << "broken backup"; }
  for (std::size_t slot = 2;
       slot <= stellar::engine::k_default_save_history_depth; ++slot) {
    const auto slot_path =
        stellar::engine::history_slot_path(save_path, slot);
    if (fs::is_regular_file(slot_path)) {
      std::ofstream broken(slot_path, std::ios::binary | std::ios::trunc);
      broken << "broken history";
    }
  }
  session->request_load();
  require(!wait_for_load(*session) &&
              session->notice().kind == SessionNoticeKind::Failure,
          "Invalid save chain was reported as loaded.");
  require(session->cache().generation == history_generation &&
              session->cache().systems_by_id.begin()->second == history_system &&
              session->frame().clock().simulation_days() == history_day,
          "Decode failure changed the live campaign.");
}

void new_campaign_save_fence(const fs::path &research_root,
                             const std::string &source, const fs::path &directory) {
  const auto path = directory / "new-game-fence.json";
  write(path, source);
  std::atomic_int entered{}, allowed{};
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer = [&](const fs::path &destination,
      const PreparedPlayerCampaignSave &prepared, bool preserve) {
    const auto call = ++entered;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (allowed.load() < call) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Test save fence was not released");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    write_prepared_player_campaign(destination, prepared, preserve);
  };
  auto session = NativeCampaignSession::load_startup(research_root, path, "0.1.7-alpha", {}, dependencies);
  struct Release final { std::atomic_int &value; ~Release(){value.store(100);} } release{allowed};
  const auto service_until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      (void)session->service("2044-05-06T07:08:12Z", true);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(predicate(), "New Game save fence did not reach its expected state");
  };
  (void)session->advance(0., "2044-05-06T07:08:11Z");
  session->request_save();
  service_until([&]{return entered.load()==1;});
  session->frame().runtime().world().campaign().seed += 33;
  const auto expected_seed=session->frame().runtime().world().campaign().seed;
  const auto day=session->frame().clock().simulation_days();
  const auto generation=session->cache().generation;
  const auto* world=&session->frame().runtime().world().campaign();
  require(session->request_new_campaign(), "New Game rejected a completed frame");
  require(!session->request_new_campaign(), "New Game admitted a duplicate transition");
  session->frame().clock().set_speed(StrategicSpeed::Normal);
  (void)session->advance(120., "2044-05-06T07:08:12Z");
  session->request_load();session->request_save();
  require(!session->load_pending() && session->frame().clock().simulation_days()==day &&
      session->new_campaign_transition()==NewCampaignTransition::Waiting,
      "Pending New Game allowed simulation, load or an older save to authorize it");
  allowed=1;
  service_until([&]{return entered.load()==2;});
  require(session->new_campaign_transition()==NewCampaignTransition::Saving &&
      Json::parse(read(path)).at("Galaxy").at("Seed")==expected_seed-33,
      "An older save was mistaken for this transition's save");
  session->cancel_new_campaign();
  require(!session->new_campaign_pending(), "Cancel left the transition locked");
  require(session->request_new_campaign(), "A cancelled transition could not be retried");
  allowed=2;
  service_until([&]{return entered.load()==3;});
  require(session->new_campaign_transition()==NewCampaignTransition::Saving,
      "A cancelled writer authorized a later New Game request");
  allowed=3;
  service_until([&]{return session->new_campaign_transition()==NewCampaignTransition::Ready;});
  require(Json::parse(read(path)).at("Galaxy").at("Seed")==expected_seed && entered.load()==3,
      "Ready New Game did not durably capture the requested campaign");
  (void)session->advance(120., "2044-05-06T07:08:13Z");
  session->cancel_new_campaign();
  require(&session->frame().runtime().world().campaign()==world &&
      session->cache().generation==generation && session->frame().clock().simulation_days()==day,
      "Cancelling ready setup replaced or advanced the original campaign");
  // A previous Saved notice must still require a fresh write.
  require(session->request_new_campaign(), "Ready cancellation could not be retried");
  require(session->new_campaign_transition()==NewCampaignTransition::Waiting,
      "An existing Saved notice bypassed the transition capture");
  session->cancel_new_campaign();
}

void new_campaign_filesystem_failure(const fs::path &research_root,
                                     const std::string &source, const fs::path &directory) {
  const auto path=directory/"new-game-disk-failure.json";
  write(path, source);
  auto session=NativeCampaignSession::load_startup(research_root,path,"0.1.7-alpha");
  (void)session->advance(0.,"2044-05-06T07:08:11Z");
  const auto* world=&session->frame().runtime().world().campaign();
  const auto preserved=directory/"new-game-preserved.json";
  fs::rename(path,preserved);
  require(fs::create_directory(path),"Could not create a real blocked save destination");
  require(session->request_new_campaign(),"Could not request filesystem-failure case");
  (void)session->service("2044-05-06T07:08:12Z",true);
  wait_for_save(*session);
  require(session->new_campaign_transition()==NewCampaignTransition::Failed &&
      session->notice().kind==SessionNoticeKind::Failure && !session->notice().message.empty() &&
      &session->frame().runtime().world().campaign()==world && read(preserved)==source && !session->exit_ready(),
      "Filesystem failure discarded the live campaign, previous save or diagnostic");
  for(int i=0;i<5;++i)(void)session->service("2044-05-06T07:08:13Z",true);
  require(session->new_campaign_transition()==NewCampaignTransition::Failed,
      "Failed New Game automatically retried");
  require(fs::remove(path),"Cannot remove the empty test obstruction");
  fs::rename(preserved,path);
  require(session->request_new_campaign(),"Explicit recovery retry was rejected");
  (void)session->service("2044-05-06T07:08:14Z",true);wait_for_save(*session);
  require(session->new_campaign_transition()==NewCampaignTransition::Ready,
      "Correcting the save destination did not recover New Game");
  session->request_exit();
  (void)session->service("2044-05-06T07:08:15Z",true);
  require(session->exit_ready() && !session->new_campaign_pending(),
      "Explicit exit after a prepared New Game failed to exit durably");
}

void failed_exit_stays_open(const fs::path &research_root,
                            const std::string &source,
                            const fs::path &directory) {
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer = [](const fs::path &,
                                const PreparedPlayerCampaignSave &, bool) {
    throw std::runtime_error("injected durable write failure");
  };
  const auto exit_path = directory / "exit.json";
  write(exit_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, exit_path, "0.1.7-alpha", {}, std::move(dependencies));
  (void)session->advance(0., "2044-05-06T07:08:11Z");
  const auto generation = session->cache().generation;
  session->request_exit();
  (void)session->service("2044-05-06T07:08:12Z", true);
  require(!session->exit_ready() &&
              session->notice().kind == SessionNoticeKind::Failure &&
              session->notice().message.find("injected durable write failure") !=
                  std::string::npos,
          "Failed Exit save closed the session or lost its diagnostic.");
  require(session->cache().generation == generation,
          "Failed Exit save changed the live campaign.");
}

void pending_save_precedes_load(const fs::path &research_root,
                                const std::string &source,
                                const fs::path &directory, bool manual) {
  struct Barriers final {
    std::mutex mutex;
    std::condition_variable changed;
    bool writer_entered{};
    bool release_writer{};
    bool loader_entered{};
    bool release_loader{};
  } barriers;
  std::atomic_int writer_calls{};
  std::atomic_int loader_calls{};
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer =
      [&](const fs::path &, const PreparedPlayerCampaignSave &, bool) {
        ++writer_calls;
        std::unique_lock lock(barriers.mutex);
        barriers.writer_entered = true;
        barriers.changed.notify_all();
        (void)barriers.changed.wait_for(lock, std::chrono::seconds(10), [&] {
          return barriers.release_writer;
        });
      };
  dependencies.loader =
      [&](const fs::path &path, const PlayerCampaignRuntimeFactory &factory,
          const std::function<void(const PlayerCampaignRestorationProgress &)> &
              progress) {
        const auto call = ++loader_calls;
        if (call > 1) {
          std::unique_lock lock(barriers.mutex);
          barriers.loader_entered = true;
          barriers.changed.notify_all();
          (void)barriers.changed.wait_for(lock, std::chrono::seconds(10), [&] {
            return barriers.release_loader;
          });
        }
        return load_existing_player_campaign_v17(path, factory, progress);
      };

  const auto save_path = directory / (manual ? "manual-before-load.json" : "pending-before-load.json");
  write(save_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, save_path, "0.1.7-alpha", {}, dependencies);
  const auto original_day = session->frame().clock().simulation_days();
  if (!manual) session->frame().clock().restore(original_day + 100.);
  (void)session->advance(0., "2044-05-06T07:08:13Z");
  if (manual) {
    session->request_save();
    (void)session->service("2044-05-06T07:08:13Z", false);
  }

  bool writer_was_entered{};
  bool loader_started_before_release{};
  std::thread release_writer([&] {
    std::unique_lock lock(barriers.mutex);
    writer_was_entered = barriers.changed.wait_for(
        lock, std::chrono::seconds(10), [&] { return barriers.writer_entered; });
    loader_started_before_release = loader_calls.load() != 1;
    barriers.release_writer = true;
    lock.unlock();
    barriers.changed.notify_all();
  });
  session->request_load();
  release_writer.join();

  bool loader_was_entered{};
  {
    std::unique_lock lock(barriers.mutex);
    loader_was_entered = barriers.changed.wait_for(
        lock, std::chrono::seconds(10), [&] { return barriers.loader_entered; });
  }
  const auto calls_before_pending_advance = writer_calls.load();
  session->frame().clock().restore(original_day + 200.);
  (void)session->advance(0., "2044-05-06T07:08:14Z");
  const bool pending_advance_started_no_save =
      writer_calls.load() == calls_before_pending_advance;
  {
    std::scoped_lock lock(barriers.mutex);
    barriers.release_loader = true;
  }
  barriers.changed.notify_all();
  const bool activated = wait_for_load(*session, false);

  require(writer_was_entered && !loader_started_before_release,
          "Loader started before the pending scheduled writer was drained.");
  require(loader_was_entered && activated,
          "Loader did not start after the pending writer drained.");
  require(pending_advance_started_no_save && writer_calls.load() == 1,
          "A new autosave started while campaign loading was pending.");
  require(session->frame().clock().speed() == StrategicSpeed::Paused &&
              session->frame().clock().resume_speed() == StrategicSpeed::Normal,
          "Closed-menu asynchronous load did not remain safely paused.");
}

void failed_pending_save_prevents_load(const fs::path &research_root,
                                       const std::string &source,
                                       const fs::path &directory, bool manual) {
  std::atomic_int loader_calls{};
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer = [](const fs::path &,
                                const PreparedPlayerCampaignSave &, bool) {
    throw std::runtime_error("injected pending write failure");
  };
  dependencies.loader =
      [&](const fs::path &path, const PlayerCampaignRuntimeFactory &factory,
          const std::function<void(const PlayerCampaignRestorationProgress &)> &
              progress) {
        ++loader_calls;
        return load_existing_player_campaign_v17(path, factory, progress);
      };
  const auto save_path = directory / (manual ? "failed-manual-drain.json" : "failed-drain.json");
  write(save_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, save_path, "0.1.7-alpha", {}, dependencies);
  const auto generation = session->cache().generation;
  const auto seed = session->frame().runtime().world().campaign().seed;
  if (!manual) session->frame().clock().restore(
      session->frame().clock().simulation_days() + 100.);
  (void)session->advance(0., "2044-05-06T07:08:15Z");
  if (manual) {
    session->request_save();
    (void)session->service("2044-05-06T07:08:15Z", false);
  }
  session->request_load();
  require(loader_calls.load() == 1 && !session->load_pending() &&
              session->notice().kind == SessionNoticeKind::Failure &&
              session->notice().message.find("injected pending write failure") !=
                  std::string::npos,
          "Failed pending save did not prevent the loader from starting.");
  require(session->cache().generation == generation &&
              session->frame().runtime().world().campaign().seed == seed,
          "Failed pre-load drain changed the live campaign.");
}

void background_manual_save(const fs::path &research_root,
                            const std::string &source,
                            const fs::path &directory, bool fail_first) {
  struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};
    bool released{};
    bool timed_out{};
  } gate;
  const auto owner = std::this_thread::get_id();
  std::atomic_int calls{};
  std::atomic_bool writer_on_owner{};
  std::string first_capture;
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer = [&](const fs::path &path,
                                 const PreparedPlayerCampaignSave &prepared,
                                 bool preserve) {
    writer_on_owner = writer_on_owner.load() || std::this_thread::get_id() == owner;
    if (++calls == 1) {
      std::unique_lock lock(gate.mutex);
      gate.entered = true;
      gate.changed.notify_all();
      gate.timed_out = !gate.changed.wait_for(lock, std::chrono::seconds(10), [&] {
        return gate.released;
      });
      lock.unlock();
      if (fail_first) throw std::runtime_error("injected manual write failure");
      first_capture = encode_player_campaign_v17_json(prepared.payload());
    }
    write_prepared_player_campaign(path, prepared, preserve);
  };
  const auto path = directory / (fail_first ? "async-failure.json" : "async-manual.json");
  write(path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, path, "0.1.7-alpha", {}, dependencies);
  // Release before session destruction even if a subsequent assertion throws.
  struct Release {
    Gate &gate;
    ~Release() {
      { std::scoped_lock lock(gate.mutex); gate.released = true; }
      gate.changed.notify_all();
    }
  } release{gate};
  (void)session->advance(0., "2044-05-06T07:08:09Z");
  const auto generation = session->cache().generation;
  const auto expected_first = encode_player_campaign_v17_json(capture_player_campaign_v17(
      session->frame().runtime(),
      {session->frame().clock().simulation_days(), "0.1.7-alpha", "2044-05-06T07:08:09Z"}));
  session->request_save();
  (void)session->service("2044-05-06T07:08:09Z", false);
  bool entered{};
  {
    std::unique_lock lock(gate.mutex);
    entered = gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.entered; });
  }
  const bool pending_notice = session->notice().kind == SessionNoticeKind::Saving;
  // Change the live world while the worker owns its immutable first capture.
  session->frame().runtime().world().campaign().seed += 17;
  session->request_save();
  session->request_save();
  for (int i = 0; i < 3; ++i) {
    (void)session->service("2044-05-06T07:08:09Z", false);
    (void)session->advance(0., "2044-05-06T07:08:09Z");
  }
  const bool one_writer = calls.load() == 1;
  {
    std::scoped_lock lock(gate.mutex);
    gate.released = true;
  }
  gate.changed.notify_all();
  wait_for_save(*session);
  require(entered && !gate.timed_out && pending_notice && one_writer && !writer_on_owner,
          "Manual saving blocked the owner, reported success early, or admitted competing writes.");
  require(session->cache().generation == generation && !session->exit_ready(),
          "Manual saving replaced the live campaign or closed it.");
  if (fail_first) {
    require(calls.load() == 1 && session->notice().kind == SessionNoticeKind::Failure &&
                session->notice().message.find("injected manual write failure") != std::string::npos &&
                read(path) == source,
            "Queued save hid a failed write, retried automatically, or damaged the primary.");
    session->request_save();
    (void)session->service("2044-05-06T07:08:09Z", false);
    wait_for_save(*session);
  } else {
    require(first_capture == expected_first && read(fs::path(path).concat(".bak")) == first_capture,
            "Worker borrowed live state or queued write bypassed ordered backup rotation.");
  }
  const auto expected_latest = encode_player_campaign_v17_json(capture_player_campaign_v17(
      session->frame().runtime(),
      {session->frame().clock().simulation_days(), "0.1.7-alpha", "2044-05-06T07:08:09Z"}));
  require(calls.load() == 2 && session->notice().kind == SessionNoticeKind::Saved &&
              session->notice().message == "Saved campaign" && read(path) == expected_latest,
          "Coalesced/retried manual save did not persist the exact latest capture.");
  require(session->notice().message_key == "SESSION_NOTICE_SAVED",
          "Saved notice did not carry its localization key.");
}

void failed_frame_clears_save_readiness(const fs::path &research_root,
                                        const std::string &source,
                                        const fs::path &directory) {
  std::atomic_int writer_calls{};
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer =
      [&](const fs::path &, const PreparedPlayerCampaignSave &, bool) {
        ++writer_calls;
      };
  const auto save_path = directory / "failed-frame.json";
  write(save_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, save_path, "0.1.7-alpha", {}, dependencies);
  (void)session->advance(0., "2044-05-06T07:08:16Z");
  session->frame().runtime().world().campaign().economies.clear();
  session->frame().clock().resume();
  bool failed{};
  try {
    (void)session->advance(1., "2044-05-06T07:08:17Z");
  } catch (const std::exception &) {
    failed = true;
  }
  session->request_exit();
  (void)session->service("2044-05-06T07:08:18Z", false);
  require(failed, "Injected invalid strategic frame unexpectedly completed.");
  require(writer_calls.load() == 0 && !session->exit_ready() &&
              session->notice().kind == SessionNoticeKind::Failure &&
              session->notice().message.find("until a campaign frame completes") !=
                  std::string::npos &&
              session->notice().message_key == "SESSION_NOTICE_SAVE_UNAVAILABLE",
          "Failed frame retained stale manual save readiness.");
}

void wrong_thread_rejected_before_mutation(const fs::path &research_root,
                                           const std::string &source,
                                           const fs::path &directory) {
  std::atomic_int loader_calls{};
  NativeCampaignSessionDependencies dependencies;
  dependencies.loader =
      [&](const fs::path &path, const PlayerCampaignRuntimeFactory &factory,
          const std::function<void(const PlayerCampaignRestorationProgress &)> &
              progress) {
        ++loader_calls;
        return load_existing_player_campaign_v17(path, factory, progress);
      };
  const auto save_path = directory / "owner-thread.json";
  write(save_path, source);
  auto session = NativeCampaignSession::load_startup(
      research_root, save_path, "0.1.7-alpha", {}, dependencies);
  const auto before_day = session->frame().clock().simulation_days();
  std::atomic_int rejected{};
  std::thread intruder([&] {
    try {
      (void)session->advance(1., "2044-05-06T07:08:19Z");
    } catch (const std::logic_error &) {
      ++rejected;
    }
    try {
      session->request_load();
    } catch (const std::logic_error &) {
      ++rejected;
    }
  });
  intruder.join();
  require(rejected.load() == 2 && loader_calls.load() == 1 &&
              session->frame().clock().simulation_days() == before_day &&
              !session->load_pending(),
          "Wrong-thread session calls mutated state before rejection.");
}

void tactical_save_reload_and_continuation(const fs::path &research_root,
                                         const std::string &source,
                                         const fs::path &directory) {
  auto fixture=Json::parse(source);
  // Arrange an encounter through the production begin command, not a second
  // hand-authored battle representation. The fixture supplies two military
  // fleets and existing civilians at one system, with an identified war.
  for(auto& fleet:fixture["Galaxy"]["Fleets"]){
    const auto id=fleet["Id"].get<int>();
    if(id!=0&&id!=6&&id!=7)continue;
    fleet["CurrentSystemId"]=0;fleet["X"]=0;fleet["Y"]=0;
    if(id==7)continue;
    fleet["Role"]=static_cast<int>(FleetRole::Military);
    fleet["DesignId"]="patrol_corvette";
    fleet["Combat"]["ProfileId"]="patrol_corvette_mk1";
    fleet["Combat"]["Shields"]=35;fleet["Combat"]["Armor"]=45;
    fleet["Combat"]["Hull"]=95;
  }
  const auto tick=static_cast<std::int64_t>(fixture["SimulationDays"].get<double>()*1000.);
  fixture["Diplomacy"]["Contacts"].push_back({
      {"ObserverCivilizationId",0},{"ContactId","native-tactical-contact"},
      {"TargetCivilizationId",3},{"FirstObservedTick",tick-8},{"LastObservedTick",tick},
      {"LastObservedSystemId",0},{"Awareness",2},{"Condition",1},
      {"CommunicationAvailable",false},{"Confidence",.9}});
  fixture["Diplomacy"]["Relationships"].push_back({
      {"CivilizationAId",0},{"CivilizationBId",3},{"PoliticalState",3},
      {"Trust",0.},{"Hostility",.9},{"Fear",.2},{"Respect",0.},
      {"Cooperation",0.},{"Grievances",Json::array()}});
  const auto path=directory/"tactical.json";write(path,fixture.dump());
  auto session=NativeCampaignSession::load_startup(research_root,path,"0.1.7-alpha");
  auto& frame=session->frame();
  require(frame.tactical_snapshot().formations.empty()&&
      !frame.issue_tactical_order({1,MassiveCombatOrderType::Hold}).accepted,
      "Tactical adapter fabricated a battle without an encounter.");
  const auto begun=frame.begin_tactical(0);
  require(begun.accepted,"Production tactical begin rejected the arranged opposing fleets.");
  auto& encounter=*frame.runtime().world().campaign().active_combat_encounter;
  const auto own=std::ranges::find_if(encounter.battle.formations,[](const auto& f){return f.civilization_id==0;});
  const auto foreign=std::ranges::find_if(encounter.battle.formations,[](const auto& f){return f.civilization_id!=0;});
  require(own!=encounter.battle.formations.end()&&foreign!=encounter.battle.formations.end(),
      "Canonical begin did not create both participants.");
  require(!frame.issue_tactical_order({foreign->id,MassiveCombatOrderType::Hold}).accepted,
      "Native tactical adapter accepted a foreign formation order.");
  require(frame.issue_tactical_order({own->id,MassiveCombatOrderType::Hold}).accepted,
      "Native tactical adapter rejected its owned formation order.");
  frame.set_tactical_speed(0.);frame.set_tactical_resume_speed(2.);
  const auto starting_tick=encounter.battle.tick;
  const auto step=session->advance(.5,"2044-05-06T07:08:20Z");
  require(step.route==CampaignFrameRoute::Tactical&&encounter.battle.tick==starting_tick&&
      frame.tactical_clock().speed_multiplier()==0.,"Changing resume speed advanced a paused battle.");
  const auto capture=[](NativeCampaignSession& value){
    return encode_player_campaign_v17_json(capture_player_campaign_v17(value.frame().runtime(),
      {value.frame().clock().simulation_days(),"0.1.7-alpha","2044-05-06T07:08:20Z"}));
  };
  const auto paused=capture(*session);
  session->request_save();(void)session->service("2044-05-06T07:08:20Z",false);wait_for_save(*session);
  require(session->notice().kind==SessionNoticeKind::Saved&&read(path)==paused,
      "Paused tactical save did not retain the complete canonical state.");
  auto loaded=NativeCampaignSession::load_startup(research_root,path,"0.1.7-alpha");
  require(capture(*loaded)==paused&&loaded->frame().tactical_clock().speed_multiplier()==0.,
      "Tactical startup reload changed its save or resumed the encounter.");
  const auto* original_frame=&session->frame();
  const auto* original_encounter=&*session->frame().runtime().world().campaign().active_combat_encounter;
  const auto* original_cache=&session->cache();
  const auto original_generation=session->cache().generation;
  require(session->request_new_campaign()&&
      session->new_campaign_transition()==NewCampaignTransition::Waiting,
      "An older tactical Saved notice authorized New Game without a fresh capture.");
  (void)session->advance(120.,"2044-05-06T07:08:20Z");
  require(capture(*session)==paused&&
      session->frame().runtime().world().campaign().active_combat_encounter->battle.tick==starting_tick,
      "Pending New Game advanced or changed the paused tactical encounter.");
  const auto transition_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  while(session->new_campaign_transition()!=NewCampaignTransition::Ready&&
        std::chrono::steady_clock::now()<transition_deadline){
    (void)session->service("2044-05-06T07:08:20Z",true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(session->new_campaign_transition()==NewCampaignTransition::Ready&&
      capture(*session)==paused&&read(path)==paused,
      "New Game did not durably preserve the exact paused tactical state.");
  session->cancel_new_campaign();
  require(&session->frame()==original_frame&&
      &*session->frame().runtime().world().campaign().active_combat_encounter==original_encounter&&
      &session->cache()==original_cache&&session->cache().generation==original_generation&&
      capture(*session)==paused,
      "Cancelling tactical New Game replaced or changed its live frame, encounter or cache.");
  frame.set_tactical_speed(2.);loaded->frame().set_tactical_speed(2.);
  for(const auto delta:{.13,.017,.25}){
    (void)session->advance(delta,"2044-05-06T07:08:20Z");
    (void)loaded->advance(delta,"2044-05-06T07:08:20Z");
    require(capture(*session)==capture(*loaded),"Tactical reload diverged during matched continuation.");
  }
  const auto running=capture(*session);
  require(running!=paused,"Matched tactical continuation did not advance.");
  session->request_save();(void)session->service("2044-05-06T07:08:20Z",false);wait_for_save(*session);
  require(session->notice().kind==SessionNoticeKind::Saved&&read(path)==running,
      "Running tactical capture lost its orders, pending time or combat events.");
  // Failed activation keeps this live encounter and its clock untouched.
  write(path,"broken tactical save");write(fs::path(path.string()+".bak"),"broken backup");
  for(std::size_t slot=2;slot<=stellar::engine::k_default_save_history_depth;++slot){
    const auto slot_path=stellar::engine::history_slot_path(path,slot);
    if(fs::is_regular_file(slot_path))write(slot_path,"broken history");
  }
  session->request_load();require(!wait_for_load(*session,false),"Broken tactical save replaced live state.");
  require(session->notice().kind==SessionNoticeKind::Failure&&capture(*session)==running,
      "Failed tactical load damaged the running campaign.");
  // Reconciliation remains canonical and idempotent after surrender.
  for(auto& f:encounter.battle.formations)if(f.civilization_id!=0)f.surrendered=true;
  frame.set_tactical_speed(0.);
  const auto ended=session->advance(0.,"2044-05-06T07:08:20Z");
  require(ended.tactical_completed&&encounter.reconciled&&frame.tactical_snapshot().formations.empty(),
      "Completed encounter did not reconcile and relinquish tactical presentation.");
  const auto completed=capture(*session);
  session->request_save();(void)session->service("2044-05-06T07:08:20Z",true);wait_for_save(*session);
  require(read(path)==completed,"Completion-frame tactical save was not durable.");
}

void developer_save_isolation(const fs::path &research_root,const std::string &source,const fs::path &directory){
  auto restored=restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),source);
  const auto restored_day=restored.simulation_days();
  auto runtime=std::move(restored).activate();
  runtime.world().campaign().developer_provenance=CampaignDeveloperProvenance{true,true,false};
  NativeCampaignSessionDependencies dev;dev.developer_session=true;
  const auto path=directory/"isolated.dev17.json";
  // This fixture is an existing campaign, not a new day-zero world. Preserve
  // its clock while changing the test envelope; never rewind its AI schedule.
  write_prepared_campaign(path,PreparedPlayerCampaignSave::capture_developer(
      runtime,{restored_day,"test","2050-03-21T00:00:00Z"}),false);
  auto session=NativeCampaignSession::load_startup(research_root,path,"test",{},dev);
  (void)session->advance(0.,"2050-03-21T00:00:00Z");
  session->request_save();(void)session->service("2050-03-21T00:00:00Z",false);wait_for_save(*session);
  require(session->notice().kind==SessionNoticeKind::Saved,"Developer session background save failed.");
  const auto first=read(path);
  require(Json::parse(first).at("DeveloperSession")==true,"Developer writer omitted provenance.");
  bool rejected=false;
  try{(void)NativeCampaignSession::load_startup(research_root,path,"test");}catch(const std::exception&){rejected=true;}
  require(rejected,"Player session loaded a developer campaign.");
  session->request_load();require(wait_for_load(*session),"Developer session could not reload its own save.");
  require(session->frame().runtime().world().campaign().developer_provenance->normal_research_completed,
      "Developer provenance was lost on reload.");
  (void)session->advance(0.,"2050-03-21T00:00:01Z");
  session->request_save();(void)session->service("2050-03-21T00:00:01Z",false);wait_for_save(*session);
  require(read(fs::path(path.string()+".bak"))==first,"Developer atomic save did not preserve its previous version.");
  write(path,"broken");
  auto recovered=NativeCampaignSession::load_startup(research_root,path,"test",{},dev);
  require(recovered->notice().kind==SessionNoticeKind::Recovered,"Developer save backup recovery failed.");
  require(recovered->frame().runtime().world().campaign().developer_provenance.has_value(),"Recovery cleared developer marker.");
  const auto prepared=PreparedPlayerCampaignSave::capture_developer(recovered->frame().runtime(),{recovered->frame().clock().simulation_days(),"test","now"});
  rejected=false;try{write_prepared_player_campaign(directory/"forbidden-player.json",prepared,false);}catch(const std::exception&){rejected=true;}
  require(rejected&&!fs::exists(directory/"forbidden-player.json"),"Player writer silently converted a developer capture.");
  rejected=false;try{write_prepared_campaign(directory/"forbidden-player.json",prepared,false);}catch(const std::exception&){rejected=true;}
  require(rejected,"Generic writer allowed a developer capture at a player path.");
  rejected=false;try{PlayerCampaignSaveController invalid({},write_prepared_campaign,CampaignSaveKind::Developer);invalid.configure(directory/"player.json",1,0,false);}catch(const std::exception&){rejected=true;}
  require(rejected,"Developer save controller accepted a player destination.");
}
} // namespace

int main(int argc, char **argv) try {
  if (argc != 4) {
    throw std::invalid_argument(
        "Usage: native_campaign_session_tests <Player17 fixture> <research root> <scratch parent>");
  }
  const auto research_root = fs::absolute(argv[2]);
  const auto source = fixture_campaign(fs::absolute(argv[1]));
  const auto default_path = default_native_campaign_save_path();
  require(default_path.is_absolute() &&
              default_path.parent_path().filename() == "NativePreview" &&
              default_path.filename() == "campaign.player17.json",
          "Default save path is outside the NativePreview namespace.");
  const auto parent = fs::absolute(argv[3]);
  fs::create_directories(parent);
  const auto directory = parent /
      ("native-session-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  require(fs::create_directory(directory), "Cannot claim session test directory.");
  developer_save_isolation(research_root,source,directory);
  save_load_and_transactional_failure(research_root, source, directory);
  new_campaign_save_fence(research_root, source, directory);
  new_campaign_filesystem_failure(research_root, source, directory);
  failed_exit_stays_open(research_root, source, directory);
  pending_save_precedes_load(research_root, source, directory, false);
  pending_save_precedes_load(research_root, source, directory, true);
  failed_pending_save_prevents_load(research_root, source, directory, false);
  failed_pending_save_prevents_load(research_root, source, directory, true);
  background_manual_save(research_root, source, directory, false);
  background_manual_save(research_root, source, directory, true);
  failed_frame_clears_save_readiness(research_root, source, directory);
  wrong_thread_rejected_before_mutation(research_root, source, directory);
  tactical_save_reload_and_continuation(research_root, source, directory);
  {std::error_code cleanup;fs::remove_all(directory,cleanup);}// retain run dir only on failure
  std::cout << "Native session save/load, ordered pending IO, transactional activation, owner-thread enforcement, cache generations and failed Exit passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native session test failed: " << error.what() << '\n';
  return 1;
}
