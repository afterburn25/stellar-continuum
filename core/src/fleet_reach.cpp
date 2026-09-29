#include <stellar/core/fleet_reach.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/fleet_transit.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}

bool blank_reason(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

std::string group_integer(std::string value) {
  for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(value.size()) - 3;
       index > 0; index -= 3)
    value.insert(static_cast<std::size_t>(index), ",");
  return value;
}

std::string superscript(int value) {
  static constexpr const char *digits[] = {"⁰", "¹", "²", "³", "⁴",
                                           "⁵", "⁶", "⁷", "⁸", "⁹"};
  if (value == 0)
    return digits[0];
  std::string result;
  std::int64_t magnitude = value;
  if (magnitude < 0) {
    result = "⁻";
    magnitude = -magnitude;
  }
  const auto ordinary = std::to_string(magnitude);
  for (const auto digit : ordinary)
    result += digits[digit - '0'];
  return result;
}

std::string scientific(double value) {
  if (!std::isfinite(value))
    throw std::invalid_argument(
        "Metric conversion exceeds the native formatting range.");
  if (value < 1'000'000.0) {
    // Invariant N0 uses midpoint-to-even rounding. This branch is bounded below
    // one million, so the integer conversion is representable.
    const auto integral = std::floor(value);
    const auto fraction = value - integral;
    auto rounded = static_cast<std::uint64_t>(integral);
    if (fraction > 0.5 || (fraction == 0.5 && rounded % 2 != 0))
      ++rounded;
    if (rounded == 0 && std::signbit(value))
      return "-0";
    return group_integer(std::to_string(rounded));
  }
  const auto exponent_value = std::floor(std::log10(value));
  if (!std::isfinite(exponent_value) ||
      exponent_value < std::numeric_limits<int>::min() ||
      exponent_value > std::numeric_limits<int>::max())
    throw std::invalid_argument(
        "Metric conversion exceeds the native formatting range.");
  const auto exponent = static_cast<int>(exponent_value);
  const auto coefficient = value / std::pow(10.0, exponent);
  return detail::legacy_custom_fixed(coefficient, 0, 3) + " × 10" +
         superscript(exponent);
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 int id) {
  const auto found =
      std::find_if(systems.begin(), systems.end(),
                   [id](const auto &system) { return system.id == id; });
  return found == systems.end() ? nullptr : &*found;
}

void ensure_revision_available(const FleetState &fleet) {
  if (fleet.mission_order_revision == std::numeric_limits<int>::max())
    throw std::overflow_error("Fleet mission revision space is exhausted.");
}

} // namespace

MissionReachAssessment supported_mission_reach(std::string reason) {
  return {true, true, std::move(reason), std::nullopt, 0.0};
}

MissionReachAssessment unsupported_mission_reach(std::string reason) {
  if (blank_reason(reason))
    reason = "Mission is beyond current operational reach.";
  return {false, true, std::move(reason), std::nullopt, 0.0};
}

MissionReachAssessment provisional_supported_mission_reach(std::string reason) {
  return {true, false, std::move(reason), std::nullopt, 0.0};
}

std::string format_interstellar_metric_primary(double light_years) {
  if (!std::isfinite(light_years) || light_years < 0.0)
    return "Distance unconfirmed";
  return scientific(light_years * kilometres_per_light_year) + " km · " +
         detail::legacy_custom_fixed(light_years, 0, 1) + " ly";
}

std::string format_interstellar_metric_speed(double light_years_per_day) {
  if (!std::isfinite(light_years_per_day) || light_years_per_day < 0.0)
    return "Speed unconfirmed";
  return scientific(light_years_per_day * kilometres_per_light_year) +
         " km/day · " + detail::legacy_custom_fixed(light_years_per_day, 0, 1) +
         " ly/day";
}

void OperationalReachBatch::prepare() {
  if(prepared_)return;
  std::unordered_map<int, double> refueling;
  for (const auto &colony : world_.colonies) {
    if (colony.civilization_id != civilization_id_)
      continue;
    auto &service = refueling[colony.system_id];
    service =
        std::max(service, colony.kind == SettlementKind::Colony ? 1.0 : 0.5);
  }
  refueling_=std::move(refueling);prepared_=true;
}

