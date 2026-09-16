#pragma once

#include <stellar/core/fresh_campaign.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace stellar::core {

// Source: Game.Campaign.DeveloperCommandService — explicit testing operations
// on a marked Developer world. Normal gameplay never calls this boundary and
// cannot activate it by selecting a faster clock or opening a panel.
struct DeveloperCommandDefinition {
  std::string_view id;
  std::string_view title;
  std::string_view description;
};

struct DeveloperCommandResult {
  bool accepted{};
  std::string message;
};

[[nodiscard]] std::span<const DeveloperCommandDefinition>
developer_command_catalog();

// Executes a catalog command against a campaign that carries Developer
// provenance. advance_days is invoked only for "advance_30_days"; the caller
// supplies the ordinary simulation step path (reference
// AdvanceDeveloperDays runs the integrated step in 0.25-day increments and
// writes one checkpoint afterward). Provenance is marked tools_used before
// any mutation so a partial failure still retains the mark.
[[nodiscard]] DeveloperCommandResult execute_developer_command(
    FreshCampaignState &galaxy, std::string_view command_id,
    const std::function<void(double)> &advance_days);

} // namespace stellar::core
