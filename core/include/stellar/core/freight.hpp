#pragma once

#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_reach.hpp>

#include <functional>
#include <span>
#include <string>

namespace stellar::core {

struct FreightOrderResult {
  bool accepted{};
  std::string message;
};

struct FreightWorldView {
  std::span<const StellarSystem> systems;
  std::span<const Civilization> civilizations;
  std::span<const PlanetaryBody> bodies;
  std::span<const ConstructionState> construction;
  std::span<FleetState> fleets;
  std::span<Colony> colonies;
  std::span<CivilizationEconomy> economies;
  InterstellarLaneNetwork &lanes;

  [[nodiscard]] OperationalReachWorldView reach() const {
    return {systems, colonies, lanes};
  }
};

using FreightReachAssessor = std::function<MissionReachAssessment(
    OperationalReachWorldView, int, const FleetState &, int,
    InterstellarMissionKind)>;

class FreightSimulation {
public:
  explicit FreightSimulation(FreightReachAssessor reach = {});

  [[nodiscard]] FreightOrderResult
  issue_transit_order(FreightWorldView world, int civilization_id, int fleet_id,
                      int target_system_id) const;
  [[nodiscard]] FreightOrderResult
  issue_collection_order(FreightWorldView world, int civilization_id,
                         int fleet_id, int outpost_id) const;
  void advance(FreightWorldView world, double simulation_days = 1.0) const;

  [[nodiscard]] static double
  cargo_transfer_rate_per_day(const FleetState &fleet);
  static constexpr double basic_hub_transfer_capacity_per_day = 4.0;
  [[nodiscard]] static double
  port_transfer_capacity_per_day(const Colony &colony);
  [[nodiscard]] static double
  effective_transfer_rate_per_day(const FleetState &fleet,
                                  const Colony &colony);

private:
  FreightReachAssessor reach_;
};

} // namespace stellar::core
