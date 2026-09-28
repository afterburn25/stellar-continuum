#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/engine/physics3d.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {

constexpr float epsilon = .0001F;

using Vector = stellar::engine::PhysicsVector3;
Vector vector(MassivePoint value) { return {value.x,value.y,value.z}; }
MassivePoint point(Vector value) { return {value.x,value.y,value.z}; }
using stellar::engine::add;
using stellar::engine::subtract;
using stellar::engine::multiply;
using stellar::engine::length;
using stellar::engine::length_squared;
Vector normalize_direction(Vector value){
  const auto magnitude=length(value);
  // Legacy planar extreme-coordinate saves used IEEE division even when the
  // squared norm overflowed. Keep that behavior at this compatibility edge.
  if(value.z==0.f && std::isinf(magnitude))return {value.x/magnitude,value.y/magnitude,0};
  return stellar::engine::normalize(value);
}
float distance_squared(MassivePoint a, MassivePoint b) {
  return length_squared(subtract(vector(a), vector(b)));
}
float distance(MassivePoint a, MassivePoint b) {
  return std::sqrt(distance_squared(a, b));
}
Vector lerp(Vector a, Vector b, float amount) {
  return add(multiply(a, 1.0F - amount), multiply(b, amount));
}

float dotnet_max(float left, float right) {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<float>::quiet_NaN();
  return left > right ? left : right;
}
float dotnet_min(float left, float right) {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<float>::quiet_NaN();
  return left < right ? left : right;
}

std::int64_t unchecked_increment(std::int64_t value) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(value) + 1U);
}

int unchecked_float_to_int(float value) noexcept {
  if (!std::isfinite(value) || value < static_cast<float>(std::numeric_limits<int>::min()) ||
      value >= 2147483648.0F)
    return std::numeric_limits<int>::min();
  return static_cast<int>(value);
}

int unchecked_add(int left, int right) noexcept {
  return std::bit_cast<int>(std::bit_cast<std::uint32_t>(left) +
                            std::bit_cast<std::uint32_t>(right));
}

int unchecked_double_to_int(double value) noexcept {
  if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min()) ||
      value >= 2147483648.0)
    return std::numeric_limits<int>::min();
  return static_cast<int>(value);
}

