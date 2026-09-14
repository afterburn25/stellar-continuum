#include "native_startup_session.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace stellar::native_startup {
namespace fs = std::filesystem;
using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_setup;

namespace {
constexpr std::u8string_view suffix = u8".player17.json";

void require_path(const fs::path &path, const char *message) {
  if (path.empty()) throw std::invalid_argument(message);
}

std::u8string base_name(const fs::path &anchor) {
  auto name = anchor.filename().u8string();
  if (name.ends_with(suffix)) name.resize(name.size() - suffix.size());
  return name;
}

bool terminal(NativeStartupPhase phase) {
  return phase == NativeStartupPhase::Failed ||
         phase == NativeStartupPhase::Cancelled ||
         phase == NativeStartupPhase::Consumed;
}
} // namespace

fs::path unique_fresh_native_save_path(const fs::path &configured_default) {
  require_path(configured_default, "A configured campaign save path is required.");
  std::error_code error;
  if (!fs::exists(configured_default, error)) {
    if (error) throw fs::filesystem_error("Cannot inspect campaign save path",
                                           configured_default, error);
    return configured_default;
  }
  const auto stem = base_name(configured_default);
  const auto parent = configured_default.parent_path();
  for (std::uint64_t index = 1; index <= 1000000; ++index) {
    const auto number_text = std::to_string(index);
    std::u8string number(number_text.begin(), number_text.end());
    const auto candidate = parent / fs::path(
        stem + u8"-native-" + number + std::u8string(suffix));
    error.clear();
    if (!fs::exists(candidate, error)) {
      if (error) throw fs::filesystem_error("Cannot inspect native save slot",
                                             candidate, error);
      return candidate;
    }
  }
  throw std::runtime_error("No unused native campaign save slot is available.");
}

NativeStartupSaveSlots list_native_startup_save_slots(
    const fs::path &configured_default, const std::size_t cap) {
  NativeStartupSaveSlots result;
  try {
    require_path(configured_default, "A configured campaign save path is required.");
    if (cap == 0 || cap > 64) throw std::invalid_argument("Save-slot cap must be from 1 to 64.");
    std::error_code path_error;
    const auto anchor = fs::absolute(configured_default, path_error).lexically_normal();
    if (path_error)
      throw fs::filesystem_error("Cannot resolve campaign save path",
                                 configured_default, path_error);
    const auto parent = anchor.parent_path();
    if (!fs::exists(parent, path_error)) {
      if (path_error)
        throw fs::filesystem_error("Cannot inspect campaign save directory",
                                   parent, path_error);
      return result;
    }
    const auto stem = base_name(anchor);
    const auto prefix = stem + u8"-native-";
    for (const auto &entry : fs::directory_iterator(parent)) {
      if (!entry.is_regular_file()) continue;
      const auto path = entry.path();
      const auto name = path.filename().u8string();
      const bool is_anchor = fs::absolute(path).lexically_normal() == anchor;
      bool native = name.starts_with(prefix) && name.ends_with(suffix);
      if (native) {
        const auto number = std::u8string_view{name}.substr(
            prefix.size(), name.size() - prefix.size() - suffix.size());
        native = !number.empty() &&
                 std::ranges::all_of(number, [](const char8_t value) {
                   return value >= u8'0' && value <= u8'9';
                 });
      }
      if (!is_anchor && !native) continue;
      result.slots.push_back({std::string(name.begin(), name.end()), path,
          static_cast<std::int64_t>(entry.last_write_time().time_since_epoch().count())});
    }
    std::ranges::sort(result.slots, [](const auto &left, const auto &right) {
      if (left.last_write_ticks != right.last_write_ticks)
        return left.last_write_ticks > right.last_write_ticks;
      return left.filename < right.filename;
    });
    if (result.slots.size() > cap) result.slots.resize(cap);
  } catch (const std::exception &error) {
    result.slots.clear();
    result.error = error.what();
  }
  return result;
}

struct NativeStartupSessionController::Storage {
  Storage(NativeCampaignSessionDependencies value,
          NativeDetachedCampaignGenerator generator)
      : dependencies(std::move(value)), generation(std::move(generator)) {}

  void require_owner() const {
    if (std::this_thread::get_id() != owner)
      throw std::logic_error("Startup session controller used from a non-owner thread.");
  }