const std::unordered_map<int, const StellarSystem *> &
OperationalReachBatch::systems_map() {
  if (!systems_built_) {
    std::unordered_map<int, const StellarSystem *> systems;
    systems.reserve(world_.systems.size());
    for (const auto &system : world_.systems)
      if (!systems.emplace(system.id, &system).second)
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: " +
            std::to_string(system.id));
    systems_ = std::move(systems);
    systems_built_ = true;
  }
  return systems_;
}

MissionReachAssessment OperationalReachBatch::assess(const FleetState &fleet,
    int target_system_id,InterstellarMissionKind /*mission_kind*/,MissionFuelPolicy fuel_policy,
    bool explain) {
  if (fleet.civilization_id != civilization_id_)
    return unsupported_mission_reach(
        "That fleet is not controlled by this civilization.");
  // Lane membership is built over the catalog (every catalog system is a
  // route slot), so the slot map answers this gate without a linear scan
  // while the id map stays unmaterialized.
  if (systems_built_?!systems_.contains(target_system_id):world_.lanes.slot_of_system(target_system_id)<0)
    return unsupported_mission_reach("Unknown mission target.");
  if (!fleet.current_system_id)
    return unsupported_mission_reach(
        "The fleet must finish its current lane leg before receiving a new "
        "interstellar route.");

  MissionReachAssessment reach;
  if (explain) {
    world_.lanes.find_shortest_route_into(
        *fleet.current_system_id, target_system_id,
        fleet.maximum_leg_range_light_years, route_scratch_);
    if (route_scratch_.empty())
      return unsupported_mission_reach(
          "No connected lane route is available within this fleet's " +
          format_interstellar_metric_primary(
              fleet.maximum_leg_range_light_years) +
          " maximum leg range.");
    reach = evaluate_route(fleet, route_scratch_);
  } else {
    reach = evaluate_route_verdict(fleet, target_system_id);
  }
  if(reach.is_supported&&fuel_policy==MissionFuelPolicy::RetainReturnToService){
    auto projected=fleet;
    projected.current_system_id=target_system_id;
    projected.fuel_remaining_light_years=*reach.arrival_fuel_light_years;
    if(!has_return_service_route(projected))
      return explain
                 ? unsupported_mission_reach("The outward route is reachable, but would leave insufficient fuel to reach any owned refuelling settlement.")
                 : MissionReachAssessment{false, true, {}, std::nullopt, 0.0};
  }
  return reach;
}

// Verdict-only evaluation for the scan loops: identical is_supported
// semantics to evaluate_route, but no reason/route materialization and no
// per-candidate route query — feasibility walks the lane network's cached
// slot-indexed route tree directly and memoizes each node's post-arrival
// fuel per slot. Since every candidate route is a path in the same tree,
// sibling targets share ancestors: a drain visits each tree node once
// instead of re-walking every route. The fuel arithmetic below is applied
// in the same order as evaluate_route (leg compare against running fuel,
// subtract, then refuel top-up), so boundary verdicts are bit-identical.
MissionReachAssessment OperationalReachBatch::evaluate_route_verdict(
    const FleetState &fleet, int target_system_id) {
  prepare();
  const int origin = *fleet.current_system_id;
  const double range = fleet.maximum_leg_range_light_years;
  // find_shortest_route_into answers unreachable before any id
  // validation — mirror that so a lane-unknown target under an invalid
  // range reports unsupported instead of throwing.
  if (range <= 0.0 || std::isnan(range))
    return {false, true, {}, std::nullopt, 0.0};
  if (feas_factor_.empty()) {
    const auto slots = world_.lanes.route_slots();
    feas_slots_ = slots;
    feas_factor_.assign(slots.size(), 0.0);
    for (const auto &[id, factor] : refueling_) {
      const int slot = world_.lanes.slot_of_system(id);
      if (slot >= 0)
        feas_factor_[slot] = factor;
    }
  }
  // Throws out_of_range on a lane-unknown origin, same as the
  // find_shortest_route_into call the explain path performs. Acquired
  // before the memo seed below so slot_of_system(origin) is guaranteed
  // non-negative there.
  ensure_feasibility_state(fleet);
  const int target_slot = world_.lanes.slot_of_system(target_system_id);
  if (target_slot < 0) {
    // Preserve the out_of_range contract for lane-unknown targets.
    world_.lanes.find_shortest_route_into(origin, target_system_id, range,
                                          route_scratch_);
    return {false, true, {}, std::nullopt, 0.0};
  }
  return verdict_for_slot(fleet, target_slot);
}