std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  for (std::size_t index = 0; index < value.size();) {
    const auto first = static_cast<unsigned char>(value[index++]);
    std::uint32_t code_point = first;
    int remaining = 0;
    if ((first & 0xe0U) == 0xc0U) {
      code_point = first & 0x1fU;
      remaining = 1;
    } else if ((first & 0xf0U) == 0xe0U) {
      code_point = first & 0x0fU;
      remaining = 2;
    } else if ((first & 0xf8U) == 0xf0U) {
      code_point = first & 0x07U;
      remaining = 3;
    }
    while (remaining-- > 0 && index < value.size())
      code_point = (code_point << 6U) |
                   (static_cast<unsigned char>(value[index++]) & 0x3fU);
    if (code_point <= 0xffffU) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000U;
      result.push_back(static_cast<std::uint16_t>(0xd800U + (code_point >> 10U)));
      result.push_back(static_cast<std::uint16_t>(0xdc00U + (code_point & 0x3ffU)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}

bool valid_order(MassiveCombatOrderType value) {
  const auto ordinal = static_cast<int>(value);
  return ordinal >= 0 && ordinal <= 18;
}
bool valid_shape(MassiveFormationShape value) {
  const auto ordinal = static_cast<int>(value);
  return ordinal >= 0 && ordinal <= 7;
}
bool requires_target(MassiveCombatOrderType type) {
  return type == MassiveCombatOrderType::Engage ||
         type == MassiveCombatOrderType::FocusFire ||
         type == MassiveCombatOrderType::Intercept ||
         type == MassiveCombatOrderType::Pursue ||
         type == MassiveCombatOrderType::ProtectCriticalAsset;
}
bool is_withdrawal(MassiveCombatOrderType type) {
  return type == MassiveCombatOrderType::BreakContact ||
         type == MassiveCombatOrderType::Disengage ||
         type == MassiveCombatOrderType::Retreat ||
         type == MassiveCombatOrderType::EmergencyRetreat ||
         type == MassiveCombatOrderType::Breakout;
}
bool is_withdrawal(const MassiveFormationState &formation) {
  return is_withdrawal(formation.order);
}

std::string_view order_name(MassiveCombatOrderType type) {
  constexpr std::array<std::string_view, 19> names{
      "Engage",                         "Hold",       "Defend",
      "Advance",                       "AdvanceCautiously",
      "StandoffAttack",                "Screen",
      "ProtectCriticalAsset",          "FocusFire",
      "FlankLeft",                     "FlankRight",
      "Intercept",                     "Pursue",
      "BreakContact",                  "Disengage",
      "Retreat",                       "EmergencyRetreat",
      "Breakout",                      "Surrender"};
  return names[static_cast<std::size_t>(type)];
}

std::string_view weapon_name(MassiveWeaponKind kind) {
  constexpr std::array<std::string_view, 5> names{
      "Beam", "Kinetic", "Missile", "PointDefense", "ElectronicWarfare"};
  return names[static_cast<std::size_t>(kind)];
}

std::string group_integer(std::int64_t value) {
  auto text = std::to_string(value);
  const auto first = !text.empty() && text.front() == '-' ? 1U : 0U;
  for (std::size_t position = text.size(); position > first + 3; position -= 3)
    text.insert(position - 3, 1, ',');
  return text;
}

std::string fixed_zero(float value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream.setf(std::ios::fixed, std::ios::floatfield);
  stream.precision(0);
  stream << value;
  return stream.str();
}

float float_sum(auto &&range, auto projection) {
  double total = 0;
  for (const auto &value : range)
    total += static_cast<double>(static_cast<float>(projection(value)));
  return static_cast<float>(total);
}

float module_condition(const MassiveFormationState &formation,
                       MassiveModuleKind kind, float default_value) {
  auto result = default_value;
  auto found = false;
  for (const auto &module : formation.loadout.modules) {
    if (module.kind != kind || !module.enabled)
      continue;
    if (!found || module.condition > result)
      result = module.condition;
    found = true;
  }
  return found ? result : default_value;
}

float maximum_weapon_range(const MassiveFormationState &formation) {
  auto result = 500.0F;
  auto found = false;
  for (const auto &weapon : formation.loadout.weapons) {
    if (weapon.kind == MassiveWeaponKind::PointDefense ||
        weapon.kind == MassiveWeaponKind::ElectronicWarfare)
      continue;
    if (!found || weapon.range > result)
      result = weapon.range;
    found = true;
  }
  return found ? result : 500.0F;
}

float shape_accuracy(MassiveFormationShape shape) {
  switch (shape) {
  case MassiveFormationShape::Line:
    return 1.08F;
  case MassiveFormationShape::Wedge:
    return 1.04F;
  case MassiveFormationShape::Dispersed:
    return .88F;
  case MassiveFormationShape::RetreatColumn:
    return .72F;
  default:
    return 1.0F;
  }
}

float mass_mobility(const MassiveFormationState &formation) {
  const auto module_mass = float_sum(
      formation.loadout.modules, [](const MassiveModuleState &module) {
        return module.mass_each * static_cast<float>(module.installed_count);
      });
  return std::clamp(formation.loadout.mass_per_ship /
                        dotnet_max(1.0F, formation.loadout.mass_per_ship + module_mass),
                    .25F, 1.0F);
}

Vector safe_direction(Vector value, Vector fallback) {
  if (length_squared(value) > epsilon)
    return normalize_direction(value);
  if (length_squared(fallback) > epsilon)
    return normalize_direction(fallback);
  return {1, 0};
}

int importance(const MassiveVesselState &vessel) {
  return vessel.is_story_ship   ? 5
         : vessel.is_flagship   ? 4
         : vessel.is_interdictor ? 3
         : vessel.is_carrier    ? 2
                                : 1;
}

float deterministic_unit(std::uint64_t seed, std::int64_t tick,
                         std::int64_t source, std::int64_t target,
                         std::string_view weapon) {
  auto value = seed ^ std::bit_cast<std::uint64_t>(tick) *
                          UINT64_C(0x9e3779b97f4a7c15) ^
               std::bit_cast<std::uint64_t>(source) *
                   UINT64_C(0xbf58476d1ce4e5b9) ^
               std::bit_cast<std::uint64_t>(target);
  for (const auto unit : utf16_units(weapon))
    value = (value ^ unit) * UINT64_C(0x100000001b3);
  value ^= value >> 30U;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27U;
  value *= UINT64_C(0x94d049bb133111eb);
  value ^= value >> 31U;
  return static_cast<float>(value >> 40U) / 16777216.0F;
}

void emit(MassiveCombatBattleState &battle, MassiveCombatEventType type,
          const MassiveFormationState &actor,
          const MassiveFormationState *target, int magnitude,
          std::string message) {
  const auto count = std::ranges::count_if(
      battle.events,
      [&](const MassiveCombatEvent &event) { return event.tick == battle.tick; });
  if (count >= massive_combat_max_events_per_tick)
    return;
  battle.events.push_back(
      {battle.next_event_sequence, battle.tick, type, actor.civilization_id,
       actor.id, target ? std::optional{target->civilization_id} : std::nullopt,
       target ? std::optional{target->id} : std::nullopt, std::max(0, magnitude),
       actor.position, std::move(message)});
  battle.next_event_sequence = unchecked_increment(battle.next_event_sequence);
  if (battle.events.size() > massive_combat_max_retained_events)
    battle.events.erase(battle.events.begin());
}

struct TickMetrics {
  int cells{};
  int candidates{};
  int weapon_groups{};
};

struct Cell {
  int x{};
  int y{};
  int z{};
  auto operator<=>(const Cell &) const = default;
};

class SpatialIndex {
public:
  explicit SpatialIndex(const std::vector<MassiveFormationState *> &formations) {
    for (auto *formation : formations) {
      cells_[cell(formation->position)].push_back(formation);
      planar_ = planar_ && formation->position.z == 0.f;
    }
    for (auto &[key, values] : cells_) {
      static_cast<void>(key);
      std::ranges::sort(values, {}, &MassiveFormationState::id);
    }
  }

  [[nodiscard]] int cell_count() const {
    return static_cast<int>(cells_.size());
  }

  std::vector<MassiveFormationState *> nearby(MassivePoint position, int radius,
                                               TickMetrics &metrics) const {
    std::vector<MassiveFormationState *> result;
    const auto origin = cell(position);
    const auto first_y = unchecked_add(origin.y, -radius);
    const auto last_y = unchecked_add(origin.y, radius);
    const auto first_x = unchecked_add(origin.x, -radius);
    const auto last_x = unchecked_add(origin.x, radius);
    const auto first_z = planar_ ? 0 : unchecked_add(origin.z, -radius);
    const auto last_z = planar_ ? 0 : unchecked_add(origin.z, radius);
    for (auto z = first_z; z <= last_z; z = unchecked_add(z, 1))
    for (auto y = first_y; y <= last_y; y = unchecked_add(y, 1)) {
      for (auto x = first_x; x <= last_x; x = unchecked_add(x, 1)) {
        const auto found = cells_.find({x, y, z});
        if (found == cells_.end())
          continue;
        for (auto *formation : found->second) {
          metrics.candidates = unchecked_add(metrics.candidates, 1);
          result.push_back(formation);
        }
      }
    }
    return result;
  }

private:
  static Cell cell(MassivePoint value) {
    return {unchecked_float_to_int(std::floor(value.x / massive_combat_spatial_cell_size)),
            unchecked_float_to_int(std::floor(value.y / massive_combat_spatial_cell_size)),
            unchecked_float_to_int(std::floor(value.z / massive_combat_spatial_cell_size))};
  }
  std::map<Cell, std::vector<MassiveFormationState *>> cells_;
  bool planar_{true};
};

MassiveModuleState *active_interdictor(MassiveFormationState &formation) {
  MassiveModuleState *result = nullptr;
  float result_strength{};
  for (auto &module : formation.loadout.modules) {
    if (module.kind != MassiveModuleKind::WarpInterdictor || !module.enabled ||
        module.condition <= .05F || formation.power_reserve < .2F)
      continue;
    const auto strength = module.field_strength * module.condition;
    if (!result || strength > result_strength ||
        (strength == result_strength && ordinal_less(module.id, result->id))) {
      result = &module;
      result_strength = strength;
    }
  }
  return result;
}

float speed_for(const MassiveFormationState &formation) {
  const auto factor = formation.order == MassiveCombatOrderType::AdvanceCautiously
                          ? .45F
                      : formation.order == MassiveCombatOrderType::StandoffAttack
                          ? .6F
                      : formation.order == MassiveCombatOrderType::EmergencyRetreat
                          ? 1.0F
                      : formation.order == MassiveCombatOrderType::Retreat ? .85F
                                                                           : .72F;
  return formation.loadout.maximum_speed * mass_mobility(formation) * factor *
         std::clamp(formation.cohesion, .3F, 1.0F);
}

void move(MassiveFormationState &formation,
          const MassiveFormationState *target) {
  const auto at = vector(formation.position);
  auto desired_point = vector(formation.objective);
  if (target) {
    const auto direction = safe_direction(
        subtract(vector(target->position), at), vector(formation.heading));
    const Vector side{-direction.y, direction.x};
    const auto desired_range =
        (formation.order == MassiveCombatOrderType::StandoffAttack ||
         formation.order == MassiveCombatOrderType::AdvanceCautiously)
            ? .82F * maximum_weapon_range(formation)
            : .45F * maximum_weapon_range(formation);
    desired_point = subtract(vector(target->position),
                             multiply(direction, desired_range));
    if (formation.order == MassiveCombatOrderType::FlankLeft)
      desired_point = add(desired_point, multiply(side, 260));
    if (formation.order == MassiveCombatOrderType::FlankRight)
      desired_point = subtract(desired_point, multiply(side, 260));
    if (formation.order == MassiveCombatOrderType::Screen ||
        formation.order == MassiveCombatOrderType::ProtectCriticalAsset)
      desired_point = lerp(at, vector(target->position), .35F);
  }
  if (is_withdrawal(formation)) {
    const auto away = target
                          ? safe_direction(
                                subtract(at, vector(target->position)),
                                vector(formation.heading))
                          : safe_direction(at, vector(formation.heading));
    desired_point = add(at, multiply(away, 2000));
    formation.shape = formation.order == MassiveCombatOrderType::Breakout
                          ? MassiveFormationShape::Breakout
                          : MassiveFormationShape::RetreatColumn;
  }
  const auto delta = subtract(desired_point, at);
  const auto desired_velocity = length_squared(delta) < 4
                                    ? Vector{}
                                    : multiply(normalize_direction(delta), speed_for(formation));
  const auto limit = formation.loadout.acceleration * mass_mobility(formation) *
                     static_cast<float>(massive_combat_tick_seconds) *
                     std::clamp(formation.cohesion, .2F, 1.0F);
  const auto moved=stellar::engine::move_toward_velocity({at,vector(formation.velocity)},desired_velocity,
      limit,static_cast<float>(massive_combat_tick_seconds));
  const auto velocity=moved.velocity;
  formation.position=point(moved.position);formation.velocity=point(velocity);
  if (length_squared(velocity) > .01F)
    formation.heading = point(normalize_direction(velocity));
}

void update_power_and_heat(MassiveFormationState &formation) {
  const auto ships = formation.active_ship_count();
  const auto reactor_condition =
      module_condition(formation, MassiveModuleKind::Reactor, 1.0F);
  const auto output = formation.loadout.reactor_output_per_ship *
                      static_cast<float>(ships) * reactor_condition;
  const auto module_power = float_sum(
      formation.loadout.modules, [](const MassiveModuleState &module) {
        return module.enabled && module.condition > 0
                   ? module.power_per_second_each *
                         static_cast<float>(module.installed_count)
                   : 0.0F;
      });
  const auto weapon_power = float_sum(
      formation.loadout.weapons, [&](const MassiveWeaponGroup &weapon) {
        return weapon.power_per_second * static_cast<float>(weapon.mounts_per_ship) *
               static_cast<float>(ships);
      });
  const auto demand = module_power + weapon_power;
  formation.power_reserve =
      demand <= epsilon ? 1.0F : std::clamp(output / demand, 0.0F, 1.0F);
  const auto module_heat = float_sum(
      formation.loadout.modules, [](const MassiveModuleState &module) {
        return module.enabled
                   ? module.heat_per_second_each *
                         static_cast<float>(module.installed_count)
                   : 0.0F;
      });
  const auto weapon_heat = float_sum(
      formation.loadout.weapons, [&](const MassiveWeaponGroup &weapon) {
        return weapon.heat_per_second * static_cast<float>(weapon.mounts_per_ship) *
               static_cast<float>(ships);
      });
  const auto heat_input = module_heat + weapon_heat * formation.power_reserve;
  formation.heat = dotnet_max(
      0.0F, formation.heat +
                (heat_input - formation.loadout.cooling_per_ship *
                                  static_cast<float>(ships)) *
                    static_cast<float>(massive_combat_tick_seconds));
}

struct Attack {
  MassiveFormationState *source{};
  MassiveFormationState *target{};
  MassiveWeaponKind kind{};
  float damage{};
  int shot_count{};
};

void queue_missile_salvo(MassiveCombatBattleState &battle,
                         MassiveFormationState &source,
                         MassiveFormationState &target, float damage,
                         int missile_count, float range,
                         std::vector<Attack> &immediate_overflow) {
  if (damage <= epsilon || missile_count <= 0)
    return;
  if (battle.active_salvos.size() >= massive_combat_max_active_salvos) {
    immediate_overflow.push_back(
        {&source, &target, MassiveWeaponKind::Missile, damage, missile_count});
    return;
  }
  const auto flight_seconds = dotnet_max(
      static_cast<float>(massive_combat_tick_seconds), range / 800.0F);
  battle.active_salvos.push_back(
      {battle.next_salvo_id, source.id, target.id, missile_count, damage,
       flight_seconds, source.position, flight_seconds});
  battle.next_salvo_id = unchecked_increment(battle.next_salvo_id);
}

void build_attacks(MassiveCombatBattleState &battle,
                   MassiveFormationState &source,
                   MassiveFormationState &target,
                   std::vector<Attack> &attacks, TickMetrics &metrics) {
  const auto range = distance(source.position, target.position);
  const auto heat_factor =
      1.0F / (1.0F + source.heat /
                        dotnet_max(1.0F, static_cast<float>(source.active_ship_count()) * 250.0F));
  const auto control_factor =
      module_condition(source, MassiveModuleKind::WeaponControl, 1.0F);
  const auto ew_attack = float_sum(
      source.loadout.weapons, [&](const MassiveWeaponGroup &weapon) {
        return weapon.kind == MassiveWeaponKind::ElectronicWarfare &&
                       range <= weapon.range
                   ? weapon.damage_per_shot *
                         static_cast<float>(weapon.mounts_per_ship)
                   : 0.0F;
      }) * source.power_reserve;
  const auto target_ew = float_sum(
      target.loadout.weapons, [](const MassiveWeaponGroup &weapon) {
        return weapon.kind == MassiveWeaponKind::ElectronicWarfare
                   ? weapon.damage_per_shot *
                         static_cast<float>(weapon.mounts_per_ship)
                   : 0.0F;
      });
  const auto electronic_factor =
      std::clamp(1.0F + (ew_attack - target_ew) / 500.0F, .55F, 1.25F);
  std::vector<MassiveWeaponGroup *> weapons;
  weapons.reserve(source.loadout.weapons.size());
  for (auto &weapon : source.loadout.weapons)
    weapons.push_back(&weapon);
  std::ranges::sort(weapons, [](const auto *left, const auto *right) {
    return ordinal_less(left->id, right->id);
  });
  for (const auto *weapon : weapons) {
    if (weapon->kind == MassiveWeaponKind::PointDefense ||
        weapon->kind == MassiveWeaponKind::ElectronicWarfare ||
        range > weapon->range)
      continue;
    ++metrics.weapon_groups;
    const auto jitter = .92F +
                        deterministic_unit(battle.seed, battle.tick, source.id,
                                           target.id, weapon->id) *
                            .16F;
    const auto shots = static_cast<float>(source.active_ship_count()) *
                       static_cast<float>(weapon->mounts_per_ship) *
                       weapon->shots_per_second *
                       static_cast<float>(massive_combat_tick_seconds);
    const auto accuracy =
        std::clamp(weapon->accuracy * electronic_factor *
                       shape_accuracy(source.shape) * heat_factor * control_factor,
                   .05F, .98F);
    const auto damage = shots * weapon->damage_per_shot * accuracy * jitter *
                        source.power_reserve;
    const auto rounded = std::nearbyint(shots);
    const auto shot_count = std::max(0, unchecked_float_to_int(rounded));
    if (weapon->kind == MassiveWeaponKind::Missile)
      queue_missile_salvo(battle, source, target, damage, shot_count, range,
                          attacks);
    else
      attacks.push_back({&source, &target, weapon->kind, damage, shot_count});
    const auto event_type = weapon->kind == MassiveWeaponKind::Beam
                                ? MassiveCombatEventType::BeamVolley
                            : weapon->kind == MassiveWeaponKind::Kinetic
                                ? MassiveCombatEventType::KineticVolley
                                : MassiveCombatEventType::MissileSalvo;
    emit(battle, event_type, source, &target, shot_count,
         source.name + " fired an aggregated " +
             std::string(weapon_name(weapon->kind)) + " volley.");
  }
}

std::vector<Attack> advance_salvos(MassiveCombatBattleState &battle) {
  std::vector<Attack> arrived;
  for (auto index = battle.active_salvos.size(); index > 0; --index) {
    auto &salvo = battle.active_salvos[index - 1];
    salvo.remaining_seconds = dotnet_max(
        0.0F, salvo.remaining_seconds -
                  static_cast<float>(massive_combat_tick_seconds));
    if (salvo.remaining_seconds > epsilon)
      continue;
    const auto source = std::ranges::find_if(
        battle.formations,
        [&](const MassiveFormationState &value) {
          return value.id == salvo.source_formation_id;
        });
    const auto target = std::ranges::find_if(
        battle.formations, [&](const MassiveFormationState &value) {
          return value.id == salvo.target_formation_id && value.active();
        });
    if (source != battle.formations.end() && target != battle.formations.end())
      arrived.push_back(
          {&*source, &*target, MassiveWeaponKind::Missile, salvo.damage,
           salvo.missile_count});
    battle.active_salvos.erase(battle.active_salvos.begin() +
                               static_cast<std::ptrdiff_t>(index - 1));
  }
  std::ranges::sort(arrived, [](const Attack &left, const Attack &right) {
    return left.source->id != right.source->id
               ? left.source->id < right.source->id
               : left.target->id < right.target->id;
  });
  return arrived;
}

void remove_ships(MassiveFormationState &target, int losses) {
  std::vector<MassiveCohortState *> cohorts;
  for (auto &cohort : target.cohorts)
    cohorts.push_back(&cohort);
  std::ranges::sort(cohorts, [](const auto *left, const auto *right) {
    return left->active_count != right->active_count
               ? left->active_count > right->active_count
               : left->id < right->id;
  });
  for (auto *cohort : cohorts) {
    const auto removed = std::min(losses, cohort->active_count);
    cohort->active_count -= removed;
    losses -= removed;
    if (losses == 0)
      return;
  }
  std::vector<MassiveVesselState *> vessels;
  for (auto &vessel : target.important_vessels)
    if (!vessel.destroyed && !vessel.escaped)
      vessels.push_back(&vessel);
  std::ranges::sort(vessels, [](const auto *left, const auto *right) {
    return importance(*left) != importance(*right)
               ? importance(*left) < importance(*right)
               : left->id > right->id;
  });
  for (auto *vessel : vessels) {
    vessel->destroyed = true;
    vessel->hull_fraction = 0;
    --losses;
    if (losses == 0)
      return;
  }
}

void apply_damage(MassiveCombatBattleState &battle,
                  MassiveFormationState &target, float damage,
                  MassiveFormationState &source) {
  if (damage <= epsilon || !target.active())
    return;
  const auto ships_before = target.active_ship_count();
  auto remaining = damage;
  const auto shields = dotnet_min(target.shield_pool, remaining);
  target.shield_pool -= shields;
  remaining -= shields;
  const auto armor = dotnet_min(target.armor_pool, remaining);
  target.armor_pool -= armor;
  remaining -= armor;
  const auto hull = dotnet_min(target.hull_pool, remaining);
  target.hull_pool -= hull;
  target.hull_damage_remainder += hull;
  const auto system_shock = hull /
                            dotnet_max(1.0F, target.loadout.hull_per_ship *
                                                static_cast<float>(ships_before));
  for (auto &module : target.loadout.modules)
    module.condition =
        dotnet_max(0.0F, module.condition - system_shock * .18F);
  for (auto &vessel : target.important_vessels) {
    if (vessel.destroyed || vessel.escaped)
      continue;
    vessel.hull_fraction =
        dotnet_max(0.0F, vessel.hull_fraction - system_shock * .12F);
    vessel.engine_fraction =
        dotnet_max(0.0F, vessel.engine_fraction - system_shock * .05F);
    vessel.warp_drive_fraction =
        dotnet_max(0.0F, vessel.warp_drive_fraction - system_shock * .04F);
    vessel.reactor_fraction =
        dotnet_max(0.0F, vessel.reactor_fraction - system_shock * .035F);
    if (vessel.is_interdictor)
      vessel.interdictor_fraction = dotnet_max(
          0.0F, vessel.interdictor_fraction - system_shock * .08F);
  }
  const auto threshold = target.hull_loss_threshold_per_ship > epsilon
                             ? target.hull_loss_threshold_per_ship
                             : target.loadout.hull_per_ship;
  const auto losses = std::min(
      target.active_ship_count(),
      unchecked_float_to_int(target.hull_damage_remainder / threshold));
  if (losses > 0) {
    target.hull_damage_remainder -= static_cast<float>(losses) * threshold;
    remove_ships(target, losses);
    target.destroyed_ships += losses;
    target.morale = dotnet_max(
        0.0F, target.morale -
                  static_cast<float>(losses) /
                      static_cast<float>(std::max(1, target.initial_ship_count)) *
                      .8F);
  }
  emit(battle, MassiveCombatEventType::Damage, source, &target, losses,
       target.name + " sustained " + fixed_zero(damage) +
           " aggregate damage and lost " + group_integer(losses) + " ships.");
  if (!target.active())
    emit(battle, MassiveCombatEventType::FormationDestroyed, source, &target,
         target.destroyed_ships,
         target.name + " was destroyed.");
}

void resolve_attacks(MassiveCombatBattleState &battle,
                     const std::vector<Attack> &attacks) {
  std::map<std::int64_t, std::vector<const Attack *>> groups;
  for (const auto &attack : attacks)
    groups[attack.target->id].push_back(&attack);
  for (const auto &[target_id, values] : groups) {
    static_cast<void>(target_id);
    auto &target = *values.front()->target;
    const auto direct = float_sum(values, [](const Attack *attack) {
      return attack->kind != MassiveWeaponKind::Missile ? attack->damage : 0.0F;
    });
    const auto missile_damage = float_sum(values, [](const Attack *attack) {
      return attack->kind == MassiveWeaponKind::Missile ? attack->damage : 0.0F;
    });
    std::int64_t missile_total = 0;
    for (const auto *attack : values)
      if (attack->kind == MassiveWeaponKind::Missile)
        missile_total += attack->shot_count;
    if (missile_total > std::numeric_limits<int>::max() ||
        missile_total < std::numeric_limits<int>::min())
      throw std::overflow_error("Arithmetic operation resulted in an overflow.");
    const auto missiles = static_cast<int>(missile_total);
    const auto pd = float_sum(
                        target.loadout.weapons,
                        [&](const MassiveWeaponGroup &weapon) {
                          return weapon.kind == MassiveWeaponKind::PointDefense
                                     ? static_cast<float>(weapon.mounts_per_ship) *
                                           weapon.shots_per_second *
                                           static_cast<float>(target.active_ship_count()) *
                                           static_cast<float>(massive_combat_tick_seconds) *
                                           weapon.accuracy
                                     : 0.0F;
                        }) *
                    target.power_reserve;
    const auto intercepted =
        std::min(missiles, unchecked_float_to_int(std::floor(pd)));
    auto adjusted_missile_damage = missile_damage;
    if (missiles > 0)
      adjusted_missile_damage *=
          1.0F - static_cast<float>(intercepted) / static_cast<float>(missiles);
    if (intercepted > 0)
      emit(battle, MassiveCombatEventType::MissileIntercepted, target, nullptr,
           intercepted, target.name + " point defense intercepted " +
                            group_integer(intercepted) + " missiles.");
    const auto source = *std::ranges::min_element(
        values, {}, [](const Attack *attack) { return attack->source->id; });
    apply_damage(battle, target, direct + adjusted_missile_damage,
                 *source->source);
  }
}

void update_interdiction(const std::vector<MassiveFormationState *> &active,
                         const SpatialIndex &grid, TickMetrics &metrics,
                         const CombatHostilityView &hostility) {
  for (auto *target : active) {
    struct Field {
      float strength{};
      std::int64_t source_id{};
    };
    std::vector<Field> fields;
    for (auto *source : grid.nearby(target->position, 4, metrics)) {
      if (!hostility(source->civilization_id, target->civilization_id))
        continue;
      const auto module = active_interdictor(*source);
      if (!module || distance(source->position, target->position) >
                         module->effective_range)
        continue;
      fields.push_back({module->field_strength * module->condition *
                            source->power_reserve,
                        source->id});
    }
    std::ranges::sort(fields, [](const Field &left, const Field &right) {
      return left.strength != right.strength
                 ? left.strength > right.strength
                 : left.source_id < right.source_id;
    });
    if (fields.empty()) {
      target->warp_blocked = false;
      continue;
    }
    auto strength = fields.front().strength;
    for (std::size_t index = 1; index < fields.size(); ++index)
      strength += fields[index].strength * (.35F / static_cast<float>(index));
    strength = dotnet_min(strength, fields.front().strength * 1.75F);
    target->warp_blocked =
        strength > target->loadout.warp_stabilization *
                       module_condition(*target, MassiveModuleKind::WarpDrive,
                                        1.0F);
  }
}

void advance_warp(MassiveCombatBattleState &battle,
                  MassiveFormationState &formation) {
  const auto warp_condition =
      module_condition(formation, MassiveModuleKind::WarpDrive, 1.0F);
  const auto reactor_condition =
      module_condition(formation, MassiveModuleKind::Reactor, 1.0F);
  if (warp_condition <= .05F || reactor_condition <= .05F ||
      formation.power_reserve < .2F) {
    formation.warp_spool_progress = 0;
    return;
  }
  if (formation.warp_blocked) {
    formation.warp_spool_progress =
        dotnet_min(.95F, formation.warp_spool_progress);
    emit(battle, MassiveCombatEventType::WarpBlocked, formation, nullptr, 0,
         "Warp completion blocked by a hostile interdiction field.");
    return;
  }
  const auto multiplier =
      formation.order == MassiveCombatOrderType::EmergencyRetreat ? 1.4F : 1.0F;
  formation.warp_spool_progress +=
      static_cast<float>(massive_combat_tick_seconds) * multiplier /
      formation.loadout.warp_spool_seconds;
  if (formation.warp_spool_progress + epsilon < 1)
    return;
  const auto escaped_ships = formation.surviving_ship_count();
  formation.warp_spool_progress = 1;
  formation.escaped = true;
  for (auto &vessel : formation.important_vessels)
    if (!vessel.destroyed)
      vessel.escaped = true;
  emit(battle, MassiveCombatEventType::Escaped, formation, nullptr,
       escaped_ships, formation.name + " completed warp escape.");
}

MassiveFormationState *resolve_target(MassiveFormationState &source,
                                      MassiveCombatBattleState &battle,
                                      const SpatialIndex &grid,
                                      TickMetrics &metrics,
                                      const CombatHostilityView &hostility) {
  if (source.order == MassiveCombatOrderType::ProtectCriticalAsset &&
      source.protected_formation_id) {
    const auto asset = std::ranges::find_if(
        battle.formations, [&](const MassiveFormationState &value) {
          return value.id == *source.protected_formation_id && value.active() &&
                 value.civilization_id == source.civilization_id;
        });
    if (asset != battle.formations.end()) {
      std::vector<MassiveFormationState *> candidates;
      for (auto *candidate : grid.nearby(asset->position, 2, metrics))
        if (candidate->active() && hostility(source.civilization_id,
                                             candidate->civilization_id))
          candidates.push_back(candidate);
      if (!candidates.empty()) {
        return *std::ranges::min_element(
            candidates, [&](const auto *left, const auto *right) {
              const auto a = distance_squared(asset->position, left->position);
              const auto b = distance_squared(asset->position, right->position);
              return a != b ? a < b : left->id < right->id;
            });
      }
      return nullptr;
    }
  }
  if (source.target_formation_id) {
    const auto explicit_target = std::ranges::find_if(
        battle.formations, [&](const MassiveFormationState &value) {
          return value.id == *source.target_formation_id && value.active() &&
                 hostility(source.civilization_id, value.civilization_id);
        });
    if (explicit_target != battle.formations.end())
      return &*explicit_target;
  }
  if (source.order == MassiveCombatOrderType::Breakout || source.warp_blocked) {
    std::vector<MassiveFormationState *> candidates;
    for (auto *candidate : grid.nearby(source.position, 4, metrics))
      if (candidate->active() &&
          hostility(source.civilization_id, candidate->civilization_id) &&
          active_interdictor(*candidate))
        candidates.push_back(candidate);
    if (!candidates.empty()) {
      return *std::ranges::min_element(
          candidates, [&](const auto *left, const auto *right) {
            const auto a = distance_squared(source.position, left->position);
            const auto b = distance_squared(source.position, right->position);
            return a != b ? a < b : left->id < right->id;
          });
    }
  }
  if (source.order == MassiveCombatOrderType::Hold ||
      source.order == MassiveCombatOrderType::Retreat ||
      source.order == MassiveCombatOrderType::Disengage ||
      source.order == MassiveCombatOrderType::EmergencyRetreat)
    return nullptr;
  std::vector<MassiveFormationState *> candidates;
  for (auto *candidate : grid.nearby(source.position, 4, metrics))
    if (candidate->active() &&
        hostility(source.civilization_id, candidate->civilization_id))
      candidates.push_back(candidate);
  if (candidates.empty())
    return nullptr;
  return *std::ranges::min_element(
      candidates, [&](const auto *left, const auto *right) {
        const auto a = distance_squared(source.position, left->position);
        const auto b = distance_squared(source.position, right->position);
        return a != b ? a < b : left->id < right->id;
      });
}

void step(MassiveCombatBattleState &battle, TickMetrics &metrics,
          const CombatHostilityView &hostility) {
  battle.tick = unchecked_increment(battle.tick);
  battle.simulated_seconds =
      static_cast<double>(battle.tick) * massive_combat_tick_seconds;
  std::vector<MassiveFormationState *> active;
  for (auto &formation : battle.formations)
    if (formation.active())
      active.push_back(&formation);
  std::ranges::sort(active, {}, &MassiveFormationState::id);
  const SpatialIndex grid(active);
  metrics.cells = std::max(metrics.cells, grid.cell_count());
  std::unordered_map<std::int64_t, MassiveFormationState *> targets;
  for (auto *formation : active) {
    update_power_and_heat(*formation);
    auto *target =
        resolve_target(*formation, battle, grid, metrics, hostility);
    if (target)
      targets[formation->id] = target;
    move(*formation, target);
  }
  update_interdiction(active, grid, metrics, hostility);
  auto attacks = advance_salvos(battle);
  for (auto *source : active) {
    const auto found = targets.find(source->id);
    if (found != targets.end())
      build_attacks(battle, *source, *found->second, attacks, metrics);
  }
  resolve_attacks(battle, attacks);
  for (auto *formation : active)
    if (formation->active() && is_withdrawal(*formation))
      advance_warp(battle, *formation);
  const auto excess = static_cast<std::ptrdiff_t>(battle.events.size()) -
                      massive_combat_max_retained_events;
  if (excess > 0)
    battle.events.erase(battle.events.begin(), battle.events.begin() + excess);
}

} // namespace

MassiveCombatArgumentRangeError::MassiveCombatArgumentRangeError(
    std::string message, std::string parameter)
    : std::out_of_range(std::move(message)), parameter_(std::move(parameter)) {}

const std::string &MassiveCombatArgumentRangeError::parameter() const noexcept {
  return parameter_;
}

std::span<const double> MassiveCombatClock::allowed_speeds() noexcept {
  static constexpr std::array values{0.0, .25, .5, 1.0, 2.0, 4.0};
  return values;
}

double MassiveCombatClock::speed_multiplier() const noexcept {
  return speed_multiplier_;
}

void MassiveCombatClock::set_speed(double multiplier) {
  if (!std::isfinite(multiplier) ||
      !std::ranges::contains(allowed_speeds(), multiplier))
    throw MassiveCombatArgumentRangeError(
        "Tactical speed must be paused, .25×, .5×, 1×, 2×, or 4×. "
        "(Parameter 'multiplier')",
        "multiplier");
  speed_multiplier_ = multiplier;
}

double MassiveCombatClock::accept_frame(double real_delta_seconds,
                                        double maximum_frame_seconds) const {
  if (!std::isfinite(real_delta_seconds) || real_delta_seconds < 0 ||
      !std::isfinite(maximum_frame_seconds) || maximum_frame_seconds <= 0)
    throw MassiveCombatArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'realDeltaSeconds')",
        "realDeltaSeconds");
  return std::min(real_delta_seconds, maximum_frame_seconds) *
         speed_multiplier_;
}

