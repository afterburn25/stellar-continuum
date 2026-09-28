#include <stellar/core/stellar_activity.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include "stellar_activity_data.hpp"
#include <nlohmann/json.hpp>
#include <numbers>
#include <numeric>
#include <set>

namespace stellar::core {
namespace {
constexpr double pi=std::numbers::pi;
std::size_t index(auto value){return static_cast<std::size_t>(value);}
bool in(double v,double a,double b){return std::isfinite(v)&&v>=a&&v<=b;}
void require(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
std::uint64_t star_seed(std::int64_t seed,int id,int component){
 stellar::engine::DeterministicRandom random(static_cast<std::uint64_t>(seed)^((static_cast<std::uint64_t>(id)*4+component)*0x9e3779b97f4a7c15ULL));return random.next_u64();
}
StellarEruptionEvent make_event(StellarActivityState& state,int system,int component,StellarEruptionType type,double day,const StellarActivityConfiguration& config,bool forced=false){
 require(state.counter<(1ULL<<38),"Stellar activity event counter exhausted");
 const auto ordinal=++state.counter;
 auto random=[&](std::uint64_t channel){return stellar::engine::timeline_random(state.seed,ordinal,channel);};
 StellarEruptionEvent e;e.id=((static_cast<std::uint64_t>(system)*4+component)<<40)|(ordinal*2);e.seed=state.seed^ordinal;
 e.system_id=system;e.component=component;e.type=type;e.activity_source=state.profile.level;e.start_day=day;e.forced=forced;
 e.visual_variant=static_cast<int>(random(1)*12);e.magnitude=.75+random(2)*.75;
 const auto& timing=config.timings[index(type)];
 for(int i=0;i<4;++i)e.stage_days[i]=(timing.minimum_minutes[i]+random(3+i)*(timing.maximum_minutes[i]-timing.minimum_minutes[i]))/1440.;
 // Each independent eruption gets a deterministic, equal-area surface site.
 // Sampling sin(latitude) avoids crowding the poles. The saved event retains
 // its site throughout its lifetime; an associated CME inherits that site.
 e.latitude=std::asin(2*random(7)-1);
 e.longitude=(2*random(8)-1)*pi;e.orientation=(random(9)*2-1)*pi;
 e.scale=.85+random(10)*.3;e.brightness=.88+random(11)*.24;
 e.cme_probability=stellar_cme_probability(state.profile,type,config);
 if(type==StellarEruptionType::Cme){e.cme_associated=true;e.cme_escaped=state.profile.spectral!=EruptionSpectralClass::M||random(12)<config.m_cme_escape;}
 else{e.cme_associated=random(12)<e.cme_probability;e.cme_escaped=e.cme_associated&&(state.profile.spectral!=EruptionSpectralClass::M||random(13)<config.m_cme_escape);}
 return e;
}
void prune(StellarActivityState& state,double day,const StellarActivityConfiguration& config){
 std::erase_if(state.events,[&](const auto& e){return !e.paused&&e.start_day+stellar_eruption_duration(e)<day-config.history_days;});
 while(state.events.size()>48){auto it=std::find_if(state.events.begin(),state.events.end(),[](const auto& e){return !e.paused;});if(it==state.events.end())break;state.events.erase(it);}
}
}

StellarActivityConfiguration parse_stellar_activity_configuration(std::string_view text){
 const auto j=nlohmann::json::parse(text);StellarActivityConfiguration c;
#define READ(name) c.name=j.at(#name).get<decltype(c.name)>()
 READ(version);READ(base_events_per_day);READ(spectral_rates);READ(activity_multipliers);READ(retention_myr);READ(rotation_reference_days);
 READ(simultaneous_caps);READ(type_weights);READ(cme_probabilities);READ(m_cme_escape);READ(giant_rate);READ(supergiant_rate);READ(history_days);
#undef READ
 require(c.version==1,"Unsupported stellar activity configuration");
 require(j.at("timings").size()==5,"Five eruption timelines required");
 for(int i=0;i<5;++i){const auto& t=j.at("timings").at(i);auto& v=c.timings[i];
  v.minimum_minutes=t.at("minimum_minutes").get<std::array<double,4>>();v.maximum_minutes=t.at("maximum_minutes").get<std::array<double,4>>();v.min_display_seconds=t.at("min_display_seconds");
  require(in(v.min_display_seconds,.1,60),"Invalid minimum display duration");
  for(int s=0;s<4;++s)require(in(v.minimum_minutes[s],.01,10080)&&in(v.maximum_minutes[s],v.minimum_minutes[s],10080),"Invalid eruption timeline range");
  require(in(c.activity_multipliers[i],.001,100)&&in(c.cme_probabilities[i],0,1)&&c.simultaneous_caps[i]>0&&c.simultaneous_caps[i]<=16,"Invalid activity state tuning");
  double total=0;for(double weight:c.type_weights[i]){require(in(weight,0,10000),"Invalid eruption weights");total+=weight;}require(total>0,"Empty eruption distribution");
 }
 for(int i=0;i<7;++i)require(in(c.spectral_rates[i],.0001,100)&&in(c.retention_myr[i],1,100000)&&in(c.rotation_reference_days[i],.1,1000),"Invalid spectral activity tuning");
 require(in(c.base_events_per_day,.001,100)&&in(c.m_cme_escape,0,1)&&in(c.giant_rate,.001,10)&&in(c.supergiant_rate,.001,10)&&in(c.history_days,0,30),"Invalid eruption rate or retention");
 return c;
}
const StellarActivityConfiguration& stellar_activity_configuration(){static const auto c=parse_stellar_activity_configuration(stellar_activity_json);return c;}
std::string_view stellar_activity_name(StellarActivityLevel level){constexpr std::array names{"Quiet","Normal","Active","Very active","Flare star"};return names.at(index(level));}
std::string_view stellar_eruption_name(StellarEruptionType type){constexpr std::array names{"Small prominence","Flare","Major flare","Superflare","CME"};return names.at(index(type));}
std::string_view eruption_spectral_name(EruptionSpectralClass type){constexpr std::array names{"O","B","A","F","G","K","M","Unsupported"};return names.at(index(type));}
EruptionSpectralClass eruption_spectral_class(StellarObjectType type){
 switch(type){
  case StellarObjectType::OHotBlueStar:return EruptionSpectralClass::O;
  case StellarObjectType::BBlueWhiteStar:case StellarObjectType::BlueGiant:case StellarObjectType::BlueSupergiant:return EruptionSpectralClass::B;
  case StellarObjectType::AWhiteStar:return EruptionSpectralClass::A;
  case StellarObjectType::FYellowWhiteStar:return EruptionSpectralClass::F;
  case StellarObjectType::GYellowStar:case StellarObjectType::YellowGiant:case StellarObjectType::YellowSupergiant:return EruptionSpectralClass::G;
  case StellarObjectType::KOrangeStar:return EruptionSpectralClass::K;
  case StellarObjectType::MRedDwarf:case StellarObjectType::RedGiant:case StellarObjectType::RedSupergiant:return EruptionSpectralClass::M;
  default:return EruptionSpectralClass::Unsupported;
 }
}
StellarActivityProfile make_stellar_activity_profile(const StellarPhysicalProperties& star,std::uint64_t seed,const StellarActivityConfiguration& c){
 StellarActivityProfile p;p.spectral=eruption_spectral_class(star.type);if(p.spectral==EruptionSpectralClass::Unsupported){p.level=StellarActivityLevel::Quiet;p.evolution_modifier=0;return p;}
 const auto k=index(p.spectral);const auto& definition=stellar_object_definition(star.type);
 p.age_modifier=std::clamp(1./std::sqrt(.2+star.age_myr/c.retention_myr[k]),.12,1.5);
 const double scatter=.55+stellar::engine::timeline_random(seed,0,1)*1.2;
 p.rotation_days=c.rotation_reference_days[k]*std::sqrt(std::max(.015,star.age_myr/c.retention_myr[k]))*scatter;
 if(star.measured_anchor&&star.type==StellarObjectType::GYellowStar){p.rotation_days=25.4;p.inferred_rotation=false;}
 p.rotation_days=std::clamp(p.rotation_days,.2,180.);
 p.rotation_modifier=std::clamp(c.rotation_reference_days[k]/p.rotation_days,.15,3.);
 p.magnetic_activity=std::clamp(.15+.30*p.age_modifier+.16*p.rotation_modifier+(stellar::engine::timeline_random(seed,0,2)-.5)*.3,0.,1.);
 p.score=std::clamp(.12+.25*p.age_modifier+.14*p.rotation_modifier+.20*p.magnetic_activity-.12,0.,1.);
 if(star.measured_anchor&&star.type==StellarObjectType::GYellowStar){p.score=.34;p.magnetic_activity=.4;}
 // Activity longevity differs by class; hot non-convective stars remain rare
 // through their independent rate weights, even if young and fast rotating.
 p.level=p.score<.27?StellarActivityLevel::Quiet:p.score<.52?StellarActivityLevel::Normal:p.score<.72?StellarActivityLevel::Active:p.score<.9?StellarActivityLevel::VeryActive:StellarActivityLevel::FlareStar;
 if(definition.evolved){const bool supergiant=star.type==StellarObjectType::RedSupergiant||star.type==StellarObjectType::BlueSupergiant||star.type==StellarObjectType::YellowSupergiant;p.evolution_modifier=supergiant?c.supergiant_rate:c.giant_rate;p.prominence_rate_multiplier=1.8;p.superflare_rate_multiplier=.45;}
 return p;
}
double stellar_eruption_rate(const StellarActivityProfile& p,const StellarActivityConfiguration& c){
 if(p.spectral==EruptionSpectralClass::Unsupported)return 0;
 return c.base_events_per_day*c.spectral_rates.at(index(p.spectral))*c.activity_multipliers.at(index(p.level))*p.flare_rate_multiplier*p.evolution_modifier;
}
double stellar_cme_probability(const StellarActivityProfile& p,StellarEruptionType type,const StellarActivityConfiguration& c){
 return std::clamp(c.cme_probabilities.at(index(type))*(.8+.3*p.score+.15*p.magnetic_activity)*p.cme_rate_multiplier,0.,.95);
}
double stellar_eruption_duration(const StellarEruptionEvent& e){return std::accumulate(e.stage_days.begin(),e.stage_days.end(),0.);}
stellar::engine::TimelineSample stellar_eruption_sample(const StellarEruptionEvent& e,double day){return stellar::engine::sample_timeline(e.stage_days,e.paused?e.paused_elapsed_days:day-e.start_day);}
void initialize_stellar_activity(std::int64_t seed,std::span<StellarSystem> systems,double day){
 require(in(day,0,1e12),"Invalid stellar activity epoch");
 for(auto& s:systems){if(!s.stellar_object)continue;if(s.stellar_activity){validate_stellar_activity(s);continue;}
  s.stellar_activity.emplace();const int count=1+(s.stellar_orbits?static_cast<int>(s.stellar_orbits->companions.size()):0);
  for(int component=0;component<count;++component){StellarActivityState a;a.seed=star_seed(seed,s.id,component);a.profile=make_stellar_activity_profile(stellar_host_physics(s,component),a.seed);a.last_day=day;
   const double rate=stellar_eruption_rate(a.profile);a.next_event_day=rate>0?day+stellar::engine::exponential_interval(rate,a.seed,0):1e15;s.stellar_activity->push_back(std::move(a));}
 }
}
void validate_stellar_activity_clock(std::optional<double> day){
 if(day)require(in(*day,0,1e12),"Invalid saved stellar activity clock");
}
void validate_stellar_activity(const StellarSystem& s){
 if(!s.stellar_activity)return;
 const auto& states=*s.stellar_activity;require(s.stellar_object.has_value()&&states.size()==1+(s.stellar_orbits?s.stellar_orbits->companions.size():0),"Invalid stellar activity component count");
 for(std::size_t component=0;component<states.size();++component){const auto& a=states[component];const auto& p=a.profile;
  require(p.version==1&&index(p.spectral)<8&&index(p.level)<5,"Invalid saved stellar activity version/class/state");
  require(p.spectral==eruption_spectral_class(stellar_host_physics(s,static_cast<int>(component)).type),"Stellar activity does not match star class");
  require(in(p.score,0,1)&&in(p.magnetic_activity,0,1)&&in(p.rotation_days,0,1000)&&in(p.age_modifier,0,5)&&in(p.rotation_modifier,0,5)&&in(p.evolution_modifier,0,10),"Invalid saved activity profile");
  for(double v:{p.flare_rate_multiplier,p.prominence_rate_multiplier,p.superflare_rate_multiplier,p.cme_rate_multiplier})require(in(v,.001,100),"Invalid saved activity multiplier");
  require(in(a.last_day,0,1e12)&&in(a.next_event_day,a.last_day,1e15)&&a.counter<(1ULL<<38)&&a.events.size()<=64,"Invalid stellar activity schedule");
  std::set<std::uint64_t> ids;
  for(const auto& e:a.events){require(e.system_id==s.id&&e.component==static_cast<int>(component)&&e.id&&ids.insert(e.id).second,"Invalid eruption identity");
   require(index(e.type)<5&&index(e.activity_source)<5&&e.visual_variant>=0&&e.visual_variant<12,"Invalid eruption type or variant");
   require(in(e.magnitude,.1,5)&&in(e.start_day,e.forced?-56.:0.,1e12)&&in(e.latitude,-pi/2,pi/2)&&in(e.longitude,-pi,pi)&&in(e.orientation,-pi,pi)&&in(e.scale,.1,5)&&in(e.brightness,.1,5)&&in(e.cme_probability,0,1),"Invalid eruption parameters");
   for(double d:e.stage_days)require(in(d,1e-8,14),"Invalid saved eruption duration");
   require(in(e.paused_elapsed_days,0,stellar_eruption_duration(e)),"Invalid paused eruption time");
  }
 }
}
void StellarActivityScheduler::rebuild(std::span<StellarSystem> systems){
 queue_={};base_=systems.data();size_=systems.size();
 for(std::size_t i=0;i<systems.size();++i)if(systems[i].stellar_activity){const auto& states=*systems[i].stellar_activity;
  for(std::size_t c=0;c<states.size();++c)if(stellar_eruption_rate(states[c].profile)>0)queue_.push({states[c].next_event_day,i,static_cast<int>(c),states[c].revision});}
}
void StellarActivityScheduler::reschedule(std::span<StellarSystem> systems,int id,int component){
 if(base_!=systems.data()||size_!=systems.size()){rebuild(systems);return;}
 for(std::size_t i=0;i<systems.size();++i)if(systems[i].id==id&&systems[i].stellar_activity){auto& a=systems[i].stellar_activity->at(component);++a.revision;
  if(stellar_eruption_rate(a.profile)>0)queue_.push({a.next_event_day,i,component,a.revision});break;}
 if(queue_.size()>size_*4+64)rebuild(systems);
}
std::vector<TravelingCmeLaunch> StellarActivityScheduler::advance(std::span<StellarSystem> systems,double day,const StellarActivityConfiguration& config){
 require(in(day,0,1e12),"Invalid stellar activity time");if(base_!=systems.data()||size_!=systems.size())rebuild(systems);
 std::vector<TravelingCmeLaunch> launches;
 while(!queue_.empty()&&queue_.top().day<=day){const auto due=queue_.top();queue_.pop();auto& s=systems[due.index];auto& a=s.stellar_activity->at(due.component);
  if(a.revision!=due.revision||a.next_event_day!=due.day)continue;
  const double rate=stellar_eruption_rate(a.profile,config);if(rate<=0)continue;
  prune(a,due.day,config);int active=0;for(const auto& e:a.events)if(e.start_day<=due.day&&!stellar_eruption_sample(e,due.day).finished)++active;
  if(active<config.simultaneous_caps.at(index(a.profile.level))){auto weights=config.type_weights.at(index(a.profile.level));weights[0]*=a.profile.prominence_rate_multiplier;weights[3]*=a.profile.superflare_rate_multiplier;
   double pick=stellar::engine::timeline_random(a.seed,a.counter+1,50)*std::accumulate(weights.begin(),weights.end(),0.);int kind=0;while(kind<4&&pick>=weights[kind])pick-=weights[kind++];
   auto e=make_event(a,s.id,due.component,static_cast<StellarEruptionType>(kind),due.day,config);++a.generated_counts[kind];
   if(e.cme_associated)++a.cme_opportunities;if(e.cme_escaped)++a.escaping_cmes;
   a.events.push_back(e);auto launch_event=e;
   if(e.cme_associated&&e.type!=StellarEruptionType::Cme){auto cme=e;cme.parent_event_id=e.id;cme.id=e.id+1;cme.type=StellarEruptionType::Cme;cme.start_day+=e.stage_days[0]+e.stage_days[1]*.5;
    const auto& t=config.timings[4];for(int k=0;k<4;++k)cme.stage_days[k]=(t.minimum_minutes[k]+stellar::engine::timeline_random(e.seed,0,60+k)*(t.maximum_minutes[k]-t.minimum_minutes[k]))/1440.;
    a.events.push_back(cme);launch_event=cme;++a.generated_counts[4];}
   if(launch_event.cme_escaped)launches.push_back({launch_event.id,s.id,due.component,launch_event.start_day+launch_event.stage_days[0]+launch_event.stage_days[1],launch_event.latitude,launch_event.longitude,350+950*launch_event.magnitude,launch_event.magnitude});
  }else{++a.counter;++a.suppressed_events;}
  a.last_day=due.day;a.next_event_day=due.day+stellar::engine::exponential_interval(rate,a.seed,a.counter);
  queue_.push({a.next_event_day,due.index,due.component,a.revision});
 }
 return launches;
}
std::uint64_t apply_developer_stellar_activity(FreshCampaignState& world,StellarActivityScheduler& scheduler,double day,const StellarActivityCommand& cmd){
 require(world.developer_provenance.has_value(),"Stellar activity tools require an isolated developer campaign");
 require(index(cmd.action)<=index(StellarActivityAction::ClearForced)&&index(cmd.type)<5&&index(cmd.level)<5&&cmd.variant>=0&&cmd.variant<12&&in(cmd.magnitude,.1,5)&&in(cmd.fraction,0,1)&&in(cmd.latitude,-pi/2,pi/2)&&in(cmd.longitude,-pi,pi)&&in(cmd.orientation,-pi,pi),"Invalid stellar activity command");
 auto it=std::find_if(world.systems.begin(),world.systems.end(),[&](const auto& s){return s.id==cmd.system_id;});require(it!=world.systems.end(),"Unknown stellar activity target");
 initialize_stellar_activity(world.seed,std::span<StellarSystem>(&*it,1),day);require(it->stellar_activity&&cmd.component>=0&&static_cast<std::size_t>(cmd.component)<it->stellar_activity->size(),"Missing stellar activity target");
 auto& a=it->stellar_activity->at(cmd.component);require(a.profile.spectral!=EruptionSpectralClass::Unsupported,"No solar-style eruptions for this object");
 world.developer_provenance->tools_used=true;
 const auto& config=stellar_activity_configuration();
 if(cmd.action==StellarActivityAction::SetLevel){a.profile.level=cmd.level;a.profile.score=std::array{.15,.4,.6,.8,.95}[index(cmd.level)];a.next_event_day=day+stellar::engine::exponential_interval(stellar_eruption_rate(a.profile),a.seed,a.counter);a.last_day=day;scheduler.reschedule(world.systems,it->id,cmd.component);return 0;}
 if(cmd.action==StellarActivityAction::ClearForced){std::erase_if(a.events,[](const auto& e){return e.forced;});return 0;}
 if(cmd.action==StellarActivityAction::Force){prune(a,day,config);require(a.events.size()<48,"Clear old test events before adding more");auto e=make_event(a,it->id,cmd.component,cmd.type,day,config,true);
  e.visual_variant=cmd.variant;e.magnitude=cmd.magnitude;
  if(!cmd.randomize_position){e.latitude=cmd.latitude;e.longitude=cmd.longitude;e.orientation=cmd.orientation;}
  a.events.push_back(e);return e.id;}
 auto found=std::find_if(a.events.begin(),a.events.end(),[&](const auto& e){return e.id==cmd.event_id;});require(found!=a.events.end(),"Eruption event no longer retained");auto& e=*found;
 if(cmd.action==StellarActivityAction::SetMagnitude)e.magnitude=cmd.magnitude;
 if(cmd.action==StellarActivityAction::SetVariant)e.visual_variant=cmd.variant;
 if(cmd.action==StellarActivityAction::MoveRegion){e.latitude=cmd.latitude;e.longitude=cmd.longitude;e.orientation=cmd.orientation;}
 if(cmd.action==StellarActivityAction::TogglePause){if(e.paused){e.start_day=day-e.paused_elapsed_days;e.paused=false;}else{e.paused_elapsed_days=stellar_eruption_sample(e,day).elapsed;e.paused=true;}}
 if(cmd.action==StellarActivityAction::Scrub){e.paused_elapsed_days=cmd.fraction*stellar_eruption_duration(e);e.paused=true;}
 if(cmd.action==StellarActivityAction::Restart){e.start_day=day;e.paused_elapsed_days=0;}
 return e.id;
}
} // namespace stellar::core
