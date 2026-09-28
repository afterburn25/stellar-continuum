#include <stellar/core/galaxy_configuration.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace stellar::core;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char** argv)try{
  check(argc==2,"Catalog required");const auto catalog=load_nearby_catalog(argv[1]);
  std::set<std::string> fingerprints;bool present=false,absent=false;
  for(int morphology=0;morphology<6;++morphology){
    std::set<PopulationState> random_states;
    for(int seed=0;seed<128;++seed){GalaxyGenerationConfig c;c.base_seed=seed;c.morphology=static_cast<GalaxyMorphology>(morphology);c=resolve_galaxy_configuration(c);
      check(c==resolve_galaxy_configuration(c),"Random resolution changed");random_states.insert(c.resolved_population);
      present|=galaxy_has_central_black_hole(c);absent|=!galaxy_has_central_black_hole(c);
    }check(random_states.size()>=3,"Random population does not vary with seed");
    for(int state=0;state<6;++state){
      GalaxyGenerationConfig c;c.base_seed=84722913;c.morphology=static_cast<GalaxyMorphology>(morphology);c.requested_population=static_cast<PopulationSelection>(state);c.system_count=250;c.pre_warp_count=4;c=resolve_galaxy_configuration(c);
      check(fingerprints.insert(c.fingerprint).second,"Different morphology or request shared a fingerprint");
      const auto pair=galaxy_visual_pair(c.morphology,c.resolved_population);
      check(pair.preview_id.find("stars_included")!=std::string::npos&&pair.map_id.find("gas_dust_only")!=std::string::npos,"Preview/map variants crossed");
      check(pair.fallback&&!pair.diagnostic.empty(),"Missing explicit population artwork was not reported");
      const auto& mask=galaxy_density_mask(c.map_asset_id);check(mask.sample(-.1,.5)==0&&mask.sample(.5,1.1)==0,"Mask leaked outside rectangle");
      PersistableFreshCampaignOptions options{"2050-03-21T00:00:00Z",250,4,1,"terran_baseline",StellarPopulationOptions{c.morphology,c.resolved_population},false,c};
      auto a=seed_persistable_fresh_campaign(c.base_seed,catalog,options),b=seed_persistable_fresh_campaign(c.base_seed,catalog,options);
      const GalaxyPayloadCaptureOptions capture{0,"test","2050-03-21T00:00:00Z"};
      const auto payload=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(a,capture));
      check(payload==encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(b,capture)),"Same generation configuration did not reproduce actual galaxy");
      auto loaded=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(payload)).galaxy;
      const auto reloaded=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(loaded,capture));
      check(nlohmann::json::parse(payload)==nlohmann::json::parse(reloaded),"Save/load changed configuration or actual galaxy");
      check(a.galactic_core.has_value()==galaxy_has_central_black_hole(c),"Central occupancy was not authoritative");
      if(!a.galactic_core)check(!a.core,"Absent SMBH left an invisible center entity");
      const auto frame=galaxy_footprint_frame(c.morphology,250);int outside=0,anchor_outside=0;
      for(const auto& star:a.systems){double density=mask.sample((star.position.x-frame.left)/frame.width,(star.position.y-frame.top)/frame.height);if(density<.054){if(star.stellar_catalog_id)++anchor_outside;else ++outside;}}
      if(outside||anchor_outside)std::cerr<<"Footprint failure: morphology="<<galaxy_morphology_id(c.morphology)<<" state="<<state<<" generated="<<outside<<" measured="<<anchor_outside<<'\n';
      check(outside==0&&anchor_outside==0,"Systems were placed in empty margins");
      if(state==0)std::cout<<galaxy_morphology_id(c.morphology)<<" anchors outside footprint="<<anchor_outside<<"\n";
      auto bad=c;bad.fingerprint[0]=bad.fingerprint[0]=='0'?'1':'0';bool rejected=false;try{validate_galaxy_configuration(bad);}catch(const std::exception&){rejected=true;}check(rejected,"Forged generation fingerprint accepted");
    }
  }
  check(present&&absent,"SMBH always present or always absent");
  for(int count:{500,1000,2500,5000,10000})for(int m=0;m<6;++m){GalaxyGenerationConfig c;c.base_seed=100;c.system_count=count;c.morphology=static_cast<GalaxyMorphology>(m);c=resolve_galaxy_configuration(c);auto world=seed_persistable_fresh_campaign(c.base_seed,catalog,{"2050-03-21T00:00:00Z",count,6,1,"terran_baseline",StellarPopulationOptions{c.morphology,c.resolved_population},false,c});check(world.systems.size()==static_cast<std::size_t>(count),"Footprint rejected valid galaxy size");}
  std::cout<<"Canonical galaxy generation, all morphologies/states/sizes and persistence passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
