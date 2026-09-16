#pragma once

#include <stellar/core/player_campaign_json.hpp>

#include <string>
#include <string_view>

namespace stellar::core {

// Source: Game.Persistence.DeveloperCampaignPersistenceService — a separate
// Developer envelope around the canonical, validated campaign payload.
// Developer provenance lives only in the envelope; the nested campaign stays
// byte-compatible with the Player17 codec.
struct LoadedDeveloperCampaignV17 {
  RestoredPlayerCampaignV17 campaign;
  bool tools_used{};
};

// Reference PrepareDeveloperPayload: the canonical capture for a world that
// carries Developer provenance. The Player capture rejects such worlds, so
// this is the only write path for a Developer campaign.
[[nodiscard]] PlayerCampaignPayloadV17Dto capture_developer_campaign_v17(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options);

[[nodiscard]] std::string encode_developer_campaign_json(
    const PlayerCampaignPayloadV17Dto &payload, bool tools_used);

// Verifies the strict envelope fields, rejects contradictory nested session
// metadata, restores the canonical payload through the Player17 codec, and
// stamps the envelope's provenance on the galaxy.
[[nodiscard]] LoadedDeveloperCampaignV17 restore_developer_campaign_json(
    AdaptiveResearchStrategicRuntime research_runtime,
    std::string_view utf8_json,
    const PlayerCampaignJsonRestoreHooks &hooks = {});

} // namespace stellar::core
