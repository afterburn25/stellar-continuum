#pragma once

#include <stellar/core/fleet_combat_intelligence.hpp>
#include <map>

namespace stellar::core::detail {

// Internal source-compatible overload. The campaign service validates its
// retained index against current fleet identities before supplying this view.
// Neither targets nor the index is retained by the intelligence operation.
void observe_fleet_combat_power_many_indexed(
    FleetCombatIntelligenceWorldView world, int observer_id,
    std::span<const FleetState *const> targets, double day, bool engaged,
    bool scanning_capability, const std::map<int, const FleetState *> &members);

} // namespace stellar::core::detail
