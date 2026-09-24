#include "native_campaign_feedback.hpp"

#include <stellar/engine/localization.hpp>

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

using namespace stellar::core;
using namespace stellar::native_campaign_feedback;
using namespace stellar::native_map;

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

IntegratedAdaptiveCampaignStepResult owned_and_foreign_step() {
  IntegratedAdaptiveCampaignStepResult step;
  step.research_events = {{7, "secret-owned-node", "raw outcome", false},
                          {9, "secret-foreign-node", "foreign raw outcome", true}};
  step.core.construction_events = {{7, "owned-build", "raw"}, {9, "foreign-build", "raw"}};
  step.core.shipbuilding_events = {{7, 1, "owned-ship", "raw"}, {9, 2, "foreign-ship", "raw"}};
  step.core.exploration_events = {
      {ExplorationEventType::SystemSurveyed, 7, 1, 3, "owned survey"},
      {ExplorationEventType::SystemSurveyed, 9, 2, 4, "foreign survey"},
      {ExplorationEventType::SensorContact, 7, 1, 3, "foreign system identity", 9},
      {ExplorationEventType::FirstContact, 9, 2, 4, "foreign contact", 7},
      {ExplorationEventType::ResourceSurveyed, 7, 1, 3, "not a completion notice"}};
  step.core.colonization_events = {{7, 10, 3, 20, "source completion only"},
                                   {9, 11, 4, 21, "foreign colony"}};
  step.core.combat_events = {
      {CombatEventType::EngagementStarted, 3, 7, 1, 9, 2, 0, 0, 0, "owned actor"},
      {CombatEventType::FleetDestroyed, 3, 9, 2, 7, 1, 0, 0, 0, "owned target"},
      {CombatEventType::FleetRetreatInitiated, 3, 9, 2, 8, 3, 0, 0, 0, "foreign"},
      {CombatEventType::DamageApplied, 3, 7, 1, 9, 2, 1, 0, 0, "not alert type"}};
  return step;
}

void collection_filters_and_classifies() {
  CampaignFrameResult frame;
  frame.strategic_results.push_back(owned_and_foreign_step());
  frame.tactical_events.push_back({CombatEventType::FleetRetreatInitiated, 2, 8, 3, 7, 1,
                                   0, 0, 0, "owned tactical target"});
  frame.tactical_events.push_back({CombatEventType::EngagementStarted, 2, 8, 3, 9, 1,
                                   0, 0, 0, "foreign tactical"});
  const auto summary = collect_campaign_feedback(frame, 7);
  require(summary.count(FeedbackKind::ResearchReport) == 1,
          "non-outcome research event was not presented as a report");
  require(summary.count(FeedbackKind::ConstructionComplete) == 1 &&
              summary.count(FeedbackKind::ShipComplete) == 1,
          "owned industry completion filtering changed");
  require(summary.count(FeedbackKind::SurveyComplete) == 1 &&
              summary.count(FeedbackKind::ContactDetected) == 1,
          "owned exploration categories were not bounded correctly");
  require(summary.count(FeedbackKind::ColonyFounded) == 1,
          "completed source colonization event was not classified");
  require(summary.count(FeedbackKind::CombatAlert) == 3,
          "combat alerts did not include only involved start/destruction/retreat events");
  require(collect_campaign_feedback(frame, -1).empty(),
          "negative observer received campaign feedback");
  require(collect_campaign_feedback(CampaignFrameResult{}, 7).empty(),
          "empty/load frame produced feedback");
}

