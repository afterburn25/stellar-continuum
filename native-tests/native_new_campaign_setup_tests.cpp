#include "native_new_campaign_setup.hpp"

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace stellar::core;
using namespace stellar::native_setup;
namespace fs = std::filesystem;

namespace {
void require(const bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

NativeNewCampaignSetupAssessment prepare(
    const NativeNewCampaignSetupController &controller, std::string seed,
    const int count = 500,
    std::string species = "terran_baseline", const int pre_warp = 6,
    const int ancient = 1) {
  return controller.prepare(
      {std::move(seed), count, std::move(species), "2044-05-06T07:08:09Z",
       pre_warp, ancient});
}

IntegratedAdaptiveCampaignRuntime create(
    const NativeNewCampaignSetupController &controller,
    const NativePreparedNewCampaign &prepared, const fs::path &research_root,
    const std::vector<CatalogStar> &catalog) {
  return controller.create_runtime(
      prepared, load_adaptive_research_strategic_runtime(research_root), catalog);
}

const Civilization &player(const FreshCampaignState &world) {
  const auto found = std::ranges::find(world.civilizations,
                                       world.player_civilization_id,
                                       &Civilization::id);
  if (found == world.civilizations.end())
    throw std::runtime_error("generated setup has no player civilization");
  return *found;
}

const Colony &home_colony(const FreshCampaignState &world,
                          const Civilization &civilization) {
  const auto found = std::ranges::find_if(world.colonies, [&](const auto &value) {
    return value.civilization_id == civilization.id &&
           value.system_id == civilization.home_system_id;
  });
  if (found == world.colonies.end())
    throw std::runtime_error("generated setup has no player home colony");
  return *found;
}

std::string deterministic_signature(const FreshCampaignState &world) {
  std::ostringstream out;
  out << world.seed << ':' << world.player_civilization_id << ':';
  for (const auto &system : world.systems)
    out << system.id << ',' << system.name << ',' << system.position.x << ','
        << system.position.y << ';';
  for (const auto &civilization : world.civilizations)
    out << civilization.id << ',' << civilization.species_id << ','
        << civilization.home_system_id << ';';
  for (const auto &colony : world.colonies)
    out << colony.id << ',' << colony.civilization_id << ',' << colony.system_id
        << ',' << colony.planetary_body_id.value_or(-1) << ';';
  return out.str();
}

void catalog_view_test() {
  NativeNewCampaignSetupController controller;
  auto first = controller.build();
  require(first.species.size() == species_environment_profiles().size() &&
              first.species.size() == 4,
          "setup did not expose the four authoritative native species");
  require(first.size_presets.size() == 4 &&
              first.size_presets[0].system_count == 250 &&
              first.size_presets[1].system_count == 500 &&
              first.size_presets[2].system_count == 1000 &&
              first.size_presets[3].system_count == 2500 &&
              first.size_presets[0].label == "Small - 250 systems" &&
              first.size_presets[1].label == "Medium - 500 systems" &&
              first.size_presets[2].label == "Large - 1,000 systems" &&
              first.size_presets[3].label == "Huge - 2,500 systems" &&
              first.size_presets[1].recommended,
          "setup did not expose the canonical size choices");
  require(first.pre_warp_civilization_presets.size() == 5 &&
              first.pre_warp_civilization_presets[0].count == 1 &&
              first.pre_warp_civilization_presets[4].count == 13 &&
              first.ancient_civilization_presets.size() == 3 &&
              first.ancient_civilization_presets[0].count == 0 &&
              first.ancient_civilization_presets[2].count == 2,
          "setup did not expose the supported civilization counts");
  for (std::size_t index = 0; index < first.species.size(); ++index) {
    const auto &actual = species_environment_profiles()[index];
    const auto &copy = first.species[index];
    require(copy.id == actual.id && copy.display_name == actual.display_name &&
                copy.gravity_g.preferred == actual.gravity_g.preferred &&
                copy.temperature_kelvin.preferred ==
                    actual.temperature_kelvin.preferred &&
                copy.pressure_kpa.preferred == actual.pressure_kpa.preferred &&
                copy.radiation_tolerance == actual.radiation_tolerance &&
                copy.requires_immersion == actual.requires_immersion &&
                copy.breathable_atmospheres == actual.breathable_atmospheres &&
                copy.compatible_solvents == actual.compatible_solvents &&
                !copy.biochemistry_label.empty() &&
                !copy.preferred_atmosphere_label.empty() &&
                !copy.biological_solvent_label.empty(),
            "setup changed an authoritative species environment fact");
  }
  first.species.front().display_name = "mutated detached copy";
  first.species.front().breathable_atmospheres.clear();
  const auto second = controller.build();
  require(second.species.front().display_name == "Terran Baseline" &&
              !second.species.front().breathable_atmospheres.empty(),
          "mutating a detached setup view changed the authoritative catalog");
}

void validation_and_detachment_test() {
  NativeNewCampaignSetupController controller;
  for (const auto bad : {"", " ", "remember me", "1.0", "1 2",
                         "9223372036854775808", "-9223372036854775809", "+",
                         "++5", "+-5", "--5", "-+5"})
    require(!prepare(controller, bad).accepted,
            "invalid numeric seed was accepted");
  require(prepare(controller, " -9223372036854775808 ").accepted &&
              prepare(controller, "+9223372036854775807").accepted,
          "valid signed int64 boundary seed was rejected");
  require(!prepare(controller, "1", 123).accepted &&
              !prepare(controller, "1", 500, "unknown_species").accepted &&
              !prepare(controller, "1", 500, "terran_baseline", 2, 1).accepted &&
              !prepare(controller, "1", 500, "terran_baseline", 6, 3).accepted &&
              !controller.prepare({"1", 500, "terran_baseline", " \t"})
                   .accepted,
          "invalid campaign setup option was accepted");
  auto ready = prepare(controller, "42");
  require(ready.accepted && ready.prepared && ready.prepared->seed() == 42 &&
              ready.prepared->options().system_count == 500 &&
              ready.prepared->options().pre_warp_civilization_count == 6 &&
              ready.prepared->options().ancient_civilization_count == 1,
          "valid setup did not create canonical immutable options");
  auto varied = prepare(controller, "43", 500, "terran_baseline", 13, 2);
  require(varied.accepted && varied.prepared &&
              varied.prepared->options().pre_warp_civilization_count == 13 &&
              varied.prepared->options().ancient_civilization_count == 2,
          "supported civilization counts did not reach immutable options");
  const auto original = ready.prepared->options();
  (void)prepare(controller, "bad", 2500, "pelagic_high_pressure");
  require(ready.prepared->options().created_at_utc == original.created_at_utc &&
              ready.prepared->options().system_count == original.system_count &&
              ready.prepared->options().player_species_id ==
                  original.player_species_id,
          "rejected setup mutated an earlier prepared request");
}

void all_sizes_test(const fs::path &research_root,
                    const std::vector<CatalogStar> &catalog) {
  NativeNewCampaignSetupController controller;
  for (const auto count : std::array{250, 500, 1000, 2500}) {
    auto assessed = prepare(controller, "8374837", count);
    require(assessed.accepted && assessed.prepared,
            "supported size failed setup validation");
    auto runtime = create(controller, *assessed.prepared, research_root, catalog);
    const auto &world = runtime.world().campaign();
    require(world.systems.size() == static_cast<std::size_t>(count) &&
                world.core && world.galactic_core &&
                world.generation_metadata &&
                world.generation_metadata->system_count == count &&
                !world.knowledge.has_galactic_core_access(
                    world.player_civilization_id) &&
                !world.knowledge.is_galactic_core_discovered(
                    world.player_civilization_id),
            "canonical size generation count/core knowledge is wrong");
  }
}

void founding_and_runtime_test(const fs::path &research_root,
                               const std::vector<CatalogStar> &catalog) {
  NativeNewCampaignSetupController controller;
  auto human_request = prepare(controller, "137500");
  auto human = create(controller, *human_request.prepared, research_root, catalog);
  const auto &human_world = human.world().campaign();
  const auto &human_player = player(human_world);
  const auto &earth = home_colony(human_world, human_player);
  const auto human_economy = std::ranges::find(
      human_world.economies, human_player.id,
      &CivilizationEconomy::civilization_id);
  const auto human_technology = std::ranges::find(
      human_world.technologies, human_player.id,
      &TechnologyState::civilization_id);
  require(human_player.species_id == "terran_baseline" &&
              human_player.home_system_id == sol_system_id &&
              earth.planetary_body_id == earth_body_id &&
              human_economy != human_world.economies.end() &&
              human_economy->credits == 500. &&
              human_technology != human_world.technologies.end() &&
              human_technology->completed_technology_ids.values().empty() &&
              !human_technology->active_research_id,
          "human setup changed canonical Earth/economy/research founding");

  auto alien_request = prepare(controller, "137501", 500,
                               "cryogenic_hydrocarbon");
  auto alien = create(controller, *alien_request.prepared, research_root, catalog);
  const auto &alien_world = alien.world().campaign();
  const auto &alien_player = player(alien_world);
  const auto &alien_home = home_colony(alien_world, alien_player);
  require(alien_player.species_id == "cryogenic_hydrocarbon" &&
              alien_home.population_species_id == "cryogenic_hydrocarbon" &&
              alien_player.home_system_id != sol_system_id &&
              alien_home.planetary_body_id != earth_body_id &&
              std::ranges::any_of(alien_world.systems, [](const auto &system) {
                return system.catalog_preset_id == sol_catalog_preset_id;
              }),
          "nonhuman setup bypassed canonical homeworld planning or removed Sol");
}

void reproducibility_test(const fs::path &research_root,
                          const std::vector<CatalogStar> &catalog) {
  NativeNewCampaignSetupController controller;
  auto request = prepare(controller, "-9223372036854775808", 250,
                         "compact_high_gravity");
  auto first = create(controller, *request.prepared, research_root, catalog);
  auto second = create(controller, *request.prepared, research_root, catalog);
  require(deterministic_signature(first.world().campaign()) ==
              deterministic_signature(second.world().campaign()),
          "identical native setup options produced different founding state");
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 3,
          "Usage: native_new_campaign_setup_tests <research-root> <catalog>");
  const auto research_root = fs::absolute(argv[1]);
  const auto catalog = load_nearby_catalog(fs::absolute(argv[2]));
  catalog_view_test();
  validation_and_detachment_test();
  all_sizes_test(research_root, catalog);
  founding_and_runtime_test(research_root, catalog);
  reproducibility_test(research_root, catalog);
  std::cout << "native new-campaign setup: 5/5 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native new-campaign setup failed: " << error.what() << '\n';
  return 1;
}
