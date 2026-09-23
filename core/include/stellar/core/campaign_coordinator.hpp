#pragma once

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/civilian_recovery.hpp>
#include <stellar/core/own_combat_fleet_status.hpp>
#include <stellar/core/strategic_input_support.hpp>
#include <stellar/core/combat_command_runtime.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/freight.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/legacy_research.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/strategic_runtime.hpp>
#include <stellar/engine/phase_timing.hpp>
#include <stellar/engine/simulation_executor.hpp>
#include <array>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace stellar::core {

struct CombatCivilizationOutcomeSummary {
  int civilization_id{};
  double shield_damage_dealt{};
  double armor_damage_dealt{};
  double hull_damage_dealt{};
  double shield_damage_taken{};
  double armor_damage_taken{};
  double hull_damage_taken{};
  int enemy_vessels_destroyed{};
  int own_vessels_lost{};
  int retreats_initiated{};
  int successful_escapes{};
  double embarked_population_casualties_inflicted_millions{};
  double embarked_population_casualties_suffered_millions{};

  [[nodiscard]] double total_damage_dealt() const noexcept;
  [[nodiscard]] double total_damage_taken() const noexcept;
};

struct CombatSystemOutcomeSummary {
  std::optional<int> system_id;
  int event_count{};
  int engagements_started{};
  int engagements_ended{};
  int damage_events{};
  int vessels_destroyed{};
  int retreats_initiated{};
  int successful_escapes{};
  double total_damage_applied{};
  double embarked_population_casualties_millions{};
  std::vector<CombatCivilizationOutcomeSummary> civilizations;
};

struct CombatOutcomeSummary {
  int event_count{};
  int engagements_started{};
  int engagements_ended{};
  int damage_events{};
  int vessels_destroyed{};
  int retreats_initiated{};
  int successful_escapes{};
  double total_damage_applied{};
  double embarked_population_casualties_millions{};
  std::vector<CombatCivilizationOutcomeSummary> civilizations;
  std::vector<CombatSystemOutcomeSummary> systems;
};

[[nodiscard]] CombatOutcomeSummary
summarize_combat_outcome(std::span<const CombatEvent> events);

struct SimulationStepResult {
  double simulation_days{};
  std::vector<CivilizationIndustryAllocation> industry_allocations;
  std::vector<ConstructionEvent> construction_events;
  std::vector<ShipbuildingEvent> shipbuilding_events;
  std::vector<LegacyResearchEvent> research_events;
  std::vector<ExplorationEvent> exploration_events;
  std::vector<CombatEvent> combat_events;
  std::vector<ColonizationEvent> colonization_events;

  [[nodiscard]] CombatOutcomeSummary combat_outcome() const;
};

// Owns the mutable fresh-campaign world and the lane graph derived from its
// astronomy. lanes() rebuilds the retained graph if the system geometry changes.
class CampaignSimulationState {
public:
  explicit CampaignSimulationState(FreshCampaignState campaign);

  [[nodiscard]] FreshCampaignState &campaign() noexcept;
  [[nodiscard]] const FreshCampaignState &campaign() const noexcept;
  [[nodiscard]] InterstellarLaneNetwork &lanes();
  [[nodiscard]] std::size_t cached_lane_route_tree_count();

private:
  FreshCampaignState campaign_;
  std::optional<InterstellarLaneNetwork> lanes_;
  std::vector<StellarSystem> lane_astronomy_;
};

using CampaignConstructionCapabilityQuery = std::function<bool(
    std::span<const TechnologyState>, int, std::string_view)>;
using CampaignShipbuildingCapabilityQuery = std::function<bool(
    std::span<const TechnologyState>, int, std::string_view)>;

// The empty queries select the source legacy/prototype capability adapters.
// Adaptive Research must supply its own two matched authority queries.
struct SourceCompatibleCampaignConfiguration {
  bool advance_legacy_research{true};
  bool use_strategic_shipbuilding_preferences{true};
  CampaignConstructionCapabilityQuery construction_capability;
  CampaignShipbuildingCapabilityQuery shipbuilding_capability;
};

