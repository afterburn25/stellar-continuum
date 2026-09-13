#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/shipyard_state.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
inline constexpr double shipbuilding_industry_per_day = 20.0;

struct ShipbuildingStrategicPreference {
  int civilization_id{};
  std::optional<FleetRole> preferred_new_fleet_role;
  bool defer_new_colonization{};
};

struct ShipbuildingReadView {
  std::span<const Civilization> civilizations;
  std::span<const StellarSystem> systems;
  std::span<const ConstructionState> construction;
  std::span<const ShipyardState> shipyards;
  std::span<const Colony> colonies;
  std::span<const CivilizationEconomy> economies;
  std::span<const FleetState> fleets;
  std::span<const ShipbuildingCapabilities> capabilities;
  std::span<const ShipbuildingStrategicPreference> strategic_preferences;
  ShipDesignReadView designs() const { return {construction, capabilities}; }
};

struct ShipbuildingWorld {
  std::span<const Civilization> civilizations;
  std::span<const StellarSystem> systems;
  std::span<const ConstructionState> construction;
  std::span<ShipyardState> shipyards;
  std::span<Colony> colonies;
  std::span<CivilizationEconomy> economies;
  std::vector<FleetState> &fleets;
  std::span<const ShipbuildingCapabilities> capabilities;
  std::span<const ShipbuildingStrategicPreference> strategic_preferences;
  ShipbuildingReadView read() const {
    return {civilizations, systems,      construction,
            shipyards,     colonies,     economies,
            fleets,        capabilities, strategic_preferences};
  }
};

struct ShipbuildingOrderResult {
  bool accepted{};
  std::string message;
};
struct ShipbuildingCancellationAssessment {
  bool can_cancel{}, is_active{};
  std::optional<std::string> design_id;
  double refund_credits{};
  std::optional<std::string> blocker;
};
struct ShipbuildingCancellationResult {
  bool accepted{};
  std::string message;
  double refunded_credits{};
};
struct ShipbuildingEvent {
  int civilization_id{}, fleet_id{};
  std::string design_id, message;
};

ShipbuildingOrderResult start_ship_build(ShipbuildingWorld world,
                                         int civilization_id,
                                         std::string_view design_id);
ShipbuildingCancellationAssessment
assess_ship_build_cancellation(ShipbuildingReadView world, int civilization_id,
                               std::string_view order_id);
ShipbuildingCancellationResult cancel_ship_build(ShipbuildingWorld world,
                                                 int civilization_id,
                                                 std::string_view order_id);
double shipbuilding_industry_demand(
    ShipbuildingReadView world, int civilization_id,
    double simulation_days = std::numeric_limits<double>::infinity());
void ensure_automatic_ship_orders(ShipbuildingWorld world);
std::vector<ShipbuildingEvent> advance_shipbuilding(
    ShipbuildingWorld world,
    std::optional<std::span<const ConstructionIndustryBudget>> budgets =
        std::nullopt,
    double simulation_days = 1.0);
std::vector<ShipbuildingEvent> advance_shipbuilding_for_civilization(
    ShipbuildingWorld world, int civilization_id, double industry_budget,
    double simulation_days = 1.0);
} // namespace stellar::core
