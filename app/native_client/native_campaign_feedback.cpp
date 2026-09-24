#include "native_campaign_feedback.hpp"

#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace stellar::native_campaign_feedback {
namespace {
using namespace stellar::core;
using namespace stellar::native_map;

constexpr Color panel_fill{8, 22, 41, 235};
constexpr Color panel_stroke{102, 180, 211, 235};
constexpr Color text_color{235, 245, 255, 255};

[[nodiscard]] constexpr std::size_t index(FeedbackKind kind) noexcept {
  return static_cast<std::size_t>(kind);
}

void saturating_add(std::uint32_t& target, std::uint32_t value) noexcept {
  const auto room = std::numeric_limits<std::uint32_t>::max() - target;
  target += std::min(room, value);
}

[[nodiscard]] bool belongs_to(const CombatEvent& event, int observer) noexcept {
  return event.actor_civilization_id == observer ||
         (event.target_civilization_id &&
          *event.target_civilization_id == observer);
}

void collect_step(CampaignFeedbackSummary& summary,
                  const IntegratedAdaptiveCampaignStepResult& step,
                  int observer) noexcept {
  for (const auto& event : step.research_events)
    if (event.civilization_id == observer)
      saturating_add(summary.counts[index(FeedbackKind::ResearchReport)], 1);
  for (const auto& event : step.core.construction_events)
    if (event.civilization_id == observer)
      saturating_add(summary.counts[index(FeedbackKind::ConstructionComplete)], 1);
  for (const auto& event : step.core.shipbuilding_events)
    if (event.civilization_id == observer)
      saturating_add(summary.counts[index(FeedbackKind::ShipComplete)], 1);
  for (const auto& event : step.core.exploration_events) {
    if (event.civilization_id != observer) continue;
    if (event.type == ExplorationEventType::SystemSurveyed)
      saturating_add(summary.counts[index(FeedbackKind::SurveyComplete)], 1);
    else if (event.type == ExplorationEventType::SensorContact ||
             event.type == ExplorationEventType::FirstContact)
      saturating_add(summary.counts[index(FeedbackKind::ContactDetected)], 1);
  }
  // ColonizationEvent is emitted by ColonizationSimulation only after its
  // establishment branch has created the colony/outpost and consumed the fleet.
  for (const auto& event : step.core.colonization_events)
    if (event.civilization_id == observer)
      saturating_add(summary.counts[index(FeedbackKind::ColonyFounded)], 1);
  for (const auto& event : step.core.combat_events)
    if (belongs_to(event, observer) &&
        (event.type == CombatEventType::EngagementStarted ||
         event.type == CombatEventType::FleetDestroyed ||
         event.type == CombatEventType::FleetRetreatInitiated))
      saturating_add(summary.counts[index(FeedbackKind::CombatAlert)], 1);
}

[[nodiscard]] const char* notice_label(FeedbackKind kind) noexcept {
  switch (kind) {
  case FeedbackKind::ResearchReport: return "Research report available";
  case FeedbackKind::ConstructionComplete: return "Construction complete";
  case FeedbackKind::ShipComplete: return "Ship complete";
  case FeedbackKind::SurveyComplete: return "Survey complete";
  case FeedbackKind::ContactDetected: return "Contact detected";
  case FeedbackKind::ColonyFounded: return "Settlement established";
  case FeedbackKind::CombatAlert: return "Combat alert";
  case FeedbackKind::Count: break;
  }
  return "Update available";
}

[[nodiscard]] const char* notice_key(FeedbackKind kind) noexcept {
  switch (kind) {
  case FeedbackKind::ResearchReport: return "FEEDBACK_RESEARCH";
  case FeedbackKind::ConstructionComplete: return "FEEDBACK_CONSTRUCTION";
  case FeedbackKind::ShipComplete: return "FEEDBACK_SHIP";
  case FeedbackKind::SurveyComplete: return "FEEDBACK_SURVEY";
  case FeedbackKind::ContactDetected: return "FEEDBACK_CONTACT";
  case FeedbackKind::ColonyFounded: return "FEEDBACK_COLONY";
  case FeedbackKind::CombatAlert: return "FEEDBACK_COMBAT";
  case FeedbackKind::Count: break;
  }
  return "FEEDBACK_UPDATE";
}

[[nodiscard]] float scale_for(int width, int height) noexcept {
  const auto safe_width = static_cast<float>(std::max(1, width));
  const auto safe_height = static_cast<float>(std::max(1, height));
  return std::clamp(std::min(safe_width / 1280.f, safe_height / 720.f), .72f, 2.4f);
}
} // namespace

std::uint32_t CampaignFeedbackSummary::count(FeedbackKind kind) const noexcept {
  const auto value = index(kind);
  return value < counts.size() ? counts[value] : 0;
}

bool CampaignFeedbackSummary::empty() const noexcept {
  return std::all_of(counts.begin(), counts.end(),
                     [](std::uint32_t value) { return value == 0; });
}