// Concrete subsystem instances preserve their own supported dependency
// injection without turning coordinator phases into replaceable callbacks.
struct CampaignSubsystemRuntime {
  LegacyResearchSimulation research{};
  ExplorationSimulation exploration{ExplorationReachAssessment{},MissionFuelPolicy::RetainReturnToService};
  FreightSimulation freight{FreightReachAssessor{}};
  ColonizationSimulation colonization{SettlementReachAssessment{}};
};

class GalaxySimulationStepCoordinator {
public:
  inline static constexpr std::array<std::string_view,12> phase_names{
    "economy","strategic_ai","automatic_orders","industry_allocation","construction","shipbuilding",
    "legacy_research","exploration","freight","combat","colonization","economy_storage"};
  void set_profiling_enabled(bool enabled) noexcept {profiling_enabled_=enabled;}
  void reset_performance_counters() noexcept {performance_={};}
  [[nodiscard]] const auto &performance_counters()const noexcept {return performance_;}
  explicit GalaxySimulationStepCoordinator(
      SourceCompatibleCampaignConfiguration configuration = {});
  GalaxySimulationStepCoordinator(
      SourceCompatibleCampaignConfiguration configuration,
      CombatCommandRuntime matched_combat,
      CampaignSubsystemRuntime subsystems = {});
  GalaxySimulationStepCoordinator(
      SourceCompatibleCampaignConfiguration configuration,
      CombatSimulation raw_combat,
      CampaignSubsystemRuntime subsystems = {});
  GalaxySimulationStepCoordinator(
      SourceCompatibleCampaignConfiguration configuration,
      CivilizationStrategicRuntimeCoordinator strategic,
      CombatCommandRuntime matched_combat,
      CampaignSubsystemRuntime subsystems = {});
  GalaxySimulationStepCoordinator(
      SourceCompatibleCampaignConfiguration configuration,
      CivilizationStrategicRuntimeCoordinator strategic,
      CombatSimulation raw_combat,
      CampaignSubsystemRuntime subsystems = {});
  // Executor phase tasks capture `this`, so a moved coordinator rebinds
  // them rather than carrying callbacks that still target the
  // moved-from object.
  GalaxySimulationStepCoordinator(GalaxySimulationStepCoordinator &&other);
  GalaxySimulationStepCoordinator &
  operator=(GalaxySimulationStepCoordinator &&other);
  GalaxySimulationStepCoordinator(const GalaxySimulationStepCoordinator &) = delete;
  GalaxySimulationStepCoordinator &
  operator=(const GalaxySimulationStepCoordinator &) = delete;

[[nodiscard]] CombatOrderResult issue_military_order(
    CampaignSimulationState *campaign, int civilization_id, int fleet_id,
    const MilitaryOrder &order);
[[nodiscard]] CombatBatchOrderResult issue_military_orders(
    CampaignSimulationState *campaign, int civilization_id,
    std::span<const int> fleet_ids, const MilitaryOrder &order);
[[nodiscard]] CombatOrderResult issue_engage_hostiles_order(
    CampaignSimulationState *campaign, int civilization_id, int fleet_id);
[[nodiscard]] CombatOrderResult issue_military_deployment_order(
    CampaignSimulationState *campaign, int civilization_id, int fleet_id,
    int destination_system_id);
[[nodiscard]] CombatOrderPreview preview_military_order(
    CampaignSimulationState *campaign, int civilization_id, int fleet_id,
    const MilitaryOrder &order) const;
[[nodiscard]] CombatBatchOrderPreview preview_military_orders(
    CampaignSimulationState *campaign, int civilization_id,
    std::span<const int> fleet_ids, const MilitaryOrder &order) const;
[[nodiscard]] MilitaryForceSummary get_own_military_force_summary(
    CampaignSimulationState *campaign, int civilization_id);
[[nodiscard]] CombatReadinessSummary get_own_combat_readiness_summary(
    CampaignSimulationState *campaign, int civilization_id) const;
[[nodiscard]] OwnCombatFleetStatusView get_own_combat_fleet_status(
    CampaignSimulationState *campaign, int civilization_id) const;

[[nodiscard]] ColonizationOpportunityPlan get_colony_opportunity_plan(
    CampaignSimulationState *campaign, int fleet_id,
    int maximum_candidates =
        ColonizationOpportunityPlanner::default_maximum_candidates) const;
[[nodiscard]] ResourceOutpostOpportunityPlan
get_resource_outpost_opportunity_plan(
    CampaignSimulationState *campaign, int fleet_id,
    int maximum_candidates =
        ResourceOutpostOpportunityPlanner::default_maximum_candidates) const;
[[nodiscard]] ResourceOutpostOrderAssessment
assess_resource_outpost_fleet_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int destination_system_id, int planetary_body_id) const;
[[nodiscard]] ColonizationOrderAssessment assess_colony_fleet_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int destination_system_id, int planetary_body_id) const;
[[nodiscard]] ColonyOrderResult issue_resource_outpost_fleet_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int destination_system_id, int planetary_body_id) const;
[[nodiscard]] ColonyOrderResult issue_colony_fleet_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int destination_system_id, int planetary_body_id) const;

