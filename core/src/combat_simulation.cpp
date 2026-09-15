#include <stellar/core/combat_simulation.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace stellar::core {
namespace {
constexpr double epsilon = .0000001;
double source_max(double a, double b) {
  return std::isnan(a) || std::isnan(b)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(a, b);
}
double source_min(double a, double b) {
  return std::isnan(a) || std::isnan(b)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(a, b);
}
std::string fixed(double v, int digits = 1) {
  return detail::legacy_custom_fixed(v, 0, digits);
}
[[noreturn]] void duplicate(int id) {
  throw std::invalid_argument(
      "An item with the same key has already been added. Key: " +
      std::to_string(id));
}
using FleetMap = std::map<int, FleetState *>;
using TargetMap = std::map<int, int>;
struct TargetPlan {
  TargetMap lookup;
  std::vector<std::pair<int, int>> insertion_order;
  void add(int source, int target) {
    if (lookup.emplace(source, target).second)
      insertion_order.emplace_back(source, target);
  }
};
using Key = std::pair<int, int>;
FleetMap index_fleets(std::span<FleetState> fleets, bool active_only) {
  FleetMap result;
  std::vector<FleetState *> selected;
  for (auto &fleet : fleets)
    if (!active_only || fleet.is_active)
      selected.push_back(&fleet);
  if (active_only)
    std::stable_sort(selected.begin(), selected.end(),
                     [](const auto *left, const auto *right) {
                       return left->id < right->id;
                     });
  for (auto *fleet : selected)
    if (!result.emplace(fleet->id, fleet).second)
      duplicate(fleet->id);
  return result;
}
void set_hold(FleetCombatState &s, bool preserve) {
  s.order = MilitaryOrderType::Hold;
  s.target_fleet_id.reset();
  s.defend_system_id.reset();
  s.retreat_progress_days = 0;
  s.retreat_started = false;
  if (!preserve) {
    s.is_disengaged = false;
    s.disengaged_system_id.reset();
  }
}
bool system_exists(std::span<const StellarSystem> systems, int id) {
  return std::any_of(systems.begin(), systems.end(),
                     [=](const auto &s) { return s.id == id; });
}
bool can_engage(FleetState &a, FleetState &t,
                const CombatHostilityView &hostility) {
  if (!a.is_active || !t.is_active || a.civilization_id == t.civilization_id)
    return false;
  if (!a.current_system_id || a.current_system_id != t.current_system_id)
    return false;
  auto &as = ensure_fleet_combat_state(a);
  auto &ts = ensure_fleet_combat_state(t);
  return !as.is_disengaged && !ts.is_disengaged &&
         hostility(a.civilization_id, t.civilization_id);
}
TargetPlan build_targets(FleetMap &active,
                         const CombatHostilityView &hostility) {
  TargetPlan targets;
  for (auto [id, a] : active) {
    auto &s = ensure_fleet_combat_state(*a);
    if (s.order != MilitaryOrderType::Attack || !s.target_fleet_id)
      continue;
    auto it = active.find(*s.target_fleet_id);
    if (it == active.end() || !can_engage(*a, *it->second, hostility)) {
      set_hold(s, true);
      continue;
    }
    targets.add(id, it->first);
  }
  const auto explicit_attacks = targets.insertion_order;
  for (auto [aid, tid] : explicit_attacks) {
    auto *a = active.at(aid);
    auto *d = active.at(tid);
    if (targets.lookup.contains(d->id))
      continue;
    auto &ds = ensure_fleet_combat_state(*d);
    const auto &dp = get_combat_profile(ds.profile_id);
    if (ds.order == MilitaryOrderType::Retreat || ds.is_disengaged ||
        !dp.has_weapon())
      continue;
    targets.add(d->id, a->id);
  }
  std::map<std::pair<int, int>, int> threats;
  for (auto [aid, tid] : explicit_attacks) {
    auto *a = active.at(aid);
    auto *t = active.at(tid);
    if (!t->current_system_id || a->current_system_id != t->current_system_id ||
        !hostility(t->civilization_id, a->civilization_id))
      continue;
    auto key = std::pair{t->civilization_id, *t->current_system_id};
    auto [it, inserted] = threats.emplace(key, a->id);
    if (!inserted && a->id < it->second)
      it->second = a->id;
  }
  for (auto [id, d] : active) {
    if (targets.lookup.contains(id))
      continue;
    auto &s = ensure_fleet_combat_state(*d);
    const auto &p = get_combat_profile(s.profile_id);
    if (s.order != MilitaryOrderType::Defend || s.is_disengaged ||
        !p.has_weapon() || !s.defend_system_id ||
        d->current_system_id != s.defend_system_id)
      continue;
    auto threat = threats.find({d->civilization_id, *s.defend_system_id});
    if (threat == threats.end())
      continue;
    auto *h = active.at(threat->second);
    if (can_engage(*d, *h, hostility))
      targets.add(id, h->id);
  }
  return targets;
}
std::set<Key> engagement_set(const TargetPlan &t) {
  std::set<Key> r;
  for (auto [a, b] : t.lookup)
    r.emplace(std::min(a, b), std::max(a, b));
  return r;
}
int advance_timer(FleetCombatState &s, const CombatProfileDefinition &p,
                  double elapsed) {
  if (!p.has_weapon() || elapsed <= 0)
    return 0;
  auto cooldown = source_max(0, s.weapon_cooldown_remaining_days);
  if (cooldown > elapsed + epsilon) {
    s.weapon_cooldown_remaining_days = cooldown - elapsed;
    return 0;
  }
  auto after = source_max(0, elapsed - cooldown);
  int additional = 0;
  if (!(after <= epsilon)) {
    const auto raw = std::floor((after - epsilon) / p.weapon_interval_days);
    if (std::isnan(raw))
      additional = 0;
    else if (!std::isfinite(raw) || raw < std::numeric_limits<int>::min() ||
             raw > std::numeric_limits<int>::max())
      throw std::overflow_error(
          "Combat volley count exceeds the native integer range.");
    else
      additional = std::max(0, static_cast<int>(raw));
  }
  if (additional == std::numeric_limits<int>::max())
    throw std::overflow_error(
        "Combat volley count exceeds the native integer range.");
  const int volleys = 1 + additional;
  auto since = after - additional * p.weapon_interval_days;
  s.weapon_cooldown_remaining_days =
      source_max(0, p.weapon_interval_days - since);
  if (s.weapon_cooldown_remaining_days <= epsilon)
    s.weapon_cooldown_remaining_days = 0;
  return volleys;
}
struct Fire {
  int source{}, target{}, volleys{};
  double damage{};
};
std::vector<Fire> build_fire(FleetMap &active, const TargetPlan &targets,
                             double days) {
  std::vector<Fire> r;
  std::set<int> engaged;
  for (auto [sid, tid] : targets.lookup) {
    auto &s = *active.at(sid);
    auto &t = *active.at(tid);
    auto &ss = ensure_fleet_combat_state(s);
    auto &ts = ensure_fleet_combat_state(t);
    const auto &p = get_combat_profile(ss.profile_id);
    engaged.insert(s.id);
    if (!p.has_weapon() || ss.order == MilitaryOrderType::Retreat ||
        ss.is_disengaged) {
      ss.weapon_cooldown_remaining_days =
          source_max(0, ss.weapon_cooldown_remaining_days - days);
      continue;
    }
    auto delta = days;
    if (ts.order == MilitaryOrderType::Retreat) {
      const auto &tp = get_combat_profile(ts.profile_id);
      delta = source_min(delta, source_max(0, tp.retreat_delay_days -
                                                  ts.retreat_progress_days));
    }
    auto volleys = advance_timer(ss, p, delta);
    if (volleys > 0)
      r.push_back({s.id, t.id, volleys, p.weapon_damage * volleys});
    auto remainder = source_max(0, days - delta);
    if (remainder > 0)
      ss.weapon_cooldown_remaining_days =
          source_max(0, ss.weapon_cooldown_remaining_days - remainder);
  }
  for (auto [id, f] : active)
    if (!engaged.contains(id)) {
      auto &s = ensure_fleet_combat_state(*f);
      s.weapon_cooldown_remaining_days =
          source_max(0, s.weapon_cooldown_remaining_days - days);
    }
  return r;
}
struct Damage {
  double shield{}, armor{}, hull{};
  double total() const { return shield + armor + hull; }
};
Damage apply_damage(FleetCombatState &s, double incoming) {
  auto remaining = source_max(0, incoming);
  auto shield = source_min(s.shields, remaining);
  s.shields -= shield;
  remaining -= shield;
  auto armor = source_min(s.armor, remaining);
  s.armor -= armor;
  remaining -= armor;
  auto hull = source_min(s.hull, remaining);
  s.hull -= hull;
  return {shield, armor, hull};
}
void apply_fire(std::span<FleetState> fleets, std::vector<Fire> actions,
                std::vector<CombatEvent> &events) {
  auto all = index_fleets(fleets, false);
  std::sort(actions.begin(), actions.end(), [](const auto &a, const auto &b) {
    return std::tie(a.target, a.source) < std::tie(b.target, b.source);
  });
  for (const auto &a : actions) {
    auto si = all.find(a.source), ti = all.find(a.target);
    if (si == all.end() || ti == all.end())
      continue;
    auto &source = *si->second;
    auto &target = *ti->second;
    auto &state = ensure_fleet_combat_state(target);
    if (state.hull <= epsilon)
      continue;
    auto d = apply_damage(state, a.damage);
    if (d.total() <= epsilon)
      continue;
    events.push_back({CombatEventType::DamageApplied, target.current_system_id,
                      source.civilization_id, source.id, target.civilization_id,
                      target.id, d.shield, d.armor, d.hull,
                      source.name + " hit " + target.name + ": " +
                          fixed(d.total()) + " damage (" + fixed(d.shield) +
                          " shields, " + fixed(d.armor) + " armor, " +
                          fixed(d.hull) + " hull)."});
    if (state.hull > epsilon || !target.is_active)
      continue;
    auto casualties = source_max(0, target.embarked_population_millions);
    target.embarked_population_millions = 0;
    target.embarked_population_species_id.reset();
    target.is_active = false;
    target.destination_system_id.reset();
    target.planned_route_system_ids.clear();
    target.destination_planetary_body_id.reset();
    set_hold(state, false);
    auto suffix = casualties > epsilon
                      ? " " + fixed(casualties, 3) +
                            " million embarked population were lost."
                      : "";
    events.push_back(
        {CombatEventType::FleetDestroyed, target.current_system_id,
         source.civilization_id, source.id, target.civilization_id, target.id,
         0, 0, 0,
         target.name + " was destroyed by " + source.name + "." + suffix,
         casualties});
  }
}
std::set<int> actively_threatened(const FleetMap &all,
                                  const TargetPlan &targets,
                                  const CombatHostilityView &hostility) {
  std::set<int> r;
  for (auto [a, t] : targets.insertion_order) {
    auto ai = all.find(a), ti = all.find(t);
    if (ai == all.end() || ti == all.end())
      continue;
    auto &af = *ai->second;
    auto &tf = *ti->second;
    if (af.is_active && tf.is_active &&
        af.current_system_id == tf.current_system_id &&
        hostility(af.civilization_id, tf.civilization_id))
      r.insert(tf.id);
  }
  return r;
}
void process_retreats(std::span<FleetState> fleets, const TargetPlan &targets,
                      double days, const CombatHostilityView &hostility,
                      std::vector<CombatEvent> &events) {
  auto all = index_fleets(fleets, false);
  auto threatened = actively_threatened(all, targets, hostility);
  std::vector<FleetState *> ordered;
  for (auto &f : fleets)
    if (f.is_active)
      ordered.push_back(&f);
  std::sort(ordered.begin(), ordered.end(),
            [](auto *a, auto *b) { return a->id < b->id; });
  for (auto *f : ordered) {
    auto &s = ensure_fleet_combat_state(*f);
    if (s.order != MilitaryOrderType::Retreat)
      continue;
    if (!s.retreat_started) {
      s.retreat_started = true;
      events.push_back({CombatEventType::FleetRetreatInitiated,
                        f->current_system_id,
                        f->civilization_id,
                        f->id,
                        {},
                        {},
                        0,
                        0,
                        0,
                        f->name + " began tactical disengagement."});
    }
    const auto &p = get_combat_profile(s.profile_id);
    if (threatened.contains(f->id))
      s.retreat_progress_days += days;
    else
      s.retreat_progress_days = p.retreat_delay_days;
    if (s.retreat_progress_days + epsilon < p.retreat_delay_days)
      continue;
    s.is_disengaged = true;
    s.disengaged_system_id = f->current_system_id;
    s.order = MilitaryOrderType::Hold;
    s.target_fleet_id.reset();
    s.defend_system_id.reset();
    s.retreat_progress_days = 0;
    s.retreat_started = false;
    events.push_back({CombatEventType::FleetEscaped,
                      f->current_system_id,
                      f->civilization_id,
                      f->id,
                      {},
                      {},
                      0,
                      0,
                      0,
                      f->name + " successfully disengaged."});
  }
}
FleetState &aggressor(FleetState &a, FleetState &b, const TargetMap &t) {
  auto &as = ensure_fleet_combat_state(a);
  if (as.order == MilitaryOrderType::Attack && as.target_fleet_id == b.id)
    return a;
  auto &bs = ensure_fleet_combat_state(b);
  if (bs.order == MilitaryOrderType::Attack && bs.target_fleet_id == a.id)
    return b;
  auto it = t.find(a.id);
  return it != t.end() && it->second == b.id ? a : b;
}
} // namespace

