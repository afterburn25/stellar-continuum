#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/lane_network.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

namespace stellar::core {

enum class InterstellarMissionKind {
  ScoutReconnaissance,
  ScienceSurvey,
  Colony,
  MilitaryDeployment,
  Logistics,
};

struct MissionReachAssessment {
  bool is_supported{};
  bool is_authoritative{};
  std::string reason;
  std::optional<std::vector<int>> route_system_ids;
  double route_distance_light_years{};
  // Present for canonical route assessments; custom/provisional providers may
  // omit it. Includes fuel actually available at arrival after owned services.
  std::optional<double> arrival_fuel_light_years;
};

enum class MissionFuelPolicy { ReachDestination, RetainReturnToService };
struct RefuelingReach {
  int system_id{};
  MissionReachAssessment reach;
};

MissionReachAssessment supported_mission_reach(
    std::string reason = "Mission is within current operational reach.");
MissionReachAssessment unsupported_mission_reach(std::string reason);
MissionReachAssessment provisional_supported_mission_reach(std::string reason);

inline constexpr double kilometres_per_light_year = 9.4607304725808e12;
// A finite light-year value whose kilometre conversion overflows is rejected;
// this avoids the source formatter's checked conversion from infinity to Int32.
std::string format_interstellar_metric_primary(double light_years);
std::string format_interstellar_metric_speed(double light_years_per_day);

struct OperationalReachWorldView {
  std::span<const StellarSystem> systems;
  std::span<const Colony> colonies;
  InterstellarLaneNetwork &lanes;
};

// Scope this to one read-only planning operation. It borrows the current world
// and indexes geometry/refueling once for many destinations. Discard it before
// mutating systems, colonies or lanes; fleet fuel/range is read on each assess.
// No cross-tick cache or save state, and the single-target API uses this same
// authoritative calculation.
class OperationalReachBatch {
public:
  OperationalReachBatch(OperationalReachWorldView world,int civilization_id)
      :world_(world),civilization_id_(civilization_id){}
  // `explain` controls payload materialization only — the is_supported
  // verdict is identical either way. High-volume scan loops that discard
  // the returned assessment should pass false: the reason strings are
  // skipped and feasibility verdicts are memoized per system along the
  // shared route-tree prefix, so sibling candidates reuse the walk.
  MissionReachAssessment assess(const FleetState &,int target_system_id,InterstellarMissionKind,
      MissionFuelPolicy = MissionFuelPolicy::ReachDestination,
      bool explain = true);
  // Scan-loop verdict for a lane-slot candidate — identical
  // is_supported semantics to assess(..., fuel_policy, explain=false)
  // for catalog systems, but the caller resolves the slot (via
  // InterstellarLaneNetwork::slot_of_system) so the probe pays no
  // id-lookup or route-query cost. Contract: target_slot must be a
  // valid lane slot for a catalog system — lane-unknown targets must
  // go through assess so the out_of_range contract is preserved.
  bool probe_supported(const FleetState &,int target_slot,
      MissionFuelPolicy fuel_policy);
  std::optional<RefuelingReach> nearest_refueling(const FleetState &,InterstellarMissionKind);
private:
  void prepare();
  MissionReachAssessment evaluate_route(const FleetState &,std::span<const int> route);
  MissionReachAssessment evaluate_route_verdict(const FleetState &,int target_system_id);
  void ensure_feasibility_state(const FleetState &);
  MissionReachAssessment verdict_for_slot(const FleetState &,int target_slot);
  bool has_return_service_route(const FleetState &);
  OperationalReachWorldView world_;
  int civilization_id_{};
  bool prepared_{};
  std::unordered_map<int,const StellarSystem *> systems_;
  std::unordered_map<int,double> refueling_;
  // Scratch for route materialization across assess calls — keeps the
  // per-candidate polling loop allocation-free. Reused buffers are never
  // exposed: supported results copy into the returned assessment.
  std::vector<int> route_scratch_;
  // Verdict-only feasibility memo for the explain=false assess path,
  // slot-indexed over the lane network's shortest-route tree. Routes from
  // one origin share tree prefixes, so each node's post-arrival fuel is
  // memoized per slot and sibling candidates reuse the walked prefix.
  // Entries replicate evaluate_route's fuel arithmetic in the same order,
  // so verdicts are bit-identical. The whole table resets whenever the
  // fleet inputs (origin, fuel, capacity, leg range) change. state: 0
  // unvisited, 1 feasible, 2 infeasible.
  int feas_origin_{};
  double feas_fuel_{}, feas_capacity_{}, feas_leg_range_{};
  bool feas_key_valid_{};
  std::vector<char> feas_state_;
  std::vector<double> feas_fuel_after_;
  // Slot -> refuel service factor (0 = none) — refueling_ projected into
  // slot space once per batch.
  std::vector<double> feas_factor_;
  std::vector<int> feas_walk_; // ancestor-path scratch
  std::span<const InterstellarLaneNetwork::RouteSlot> feas_slots_;
  // Route tree pinned per fleet-state key — re-acquired when the lane
  // cache revision moves (capacity eviction). Rebuilt trees are
  // deterministic per (origin, range), so the slot memo above survives
  // eviction; only the borrowed spans need re-borrowing.
  InterstellarLaneNetwork::RouteTreeView feas_tree_{};
  std::uint64_t feas_tree_revision_{};
};

MissionReachAssessment
assess_operational_reach(OperationalReachWorldView world, int civilization_id,
                         const FleetState &fleet, int target_system_id,
                         InterstellarMissionKind mission_kind);

void assign_fleet_route(OperationalReachWorldView world, FleetState &fleet,
                        int final_destination_system_id,
                        const MissionReachAssessment &reach);
// Both mutations reject Int32 revision exhaustion before changing fleet state.
void clear_fleet_route(FleetState &fleet);

} // namespace stellar::core
