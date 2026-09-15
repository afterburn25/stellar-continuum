#include "native_new_campaign_generation.hpp"

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/player_campaign_persistence.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace stellar::core;
using namespace stellar::native_setup;
namespace fs = std::filesystem;

namespace {
void require(const bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

NativePreparedNewCampaign prepared(const int count = 250) {
  NativeNewCampaignSetupController setup;
  auto result = setup.prepare(
      {"139500", count, "terran_baseline", "2044-05-06T07:08:09Z"});
  require(result.accepted && result.prepared, "test setup preparation failed");
  return std::move(*result.prepared);
}

NativeNewCampaignGenerationView wait_terminal(
    NativeNewCampaignGenerationController &controller,
    const std::chrono::seconds timeout = std::chrono::seconds{90}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    auto view = controller.poll();
    if (view.phase == NativeNewCampaignGenerationPhase::Ready ||
        view.phase == NativeNewCampaignGenerationPhase::Failed ||
        view.phase == NativeNewCampaignGenerationPhase::Cancelled)
      return view;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("asynchronous generation did not finish");
    std::this_thread::yield();
  }
}

void actual_handoff_and_main_owner_test(const fs::path &research_root,
                                        const fs::path &catalog_path) {
  const auto owner = std::this_thread::get_id();
  std::thread::id generator_thread;
  NativeNewCampaignGenerationController controller(
      [&](const NativePreparedNewCampaign &request, const fs::path &research,
          const fs::path &catalog, const NativeNewCampaignPhaseSink &phase) {
        generator_thread = std::this_thread::get_id();
        return generate_detached_new_campaign(request, research, catalog, phase);
      });
  const auto request = controller.start(prepared(250), research_root, catalog_path);
  require(request.accepted && request.request_id > 0,
          "actual detached generation did not start");
  const auto ready = wait_terminal(controller);
  require(generator_thread != owner && ready.ready &&
              ready.phase == NativeNewCampaignGenerationPhase::Ready &&
              ready.status == "Campaign generation is ready.",
          "actual detached generation did not become ready");

  auto activated = controller.activate_ready(request.request_id);
  require(activated && controller.poll().phase ==
                           NativeNewCampaignGenerationPhase::Consumed &&
              activated->world().campaign().systems.size() == 250,
          "detached result was not activated on the consuming thread");
  auto &lanes = activated->world().lanes();
  const auto built = lanes.build();
  require(!built.empty(), "main-thread activation did not construct real lanes");
  const auto route = lanes.find_shortest_route(
      built.front().first_system_id, built.front().second_system_id, 1000.);
  require(route.size() == 2,
          "main-thread lane query failed after detached handoff");

  StrategicClock clock;
  clock.set_speed(StrategicSpeed::Normal);
  CampaignFrame frame(std::move(*activated), std::move(clock),
                      CampaignFramePolicy::Player);
  const auto step = frame.advance(.25);
  require(step.ready_for_save_capture &&
              frame.clock().simulation_days() > 0.,
          "main-thread frame did not advance an activated campaign");
  const auto payload = capture_player_campaign_v17(
      frame.runtime(), {frame.clock().simulation_days(), "test",
                        "2044-05-06T07:08:10Z"});
  require(payload.format_version == 17 && payload.galaxy.systems &&
              payload.galaxy.systems->size() == 250,
          "main-thread save capture failed after detached handoff");
}

void take_and_repeat_500_test(const fs::path &research_root,
                              const fs::path &catalog_path) {
  NativeNewCampaignGenerationController controller;
  auto first = controller.start(prepared(250), research_root, catalog_path);
  require(first.accepted && wait_terminal(controller).ready,
          "repeat fixture did not become ready");
  auto detached = controller.take_ready(first.request_id);
  require(detached && detached->world.systems.size() == 250,
          "take_ready did not return detached state");
  auto second = controller.start(prepared(500), research_root, catalog_path);
  require(second.accepted && second.request_id != first.request_id &&
              wait_terminal(controller).ready,
          "controller did not start a second generation after consumption");
  auto larger = controller.take_ready(second.request_id);
  require(larger && larger->world.systems.size() == 500 &&
              !controller.take_ready(first.request_id),
          "500-system result or stale request protection is wrong");
  auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
      std::move(larger->research), std::move(larger->world));
  require(runtime.world().campaign().systems.size() == 500,
          "taken detached owner could not activate on the main thread");
}

