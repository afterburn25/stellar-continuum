#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/legacy_technology.hpp>

#include <span>
#include <stdexcept>
#include <vector>

namespace stellar::core {

class LegacyCampaignRecoveryDataError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class LegacyCampaignRecoveryOperationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

CivilizationKnowledgeState
create_legacy_initial_knowledge(std::span<const StellarSystem> systems,
                                std::span<const Civilization> civilizations);

std::vector<TechnologyState> create_migrated_legacy_technology_states(
    std::span<const Civilization> civilizations);

std::vector<ConstructionState> create_migrated_legacy_construction_states(
    std::span<const Civilization> civilizations);

// Mutates the supplied fleets and source-colony populations in source order.
// A failed late lookup may therefore leave an already deducted population.
void ensure_legacy_expansion_fleets(
    std::vector<FleetState> &fleets,
    std::span<const StellarSystem> systems,
    std::span<const Civilization> civilizations,
    std::span<Colony> colonies);

} // namespace stellar::core
