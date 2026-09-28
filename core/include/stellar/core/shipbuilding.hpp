#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/shipyard_state.hpp>
#include <stellar/core/civilization_control.hpp>

#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
inline constexpr double shipbuilding_industry_per_day = 20.0;
inline constexpr double minimum_retained_colony_population_millions = 500.0;

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
  std::function<bool(int, std::string_view)> capability_query;
  std::function<ShipbuildingStrategicPreference(int)> preference_query;
  ShipDesignReadView designs() const {
    return {construction, capabilities, capability_query};
  }
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
  std::function<bool(int, std::string_view)> capability_query;
  std::function<ShipbuildingStrategicPreference(int)> preference_query;
  CivilizationControlQuery control;
  ShipbuildingReadView read() const {
    return {civilizations, systems,      construction,
            shipyards,     colonies,     economies,
            fleets,        capabilities, strategic_preferences,
            capability_query, preference_query};
  }
};

struct ShipbuildingOrderResult {
  bool accepted{};
  std::string message;
};
struct ShipbuildingStartAssessment {
  bool can_start{}, will_queue{};
  std::optional<std::string> blocker;
  std::optional<std::string> design_id;
  std::optional<std::string> design_name;
  double industry_cost{}, credit_cost{}, population_cost_millions{};
  double minimum_source_population_millions{};
  int pending_build_count{}, maximum_pending_builds{};
  std::optional<std::string> prepared_order_id;
  std::optional<int> population_source_colony_id;
  std::optional<std::string> population_species_id;
  std::optional<double> population_source_current_millions;
};
struct ShipbuildingCancellationAssessment {
  bool can_cancel{}, is_active{};
  std::optional<std::string> design_id;
  double refund_credits{};
  std::optional<std::string> blocker;
};
struct ShipbuildingBatchAssessment {
  int quantity{};
  bool can_start{};
  std::optional<std::string> blocker;
  double credit_cost{}, industry_cost{}, population_cost_millions{};
  double minimum_build_days_at_full_shipyard_rate{};
};
// Quotes each quantity from 1 through the existing queue capacity in one
// staged admission pass. Costs and population reservations use canonical rules.
std::vector<ShipbuildingBatchAssessment> assess_ship_build_batches(
    ShipbuildingReadView world, int civilization_id, std::string_view design_id);
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
// Batch admission is atomic: all normal authorization/population rules are
// evaluated before any live treasury, colony or queue is changed.
ShipbuildingOrderResult start_ship_build_batch(ShipbuildingWorld world,
    int civilization_id,std::string_view design_id,int quantity);
ShipbuildingOrderResult move_queued_ship_build(ShipbuildingWorld world,
    int civilization_id,std::string_view order_id,int direction);
ShipbuildingStartAssessment assess_start_ship_build(
    ShipbuildingReadView world, int civilization_id,
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
