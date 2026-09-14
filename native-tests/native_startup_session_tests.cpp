#include "native_startup_session.hpp"

#include <stellar/core/player_campaign_json.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_setup;
using namespace stellar::native_startup;
namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

NativePreparedNewCampaign prepared() {
  NativeNewCampaignSetupController setup;
  auto value = setup.prepare(
      {"140250", 250, "terran_baseline", "2045-01-02T03:04:05Z"});
  require(value.accepted && value.prepared, "setup fixture failed");
  return std::move(*value.prepared);
}

NativeStartupView wait_terminal(NativeStartupSessionController &controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{90};
  for (;;) {
    controller.service();
    const auto view = controller.poll();
    if (view.phase == NativeStartupPhase::Ready ||
        view.phase == NativeStartupPhase::Failed ||
        view.phase == NativeStartupPhase::Cancelled)
      return view;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("startup operation timed out");
    std::this_thread::yield();
  }
}

void fresh_and_load(const fs::path &research, const fs::path &catalog,
                    const fs::path &scratch) {
  const auto anchor = scratch / "campaign.player17.json";
  NativeStartupSessionController startup;
  const auto request = startup.start_new(prepared(), research, catalog, anchor, "test");
  require(request.accepted && wait_terminal(startup).ready,
          "fresh startup did not become ready");
  const auto ready = startup.poll();
  startup.service();
  require(startup.poll().phase == NativeStartupPhase::Ready &&
              startup.poll().revision == ready.revision,
          "repeated service invalidated an untaken ready session");
  auto session = startup.take_ready_session(request.request_id);
  require(session && !startup.take_ready_session(request.request_id) &&
              session->save_path() == anchor &&
              session->frame().runtime().world().campaign().systems.size() == 250,
          "fresh session was not consumable exactly once");
  const auto consumed = startup.poll();
  startup.service();
  require(startup.poll().phase == NativeStartupPhase::Consumed &&
              startup.poll().revision == consumed.revision,
          "repeated service changed a consumed startup result");
  const auto lanes = session->frame().runtime().world().lanes().build();
  require(!lanes.empty(), "fresh owner session did not build lanes");
  auto original = PreparedPlayerCampaignSave::capture(
      session->frame().runtime(), {0., "test", "2045-01-02T03:04:06Z"});
  write_prepared_player_campaign(anchor, original, false);

  NativeStartupSessionController loader;
  const auto load = loader.start_load(anchor, research, "test");
  require(load.accepted && wait_terminal(loader).ready,
          "selected save did not load asynchronously");
  auto restored = loader.take_ready_session(load.request_id);
  require(restored && restored->frame().clock().speed() == StrategicSpeed::Paused &&
              restored->save_path() == anchor,
          "loaded session did not preserve path and paused startup clock");
  const auto replay = PreparedPlayerCampaignSave::capture(
      restored->frame().runtime(), {0., "test", "2045-01-02T03:04:06Z"});
  const auto expected_json = encode_player_campaign_v17_json(original.payload());
  const auto actual_json = encode_player_campaign_v17_json(replay.payload());
  const auto expected_state = nlohmann::json::parse(expected_json);
  const auto actual_state = nlohmann::json::parse(actual_json);
  require(expected_state == actual_state,
          "selected save reload changed the Player17 value tree");
}

void slots_and_unique_paths(const fs::path &scratch) {
  const auto anchor = scratch / "slot.player17.json";
  std::ofstream(anchor) << "anchor";
  std::ofstream(scratch / "slot-native-1.player17.json") << "one";
  std::ofstream(scratch / "slot-native-other.player17.json") << "unrelated";
  std::ofstream(scratch / "slot-native-2.player17.json.bak") << "backup";
  std::ofstream(scratch / "unrelated.player17.json") << "unrelated";
  require(unique_fresh_native_save_path(anchor).filename() ==
              "slot-native-2.player17.json",
          "fresh path could overwrite an existing slot");
  const auto slots = list_native_startup_save_slots(anchor, 16);
  require(slots.error.empty() && slots.slots.size() == 2,
          "slot listing included unrelated or backup files");
  const auto relative_slots = list_native_startup_save_slots(
      fs::relative(anchor, fs::current_path()), 16);
  require(relative_slots.error.empty() && relative_slots.slots.size() == 2,
          "relative configured path did not include its explicit anchor");
  const auto absent = list_native_startup_save_slots(
      scratch / "not-created" / "campaign.player17.json", 16);
  require(absent.error.empty() && absent.slots.empty(),
          "first-start missing save directory was reported as an error");
  std::ofstream(scratch / "slot-native-2.player17.json") << "two";
  require(unique_fresh_native_save_path(anchor).filename() ==
              "slot-native-3.player17.json",
          "fresh path was not re-evaluated after a new file appeared");
}

