#include "native_notification_events.hpp"
#include "native_campaign_calendar.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace stellar::native_notifications {
namespace {
const char* chronicle_category_label(std::string_view category){
  if(category.starts_with("construction."))return "Construction";
  if(category.starts_with("shipbuilding."))return "Ships";
  if(category.starts_with("research."))return "Research";
  if(category.starts_with("exploration."))return "Exploration";
  if(category.starts_with("colony."))return "Colony";
  if(category.starts_with("war."))return "Combat";
  return nullptr;
}
const char* diplomatic_message(core::DiplomaticEventKind kind){
  using enum core::DiplomaticEventKind;
  switch(kind){
  case contact_established:return "A new diplomatic contact has been established.";
  case communication_available:return "A diplomatic communication channel is now available.";
  case proposal_sent:return "A diplomatic proposal has been sent.";
  case proposal_accepted:return "A diplomatic proposal has been accepted.";
  case proposal_rejected:return "A diplomatic proposal has been declined.";
  case agreement_activated:return "A diplomatic agreement is now active.";
  case agreement_terminated:return "A diplomatic agreement has ended.";
  case war_declared:return "A declaration of war has been recorded.";
  default:return nullptr;
  }
}
}
void publish_campaign_notifications(NativeNotificationFeed& feed,
    const native_campaign_feedback::CampaignFeedbackSummary& summary,double day){
  constexpr std::array<const char*,7> categories{
      "Research","Construction","Ships","Exploration","Exploration","Colony","Combat"};
  constexpr std::array<const char*,7> messages{
      "Research report available","Construction complete","Ship complete",
      "Survey complete","Contact detected","Settlement established","Combat alert"};
  static_assert(categories.size()==native_campaign_feedback::feedback_kind_count);
  const auto date=native_campaign::format_campaign_date(day);
  for(std::size_t index=0;index<categories.size();++index){
    const auto count=summary.counts[index];
    if(!count)continue;
    auto message=std::string(messages[index]);
    if(count>1)message+=" ("+std::to_string(count)+")";
    feed.publish(categories[index],date,std::move(message));
  }
}

void seed_chronicle_notifications(NativeNotificationFeed& feed,
    const engine::EventHistory& history,int observer_civilization_id,
    std::size_t max_entries){
  const auto events=history.feed(
      static_cast<std::uint64_t>(observer_civilization_id),
      -std::numeric_limits<double>::infinity());
  const auto begin=events.size()>max_entries?events.end()-max_entries
                                           :events.begin();
  for(auto it=begin;it!=events.end();++it){
    const auto* event=*it;
    const char* label=chronicle_category_label(event->category);
    feed.publish(label?std::string(label):event->category,
        native_campaign::format_campaign_date(event->at_day),event->summary);
  }
}

void NativeDiplomaticNotifications::seed(const core::DiplomaticStateView& view){
  observer_=view.observer_civilization_id;seen_.clear();
  for(const auto& event:view.recent_events)seen_.insert(event.event_id);
}

void NativeDiplomaticNotifications::harvest(NativeNotificationFeed& feed,
    const core::DiplomaticStateView& view){
  if(observer_!=view.observer_civilization_id)
    throw std::logic_error("Notification observer changed without campaign admission.");
  std::unordered_set<std::int64_t> retained;
  const auto identified=[&](int id){
    return id==observer_||std::ranges::any_of(view.contacts,[&](const auto& contact){
      return contact.target_civilization_id==id&&
             contact.awareness>=core::ContactAwareness::identified;
    });
  };
  for(const auto& event:view.recent_events){
    if(!retained.insert(event.event_id).second||seen_.contains(event.event_id))continue;
    const auto message=diplomatic_message(event.kind);
    if(!message)continue;
    // Defense in depth: caller supplied the observer view, but reject a raw
    // foreign-audience row if a future adapter violates that contract.
    if(std::ranges::find(event.known_to_civilization_ids,observer_)==
       event.known_to_civilization_ids.end())continue;
    const bool names_visible=identified(event.primary_civilization_id)&&
        (!event.secondary_civilization_id||identified(*event.secondary_civilization_id));
    std::optional<int> counterpart;
    if(names_visible){
      if(event.primary_civilization_id==observer_)counterpart=event.secondary_civilization_id;
      else if(event.secondary_civilization_id==observer_)counterpart=event.primary_civilization_id;
    }
    feed.publish("Diplomacy",native_campaign::format_campaign_date(
        static_cast<double>(event.tick)/1000.),
        names_visible?message:"A diplomatic signal was received from an unidentified contact.",
        counterpart);
  }
  // At most Core's 256 retained events; old IDs cannot accumulate indefinitely.
  seen_=std::move(retained);
}
} // namespace stellar::native_notifications
