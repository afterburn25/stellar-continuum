#include "native_new_campaign_generation.hpp"

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace stellar::native_setup {
namespace {
std::string status_for(const NativeNewCampaignGenerationPhase phase) {
  switch (phase) {
  case NativeNewCampaignGenerationPhase::Idle:
    return "No campaign generation is active.";
  case NativeNewCampaignGenerationPhase::LoadingCatalog:
    return "Loading the stellar catalog.";
  case NativeNewCampaignGenerationPhase::LoadingResearchDefinitions:
    return "Loading research definitions.";
  case NativeNewCampaignGenerationPhase::SeedingCampaign:
    return "Generating the campaign. This step has no progress estimate.";
  case NativeNewCampaignGenerationPhase::Ready:
    return "Campaign generation is ready.";
  case NativeNewCampaignGenerationPhase::Failed:
    return "Campaign generation failed.";
  case NativeNewCampaignGenerationPhase::Cancelling:
    return "Cancellation requested. Work stops at the next generation "
           "boundary; an in-progress campaign seed must finish before its "
           "result is discarded.";
  case NativeNewCampaignGenerationPhase::Cancelled:
    return "Campaign generation was discarded.";
  case NativeNewCampaignGenerationPhase::Consumed:
    return "Campaign generation was consumed.";
  }
  return "Campaign generation state is unavailable.";
}

bool terminal(const NativeNewCampaignGenerationPhase phase) noexcept {
  return phase == NativeNewCampaignGenerationPhase::Failed ||
         phase == NativeNewCampaignGenerationPhase::Cancelled ||
         phase == NativeNewCampaignGenerationPhase::Consumed;
}

bool worker_progress_phase(
    const NativeNewCampaignGenerationPhase phase) noexcept {
  return phase == NativeNewCampaignGenerationPhase::LoadingCatalog ||
         phase == NativeNewCampaignGenerationPhase::LoadingResearchDefinitions ||
         phase == NativeNewCampaignGenerationPhase::SeedingCampaign;
}

struct GenerationCancelled final {};
} // namespace

NativeDetachedNewCampaign generate_detached_new_campaign(
    const NativePreparedNewCampaign &prepared,
    const std::filesystem::path &research_root,
    const std::filesystem::path &catalog_path,
    const NativeNewCampaignPhaseSink &phase) {
  phase(NativeNewCampaignGenerationPhase::LoadingCatalog);
  auto catalog = stellar::core::load_nearby_catalog(catalog_path);
  phase(NativeNewCampaignGenerationPhase::LoadingResearchDefinitions);
  auto research =
      stellar::core::load_adaptive_research_strategic_runtime(research_root);
  phase(NativeNewCampaignGenerationPhase::SeedingCampaign);
  auto world = stellar::core::seed_persistable_fresh_campaign(
      prepared.seed(), catalog, prepared.options());
  return {std::move(world), std::move(research)};
}

struct NativeNewCampaignGenerationController::Storage {
  explicit Storage(NativeDetachedCampaignGenerator value)
      : generator(std::move(value)) {}

  void require_owner() const {
    if (std::this_thread::get_id() != owner)
      throw std::logic_error(
          "New-campaign generation controller used from a non-owner thread.");
  }

  std::thread::id owner{std::this_thread::get_id()};
  NativeDetachedCampaignGenerator generator;
  mutable std::mutex mutex;
  std::jthread worker;
  std::uint64_t next_request_id{1}, request_id{}, revision{};
  NativeNewCampaignGenerationPhase phase{NativeNewCampaignGenerationPhase::Idle};
  std::string status{status_for(NativeNewCampaignGenerationPhase::Idle)};
  bool cancel_requested{};
  std::optional<NativeDetachedNewCampaign> result;
};

NativeNewCampaignGenerationController::NativeNewCampaignGenerationController()
    : NativeNewCampaignGenerationController(generate_detached_new_campaign) {}

NativeNewCampaignGenerationController::NativeNewCampaignGenerationController(
    NativeDetachedCampaignGenerator generator)
    : storage_(std::make_unique<Storage>(std::move(generator))) {
  if (!storage_->generator)
    throw std::invalid_argument("Detached campaign generator is required.");
}

NativeNewCampaignGenerationController::~NativeNewCampaignGenerationController() {
  if (!storage_) return;
  {
    std::lock_guard lock(storage_->mutex);
    storage_->cancel_requested = true;
  }
  if (storage_->worker.joinable()) storage_->worker.join();
}

NativeNewCampaignGenerationStart NativeNewCampaignGenerationController::start(
    const NativePreparedNewCampaign &prepared,
    std::filesystem::path research_root, std::filesystem::path catalog_path) {
  storage_->require_owner();
  {
    std::lock_guard lock(storage_->mutex);
    if (storage_->phase == NativeNewCampaignGenerationPhase::Ready ||
        (!terminal(storage_->phase) &&
         storage_->phase != NativeNewCampaignGenerationPhase::Idle))
      return {false, storage_->request_id,
              "Finish or discard the current campaign generation first."};
  }
  if (storage_->worker.joinable()) storage_->worker.join();

  std::uint64_t request_id{};
  {
    std::lock_guard lock(storage_->mutex);
    request_id = storage_->next_request_id++;
    storage_->request_id = request_id;
    storage_->cancel_requested = false;
    storage_->result.reset();
    storage_->phase = NativeNewCampaignGenerationPhase::LoadingCatalog;
    storage_->status = status_for(storage_->phase);
    ++storage_->revision;
  }
  try {
    auto generator = storage_->generator;
    auto *state = storage_.get();
    storage_->worker = std::jthread(
      [state, generator = std::move(generator), prepared,
       research_root = std::move(research_root),
       catalog_path = std::move(catalog_path), request_id]() mutable {
        const auto report = [state, request_id](const auto phase) {
          if (!worker_progress_phase(phase))
            throw std::invalid_argument(
                "Detached generator reported an invalid progress phase.");
          std::lock_guard lock(state->mutex);
          if (state->request_id != request_id || state->cancel_requested)
            throw GenerationCancelled{};
          state->phase = phase;
          state->status = status_for(phase);
          ++state->revision;
        };
        try {
          auto generated =
              generator(prepared, research_root, catalog_path, report);
          if(prepared.developer_mode()){
            if(!generated.world.developer_provenance)generated.world.developer_provenance=stellar::core::CampaignDeveloperProvenance{};
            generated.developer_research=prepared.developer_research();
            if(prepared.developer_full_exploration())stellar::core::fully_explore_developer_galaxy(generated.world);
          }
          std::lock_guard lock(state->mutex);
          if (state->request_id != request_id || state->cancel_requested) {
            state->phase = NativeNewCampaignGenerationPhase::Cancelled;
            state->status = status_for(state->phase);
          } else {
            state->result.emplace(std::move(generated));
            state->phase = NativeNewCampaignGenerationPhase::Ready;
            state->status = status_for(state->phase);
          }
          ++state->revision;
        } catch (const GenerationCancelled &) {
          std::lock_guard lock(state->mutex);
          state->result.reset();
          state->phase = NativeNewCampaignGenerationPhase::Cancelled;
          state->status = status_for(state->phase);
          ++state->revision;
        } catch (const std::exception &error) {
          std::lock_guard lock(state->mutex);
          if (state->request_id != request_id || state->cancel_requested) {
            state->phase = NativeNewCampaignGenerationPhase::Cancelled;
            state->status = status_for(state->phase);
          } else {
            state->phase = NativeNewCampaignGenerationPhase::Failed;
            state->status = std::string{"Campaign generation failed: "} +
                            error.what();
          }
          ++state->revision;
        } catch (...) {
          std::lock_guard lock(state->mutex);
          state->phase = state->cancel_requested
                             ? NativeNewCampaignGenerationPhase::Cancelled
                             : NativeNewCampaignGenerationPhase::Failed;
          state->status = state->cancel_requested
                              ? status_for(state->phase)
                              : "Campaign generation failed: unknown error.";
          ++state->revision;
        }
      });
  } catch (const std::exception &error) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeNewCampaignGenerationPhase::Failed;
    storage_->status = std::string{"Campaign generation failed to start: "} +
                       error.what();
    ++storage_->revision;
    return {false, request_id, storage_->status};
  } catch (...) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeNewCampaignGenerationPhase::Failed;
    storage_->status =
        "Campaign generation failed to start: unknown error.";
    ++storage_->revision;
    return {false, request_id, storage_->status};
  }
  return {true, request_id, "Campaign generation started."};
}

