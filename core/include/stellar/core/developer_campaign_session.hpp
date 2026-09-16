#pragma once

#include <stellar/core/developer_campaign_json.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_recovery.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

// Source: Game.Campaign.DeveloperCampaignSessionService — Developer session
// lifecycle with a separate save slot and ordinary campaign rules.
inline constexpr std::string_view developer_save_file_name =
    "developer-autosave.json";
inline constexpr std::string_view legacy_demo_save_file_name =
    "demo-autosave.json";
// Source: PlayableDemoScenario.Seed — the repeatable human Earth/Sol demo
// world used when no Developer or legacy demo save exists.
inline constexpr std::int64_t playable_demo_seed = 20260908;

enum class DeveloperCampaignSource {
  Created,
  LoadedSave,
  RecoveredFromBackup,
  RecoveredFromInvalidSave,
  ImportedLegacyDemo,
  ImportedLegacyDemoBackup,
};

struct DeveloperCampaignBootstrap {
  // Loaded sources carry a restored campaign; Created and
  // RecoveredFromInvalidSave carry a fresh persistable world instead.
  std::optional<RestoredPlayerCampaignV17> loaded;
  std::optional<FreshCampaignState> fresh;
  DeveloperCampaignSource source{};
  std::filesystem::path requested_path;
  std::filesystem::path loaded_path;
  std::vector<PlayerCampaignLoadAttempt> prior_attempts;
  std::string load_failure;
};

// Reference SavePathBeside: the Developer slot shares the player save
// directory under its own file name.
[[nodiscard]] std::filesystem::path
developer_save_path_beside(const std::filesystem::path &player_save_path);

// Fresh Developer campaign: canonical full-galaxy generation stamped with
// unused Developer provenance.
[[nodiscard]] FreshCampaignState create_developer_campaign(
    std::int64_t seed, std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options);

// Loads the existing Developer slot without generating or writing a
// replacement. Falls back to the .bak twin, then to the legacy demo save
// pair, and throws PlayerCampaignLoadError when every candidate fails.
[[nodiscard]] DeveloperCampaignBootstrap load_existing_developer_campaign(
    const std::filesystem::path &save_path,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const std::function<void(const PlayerCampaignRestorationProgress &)>
        &progress = {});

// Reference LoadOrCreate: primary, backup, legacy demo pair, then a fresh
// campaign. Broken Developer saves never cause an older demo import to
// silently replace later progress; the legacy import happens only when no
// Developer primary or backup exists.
[[nodiscard]] DeveloperCampaignBootstrap load_or_create_developer_campaign(
    const std::filesystem::path &save_path, std::int64_t fallback_seed,
    std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const std::function<void(const PlayerCampaignRestorationProgress &)>
        &progress = {});

} // namespace stellar::core
