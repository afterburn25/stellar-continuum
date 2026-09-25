#include "native_notification_events.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace stellar::core;
using namespace stellar::native_notifications;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
DiplomaticHistoryEventSnapshot event(std::int64_t id,int target,std::string message){
  DiplomaticHistoryEventSnapshot row;
  row.event_id=id;row.tick=1000;row.kind=DiplomaticEventKind::proposal_sent;
  row.primary_civilization_id=1;row.secondary_civilization_id=target;
  row.summary=std::move(message);row.known_to_civilization_ids={1};return row;
}
void observer_and_activation(){
  NativeNotificationFeed feed;NativeDiplomaticNotifications publisher;
  DiplomaticStateView view;view.observer_civilization_id=1;
  DiplomaticContactView identified;identified.target_civilization_id=2;
  identified.awareness=ContactAwareness::identified;view.contacts.push_back(identified);
  DiplomaticContactView unidentified;unidentified.target_civilization_id=3;
  view.contacts.push_back(unidentified);
  view.recent_events={event(1,2,"Retained old report")};
  publisher.seed(view);publisher.harvest(feed,view);
  require(feed.items().empty(),"Admission replayed retained diplomatic history");
  view.recent_events.push_back(event(2,2,"Proposal sent to known contact"));
  publisher.harvest(feed,view);
  require(feed.items().size()==1&&feed.items().front().diplomatic_contact_id==2,
          "Known audience report lost its identified counterpart");
  require(feed.items().front().message=="A diplomatic proposal has been sent."&&
      feed.items().front().message_key=="NOTIFY_DIP_PROPOSAL_SENT",
          "Raw internal event text leaked into the player report");
  view.recent_events.push_back(view.recent_events.back());
  publisher.harvest(feed,view);
  require(feed.items().size()==1,"Unchanged view repeated a report");
  view.recent_events.push_back(event(3,3,"SECRET ALIEN IDENTITY"));
  auto hidden=event(4,2,"PRIVATE FOREIGN REPORT");hidden.known_to_civilization_ids={2};
  view.recent_events.push_back(hidden);
  publisher.harvest(feed,view);
  require(feed.items().size()==2&&!feed.items().back().diplomatic_contact_id&&
      feed.items().back().message.find("SECRET")==std::string::npos,
      "Unknown identity or foreign audience escaped into notifications");
  feed.clear();publisher.seed(view);publisher.harvest(feed,view);
  require(feed.items().empty(),"Reload emitted historical alerts");
  auto other=view;other.observer_civilization_id=2;
  bool rejected=false;
  try{publisher.harvest(feed,other);}catch(const std::logic_error&){rejected=true;}
  require(rejected&&feed.items().empty(),"Observer changed without re-admission");
}
void chronicle_seeding(){
  stellar::engine::EventHistory history;
  const auto add=[&](double day,std::string category,std::string summary,
                     std::vector<std::uint64_t> visible={},
                     double significance=0.5,std::uint64_t location=0,
                     std::vector<std::uint64_t> actors={}){
    stellar::engine::HistoryEvent event;event.at_day=day;
    event.category=std::move(category);event.summary=std::move(summary);
    event.significance=significance;event.location=location;
    event.actors=std::move(actors);
    event.visible_to=std::move(visible);history.record(std::move(event));};
  add(400.,"exploration.system_surveyed","System survey completed",
      {},0.5,9,{1,7});
  add(410.,"war.battle","FOREIGN BATTLE REPORT",{7});
  add(415.,"war.damage_applied","TRIVIA DAMAGE TICK",{1},0.1);
  add(420.,"colony.founded","Colony established",{1},0.5,0,{1,7});
  add(430.,"unknown.happening","Uncategorized record",{1},0.5,0,{7,9});
  NativeNotificationFeed feed;
  seed_chronicle_notifications(feed,history,1);
  require(feed.items().size()==3,"Observer-invisible chronicle entry leaked");
  require(feed.items().front().category=="Exploration"&&
      feed.items().front().message=="System survey completed",
      "Chronicle order or summary mapping broken");
  require(feed.items()[1].category=="Colony","Category label not mapped");
  require(feed.items().back().category=="unknown.happening",
      "Unmapped category lost its stable id");
  require(feed.items().front().date!=feed.items().back().date,
      "Recorded event dates not preserved");
  require(feed.items().front().system_id&&*feed.items().front().system_id==9&&
      !feed.items()[1].system_id,
      "Seeded report lost its system location");
  require(feed.items().front().diplomatic_contact_id&&
      *feed.items().front().diplomatic_contact_id==7&&
      feed.items()[1].diplomatic_contact_id&&
      *feed.items()[1].diplomatic_contact_id==7&&
      !feed.items().back().diplomatic_contact_id,
      "Single-foreign-actor contact not propagated to seeded reports");
  NativeNotificationFeed bounded;
  seed_chronicle_notifications(bounded,history,1,2);
  require(bounded.items().size()==2&&
      bounded.items().front().message=="Colony established",
      "Chronicle seed bound kept wrong entries");
  NativeNotificationFeed fresh;
  seed_chronicle_notifications(fresh,stellar::engine::EventHistory{},1);
  require(fresh.items().empty(),"Empty chronicle seeded phantom entries");
}
void bounded_categories(){
  using namespace stellar::native_campaign_feedback;
  NativeNotificationFeed feed;CampaignFeedbackSummary summary;
  summary.counts[static_cast<std::size_t>(FeedbackKind::ResearchReport)]=3;
  summary.counts[static_cast<std::size_t>(FeedbackKind::ShipComplete)]=1;
  publish_campaign_notifications(feed,summary,0.);
  require(feed.items().size()==2&&feed.items().front().date=="2050-03-21"&&
      feed.items().front().message=="Research report available (3)"&&
      feed.items().front().message_key=="NOTIFY_MSG_RESEARCH"&&
      feed.items().front().message_arg==" (3)"&&
      !feed.items().front().diplomatic_contact_id,
      "Coalesced observer summary was not retained as dated safe categories");
  for(int i=0;i<100;++i)publish_campaign_notifications(feed,summary,i);
  require(feed.items().size()==NativeNotificationFeed::maximum_items,
      "Accelerated event history exceeded its memory bound");
}
}
int main(){try{observer_and_activation();chronicle_seeding();bounded_categories();}
  catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