  std::thread::id owner{std::this_thread::get_id()};
  NativeCampaignSessionDependencies dependencies;
  NativeNewCampaignGenerationController generation;
  std::jthread loader;
  mutable std::mutex mutex;
  std::uint64_t next_id{1}, request_id{}, revision{}, generation_id{};
  NativeStartupPhase phase{NativeStartupPhase::Idle};
  std::string status{"No startup operation is active."};
  std::optional<double> progress;
  bool cancel_requested{};
  std::optional<LoadedPlayerCampaignV17> loaded;
  std::unique_ptr<NativeCampaignSession> ready_session;
  fs::path research_root, save_path, catalog_path;
  std::string game_version;
  enum class Kind { None, New, Load } kind{Kind::None};
};

NativeStartupSessionController::NativeStartupSessionController(
    NativeCampaignSessionDependencies dependencies,
    NativeDetachedCampaignGenerator generator)
    : storage_(std::make_unique<Storage>(std::move(dependencies),
                                         std::move(generator))) {}

NativeStartupSessionController::~NativeStartupSessionController() {
  if (!storage_) return;
  {
    std::lock_guard lock(storage_->mutex);
    storage_->cancel_requested = true;
  }
  if (storage_->generation_id) {
    try { (void)storage_->generation.cancel(storage_->generation_id); } catch (...) {}
  }
  if (storage_->loader.joinable()) storage_->loader.join();
}

NativeStartupStart NativeStartupSessionController::start_new(
    const NativePreparedNewCampaign &prepared, fs::path research_root,
    fs::path catalog_path, fs::path configured_default_save,
    std::string game_version) {
  storage_->require_owner();
  require_path(configured_default_save, "A configured campaign save path is required.");
  const auto current = poll();
  if (current.phase != NativeStartupPhase::Idle && !terminal(current.phase))
    return {false, current.request_id, "Finish or cancel the current startup operation first."};
  if (storage_->loader.joinable()) storage_->loader.join();
  const auto started = storage_->generation.start(prepared, research_root, catalog_path);
  std::lock_guard lock(storage_->mutex);
  storage_->request_id = storage_->next_id++;
  storage_->generation_id = started.request_id;
  storage_->research_root = std::move(research_root);
  storage_->catalog_path = std::move(catalog_path);
  storage_->save_path = std::move(configured_default_save);
  storage_->game_version = std::move(game_version);
  storage_->kind = Storage::Kind::New;
  storage_->cancel_requested = false;
  storage_->ready_session.reset();
  storage_->loaded.reset();
  storage_->phase = started.accepted ? NativeStartupPhase::Generating
                                     : NativeStartupPhase::Failed;
  storage_->status = started.message;
  storage_->progress.reset();
  ++storage_->revision;
  return {started.accepted, storage_->request_id, storage_->status};
}

