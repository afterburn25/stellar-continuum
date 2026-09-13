#pragma once

#include <stellar/core/combat_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

enum class MassiveCombatOrderType {
  Engage,
  Hold,
  Defend,
  Advance,
  AdvanceCautiously,
  StandoffAttack,
  Screen,
  ProtectCriticalAsset,
  FocusFire,
  FlankLeft,
  FlankRight,
  Intercept,
  Pursue,
  BreakContact,
  Disengage,
  Retreat,
  EmergencyRetreat,
  Breakout,
  Surrender,
};

enum class MassiveFormationShape {
  Screen,
  Line,
  Wedge,
  Standoff,
  Dispersed,
  Escort,
  RetreatColumn,
  Breakout,
};

enum class InterdictorProtectionPolicy { Low, Standard, High, Absolute };
enum class MassiveCombatEventType {
  Engagement,
  BeamVolley,
  KineticVolley,
  MissileSalvo,
  MissileIntercepted,
  Damage,
  FormationDestroyed,
  WarpSpooling,
  WarpBlocked,
  Escaped,
  Surrendered,
  OrderChanged,
};

struct MassivePoint {
  float x{};
  float y{};
  [[nodiscard]] bool is_finite() const noexcept;
};

struct MassiveCombatEvent {
  std::int64_t sequence{};
  std::int64_t tick{};
  MassiveCombatEventType type{};
  int actor_civilization_id{};
  std::int64_t actor_formation_id{};
  std::optional<int> target_civilization_id;
  std::optional<std::int64_t> target_formation_id;
  int magnitude{};
  MassivePoint position;
  std::string message;
};

struct MassiveMissileSalvoState {
  std::int64_t id{};
  std::int64_t source_formation_id{};
  std::int64_t target_formation_id{};
  int missile_count{};
  float damage{};
  float remaining_seconds{};
  std::optional<MassivePoint> launch_position;
  float initial_flight_seconds{};
  [[nodiscard]] bool is_valid() const noexcept;
};

struct MassiveCohortState {
  std::int64_t id{};
  std::string design_id;
  int initial_count{};
  int active_count{};
  float experience{.5f};
};

struct MassiveFormationState {
  std::int64_t id{};
  int civilization_id{};
  int fleet_id{};
  int task_force_id{};
  std::string name;
  MassivePoint position;
  MassivePoint velocity;
  MassivePoint heading{1, 0};
  MassivePoint objective;
  MassiveFormationShape shape{MassiveFormationShape::Line};
  MassiveCombatOrderType order{MassiveCombatOrderType::Hold};
  std::optional<std::int64_t> target_formation_id;
  std::optional<std::int64_t> protected_formation_id;
  InterdictorProtectionPolicy interdictor_protection{
      InterdictorProtectionPolicy::Standard};
  float cohesion{1};
  float morale{1};
  float shield_pool{};
  float armor_pool{};
  float hull_pool{};
  float hull_loss_threshold_per_ship{};
  float heat{};
  float power_reserve{1};
  float warp_spool_progress{};
  bool warp_blocked{};
  bool escaped{};
  bool surrendered{};
  int initial_ship_count{};
  int destroyed_ships{};
  float hull_damage_remainder{};
  MassiveCombatLoadout loadout;
  std::vector<MassiveCohortState> cohorts;
  std::vector<MassiveVesselState> important_vessels;

  [[nodiscard]] int surviving_ship_count() const;
  [[nodiscard]] int active_ship_count() const;
  [[nodiscard]] bool active() const;
};

struct MassiveCombatBattleState {
  // Bytes use the ordering returned by System.Guid.ToByteArray().
  std::array<std::uint8_t, 16> battle_id{};
  std::uint64_t seed{1};
  std::int64_t tick{};
  double simulated_seconds{};
  double pending_seconds{};
  std::int64_t next_event_sequence{1};
  std::int64_t next_salvo_id{1};
  std::vector<MassiveFormationState> formations;
  std::vector<MassiveCombatEvent> events;
  std::vector<MassiveMissileSalvoState> active_salvos;

  [[nodiscard]] bool is_complete() const;
};

struct CampaignCombatBinding {
  int fleet_id{};
  std::int64_t formation_id{};
};

struct CampaignCombatEngagement {
  std::int64_t first_formation_id{};
  std::int64_t second_formation_id{};
};

struct CampaignMassiveEncounter {
  int system_id{};
  double started_day{};
  MassiveCombatBattleState battle;
  std::vector<CampaignCombatBinding> vessels;
  std::vector<CampaignCombatEngagement> engaged_formation_pairs;
  std::int64_t last_observed_event_sequence{};
  bool reconciled{};
};

struct CampaignMassiveEncounterWorldView {
  std::span<const StellarSystem> systems;
  std::span<const FleetState> fleets;
};

class MassiveCombatStateError final : public std::runtime_error {
public:
  explicit MassiveCombatStateError(std::string message);
};
class CampaignMassiveEncounterDataError final : public std::runtime_error {
public:
  explicit CampaignMassiveEncounterDataError(std::string message);
};
class CampaignMassiveEncounterArgumentError final : public std::invalid_argument {
public:
  explicit CampaignMassiveEncounterArgumentError(std::string message);
};

inline constexpr int massive_combat_max_ships = 200'000;
inline constexpr int massive_combat_max_formations = 4'096;
inline constexpr int massive_combat_max_cohorts_per_formation = 128;
inline constexpr int massive_combat_max_important_vessels_per_formation = 256;
inline constexpr int massive_combat_max_retained_events = 256;
inline constexpr int massive_combat_max_events_per_tick = 32;
inline constexpr int massive_combat_max_active_salvos = 256;
inline constexpr int massive_combat_max_catch_up_ticks = 600;
inline constexpr int campaign_massive_max_engagement_evidence = 65'536;

// Matches source Validate, including legacy InitialShipCount materialization.
void validate_massive_combat_battle(MassiveCombatBattleState &battle);
void validate_campaign_massive_encounter(
    CampaignMassiveEncounter &encounter,
    CampaignMassiveEncounterWorldView world);

[[nodiscard]] MassiveCombatBattleState
clone_massive_combat_battle(const MassiveCombatBattleState &source);
[[nodiscard]] CampaignMassiveEncounter
clone_campaign_massive_encounter(const CampaignMassiveEncounter &source);

} // namespace stellar::core
