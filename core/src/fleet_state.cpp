#include <stellar/core/fleet_state.hpp>

namespace stellar::core {
std::vector<EconomyFleetState>
economic_fleet_projection(std::span<const FleetState> fleets) {
  std::vector<EconomyFleetState> projection;
  projection.reserve(fleets.size());
  for (const auto &fleet : fleets)
    projection.push_back({fleet.civilization_id,
                          static_cast<EconomyFleetRole>(fleet.role),
                          fleet.is_active});
  return projection;
}
} // namespace stellar::core
