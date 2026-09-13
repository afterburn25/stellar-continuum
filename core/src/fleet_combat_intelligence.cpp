#include <stellar/core/fleet_combat_intelligence.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

bool dotnet_double_equals(double left, double right) noexcept {
  return left == right || (std::isnan(left) && std::isnan(right));
}

int dotnet_double_compare(double left, double right) noexcept {
  if (dotnet_double_equals(left, right)) return 0;
  if (std::isnan(left)) return -1;
  if (std::isnan(right)) return 1;
  return left < right ? -1 : 1;
}

struct ObservationLess {
  bool operator()(const FleetPowerObservation &left,
                  const FleetPowerObservation &right) const noexcept {
    if (left.observer_id != right.observer_id)
      return left.observer_id < right.observer_id;
    if (left.fleet_id != right.fleet_id)
      return left.fleet_id < right.fleet_id;
    const auto power_order = dotnet_double_compare(left.power, right.power);
    if (power_order != 0) return power_order < 0;
    const auto day_order =
        dotnet_double_compare(left.observed_day, right.observed_day);
    if (day_order != 0) return day_order < 0;
    return left.evidence < right.evidence;
  }
};

double clamp_like_dotnet(double value, double minimum,
                         double maximum) noexcept {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

float per_ship_power(const MassiveCombatLoadout &loadout) {
  loadout.validate();
  const float durability = loadout.shield_per_ship + loadout.armor_per_ship +
                           loadout.hull_per_ship;
  double offense_accumulator{};
  double defense_accumulator{};
  for (const auto &weapon : loadout.weapons) {
    if (weapon.kind == MassiveWeaponKind::PointDefense) {
      defense_accumulator += static_cast<double>(
          weapon.shots_per_second *
          static_cast<float>(weapon.mounts_per_ship) * 4.0f);
    } else if (weapon.kind != MassiveWeaponKind::ElectronicWarfare) {
      offense_accumulator += static_cast<double>(
          weapon.damage_per_shot * weapon.shots_per_second *
          static_cast<float>(weapon.mounts_per_ship) *
          std::clamp(weapon.accuracy, 0.0f, 1.0f));
    }
  }
  double capability_accumulator{};
  for (const auto &module : loadout.modules)
    if (module.enabled)
      capability_accumulator += static_cast<double>(
          module.field_strength * module.condition * 0.2f);
  const auto offense = static_cast<float>(offense_accumulator);
  const auto defense = static_cast<float>(defense_accumulator);
  const auto capability = static_cast<float>(capability_accumulator);
  const auto computed = durability + offense * 8.0f + defense + capability;
  return std::isnan(computed) ? computed : std::max(0.0f, computed);
}

const CombatProfileDefinition &profile(const FleetState &fleet) {
  if (fleet.combat) {
    if (const auto *found = find_combat_profile(fleet.combat->profile_id))
      return *found;
  }
  return get_combat_profile(default_combat_profile_id(fleet.role));
}

struct PowerCache {
  std::map<const MassiveCombatLoadout *, float> tactical;
  std::map<std::string, float, std::less<>> legacy;
};

double own_power(const FleetState &fleet, PowerCache &cache) {
  if (!fleet.is_active) return 0.0;
  const auto &combat_profile = profile(fleet);
  float per_ship{};
  if (fleet.tactical_loadout) {
    const auto *key = &*fleet.tactical_loadout;
    const auto found = cache.tactical.find(key);
    if (found != cache.tactical.end()) {
      per_ship = found->second;
    } else {
      per_ship = per_ship_power(*key);
      cache.tactical.emplace(key, per_ship);
    }
  } else {
    const auto found = cache.legacy.find(combat_profile.id);
    if (found != cache.legacy.end()) {
      per_ship = found->second;
    } else {
      per_ship = per_ship_power(massive_loadout_from_legacy(combat_profile));
      cache.legacy.emplace(combat_profile.id, per_ship);
    }
  }
  const double maximum = combat_profile.max_shields +
                         combat_profile.max_armor + combat_profile.max_hull;
  const double present = fleet.combat
                             ? fleet.combat->shields + fleet.combat->armor +
                                   fleet.combat->hull
                             : maximum;
  const double condition =
      clamp_like_dotnet(present / std::max(1.0, maximum), 0.0, 1.0);
  return static_cast<double>(per_ship) * condition;
}

std::map<int, const FleetState *> fleet_map(
    std::span<const FleetState> fleets) {
  std::map<int, const FleetState *> result;
  for (const auto &fleet : fleets) {
    if (!result.emplace(fleet.id, &fleet).second)
      throw FleetCombatIntelligenceArgumentError(
          "An item with the same key has already been added. Key: " +
          std::to_string(fleet.id));
  }
  return result;
}

void validate_observer_and_day(int observer_id, double day) {
  if (observer_id < 0 || !std::isfinite(day) || day < 0.0)
    throw FleetCombatIntelligenceRangeError(
        "Combat intelligence requires valid campaign identities and time. (Parameter 'day')");
}

void observe_with_map(FleetCombatIntelligenceWorldView world, int observer_id,
                      std::span<const FleetState *const> targets, double day,
                      bool engaged, bool scanning_capability,
                      const std::map<int, const FleetState *> &members) {
  validate_observer_and_day(observer_id, day);
  if (!engaged && !scanning_capability) return;

  std::vector<const FleetState *> observed(targets.begin(), targets.end());
  if (std::ranges::any_of(observed,
                          [](const FleetState *fleet) { return !fleet; }))
    throw FleetCombatIntelligenceRangeError(
        "Combat intelligence target is not a campaign fleet. (Parameter 'targets')");
  std::stable_sort(observed.begin(), observed.end(),
                   [](const FleetState *left, const FleetState *right) {
                     return left->id < right->id;
                   });
  if (observed.size() > maximum_fleet_power_observations_per_observer)
    observed.resize(maximum_fleet_power_observations_per_observer);
  for (const auto *target : observed) {
    const auto found = members.find(target->id);
    if (found == members.end() || found->second != target)
      throw FleetCombatIntelligenceRangeError(
          "Combat intelligence target is not a campaign fleet. (Parameter 'targets')");
  }

  std::set<int> observed_ids;
  for (const auto *target : observed) observed_ids.insert(target->id);
  std::erase_if(world.observations, [&](const auto &entry) {
    return entry.observer_id == observer_id &&
           observed_ids.contains(entry.fleet_id);
  });
  PowerCache cache;
  for (const auto *target : observed)
    world.observations.push_back(
        {observer_id, target->id, own_power(*target, cache), day,
         engaged ? "Engagement" : "Combat scanner"});

  const auto observer_count = std::ranges::count_if(
      world.observations, [observer_id](const auto &entry) {
        return entry.observer_id == observer_id;
      });
  const auto overflow = observer_count -
                        maximum_fleet_power_observations_per_observer;
  if (overflow > 0) {
    std::vector<FleetPowerObservation> candidates;
    for (const auto &entry : world.observations)
      if (entry.observer_id == observer_id) candidates.push_back(entry);
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto &left, const auto &right) {
                       const int day_order = dotnet_double_compare(
                           left.observed_day, right.observed_day);
                       if (day_order != 0) return day_order < 0;
                       return left.fleet_id < right.fleet_id;
                     });
    candidates.resize(static_cast<std::size_t>(overflow));
    const std::set<FleetPowerObservation, ObservationLess> evicted(
        candidates.begin(), candidates.end());
    std::erase_if(world.observations,
                  [&](const auto &entry) { return evicted.contains(entry); });
  }
  if (world.observations.size() > maximum_fleet_power_observations)
    world.observations.erase(
        world.observations.begin(),
        world.observations.begin() + static_cast<std::ptrdiff_t>(
                                         world.observations.size() -
                                         maximum_fleet_power_observations));
}

} // namespace

