#pragma once

#include <stellar/core/massive_combat_engine.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stellar::core {

struct MassiveObservedCombatEvent {
  std::int64_t sequence{};
  std::int64_t tick{};
  MassiveCombatEventType type{};
  std::optional<int> actor_civilization_id;
  std::optional<std::int64_t> actor_formation_id;
  std::optional<int> target_civilization_id;
  std::optional<std::int64_t> target_formation_id;
  std::optional<int> magnitude;
  std::optional<MassivePoint> position;
  std::string message;
  bool details_known{};
  std::optional<MassivePoint> impact_position;
};

struct MassiveObservedCohort {
  std::int64_t cohort_id{};
  std::string display_class;
  int count_low{};
  int count_high{};
  bool identified{};
};

struct MassiveObservedVessel {
  std::int64_t vessel_id{};
  std::string display_name;
  std::string design_id;
  std::optional<float> combat_power;
  bool is_flagship{};
  bool is_carrier{};
  bool is_interdictor{};
  bool is_critically_damaged{};
};

struct MassiveObservedFormation {
  std::int64_t formation_id{};
  int civilization_id{};
  std::string display_name;
  MassivePoint position;
  MassivePoint velocity;
  MassiveFormationShape shape{};
  int ship_count_low{};
  int ship_count_high{};
  std::optional<float> strength_low;
  std::optional<float> strength_high;
  bool is_exact{};
  bool is_interdicting{};
  bool is_warp_blocked{};
  float warp_spool_progress{};
  std::optional<float> per_ship_combat_power;
  std::vector<MassiveObservedCohort> cohorts;
  std::vector<MassiveObservedVessel> important_vessels;
  float heading_radians{};
};

struct MassiveObservedMissileSalvo {
  std::int64_t salvo_id{};
  std::optional<std::int64_t> source_formation_id;
  std::optional<MassivePoint> source_position;
  std::optional<std::int64_t> target_formation_id;
  std::optional<MassivePoint> target_position;
  std::optional<MassivePoint> current_position;
  float remaining_seconds{};
  std::optional<float> progress_01;
  std::optional<int> count_low;
  std::optional<int> count_high;
  bool incoming_to_own{};
};

struct MassiveCombatSnapshot {
  std::array<std::uint8_t, 16> battle_id{};
  std::int64_t tick{};
  double simulated_seconds{};
  int exact_own_ships{};
  std::vector<MassiveObservedFormation> formations;
  std::vector<MassiveObservedCombatEvent> events;
  std::vector<MassiveObservedMissileSalvo> active_missile_salvos;
};

struct MassiveCombatSensorView {
  std::function<float(int, std::int64_t)> confidence;
  std::function<bool(int, std::int64_t)> identifies_cohorts;
  std::function<bool(int, std::int64_t)> identifies_important_vessels;
  std::function<bool(int, std::int64_t)> can_estimate_combat_power;
};

[[nodiscard]] MassiveCombatSnapshot build_massive_combat_snapshot(
    const MassiveCombatBattleState &battle, int observer_civilization_id,
    const MassiveCombatSensorView &sensors);

[[nodiscard]] std::vector<MassiveCombatOrder>
decide_massive_combat_doctrine(const MassiveCombatSnapshot &snapshot,
                               int civilization_id);

} // namespace stellar::core
