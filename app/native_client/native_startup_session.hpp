#pragma once

#include "native_campaign_session.hpp"
#include "native_new_campaign_generation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_startup {

enum class NativeStartupPhase {
  Idle, Generating, LoadingSave, AwaitingOwnerActivation, Activating,
  Ready, Failed, Cancelling, Cancelled, Consumed
};

struct NativeStartupView {
  std::uint64_t request_id{}, revision{};
  NativeStartupPhase phase{NativeStartupPhase::Idle};
  std::string status;
  std::optional<double> determinate_progress;
  bool worker_running{}, ready{};
};

struct NativeStartupStart {
  bool accepted{};
  std::uint64_t request_id{};
  std::string message;
};

struct NativeStartupSaveSlot {
  std::string filename;
  std::filesystem::path path;
  std::int64_t last_write_ticks{};
};

struct NativeStartupSaveSlots {
  std::vector<NativeStartupSaveSlot> slots;
  std::string error;
};

[[nodiscard]] std::filesystem::path unique_fresh_native_save_path(
    const std::filesystem::path &configured_default);
[[nodiscard]] NativeStartupSaveSlots list_native_startup_save_slots(
    const std::filesystem::path &configured_default, std::size_t cap = 16);

class NativeStartupSessionController final {
public:
  explicit NativeStartupSessionController(
      stellar::native_map::NativeCampaignSessionDependencies = {},
      stellar::native_setup::NativeDetachedCampaignGenerator =
          stellar::native_setup::generate_detached_new_campaign);
  ~NativeStartupSessionController();
  NativeStartupSessionController(const NativeStartupSessionController &) = delete;
  NativeStartupSessionController &operator=(const NativeStartupSessionController &) = delete;

  [[nodiscard]] NativeStartupStart start_new(
      const stellar::native_setup::NativePreparedNewCampaign &,
      std::filesystem::path research_root, std::filesystem::path catalog_path,
      std::filesystem::path configured_default_save, std::string game_version);
  [[nodiscard]] NativeStartupStart start_load(
      std::filesystem::path selected_save, std::filesystem::path research_root,
      std::string game_version);
  // Performs only owner-thread activation. It never blocks waiting for a worker.
  void service();
  void set_developer_mode(bool enabled);
  [[nodiscard]] NativeStartupView poll() const;
  [[nodiscard]] bool cancel(std::uint64_t request_id);
  [[nodiscard]] std::unique_ptr<stellar::native_map::NativeCampaignSession>
  take_ready_session(std::uint64_t request_id);

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::native_startup