// Ensures the pinned route tree and slot-indexed memo match the fleet's
// (origin, fuel, capacity, leg range) state. The tree view is re-borrowed
// whenever the lane cache revision moves — inserts keep node storage
// stable, so only capacity eviction (which bumps the revision) can
// invalidate the spans. Rebuilt trees are deterministic per key, so the
// memo arrays remain valid across eviction; only the spans refresh.
void OperationalReachBatch::ensure_feasibility_state(
    const FleetState &fleet) {
  const int origin = *fleet.current_system_id;
  const double range = fleet.maximum_leg_range_light_years;
  if (!feas_key_valid_ || feas_origin_ != origin ||
      feas_fuel_ != fleet.fuel_remaining_light_years ||
      feas_capacity_ != fleet.fuel_capacity_light_years ||
      feas_leg_range_ != range) {
    feas_tree_ =
        world_.lanes.route_tree_view(origin, range);
    feas_tree_revision_ = world_.lanes.routes_cache_revision();
    feas_origin_ = origin;
    feas_fuel_ = fleet.fuel_remaining_light_years;
    feas_capacity_ = fleet.fuel_capacity_light_years;
    feas_leg_range_ = range;
    feas_key_valid_ = true;
    feas_state_.assign(feas_slots_.size(), 0);
    feas_fuel_after_.assign(feas_slots_.size(), 0.0);
    feas_dist_.assign(feas_slots_.size(), 0.0);
    // The origin node carries evaluate_route's own top-up: the running
    // fuel after origin refuel, in identical expression order.
    const int origin_slot = world_.lanes.slot_of_system(origin);
    auto fuel = feas_fuel_;
    if (const double factor = feas_factor_[origin_slot]; factor != 0.0)
      fuel = feas_capacity_ * factor;
    feas_state_[origin_slot] = 1;
    feas_fuel_after_[origin_slot] = fuel;
    return;
  }
  if (world_.lanes.routes_cache_revision() != feas_tree_revision_) {
    feas_tree_ =
        world_.lanes.route_tree_view(origin, range);
    feas_tree_revision_ = world_.lanes.routes_cache_revision();
  }
}

MissionReachAssessment OperationalReachBatch::verdict_for_slot(
    const FleetState &, int target_slot) {
  const auto &tree = feas_tree_;
  if (!std::isfinite(tree.distance[target_slot]))
    return {false, true, {}, std::nullopt, 0.0};
  feas_walk_.clear();
  int slot = target_slot;
  while (feas_state_[slot] == 0) {
    feas_walk_.push_back(slot);
    slot = tree.prior[slot];
  }
  bool feasible = feas_state_[slot] == 1;
  double fuel = feas_fuel_after_[slot];
  for (auto it = feas_walk_.rbegin(); it != feas_walk_.rend(); ++it) {
    const int s = *it;
    const int p = tree.prior[s];
    if (!feasible) {
      feas_state_[s] = 2;
      continue;
    }
    const double leg = distance_light_years(feas_slots_[p].position,
                                            feas_slots_[s].position);
    if (leg > fuel + 1e-9) {
      feas_state_[s] = 2;
      feasible = false;
      continue;
    }
    fuel -= leg;
    // Accumulate parent-first in the same association order as
    // evaluate_route's `distance += leg` walk, so route distances —
    // including nearest_refueling's exact-equality tie-break — are
    // bit-identical.
    feas_dist_[s] = feas_dist_[p] + leg;
    if (const double factor = feas_factor_[s]; factor != 0.0)
      fuel = feas_capacity_ * factor;
    feas_fuel_after_[s] = fuel;
    feas_state_[s] = 1;
  }
  if (feas_state_[target_slot] != 1)
    return {false, true, {}, std::nullopt, 0.0};
  return {true, true, {}, std::nullopt, feas_dist_[target_slot],
          feas_fuel_after_[target_slot]};
}

bool OperationalReachBatch::probe_supported(
    const FleetState &fleet, int target_slot,
    MissionFuelPolicy fuel_policy) {
  if (fleet.civilization_id != civilization_id_ || !fleet.current_system_id)
    return false;
  prepare();
  const double range = fleet.maximum_leg_range_light_years;
  if (range <= 0.0 || std::isnan(range))
    return false;
  if (feas_factor_.empty()) {
    const auto slots = world_.lanes.route_slots();
    feas_slots_ = slots;
    feas_factor_.assign(slots.size(), 0.0);
    for (const auto &[id, factor] : refueling_) {
      const int slot = world_.lanes.slot_of_system(id);
      if (slot >= 0)
        feas_factor_[slot] = factor;
    }
  }
  // Same unknown-origin throw contract as the assess path.
  ensure_feasibility_state(fleet);
  const auto reach = verdict_for_slot(fleet, target_slot);
  if (!reach.is_supported)
    return false;
  if (fuel_policy == MissionFuelPolicy::RetainReturnToService) {
    auto projected = fleet;
    projected.current_system_id = feas_slots_[target_slot].id;
    projected.fuel_remaining_light_years =
        *reach.arrival_fuel_light_years;
    return has_return_service_route(projected);
  }
  return true;
}

