#include "native_notification_events.hpp"
#include "native_campaign_calendar.hpp"
#include "native_chronicle.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace stellar::native_notifications {
namespace {
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
const char* diplomatic_message_key(core::DiplomaticEventKind kind){
  using enum core::DiplomaticEventKind;
  switch(kind){
  case contact_established:return "NOTIFY_DIP_CONTACT";
  case communication_available:return "NOTIFY_DIP_CHANNEL";
  case proposal_sent:return "NOTIFY_DIP_PROPOSAL_SENT";
  case proposal_accepted:return "NOTIFY_DIP_PROPOSAL_ACCEPTED";
  case proposal_rejected:return "NOTIFY_DIP_PROPOSAL_REJECTED";
  case agreement_activated:return "NOTIFY_DIP_AGREEMENT_ACTIVE";
  case agreement_terminated:return "NOTIFY_DIP_AGREEMENT_ENDED";
  case war_declared:return "NOTIFY_DIP_WAR";
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
  constexpr std::array<const char*,7> keys{
      "NOTIFY_MSG_RESEARCH","NOTIFY_MSG_CONSTRUCTION","NOTIFY_MSG_SHIP",
      "NOTIFY_MSG_SURVEY","NOTIFY_MSG_CONTACT","NOTIFY_MSG_SETTLEMENT",
      "NOTIFY_MSG_COMBAT"};
  constexpr std::array<NotificationSeverity,7> severities{
      NotificationSeverity::Info,NotificationSeverity::Positive,
      NotificationSeverity::Positive,NotificationSeverity::Positive,
      NotificationSeverity::Caution,NotificationSeverity::Positive,
      NotificationSeverity::Alert};
  static_assert(categories.size()==native_campaign_feedback::feedback_kind_count);
  const auto date=native_campaign::format_campaign_date(day);
  for(std::size_t index=0;index<categories.size();++index){
    const auto count=summary.counts[index];
    if(!count)continue;
    std::string suffix;
    if(count>1)suffix=" ("+std::to_string(count)+")";
    auto message=std::string(messages[index])+suffix;
    feed.publish(categories[index],date,std::move(message),
                 std::nullopt,std::nullopt,keys[index],std::move(suffix),
                 severities[index]);
  }
}

void seed_chronicle_notifications(NativeNotificationFeed& feed,
    const engine::EventHistory& history,int observer_civilization_id,
    std::size_t max_entries){
  // Report floor: the category vocabulary assigns high-volume trivia
  // (war.damage_applied 0.1, signature/system detections <=0.3,
  // survey_started 0.2) below it — the feed is for reports, not noise.
  constexpr double report_significance=0.35;
  const auto events=history.feed(
      static_cast<std::uint64_t>(observer_civilization_id),
      -std::numeric_limits<double>::infinity(),report_significance);
  const auto begin=events.size()>max_entries?events.end()-max_entries
                                           :events.begin();
  const auto observer=static_cast<std::uint64_t>(observer_civilization_id);
  for(auto it=begin;it!=events.end();++it){
    const auto* event=*it;
    const char* label=native_chronicle::category_label(event->category);
    // Located reports get a VIEW SYSTEM action — the feed() projection
    // already confined them to the observer's authorized visibility.
    std::optional<int> system;
    if(event->location)system=static_cast<int>(event->location);
    // Exactly one foreign actor → the report can offer OPEN RELATIONS,
    // same rule the chronicle browser uses for its DIP action. The
    // diplomacy workspace's own identification check still gates what
    // the contact actually shows.
    std::uint64_t foreign=0;
    for(const auto id:event->actors)
      if(id!=observer){
        if(foreign!=0&&foreign!=id){foreign=0;break;}
        foreign=id;
      }
    const std::optional<int> counterpart=
        foreign?std::optional<int>(static_cast<int>(foreign)):std::nullopt;
    feed.publish(label?std::string(label):event->category,
        native_campaign::format_campaign_date(event->at_day),
        event->summary,counterpart,system,{},{},
        event->category.starts_with("war.")?NotificationSeverity::Alert
            :NotificationSeverity::Info);
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
    using enum core::DiplomaticEventKind;
    const auto severity=
        event.kind==war_declared?NotificationSeverity::Alert
        :event.kind==proposal_rejected||event.kind==agreement_terminated
            ?NotificationSeverity::Caution
        :event.kind==proposal_accepted||event.kind==agreement_activated||
             event.kind==contact_established?NotificationSeverity::Positive
        :NotificationSeverity::Info;
    feed.publish("Diplomacy",native_campaign::format_campaign_date(
        static_cast<double>(event.tick)/1000.),
        names_visible?message:"A diplomatic signal was received from an unidentified contact.",
        counterpart,std::nullopt,
        names_visible?std::string(diplomatic_message_key(event.kind))
                     :std::string("NOTIFY_DIP_SIGNAL"),{},severity);
  }
  // At most Core's 256 retained events; old IDs cannot accumulate indefinitely.
  seen_=std::move(retained);
}
} // namespace stellar::native_notifications
