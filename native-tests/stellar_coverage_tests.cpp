#include <stellar/core/stellar_coverage.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <set>
using namespace stellar::core;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv)try{
  require(argc==3,"Expected catalog and research root.");const auto catalog=load_nearby_catalog(argv[1]);
  for(const auto size:{250,500,1000,2500}){
    const auto generated=[&](bool coverage){return seed_persistable_fresh_campaign(-9142050,catalog,
        {"2050-03-21T00:00:00Z",size,6,1,"terran_baseline",StellarPopulationOptions{},coverage});};
    auto normal=generated(false),covered=generated(true),again=generated(true);
    require(!normal.developer_provenance&&covered.developer_provenance->full_celestial_coverage,"Coverage leaked into normal generation.");
    validate_developer_coverage(covered);
    bool normal_denied=false;try{(void)build_developer_celestial_index(normal);}catch(const std::exception&){normal_denied=true;}
    require(normal_denied,"Normal observer accessed hidden celestial index.");
    const auto index=build_developer_celestial_index(covered);int forced_total=0;
    for(const auto &c:index.counts){require(c.natural+c.forced>0,"Coverage report omitted a required class.");forced_total+=c.forced;}
    require(forced_total==static_cast<int>(covered.developer_provenance->coverage_forced_system_ids.size())&&
        index.entries.size()==covered.systems.size()+1,"Natural/forced report or single central object count incorrect.");
    require(covered.systems.size()==normal.systems.size(),"Coverage changed requested system count.");
    require(covered.developer_provenance==again.developer_provenance,"Coverage metadata is nondeterministic.");
    for(std::size_t i=0;i<normal.systems.size();++i){
      const auto &n=normal.systems[i],&c=covered.systems[i],&a=again.systems[i];
      require(c.id==n.id&&c.position.x==n.position.x&&c.position.y==n.position.y&&
          c.position.depth_light_years==n.position.depth_light_years&&c.stellar_object==a.stellar_object,"Coverage moved systems or changed on repeated seed.");
      if(n.stellar_catalog_id||n.stellar_object->hooks.is_rare_discovery)require(c.stellar_object==n.stellar_object,"Coverage replaced a measured anchor or natural rare object.");
    }
    auto physics=covered.systems;require(ensure_stellar_coverage(1,physics).forced_system_ids.empty(),"Repeated coverage added duplicate rare objects.");
    if(size==250){
      auto runtime=IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(covered));
      const auto origin=runtime.world().campaign().galactic_core;
      for(const auto state:{CentralBlackHoleState::Quiescent,CentralBlackHoleState::Accreting,CentralBlackHoleState::RelativisticJets}){
        set_developer_central_black_hole_state(runtime,state);const auto &core=runtime.world().campaign().galactic_core;
        require(core->x==origin->x&&core->y==origin->y&&core->black_hole->state==state,"SMBH state override moved or duplicated the central object.");
      }
      const auto saved=capture_developer_campaign_json(runtime,{0,"test","2050-03-21T00:00:00Z"});
      auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[2]),saved);
      auto loaded=std::move(restored).activate();validate_developer_coverage(loaded.world().campaign());
      require(nlohmann::json::parse(saved)==nlohmann::json::parse(capture_developer_campaign_json(loaded,{0,"test","2050-03-21T00:00:00Z"})),"Coverage or SMBH state did not round-trip.");
      auto broken=nlohmann::json::parse(saved);broken["CelestialCoverage"]["ForcedSystemIds"].push_back(999999);
      bool rejected=false;try{(void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[2]),broken.dump());}catch(const std::exception&){rejected=true;}
      require(rejected,"Save accepted an absent forced object.");
      rejected=false;try{(void)capture_player_campaign_v17(runtime,{0,"test","2050-03-21T00:00:00Z"});}catch(const std::exception&){rejected=true;}
      require(rejected,"Coverage campaign became a normal player save.");
      // Instrumentation is transient and cannot alter authoritative decisions.
      auto plain_state=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[2]),saved);
      auto plain=std::move(plain_state).activate();loaded.set_profiling_enabled(true);
      (void)plain.advance(.25,.25);(void)loaded.advance(.25,.25);
      require(capture_developer_campaign_json(plain,{.25,"test","2050-03-21T00:00:00Z"})==
          capture_developer_campaign_json(loaded,{.25,"test","2050-03-21T00:00:00Z"}),"Profiling changed canonical continuation.");
      for(const auto &sample:loaded.performance_samples())require(sample.timing.samples==1,"Phase timing did not observe the authoritative tick.");
      loaded.set_profiling_enabled(false);loaded.reset_performance_counters();
      (void)loaded.advance(.25,.5);(void)plain.advance(.25,.5);
      for(const auto &sample:loaded.performance_samples())require(sample.timing.samples==0,"Disabled phase timing retained samples.");
      require(capture_developer_campaign_json(plain,{.5,"test","2050-03-21T00:00:00Z"})==
          capture_developer_campaign_json(loaded,{.5,"test","2050-03-21T00:00:00Z"}),"Resetting/disabling profiling changed simulation.");
    }
  }
  std::cout<<"stellar coverage tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