struct Barrier { std::mutex mutex; std::condition_variable changed; bool entered{}, release{}; };

void cancellation_and_repeat(const fs::path &research, const fs::path &catalog,
                             const fs::path &scratch) {
  Barrier barrier;
  NativeStartupSessionController startup({},
      [&](const auto &request, const auto &r, const auto &c, const auto &phase) {
        phase(NativeNewCampaignGenerationPhase::LoadingCatalog);
        std::unique_lock lock(barrier.mutex);
        barrier.entered = true;
        barrier.changed.notify_all();
        barrier.changed.wait(lock, [&] { return barrier.release; });
        lock.unlock();
        return generate_detached_new_campaign(request, r, c, phase);
      });
  const auto first = startup.start_new(prepared(), research, catalog,
                                       scratch / "cancel.player17.json", "test");
  {
    std::unique_lock lock(barrier.mutex);
    require(barrier.changed.wait_for(lock, std::chrono::seconds{10},
                                     [&] { return barrier.entered; }),
            "blocking generation did not start");
  }
  require(!startup.start_new(prepared(), research, catalog,
                             scratch / "other.player17.json", "test").accepted &&
              startup.cancel(first.request_id),
          "repeat/cancel admission was incorrect");
  {
    std::lock_guard lock(barrier.mutex);
    barrier.release = true;
    barrier.changed.notify_all();
  }
  require(wait_terminal(startup).phase == NativeStartupPhase::Cancelled &&
              !startup.take_ready_session(first.request_id) &&
              !fs::exists(scratch / "cancel.player17.json"),
          "cancelled generation activated or wrote a save");
}

void explicit_load_failure(const fs::path &research, const fs::path &catalog,
                           const fs::path &scratch) {
  NativeStartupSessionController startup;
  const auto missing = scratch / "missing.player17.json";
  const auto request = startup.start_load(missing, research, "test");
  const auto failed = wait_terminal(startup);
  require(request.accepted && failed.phase == NativeStartupPhase::Failed &&
              failed.status.find("missing") != std::string::npos &&
              !startup.take_ready_session(request.request_id) &&
              !fs::exists(missing),
          "explicit missing save fell back or lacked diagnostics");

  NativeCampaignSessionDependencies dependencies;
  dependencies.loader = [](const fs::path &,
                           const PlayerCampaignRuntimeFactory &,
                           const std::function<void(
                               const PlayerCampaignRestorationProgress &)> &progress)
      -> LoadedPlayerCampaignV17 {
    progress({std::numeric_limits<double>::quiet_NaN(), "invalid progress"});
    throw 7;
  };
  NativeStartupSessionController nonstandard(std::move(dependencies));
  const auto unusual = nonstandard.start_load(missing, research, "test");
  const auto unusual_failed = wait_terminal(nonstandard);
  require(unusual.accepted && unusual_failed.phase == NativeStartupPhase::Failed &&
              unusual_failed.determinate_progress == 0. &&
              unusual_failed.status.find("unknown error") != std::string::npos,
          "nonstandard load failure escaped or NaN progress was published");

  NativeStartupSessionController invalid({},
      [](const auto &request, const auto &r, const auto &c, const auto &phase) {
        auto detached = generate_detached_new_campaign(request, r, c, phase);
        detached.world.civilizations.front().species_id = "invalid_startup_species";
        return detached;
      });
  const auto invalid_request = invalid.start_new(
      prepared(), research, catalog, scratch / "invalid.player17.json", "test");
  const auto activation_failed = wait_terminal(invalid);
  const auto failed_revision = activation_failed.revision;
  invalid.service();
  require(invalid_request.accepted &&
              activation_failed.phase == NativeStartupPhase::Failed &&
              activation_failed.status.find("invalid_startup_species") !=
                  std::string::npos &&
              invalid.poll().phase == NativeStartupPhase::Failed &&
              invalid.poll().revision == failed_revision,
          "repeated service changed a failed new-campaign activation");
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 4, "Usage: tests <research> <catalog> <scratch>");
  const auto scratch = fs::absolute(argv[3]);
  fs::create_directories(scratch);
  fresh_and_load(fs::absolute(argv[1]), fs::absolute(argv[2]), scratch);
  slots_and_unique_paths(scratch);
  cancellation_and_repeat(fs::absolute(argv[1]), fs::absolute(argv[2]), scratch);
  explicit_load_failure(fs::absolute(argv[1]), fs::absolute(argv[2]), scratch);
  std::cout << "native startup session: 4/4 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native startup session failed: " << error.what() << '\n';
  return 1;
}