bool FleetPowerObservation::operator==(
    const FleetPowerObservation &other) const {
  return observer_id == other.observer_id && fleet_id == other.fleet_id &&
         dotnet_double_equals(power, other.power) &&
         dotnet_double_equals(observed_day, other.observed_day) &&
         evidence == other.evidence;
}

FleetCombatIntelligenceArgumentError::FleetCombatIntelligenceArgumentError(
    std::string message)
    : std::invalid_argument(std::move(message)) {}
FleetCombatIntelligenceRangeError::FleetCombatIntelligenceRangeError(
    std::string message)
    : std::out_of_range(std::move(message)) {}

float massive_combat_per_ship_power(const MassiveCombatLoadout &loadout) {
  return per_ship_power(loadout);
}

double own_fleet_combat_power(const FleetState &fleet) {
  PowerCache cache;
  return own_power(fleet, cache);
}

std::optional<double> observed_fleet_combat_power(
    std::span<const FleetPowerObservation> observations, int observer_id,
    const FleetState &target) {
  if (target.civilization_id == observer_id)
    return own_fleet_combat_power(target);
  for (auto entry = observations.rbegin(); entry != observations.rend(); ++entry)
    if (entry->observer_id == observer_id && entry->fleet_id == target.id)
      return entry->power;
  return std::nullopt;
}

