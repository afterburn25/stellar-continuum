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
  std::optional<RefuelingReach> nearest_refueling(const FleetState &,InterstellarMissionKind);
private:
  void prepare();
  MissionReachAssessment evaluate_route(const FleetState &,std::span<const int> route);
  MissionReachAssessment evaluate_route_verdict(const FleetState &,std::span<const int> route);
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
  // Verdict-only feasibility memo for the explain=false assess path.
  // Routes from one origin share shortest-tree prefixes, so each node's
  // post-arrival fuel is memoized per system id and sibling candidates
  // reuse the walked prefix. Entries replicate evaluate_route's fuel
  // arithmetic in the same order, so verdicts are bit-identical. The
  // whole map resets whenever the fleet inputs (origin, fuel, capacity,
  // leg range) change.
  struct FeasibilityNode {
    double fuel_after{}; // post-arrival fuel incl. refuel top-up (arrival fuel)
    bool feasible{};
  };
  int feas_origin_{};
  double feas_fuel_{}, feas_capacity_{}, feas_leg_range_{};
  bool feas_key_valid_{};
  std::unordered_map<int, FeasibilityNode> feas_memo_;
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
