#include <stellar/core/developer_commands.hpp>

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/legacy_research.hpp>
#include <stellar/core/legacy_technology.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace stellar::core {
namespace {

constexpr std::array<DeveloperCommandDefinition, 6> command_definitions{{
    {"grant_resources", "Add test resources",
     "Add sovereign treasury funds and 1,000 industry to your civilization."},
    {"finish_orders", "Finish current orders",
     "Fund and finish your active research, project, ship and placed surface "
     "sites. Only the active ship completes; later ships still need "
     "construction."},
    {"reveal_galaxy", "Survey the galaxy",
     "Reveal system survey information to your Developer civilization."},
    {"unlock_technology", "Unlock gameplay technology",
     "Complete the current gameplay technology and empire project catalogs. "
     "Ships still need construction."},
    {"unlock_research", "Unlock research only",
     "Grant gameplay capabilities while preserving construction orders and "
     "project timers for testing."},
    {"advance_30_days", "Advance 30 days",
     "Run 30 days through normal simulation rules, including other "
     "civilizations and diplomacy."},
}};

std::span<const DeveloperCommandDefinition> catalog() noexcept {
  return command_definitions;
}

LegacyResearchWorldView research_world(FreshCampaignState &galaxy) noexcept {
  return {galaxy.civilizations, galaxy.technologies, galaxy.construction,
          galaxy.economies};
}

std::function<bool(int, std::string_view)> all_capabilities() {
  return [](int, std::string_view) { return true; };
}

ConstructionWorld construction_world(FreshCampaignState &galaxy) {
  return {galaxy.civilizations, galaxy.bodies, galaxy.construction,
          galaxy.colonies,      galaxy.economies,
          std::span<const CivilizationConstructionCapabilities>{},
          all_capabilities()};
}

ShipbuildingWorld shipbuilding_world(FreshCampaignState &galaxy) {
  return {galaxy.civilizations,
          galaxy.systems,
          galaxy.construction,
          galaxy.shipyards,
          galaxy.colonies,
          galaxy.economies,
          galaxy.fleets,
          std::span<const ShipbuildingCapabilities>{},
          std::span<const ShipbuildingStrategicPreference>{},
          all_capabilities(),
          {}};
}

// Source DeveloperCommandService.FinishOrders: fund and run the active
// research, empire project plus placed surface sites, and only the active
// ship through the canonical simulation passes.
void finish_orders(FreshCampaignState &galaxy, const int player_id) {
  auto &economy = *std::ranges::find(
      galaxy.economies, player_id, &CivilizationEconomy::civilization_id);
  auto &research = *std::ranges::find(
      galaxy.technologies, player_id, &TechnologyState::civilization_id);
  if (research.active_research_id) {
    const auto remaining = std::max(
        0., get_legacy_technology(*research.active_research_id).research_cost -
                research.active_research_progress);
    economy.science = std::max(economy.science, remaining);
    (void)LegacyResearchSimulation{}.advance_for_civilization(
        research_world(galaxy), player_id);
  }
  const auto world = construction_world(galaxy);
  const auto demand =
      construction_industry_demand(world.read(), player_id);
  economy.industry = std::max(economy.industry, demand);
  (void)advance_construction_for_civilization(world, player_id, demand,
                                              1'000'000.);
  const auto ships = shipbuilding_world(galaxy);
  const auto ship_demand =
      shipbuilding_industry_demand(ships.read(), player_id);
  if (std::ranges::find(galaxy.shipyards, player_id,
                        &ShipyardState::civilization_id)
          ->active_design_id) {
    // Keep the canonical population/cargo/fleet handoff. Only the active
    // ship completes.
    economy.industry = std::max(economy.industry, ship_demand);
    (void)advance_shipbuilding_for_civilization(ships, player_id, ship_demand,
                                                1'000'000.);
  }
}

} // namespace

std::span<const DeveloperCommandDefinition> developer_command_catalog() {
  return catalog();
}

DeveloperCommandResult execute_developer_command(
    FreshCampaignState &galaxy, const std::string_view command_id,
    const std::function<void(double)> &advance_days) {
  if (!galaxy.developer_provenance)
    return {false,
            "Developer commands are unavailable in Player mode. Open your "
            "separate Developer campaign first."};
  const auto known = std::ranges::find(catalog(), command_id,
                                       &DeveloperCommandDefinition::id);
  if (known == catalog().end())
    return {false, "Unknown Developer command."};
  if (command_id == "advance_30_days" && !advance_days)
    return {false,
            "The campaign simulation must be connected before advancing "
            "time."};
  const auto player_id = galaxy.player_civilization_id;
  auto &economy = *std::ranges::find(
      galaxy.economies, player_id, &CivilizationEconomy::civilization_id);
  if (!std::isfinite(economy.credits) || !std::isfinite(economy.industry) ||
      !std::isfinite(economy.science) || economy.credits < 0. ||
      economy.industry < 0. || economy.science < 0.)
    return {false,
            "The campaign has invalid resources. Load a valid checkpoint "
            "before using Developer commands."};
  // Mark before mutation: even an unexpected partial failure must retain
  // its provenance.
  galaxy.developer_provenance->tools_used = true;
  if (command_id == "grant_resources") {
    economy.credits += 1000.;
    economy.industry += 1000.;
    const auto currency = sovereign_currency_for_civilization(
        galaxy.civilizations, player_id);
    return {true, "Added " + currency.format(1000.) +
                      " and 1,000 industry. This Developer campaign is "
                      "marked Tools used."};
  }
  if (command_id == "finish_orders") {
    finish_orders(galaxy, player_id);
    return {true,
            "Current research, project, active ship and surface sites "
            "completed. Tools used is saved with this campaign."};
  }
  if (command_id == "reveal_galaxy") {
    for (const auto &system : galaxy.systems)
      (void)galaxy.knowledge.mark_system_fully_surveyed(player_id,
                                                        system.id);
    return {true,
            "Your Developer civilization has surveyed every system. Other "
            "observers keep their own knowledge."};
  }
  if (command_id == "unlock_technology" || command_id == "unlock_research") {
    auto &technology = *std::ranges::find(
        galaxy.technologies, player_id, &TechnologyState::civilization_id);
    for (const auto &definition : legacy_technology_catalog())
      (void)technology.completed_technology_ids.insert(definition.id);
    technology.active_research_id.reset();
    technology.active_research_progress = 0.;
    if (command_id == "unlock_technology") {
      auto &construction = *std::ranges::find(
          galaxy.construction, player_id,
          &ConstructionState::civilization_id);
      for (const auto &project : construction_project_catalog())
        if (std::ranges::find(construction.completed_project_ids,
                              project.id) ==
            construction.completed_project_ids.end())
          construction.completed_project_ids.push_back(project.id);
      construction.active_project_id.reset();
      construction.active_project_progress = 0.;
      construction.active_project_authorization_credits = 0.;
      construction.queued_projects.clear();
    }
    for (auto &civilization : galaxy.civilizations)
      if (civilization.id == player_id &&
          civilization.development_stage ==
              CivilizationDevelopmentStage::PreWarp)
        civilization.development_stage =
            CivilizationDevelopmentStage::WarpCapable;
    return {true,
            command_id == "unlock_research"
                ? "Gameplay technology unlocked. Infrastructure still "
                  "requires construction. Tools used is saved with this "
                  "campaign."
                : "Gameplay technology and empire projects unlocked for "
                  "your Developer civilization."};
  }
  advance_days(30.);
  return {true,
          "Advanced 30 days through the normal simulation. Tools used is "
          "saved with this campaign."};
}

} // namespace stellar::core