MissionReachAssessment OperationalReachBatch::evaluate_route(
    const FleetState &fleet,std::span<const int> route) {
  prepare();
  auto fuel = fleet.fuel_remaining_light_years;
  if (const auto service = refueling_.find(*fleet.current_system_id);
      service != refueling_.end())
    fuel = fleet.fuel_capacity_light_years * service->second;

  double distance = 0.0;
  // Leg positions resolve through the lane slot table — the id->system
  // map build is no longer paid by explain callers (the duplicate-id
  // invalid_argument now surfaces only on the name-lookup failure path).
  const auto route_slots = world_.lanes.route_slots();
  int prev_slot =
      route.empty() ? -1 : world_.lanes.slot_of_system(route.front());
  for (std::size_t index = 1; index < route.size(); ++index) {
    const int cur_slot = world_.lanes.slot_of_system(route[index]);
    if (prev_slot < 0 || cur_slot < 0) {
      // Lane-derived routes never land here — preserves the id-map
      // .at() out_of_range contract for a foreign id.
      (void)systems_map().at(route[index - 1]);
      (void)systems_map().at(route[index]);
    }
    const auto leg = distance_light_years(route_slots[prev_slot].position,
                                          route_slots[cur_slot].position);
    if (leg > fuel + 1e-9) {
      const auto *second = find_system(world_.systems, route[index]);
      return unsupported_mission_reach(
          "Insufficient fuel endurance for the lane into " + second->name +
          ": " + format_interstellar_metric_primary(leg) + " required, " +
          format_interstellar_metric_primary(fuel) +
          " available before refueling.");
    }
    fuel -= leg;
    distance += leg;
    if (const auto service = refueling_.find(route[index]);
        service != refueling_.end())
      fuel = fleet.fuel_capacity_light_years * service->second;
    prev_slot = cur_slot;
  }

  const auto legs = static_cast<int>(route.size() - 1);
  auto reason = legs == 0
                    ? std::string("The fleet is already in the target system.")
                    : "Route: " + std::to_string(legs) + " lane leg" +
                          (legs == 1 ? "" : "s") + ", " +
                          format_interstellar_metric_primary(distance) +
                          " total; maximum leg " +
                          format_interstellar_metric_primary(
                              fleet.maximum_leg_range_light_years) +
                          "; projected fuel reserve " +
                          format_interstellar_metric_primary(fuel) + ".";
  // The span is materialized only into supported results — the caller's
  // scratch buffer stays reusable for the next probe.
  return {true, true, std::move(reason),
          std::vector<int>(route.begin(), route.end()), distance, fuel};
}

bool OperationalReachBatch::has_return_service_route(const FleetState &fleet) {
  if(refueling_.contains(*fleet.current_system_id))return true;
  const double range=fleet.maximum_leg_range_light_years;
  // find_shortest_route_into answers unreachable before validating ids, so
  // an invalid range yielded an empty route per service — no throw, no
  // route. Mirror that up front.
  if(range<=0.0||std::isnan(range))return false;
  // The old loop never touched the lane network on an empty set.
  if(refueling_.empty())return false;
  const auto slots=world_.lanes.route_slots();
  const int target_slot=world_.lanes.slot_of_system(*fleet.current_system_id);
  for(const auto &[id,ignored]:refueling_){
    if(target_slot<0){
      // Preserve the destination-validation throw (first iterated
      // service, same as the materialized-route path).
      world_.lanes.find_shortest_route_into(id,*fleet.current_system_id,range,route_scratch_);
    }
    // Lanes are undirected. Borrow the service-rooted tree and walk the
    // target's predecessor chain in slot space — the same (leg compare,
    // subtract, refuel top-up) sequence evaluate_route applies to the
    // reversed route, without materializing it.
    const auto tree=world_.lanes.route_tree_view(id,range);
    if(tree.distance.empty()||!std::isfinite(tree.distance[target_slot]))continue;
    // The origin top-up targets the fleet's current system, which the
    // early return above guarantees is not a refueling site.
    double fuel=fleet.fuel_remaining_light_years;
    const int root=world_.lanes.slot_of_system(id);
    bool supported=true;
    for(int s=target_slot;s!=root;){
      const int p=tree.prior[s];
      const double leg=distance_light_years(slots[p].position,slots[s].position);
      if(leg>fuel+1e-9){supported=false;break;}
      fuel-=leg;
      if(const auto it=refueling_.find(slots[p].id);it!=refueling_.end())
        fuel=fleet.fuel_capacity_light_years*it->second;
      s=p;
    }
    if(supported)return true;
  }
  return false;
}

