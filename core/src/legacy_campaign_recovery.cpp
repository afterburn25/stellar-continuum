#include <stellar/core/legacy_campaign_recovery.hpp>

#include <stellar/core/combat_state.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  if (value.empty()) return false;
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty()) return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value)) return false;
  return true;
}

bool known_species(std::string_view id) {
  return std::ranges::any_of(species_environment_profiles(),
                             [id](const auto &profile) {
                               return profile.id == id;
                             });
}

std::string require_species(const std::string &id, int colony_id) {
  if (blank(id) || !known_species(id))
    throw LegacyCampaignRecoveryDataError(
        "source colony " + std::to_string(colony_id) +
        " references unknown population species ID '" + id + "'.");
  return id;
}

const ShipDesignDefinition *first_colony_design() {
  const auto designs = ship_design_catalog();
  const auto found = std::ranges::find_if(
      designs, [](const auto &design) { return design.role == FleetRole::Colony; });
  return found == designs.end() ? nullptr : &*found;
}

bool candidate_precedes(double candidate, double current) noexcept {
  // Comparer<double>.Default orders NaN before finite values. Descending order
  // therefore places finite values first while stable ties keep input order.
  if (std::isnan(current)) return !std::isnan(candidate);
  return !std::isnan(candidate) && candidate > current;
}

Colony *largest_colony(std::span<Colony> colonies, int civilization_id) {
  Colony *selected = nullptr;
  for (auto &colony : colonies) {
    if (colony.civilization_id != civilization_id) continue;
    if (!selected ||
        candidate_precedes(colony.population_millions,
                           selected->population_millions))
      selected = &colony;
  }
  return selected;
}

int wrapping_increment(int value) noexcept {
  static_assert(sizeof(int) == sizeof(std::uint32_t));
  const auto bits = std::bit_cast<std::uint32_t>(value) + std::uint32_t{1};
  return std::bit_cast<int>(bits);
}

const StellarSystem &require_home(std::span<const StellarSystem> systems,
                                  int home_system_id) {
  const auto found = std::ranges::find_if(
      systems, [home_system_id](const auto &system) {
        return system.id == home_system_id;
      });
  if (found == systems.end())
    throw LegacyCampaignRecoveryOperationError(
        "Sequence contains no matching element");
  return *found;
}

} // namespace

CivilizationKnowledgeState
create_legacy_initial_knowledge(std::span<const StellarSystem> systems,
                                std::span<const Civilization> civilizations) {
  CivilizationKnowledgeState knowledge;
  for (const auto &civilization : civilizations) {
    knowledge.mark_system_fully_surveyed(civilization.id,
                                         civilization.home_system_id);
    if (std::ranges::none_of(systems, [&](const auto &system) {
          return system.id == civilization.home_system_id;
        }))
      throw LegacyCampaignRecoveryOperationError(
          "Unknown sensor origin system " +
          std::to_string(civilization.home_system_id) + ".");
    knowledge.reveal_within_sensor_range(
        civilization.id, civilization.home_system_id, systems,
        civilization.is_seeded_ancient ? 420.0F : 95.0F);
  }
  return knowledge;
}

std::vector<TechnologyState> create_migrated_legacy_technology_states(
    std::span<const Civilization> civilizations) {
  auto states = seed_legacy_technologies(civilizations);
  for (const auto &civilization : civilizations) {
    if (civilization.development_stage !=
        CivilizationDevelopmentStage::WarpCapable)
      continue;
    const auto state = std::ranges::find_if(
        states, [&](const auto &candidate) {
          return candidate.civilization_id == civilization.id;
        });
    for (const auto &technology : legacy_technology_catalog())
      state->completed_technology_ids.insert(technology.id);
  }
  return states;
}

std::vector<ConstructionState> create_migrated_legacy_construction_states(
    std::span<const Civilization> civilizations) {
  auto states = seed_construction(civilizations);
  for (const auto &civilization : civilizations) {
    if (civilization.development_stage !=
        CivilizationDevelopmentStage::WarpCapable)
      continue;
    const auto state = std::ranges::find_if(
        states, [&](const auto &candidate) {
          return candidate.civilization_id == civilization.id;
        });
    for (const auto &project : construction_project_catalog())
      if (std::ranges::find(state->completed_project_ids, project.id) ==
          state->completed_project_ids.end())
        state->completed_project_ids.push_back(project.id);
  }
  return states;
}

void ensure_legacy_expansion_fleets(
    std::vector<FleetState> &fleets,
    std::span<const StellarSystem> systems,
    std::span<const Civilization> civilizations,
    std::span<Colony> colonies) {
  const auto *design = first_colony_design();
  if (!design || design->population_cost_millions <= 0) return;

  int next_id = 0;
  if (!fleets.empty()) {
    const auto maximum = std::ranges::max_element(
        fleets, {}, &FleetState::id);
    next_id = wrapping_increment(maximum->id);
  }

  for (const auto &civilization : civilizations) {
    if (civilization.development_stage !=
            CivilizationDevelopmentStage::WarpCapable ||
        !civilization.expansion_allowed)
      continue;

    const auto existing = std::ranges::find_if(
        fleets, [&](const auto &fleet) {
          return fleet.is_active &&
                 fleet.civilization_id == civilization.id &&
                 fleet.role == FleetRole::Colony;
        });
    if (existing != fleets.end() &&
        existing->embarked_population_millions > 0)
      continue;

    auto *source = largest_colony(colonies, civilization.id);
    if (!source ||
        source->population_millions <
            design->population_cost_millions + 500.0)
      continue;

    const auto species = require_species(source->population_species_id,
                                         source->id);
    source->population_millions -= design->population_cost_millions;

    if (existing != fleets.end()) {
      existing->embarked_population_millions =
          design->population_cost_millions;
      existing->embarked_population_species_id = species;
      ensure_fleet_combat_state(*existing);
      continue;
    }

    const auto &home = require_home(systems, civilization.home_system_id);
    FleetState fleet;
    fleet.id = next_id;
    next_id = wrapping_increment(next_id);
    fleet.civilization_id = civilization.id;
    fleet.name = civilization.is_player
                     ? "Pioneer One"
                     : civilization.name + " Pioneer";
    fleet.role = FleetRole::Colony;
    fleet.position = {static_cast<float>(home.position.x),
                      static_cast<float>(home.position.y)};
    fleet.current_system_id = home.id;
    fleet.strategic_speed = design->strategic_speed;
    fleet.sensor_range = design->sensor_range;
    fleet.is_active = true;
    fleet.embarked_population_millions = design->population_cost_millions;
    fleet.embarked_population_species_id = species;
    fleet.combat = create_initial_fleet_combat_state(
        design->combat_profile_id, FleetRole::Colony);
    fleets.push_back(std::move(fleet));
  }
}

} // namespace stellar::core
