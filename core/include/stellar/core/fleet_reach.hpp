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
  MissionReachAssessment assess(const FleetState &,int target_system_id,InterstellarMissionKind,
      MissionFuelPolicy = MissionFuelPolicy::ReachDestination);
  std::optional<RefuelingReach> nearest_refueling(const FleetState &,InterstellarMissionKind);
private:
  void prepare();
  MissionReachAssessment evaluate_route(const FleetState &,std::vector<int> route);
  bool has_return_service_route(const FleetState &);
  OperationalReachWorldView world_;
  int civilization_id_{};
  bool prepared_{};
  std::unordered_map<int,const StellarSystem *> systems_;
  std::unordered_map<int,double> refueling_;
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
