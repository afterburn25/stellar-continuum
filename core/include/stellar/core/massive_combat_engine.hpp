#pragma once

#include <stellar/core/combat_simulation.hpp>
#include <stellar/core/massive_combat_persistence.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace stellar::core {

struct MassiveCombatOrder {
  std::int64_t formation_id{};
  MassiveCombatOrderType type{};
  std::optional<std::int64_t> target_formation_id;
  std::optional<MassivePoint> objective;
  std::optional<MassiveFormationShape> shape;
};

struct MassiveCombatOrderResult {
  bool accepted{};
  std::string message;
};

struct MassiveCombatMetrics {
  std::int64_t tick{};
  int active_formations{};
  int active_ships{};
  int spatial_cells{};
  int target_candidates_examined{};
  int weapon_groups_resolved{};
  int events_retained{};
};

class MassiveCombatArgumentRangeError final : public std::out_of_range {
public:
  MassiveCombatArgumentRangeError(std::string message, std::string parameter);
  [[nodiscard]] const std::string &parameter() const noexcept;

private:
  std::string parameter_;
};

inline constexpr double massive_combat_tick_seconds = .1;
inline constexpr float massive_combat_spatial_cell_size = 500.0F;

class MassiveCombatClock final {
public:
  [[nodiscard]] static std::span<const double> allowed_speeds() noexcept;
  [[nodiscard]] double speed_multiplier() const noexcept;
  void set_speed(double multiplier);
  [[nodiscard]] double accept_frame(double real_delta_seconds,
                                    double maximum_frame_seconds = .25) const;

private:
  double speed_multiplier_{1};
};

class MassiveCombatEngine final {
public:
  // The callable is owned. Any references captured by it must outlive the
  // engine and remain at stable addresses for every engine call.
  explicit MassiveCombatEngine(CombatHostilityView hostility = {});
  ~MassiveCombatEngine();
  MassiveCombatEngine(MassiveCombatEngine &&) noexcept;
  MassiveCombatEngine &operator=(MassiveCombatEngine &&) noexcept;
  MassiveCombatEngine(const MassiveCombatEngine &) = delete;
  MassiveCombatEngine &operator=(const MassiveCombatEngine &) = delete;

  [[nodiscard]] bool
  has_active_hostilities(const MassiveCombatBattleState &battle) const;
  [[nodiscard]] MassiveCombatOrderResult
  issue_order(MassiveCombatBattleState &battle, int civilization_id,
              MassiveCombatOrder order) const;
  // Source LastMetrics is transient and JsonIgnored. Native returns the same
  // complete value without adding it to the durable battle state.
  [[nodiscard]] MassiveCombatMetrics
  advance(MassiveCombatBattleState &battle, double elapsed_seconds) const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
