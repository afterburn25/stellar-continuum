#include <stellar/core/phenomenon_art.hpp>
#include <stellar/engine/foundation.hpp>
#include "phenomenon_art_data.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>

namespace stellar::core {
namespace {
PhenomenonType family(PhenomenonType t){
  if(t==PhenomenonType::Radiation||t==PhenomenonType::Exotic)return PhenomenonType::RareEnergetic;
  if(t==PhenomenonType::IonizedGas)return PhenomenonType::DiffuseGas;
  if(t==PhenomenonType::DustLane)return PhenomenonType::DarkNebula;
  return t;
}
std::uint64_t hash(std::string_view text){std::uint64_t h=1469598103934665603ULL;for(unsigned char c:text){h^=c;h*=1099511628211ULL;}return h;}
std::vector<const PhenomenonVisualAsset*> pool(PhenomenonType t){std::vector<const PhenomenonVisualAsset*> out;for(const auto& a:phenomenon_art_catalog())if(a.functional_type==family(t))out.push_back(&a);return out;}
}
const std::vector<PhenomenonVisualAsset>& phenomenon_art_catalog(){
  static const auto assets=[] {
    const auto j=nlohmann::json::parse(phenomenon_art_data);
    if(j.at("schemaVersion")!=1)throw std::logic_error("Unsupported phenomenon art manifest");
    const std::map<std::string,PhenomenonType> types{{"emission",PhenomenonType::Emission},{"reflection",PhenomenonType::Reflection},{"dark_nebula",PhenomenonType::DarkNebula},{"molecular_cloud",PhenomenonType::MolecularCloud},{"hii_region",PhenomenonType::HII},{"diffuse_gas",PhenomenonType::DiffuseGas},{"mixed_nebula",PhenomenonType::MixedNebula},{"supernova_remnant",PhenomenonType::SupernovaRemnant},{"rare_energetic",PhenomenonType::RareEnergetic}};
    std::vector<PhenomenonVisualAsset> out;std::set<std::string> ids,files;std::set<PhenomenonType> found;
    for(const auto& a:j.at("assets")){
      PhenomenonVisualAsset v;v.asset_id=a.at("assetId");v.filename=a.at("filename");v.source_category=a.at("sourceCategory");v.path=a.at("path");v.functional_type=types.at(a.at("functionalType"));v.variant_index=a.at("variantIndex");v.width=a.at("dimensions").at(0);v.height=a.at("dimensions").at(1);v.aspect_ratio=a.at("aspectRatio");v.system_view_eligible=a.at("systemViewEligible");v.galaxy_view_eligible=a.at("galaxyViewEligible");v.obscuring=a.at("blendMode")=="obscuring";
      if(!ids.insert(v.asset_id).second||!files.insert(v.filename).second||v.width<=0||v.height<=0||std::abs(v.aspect_ratio-v.width/static_cast<double>(v.height))>1e-8||v.path!="assets/visual/phenomena/"+v.filename)throw std::logic_error("Invalid or duplicate phenomenon art mapping");
      if((v.functional_type==PhenomenonType::SupernovaRemnant||v.functional_type==PhenomenonType::RareEnergetic)&&v.system_view_eligible)throw std::logic_error("Map-only phenomenon marked as generic system background");
      found.insert(v.functional_type);out.push_back(std::move(v));
    }
    if(found.size()!=types.size())throw std::logic_error("Missing phenomenon art family");return out;
  }();return assets;
}
const PhenomenonVisualAsset& phenomenon_art_role(std::string_view role){
  static const auto roles=nlohmann::json::parse(phenomenon_art_data).at("roles");const auto id=roles.at(std::string(role)).get<std::string>();
  for(const auto& a:phenomenon_art_catalog())if(a.asset_id==id)return a;throw std::logic_error("Unknown phenomenon artwork role");
}
const PhenomenonVisualAsset& phenomenon_art(const GalaxyPhenomena& f,const GalaxyPhenomenon& r){
  if(!r.asset_id.empty()){
    for(const auto& a:phenomenon_art_catalog())if(a.asset_id==r.asset_id){if(a.functional_type!=family(r.type))throw std::invalid_argument("Saved phenomenon artwork family mismatch");return a;}
    throw std::invalid_argument("Unknown saved phenomenon artwork ID");
  }
  // Frozen deterministic migration for old saves; never mutate their simulation.
  const auto candidates=pool(r.type);if(candidates.empty())throw std::logic_error("Missing phenomenon artwork");
  stellar::engine::DeterministicRandom rng(hash(f.configuration_fingerprint)^r.shape.seed^r.id);
  return *candidates[rng.next_u64()%candidates.size()];
}
void assign_phenomenon_art(GalaxyPhenomena& f,std::uint64_t seed){
  for(std::size_t type=0;type<phenomenon_type_count;++type){
    auto candidates=pool(static_cast<PhenomenonType>(type));if(candidates.empty())continue;
    stellar::engine::DeterministicRandom rng(seed^(type*0x9e3779b97f4a7c15ULL));
    for(std::size_t i=candidates.size();i>1;--i)std::swap(candidates[i-1],candidates[rng.next_u64()%i]);
    std::size_t used=0;for(auto& r:f.regions)if(static_cast<std::size_t>(r.type)==type){r.asset_id=candidates[used++%candidates.size()]->asset_id;r.mirrored=(rng.next_u64()&1)!=0;}
  }
}
std::string phenomenon_art_inventory(){std::map<std::string,int> counts;for(const auto& a:phenomenon_art_catalog())++counts[a.source_category];std::ostringstream out;out<<"GALAXY PHENOMENA ART LIBRARY: "<<phenomenon_art_catalog().size()<<" files\n";for(const auto& [name,count]:counts)out<<name<<": "<<count<<'\n';return out.str();}
std::string phenomenon_art_usage(const GalaxyPhenomena& f){
  std::ostringstream out;std::set<std::string> used;std::map<std::string,int> counts;
  out<<"GALAXY PHENOMENA ART USAGE\nNatural/saved field count: "<<f.regions.size()<<'\n';
  for(const auto& r:f.regions){const auto& a=phenomenon_art(f,r);used.insert(a.asset_id);++counts[phenomenon_definition(r.type).name];
    out<<"ID "<<r.id<<" | "<<phenomenon_definition(r.type).name<<" | "<<a.filename<<'\n';
    out<<"  Position "<<r.shape.x<<", "<<r.shape.y<<" | extent "<<r.shape.extent_x<<", "<<r.shape.extent_y<<" | rotation "<<r.shape.rotation<<" rad | mirror "<<r.mirrored<<'\n';
    out<<"  System eligible "<<a.system_view_eligible<<" | systems "<<r.systems_contained.size()<<" | base weight "<<r.natural_weight<<" | population x"<<r.population_modifier<<" | morphology x"<<r.morphology_modifier<<'\n';
  }
  for(const auto& [name,n]:counts)out<<name<<": "<<n<<'\n';out<<"SUPPLIED ART NOT USED (expected):\n";
  for(const auto& a:phenomenon_art_catalog())if(!used.contains(a.asset_id))out<<a.filename<<'\n';return out.str();
}
} // namespace stellar::core
