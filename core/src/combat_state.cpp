#include <stellar/core/combat_state.hpp>
#include <stellar/core/fleet_state.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace stellar::core {
namespace {
const std::array<CombatProfileDefinition, 4> profiles{{
    {"civilian_light_v1", 0, 10, 45, 0, 1, .75},
    {"civilian_science_v1", 0, 14, 60, 0, 1, 1},
    {"civilian_heavy_v1", 0, 24, 105, 0, 1, 1.5},
    {"patrol_corvette_mk1", 35, 45, 95, 28, .75, 1.5},
}};
bool finite_nonnegative(float value) {
  return std::isfinite(value) && value >= 0;
}
bool utf8_whitespace(std::string_view &value) {
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
bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!utf8_whitespace(value))
      return false;
  return true;
}
double nonnegative_preserving_nan(double value) {
  return std::isnan(value) ? value : std::max(0.0, value);
}
float max_preserving_nan(float left, float right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<float>::quiet_NaN()
             : std::max(left, right);
}
std::int32_t unchecked_multiply(std::int32_t left, std::int32_t right) {
  const auto product =
      static_cast<std::uint32_t>(left) * static_cast<std::uint32_t>(right);
  return static_cast<std::int32_t>(product);
}
std::int32_t checked_add(std::int32_t left, std::int32_t right) {
  const auto result = static_cast<std::int64_t>(left) + right;
  if (result < std::numeric_limits<std::int32_t>::min() ||
      result > std::numeric_limits<std::int32_t>::max())
    throw std::overflow_error("Arithmetic operation resulted in an overflow.");
  return static_cast<std::int32_t>(result);
}
} // namespace
bool CombatProfileDefinition::has_weapon() const {
  return weapon_damage > 0 && weapon_interval_days > 0;
}
double CombatProfileDefinition::sustained_damage_per_day() const {
  return has_weapon() ? weapon_damage / weapon_interval_days : 0;
}
std::span<const CombatProfileDefinition> combat_profile_catalog() {
  return profiles;
}
const CombatProfileDefinition *find_combat_profile(std::string_view id) {
  const auto item =
      std::find_if(profiles.begin(), profiles.end(),
                   [=](const auto &profile) { return profile.id == id; });
  return item == profiles.end() ? nullptr : &*item;
}
const CombatProfileDefinition &get_combat_profile(std::string_view id) {
  const auto *profile = find_combat_profile(id);
  if (!profile)
    throw std::out_of_range("The given key '" + std::string(id) +
                            "' was not present in the dictionary.");
  return *profile;
}
std::string_view default_combat_profile_id(FleetRole role) {
  switch (role) {
  case FleetRole::Military:
    return "patrol_corvette_mk1";
  case FleetRole::Science:
    return "civilian_science_v1";
  case FleetRole::Colony:
    return "civilian_heavy_v1";
  default:
    return "civilian_light_v1";
  }
}
FleetCombatState
create_initial_fleet_combat_state(std::optional<std::string_view> id,
                                  FleetRole role) {
  const auto *profile = id && !id->empty() ? find_combat_profile(*id) : nullptr;
  if (!profile)
    profile = &get_combat_profile(default_combat_profile_id(role));
  return {profile->id, profile->max_shields, profile->max_armor,
          profile->max_hull};
}
FleetCombatState &ensure_fleet_combat_state(FleetState &fleet) {
  if (!fleet.combat || !find_combat_profile(fleet.combat->profile_id)) {
    fleet.combat = create_initial_fleet_combat_state(std::nullopt, fleet.role);
    return *fleet.combat;
  }
  auto &state = *fleet.combat;
  const auto &profile = get_combat_profile(state.profile_id);
  state.shields = std::clamp(state.shields, 0.0, profile.max_shields);
  state.armor = std::clamp(state.armor, 0.0, profile.max_armor);
  state.hull = std::clamp(state.hull, 0.0, profile.max_hull);
  state.weapon_cooldown_remaining_days =
      nonnegative_preserving_nan(state.weapon_cooldown_remaining_days);
  state.retreat_progress_days =
      nonnegative_preserving_nan(state.retreat_progress_days);
  if (static_cast<int>(state.order) < 0 || static_cast<int>(state.order) > 3)
    state.order = MilitaryOrderType::Hold;
  if (state.is_disengaged &&
      state.disengaged_system_id != fleet.current_system_id) {
    state.is_disengaged = false;
    state.disengaged_system_id.reset();
  }
  return state;
}
void MassiveWeaponGroup::validate() const {
  if (blank(id) || mounts_per_ship < 0 || static_cast<int>(kind) < 0 ||
      static_cast<int>(kind) > 4)
    throw std::invalid_argument("Weapon group identity is invalid.");
  for (float value : {damage_per_shot, shots_per_second, range, accuracy,
                      power_per_second, heat_per_second})
    if (!finite_nonnegative(value))
      throw std::invalid_argument("Weapon group scalar is invalid.");
}
void MassiveModuleState::validate() const {
  if (blank(id) || installed_count < 0 || slots < 0 ||
      static_cast<int>(kind) < 0 || static_cast<int>(kind) > 7 ||
      !std::isfinite(condition) || condition < 0 || condition > 1)
    throw std::invalid_argument("Module identity or condition is invalid.");
  for (float value : {mass_each, power_per_second_each, heat_per_second_each,
                      effective_range, field_strength, detection_signature})
    if (!finite_nonnegative(value))
      throw std::invalid_argument("Module scalar is invalid.");
  if (effective_range > 2000)
    throw std::invalid_argument(
        "Module range exceeds the bounded combat spatial search.");
}
void MassiveCombatLoadout::validate() const {
  for (float value :
       {mass_per_ship, acceleration, maximum_speed, shield_per_ship,
        armor_per_ship, hull_per_ship, reactor_output_per_ship,
        cooling_per_ship, warp_stabilization, warp_spool_seconds})
    if (!finite_nonnegative(value))
      throw std::invalid_argument("Combat loadout contains an invalid scalar.");
  if (hull_per_ship <= 0 || warp_spool_seconds <= 0 ||
      module_slot_capacity < 0 || maximum_module_mass < 0 ||
      weapons.size() > 32 || modules.size() > 32)
    throw std::invalid_argument("Combat loadout bounds are invalid.");
  for (const auto &weapon : weapons)
    weapon.validate();
  for (const auto &module : modules)
    module.validate();
  std::int32_t slots = 0;
  double mass_sum = 0;
  for (const auto &module : modules) {
    slots = checked_add(
        slots, unchecked_multiply(module.slots, module.installed_count));
    const auto product = module.mass_each * module.installed_count;
    mass_sum += static_cast<double>(product);
  }
  const auto mass = static_cast<float>(mass_sum);
  if (slots > module_slot_capacity || mass > maximum_module_mass)
    throw std::invalid_argument(
        "Installed combat modules exceed the design's slot or mass budget.");
}
void MassiveVesselState::validate() const {
  if (id <= 0 || blank(name) || blank(design_id))
    throw std::invalid_argument("Important vessel identity is invalid.");
  for (float value :
       {hull_fraction, engine_fraction, sensor_fraction, warp_drive_fraction,
        reactor_fraction, interdictor_fraction})
    if (!std::isfinite(value) || value < 0 || value > 1)
      throw std::invalid_argument("Important vessel condition is invalid.");
}
MassiveCombatLoadout
massive_loadout_from_legacy(const CombatProfileDefinition &profile) {
  MassiveCombatLoadout result;
  result.shield_per_ship = static_cast<float>(profile.max_shields);
  result.armor_per_ship = static_cast<float>(profile.max_armor);
  result.hull_per_ship = static_cast<float>(profile.max_hull);
  result.warp_spool_seconds = max_preserving_nan(
      4.f, static_cast<float>(profile.retreat_delay_days) * 8);
  result.modules.push_back({"reactor", MassiveModuleKind::Reactor, 1, 18, 0, 0,
                            1, true, 0, 100, 0, 1});
  result.modules.push_back(
      {"warp_drive", MassiveModuleKind::WarpDrive, 1, 22, 12});
  if (profile.has_weapon())
    result.weapons.push_back(
        {"beam_battery", MassiveWeaponKind::Beam, 1,
         static_cast<float>(profile.weapon_damage),
         1.f / std::max(.1f,
                        static_cast<float>(profile.weapon_interval_days) * 8),
         700});
  return result;
}
MassiveModuleState warp_interdictor(float range, float strength) {
  return {"warp_interdiction_array",
          MassiveModuleKind::WarpInterdictor,
          1,
          85,
          42,
          18,
          1,
          true,
          range,
          strength,
          85,
          1};
}
} // namespace stellar::core