struct MassiveCombatEngine::Storage {
  explicit Storage(CombatHostilityView value)
      : hostility(value ? std::move(value)
                        : CombatHostilityView{
                              [](int first, int second) {
                                return first != second;
                              }}) {}
  CombatHostilityView hostility;
};

MassiveCombatEngine::MassiveCombatEngine(CombatHostilityView hostility)
    : storage_(std::make_unique<Storage>(std::move(hostility))) {}
MassiveCombatEngine::~MassiveCombatEngine() = default;
MassiveCombatEngine::MassiveCombatEngine(MassiveCombatEngine &&) noexcept =
    default;
MassiveCombatEngine &
MassiveCombatEngine::operator=(MassiveCombatEngine &&) noexcept = default;

bool MassiveCombatEngine::has_active_hostilities(
    const MassiveCombatBattleState &battle) const {
  std::vector<int> civilizations;
  for (const auto &formation : battle.formations)
    if (formation.active())
      civilizations.push_back(formation.civilization_id);
  std::ranges::sort(civilizations);
  civilizations.erase(std::unique(civilizations.begin(), civilizations.end()),
                       civilizations.end());
  for (std::size_t first = 0; first < civilizations.size(); ++first)
    for (auto second = first + 1; second < civilizations.size(); ++second)
      if (storage_->hostility(civilizations[first], civilizations[second]) ||
          storage_->hostility(civilizations[second], civilizations[first]))
        return true;
  return false;
}