NativeStartupStart NativeStartupSessionController::start_load(
    fs::path selected_save, fs::path research_root, std::string game_version) {
  storage_->require_owner();
  require_path(selected_save, "A selected campaign save path is required.");
  const auto current = poll();
  if (current.phase != NativeStartupPhase::Idle && !terminal(current.phase))
    return {false, current.request_id, "Finish or cancel the current startup operation first."};
  if (storage_->loader.joinable()) storage_->loader.join();
  std::uint64_t id;
  {
    std::lock_guard lock(storage_->mutex);
    id = storage_->request_id = storage_->next_id++;
    storage_->research_root = research_root;
    storage_->save_path = selected_save;
    storage_->game_version = std::move(game_version);
    storage_->kind = Storage::Kind::Load;
    storage_->cancel_requested = false;
    storage_->loaded.reset();
    storage_->ready_session.reset();
    storage_->phase = NativeStartupPhase::LoadingSave;
    storage_->status = "Loading the selected campaign save.";
    storage_->progress = 0.;
    ++storage_->revision;
  }
  auto *state = storage_.get();
  try {
    auto loader = storage_->dependencies.loader;
    storage_->loader = std::jthread([state, loader = std::move(loader),
                                    selected_save = std::move(selected_save),
                                    research_root = std::move(research_root), id] {
      try {
        const auto factory = [research_root] {
          return load_adaptive_research_strategic_runtime(research_root);
        };
        auto loaded = loader(selected_save, factory, [state, id](const auto &p) {
          std::lock_guard lock(state->mutex);
          if (state->request_id != id || state->cancel_requested) return;
          if (std::isfinite(p.fraction))
            state->progress = std::max(state->progress.value_or(0.),
                                       std::clamp(p.fraction, 0., 1.));
          state->status = p.status;
          ++state->revision;
        });
        std::lock_guard lock(state->mutex);
        if (state->request_id != id || state->cancel_requested) {
          state->phase = NativeStartupPhase::Cancelled;
          state->status = "Startup load was discarded.";
        } else {
          state->loaded.emplace(std::move(loaded));
          state->phase = NativeStartupPhase::AwaitingOwnerActivation;
          state->status = "Save restored and awaiting owner-thread activation.";
          state->progress = 1.;
        }
        ++state->revision;
      } catch (const std::exception &error) {
        std::lock_guard lock(state->mutex);
        state->phase = state->cancel_requested ? NativeStartupPhase::Cancelled
                                               : NativeStartupPhase::Failed;
        state->status = state->cancel_requested
                            ? "Startup load was discarded."
                            : std::string{"Campaign load failed: "} + error.what();
        ++state->revision;
      } catch (...) {
        std::lock_guard lock(state->mutex);
        state->phase = state->cancel_requested ? NativeStartupPhase::Cancelled
                                               : NativeStartupPhase::Failed;
        state->status = state->cancel_requested
                            ? "Startup load was discarded."
                            : "Campaign load failed: unknown error.";
        ++state->revision;
      }
    });
  } catch (const std::exception &error) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeStartupPhase::Failed;
    storage_->status = std::string{"Campaign load failed to start: "} + error.what();
    ++storage_->revision;
    return {false, id, storage_->status};
  } catch (...) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeStartupPhase::Failed;
    storage_->status = "Campaign load failed to start: unknown error.";
    ++storage_->revision;
    return {false, id, storage_->status};
  }
  return {true, id, "Campaign load started."};
}

void NativeStartupSessionController::service() {
  storage_->require_owner();
  Storage::Kind kind;
  std::uint64_t id{}, generation_id{};
  NativeStartupPhase startup_phase;
  {
    std::lock_guard lock(storage_->mutex);
    kind = storage_->kind;
    id = storage_->request_id;
    generation_id = storage_->generation_id;
    startup_phase = storage_->phase;
  }
  if (kind == Storage::Kind::New) {
    if (startup_phase != NativeStartupPhase::Generating &&
        startup_phase != NativeStartupPhase::Cancelling)
      return;
    const auto generated = storage_->generation.poll();
    if (generated.phase == NativeNewCampaignGenerationPhase::Failed) {
      std::lock_guard lock(storage_->mutex);
      storage_->phase = NativeStartupPhase::Failed;
      storage_->status = generated.status;
      ++storage_->revision;
      return;
    }
    if (generated.phase == NativeNewCampaignGenerationPhase::Cancelled) {
      std::lock_guard lock(storage_->mutex);
      storage_->phase = NativeStartupPhase::Cancelled;
      storage_->status = generated.status;
      ++storage_->revision;
      return;
    }
    if (!generated.ready) {
      std::lock_guard lock(storage_->mutex);
      if (storage_->phase != NativeStartupPhase::Cancelling) {
        if (storage_->phase != NativeStartupPhase::Generating ||
            storage_->status != generated.status) {
          storage_->phase = NativeStartupPhase::Generating;
          storage_->status = generated.status;
          ++storage_->revision;
        }
      }
      return;
    }
    try {
      {
        std::lock_guard lock(storage_->mutex);
        storage_->phase = NativeStartupPhase::Activating;
        storage_->status = "Creating the campaign session on its owner thread.";
        ++storage_->revision;
      }
      auto runtime = storage_->generation.activate_ready(generation_id);
      if (!runtime) throw std::runtime_error("Generated campaign was no longer available.");
      const auto path = unique_fresh_native_save_path(storage_->save_path);
      auto session = NativeCampaignSession::create_fresh(
          std::move(*runtime), storage_->research_root, path,
          storage_->game_version, storage_->dependencies);
      std::lock_guard lock(storage_->mutex);
      if (storage_->request_id != id || storage_->cancel_requested) {
        storage_->phase = NativeStartupPhase::Cancelled;
        storage_->status = "Generated campaign session was discarded.";
      } else {
        storage_->ready_session = std::move(session);
        storage_->phase = NativeStartupPhase::Ready;
        storage_->status = "New campaign session is ready.";
      }
      ++storage_->revision;
    } catch (const std::exception &error) {
      std::lock_guard lock(storage_->mutex);
      storage_->phase = NativeStartupPhase::Failed;
      storage_->status = std::string{"Campaign activation failed: "} + error.what();
      ++storage_->revision;
    } catch (...) {
      std::lock_guard lock(storage_->mutex);
      storage_->phase = NativeStartupPhase::Failed;
      storage_->status = "Campaign activation failed: unknown error.";
      ++storage_->revision;
    }
    return;
  }

  if (kind != Storage::Kind::Load) return;
  std::optional<LoadedPlayerCampaignV17> loaded;
  fs::path research, save;
  std::string version;
  {
    std::lock_guard lock(storage_->mutex);
    if (storage_->phase != NativeStartupPhase::AwaitingOwnerActivation ||
        !storage_->loaded) return;
    storage_->phase = NativeStartupPhase::Activating;
    storage_->status = "Creating the loaded campaign session on its owner thread.";
    loaded = std::move(storage_->loaded);
    research = storage_->research_root;
    save = storage_->save_path;
    version = storage_->game_version;
    ++storage_->revision;
  }
  if (storage_->loader.joinable()) storage_->loader.join();
  try {
    auto session = NativeCampaignSession::create_loaded(
        std::move(*loaded), std::move(research), std::move(save),
        std::move(version), storage_->dependencies);
    std::lock_guard lock(storage_->mutex);
    if (storage_->request_id != id || storage_->cancel_requested) {
      storage_->phase = NativeStartupPhase::Cancelled;
      storage_->status = "Loaded campaign session was discarded.";
    } else {
      storage_->ready_session = std::move(session);
      storage_->phase = NativeStartupPhase::Ready;
      storage_->status = "Loaded campaign session is ready.";
    }
    ++storage_->revision;
  } catch (const std::exception &error) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeStartupPhase::Failed;
    storage_->status = std::string{"Campaign activation failed: "} + error.what();
    ++storage_->revision;
  } catch (...) {
    std::lock_guard lock(storage_->mutex);
    storage_->phase = NativeStartupPhase::Failed;
    storage_->status = "Campaign activation failed: unknown error.";
    ++storage_->revision;
  }
}

