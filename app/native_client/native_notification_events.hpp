#pragma once

#include "native_campaign_feedback.hpp"
#include "native_notifications.hpp"
#include <stellar/core/diplomacy_state.hpp>
#include <unordered_set>

namespace stellar::native_notifications {

// Shares the already observer-filtered category summary with transient feedback.
// No raw simulation names/messages pass through this publication path.
void publish_campaign_notifications(NativeNotificationFeed&,
    const native_campaign_feedback::CampaignFeedbackSummary&, double day);

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
