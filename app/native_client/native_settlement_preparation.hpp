#pragma once
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/colony_biology.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace stellar::native_settlement_preparation {

struct Option {
  std::string design_id, design_name, formatted_ship_cost, formatted_expedition_cost;
  double industry_cost{}, minimum_build_days{}, population_reservation_millions{};
  double expedition_cost{}, establishment_days{};
  std::optional<std::string> shipbuilding_blocker;
};

struct View {
  std::uint64_t campaign_generation{};
  int player_civilization_id{}, system_id{}, body_id{};
  std::string body_name, species_id, species_name, formatted_treasury;
  stellar::core::SovereignCurrencyDefinition currency;
  double treasury{};
  bool solid_surface{}, native_pre_warp_life{}, rare_resource{};
  // This is only the canonical body's static biology/site assessment. Fleet,
  // route, claim, and expedition-funding checks remain order-time concerns.
  bool site_can_found_current_colony{};
  stellar::core::SpeciesPlanetaryColonizationAssessment suitability;
  Option colony_ship, resource_outpost;
};

[[nodiscard]] std::optional<View> build_settlement_preparation(
    stellar::core::CampaignFrame &, std::uint64_t, int, int);

}  // namespace stellar::native_settlement_preparation