[[nodiscard]] FreightOrderResult issue_freight_transit_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int target_system_id) const;
[[nodiscard]] FreightOrderResult issue_freight_collection_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, int outpost_id) const;
[[nodiscard]] CivilianFleetHoldOrderResult issue_civilian_hold_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id) const;
[[nodiscard]] CivilianFleetHoldOrderResult issue_civilian_resume_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id) const;
[[nodiscard]] CivilianFleetReturnOrderResult
issue_civilian_return_to_base_order(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id, bool confirm_abandon_colony_work = false) const;
[[nodiscard]] CivilianFleetReturnOrderResult preview_civilian_return_to_base(
    CampaignSimulationState *campaign, int acting_civilization_id,
    int fleet_id) const;
  [[nodiscard]] SimulationStepResult
  advance(CampaignSimulationState *campaign, double simulation_days);

  // The engine executor driving the 12 phase tasks — exposed for
  // diagnostics (domain stats, tick history, tier counts). Phase cadence
  // policy is coordinator-owned; callers may inspect but not mutate.
  [[nodiscard]] const stellar::engine::SimulationExecutor &
  executor() const noexcept { return executor_; }

  [[nodiscard]] bool has_matched_combat_runtime() const noexcept;
  [[nodiscard]] CivilizationStrategicRuntimeCoordinator &strategic_runtime()
      noexcept;
  [[nodiscard]] const CivilizationStrategicRuntimeCoordinator &
  strategic_runtime() const noexcept;
  [[nodiscard]] CombatSimulation &combat_simulation() noexcept;
  [[nodiscard]] const CombatSimulation &combat_simulation() const noexcept;

private:
  // Engine-level phase pipeline: every strategic step runs the 12
  // coordinator phases as SimulationExecutor tasks (Active tier,
  // dependency-chained to the historical order). This is the Core
  // consumer of the engine simulation LOD machinery — per-phase domain
  // statistics, wakeups and future tier demotion without restructuring
  // advance(). Per-step inputs flow through StepPhaseContext; task
  // callbacks never capture stack state.
  struct StepPhaseContext {
    CampaignSimulationState *state{};
    double simulation_days{};
    SimulationStepResult *result{};
    std::vector<IndustryReserve> existing_reserves;
    std::vector<EconomyConstructionState> economic_construction;
    std::vector<EconomyFleetState> economic_fleets;
    std::vector<ConstructionIndustryBudget> construction_budgets;
    std::vector<ConstructionIndustryBudget> shipbuilding_budgets;
  };
  void configure_phase_tasks();
  bool profiling_enabled_{};
  std::array<stellar::engine::PerformanceCounter,phase_names.size()> performance_{};
  StepPhaseContext step_{};
  stellar::engine::SimulationExecutor executor_;
  bool advance_legacy_research_{};
  bool use_strategic_shipbuilding_preferences_{};
  std::shared_ptr<CampaignConstructionCapabilityQuery>
      construction_capability_;
  std::shared_ptr<CampaignShipbuildingCapabilityQuery>
      shipbuilding_capability_;
  CivilizationStrategicRuntimeCoordinator strategic_;
  std::variant<CombatCommandRuntime, CombatSimulation> combat_;
  CampaignSubsystemRuntime subsystems_;
};

} // namespace stellar::core
