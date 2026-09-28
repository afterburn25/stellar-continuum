#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <algorithm>
#include <tuple>
#include <stellar/core/stellar_coverage.hpp>
#include <unordered_set>
#include <map>

namespace stellar::core {
std::vector<TerritorialClaimSnapshot> campaign_territorial_claims(
    const IntegratedAdaptiveCampaignRuntime &campaign,int observer) {
  const auto &world=campaign.world().campaign();
  const bool all=world.developer_provenance && world.developer_provenance->full_exploration && observer==world.player_civilization_id;
  return campaign.diplomacy().territorial_claims(all?std::nullopt:std::optional<int>{observer});
}
std::vector<DeveloperEmpireSummary> developer_empire_summaries(const IntegratedAdaptiveCampaignRuntime &campaign) {
  const auto &world=campaign.world().campaign();
  if(!world.developer_provenance)throw std::invalid_argument("Empire monitoring requires an isolated developer campaign.");
  std::map<int,DeveloperEmpireSummary> rows;
  for(const auto &c:world.civilizations){
    auto &r=rows[c.id];r.civilization_id=c.id;r.home_system_id=c.home_system_id;r.name=c.name;
    r.stage=c.development_stage;r.player=c.id==world.player_civilization_id;r.expansion_allowed=c.expansion_allowed;
    if(const auto *research=campaign.research().try_get_civilization(c.id)){
      for(const auto &node:research->node_states())if(node.counts_as_established_knowledge())++r.established_research;
      r.active_projects=static_cast<int>(research->active_projects().size());
    }
  }
  for(const auto &c:world.colonies)if(auto it=rows.find(c.civilization_id);it!=rows.end()){
    auto &r=it->second;if(c.kind==SettlementKind::Colony)++r.colonies;else ++r.outposts;
    r.population_millions+=c.population_millions;
    for(const auto &building:c.surface_buildings)if(!building.is_complete||building.pending_upgrade_type_id)++r.buildings_in_progress;
  }
  for(const auto &f:world.fleets)if(auto it=rows.find(f.civilization_id);it!=rows.end())++it->second.fleets;
  for(const auto &claim:campaign.diplomacy().territorial_claims())if(claim.active)
    if(auto it=rows.find(claim.claimant_civilization_id);it!=rows.end())++it->second.active_claims;
  for(const auto &e:world.economies)if(auto it=rows.find(e.civilization_id);it!=rows.end()){
    auto &r=it->second;r.credits=e.credits;r.industry=e.industry;
    r.research_spending_per_day=e.last_research_spending_per_day;r.research_funding_fraction=e.last_research_funding_fraction;
  }
  std::vector<DeveloperEmpireSummary> result;result.reserve(rows.size());
  for(auto &[id,row]:rows)result.push_back(std::move(row));
  return result;
}
AdaptiveResearchView developer_empire_research(const IntegratedAdaptiveCampaignRuntime &campaign,int id) {
  if(!campaign.world().campaign().developer_provenance)throw std::invalid_argument("Empire monitoring requires an isolated developer campaign.");
  return campaign.research_runtime().authority().build_view(campaign.research().get_civilization(id));
}
void fully_explore_developer_galaxy(FreshCampaignState &world){
  if(!world.developer_provenance)
    throw std::invalid_argument("Full exploration requires an isolated developer campaign.");
  const int observer=world.player_civilization_id;
  if(std::ranges::find(world.civilizations,observer,&Civilization::id)==world.civilizations.end())
    throw std::invalid_argument("Full exploration requires an existing player civilization.");
  world.developer_provenance->tools_used=true;
  for(const auto &system:world.systems)world.knowledge.mark_system_fully_surveyed(observer,system.id);
  for(const auto &civilization:world.civilizations)world.knowledge.reveal_civilization(observer,civilization.id);
  if(world.core||world.galactic_core){
    world.knowledge.unlock_galactic_core_access(observer);
    (void)world.knowledge.record_galactic_core_exploration(observer);
  }
  world.developer_provenance->full_exploration=true;
}
void validate_developer_coverage(const FreshCampaignState &world){
  if(!world.developer_provenance)return;
  const auto &p=*world.developer_provenance;
  if(!p.full_celestial_coverage){
    if(!p.coverage_generation_version.empty()||!p.coverage_forced_system_ids.empty())
      throw std::invalid_argument("Inactive coverage contains forced-object metadata.");
    return;
  }
  if(!p.tools_used||p.coverage_generation_version!=stellar_coverage_version||p.coverage_forced_system_ids.size()>stellar_object_type_count)
    throw std::invalid_argument("Invalid coverage provenance/version.");
  std::unordered_set<int> forced;
  for(const int id:p.coverage_forced_system_ids){
    const auto found=std::ranges::find(world.systems,id,&StellarSystem::id);
    if(!forced.insert(id).second||found==world.systems.end()||!found->stellar_object||found->stellar_object->measured_anchor||found->stellar_catalog_id)
      throw std::invalid_argument("Invalid forced stellar system reference.");
  }
  std::array<bool,stellar_object_type_count> seen{};
  for(const auto &s:world.systems)if(s.stellar_object)seen.at(static_cast<std::size_t>(s.stellar_object->type))=true;
  if(std::ranges::find(seen,false)!=seen.end()||!world.galactic_core||!world.galactic_core->black_hole)
    throw std::invalid_argument("Developer coverage is missing required stellar types or its central black hole.");
}
void set_developer_central_black_hole_state(IntegratedAdaptiveCampaignRuntime &campaign,CentralBlackHoleState state){
  auto &w=campaign.world().campaign();
  if(!w.developer_provenance||!w.galactic_core||!w.galactic_core->black_hole||!w.generation_metadata)
    throw std::invalid_argument("Central state override requires a developer galaxy with an existing central black hole.");
  auto value=central_black_hole_with_state(*w.galactic_core->black_hole,state);
  w.galactic_core->black_hole=value;w.generation_metadata->galactic_core=w.galactic_core;w.developer_provenance->tools_used=true;
}
void set_developer_ai_control(IntegratedAdaptiveCampaignRuntime &campaign,bool enabled){
  auto &world=campaign.world().campaign();
  if(!world.developer_provenance)
    throw std::invalid_argument("AI takeover requires an isolated developer campaign.");
  if(world.developer_provenance->player_ai_control==enabled)return;
  world.developer_provenance->player_ai_control=enabled;
  world.developer_provenance->tools_used=true;
  campaign.core().strategic_runtime().remove_civilization(world.player_civilization_id);
}
void validate_developer_simulation_state(const DeveloperSimulationState &state){
  if((state.speed!=1&&state.speed!=2&&state.speed!=5&&state.speed!=10&&state.speed!=25)||
     state.backlog_nanoseconds<0||state.tactical_backlog_nanoseconds<0||
     (!state.fixed_ticks&&(state.completed_ticks||state.backlog_nanoseconds||
                          state.tactical_completed_ticks||state.tactical_backlog_nanoseconds)))
    throw std::invalid_argument("Invalid developer fixed simulation state.");
}

DeveloperResearchSetupResult initialize_developer_research(
    IntegratedAdaptiveCampaignRuntime &campaign,DeveloperResearchOptions options){
  auto &world=campaign.world().campaign();
  if(!world.developer_provenance)
    throw std::invalid_argument("Research overrides require an isolated developer campaign.");
  DeveloperResearchSetupResult result;
  if(!options.complete_normal_research&&!options.complete_special_research)return result;
  const auto id=world.player_civilization_id;
  auto &research=campaign.research();
  auto &state=detail::AdaptiveResearchCampaignStateAccess::get_civilization(research,id);
  const auto &authority=research.runtime().authority();
  std::vector<const AdaptiveResearchNodeDefinition*> nodes;
  for(const auto &node:authority.catalog().nodes())
    if(node.public_normal_research?options.complete_normal_research:options.complete_special_research)
      nodes.push_back(&node);
  std::sort(nodes.begin(),nodes.end(),[](auto a,auto b){
    return std::tie(a->graph_depth,a->id)<std::tie(b->graph_depth,b->id);
  });
  // Tag before any override: even an interrupted setup must never become a
  // normal player save. Real funded projects close their milestone commitments.
  world.developer_provenance->tools_used=true;
  for(const auto *node:nodes){
    const auto *prior=state.try_get_node_state(node->id);
    if(prior&&prior->maturity==ResearchMaturity::mature)continue;
    auto established=authority.kernel().establish_technology(state,node->id,
        research.get_start(id).applicability_context_id);
    if(!established.accepted)throw std::runtime_error(established.message);
    (void)detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(research,id,node->id,true);
    (void)research.edit_plan(id,ResearchPlanCommand::RemoveQueued,node->id);
    if(node->public_normal_research)++result.completed_normal;else ++result.completed_special;
    result.events.insert(result.events.end(),established.events.begin(),established.events.end());
  }
  world.developer_provenance->normal_research_completed|=options.complete_normal_research;
  world.developer_provenance->special_research_completed|=options.complete_special_research;
  AdaptiveResearchCampaignProgression::synchronize_development_stages(world,research);
  return result;
}
} // namespace stellar::core