std::optional<RefuelingReach> OperationalReachBatch::nearest_refueling(
    const FleetState &fleet,InterstellarMissionKind kind) {
  const auto verdict = nearest_refueling_verdict(fleet);
  if (!verdict)
    return std::nullopt;
  // The winner alone pays the explain path so the returned reach
  // carries the same route and reason payload as a full scan.
  return RefuelingReach{verdict->system_id,
                        assess(fleet, verdict->system_id, kind)};
}

std::optional<RefuelingReach> OperationalReachBatch::nearest_refueling_verdict(
    const FleetState &fleet) {
  if(fleet.civilization_id!=civilization_id_||!fleet.current_system_id)return std::nullopt;
  prepare();
  // Gather the lane-indexed refueling slots first — mirror assess's
  // "Unknown mission target" gate so a non-catalog colony site skips
  // instead of throwing. Lane membership indexes the whole catalog, so
  // the slot lookup is the same test without a linear scan while the id
  // map stays lazy.
  return_needed_.clear();
  for(const auto &[id,ignored]:refueling_){
    if (systems_built_?!systems_.contains(id):world_.lanes.slot_of_system(id)<0)
      continue;
    const int slot=world_.lanes.slot_of_system(id);
    if(slot<0)continue;  // defensive: catalog ids always carry a slot
    return_needed_.push_back(slot);
  }
  if(return_needed_.empty())return std::nullopt;
  const double range=fleet.maximum_leg_range_light_years;
  if(range<=0.0||std::isnan(range))return std::nullopt;
  // One early-stopped tree rooted at the fleet settles needed sites in
  // (distance, id) order and stops at the first fuel-feasible one — that
  // is exactly the min-(distance, id) supported pick the full scan made,
  // without a full-catalog Dijkstra or a cached-tree eviction cycle per
  // hop. Throws on an unknown origin exactly where
  // evaluate_route_verdict's tree acquisition did.
  const auto slots=world_.lanes.route_slots();
  const int origin_slot=world_.lanes.slot_of_system(*fleet.current_system_id);
  if(origin_slot>=0){
    // Prune sites outside the origin's component — they can never
    // settle, and keeping them in `needed` would force the traversal to
    // exhaust the entire reachable frontier after every reachable site
    // was already decided. Mirrors find_shortest_route_into's union-find
    // early-out before tree acquisition.
    const auto membership=world_.lanes.route_components(range);
    const int component=membership[origin_slot];
    return_needed_.erase(
        std::remove_if(return_needed_.begin(),return_needed_.end(),
            [&](int slot){return membership[slot]!=component;}),
        return_needed_.end());
    if(return_needed_.empty())return std::nullopt;
  }
  std::optional<RefuelingReach> nearest;
  const auto accept=[&](int slot){
    // Collect the target->root predecessor chain, then consume it
    // root->target so the (leg compare, subtract, top-up) sequence runs
    // in evaluate_route's order over the materialized route. Every node
    // on the chain is settled — a settled node's ancestors settled first.
    feas_walk_.clear();
    for(int s=slot;s!=origin_slot;s=return_prior_[s])
      feas_walk_.push_back(s);
    double fuel=fleet.fuel_remaining_light_years;
    if(const auto it=refueling_.find(*fleet.current_system_id);it!=refueling_.end())
      fuel=fleet.fuel_capacity_light_years*it->second;
    for(auto it=feas_walk_.rbegin();it!=feas_walk_.rend();++it){
      const int s=*it;
      const int p=return_prior_[s];
      const double leg=distance_light_years(slots[p].position,slots[s].position);
      if(leg>fuel+1e-9)return false;
      fuel-=leg;
      if(const auto site=refueling_.find(slots[s].id);site!=refueling_.end())
        fuel=fleet.fuel_capacity_light_years*site->second;
    }
    // distance[] is the tree's parent-first leg accumulation — the same
    // association order as evaluate_route's route distance. The settled
    // prior chain also materializes the winner's route bit-identically
    // to find_shortest_route's own prior-walk, so assign_fleet_route
    // consumes it directly instead of re-deriving (and re-caching) a
    // tree rooted at this same hop origin.
    std::vector<int> route;
    route.reserve(feas_walk_.size()+1);
    route.push_back(*fleet.current_system_id);
    for(auto it=feas_walk_.rbegin();it!=feas_walk_.rend();++it)
      route.push_back(slots[*it].id);
    nearest=RefuelingReach{slots[slot].id,
                           MissionReachAssessment{true,true,{},std::move(route),
                                                  return_dist_[slot],fuel}};
    return true;
  };
  world_.lanes.route_tree_toward(*fleet.current_system_id,range,
                                 return_needed_,return_dist_,return_prior_,
                                 accept);
  return nearest;
}

