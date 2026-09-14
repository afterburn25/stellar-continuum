#pragma once

#include "native_new_campaign_setup.hpp"
#include "native_startup_session.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace stellar::native_startup_ui {
struct StartupHostConfig {
  std::filesystem::path research_root, catalog_path, default_save_path;
  std::string game_version;
};
struct StartupHostResult {
  bool accepted{};
  std::string message;
};
class NativeStartupHost final {
public:
  explicit NativeStartupHost(
      StartupHostConfig,
      stellar::native_map::NativeCampaignSessionDependencies = {},
      stellar::native_setup::NativeDetachedCampaignGenerator =
          stellar::native_setup::generate_detached_new_campaign);
  [[nodiscard]] stellar::native_setup::NativeNewCampaignSetupView setup() const;
  [[nodiscard]] stellar::native_startup::NativeStartupSaveSlots slots() const;
  [[nodiscard]] StartupHostResult start_new(
      const stellar::native_setup::NativeNewCampaignSetupInput &);
  [[nodiscard]] StartupHostResult start_load(const std::filesystem::path &);
  void service();
  [[nodiscard]] stellar::native_startup::NativeStartupView poll() const;
  [[nodiscard]] bool cancel();
  [[nodiscard]] std::unique_ptr<stellar::native_map::NativeCampaignSession>
  take_ready();
private:
  StartupHostConfig config_;
  stellar::native_setup::NativeNewCampaignSetupController setup_controller_;
  stellar::native_startup::NativeStartupSessionController startup_;
  std::uint64_t request_id_{};
};
} // namespace stellar::native_startup_ui