struct Barrier {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{}, released{};
};

void cancellation_and_owner_test(const fs::path &research_root,
                                 const fs::path &catalog_path) {
  Barrier barrier;
  std::atomic_bool crossed_cancel_boundary{};
  NativeDetachedCampaignGenerator blocked =
      [&](const NativePreparedNewCampaign &request, const fs::path &research,
          const fs::path &catalog, const NativeNewCampaignPhaseSink &phase) {
        phase(NativeNewCampaignGenerationPhase::LoadingCatalog);
        {
          std::unique_lock lock(barrier.mutex);
          barrier.entered = true;
          barrier.changed.notify_all();
          barrier.changed.wait(lock, [&] { return barrier.released; });
        }
        phase(NativeNewCampaignGenerationPhase::LoadingResearchDefinitions);
        crossed_cancel_boundary = true;
        return generate_detached_new_campaign(request, research, catalog, phase);
      };
  NativeNewCampaignGenerationController controller(blocked);
  const auto request = controller.start(prepared(), research_root, catalog_path);
  {
    std::unique_lock lock(barrier.mutex);
    require(barrier.changed.wait_for(lock, std::chrono::seconds{10},
                                     [&] { return barrier.entered; }),
            "blocking generator did not enter its barrier");
  }
  const auto before_wrong_thread = controller.poll();
  std::atomic_bool wrong_thread_rejected{};
  std::thread foreign([&] {
    try {
      (void)controller.cancel(request.request_id);
    } catch (const std::logic_error &) {
      wrong_thread_rejected = true;
    }
  });
  foreign.join();
  require(wrong_thread_rejected &&
              controller.poll().revision == before_wrong_thread.revision,
          "wrong-thread cancellation mutated controller state");
  require(!controller.start(prepared(), research_root, catalog_path).accepted &&
              controller.cancel(request.request_id) &&
              controller.poll().phase ==
                  NativeNewCampaignGenerationPhase::Cancelling,
          "busy/cancellation state did not preserve the active request");
  {
    std::lock_guard lock(barrier.mutex);
    barrier.released = true;
    barrier.changed.notify_all();
  }
  require(wait_terminal(controller).phase ==
              NativeNewCampaignGenerationPhase::Cancelled &&
              !crossed_cancel_boundary &&
              !controller.activate_ready(request.request_id) &&
              !controller.take_ready(request.request_id),
          "cancelled worker result remained activatable");
}

struct ThrowingCopyGenerator {
  std::shared_ptr<std::atomic_bool> fail_copy;

  explicit ThrowingCopyGenerator(std::shared_ptr<std::atomic_bool> value)
      : fail_copy(std::move(value)) {}
  ThrowingCopyGenerator(const ThrowingCopyGenerator &other)
      : fail_copy(other.fail_copy) {
    if (*fail_copy) throw std::runtime_error("callable copy fixture failure");
  }
  ThrowingCopyGenerator(ThrowingCopyGenerator &&) noexcept = default;

  NativeDetachedNewCampaign operator()(
      const NativePreparedNewCampaign &request, const fs::path &research,
      const fs::path &catalog, const NativeNewCampaignPhaseSink &phase) const {
    return generate_detached_new_campaign(request, research, catalog, phase);
  }
};

void launch_failure_and_retry_test(const fs::path &research_root,
                                   const fs::path &catalog_path) {
  auto fail_copy = std::make_shared<std::atomic_bool>(false);
  NativeDetachedCampaignGenerator callable = ThrowingCopyGenerator{fail_copy};
  NativeNewCampaignGenerationController controller(std::move(callable));
  *fail_copy = true;
  const auto failed = controller.start(prepared(), research_root, catalog_path);
  const auto failed_view = controller.poll();
  require(!failed.accepted && failed.request_id > 0 &&
              failed_view.phase == NativeNewCampaignGenerationPhase::Failed &&
              !failed_view.worker_running &&
              failed_view.status.find("callable copy fixture failure") !=
                  std::string::npos,
          "callable-copy launch failure left a busy or opaque controller");

  *fail_copy = false;
  const auto retried = controller.start(prepared(), research_root, catalog_path);
  require(retried.accepted && retried.request_id != failed.request_id &&
              wait_terminal(controller).ready &&
              controller.discard(retried.request_id),
          "controller did not permit an explicit retry after launch failure");
}