NativeNewCampaignGenerationView
NativeNewCampaignGenerationController::poll() const {
  storage_->require_owner();
  std::lock_guard lock(storage_->mutex);
  return {storage_->request_id,
          storage_->revision,
          storage_->phase,
          storage_->status,
          storage_->phase != NativeNewCampaignGenerationPhase::Idle &&
              storage_->phase != NativeNewCampaignGenerationPhase::Ready &&
              !terminal(storage_->phase),
          storage_->phase == NativeNewCampaignGenerationPhase::Ready};
}

bool NativeNewCampaignGenerationController::cancel(
    const std::uint64_t request_id) {
  storage_->require_owner();
  std::lock_guard lock(storage_->mutex);
  if (request_id == 0 || request_id != storage_->request_id ||
      storage_->phase == NativeNewCampaignGenerationPhase::Idle ||
      terminal(storage_->phase))
    return false;
  storage_->cancel_requested = true;
  storage_->result.reset();
  if (storage_->phase == NativeNewCampaignGenerationPhase::Ready) {
    storage_->phase = NativeNewCampaignGenerationPhase::Cancelled;
  } else {
    storage_->phase = NativeNewCampaignGenerationPhase::Cancelling;
  }
  storage_->status = status_for(storage_->phase);
  ++storage_->revision;
  return true;
}

