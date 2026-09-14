#pragma once

#include "native_startup_host.hpp"
#include "native_startup_workspace.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stellar::native_startup_ui {
struct StartupEntryConfig {
  StartupHostConfig host;
  std::filesystem::path asset_root;
  std::function<std::string()> utc_timestamp;
};
struct StartupEntryAutomation {
  std::string seed_text, species_id;
  int system_count{};
  std::filesystem::path setup_screenshot, loading_screenshot;
};
struct StartupEntryEvidence {
  bool entry_opened{}, setup_opened{}, species_selected{}, size_selected{},
      seed_entered{}, create_requested{}, indeterminate_observed{};
  std::vector<std::string> displayed_statuses;
};
struct StartupEntryResult {
  std::unique_ptr<stellar::native_map::NativeCampaignSession> session;
  bool exit_requested{};
  StartupEntryEvidence evidence;
};
[[nodiscard]] StartupEntryResult run_native_startup_entry(
    stellar::native_map::Window &, StartupEntryConfig,
    const StartupEntryAutomation *automation = nullptr);
} // namespace stellar::native_startup_ui