CombatSimulation::CombatSimulation(CombatHostilityView hostility)
    : hostility_(std::move(hostility)) {
  if (!hostility_)
    hostility_ = [](int, int) { return false; };
}
CombatOrderResult CombatSimulation::issue_order(CombatWorldView world,
                                                int civilization_id,
                                                int fleet_id,
                                                const MilitaryOrder &order) {
  auto it = std::find_if(
      world.fleets.begin(), world.fleets.end(), [=](const auto &f) {
        return f.id == fleet_id && f.civilization_id == civilization_id &&
               f.is_active;
      });
  if (it == world.fleets.end())
    return {false,
            "No active fleet with that identity belongs to the civilization."};
  auto &s = ensure_fleet_combat_state(*it);
  const auto &p = get_combat_profile(s.profile_id);
  switch (order.type) {
  case MilitaryOrderType::Hold:
    set_hold(s, true);
    return {true, it->name + " is holding position."};
  case MilitaryOrderType::Defend: {
    if (!p.has_weapon())
      return {false, it->name + " has no combat-capable weapon system."};
    auto system =
        order.defend_system_id ? order.defend_system_id : it->current_system_id;
    if (!system || it->current_system_id != system ||
        !system_exists(world.systems, *system))
      return {false, "A defend order currently requires the fleet to be "
                     "present in the defended system."};
    s.order = MilitaryOrderType::Defend;
    s.target_fleet_id.reset();
    s.defend_system_id = system;
    s.retreat_progress_days = 0;
    s.retreat_started = false;
    s.is_disengaged = false;
    s.disengaged_system_id.reset();
    return {true,
            it->name + " is defending system " + std::to_string(*system) + "."};
  }
  case MilitaryOrderType::Attack: {
    if (!p.has_weapon())
      return {false, it->name + " has no combat-capable weapon system."};
    if (!order.target_fleet_id)
      return {false, "An attack order requires a target fleet."};
    auto target = std::find_if(
        world.fleets.begin(), world.fleets.end(), [&](const auto &f) {
          return f.id == *order.target_fleet_id && f.is_active;
        });
    if (target == world.fleets.end() ||
        target->civilization_id == civilization_id)
      return {false, "The requested target is not a valid hostile fleet."};
    if (!it->current_system_id ||
        it->current_system_id != target->current_system_id)
      return {false,
              "The target must be in the same system before combat can begin."};
    auto &ts = ensure_fleet_combat_state(*target);
    if (ts.is_disengaged && ts.disengaged_system_id == it->current_system_id)
      return {false, "The target has tactically disengaged from this "
                     "system-level engagement."};
    if (!hostility_(civilization_id, target->civilization_id))
      return {false, "Diplomatic/political state does not currently permit a "
                     "hostile engagement."};
    s.order = MilitaryOrderType::Attack;
    s.target_fleet_id = target->id;
    s.defend_system_id.reset();
    s.retreat_progress_days = 0;
    s.retreat_started = false;
    s.is_disengaged = false;
    s.disengaged_system_id.reset();
    return {true, it->name + " is attacking " + target->name + "."};
  }
  case MilitaryOrderType::Retreat:
    s.order = MilitaryOrderType::Retreat;
    s.target_fleet_id.reset();
    s.defend_system_id.reset();
    s.retreat_progress_days = 0;
    s.retreat_started = false;
    return {true, it->name + " is attempting to disengage."};
  default:
    return {false, "Unknown military order."};
  }
}