MassiveCombatOrderResult MassiveCombatEngine::issue_order(
    MassiveCombatBattleState &battle, int civilization_id,
    MassiveCombatOrder order) const {
  if (!valid_order(order.type) || (order.shape && !valid_shape(*order.shape)))
    return {false, "The combat order contains an invalid tactical mode."};
  if (order.objective && !order.objective->is_finite())
    return {false, "Combat objective must be finite."};
  const auto formation = std::ranges::find_if(
      battle.formations, [&](const MassiveFormationState &value) {
        return value.id == order.formation_id && value.active();
      });
  if (formation == battle.formations.end() ||
      formation->civilization_id != civilization_id)
    return {false, "No active owned formation has that identity."};
  if (order.target_formation_id) {
    const auto target = std::ranges::find_if(
        battle.formations, [&](const MassiveFormationState &value) {
          return value.id == *order.target_formation_id && value.active();
        });
    const auto protect =
        order.type == MassiveCombatOrderType::ProtectCriticalAsset;
    if (target == battle.formations.end() ||
        protect != (target->civilization_id == civilization_id))
      return {false,
              protect
                  ? "The protected asset must be an active friendly formation."
                  : "The requested target is not an active hostile formation."};
    if (!protect &&
        !storage_->hostility(civilization_id, target->civilization_id))
      return {false,
              "The requested target is not an active hostile formation."};
  }
  if (requires_target(order.type) && !order.target_formation_id)
    return {false, "That combat order requires a target formation."};
  formation->order = order.type;
  formation->target_formation_id = order.target_formation_id;
  if (order.objective)
    formation->objective = *order.objective;
  if (order.shape)
    formation->shape = *order.shape;
  if (order.type == MassiveCombatOrderType::ProtectCriticalAsset)
    formation->protected_formation_id = order.target_formation_id;
  if (order.type == MassiveCombatOrderType::Surrender) {
    const auto surrendered_ships = formation->surviving_ship_count();
    formation->surrendered = true;
    formation->velocity = {};
    emit(battle, MassiveCombatEventType::Surrendered, *formation, nullptr,
         surrendered_ships, formation->name + " surrendered.");
    return {true, formation->name + " acknowledged surrender."};
  }
  if (is_withdrawal(order.type) && formation->warp_spool_progress <= 0)
    emit(battle, MassiveCombatEventType::WarpSpooling, *formation, nullptr, 0,
         "Warp preparation started.");
  emit(battle, MassiveCombatEventType::OrderChanged, *formation, nullptr, 0,
       formation->name + ": " + std::string(order_name(order.type)) + ".");
  return {true, formation->name + " acknowledged " +
                    std::string(order_name(order.type)) + "."};
}

