#pragma once

#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/engine/history.hpp>

#include <cstdint>
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
//
// Actors carry involved civilization ids (u64). Fleet/system/body/entity
// references are preserved as tags ("fleet:12", "system:5", "body:3",
// "colony:9", "project:<id>", "design:<id>", "tech:<id>", "node:<id>").
// `location` is the system id where the event has one, else 0.
//
// Visibility defaults to the involved civilizations only — fog-of-war
// safe. Widening visibility to observers that know the location is
// knowledge-layer work on top of this adapter.
//
// Every record gets at_day = the step's absolute end day — the campaign
// clock, never wall time. Mapping is pure and deterministic: the same
// step result produces the same records.

// Converts one step's events into records (not yet appended; ids are
// assigned by EventHistory::record).
[[nodiscard]] std::vector<engine::HistoryEvent>
history_events_for_step(const IntegratedAdaptiveCampaignStepResult &step,
                        double end_day);

// Appends the step's events to `history`; returns assigned record ids in
// record order.
std::vector<std::uint64_t>
record_step_events(engine::EventHistory &history,
                   const IntegratedAdaptiveCampaignStepResult &step,
                   double end_day);

} // namespace stellar::core