bool NativeNewCampaignGenerationController::discard(
    const std::uint64_t request_id) {
  return cancel(request_id);
}

std::optional<NativeDetachedNewCampaign>
NativeNewCampaignGenerationController::take_ready(
    const std::uint64_t request_id) {
  storage_->require_owner();
  std::lock_guard lock(storage_->mutex);
  if (request_id == 0 || request_id != storage_->request_id ||
      storage_->phase != NativeNewCampaignGenerationPhase::Ready ||
      !storage_->result)
    return {};
  auto result = std::move(storage_->result);
  storage_->result.reset();
  storage_->phase = NativeNewCampaignGenerationPhase::Consumed;
  storage_->status = status_for(storage_->phase);
  ++storage_->revision;
  return result;
}

std::optional<stellar::core::IntegratedAdaptiveCampaignRuntime>
NativeNewCampaignGenerationController::activate_ready(
    const std::uint64_t request_id) {
  storage_->require_owner();
  auto detached = take_ready(request_id);
  if (!detached) return {};
  try {
    auto runtime=stellar::core::IntegratedAdaptiveCampaignRuntime::create_fresh(
        std::move(detached->research), std::move(detached->world));
    if(runtime.world().campaign().developer_provenance)
      (void)stellar::core::initialize_developer_research(runtime,detached->developer_research);
    return runtime;
  } catch (const std::exception &error) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeNewCampaignGenerationPhase::Failed;
    storage_->status = std::string{"Campaign activation failed: "} + error.what();
    ++storage_->revision;
    throw;
  } catch (...) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeNewCampaignGenerationPhase::Failed;
    storage_->status = "Campaign activation failed: unknown error.";
    ++storage_->revision;
    throw;
  }
}

} // namespace stellar::native_setup
