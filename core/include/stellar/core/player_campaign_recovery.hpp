#pragma once

#include <stellar/core/player_campaign_json.hpp>

#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class PlayerCampaignLoadOrigin { Primary, Backup };
enum class PlayerCampaignLoadAttemptKind { Missing, Failed };

struct PlayerCampaignRestorationProgress {
  double fraction{};
  std::string status;
};

struct PlayerCampaignLoadAttempt {
  PlayerCampaignLoadOrigin origin{};
  std::filesystem::path path;
  PlayerCampaignLoadAttemptKind kind{};
  std::string failure;
  std::exception_ptr exception;
};

class PlayerCampaignLoadError final : public std::runtime_error {
public:
  PlayerCampaignLoadError(std::string message,
                          std::vector<PlayerCampaignLoadAttempt> attempts);
  [[nodiscard]] const std::vector<PlayerCampaignLoadAttempt> &attempts()
      const noexcept;
private:
  std::vector<PlayerCampaignLoadAttempt> attempts_;
};

// Each restore attempt consumes its runtime. The factory must return a new
// independently owned runtime on every invocation; a failed attempt is never
// reused for the backup attempt.
using PlayerCampaignRuntimeFactory =
    std::function<AdaptiveResearchStrategicRuntime()>;

struct LoadedPlayerCampaignV17 {
  RestoredPlayerCampaignV17 campaign;
  PlayerCampaignLoadOrigin origin;
  std::filesystem::path requested_path;
  std::filesystem::path loaded_path;
  std::vector<PlayerCampaignLoadAttempt> prior_attempts;
};

// Bounded current-Player recovery only: request path once, then request path
// plus .bak once. It neither creates a campaign nor writes/repairs either file.
[[nodiscard]] LoadedPlayerCampaignV17 load_existing_player_campaign_v17(
    const std::filesystem::path &save_path,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const std::function<void(const PlayerCampaignRestorationProgress &)> &
        progress = {});

// Explicit developer entry uses the same recovery/validation pipeline but
// accepts only the tagged developer envelope; it never promotes a player save.
[[nodiscard]] LoadedPlayerCampaignV17 load_existing_developer_campaign(
    const std::filesystem::path &, const PlayerCampaignRuntimeFactory &,
    const std::function<void(const PlayerCampaignRestorationProgress &)> & = {});

} // namespace stellar::core