void observe_fleet_combat_power(FleetCombatIntelligenceWorldView world,
                                int observer_id, const FleetState &target,
                                double day, bool engaged,
                                bool scanning_capability) {
  const FleetState *targets[] = {&target};
  observe_fleet_combat_power_many(world, observer_id, targets, day, engaged,
                                  scanning_capability);
}

void observe_fleet_combat_power_many(
    FleetCombatIntelligenceWorldView world, int observer_id,
    std::span<const FleetState *const> targets, double day, bool engaged,
    bool scanning_capability) {
  validate_observer_and_day(observer_id, day);
  if (!engaged && !scanning_capability) return;
  const std::vector<const FleetState *> owned_targets(targets.begin(),
                                                      targets.end());
  const auto members = fleet_map(world.fleets);
  observe_with_map(world, observer_id, owned_targets, day, engaged,
                   scanning_capability, members);
}

int record_fleet_sensor_contacts(FleetCombatIntelligenceWorldView world,
                                 int observer_id, double day,
                                 bool scanning_capability) {
  if (observer_id < 0 || !std::isfinite(day) || day < 0.0 ||
      !std::ranges::any_of(world.civilizations, [observer_id](const auto &value) {
        return value.id == observer_id;
      }))
    throw FleetCombatIntelligenceRangeError(
        "Scanner observation requires a valid campaign observer and time. (Parameter 'observerId')");
  if (!scanning_capability) return 0;
  std::set<int> occupied;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id == observer_id &&
        fleet.current_system_id)
      occupied.insert(*fleet.current_system_id);
  if (occupied.empty()) return 0;
  std::vector<const FleetState *> visible;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id != observer_id &&
        fleet.current_system_id && occupied.contains(*fleet.current_system_id))
      visible.push_back(&fleet);
  std::stable_sort(visible.begin(), visible.end(),
                   [](const FleetState *left, const FleetState *right) {
                     return left->id < right->id;
                   });
  if (visible.size() > maximum_fleet_power_observations_per_observer)
    visible.resize(maximum_fleet_power_observations_per_observer);
  if (visible.empty()) return 0;
  const auto members = fleet_map(world.fleets);
  observe_with_map(world, observer_id, visible, day, false, true, members);
  return static_cast<int>(visible.size());
}

bool has_combat_scanner(
    const AdaptiveResearchCivilizationState *research) noexcept {
  return research &&
         (research->has_capability("tech:quantum_sensors") ||
          research->has_capability("tech:distributed_sensor_network"));
}

} // namespace stellar::core
