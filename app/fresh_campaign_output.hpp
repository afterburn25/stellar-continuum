#pragma once

#include <nlohmann/json.hpp>
#include <stellar/core/fresh_campaign.hpp>

// Diagnostic output only. This is not the campaign save or player-observation
// protocol.
void append_fresh_campaign_state(
    nlohmann::json &output, const stellar::core::FreshCampaignState &campaign);
