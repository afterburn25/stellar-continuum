#include <stellar/core/construction_projects.hpp>
#include <stellar/core/developer_commands.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/legacy_technology.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>

using namespace stellar::core;

namespace {
int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

FreshCampaignState developer_campaign(std::span<const CatalogStar> catalog) {
  auto galaxy = seed_fresh_campaign(4242, catalog, 250, 2, 0);
  galaxy.developer_provenance = CampaignDeveloperProvenance{};
  return galaxy;
}

CivilizationEconomy &player_economy(FreshCampaignState &galaxy) {
  return *std::ranges::find(galaxy.economies, galaxy.player_civilization_id,
                            &CivilizationEconomy::civilization_id);
}

TechnologyState &player_research(FreshCampaignState &galaxy) {
  return *std::ranges::find(galaxy.technologies, galaxy.player_civilization_id,
                            &TechnologyState::civilization_id);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: developer_commands_tests <catalog.json>\n";
    return 2;
  }
  const auto catalog = load_nearby_catalog(argv[1]);
  const auto definitions = developer_command_catalog();
  check(definitions.size() == 6, "catalog exposes the six reference commands");
  check(std::ranges::find(definitions, std::string_view("advance_30_days"),
                          &DeveloperCommandDefinition::id) != definitions.end(),
        "catalog includes advance_30_days");

  auto player = seed_fresh_campaign(4242, catalog, 250, 2, 0);
  const std::function<void(double)> no_advance;
  auto result =
      execute_developer_command(player, "grant_resources", no_advance);
  check(!result.accepted && result.message.contains("Player mode"),
        "commands reject campaigns without Developer provenance");

  auto galaxy = developer_campaign(catalog);
  result = execute_developer_command(galaxy, "not_a_command", no_advance);
  check(!result.accepted, "unknown command ids reject");
  result = execute_developer_command(galaxy, "advance_30_days", no_advance);
  check(!result.accepted, "advance_30_days requires a simulation callback");

  player_economy(galaxy).credits = std::numeric_limits<double>::quiet_NaN();
  result = execute_developer_command(galaxy, "grant_resources", no_advance);
  check(!result.accepted, "commands reject invalid campaign resources");
  check(!galaxy.developer_provenance->tools_used,
        "rejected commands do not mark tools_used");

  galaxy = developer_campaign(catalog);
  const auto credits = player_economy(galaxy).credits;
  const auto industry = player_economy(galaxy).industry;
  result = execute_developer_command(galaxy, "grant_resources", no_advance);
  check(result.accepted, "grant_resources accepts on a Developer campaign");
  check(player_economy(galaxy).credits == credits + 1000. &&
            player_economy(galaxy).industry == industry + 1000.,
        "grant_resources adds the reference resource amounts");
  check(galaxy.developer_provenance->tools_used,
        "accepted commands mark tools_used");

  result = execute_developer_command(galaxy, "reveal_galaxy", no_advance);
  check(result.accepted, "reveal_galaxy accepts");
  check(std::ranges::all_of(
            galaxy.systems,
            [&](const StellarSystem &system) {
              return galaxy.knowledge.is_system_fully_surveyed(
                  galaxy.player_civilization_id, system.id);
            }),
        "reveal_galaxy fully surveys every system for the player");

  auto &research = player_research(galaxy);
  const auto tech = std::ranges::find_if(
      legacy_technology_catalog(),
      [](const TechnologyDefinition &definition) {
        return definition.prerequisites.empty() &&
               definition.required_projects.empty() &&
               definition.research_cost > 0.;
      });
  check(tech != legacy_technology_catalog().end(),
        "technology catalog has a prerequisite-free technology");
  if (tech != legacy_technology_catalog().end()) {
    research.active_research_id = tech->id;
    research.active_research_progress = 0.;
    result = execute_developer_command(galaxy, "finish_orders", no_advance);
    check(result.accepted, "finish_orders accepts");
    check(research.completed_technology_ids.contains(tech->id) &&
              !research.active_research_id,
          "finish_orders completes active research through the canonical pass");
  }

  result = execute_developer_command(galaxy, "unlock_research", no_advance);
  check(result.accepted, "unlock_research accepts");
  check(std::ranges::all_of(
            legacy_technology_catalog(),
            [&](const TechnologyDefinition &definition) {
              return research.completed_technology_ids.contains(definition.id);
            }),
        "unlock_research completes the whole technology catalog");

  auto &construction = *std::ranges::find(
      galaxy.construction, galaxy.player_civilization_id,
      &ConstructionState::civilization_id);
  result = execute_developer_command(galaxy, "unlock_technology", no_advance);
  check(result.accepted, "unlock_technology accepts");
  check(std::ranges::all_of(
            construction_project_catalog(),
            [&](const ConstructionProjectDefinition &project) {
              return std::ranges::find(construction.completed_project_ids,
                                       project.id) !=
                     construction.completed_project_ids.end();
            }),
        "unlock_technology completes the empire project catalog");
  check(std::ranges::find(galaxy.civilizations,
                          galaxy.player_civilization_id, &Civilization::id)
            ->development_stage == CivilizationDevelopmentStage::WarpCapable,
        "unlock commands promote a PreWarp player to WarpCapable");

  double advanced = 0.;
  result = execute_developer_command(
      galaxy, "advance_30_days", [&](const double days) { advanced = days; });
  check(result.accepted && advanced == 30.,
        "advance_30_days runs 30 days through the caller simulation path");

  if (failures != 0) {
    std::cerr << failures << " developer command checks failed\n";
    return 1;
  }
  std::cout << "developer command checks passed\n";
  return 0;
}
