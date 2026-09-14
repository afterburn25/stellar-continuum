#pragma once

#include <stellar/core/player_campaign_persistence.hpp>

namespace stellar::core::detail {

// Internal staged-restore seam. Callers must have completed the Player wrapper
// and Diplomacy structural checks. This finalizes the remaining authored order:
// world reference validation, deferred research decoding/restoration, then
// Diplomacy restoration. Deferred decoding preserves Player17 JSON error order.
[[nodiscard]] RestoredPlayerCampaignV17 finalize_restored_player_campaign_v17(
    AdaptiveResearchStrategicRuntime research_runtime,
    RestoredGalaxyPayloadV16 restored_galaxy,
    std::function<AdaptiveResearchCampaignSnapshot()> decode_research,
    const DiplomacyStateSnapshot &diplomacy_snapshot);

} // namespace stellar::core::detail
