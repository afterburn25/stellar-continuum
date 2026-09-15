#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/fleet_state.hpp>

#include <optional>
#include <span>
#include <vector>

namespace stellar::core {
std::vector<FleetState>
seed_fleets(std::span<const StellarSystem> systems,
            std::span<const Civilization> civilizations,
            std::optional<std::span<Colony>> population_sources = std::nullopt);

void ensure_starter_fleets(std::span<const StellarSystem> systems,
                           std::span<const Civilization> civilizations,
                           std::span<Colony> population_sources,
                           std::vector<FleetState> &fleets,
                           int civilization_id);
} // namespace stellar::core
