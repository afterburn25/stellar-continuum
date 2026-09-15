#include "native_construction_controller.hpp"
#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_construction {
namespace {
using namespace stellar::core;
template<class Range, class Member> auto *find_one(Range &r, int id, Member m) {
  const auto i=std::ranges::find(r,id,m); return i==r.end()?nullptr:&*i;
}
struct Context {
  FreshCampaignState &world;
  const Civilization &player;
  ConstructionState &state;
  CivilizationEconomy &economy;
  ConstructionWorld command;
  ConstructionReadView read;
};
Context context(CampaignFrame &frame) {
  auto &runtime=frame.runtime(); auto &world=runtime.world().campaign();
  auto *player=find_one(world.civilizations,world.player_civilization_id,&Civilization::id);
  auto *state=find_one(world.construction,world.player_civilization_id,&ConstructionState::civilization_id);
  auto *economy=find_one(world.economies,world.player_civilization_id,&CivilizationEconomy::civilization_id);
  if(!player||!player->is_player||!state||!economy) throw std::runtime_error("The player campaign has no complete construction state.");
  auto *research=&runtime.research();
  auto query=[research](int civilization,std::string_view id){
    return AdaptiveResearchConstructionCapabilityView(*research)
        .has_civilization_capability(civilization,id);
  };
  ConstructionWorld command{world.civilizations,world.bodies,world.construction,world.colonies,world.economies,{},query};
  return {world,*player,*state,*economy,command,command.read()};
}
std::string requirement_name(std::string_view id) {
  if(id=="orbital_industry") return "Orbital Industry";
  if(id=="warp_field_control") return "Warp Field Control";
  return std::string(id);
}
void append(std::ostringstream &out,std::string_view value){out<<value.size()<<':'<<value<<';';}
bool same_currency(const SovereignCurrencyDefinition&a,const SovereignCurrencyDefinition&b){return a.name==b.name&&a.code==b.code&&a.symbol==b.symbol&&a.local_units_per_budget_unit==b.local_units_per_budget_unit;}
bool same_action_terms(const NativeConstructionProject&a,const NativeConstructionProject&b,ConstructionOrderIntent intent){
  const auto &x=intent==ConstructionOrderIntent::Start?a.start:a.queue;
  const auto &y=intent==ConstructionOrderIntent::Start?b.start:b.queue;
  return a.id==b.id&&a.industry_cost==b.industry_cost&&a.credit_cost==b.credit_cost&&a.upkeep_credits_per_day==b.upkeep_credits_per_day&&x.will_start_now==y.will_start_now&&x.will_queue==y.will_queue;
}
struct Projection{NativeConstructionView view;std::string species,signature;};
Projection project(CampaignFrame &frame,std::uint64_t generation){
  auto c=context(frame); Projection out; auto &v=out.view;
  v.campaign_generation=generation; v.player_civilization_id=c.player.id; v.home_system_id=c.player.home_system_id;
  out.species=c.player.species_id; v.currency=sovereign_currency_for_civilization(c.world.civilizations,c.player.id);
  v.treasury_credits=c.economy.credits; v.available_industry=c.economy.industry; v.formatted_treasury=v.currency.format(c.economy.credits);
  std::unordered_set<std::string> known(c.state.completed_project_ids.begin(),c.state.completed_project_ids.end());
  if(c.state.active_project_id) known.insert(*c.state.active_project_id);
  for(const auto&o:c.state.queued_projects) known.insert(o.project_id);
  for(const auto&p:available_construction_projects(c.read,c.player.id)) known.insert(p.id);
  const auto queue_blocker=construction_queue_blocker(c.read,c.player.id);
  double cumulative_queue_days{};
  if (c.state.active_project_id) {
    const auto &active = get_construction_project(*c.state.active_project_id);
    cumulative_queue_days =
        std::max(0., active.industry_cost - c.state.active_project_progress) /
        construction_project_industry_per_day;
  }
  std::unordered_map<std::string, double> queued_completion_days;
  for (const auto &order : c.state.queued_projects) {
    cumulative_queue_days += get_construction_project(order.project_id).industry_cost /
                             construction_project_industry_per_day;
    queued_completion_days.emplace(order.project_id, cumulative_queue_days);
  }
  for(const auto &definition:construction_project_catalog()){
    if(!known.contains(definition.id)) continue;
    NativeConstructionProject p;
    p.id=definition.id;p.name=definition.name;p.description=definition.description;p.category=definition.category;
    p.industry_cost=definition.industry_cost;p.credit_cost=definition.credit_cost;p.upkeep_credits_per_day=definition.upkeep_credits_per_day;p.industry_per_day=definition.industry_per_day;
    p.formatted_credit_cost=v.currency.format(p.credit_cost);p.formatted_upkeep_rate=v.currency.format_rate(-p.upkeep_credits_per_day);
    for(const auto&id:definition.required_technologies)p.requirements.push_back(requirement_name(id));
    for(const auto&id:definition.required_projects)p.requirements.push_back(get_construction_project(id).name);
    p.complete=std::ranges::contains(c.state.completed_project_ids,p.id);p.active=c.state.active_project_id==p.id;
    const auto queued=std::ranges::find(c.state.queued_projects,p.id,&QueuedConstructionProject::project_id);
    p.queued=queued!=c.state.queued_projects.end();
    if(p.queued){p.queue_position=static_cast<int>(std::distance(c.state.queued_projects.begin(),queued))+1;p.authorization_credits=queued->authorization_credits;if(p.queue_position==1&&!c.state.active_project_id)p.queue_blocker=queue_blocker;}
    if(p.active){p.industry_progress=c.state.active_project_progress;p.authorization_credits=c.state.active_project_authorization_credits;}
    p.industry_remaining=p.complete?0.:std::max(0.,p.industry_cost-p.industry_progress);
    p.progress_fraction=p.complete?1.:p.industry_cost>0.?std::clamp(p.industry_progress/p.industry_cost,0.,1.):0.;
    p.minimum_days_remaining = p.complete ? 0. : p.queued
        ? queued_completion_days.at(p.id)
        : p.industry_remaining / construction_project_industry_per_day;
    if(p.active||p.queued)p.cancellation_refund=construction_cancellation_refund_preview(c.state,p.id);
    p.formatted_authorization=v.currency.format(p.authorization_credits);p.formatted_cancellation_refund=v.currency.format(p.cancellation_refund);
    const auto start=assess_construction_project_order(c.read,c.player.id,p.id,ConstructionOrderIntent::Start);
    const auto queue=assess_construction_project_order(c.read,c.player.id,p.id,ConstructionOrderIntent::Queue);
    p.start={start.accepted,start.will_start_now,start.will_queue,start.message};
    p.queue={queue.accepted,queue.will_start_now,queue.will_queue,queue.message};
    v.projects.push_back(std::move(p));
  }
  std::ostringstream s;s.imbue(std::locale::classic());s<<generation<<';'<<v.player_civilization_id<<';'<<v.home_system_id<<';'<<std::hexfloat<<v.treasury_credits<<';'<<v.available_industry<<';';append(s,out.species);append(s,v.currency.name);append(s,v.currency.code);append(s,v.currency.symbol);s<<v.currency.local_units_per_budget_unit<<';';
  for(const auto&p:v.projects){append(s,p.id);s<<p.complete<<';'<<p.active<<';'<<p.queued<<';'<<p.queue_position<<';'<<p.industry_progress<<';'<<p.authorization_credits<<';'<<p.cancellation_refund<<';'<<p.start.enabled<<';'<<p.start.will_start_now<<';'<<p.start.will_queue<<';';append(s,p.start.message);s<<p.queue.enabled<<';'<<p.queue.will_start_now<<';'<<p.queue.will_queue<<';';append(s,p.queue.message);}
  out.signature=s.str();return out;
}
NativeConstructionCommandOutcome stale(){return {false,"Construction changed; refresh before issuing an order.",0.};}
}
void NativeConstructionController::require_owner()const{if(std::this_thread::get_id()!=owner_)throw std::logic_error("Native construction control must run on the simulation owner thread.");}
NativeConstructionView NativeConstructionController::build(CampaignFrame&f,std::uint64_t g){require_owner();if(generation_&&g<*generation_)throw std::invalid_argument("A stale campaign generation cannot replace construction.");if(!generation_||*generation_!=g){generation_=g;revision_=0;signature_.reset();projected_view_.reset();player_species_id_.reset();}auto p=project(f,g);if(!signature_||*signature_!=p.signature){if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native construction revision is exhausted.");++revision_;signature_=p.signature;}p.view.construction_revision=revision_;projected_view_=p.view;player_species_id_=p.species;return p.view;}
NativeConstructionCommandOutcome NativeConstructionController::start(CampaignFrame&f,std::uint64_t g,std::uint64_t r,std::string_view id){require_owner();if(!generation_||*generation_!=g||r!=revision_||!projected_view_||!player_species_id_)return stale();auto cur=project(f,g);const auto old=std::ranges::find(projected_view_->projects,id,&NativeConstructionProject::id),now=std::ranges::find(cur.view.projects,id,&NativeConstructionProject::id);if(old==projected_view_->projects.end()||now==cur.view.projects.end()||projected_view_->player_civilization_id!=cur.view.player_civilization_id||projected_view_->home_system_id!=cur.view.home_system_id||*player_species_id_!=cur.species||!same_currency(projected_view_->currency,cur.view.currency)||!same_action_terms(*old,*now,ConstructionOrderIntent::Start))return stale();if(!now->start.enabled)return {false,now->start.message,0.};auto c=context(f);auto result=start_construction_project(c.command,c.player.id,id);if(result.accepted){signature_.reset();projected_view_.reset();}return {result.accepted,result.message,0.};}
NativeConstructionCommandOutcome NativeConstructionController::queue(CampaignFrame&f,std::uint64_t g,std::uint64_t r,std::string_view id){require_owner();if(!generation_||*generation_!=g||r!=revision_||!projected_view_||!player_species_id_)return stale();auto cur=project(f,g);const auto old=std::ranges::find(projected_view_->projects,id,&NativeConstructionProject::id),now=std::ranges::find(cur.view.projects,id,&NativeConstructionProject::id);if(old==projected_view_->projects.end()||now==cur.view.projects.end()||projected_view_->player_civilization_id!=cur.view.player_civilization_id||projected_view_->home_system_id!=cur.view.home_system_id||*player_species_id_!=cur.species||!same_currency(projected_view_->currency,cur.view.currency)||!same_action_terms(*old,*now,ConstructionOrderIntent::Queue))return stale();if(!now->queue.enabled)return {false,now->queue.message,0.};auto c=context(f);auto result=queue_construction_project(c.command,c.player.id,id);if(result.accepted){signature_.reset();projected_view_.reset();}return {result.accepted,result.message,0.};}
NativeConstructionCommandOutcome NativeConstructionController::cancel(CampaignFrame&f,std::uint64_t g,std::uint64_t r,std::string_view id){require_owner();if(!generation_||*generation_!=g||r!=revision_||!signature_)return stale();auto cur=project(f,g);if(cur.signature!=*signature_)return stale();auto c=context(f);auto result=cancel_construction_project(c.command,c.player.id,id);if(result.accepted){signature_.reset();projected_view_.reset();}return {result.accepted,result.message,result.refunded_credits};}
}
