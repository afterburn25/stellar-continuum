#include <stellar/core/stellar_activity.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/campaign_foundation_persistence.hpp>
#include "../core/src/stellar_activity_json.hpp"
#include <iostream>
#include <set>
using namespace stellar::core;
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main()try{
 std::vector<StellarSystem> systems;
 for(int id=1;id<=100;++id){StellarSystem s;s.id=id;s.name="Activity";s.stellar_object=generate_stellar_physics(id,id<=50?StellarObjectType::MRedDwarf:StellarObjectType::GYellowStar);systems.push_back(s);}
 initialize_stellar_activity(41,systems);
 for(auto& s:systems){auto& a=s.stellar_activity->front();a.profile.level=s.id<=50?StellarActivityLevel::Active:StellarActivityLevel::Quiet;a.next_event_day=.001;}
 auto chunked=systems;StellarActivityScheduler first,second;
 const auto hooks=first.advance(systems,25);
 for(int i=1;i<=2500;++i)(void)second.advance(chunked,i*.01);
 std::uint64_t m=0,g=0,ordinary=0,super=0;
 for(std::size_t i=0;i<systems.size();++i){const auto& a=systems[i].stellar_activity->front();
  check(*systems[i].stellar_activity==*chunked[i].stellar_activity,"Event schedule depends on frame size/game speed");
  validate_stellar_activity(systems[i]);if(i<50)m+=a.counter;else g+=a.counter;
  ordinary+=a.generated_counts[1];super+=a.generated_counts[3];
  check(stellar_cme_probability(a.profile,StellarEruptionType::Flare)<stellar_cme_probability(a.profile,StellarEruptionType::MajorFlare)&&stellar_cme_probability(a.profile,StellarEruptionType::MajorFlare)<stellar_cme_probability(a.profile,StellarEruptionType::Superflare),"CME chance does not rise with magnitude");
 }
 check(m>g*10&&ordinary>super*10&&!hooks.empty(),"Natural activity distribution is wrong");
 const auto dto=capture_stellar_systems(systems);auto restored=restore_stellar_systems(dto);
 for(std::size_t i=0;i<systems.size();++i){check(restored[i].stellar_activity==systems[i].stellar_activity,"DTO lost active eruption");nlohmann::json j=*systems[i].stellar_activity;check(j.get<std::vector<StellarActivityState>>()==*systems[i].stellar_activity,"JSON lost active eruption");}
 auto before=restored[0].stellar_activity;initialize_stellar_activity(999,restored,25);check(before==restored[0].stellar_activity,"Restore rerolled activity");
 // Repeated eruptions on ONE star within the former three-day region epoch
 // must cover all octants, rather than clustering around one persistent site.
 auto sites=std::vector<StellarSystem>{systems.front()};sites.front().stellar_activity.reset();initialize_stellar_activity(41,sites);
 auto& active=sites.front().stellar_activity->front();active.profile.level=StellarActivityLevel::FlareStar;active.next_event_day=.001;
 auto config=stellar_activity_configuration();config.base_events_per_day=100;config.simultaneous_caps.fill(16);
 for(auto& timing:config.timings){timing.minimum_minutes.fill(.02);timing.maximum_minutes.fill(.04);}
 StellarActivityScheduler site_scheduler;std::set<std::uint64_t> seen;std::array<int,8> octants{};double latitude_mean=0;int related=0;
 for(int tick=1;tick<=200;++tick){(void)site_scheduler.advance(sites,tick*.005,config);
  for(const auto& event:active.events){
   if(event.parent_event_id){const auto parent=std::ranges::find(active.events,event.parent_event_id,&StellarEruptionEvent::id);
    if(parent!=active.events.end()){check(event.latitude==parent->latitude&&event.longitude==parent->longitude,"Associated CME detached from its eruption site");++related;}continue;}
   if(!seen.insert(event.id).second)continue;
   const double x=std::cos(event.latitude)*std::sin(event.longitude),y=std::sin(event.latitude),z=std::cos(event.latitude)*std::cos(event.longitude);
   ++octants[(x>=0?1:0)+(y>=0?2:0)+(z>=0?4:0)];latitude_mean+=y;
  }
 }
 check(seen.size()>200&&related>0,"Insufficient natural eruption location samples");
 for(int count:octants)check(count>static_cast<int>(seen.size()/16),"Natural eruptions remain concentrated in one stellar region");
 check(std::abs(latitude_mean/seen.size())<.12,"Surface sampling is biased toward a pole");
 StellarActivityScheduler resumed;(void)first.advance(systems,26);(void)resumed.advance(restored,26);
 for(std::size_t i=0;i<systems.size();++i)check(systems[i].stellar_activity==restored[i].stellar_activity,"Reload changed future eruption sites or timing");
 for(auto type:{StellarObjectType::WhiteDwarf,StellarObjectType::Magnetar,StellarObjectType::QuiescentBlackHole})check(stellar_eruption_rate(make_stellar_activity_profile(generate_stellar_physics(17,type),5))==0,"Solar flare assigned to compact object");
 StellarEruptionEvent e;e.stage_days={.01,.02,.01,.10};e.start_day=2;auto sample=stellar_eruption_sample(e,2+.65*.14);check(std::abs(sample.fraction-.65)<1e-12,"Timeline lost 65% progression");
 double sum=0,sum2=0;for(int i=0;i<10000;++i){double t=stellar::engine::exponential_interval(2.,17,i);sum+=t;sum2+=t*t;}check(std::abs(sum/10000-.5)<.02&&sum2/10000>.45,"Hazard intervals are not exponential");
 std::cout<<"Deterministic natural activity, 25x chunk equivalence, CME distribution, DTO/JSON persistence, compact exclusion passed. M="<<m<<" quiet G="<<g<<" ordinary="<<ordinary<<" super="<<super<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
