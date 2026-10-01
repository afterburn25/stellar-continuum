#pragma once

#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/engine/history.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace stellar::core {

// Campaign event history adapter — the authoritative Core consumer of
// engine::EventHistory (SPACE_STRATEGY_ENGINE milestone 14/15).
//
// Maps the discrete event streams emitted by one authoritative strategic
// step (IntegratedAdaptiveCampaignStepResult) onto HistoryEvent records
// with a stable category vocabulary, actor/location/significance fields
// and involved-party visibility. Aggregate phase counters (sensor
// contact recordings, diplomacy maintenance counts) are not discrete
// happenings and are intentionally not recorded.
//
// Category vocabulary (query-stable):
//   "construction.project"      — colony/megaproject construction events
//   "shipbuilding.ship"         — ship construction completions
//   "research.legacy"           — legacy research completions
//   "research.adaptive"         — adaptive research events (outcome vs progress)
//   "exploration.<type>"        — e.g. exploration.system_surveyed,
//                                 exploration.first_contact
//   "war.<type>"                — e.g. war.engagement_started,
//                                 war.fleet_destroyed
//   "colony.founded"            — colony/outpost establishment
//   "diplomacy.<kind>"          — e.g. diplomacy.war_declared,
//                                 diplomacy.agreement_activated
//
// Actors carry involved civilization ids (u64). Fleet/system/body/entity
// references are preserved as tags ("fleet:12", "system:5", "body:3",
// "colony:9", "project:<id>", "design:<id>", "tech:<id>", "node:<id>").
// `location` is the system id where the event has one, else 0.
//
// Visibility: involved civilizations always see their events; events
// located at a system are additionally visible to civilizations that
// know that system (see widen_history_visibility). Locationless events
// stay involved-party-only. Diplomatic journal entries carry their own
// authoritative audience (known_to_civilization_ids) which is used
// verbatim — diplomacy already decided who knows.
//
// Step events get at_day = the step's absolute end day — the campaign
// clock, never wall time; diplomatic entries keep their own journal
// timestamp (ticks are campaign-milli-days). Mapping is pure and
// deterministic: the same step result produces the same records.
//
// The runtime watermarks the diplomatic journal
// (DiplomacyState::history_events_since) so each entry is recorded
// exactly once across advances and save/load.

// Converts one step's events into records (not yet appended; ids are
// assigned by EventHistory::record).
[[nodiscard]] std::vector<engine::HistoryEvent>
history_events_for_step(const IntegratedAdaptiveCampaignStepResult &step,
                        double end_day,
                        std::span<const DiplomaticHistoryEventSnapshot>
                            diplomacy_events = {});

// Widens `visible_to` on located events: every civilization that knows
// the event's system (CivilizationKnowledgeState::is_system_known) may
// see it — major happenings in known space propagate as news. Events
// with no location (research, construction, shipbuilding) stay
// involved-party-only. Order and content are deterministic given the
// same inputs.
void widen_history_visibility(std::vector<engine::HistoryEvent> &events,
                              const FreshCampaignState &campaign);

// Appends the step's events to `history`; returns assigned record ids in
// record order.
std::vector<std::uint64_t>
record_step_events(engine::EventHistory &history,
                   const IntegratedAdaptiveCampaignStepResult &step,
                   double end_day);

// Same, with knowledge-based visibility widening applied — the
// authoritative path used by the campaign runtime. `diplomacy_events`
// is the step's new diplomatic journal slice (the runtime watermarks
// it against the authoritative journal).
std::vector<std::uint64_t>
record_step_events(engine::EventHistory &history,
                   const IntegratedAdaptiveCampaignStepResult &step,
                   double end_day, const FreshCampaignState &campaign,
                   std::span<const DiplomaticHistoryEventSnapshot>
                       diplomacy_events = {});

// Chronicle retention policy, applied after each recorded step. When
// the chronicle nears capacity (>= 90%), routine records older than
// `chronicle_prune_horizon_days` are pruned so the bounded capacity
// eviction (oldest-first pop) does not silently discard major events.
// `chronicle_report_significance` matches the news-report vocabulary
// floor — anything a feed could ever surface survives pruning.
// Returns the pruned count (0 when under the trigger or nothing to
// prune). Deterministic given the same history contents.
inline constexpr double chronicle_prune_horizon_days = 365.0;
inline constexpr double chronicle_report_significance = 0.35;
std::size_t maintain_chronicle(engine::EventHistory &history,
                               double current_day);

} // namespace stellar::core