MassiveCombatMetrics MassiveCombatEngine::advance(
    MassiveCombatBattleState &battle, double elapsed_seconds) const {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0)
    throw MassiveCombatArgumentRangeError(
        "Specified argument was out of the range of valid values. "
        "(Parameter 'elapsedSeconds')",
        "elapsedSeconds");
  validate_massive_combat_battle(battle);
  battle.pending_seconds += elapsed_seconds;
  const auto converted = unchecked_double_to_int(
      std::floor((battle.pending_seconds + 1e-10) /
                 massive_combat_tick_seconds));
  const auto ticks = std::min(massive_combat_max_catch_up_ticks, converted);
  TickMetrics metrics;
  for (auto index = 0; index < ticks; ++index) {
    battle.pending_seconds -= massive_combat_tick_seconds;
    step(battle, metrics, storage_->hostility);
  }
  if (battle.pending_seconds < 1e-9)
    battle.pending_seconds = 0;
  MassiveCombatMetrics result;
  result.tick = battle.tick;
  for (const auto &formation : battle.formations) {
    if (!formation.active())
      continue;
    ++result.active_formations;
    result.active_ships += formation.active_ship_count();
  }
  result.spatial_cells = metrics.cells;
  result.target_candidates_examined = metrics.candidates;
  result.weapon_groups_resolved = metrics.weapon_groups;
  result.events_retained = static_cast<int>(battle.events.size());
  return result;
}

} // namespace stellar::core