MissionReachAssessment assess_operational_reach(OperationalReachWorldView world,
    int civilization_id,const FleetState &fleet,int target_system_id,InterstellarMissionKind kind) {
  return OperationalReachBatch(world,civilization_id).assess(fleet,target_system_id,kind);
}

void assign_fleet_route(OperationalReachWorldView world, FleetState &fleet,
                        int final_destination_system_id,
                        const MissionReachAssessment &reach) {
  if (!reach.is_supported)
    throw std::runtime_error("A fleet route can only be assigned from a "
                             "supported reach assessment.");

  auto route = reach.route_system_ids;
  if (!route && fleet.current_system_id)
    route = world.lanes.find_shortest_route(
        *fleet.current_system_id, final_destination_system_id,
        fleet.maximum_leg_range_light_years);
  if (!route || route->empty())
    route = std::vector<int>{final_destination_system_id};
  ensure_revision_available(fleet);

  fleet.destination_system_id = final_destination_system_id;
  ++fleet.mission_order_revision;
  fleet.hold_requested = false;
  fleet.return_to_base_requested = false;
  fleet.return_to_base_failure_reason.reset();
  fleet.planned_route_system_ids.clear();
  std::copy_if(route->begin(), route->end(),
               std::back_inserter(fleet.planned_route_system_ids), [&](int id) {
                 return !fleet.current_system_id ||
                        id != *fleet.current_system_id;
               });

  if (fleet.current_system_id &&
      (fleet.transit_phase == FleetTransitPhase::LocalDeparture ||
       fleet.transit_phase == FleetTransitPhase::LocalArrival)) {
    const auto current = find_system(world.systems, *fleet.current_system_id);
    if (!current)
      return;
    const auto next_id = fleet.planned_route_system_ids.empty()
                             ? final_destination_system_id
                             : fleet.planned_route_system_ids.front();
    const auto next = find_system(world.systems, next_id);
    if (!next)
      return;
    fleet.transit_origin_system_id = current->id;
    fleet.transit_target_system_id = next->id;
    begin_fleet_local_transit(
        fleet, FleetTransitPhase::LocalDeparture, fleet.local_transit_position,
        fleet_gate_towards(Vec2{next->position.x, next->position.y},
                           Vec2{current->position.x, current->position.y}),
        current->stellar_object?&*current->stellar_object:nullptr);
  }
}

void clear_fleet_route(FleetState &fleet) {
  ensure_revision_available(fleet);
  fleet.destination_system_id.reset();
  ++fleet.mission_order_revision;
  fleet.hold_requested = false;
  fleet.return_to_base_requested = false;
  fleet.return_to_base_failure_reason.reset();
  fleet.planned_route_system_ids.clear();
  if (fleet.current_system_id &&
      fleet.transit_phase != FleetTransitPhase::None) {
    begin_fleet_local_transit(fleet, FleetTransitPhase::LocalArrival,
                              fleet.local_transit_position, fleet.stellar_transit_path.empty()?Vec2{}:fleet.local_transit_position);
    fleet.transit_target_system_id.reset();
  } else if (fleet.transit_phase != FleetTransitPhase::InterstellarWarp) {
    fleet.transit_phase = FleetTransitPhase::None;
    fleet.transit_origin_system_id.reset();
    fleet.transit_target_system_id.reset();
    fleet.transit_progress = 0.0;
  }
}

} // namespace stellar::core
