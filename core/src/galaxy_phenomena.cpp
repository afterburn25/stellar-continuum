#include <stellar/core/galaxy_phenomena.hpp>
#include <stellar/engine/foundation.hpp>
#include <nlohmann/json.hpp>
#include "galaxy_phenomena_data.hpp"
#include "phenomenon_art_data.hpp"
#include <stellar/core/phenomenon_art.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace stellar::core {
namespace {
const nlohmann::json& tuning(){static const auto j=nlohmann::json::parse(galaxy_phenomena_data);return j;}
const nlohmann::json& distribution(){static const auto j=nlohmann::json::parse(phenomenon_distribution_data);return j;}
double blend_modifier(double current,double value,double strength){return current*std::lerp(1.,value,strength);}
PhenomenonEffects read_effects(const nlohmann::json& j){
  return {j.at("sensor"),j.at("scanning"),j.at("movement"),j.at("hazard"),j.at("colonization"),j.at("research"),
    j.at("anomaly_bias"),j.at("resource_bias"),j.at("concealment"),j.at("combat_visibility"),j.at("attrition"),j.at("interference")};
}
struct Footprint {
  GalaxyFootprintFrame frame;const stellar::engine::DensityMask& mask;GalacticCore core;
  explicit Footprint(const GalaxyGenerationConfig& c):frame(galaxy_footprint_frame(c.morphology,c.system_count,c.resolved_population)),mask(galaxy_density_mask(c.map_asset_id)),core(full_galaxy_core(c.system_count)){core.position={static_cast<float>(frame.left+frame.width*.5),static_cast<float>(frame.top+frame.height*.5)};}
  double density(double x,double y)const{return mask.sample((x-frame.left)/frame.width,(y-frame.top)/frame.height);}
};
bool footprint_fits(const Footprint& footprint,const stellar::engine::OrganicRegion& s){
  // A rotated ellipse contains the entire organic silhouette. Validate its
  // interior as well as its perimeter, so dust-lane gaps cannot hide inside it.
  const auto cs=std::cos(s.rotation),sn=std::sin(s.rotation);
  const auto& core=footprint.core;
  for(int y=-6;y<=6;++y)for(int x=-6;x<=6;++x){
    const double u=x/6.,v=y/6.;if(u*u+v*v>1.01)continue;
    const auto px=s.x+cs*u*s.extent_x-sn*v*s.extent_y,py=s.y+sn*u*s.extent_x+cs*v*s.extent_y;
    if(footprint.density(px,py)<.055||std::hypot(px-core.position.x,py-core.position.y)<core.exclusion_radius)return false;
  }
  return true;
}
}
const std::array<PhenomenonDefinition,phenomenon_type_count>& phenomenon_definitions(){
  static const auto result=[] {
    std::array<PhenomenonDefinition,phenomenon_type_count> out{};const auto& all=tuning().at("types");
    if(all.size()!=10)throw std::logic_error("Phenomenon definition count changed without a schema revision");
    for(std::size_t i=0;i<out.size();++i){const auto& j=i<10?all.at(i):distribution().at("extraDefinitions").at(i-10);auto& d=out[i];
      d.id=j.at("id");d.name=j.at("name");d.description=j.at("description");d.rarity=j.at("rarity");
      d.shape=static_cast<stellar::engine::OrganicShape>(j.at("shape").get<int>());d.color=j.at("color").get<std::array<int,3>>();
      d.extent=j.at("extent").get<std::array<double,2>>();d.intensity=j.at("intensity").get<std::array<double,2>>();
      d.weight=j.at("weight");d.opacity=j.at("opacity");d.elongation=j.at("elongation");d.star_forming=j.at("star_forming");d.effects=read_effects(j.at("effects"));
    }return out;
  }();return result;
}
const PhenomenonDefinition& phenomenon_definition(PhenomenonType type){return phenomenon_definitions().at(static_cast<std::size_t>(type));}
double phenomenon_footprint_density(const GalaxyGenerationConfig& c,double x,double y){
  const auto f=galaxy_footprint_frame(c.morphology,c.system_count,c.resolved_population);
  return galaxy_density_mask(c.map_asset_id).sample((x-f.left)/f.width,(y-f.top)/f.height);
}
std::array<PhenomenonWeight,phenomenon_type_count> phenomenon_weights(GalaxyMorphology morphology,PopulationState population){
  std::array<PhenomenonWeight,phenomenon_type_count> out{};double total=0;
  for(std::size_t i=0;i<out.size();++i){const auto& j=distribution().at("types").at(i);auto& w=out[i];w.base=j.at("weight");w.population=j.at("population").at(static_cast<int>(population));w.morphology=j.at("morphology").at(static_cast<int>(morphology));w.normalized=w.base*w.population*w.morphology;total+=w.normalized;}
  for(auto& w:out)w.normalized/=total;return out;
}
int natural_phenomenon_count(int system_count,GalaxyMorphology morphology,PopulationState population,std::uint64_t seed){
  if(system_count<=0)return 0;const auto& data=distribution();const auto& bands=data.at("countBands");
  std::size_t band=0;while(band+1<bands.size()&&system_count>bands.at(band).at("upTo").get<int>())++band;
  const auto& b=bands.at(band);const double low=b.at("minimum"),high=b.at("maximum");
  const double start=band?bands.at(band-1).at("upTo").get<double>():0.;const double end=std::min(b.at("upTo").get<double>(),200000.);
  const double mean=std::lerp(low+(high-low)*.35,low+(high-low)*.65,std::clamp((system_count-start)/(end-start),0.,1.));
  // Triangular variation is concentrated near the configured band's middle.
  stellar::engine::DeterministicRandom rng(seed^0x434f554e545632ULL);
  const double target=mean*data.at("populationCount").at(static_cast<int>(population)).get<double>()*data.at("morphologyCount").at(static_cast<int>(morphology)).get<double>()*(.8+.2*(rng.unit_double()+rng.unit_double()));
  return std::clamp(static_cast<int>(std::floor(target+rng.unit_double())),0,data.at("maximumCount").get<int>());
}
GalaxyPhenomena generate_galaxy_phenomena(const GalaxyGenerationConfig& c,std::span<const StellarSystem> systems){
  validate_galaxy_configuration(c);
  const bool legacy=c.generator_version=="galaxy-configuration-v3";
  GalaxyPhenomena result;if(legacy)result.version="galaxy-phenomena-v1";result.configuration_fingerprint=c.fingerprint;
  if(systems.empty())return result;
  const Footprint footprint(c);
  stellar::engine::DeterministicRandom rng(galaxy_generation_stream(c)^0x4e4542554c414501ULL);
  const auto& data=tuning();const auto m=static_cast<int>(c.morphology),p=static_cast<int>(c.resolved_population);
  const double target=data.at("baseCount500").get<double>()*std::pow(c.system_count/500.,.72)*data.at("morphologyCount").at(m).get<double>()*data.at("populationCount").at(p).get<double>();
  int count=legacy?std::clamp(static_cast<int>(std::floor(target+rng.unit_double())),0,data.at("maximumCount").get<int>()):natural_phenomenon_count(c.system_count,c.morphology,c.resolved_population,galaxy_generation_stream(c));
  if(!legacy&&c.developer_full_coverage)count=std::max(count,9);
  const auto natural_weights=phenomenon_weights(c.morphology,c.resolved_population);
  std::array<std::vector<double>,phenomenon_type_count> anchors_by_type;std::array<double,phenomenon_type_count> anchor_totals{};
  for(std::size_t type=0;type<(legacy?10:phenomenon_type_count);++type)for(const auto& system:systems){
    double weight=(legacy?data:distribution()).at("regionWeights").at(static_cast<int>(system.stellar_region.value_or(StellarRegion::Disk))).get<double>();
    if(system.stellar_object){const auto& star=stellar_object_definition(system.stellar_object->type);
      if(phenomenon_definitions()[type].star_forming&&star.young)weight*=3.;
      if((type==5||type==6)&&star.remnant)weight*=4.;
    }
    if(!legacy){const auto region=system.stellar_region.value_or(StellarRegion::Disk);
      if(type==10||type==11)weight=std::sqrt(weight); // diffuse gas reaches disk/interarm too
      if(type==2&&c.morphology==GalaxyMorphology::Lenticular&&region==StellarRegion::Disk)weight*=4;
      if(phenomenon_definitions()[type].star_forming&&c.morphology==GalaxyMorphology::Ring&&region==StellarRegion::Ring)weight*=5;
      if(phenomenon_definitions()[type].star_forming&&c.morphology==GalaxyMorphology::BarredSpiral&&region==StellarRegion::Bar)weight*=3;
    }
    anchor_totals[type]+=weight;anchors_by_type[type].push_back(anchor_totals[type]);
  }
  std::array<double,phenomenon_type_count> weights{};double total_weight=0;
  for(std::size_t i=0;i<(legacy?10:weights.size());++i){const auto& d=phenomenon_definitions()[i];double w=d.weight;
    if(d.star_forming)w*=data.at("formationMultiplier").at(p).get<double>()*(c.morphology==GalaxyMorphology::Elliptical?.04:c.morphology==GalaxyMorphology::Lenticular?.3:1.);
    if(i==5||i==6)w*=p==0?1.8:p==1?1.3:1.;
    if(!legacy)w=natural_weights[i].normalized;total_weight+=w;weights[i]=total_weight;
  }
  for(int index=0;index<count;++index){
    const auto type=(!legacy&&c.developer_full_coverage&&index<9)?std::array{PhenomenonType::Emission,PhenomenonType::Reflection,PhenomenonType::DarkNebula,PhenomenonType::MolecularCloud,PhenomenonType::HII,PhenomenonType::SupernovaRemnant,PhenomenonType::DiffuseGas,PhenomenonType::MixedNebula,PhenomenonType::RareEnergetic}[index]:[&]{const auto roll=rng.unit_double()*total_weight;return static_cast<PhenomenonType>((legacy?std::lower_bound(weights.begin(),weights.begin()+10,roll):std::upper_bound(weights.begin(),weights.end(),roll))-weights.begin());}();
    auto d=phenomenon_definition(type);if(!legacy){d.extent=distribution().at("types").at(static_cast<int>(type)).at("extent").get<std::array<double,2>>();d.elongation=2944./1648.;}
    GalaxyPhenomenon region;region.id=static_cast<std::uint32_t>(index+1);region.type=type;
    const auto& anchors=anchors_by_type[static_cast<std::size_t>(type)];const auto total_anchors=anchor_totals[static_cast<std::size_t>(type)];
    region.designation="SC-N "+std::to_string(index+1);region.color=d.color;region.opacity=d.opacity;
    region.intensity=std::lerp(d.intensity[0],d.intensity[1],rng.unit_double());
    if(c.morphology==GalaxyMorphology::Elliptical)region.intensity*=.5;
    region.effects=d.effects;bool accepted=false;
    if(!legacy){const auto& nw=natural_weights[static_cast<std::size_t>(type)];region.natural_weight=nw.base;region.population_modifier=nw.population;region.morphology_modifier=nw.morphology;region.intensity*=std::array{1.1,1.05,1.,.8,.6}.at(p);region.intensity=std::min(1.,region.intensity);}
    region.hooks={d.rarity,"phenomenon:"+d.id,"phenomenon.description."+d.id,d.id,"phenomenon:"+d.id,d.rarity=="rare"||d.rarity=="exceptional",{d.id},{"phenomenon-survey:"+d.id},{}, {}};
    if(d.effects.resource_bias>0)region.hooks.resource_opportunity_ids.push_back("phenomenon-resource:"+d.id);
    if(type==PhenomenonType::Exotic)region.hooks.unique_interaction_ids.push_back("exotic-phenomenon-investigation");
    for(int attempt=0;attempt<160&&!accepted;++attempt){
      const auto ai=static_cast<std::size_t>(std::lower_bound(anchors.begin(),anchors.end(),rng.unit_double()*total_anchors)-anchors.begin());
      const auto& anchor=systems[std::min(ai,systems.size()-1)];region.affinity=anchor.stellar_region.value_or(StellarRegion::Disk);
      auto& shape=region.shape;const auto radius=std::lerp(d.extent[0],d.extent[1],rng.unit_double())*(attempt>100?.45:attempt>55?.7:1.)*(legacy?1.:std::clamp(std::pow(c.system_count/500.,.32),.7,5.));
      shape={anchor.position.x+(rng.unit_double()-.5)*radius,anchor.position.y+(rng.unit_double()-.5)*radius,radius,radius/d.elongation,rng.unit_double()*6.283185307179586,.18+rng.unit_double()*.15,rng.next_u64(),d.shape};
      if(region.affinity==StellarRegion::Arm||region.affinity==StellarRegion::Ring||region.affinity==StellarRegion::Bar){const auto center=footprint.core.position;shape.rotation=std::atan2(shape.y-center.y,shape.x-center.x)+1.5707963267948966;}
      if(c.morphology==GalaxyMorphology::Lenticular)shape.rotation=.12+(rng.unit_double()-.5)*.25;
      accepted=footprint_fits(footprint,shape);
    }
    if(!accepted)continue;
    for(const auto& system:systems)if(stellar::engine::sample_region(region.shape,system.position.x,system.position.y).density>.005)region.systems_contained.push_back(system.id);
    result.regions.push_back(std::move(region));
  }
  if(!legacy)assign_phenomenon_art(result,galaxy_generation_stream(c));
  return result;
}
SystemPhenomenonContext phenomenon_context(const GalaxyPhenomena* regions,double x,double y,int system_id){
  SystemPhenomenonContext out;out.world_x=x;out.world_y=y;if(!regions)return out;
  std::uint64_t local_seed=static_cast<std::uint64_t>(system_id)*0x9e3779b97f4a7c15ULL ^ std::bit_cast<std::uint64_t>(x) ^ std::rotl(std::bit_cast<std::uint64_t>(y),27);
  for(const auto& r:regions->regions){const auto s=stellar::engine::sample_region(r.shape,x,y);if(s.density<=.005)continue;
    const auto strength=s.density*r.intensity;out.overlaps.push_back({r.id,s.overlap,s.density,strength,s.edge_distance,s.center_distance});
    local_seed^=r.shape.seed+static_cast<std::uint64_t>(r.id)*0xd6e8feb86659fd93ULL;
    out.visual_intensity+=strength*r.opacity;const auto& e=r.effects;auto& a=out.effects;
    a.sensor=blend_modifier(a.sensor,e.sensor,strength);a.scanning=blend_modifier(a.scanning,e.scanning,strength);
    a.movement=blend_modifier(a.movement,e.movement,strength);a.combat_visibility=blend_modifier(a.combat_visibility,e.combat_visibility,strength);
    a.hazard+=e.hazard*strength;a.colonization+=e.colonization*strength;a.research+=e.research*strength;
    a.anomaly_bias+=e.anomaly_bias*strength;a.resource_bias+=e.resource_bias*strength;a.concealment+=e.concealment*strength;
    a.attrition+=e.attrition*strength;a.interference+=e.interference*strength;
  }
  std::stable_sort(out.overlaps.begin(),out.overlaps.end(),[](const auto& a,const auto& b){return a.intensity>b.intensity;});
  if(!out.overlaps.empty())out.dominant=out.overlaps.front().id;
  out.local_seed=local_seed;out.visual_intensity=std::min(.42,out.visual_intensity);
  out.effects.sensor=std::clamp(out.effects.sensor,.45,1.);out.effects.scanning=std::clamp(out.effects.scanning,1.,1.8);
  out.effects.movement=std::clamp(out.effects.movement,.7,1.2);out.effects.combat_visibility=std::clamp(out.effects.combat_visibility,.5,1.);
  out.effects.hazard=std::min(out.effects.hazard,1.);out.effects.concealment=std::min(out.effects.concealment,.7);out.effects.interference=std::min(out.effects.interference,1.);
  out.effects.anomaly_bias=std::min(out.effects.anomaly_bias,.4);out.effects.resource_bias=std::min(out.effects.resource_bias,.25);
  return out;
}
std::optional<PhenomenonOverlap> nearest_phenomenon(const GalaxyPhenomena* field,double x,double y){
  std::optional<PhenomenonOverlap> nearest;if(!field||!std::isfinite(x)||!std::isfinite(y))return nearest;
  for(const auto& r:field->regions){const auto s=stellar::engine::sample_region(r.shape,x,y,false);if(!nearest||std::abs(s.edge_distance)<std::abs(nearest->edge_distance))nearest=PhenomenonOverlap{r.id,s.overlap,s.density,s.density*r.intensity,s.edge_distance,s.center_distance};}
  return nearest;
}
void validate_galaxy_phenomena(const GalaxyPhenomena& field,const GalaxyGenerationConfig& config,std::span<const StellarSystem> systems){
  if((field.version!=galaxy_phenomena_version&&field.version!="galaxy-phenomena-v1")||field.configuration_fingerprint!=config.fingerprint||field.regions.size()>160)throw std::invalid_argument("Invalid saved phenomenon field identity");
  std::set<std::uint32_t> ids;
  const Footprint footprint(config);
  for(const auto& r:field.regions){
    stellar::engine::validate_organic_region(r.shape);
    if(!footprint_fits(footprint,r.shape))throw std::invalid_argument("Saved phenomenon is outside the galaxy footprint");
    if(!r.id||!ids.insert(r.id).second||r.designation.empty()||r.designation.size()>80||static_cast<unsigned>(r.type)>=phenomenon_type_count||static_cast<unsigned>(r.affinity)>=12||
       !std::isfinite(r.intensity)||r.intensity<0||r.intensity>1||!std::isfinite(r.opacity)||r.opacity<0||r.opacity>.65||
       std::ranges::any_of(r.color,[](int c){return c<0||c>255;}))throw std::invalid_argument("Invalid saved phenomenon region");
    (void)phenomenon_art(field,r);
    if(field.version==galaxy_phenomena_version&&r.asset_id.empty())throw std::invalid_argument("New phenomenon has no saved artwork identity");
    for(double v:{r.natural_weight,r.population_modifier,r.morphology_modifier})if(!std::isfinite(v)||v<0||v>100)throw std::invalid_argument("Invalid phenomenon generation diagnostics");
    const auto& e=r.effects;
    const std::array<double,12> values{e.sensor,e.scanning,e.movement,e.hazard,e.colonization,e.research,e.anomaly_bias,e.resource_bias,e.concealment,e.combat_visibility,e.attrition,e.interference};
    if(std::ranges::any_of(values,[](double value){return !std::isfinite(value)||value<0||value>4;})||e.scanning<1||e.sensor<=0||e.sensor>1)throw std::invalid_argument("Invalid saved phenomenon modifiers");
    std::vector<int> contained;for(const auto& s:systems)if(stellar::engine::sample_region(r.shape,s.position.x,s.position.y).density>.005)contained.push_back(s.id);
    if(contained!=r.systems_contained)throw std::invalid_argument("Saved phenomenon membership disagrees with its geometry");
  }
}
std::string phenomena_diagnostics(const GalaxyPhenomena* field,const SystemPhenomenonContext* context){
  if(!field)return "This legacy galaxy has no generated phenomenon field.";
  std::ostringstream out;out<<field->version<<" | "<<field->regions.size()<<" regions\nConfiguration "<<field->configuration_fingerprint<<'\n';
  if(context){out<<"Dominant: "<<(context->dominant?std::to_string(*context->dominant):"none")<<" | local seed "<<context->local_seed<<"\nSensor "<<context->effects.sensor<<"x | scan effort "<<context->effects.scanning<<"x | hazard "<<context->effects.hazard<<"\nVisual ceiling "<<context->visual_intensity<<" | concealment "<<context->effects.concealment<<" | research interest "<<context->effects.research<<"\nAnomaly bias "<<context->effects.anomaly_bias<<" | resource bias "<<context->effects.resource_bias<<" | interference "<<context->effects.interference<<"\n";
    for(const auto& s:context->overlaps)out<<"ID "<<s.id<<" overlap "<<s.overlap<<" density "<<s.density<<" opacity strength "<<s.intensity<<" edge "<<s.edge_distance<<" ly\n";
  }else for(const auto& r:field->regions){out<<r.designation<<" "<<phenomenon_definition(r.type).name<<" | "<<r.systems_contained.size()<<" systems | shape seed "<<r.shape.seed<<'\n';out<<"  System IDs: ";for(int id:r.systems_contained)out<<id<<' ';out<<'\n';}
  return out.str();
}
} // namespace stellar::core
