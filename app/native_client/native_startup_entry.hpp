#pragma once

#include "native_startup_host.hpp"
#include "native_startup_workspace.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stellar::native_audio { class NativeAudioSettings; }
namespace stellar::native_startup_ui {
struct StartupAudioHooks {
  std::function<void()> service, menu_ready, confirm;
  std::function<bool()> assets_ready;
};
struct StartupEntryConfig {
  StartupHostConfig host;
  std::filesystem::path asset_root;
  std::function<std::string()> utc_timestamp;
  std::chrono::milliseconds minimum_boot_artwork{std::chrono::seconds(7)};
  StartupAudioHooks audio;
  stellar::native_audio::NativeAudioSettings* audio_settings{};
  bool return_to_campaign_available{};
};
enum class StartupEntryAutomationAction { Create, ReturnToCampaign, Exit };
struct StartupEntryAutomation {
  std::string seed_text, species_id;
  int system_count{};
  std::filesystem::path setup_screenshot, loading_screenshot;
  std::filesystem::path audio_settings_path, audio_settings_screenshot;
  StartupEntryAutomationAction action{StartupEntryAutomationAction::Create};
};
struct StartupEntryEvidence {
  bool entry_opened{}, setup_opened{}, species_selected{}, size_selected{},
      seed_entered{}, create_requested{}, indeterminate_observed{};
  std::vector<std::string> displayed_statuses;
  bool boot_presented{}, menu_ready_called{}, returned_to_campaign{},
      exit_requested{};
};
struct StartupEntryResult {
  std::unique_ptr<stellar::native_map::NativeCampaignSession> session;
  bool exit_requested{};
  StartupEntryEvidence evidence;
  bool return_to_campaign{};
};
[[nodiscard]] StartupEntryResult run_native_startup_entry(
    stellar::native_map::Window &, StartupEntryConfig,
    const StartupEntryAutomation *automation = nullptr);
} // namespace stellar::native_startup_ui
