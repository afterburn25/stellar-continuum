// Native inspection card tests — the observer-gated system intelligence
// snapshot behind the galaxy-map selected-star card: unknown/detected/partial
// survey gates, detailed-survey facts, colony secrecy and own-colony logistics.
#include "native_inspection.hpp"

#include <iostream>
#include <string>

using namespace stellar::native_inspection;
using namespace stellar::core;
namespace native_map = stellar::native_map;

namespace {

int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

FreshCampaignState campaign_fixture() {
  FreshCampaignState campaign;
  campaign.player_civilization_id = 1;

  StellarSystem home;
  home.id = 1;
  home.name = "Sol";
  home.position = {0.f, 0.f, 0.f};
  home.primary = StellarClass::GYellowDwarf;
  StellarSystem target;
  target.id = 2;
  target.name = "Proxima";
  target.position = {4.24f, 0.f, 0.f};
  target.primary = StellarClass::MRedDwarf;
  target.archetype = StarArchetype::HabitableRich;
  target.has_habitable_world = true;
  target.has_anomaly = true;
  StellarSystem foreign_home;
  foreign_home.id = 3;
  foreign_home.name = "Vask Prime";
  foreign_home.position = {30.f, 0.f, 0.f};
  campaign.systems = {home, target, foreign_home};

  Civilization player;
  player.id = 1;
  player.name = "Terrans";
  player.home_system_id = 1;
  player.is_player = true;
  player.development_stage = CivilizationDevelopmentStage::WarpCapable;
  Civilization foreign;
  foreign.id = 2;
  foreign.name = "Vask";
  foreign.home_system_id = 3;
  campaign.civilizations = {player, foreign};
  return campaign;
}

} // namespace

int main() {
  {
    // No selection: the card prompts for a target.
    const auto campaign = campaign_fixture();
    const auto inspection = build_system_inspection(campaign, -1);
    check(inspection.name == "SELECT A STAR",
          "an empty selection must prompt for a star");
    check(inspection.facts.empty(),
          "an empty selection must not leak facts");
  }
  {
    // A system outside the campaign is a lost target.
    const auto campaign = campaign_fixture();
    const auto inspection = build_system_inspection(campaign, 99);
    check(inspection.name == "TARGET LOST",
          "a missing system must read as a lost target");
  }
  {
    // Unknown survey: name stays UNKNOWN, only the distance fact shows, and
    // the colony card is gated behind "Survey required".
    auto campaign = campaign_fixture();
    Colony secret;
    secret.id = 5;
    secret.civilization_id = 2;
    secret.system_id = 2;
    secret.name = "Vask Hold";
    campaign.colonies.push_back(secret);
    const auto inspection = build_system_inspection(campaign, 2);
    check(inspection.name == "UNKNOWN" && inspection.survey_status == "Unknown",
          "an unsurveyed system must never leak its name");
    check(inspection.facts.size() == 1 &&
              inspection.facts[0].label == "DISTANCE FROM HOMEWORLD",
          "an unsurveyed system must only show the homeward distance");
    check(inspection.colony_name == "NO COLONY DATA" &&
              inspection.colony_details == "Survey required",
          "an unsurveyed system must hide all settlement data");
  }
  {
    // Detected survey: name revealed, no fact grid, colony still gated.
    auto campaign = campaign_fixture();
    campaign.knowledge.reveal_system(1, 2);
    Colony secret;
    secret.id = 5;
    secret.civilization_id = 2;
    secret.system_id = 2;
    secret.name = "Vask Hold";
    campaign.colonies.push_back(secret);
    const auto inspection = build_system_inspection(campaign, 2);
    check(inspection.name == "PROXIMA" &&
              inspection.survey_status == "Detected",
          "a detected system must reveal its catalog name");
    check(inspection.colony_name == "COLONY STATUS UNKNOWN" &&
              inspection.colony_details == "Detailed survey required",
          "a partial survey must gate settlement intelligence");
    check(inspection.guidance.find("detailed survey") != std::string::npos,
          "a detected system must explain the survey requirement");
  }
  {
    // Fully surveyed: the fact grid lists the detailed signatures and a
    // foreign colony the player does not know stays unidentified.
    auto campaign = campaign_fixture();
    campaign.knowledge.mark_system_fully_surveyed(1, 2);
    Colony secret;
    secret.id = 5;
    secret.civilization_id = 2;
    secret.system_id = 2;
    secret.name = "Vask Hold";
    campaign.colonies.push_back(secret);
    const auto inspection = build_system_inspection(campaign, 2);
    check(inspection.has_detailed_survey && inspection.facts.size() == 7,
          "a surveyed system must show the full fact grid");
    check(inspection.facts[0].label == "PRIMARY STAR" &&
              inspection.facts[0].value == "M-type red dwarf",
          "the star class must use the reference label");
    check(inspection.facts[3].value == "Yes" && inspection.facts[4].value == "Yes",
          "habitable/anomaly signatures must report on a surveyed system");
    check(inspection.colony_name == "COLONY PRESENCE UNIDENTIFIED",
          "an unknown owner's colony must stay unidentified");
    check(inspection.colony_details.find("Vask") == std::string::npos,
          "colony secrecy must not leak the owner name");
  }
  {
    // Known foreign colony: name and stats are public; logistics stay hidden.
    auto campaign = campaign_fixture();
    campaign.knowledge.mark_system_fully_surveyed(1, 2);
    campaign.knowledge.reveal_civilization(1, 2);
    Colony foreign_colony;
    foreign_colony.id = 5;
    foreign_colony.civilization_id = 2;
    foreign_colony.system_id = 2;
    foreign_colony.name = "Vask Hold";
    foreign_colony.population_millions = 12.34;
    campaign.colonies.push_back(foreign_colony);
    const auto inspection = build_system_inspection(campaign, 2);
    check(inspection.colony_name == "VASK HOLD",
          "a known owner's colony must show its name");
    check(inspection.colony_details.find("Supply") == std::string::npos,
          "a foreign colony must not expose the player's logistics");
  }
  {
    // Own colony: the card carries the player's logistics detail.
    auto campaign = campaign_fixture();
    campaign.knowledge.mark_system_fully_surveyed(1, 1);
    Colony own;
    own.id = 7;
    own.civilization_id = 1;
    own.system_id = 1;
    own.name = "Sol Prime";
    own.population_millions = 812.5;
    campaign.colonies.push_back(own);
    CivilizationEconomy economy;
    economy.civilization_id = 1;
    campaign.economies.push_back(economy);
    const auto inspection = build_system_inspection(campaign, 1);
    check(inspection.colony_name == "SOL PRIME",
          "an owned colony must show its name");
    check(inspection.colony_details.find("Population 812.5M") !=
              std::string::npos,
          "an owned colony must show its population");
  }
  {
    // The renderer must emit a bounded card inside the viewport.
    native_map::DrawList out;
    const auto campaign = campaign_fixture();
    const auto inspection = build_system_inspection(campaign, 2);
    const auto bounds = append_inspection_card(out, inspection,
                                               {14.f, 512.f}, 1.f);
    check(bounds.width > 100.f && bounds.height > 100.f && bounds.y > 0.f,
          "the card must render a bounded panel");
    check(!out.overlay.empty(), "the card must emit overlay commands");
  }

  if (failures) {
    std::cerr << failures << " native inspection test(s) failed\n";
    return 1;
  }
  std::cout << "native inspection tests passed\n";
  return 0;
}