std::vector<CombatEvent> CombatSimulation::advance(CombatWorldView world,
                                                   double days) {
  if (days <= 0)
    return {};
  if (!std::isfinite(days))
    throw std::out_of_range("Nonfinite combat time cannot be converted safely "
                            "by the native volley counter.");
  std::vector<CombatEvent> events;
  auto active = index_fleets(world.fleets, true);
  for (auto [id, f] : active)
    ensure_fleet_combat_state(*f);
  auto targets = build_targets(active, hostility_);
  auto current = engagement_set(targets);
  auto all = index_fleets(world.fleets, false);
  for (auto key : current)
    if (!active_engagements_.contains(key)) {
      auto &first = *all.at(key.first);
      auto &second = *all.at(key.second);
      auto &actor = aggressor(first, second, targets.lookup);
      auto &target = actor.id == first.id ? second : first;
      events.push_back({CombatEventType::EngagementStarted,
                        actor.current_system_id ? actor.current_system_id
                                                : target.current_system_id,
                        actor.civilization_id, actor.id, target.civilization_id,
                        target.id, 0, 0, 0,
                        actor.name + " engaged " + target.name + "."});
    }
  auto actions = build_fire(active, targets, days);
  apply_fire(world.fleets, std::move(actions), events);
  process_retreats(world.fleets, targets, days, hostility_, events);
  auto survivors = index_fleets(world.fleets, true);
  auto remaining_targets = build_targets(survivors, hostility_);
  auto remaining = engagement_set(remaining_targets);
  auto all_after = index_fleets(world.fleets, false);
  for (auto key : active_engagements_)
    if (!remaining.contains(key)) {
      auto fi = all_after.find(key.first), si = all_after.find(key.second);
      if (fi == all_after.end() || si == all_after.end())
        continue;
      auto &first = *fi->second;
      auto &second = *si->second;
      events.push_back({CombatEventType::EngagementEnded,
                        first.current_system_id ? first.current_system_id
                                                : second.current_system_id,
                        first.civilization_id, first.id, second.civilization_id,
                        second.id, 0, 0, 0,
                        "Engagement between " + first.name + " and " +
                            second.name + " ended."});
    }
  active_engagements_ = std::move(remaining);
  return events;
}

MilitaryForceSummary
CombatSimulation::get_own_military_force_summary(CombatWorldView world,
                                                 int civilization_id) {
  MilitaryForceSummary result{civilization_id};
  for (auto &f : world.fleets)
    if (f.is_active && f.civilization_id == civilization_id) {
      auto &s = ensure_fleet_combat_state(f);
      const auto &p = get_combat_profile(s.profile_id);
      if (!p.has_weapon())
        continue;
      ++result.active_combat_vessels;
      auto current = s.shields + s.armor + s.hull;
      auto maximum = p.max_shields + p.max_armor + p.max_hull;
      auto readiness = p.max_hull <= epsilon
                           ? 0.0
                           : std::clamp(s.hull / p.max_hull, 0.0, 1.0);
      auto offense = p.sustained_damage_per_day() * 3;
      result.current_strength += current + offense * readiness;
      result.maximum_strength += maximum + offense;
    }
  return result;
}
} // namespace stellar::core
