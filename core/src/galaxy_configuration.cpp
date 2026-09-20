#include <stellar/core/galaxy_configuration.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/sha256.hpp>
#include "galaxy_configuration_data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <sstream>
namespace stellar::core {
namespace {
using Json=nlohmann::json;
const Json& visuals(){static const auto j=Json::parse(galaxy_visual_data);return j;}
const Json& rules(){static const auto j=Json::parse(galaxy_generation_data);return j;}
std::string digest(const Json& j){const auto s=j.dump();const auto bytes=stellar::engine::sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});std::string out;out.reserve(64);for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::uint64_t stream(std::string_view text){std::uint64_t value=0;for(char c:text.substr(0,16))value=(value<<4)|static_cast<unsigned>(c<='9'?c-'0':c-'a'+10);return value;}
}
std::string_view galaxy_morphology_id(GalaxyMorphology m){constexpr std::array ids{"spiral","barred_spiral","lenticular","elliptical","irregular","ring"};return ids.at(static_cast<std::size_t>(m));}
std::string_view population_selection_id(PopulationSelection s){constexpr std::array ids{"random","starburst","active","mature","aging","quiescent"};return ids.at(static_cast<std::size_t>(s));}
std::string_view population_selection_label(PopulationSelection s){constexpr std::array labels{"Random","Starburst","Active","Mature","Aging","Quiescent"};return labels.at(static_cast<std::size_t>(s));}
PopulationState resolve_population_state(std::int64_t seed,GalaxyMorphology morphology,PopulationSelection request,std::string_view version){
  (void)population_selection_id(request);const auto id=galaxy_morphology_id(morphology);
  if(request!=PopulationSelection::Random)return static_cast<PopulationState>(static_cast<int>(request)-1);
  const auto weights=rules().at("morphologies").at(id).at("randomPopulationWeights").get<std::array<double,5>>();
  double sum=0;for(double w:weights){if(!std::isfinite(w)||w<0)throw std::logic_error("Invalid population selection weight");sum+=w;}
  if(sum<=0)throw std::logic_error("Empty population selection weights");
  stellar::engine::DeterministicRandom rng(stream(digest(Json{{"version",version},{"seed",seed},{"morphology",id},{"purpose","population"}})));
  double roll=rng.unit_double()*sum;for(std::size_t i=0;i<weights.size();++i){if(roll<weights[i])return static_cast<PopulationState>(i);roll-=weights[i];}return PopulationState::Quiescent;
}
GalaxyVisualPair galaxy_visual_pair(GalaxyMorphology morphology,std::optional<PopulationState> state){
  const auto id=galaxy_morphology_id(morphology);std::string requested=state?std::string(population_selection_id(static_cast<PopulationSelection>(static_cast<int>(*state)+1))):"";
  // Resolve an entire pair together. Never mix population states or morphologies.
  std::map<std::string,std::array<const Json*,2>> pairs;
  for(const auto& a:visuals().at("assets"))if(a.at("morphology").get<std::string>()==id){
    const auto population=a.at("populationState").is_null()?std::string{}:a.at("populationState").get<std::string>();
    const auto variant=a.at("variant").get<std::string>();if(variant!="stars_included"&&variant!="gas_dust_only")throw std::logic_error("Invalid galaxy asset variant");
    auto& slot=pairs[population][variant=="stars_included"?0:1];if(slot)throw std::logic_error("Duplicate galaxy visual mapping");slot=&a;
  }
  std::vector<std::string> priority;if(state)priority.push_back(requested);priority.push_back("");priority.push_back("mature");for(const auto& [key,value]:pairs)priority.push_back(key);
  for(const auto& key:priority){auto found=pairs.find(key);if(found==pairs.end()||!found->second[0]||!found->second[1])continue;
    const auto& p=*found->second[0];const auto& m=*found->second[1];GalaxyVisualPair result{p.at("id"),m.at("id"),p.at("path"),m.at("path")};
    result.aspect=m.at("runtimeDimensions").at(0).get<double>()/m.at("runtimeDimensions").at(1).get<double>();result.fallback=state?key!=requested:!key.empty();
    if(result.fallback)result.diagnostic="Missing exact galaxy visual pair: morphology="+std::string(id)+" population="+requested+"; same-morphology fallback="+(key.empty()?"generic":key)+"; preview="+result.preview_id+"; map="+result.map_id;
    return result;
  }throw std::logic_error("No validated same-morphology galaxy visual pair");
}
GalaxyGenerationConfig resolve_galaxy_configuration(GalaxyGenerationConfig c){
  if((c.generator_version!=galaxy_generator_version&&c.generator_version!="galaxy-configuration-v2"&&c.generator_version!="galaxy-configuration-v3")||c.asset_set_version!=galaxy_visual_version)throw std::invalid_argument("Unsupported galaxy generation or artwork version");
  if(!supported_full_galaxy_system_count(c.system_count))throw std::invalid_argument("Unsupported galaxy size");
  if(c.pre_warp_count<1||c.ancient_count<0||static_cast<std::int64_t>(c.pre_warp_count)+c.ancient_count>c.system_count||c.player_species_id.empty())throw std::invalid_argument("Invalid galaxy civilization configuration");
  c.resolved_population=resolve_population_state(c.base_seed,c.morphology,c.requested_population,c.generator_version);
  const auto pair=galaxy_visual_pair(c.morphology,c.resolved_population);c.preview_asset_id=pair.preview_id;c.map_asset_id=pair.map_id;
  c.fingerprint=digest(Json{{"generator",c.generator_version},{"assetSet",c.asset_set_version},{"seed",c.base_seed},{"morphology",galaxy_morphology_id(c.morphology)},{"requestedPopulation",population_selection_id(c.requested_population)},{"resolvedPopulation",population_selection_id(static_cast<PopulationSelection>(static_cast<int>(c.resolved_population)+1))},{"systems",c.system_count},{"preWarp",c.pre_warp_count},{"ancients",c.ancient_count},{"species",c.player_species_id},{"developerCoverage",c.developer_full_coverage},{"profile",stellar_population_profile_version()},{"preview",c.preview_asset_id},{"map",c.map_asset_id}});return c;
}
void validate_galaxy_configuration(const GalaxyGenerationConfig& c){if(resolve_galaxy_configuration(c)!=c)throw std::invalid_argument("Galaxy configuration fingerprint or resolved values disagree");}
std::uint64_t galaxy_generation_stream(const GalaxyGenerationConfig& c){validate_galaxy_configuration(c);return stream(c.fingerprint);}
const stellar::engine::DensityMask& galaxy_density_mask(std::string_view id){
  static const auto masks=[] {std::map<std::string,stellar::engine::DensityMask> result;for(const auto& [key,j]:visuals().at("densityMasks").items())result.emplace(key,stellar::engine::DensityMask(j.at("width"),j.at("height"),j.at("values").get<std::vector<std::uint8_t>>()));return result;}();return masks.at(std::string(id));
}
GalaxyFootprintFrame galaxy_footprint_frame(GalaxyMorphology m,int count,std::optional<PopulationState> state){
  const auto pair=galaxy_visual_pair(m,state);const auto& fit=visuals().at("footprintFrames").at(pair.map_id);
  const double width=full_galaxy_radius(count)*fit.at("worldDiameterScale").get<double>(),height=width/pair.aspect;
  return {-fit.at("homeNeighborhoodUv").at(0).get<double>()*width,-fit.at("homeNeighborhoodUv").at(1).get<double>()*height,width,height};
}
bool galaxy_has_central_black_hole(const GalaxyGenerationConfig& c){validate_galaxy_configuration(c);const auto& rule=rules().at("morphologies").at(galaxy_morphology_id(c.morphology));if(!rule.at("smbhEligible").get<bool>()||c.system_count<rule.at("minimumGalaxySize").get<int>())return false;if(c.developer_full_coverage)return true;const double chance=std::clamp(rule.at("smbhOccupationChance").get<double>()*rule.at("sizeOccupationMultiplier").at(std::to_string(c.system_count)).get<double>(),0.,1.);stellar::engine::DeterministicRandom rng(galaxy_generation_stream(c)^0x534d42484f434350ULL);return rng.unit_double()<chance;}
std::string galaxy_configuration_description(const GalaxyGenerationConfig& c){return "Type: "+std::string(morphology_name(c.morphology))+"\nPopulation selection: "+std::string(population_selection_label(c.requested_population))+"\nResolved population: "+std::string(population_state_name(c.resolved_population))+"\nSeed: "+std::to_string(c.base_seed)+"\nSystems: "+std::to_string(c.system_count)+"\nRival empires: "+std::to_string(c.pre_warp_count-1)+"\nAncient empires: "+std::to_string(c.ancient_count)+"\nSpecies: "+c.player_species_id+"\nGenerator: "+c.generator_version+"\nFingerprint: "+c.fingerprint+"\nTo reproduce this galaxy, use the same seed and the same galaxy-generation settings.";}
}
