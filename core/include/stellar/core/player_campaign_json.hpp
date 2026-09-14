#pragma once

#include <stellar/core/player_campaign_persistence.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::core {

enum class PlayerCampaignJsonStage {
  Parse,
  Envelope,
  DiplomacyDecode,
  DiplomacyValidation,
  GalaxyDecode,
  GalaxyRestore,
  DiplomacyReferences,
  ResearchDecode,
  ResearchRestore,
  DiplomacyRestore,
  Representability,
  Encode,
};

struct PlayerCampaignJsonRestoreHooks {
  std::function<void(PlayerCampaignJsonStage)> on_stage;
};

class PlayerCampaignJsonError final : public std::runtime_error {
public:
  PlayerCampaignJsonError(
      PlayerCampaignJsonStage stage, std::string source_type,
      std::string message, std::optional<std::string> inner_type = std::nullopt,
      std::optional<std::string> inner_message = std::nullopt,
      std::string path = {}, std::optional<std::size_t> byte = std::nullopt);
  [[nodiscard]] PlayerCampaignJsonStage stage() const noexcept;
  [[nodiscard]] const std::string &source_type() const noexcept;
  [[nodiscard]] const std::optional<std::string> &inner_type() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  inner_message() const noexcept;
  [[nodiscard]] const std::string &path() const noexcept;
  [[nodiscard]] const std::optional<std::size_t> &byte() const noexcept;

private:
  PlayerCampaignJsonStage stage_;
  std::string source_type_;
  std::optional<std::string> inner_type_;
  std::optional<std::string> inner_message_;
  std::string path_;
  std::optional<std::size_t> byte_;
};

[[nodiscard]] std::string
encode_player_campaign_v17_json(const PlayerCampaignPayloadV17Dto &payload);

[[nodiscard]] RestoredPlayerCampaignV17 restore_player_campaign_v17_json(
    AdaptiveResearchStrategicRuntime research_runtime,
    std::string_view utf8_json,
    const PlayerCampaignJsonRestoreHooks &hooks = {});

} // namespace stellar::core
