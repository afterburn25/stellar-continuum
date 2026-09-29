#pragma once

#include <stellar/core/civilization_control.hpp>
#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/fleet_power_observation.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace stellar::core {

struct Colony;
struct Civilization;
class InterstellarLaneNetwork;

// Read-mostly world view for autonomous warfare decisions. fleets and lanes are
// mutable because canonical routing writes transit fields and the lane graph is
// a lazily-built cache. No state is retained between advance() calls, so the
// advisor introduces no new persistence surface.
struct WarfareWorldView {
  std::span<const StellarSystem> systems;
  std::span<const Civilization> civilizations;
  std::span<const Colony> colonies;
  std::span<FleetState> fleets;
  std::span<const FleetPowerObservation> combat_intelligence;
  InterstellarLaneNetwork &lanes;
  CivilizationControlQuery control;
};

struct WarfareStepResult {
  int trespasses_recorded{};
  int wars_declared{};
  int belligerent_contacts_reacquired{};
  int communications_established{};
  int peace_offers_sent{};
  int peace_offers_accepted{};
  int peace_offers_rejected{};
  int engagement_orders{};
  int deployment_orders{};
};

// Autonomous warfare advisor: turns diplomacy knowledge, fleet power
// observations and territorial adjacency into canonical commands. War
// declarations go through ObserverDiplomacyCommandService, attacks through
// CombatCommandRuntime and power projection through assign_fleet_route — the
// advisor owns no simulation rules of its own.
class WarfareCoordinator {
public:
  // Colony systems within this distance of a counterpart colony system count
  // as a shared border for territorial friction. Chosen just under the
  // default fleet sensor range (135 ly): a foreign colony inside a fleet's
  // sensor shadow is visibly pressing on the frontier.
  static constexpr double shared_border_light_years = 120.0;
  // Each civilization reviews offensive war options at most once per interval
  // (diplomacy ticks, 1000 per simulation day = ~7 days), phase-shifted by id
  // so declarations spread across the interval deterministically.
  static constexpr std::int64_t review_interval_ticks = 7000;
  // Confidence assigned when a counterpart's fleets have never been observed.
  // The neutral-strength prior is expressed through the estimate bounds; the
  // low confidence inflates the perceived threat in StrategicDecisionEvaluator
  // so unobserved opponents are treated cautiously, not as free victories.
  static constexpr double unobserved_strength_confidence = 0.10;
  // High bound of the neutral prior as a multiple of own armed strength.
  static constexpr double unobserved_strength_multiplier = 1.5;
  // War duration (diplomacy ticks, 1000/day) at which war weariness saturates
  // for peace evaluation — ten years. When the declaration scrolled off the
  // bounded diplomacy journal the war counts as fully wearisome.
  static constexpr std::int64_t war_weariness_full_ticks = 3650 * 1000;
  // After a counterpart rejects a peace or ceasefire offer the civilization
  // waits this long before offering again.
  static constexpr std::int64_t peace_offer_cooldown_ticks =
      review_interval_ticks * 4;

  WarfareStepResult advance(WarfareWorldView world,
                            DiplomacyCampaignRuntimeCoordinator &diplomacy,
                            std::int64_t diplomacy_tick,
                            std::int64_t elapsed_ticks);
};

} // namespace stellar::core
