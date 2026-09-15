#pragma once

#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/combat_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/fleet_power_observation.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

class FleetCombatIntelligenceArgumentError final
    : public std::invalid_argument {
public:
  explicit FleetCombatIntelligenceArgumentError(std::string message);
};

class FleetCombatIntelligenceRangeError final : public std::out_of_range {
public:
  explicit FleetCombatIntelligenceRangeError(std::string message);
};

struct FleetCombatIntelligenceWorldView {
  std::span<const Civilization> civilizations;
  std::span<const FleetState> fleets;
  std::vector<FleetPowerObservation> &observations;
};

inline constexpr int maximum_fleet_power_observations = 4096;
inline constexpr int maximum_fleet_power_observations_per_observer = 2048;

[[nodiscard]] float
massive_combat_per_ship_power(const MassiveCombatLoadout &loadout);
[[nodiscard]] double own_fleet_combat_power(const FleetState &fleet);
[[nodiscard]] std::optional<double> observed_fleet_combat_power(
    std::span<const FleetPowerObservation> observations, int observer_id,
    const FleetState &target);

void observe_fleet_combat_power(FleetCombatIntelligenceWorldView world,
                                int observer_id, const FleetState &target,
                                double day, bool engaged,
                                bool scanning_capability);
void observe_fleet_combat_power_many(
    FleetCombatIntelligenceWorldView world, int observer_id,
    std::span<const FleetState *const> targets, double day, bool engaged,
    bool scanning_capability);
[[nodiscard]] int record_fleet_sensor_contacts(
    FleetCombatIntelligenceWorldView world, int observer_id, double day,
    bool scanning_capability);

[[nodiscard]] bool has_combat_scanner(
    const AdaptiveResearchCivilizationState *research) noexcept;

} // namespace stellar::core
