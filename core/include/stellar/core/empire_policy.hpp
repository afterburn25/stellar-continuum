#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct FreshCampaignState;

// Empire-level policy catalog: one active stance per domain per civilization.
// Effects are multiplicative factors consumed by authoritative tick paths —
// the UI previews them but never applies them itself.
enum class EmpirePolicyDomain { economy, military, research, frontier };

struct EmpirePolicyEffects {
  double industry_factor{1.0};
  double credit_factor{1.0};
  double science_factor{1.0};
  double shipbuilding_factor{1.0};
  double expansion_factor{1.0};
  double war_exhaustion_factor{1.0};
};

struct EmpirePolicyDefinition {
  std::string_view id;
  std::string_view display_name;
  EmpirePolicyDomain domain;
  EmpirePolicyEffects effects;
  std::string_view summary;
};

[[nodiscard]] std::span<const EmpirePolicyDefinition> empire_policy_catalog();
[[nodiscard]] const EmpirePolicyDefinition *find_empire_policy(
    std::string_view policy_id) noexcept;
[[nodiscard]] const EmpirePolicyDefinition *
default_empire_policy(EmpirePolicyDomain domain) noexcept;

// Minimum ticks between policy changes in one domain (~30 simulation days at
// the diplomacy clock of 1000 ticks/day).
inline constexpr std::int64_t empire_policy_change_cooldown_ticks = 30000;

struct EmpirePolicyAssignment {
  EmpirePolicyDomain domain{};
  std::string policy_id;
  std::int64_t changed_at_tick{};
};
struct EmpirePolicyState {
  int civilization_id{};
  std::vector<EmpirePolicyAssignment> assignments;
};

// Combined multiplicative effects for a civilization; missing assignments
// resolve to the domain default. Deterministic and allocation-free callers can
// rely on span input.
[[nodiscard]] EmpirePolicyEffects
empire_policy_effects(std::span<const EmpirePolicyState> states,
                      int civilization_id) noexcept;
[[nodiscard]] const EmpirePolicyDefinition *
active_empire_policy(const EmpirePolicyState &state,
                     EmpirePolicyDomain domain) noexcept;

struct EmpirePolicyCommandResult {
  bool accepted{};
  std::string message;
};

// Authoritative policy change: validates civilization, policy id and the
// per-domain change cooldown. Missing state entries materialize on first
// change so campaigns that never diverge from defaults persist nothing.
// `tick` is the diplomacy-clock tick of the issuing campaign step.
[[nodiscard]] EmpirePolicyCommandResult
set_empire_policy(FreshCampaignState &campaign, int civilization_id,
                  std::string_view policy_id, std::int64_t tick);

// Additive persistence tail on the galaxy payload; absent in older saves.
struct EmpirePolicySaveDto {
  int civilization_id{};
  std::string policy_id;
  std::int64_t changed_at_tick{};
};
struct Civilization;
[[nodiscard]] std::vector<EmpirePolicySaveDto>
capture_empire_policies(std::span<const EmpirePolicyState> states);
[[nodiscard]] std::vector<EmpirePolicyState>
restore_empire_policies(std::span<const EmpirePolicySaveDto> source,
                        std::span<const Civilization> civilizations);

} // namespace stellar::core
