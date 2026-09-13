#pragma once

#include <stellar/core/fleet_role.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
struct FleetState;
enum class MilitaryOrderType { Hold, Defend, Attack, Retreat };
enum class MassiveWeaponKind {
  Beam,
  Kinetic,
  Missile,
  PointDefense,
  ElectronicWarfare
};
enum class MassiveModuleKind {
  Reactor,
  Engine,
  Sensor,
  WarpDrive,
  WeaponControl,
  PointDefense,
  ElectronicWarfare,
  WarpInterdictor
};
struct CombatProfileDefinition {
  std::string id;
  double max_shields{}, max_armor{}, max_hull{}, weapon_damage{},
      weapon_interval_days{}, retreat_delay_days{};
  bool has_weapon() const;
  double sustained_damage_per_day() const;
};
struct FleetCombatState {
  std::string profile_id;
  double shields{}, armor{}, hull{}, weapon_cooldown_remaining_days{};
  MilitaryOrderType order{MilitaryOrderType::Hold};
  std::optional<int> target_fleet_id, defend_system_id;
  double retreat_progress_days{};
  bool retreat_started{}, is_disengaged{};
  std::optional<int> disengaged_system_id;
};
struct MassiveWeaponGroup {
  std::string id;
  MassiveWeaponKind kind{};
  int mounts_per_ship{1};
  float damage_per_shot{8}, shots_per_second{1}, range{650}, accuracy{.65f},
      power_per_second{3}, heat_per_second{2};
  void validate() const;
};
struct MassiveModuleState {
  std::string id;
  MassiveModuleKind kind{};
  int installed_count{1};
  float mass_each{}, power_per_second_each{}, heat_per_second_each{},
      condition{1};
  bool enabled{true};
  float effective_range{}, field_strength{}, detection_signature{};
  int slots{1};
  void validate() const;
};
struct MassiveCombatLoadout {
  float mass_per_ship{100}, acceleration{18}, maximum_speed{120},
      shield_per_ship{35}, armor_per_ship{45}, hull_per_ship{95},
      reactor_output_per_ship{100}, cooling_per_ship{28},
      warp_stabilization{50}, warp_spool_seconds{12};
  int module_slot_capacity{12};
  float maximum_module_mass{420};
  std::vector<MassiveWeaponGroup> weapons;
  std::vector<MassiveModuleState> modules;
  void validate() const;
};
struct MassiveVesselState {
  std::int64_t id{};
  std::string name, design_id;
  bool is_flagship{}, is_carrier{}, is_interdictor{}, is_story_ship{};
  float hull_fraction{1}, engine_fraction{1}, sensor_fraction{1},
      warp_drive_fraction{1}, reactor_fraction{1}, interdictor_fraction{1};
  int battles_fought{}, confirmed_kills{};
  bool destroyed{}, escaped{};
  void validate() const;
};
std::span<const CombatProfileDefinition> combat_profile_catalog();
const CombatProfileDefinition *find_combat_profile(std::string_view id);
const CombatProfileDefinition &get_combat_profile(std::string_view id);
std::string_view default_combat_profile_id(FleetRole role);
FleetCombatState
create_initial_fleet_combat_state(std::optional<std::string_view> profile_id,
                                  FleetRole role);
FleetCombatState &ensure_fleet_combat_state(FleetState &fleet);
MassiveCombatLoadout
massive_loadout_from_legacy(const CombatProfileDefinition &profile);
MassiveModuleState warp_interdictor(float range = 900, float strength = 72);
} // namespace stellar::core