CampaignFeedbackSummary collect_campaign_feedback(
    const CampaignFrameResult& frame, int observer_civilization_id) noexcept {
  CampaignFeedbackSummary summary;
  if (observer_civilization_id < 0) return summary;
  for (const auto& step : frame.strategic_results)
    collect_step(summary, step, observer_civilization_id);
  for (const auto& event : frame.tactical_events)
    if (belongs_to(event, observer_civilization_id) &&
        (event.type == CombatEventType::EngagementStarted ||
         event.type == CombatEventType::FleetDestroyed ||
         event.type == CombatEventType::FleetRetreatInitiated))
      saturating_add(summary.counts[index(FeedbackKind::CombatAlert)], 1);
  return summary;
}

std::string NativeCampaignFeedback::tr(std::string_view key,
                                       std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

void NativeCampaignFeedback::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native campaign feedback must be used on its owner thread.");
}

void NativeCampaignFeedback::publish(const CampaignFeedbackSummary& summary) {
  require_owner();
  for (std::size_t kind = 0; kind < feedback_kind_count; ++kind) {
    const auto incoming = summary.counts[kind];
    if (incoming == 0) continue;
    auto found = notice_count_;
    for (std::size_t i = 0; i < notice_count_; ++i)
      if (index(notices_[i].kind) == kind) { found = i; break; }
    if (found == notice_count_) {
      if (notice_count_ >= maximum_notices) continue;
      found = notice_count_++;
      notices_[found].kind = static_cast<FeedbackKind>(kind);
      notices_[found].count = 0;
    }
    saturating_add(notices_[found].count, incoming);
    notices_[found].remaining_seconds = notice_lifetime_seconds;
    sequences_[found] = ++next_sequence_;
  }
}

void NativeCampaignFeedback::advance(double real_seconds) {
  require_owner();
  if (!std::isfinite(real_seconds) || real_seconds <= 0.) return;
  const auto elapsed = static_cast<float>(std::min(real_seconds, 60.0));
  std::size_t write{};
  for (std::size_t read = 0; read < notice_count_; ++read) {
    auto notice = notices_[read];
    notice.remaining_seconds -= elapsed;
    if (notice.remaining_seconds <= 0.f) continue;
    notices_[write] = notice;
    sequences_[write] = sequences_[read];
    ++write;
  }
  notice_count_ = write;
}

void NativeCampaignFeedback::reset() {
  require_owner();
  notice_count_ = 0;
  next_sequence_ = 0;
}

std::span<const CampaignFeedbackNotice> NativeCampaignFeedback::recent() const {
  require_owner();
  return {notices_.data(), notice_count_};
}

CampaignFeedbackSummary NativeCampaignFeedback::counts() const {
  require_owner();
  CampaignFeedbackSummary summary;
  for (const auto& notice : recent())
    saturating_add(summary.counts[index(notice.kind)], notice.count);
  return summary;
}

void NativeCampaignFeedback::render(DrawList& draw, int width, int height) const {
  require_owner();
  if (width <= 0 || height <= 0 || notice_count_ == 0) return;
  const float scale = scale_for(width, height);
  const float margin = 14.f * scale;
  const float toolbar_bottom = std::min(static_cast<float>(height) - margin,
                                        58.f * scale + margin);
  const float panel_width = std::min(std::max(1.f, 288.f * scale),
                                     std::max(1.f, static_cast<float>(width) - margin * 2.f));
  const float panel_height = 48.f * scale;
  // Keep the fixed right inspector and left navigation unobscured.
  const float right = (static_cast<float>(width) + panel_width) * .5f;
  std::array<std::size_t, maximum_notices> ordered{};
  for (std::size_t i = 0; i < notice_count_; ++i) ordered[i] = i;
  std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(notice_count_),
            [&](std::size_t left, std::size_t right_index) {
              return sequences_[left] > sequences_[right_index];
            });
  const auto visible = std::min<std::size_t>(3, notice_count_);
  for (std::size_t row = 0; row < visible; ++row) {
    const auto& notice = notices_[ordered[row]];
    const float y = toolbar_bottom + static_cast<float>(row) * (panel_height + 8.f * scale);
    if (y >= height - margin) break;
    const UiRect panel{right - panel_width, y, panel_width,
                       std::min(panel_height, static_cast<float>(height) - margin - y)};
    if (panel.height <= 0.f) break;
    const float fade = std::clamp(notice.remaining_seconds, 0.f, 1.f);
    const auto alpha = static_cast<std::uint8_t>(std::lround(235.f * fade));
    draw.overlay.emplace_back(FilledRectangle{panel, {panel_fill.r, panel_fill.g, panel_fill.b, alpha}});
    draw.overlay.emplace_back(StrokedRectangle{panel, {panel_stroke.r, panel_stroke.g, panel_stroke.b, alpha}});
    std::string value = tr(notice_key(notice.kind), notice_label(notice.kind));
    if (notice.count > 1) value += " x" + std::to_string(notice.count);
    const UiRect text_bounds{panel.x + 12.f * scale, panel.y + 14.f * scale,
                             std::max(1.f, panel.width - 24.f * scale), panel.height - 8.f * scale};
    draw.overlay.emplace_back(Text{{text_bounds.x, text_bounds.y}, std::move(value),
                                   {text_color.r, text_color.g, text_color.b, alpha},
                                   std::max(12, static_cast<int>(std::lround(16.f * scale))),
                                   text_bounds.width, panel, TextAlign::Left, FontFace::Interface});
  }
}

} // namespace stellar::native_campaign_feedback
