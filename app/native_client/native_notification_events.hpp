#pragma once

#include "native_campaign_feedback.hpp"
#include "native_notifications.hpp"
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/engine/history.hpp>
#include <unordered_set>

namespace stellar::native_notifications {

// Shares the already observer-filtered category summary with transient feedback.
// No raw simulation names/messages pass through this publication path.
void publish_campaign_notifications(NativeNotificationFeed&,
    const native_campaign_feedback::CampaignFeedbackSummary&, double day);

// Seed-projection parameters: the significance floor keeps the feed to
// reports (not detection trivia) and the entry cap leaves headroom in the
// bounded feed for fresh transient reports. Exported so consumers and
// smoke checks can derive the expected seeded set without duplicating them.
inline constexpr double chronicle_seed_min_significance = 0.35;
inline constexpr std::size_t chronicle_seed_max_entries = 16;

// Seeds the bounded feed with the campaign's persisted strategic chronicle
// on admission — engine::EventHistory survives save/load while the
// transient feed does not, so after loading a campaign the panel still
// shows the player's recent recorded history. Entries carry their recorded
// campaign dates and summaries; audience filtering is EventHistory::feed()'s
// projection, never re-derived here. `max_entries` leaves headroom in the
// bounded feed for fresh transient reports.
void seed_chronicle_notifications(NativeNotificationFeed&,
    const engine::EventHistory&, int observer_civilization_id,
    std::size_t max_entries = chronicle_seed_max_entries);

// Consumes only Core's audience-filtered view. Retained history is seeded on
// campaign admission, never replayed as newly received reports after loading.
class NativeDiplomaticNotifications final {
 public:
  void seed(const core::DiplomaticStateView&);
  void harvest(NativeNotificationFeed&, const core::DiplomaticStateView&);
 private:
  int observer_{-1};
  std::unordered_set<std::int64_t> seen_;
};
} // namespace stellar::native_notifications
