#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/colony_economy.hpp>

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::core {

struct AdaptiveResearchFundingQuote {
  double assigned_effective_labs{};
  double authorization_credits{};
  double milestone_commitment_credits{};
  double operating_credits_per_day{};
  double estimated_total_operating_credits{};
  double estimated_total_credits{};
  double estimated_years_at_full_funding{};
  std::string complexity;
};

class AdaptiveResearchFundingArgumentRangeError final
    : public std::out_of_range {
public:
  explicit AdaptiveResearchFundingArgumentRangeError(std::string message);
};

class AdaptiveResearchFundingPolicyError final : public std::runtime_error {
public:
  explicit AdaptiveResearchFundingPolicyError(std::string message);
};

class AdaptiveResearchFundingSequenceError final : public std::runtime_error {
public:
  explicit AdaptiveResearchFundingSequenceError(std::string message);
};

class AdaptiveResearchFundingPolicy final {
public:
  // Preserves the source campaign's existing internal credit scale.
  static constexpr double base_annual_credits_per_effective_lab = 1.5;

  [[nodiscard]] static AdaptiveResearchFundingQuote
  quote(const AdaptiveResearchNodeDefinition &node,
        double assigned_effective_labs,
        const AdaptiveResearchCatalog &catalog);
  [[nodiscard]] static double
  authorization_credits(std::string_view complexity);
  [[nodiscard]] static double
  milestone_commitment_credits(std::string_view complexity);
  [[nodiscard]] static double estimate_treasury_runway_days(
      double available_credits, double net_credits_per_day_before_research,
      double research_operating_credits_per_day);
  [[nodiscard]] static double
  complexity_multiplier(std::string_view complexity);
};

struct AdaptiveResearchFundingWorldView {
  // Commands borrow these spans only for the call. Economy entries are mutated
  // only after the corresponding research command and reservation succeed.
  std::span<const Civilization> civilizations;
  std::span<CivilizationEconomy> economies;
};

class AdaptiveResearchCampaignCommands final {
public:
  [[nodiscard]] static double
  credits_needed_to_start(const AdaptiveResearchFundingQuote &quote) noexcept;

  [[nodiscard]] static AdaptiveResearchCommandResult start_directed_research(
      AdaptiveResearchFundingWorldView world,
      AdaptiveResearchCampaignState &campaign, int civilization_id,
      std::string_view node_id, double requested_assigned_labs,
      std::optional<std::string_view> target_applicability_context_id =
          std::nullopt);

  [[nodiscard]] static AdaptiveResearchCommandResult pause_directed_research(
      AdaptiveResearchCampaignState &campaign, int civilization_id,
      std::string_view node_id);

  [[nodiscard]] static AdaptiveResearchCommandResult resume_directed_research(
      AdaptiveResearchFundingWorldView world,
      AdaptiveResearchCampaignState &campaign, int civilization_id,
      std::string_view node_id, double requested_assigned_labs);
};

} // namespace stellar::core
