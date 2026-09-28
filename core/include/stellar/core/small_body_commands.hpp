#pragma once
#include <stellar/core/fresh_campaign.hpp>
namespace stellar::core {
// Returns the stable field ID. Requires the isolated developer envelope.
int force_developer_small_body_field(FreshCampaignState&,int system_id,SmallBodyFieldType,double epoch_days,int preferred_parent=-1);
// Effort/cargo-limited hook for mining, salvage or colony supply consumers.
// The caller credits returned units to its stock in the same transaction.
std::array<double,small_body_resource_count> harvest_small_body_supply(
    SmallBodyField&,std::uint32_t body,double capacity,
    const std::array<double,small_body_resource_count>& demand);
}