void bounded_coalescing_expiry_and_reset() {
  NativeCampaignFeedback feedback;
  CampaignFeedbackSummary burst;
  for (std::size_t index = 0; index < feedback_kind_count; ++index)
    burst.counts[index] = 50;
  feedback.publish(burst);
  feedback.publish(burst);
  require(feedback.recent().size() == feedback_kind_count,
          "feedback queue exceeded its category bound");
  require(feedback.counts().count(FeedbackKind::CombatAlert) == 100,
          "same category feedback did not coalesce");
  feedback.advance(5.25);
  require(!feedback.recent().empty(), "feedback expired before its lifetime");
  feedback.advance(1.0);
  require(feedback.recent().empty(), "feedback did not expire on real time");
  feedback.publish(burst);
  feedback.reset();
  require(feedback.recent().empty() && feedback.counts().empty(),
          "load/reset did not clear campaign feedback");
}

void rendering_is_compact_clipped_and_owner_bound() {
  NativeCampaignFeedback feedback;
  CampaignFeedbackSummary summary;
  summary.counts[static_cast<std::size_t>(FeedbackKind::ResearchReport)] = 2;
  summary.counts[static_cast<std::size_t>(FeedbackKind::ConstructionComplete)] = 1;
  summary.counts[static_cast<std::size_t>(FeedbackKind::ShipComplete)] = 1;
  summary.counts[static_cast<std::size_t>(FeedbackKind::CombatAlert)] = 1;
  feedback.publish(summary);
  CampaignFeedbackSummary later_research;
  later_research.counts[static_cast<std::size_t>(FeedbackKind::ResearchReport)] = 1;
  feedback.publish(later_research);
  for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080},
                                     std::pair{3840, 2160}}) {
    DrawList draw;
    feedback.render(draw, width, height);
    std::size_t panels{}, labels{};
    bool research_report_label{};
    for (const auto& command : draw.overlay) {
      if (const auto* panel = std::get_if<FilledRectangle>(&command)) {
        ++panels;
        require(panel->bounds.x >= 0 && panel->bounds.y >= 0 &&
                    panel->bounds.x + panel->bounds.width <= width &&
                    panel->bounds.y + panel->bounds.height <= height,
                "feedback panel escaped viewport");
      }
      if (const auto* label = std::get_if<Text>(&command)) {
        ++labels;
        require(label->clip && label->clip->x >= 0 && label->clip->y >= 0 &&
                    label->clip->x + label->clip->width <= width &&
                    label->clip->y + label->clip->height <= height,
                "feedback text escaped matching panel clip");
        require(label->value.find("secret") == std::string::npos,
                "feedback forwarded a source name or message");
        research_report_label = research_report_label ||
                                label->value.find("Research report available") != std::string::npos;
      }
    }
    require(panels == 3 && labels == 3,
            "feedback rendered more than the compact three-panel maximum");
    require(research_report_label,
            "research feedback promised a completion instead of a report");
  }
  // A bound catalog localizes notice labels; unbound keys keep literals.
  {
    stellar::engine::LocalizationTable locale{"en", "en"};
    require(locale.load_json(
                R"({"locale":"en","strings":{"FEEDBACK_RESEARCH":"FORSCHUNG BEREIT"}})"),
            "the test catalog must parse");
    feedback.set_localization(&locale);
    DrawList draw;
    feedback.render(draw, 1280, 720);
    bool translated{}, fallback_kept{};
    for (const auto& command : draw.overlay) {
      if (const auto* label = std::get_if<Text>(&command)) {
        translated = translated || label->value.find("FORSCHUNG BEREIT") !=
                                       std::string::npos;
        fallback_kept = fallback_kept ||
                        label->value.find("Ship complete") !=
                            std::string::npos;
      }
    }
    feedback.set_localization(nullptr);
    require(translated && fallback_kept,
            "feedback notices did not resolve through the bound catalog");
  }
  std::atomic<bool> rejected{};
  std::thread foreign([&] {
    try { (void)feedback.counts(); }
    catch (const std::logic_error&) { rejected = true; }
  });
  foreign.join();
  require(rejected, "feedback accepted access from a non-owner thread");
}
} // namespace

int main() try {
  collection_filters_and_classifies();
  bounded_coalescing_expiry_and_reset();
  rendering_is_compact_clipped_and_owner_bound();
  std::cout << "Native campaign feedback tests passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