NativeStartupView NativeStartupSessionController::poll() const {
  storage_->require_owner();
  std::lock_guard lock(storage_->mutex);
  const bool running = storage_->phase == NativeStartupPhase::Generating ||
                       storage_->phase == NativeStartupPhase::LoadingSave ||
                       storage_->phase == NativeStartupPhase::Cancelling;
  return {storage_->request_id, storage_->revision, storage_->phase,
          storage_->status, storage_->progress, running,
          storage_->phase == NativeStartupPhase::Ready};
}

bool NativeStartupSessionController::cancel(const std::uint64_t request_id) {
  storage_->require_owner();
  Storage::Kind kind;
  {
    std::lock_guard lock(storage_->mutex);
    if (request_id == 0 || request_id != storage_->request_id ||
        storage_->phase == NativeStartupPhase::Idle || terminal(storage_->phase))
      return false;
    const bool immediate = storage_->phase == NativeStartupPhase::Ready ||
                           storage_->phase == NativeStartupPhase::AwaitingOwnerActivation;
    storage_->cancel_requested = true;
    storage_->ready_session.reset();
    storage_->loaded.reset();
    kind = storage_->kind;
    storage_->phase = immediate ? NativeStartupPhase::Cancelled
                                : NativeStartupPhase::Cancelling;
    storage_->status = immediate
                           ? "Startup result was discarded."
                           : "Discarding startup work when its current operation finishes.";
    ++storage_->revision;
  }
  if (kind == Storage::Kind::New)
    (void)storage_->generation.cancel(storage_->generation_id);
  return true;
}

std::unique_ptr<NativeCampaignSession>
NativeStartupSessionController::take_ready_session(const std::uint64_t request_id) {
  storage_->require_owner();
  std::lock_guard lock(storage_->mutex);
  if (request_id == 0 || request_id != storage_->request_id ||
      storage_->phase != NativeStartupPhase::Ready || !storage_->ready_session)
    return {};
  auto result = std::move(storage_->ready_session);
  storage_->phase = NativeStartupPhase::Consumed;
  storage_->status = "Startup campaign session was consumed.";
  ++storage_->revision;
  return result;
}

} // namespace stellar::native_startup