void activation_failure_test(const fs::path &research_root,
                             const fs::path &catalog_path) {
  NativeNewCampaignGenerationController controller(
      [](const NativePreparedNewCampaign &request, const fs::path &research,
         const fs::path &catalog, const NativeNewCampaignPhaseSink &phase) {
        auto detached =
            generate_detached_new_campaign(request, research, catalog, phase);
        detached.world.civilizations.front().species_id =
            "invalid_activation_species";
        return detached;
      });
  const auto request = controller.start(prepared(), research_root, catalog_path);
  require(request.accepted && wait_terminal(controller).ready,
          "invalid activation fixture did not become ready");
  bool threw{};
  try {
    (void)controller.activate_ready(request.request_id);
  } catch (const std::exception &) {
    threw = true;
  }
  const auto failed = controller.poll();
  require(threw && failed.phase == NativeNewCampaignGenerationPhase::Failed &&
              !failed.worker_running &&
              failed.status.find("invalid_activation_species") !=
                  std::string::npos &&
              !controller.activate_ready(request.request_id),
          "activation failure was left consumed or remained activatable");
}

void failure_preserves_live_runtime_test(const fs::path &research_root,
                                         const fs::path &catalog_path) {
  auto live_detached = generate_detached_new_campaign(
      prepared(), research_root, catalog_path,
      [](NativeNewCampaignGenerationPhase) {});
  auto live = IntegratedAdaptiveCampaignRuntime::create_fresh(
      std::move(live_detached.research), std::move(live_detached.world));
  const auto live_count = live.world().campaign().systems.size();
  const auto live_seed = live.world().campaign().seed;

  NativeNewCampaignGenerationController failed(
      [](const NativePreparedNewCampaign &, const fs::path &, const fs::path &,
         const NativeNewCampaignPhaseSink &phase)
          -> NativeDetachedNewCampaign {
        phase(NativeNewCampaignGenerationPhase::LoadingCatalog);
        throw std::runtime_error("catalog fixture failure");
      });
  const auto request = failed.start(prepared(), research_root, catalog_path);
  const auto result = wait_terminal(failed);
  require(request.accepted &&
              result.phase == NativeNewCampaignGenerationPhase::Failed &&
              result.status.find("catalog fixture failure") != std::string::npos &&
              !failed.activate_ready(request.request_id) &&
              live.world().campaign().systems.size() == live_count &&
              live.world().campaign().seed == live_seed,
          "worker failure lost diagnostics or changed the live campaign");
}

void ready_discard_and_join_test(const fs::path &research_root,
                                 const fs::path &catalog_path) {
  std::atomic_bool worker_returned{};
  {
    NativeNewCampaignGenerationController controller(
        [&](const NativePreparedNewCampaign &request, const fs::path &research,
            const fs::path &catalog, const NativeNewCampaignPhaseSink &phase) {
          auto value =
              generate_detached_new_campaign(request, research, catalog, phase);
          worker_returned = true;
          return value;
        });
    const auto request = controller.start(prepared(), research_root, catalog_path);
    require(wait_terminal(controller).ready && controller.discard(request.request_id) &&
                controller.poll().phase ==
                    NativeNewCampaignGenerationPhase::Cancelled &&
                !controller.activate_ready(request.request_id),
            "ready result was not safely discarded");
  }
  require(worker_returned,
          "controller destruction did not join its completed worker cleanly");
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 3,
          "Usage: native_new_campaign_generation_tests <research-root> <catalog>");
  const auto research_root = fs::absolute(argv[1]);
  const auto catalog_path = fs::absolute(argv[2]);
  actual_handoff_and_main_owner_test(research_root, catalog_path);
  take_and_repeat_500_test(research_root, catalog_path);
  cancellation_and_owner_test(research_root, catalog_path);
  launch_failure_and_retry_test(research_root, catalog_path);
  activation_failure_test(research_root, catalog_path);
  failure_preserves_live_runtime_test(research_root, catalog_path);
  ready_discard_and_join_test(research_root, catalog_path);
  std::cout << "native async campaign generation: 7/7 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native async campaign generation failed: " << error.what()
            << '\n';
  return 1;
}
