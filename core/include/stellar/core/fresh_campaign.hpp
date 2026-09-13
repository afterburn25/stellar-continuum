#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_seeding.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/legacy_technology.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/shipyard_state.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {

struct FreshCampaignState {
  std::int64_t seed{};
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<TechnologyState> technologies;
  std::vector<ConstructionState> construction;
  std::vector<ShipyardState> shipyards;
  int player_civilization_id{};
  CivilizationKnowledgeState knowledge;
  std::optional<GalacticCore> core;
  bool used_constrained_home_fallback{};
};

FreshCampaignState
seed_fresh_campaign(std::int64_t seed, std::span<const CatalogStar> catalog,
                    int system_count = 500, int pre_warp_count = 6,
                    int ancient_count = 1,
                    const std::string &player_species_id = "terran_baseline");

} // namespace stellar::core
