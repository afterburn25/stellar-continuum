#include <stellar/core/stellar_object.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/core/stellar_coverage.hpp>
#include <stellar/core/galaxy_configuration.hpp>
#include <stellar/engine/spatial_region_index.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include "stellar_population_config.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
constexpr double tau=6.2831853071795864769;
using Json=nlohmann::json;
const Json& config() { static const auto j=Json::parse(stellar_population_config_json); return j; }
struct Random {
  std::uint64_t state;
  std::uint64_t next() { auto z=(state+=0x9e3779b97f4a7c15ULL); z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL; z=(z^(z>>27))*0x94d049bb133111ebULL; return z^(z>>31); }
  double unit() { return static_cast<double>(next()>>11)*0x1.0p-53; }
  double range(StellarRange r) { return r.minimum+(r.maximum-r.minimum)*unit(); }
  double logarithmic(StellarRange r) { return r.minimum>0 ? std::exp(std::log(r.minimum)+unit()*std::log(r.maximum/r.minimum)) : range(r); }
};
StellarRange range(const Json& j,const char* key) { const auto& v=j.at(key); return {v.at(0).get<double>(),v.at(1).get<double>()}; }
const char* state_key(PopulationState s) {
  static constexpr const char* names[]={"Starburst","Active","Mature","Aging","Quiescent"};
  if(static_cast<unsigned>(s)>=std::size(names)) throw std::invalid_argument("Unknown population state"); return names[static_cast<unsigned>(s)];
}
const char* morphology_key(GalaxyMorphology m) {
  static constexpr const char* names[]={"Spiral","BarredSpiral","Lenticular","Elliptical","Irregular","Ring"};
  if(static_cast<unsigned>(m)>=std::size(names)) throw std::invalid_argument("Unknown galaxy morphology"); return names[static_cast<unsigned>(m)];
}
double hazard(const char* key) { return config().at("hazards").at(key).get<double>(); }
StellarObjectType detailed(StellarClass c) {
  using T=StellarObjectType;
  switch(c) {
  case StellarClass::KOrangeDwarf:return T::KOrangeStar;case StellarClass::GYellowDwarf:return T::GYellowStar;
  case StellarClass::FYellowWhiteDwarf:return T::FYellowWhiteStar;case StellarClass::AWhiteStar:return T::AWhiteStar;
  case StellarClass::HotBlueStar:return T::BBlueWhiteStar;case StellarClass::Giant:return T::RedGiant;
  case StellarClass::WhiteDwarf:return T::WhiteDwarf;case StellarClass::NeutronStar:return T::QuietNeutronStar;
  case StellarClass::Pulsar:return T::Pulsar;case StellarClass::BlackHole:return T::QuiescentBlackHole;
  default:return T::MRedDwarf;
  }
}
StellarClass coarse(StellarObjectType t) {
  using T=StellarObjectType;
  const auto& d=stellar_object_definition(t);
  if(d.black_hole)return StellarClass::BlackHole;
  if(d.evolved)return StellarClass::Giant;
  switch(t) {
  case T::KOrangeStar:return StellarClass::KOrangeDwarf;case T::GYellowStar:return StellarClass::GYellowDwarf;
  case T::FYellowWhiteStar:return StellarClass::FYellowWhiteDwarf;case T::AWhiteStar:return StellarClass::AWhiteStar;
  case T::BBlueWhiteStar:case T::OHotBlueStar:case T::WolfRayet:return StellarClass::HotBlueStar;
  case T::WhiteDwarf:return StellarClass::WhiteDwarf;case T::QuietNeutronStar:case T::Magnetar:return StellarClass::NeutronStar;
  case T::Pulsar:return StellarClass::Pulsar;default:return StellarClass::MRedDwarf;
  }
}
void finite_nonnegative(double x) { if(!std::isfinite(x)||x<0) throw std::invalid_argument("Invalid stellar physical value"); }
}

const std::vector<StellarObjectDefinition>& stellar_object_definitions() {
  static const auto definitions=[] {
    std::vector<StellarObjectDefinition> v;
    for(const auto& j:config().at("objects")) {
      StellarObjectDefinition d; d.type=static_cast<StellarObjectType>(v.size());
      d.id=j.at("id");d.name=j.at("name");d.weight_millionths=j.at("weightMillionths");
      d.mass_solar=range(j,"massSolar");d.radius_solar=range(j,"radiusSolar");d.luminosity_solar=range(j,"luminositySolar");
      d.temperature_kelvin=range(j,"temperatureKelvin");d.age_myr=range(j,"ageMyr");d.lifetime_myr=range(j,"lifetimeMyr");
      d.radiation_modifier=j.at("radiationModifier");d.wind_modifier=j.at("windModifier");d.habitability_modifier=j.at("habitabilityModifier");
      d.visual_scale=j.at("visualScale");d.distance_luminosity=j.at("distanceLuminosity");d.color=j.at("color").get<std::array<int,3>>();
      d.young=j.at("young");d.evolved=j.at("evolved");d.remnant=j.at("remnant");d.black_hole=j.at("blackHole");d.directional_jets=j.at("directionalJets");
      d.evolution_class=j.at("evolutionClass");d.fallback_distance_mode=j.at("fallbackDistanceMode");
      auto& h=d.hooks;h.rarity_tier=j.at("rarityTier");h.is_rare_discovery=j.at("isRareDiscovery");h.discovery_event_id=j.at("discoveryEventId");h.discovery_text_key=j.at("discoveryTextKey");h.sensor_signature=j.at("sensorSignature");h.hazard_profile=j.at("hazardProfile");
      h.special_feature_ids=j.at("specialFeatureIds").get<std::vector<std::string>>();h.research_opportunity_ids=j.at("researchOpportunityIds").get<std::vector<std::string>>();h.resource_opportunity_ids=j.at("resourceOpportunityIds").get<std::vector<std::string>>();h.unique_interaction_ids=j.at("uniqueInteractionIds").get<std::vector<std::string>>();
      if(d.weight_millionths<0)throw std::logic_error("Negative stellar population weight"); v.push_back(std::move(d));
    }
    if(v.size()!=stellar_object_type_count||std::accumulate(v.begin(),v.end(),0,[](int n,const auto& d){return n+d.weight_millionths;})!=1000000)throw std::logic_error("Stellar population must total exactly 100 percent");
    return v;
  }();return definitions;
}
const StellarObjectDefinition& stellar_object_definition(StellarObjectType t) { return stellar_object_definitions().at(static_cast<std::size_t>(t)); }
std::string_view population_state_name(PopulationState s) { return state_key(s); }
std::string_view morphology_name(GalaxyMorphology m) { return morphology_key(m); }
std::array<double,stellar_object_type_count> stellar_population_weights(StellarPopulationOptions options,std::optional<StellarRegion> region) {
  return region ? stellar_region_profile_weights(options,*region) : stellar_profile_weights(options);
}
StellarObjectType sample_stellar_object(std::uint64_t seed,StellarPopulationOptions options,std::optional<StellarRegion> region) {
  return sample_stellar_profile(seed,stellar_population_weights(options,region));
}
std::pair<double,double> stellar_habitable_zone(double luminosity) { finite_nonnegative(luminosity);const auto s=std::sqrt(luminosity);return {.95*s,1.67*s}; }
StellarPhysicalProperties generate_stellar_physics(std::uint64_t seed,StellarObjectType type) {
  const auto& d=stellar_object_definition(type);Random rng{seed^0x5048595349435301ULL};StellarPhysicalProperties p;
  p.type=type;p.mass_solar=rng.logarithmic(d.mass_solar);p.age_myr=rng.logarithmic(d.age_myr);p.lifetime_myr=rng.logarithmic(d.lifetime_myr);
  if(!d.evolved&&!d.remnant) {
    // A main-sequence star cannot already be older than its generated lifetime.
    p.age_myr=std::min(p.age_myr,p.lifetime_myr*.95);
  }
  p.radiation_modifier=d.radiation_modifier*(.8+.4*rng.unit());p.wind_modifier=d.wind_modifier*(.8+.4*rng.unit());p.habitability_modifier=d.habitability_modifier;p.hooks=d.hooks;
  p.active=type!=StellarObjectType::MRedDwarf||rng.unit()<.25;
  if(type==StellarObjectType::MRedDwarf&&!p.active)p.radiation_modifier*=.4;
  if(d.black_hole) {p.radius_solar=2.95325*p.mass_solar/695700.;p.luminosity_solar=0;p.effective_temperature_kelvin=0;}
  else {
    // Rejection preserves R/T/L consistency (Stefan-Boltzmann), rather than drawing contradictory triples.
    bool valid=false;
    for(int attempt=0;attempt<10000&&!valid;++attempt) {
      p.radius_solar=rng.logarithmic(d.radius_solar);p.effective_temperature_kelvin=rng.range(d.temperature_kelvin);
      p.luminosity_solar=p.radius_solar*p.radius_solar*std::pow(p.effective_temperature_kelvin/5772.,4);
      valid=p.luminosity_solar>=d.luminosity_solar.minimum&&p.luminosity_solar<=d.luminosity_solar.maximum;
    }
    if(p.luminosity_solar<d.luminosity_solar.minimum||p.luminosity_solar>d.luminosity_solar.maximum)throw std::logic_error("Inconsistent physical ranges for "+d.id);
  }
  const auto hz=stellar_habitable_zone(p.luminosity_solar);p.inner_hz_au=hz.first;p.outer_hz_au=hz.second;
  p.destruction_radius_au=p.radius_solar*solar_radius_au;
  p.safe_approach_au=std::max({p.destruction_radius_au*hazard("photosphereClearance"),std::sqrt(p.luminosity_solar/hazard("safeFlux")),std::sqrt(p.radiation_modifier)*hazard("radiationApproachAu"),std::sqrt(p.wind_modifier)*hazard("windApproachAu")});
  p.jet_axis_radians=rng.unit()*tau;p.jet_half_angle_radians=d.directional_jets?hazard("jetHalfAngleRadians"):0;
  validate_stellar_physics(p);return p;
}
StellarPlanetProperties stellar_planet_exposure(const StellarPhysicalProperties& p,double orbit) {
  if(!std::isfinite(orbit)||orbit<=0)throw std::invalid_argument("Invalid stellar orbital distance");
  const double flux=p.luminosity_solar/(orbit*orbit);
  return {orbit,flux,p.safe_approach_au,flux>=hazard("bakedFlux")||orbit<=p.safe_approach_au,p.luminosity_solar>0&&orbit>=p.inner_hz_au&&orbit<=p.outer_hz_au};
}
double stellar_hazard_extent_au(const StellarPhysicalProperties& p) {
  return std::max(p.safe_approach_au,p.jet_half_angle_radians>0?hazard("jetExtentAu")*std::max(1.,std::sqrt(p.radiation_modifier)):0.);
}
double stellar_navigation_au_per_unit(const StellarPhysicalProperties& p) {
  return std::max({50.,p.outer_hz_au*1.8,stellar_hazard_extent_au(p)*2.});
}
bool stellar_approach_unsafe(const StellarPhysicalProperties& p,double x,double y) {
  if(!std::isfinite(x)||!std::isfinite(y))return true;
  const double r=std::hypot(x,y);if(r<=p.safe_approach_au)return true;
  if(p.jet_half_angle_radians>0&&r<hazard("jetExtentAu")*std::max(1.,std::sqrt(p.radiation_modifier))) {
    const double alignment=std::abs(std::cos(std::atan2(y,x)-p.jet_axis_radians));
    if(alignment>=std::cos(p.jet_half_angle_radians))return true;
  }return false;
}
void validate_stellar_physics(const StellarPhysicalProperties& p) {
  if(p.generation_version!="stellar-population-v1")throw std::invalid_argument("Unsupported stellar generation version");
  (void)stellar_object_definition(p.type);
  for(double x:{p.mass_solar,p.radius_solar,p.luminosity_solar,p.effective_temperature_kelvin,p.age_myr,p.lifetime_myr,p.radiation_modifier,p.wind_modifier,p.habitability_modifier,p.inner_hz_au,p.outer_hz_au,p.safe_approach_au,p.destruction_radius_au,p.jet_axis_radians,p.jet_half_angle_radians})finite_nonnegative(x);
  if(p.mass_solar<=0||p.radius_solar<=0||p.inner_hz_au>p.outer_hz_au||p.safe_approach_au<p.destruction_radius_au||p.jet_axis_radians>tau||p.jet_half_angle_radians>tau/4)throw std::invalid_argument("Invalid stellar physical relationships");
}
void validate_stellar_planet(const StellarPlanetProperties& p) { for(double x:{p.orbit_au,p.incident_flux,p.safe_approach_au})finite_nonnegative(x);if(p.orbit_au<=0)throw std::invalid_argument("Invalid planetary orbit"); }
CentralBlackHoleProperties generate_central_black_hole(std::uint64_t seed) {
  Random rng{seed^0x43454e5452454248ULL};const auto& j=config().at("centralBlackHole");const auto weights=j.at("stateWeights").get<std::array<double,3>>();
  double u=rng.unit()*std::accumulate(weights.begin(),weights.end(),0.);int state=0;while(state<2&&u>=weights[static_cast<std::size_t>(state)])u-=weights[static_cast<std::size_t>(state++)];
  return {"stellar-population-v1",static_cast<CentralBlackHoleState>(state),rng.logarithmic(range(j,"massSolar")),rng.unit()*tau,state==2?hazard("jetHalfAngleRadians"):0};
}
CentralBlackHoleProperties central_black_hole_with_state(CentralBlackHoleProperties value,CentralBlackHoleState state){
  value.state=state;value.jet_half_angle_radians=state==CentralBlackHoleState::RelativisticJets?hazard("jetHalfAngleRadians"):0;
  validate_central_black_hole(value);return value;
}
void validate_central_black_hole(const CentralBlackHoleProperties& p) {
  if(p.generation_version!="stellar-population-v1"||static_cast<unsigned>(p.state)>2||!std::isfinite(p.mass_solar)||p.mass_solar<100000)throw std::invalid_argument("Invalid central black hole");
  finite_nonnegative(p.jet_axis_radians);finite_nonnegative(p.jet_half_angle_radians);
}

void apply_stellar_population(std::int64_t seed,std::vector<StellarSystem>& systems,StellarPopulationOptions options,bool visual_footprint) {
  (void)stellar_population_weights(options);
  auto core=full_galaxy_core(static_cast<int>(systems.size()));const auto radius=full_galaxy_radius(static_cast<int>(systems.size()));
  std::vector<Vec2> placed;double protected_radius=0;
  for(const auto& s:systems)if(s.stellar_catalog_id) {placed.push_back({s.position.x,s.position.y});protected_radius=std::max(protected_radius,std::hypot(s.position.x,s.position.y)+3.5);}
  stellar::engine::SpatialRegionIndex spacing(3.5);
  for(std::size_t i=0;i<placed.size();++i)spacing.insert(i,{placed[i].x,placed[i].y,placed[i].x,placed[i].y});
  const auto frame=visual_footprint?galaxy_footprint_frame(options.morphology,static_cast<int>(systems.size()),options.state):GalaxyFootprintFrame{};
  if(visual_footprint)core.position={static_cast<float>(frame.left+frame.width*.5),static_cast<float>(frame.top+frame.height*.5)};
  const auto* mask=visual_footprint?&galaxy_density_mask(galaxy_visual_pair(options.morphology,options.state).map_id):nullptr;
  StellarRegionCounts region_counts{};
  const double formation=stellar_star_forming_density_multiplier(options.state);
  // Position streams never depend on the sampled class. Generate the geometry
  // and classify its regions first; then condition the common population on it.
  for(auto& s:systems) {
    const auto key=static_cast<std::uint64_t>(seed)^(static_cast<std::uint64_t>(s.id+1)*0xd6e8feb86659fd93ULL);
    if(s.stellar_catalog_id){s.stellar_region=StellarRegion::Disk;continue;}
    Random rng{key^0x5350415449414c01ULL};bool found=false;
    for(int attempt=0;attempt<30000&&!found;++attempt) {
      const double u=rng.unit(),angle=rng.unit()*tau;
      double r=radius*std::sqrt(u),a=angle,flatten=.78;StellarRegion region=StellarRegion::Disk;
      if(options.morphology==GalaxyMorphology::Elliptical) {
        r=radius*std::pow(u,.75);flatten=.86;
        region=r<radius*.5?StellarRegion::Bulge:r>radius*.85?StellarRegion::Halo:StellarRegion::Interior;
      } else if(options.morphology==GalaxyMorphology::Ring) {
        const bool active_ring=rng.unit()<std::clamp(.60*formation,.22,.82);
        r=radius*(active_ring?.66+.15*u:.15+.83*std::sqrt(u));
        region=r>radius*.63&&r<radius*.84?StellarRegion::Ring:r>radius*.9?StellarRegion::Halo:StellarRegion::Interior;
      }
      else if(options.morphology==GalaxyMorphology::Irregular) {
        flatten=1;
        if(rng.unit()<std::clamp(.46*formation,.15,.80)) {
          const int clump=static_cast<int>(rng.unit()*5);const double ca=tau*clump/5+.35*std::sin(clump*2.7),cr=radius*(.38+.09*(clump%3));
          const double x=cr*std::cos(ca)+radius*.19*std::sqrt(u)*std::cos(a),y=cr*std::sin(ca)+radius*.19*std::sqrt(u)*std::sin(a);
          r=std::hypot(x,y);a=std::atan2(y,x);
        }
        region=r>radius*.87?StellarRegion::Halo:StellarRegion::Disk;
        for(int clump=0;clump<5;++clump) {
          const double ca=tau*clump/5+.35*std::sin(clump*2.7),cr=radius*(.38+.09*(clump%3));
          if(std::hypot(r*std::cos(a)-cr*std::cos(ca),r*std::sin(a)-cr*std::sin(ca))<radius*.20){region=StellarRegion::IrregularClump;break;}
        }
      } else if(options.morphology==GalaxyMorphology::Lenticular) {
        region=r<radius*.42?StellarRegion::Bulge:r>radius*.9?StellarRegion::Halo:r>radius*.65?StellarRegion::OuterDisk:StellarRegion::InnerDisk;
      } else {
        if(r>radius*.28&&r<radius*.9&&rng.unit()<.60) {
          // Sequence RNG draws explicitly; expression evaluation order must not
          // change the saved seed's geometry when the placement loop is optimized.
          const double arm=rng.unit(),jitter=rng.unit();
          a=2.6*std::log(std::max(.15,r/radius))+std::floor(arm*4)*tau/4+(jitter-.5)*.36;
        }
        const double phase=std::remainder(a-2.6*std::log(std::max(.15,r/radius)),tau/4);
        region=r<radius*.28?StellarRegion::Bulge:r>radius*.92?StellarRegion::Halo:r>radius*.83?StellarRegion::OuterDisk:std::abs(phase)<.22?StellarRegion::Arm:r<radius*.4?StellarRegion::InnerDisk:StellarRegion::InterArm;
        if(region==StellarRegion::Arm&&rng.unit()<std::clamp(.22*formation,.02,.65))region=StellarRegion::StarForming;
        if(options.morphology==GalaxyMorphology::BarredSpiral&&r<radius*.36&&std::abs(std::sin(a))<.3)region=StellarRegion::Bar;
      }
      Vec2 p{core.position.x+static_cast<float>(r*std::cos(a)),core.position.y+static_cast<float>(r*std::sin(a)*flatten)};
      if(mask){
        const double density=mask->sample((p.x-frame.left)/frame.width,(p.y-frame.top)/frame.height);
        // The procedural morphology proposes locations. The cached visual field
        // rejects empty margins and gently weights valid structure, not classes.
        if(density<.055||rng.unit()>std::clamp(.35+density*1.7,.35,1.))continue;
      }
      if(std::hypot(p.x-core.position.x,p.y-core.position.y)<core.exclusion_radius||std::hypot(p.x,p.y)<protected_radius)continue;
      const auto nearby=spacing.query({p.x-3.5,p.y-3.5,p.x+3.5,p.y+3.5});
      if(std::any_of(nearby.begin(),nearby.end(),[&](std::size_t id){const auto other=placed[id];return std::hypot(other.x-p.x,other.y-p.y)<3.5;}))continue;
      s.position={p.x,p.y,std::nullopt};s.stellar_region=region;++region_counts[static_cast<std::size_t>(region)];
      spacing.insert(placed.size(),{p.x,p.y,p.x,p.y});placed.push_back(p);found=true;
    }
    if(!found)throw std::runtime_error("Stellar placement exhausted safe spacing; generation aborted without a partial galaxy");
  }
  const auto profile=calibrate_stellar_population_profile(static_cast<std::uint64_t>(seed),options,region_counts);
  for(auto& s:systems) {
    const auto key=static_cast<std::uint64_t>(seed)^(static_cast<std::uint64_t>(s.id+1)*0xd6e8feb86659fd93ULL);
    // Measured anchors remain separate from the procedural statistical profile.
    const auto type=s.stellar_catalog_id?detailed(s.primary.value_or(StellarClass::MRedDwarf)):
        sample_stellar_profile(key,profile.region_weights[static_cast<std::size_t>(*s.stellar_region)]);
    s.stellar_object=generate_stellar_physics(key,type);s.stellar_object->measured_anchor=s.stellar_catalog_id.has_value();
    if(s.id==0&&s.catalog_preset_id=="sol-v1") {
      auto& p=*s.stellar_object;p.mass_solar=p.radius_solar=p.luminosity_solar=1;p.effective_temperature_kelvin=5772;p.age_myr=4600;p.lifetime_myr=10000;p.radiation_modifier=p.wind_modifier=p.habitability_modifier=1;p.inner_hz_au=.95;p.outer_hz_au=1.67;p.destruction_radius_au=solar_radius_au;p.safe_approach_au=std::sqrt(1./hazard("safeFlux"));
    }
    const auto& d=stellar_object_definition(type);s.primary=coarse(type);
    if(!s.stellar_catalog_id) {
      if(d.black_hole)s.archetype=StarArchetype::BlackHole;
      else if(type==StellarObjectType::QuietNeutronStar||type==StellarObjectType::Pulsar||type==StellarObjectType::Magnetar)s.archetype=StarArchetype::NeutronPulsar;
      else if(s.archetype==StarArchetype::BlackHole||s.archetype==StarArchetype::NeutronPulsar)s.archetype=StarArchetype::Standard;
    }
    if(d.habitability_modifier<.2||d.evolved||d.remnant||type==StellarObjectType::BrownDwarf)s.has_habitable_world=false;
  }
}
StellarCoverageResult ensure_stellar_coverage(std::uint64_t seed,std::vector<StellarSystem>& systems) {
  auto staged=systems;StellarCoverageResult result;
  std::array<int,stellar_object_type_count> counts{};
  std::unordered_set<int> ids;
  for(const auto &s:staged){
    if(!s.stellar_object||!ids.insert(s.id).second)throw std::invalid_argument("Coverage requires unique, physically typed systems.");
    validate_stellar_physics(*s.stellar_object);++counts.at(static_cast<std::size_t>(s.stellar_object->type));
  }
  for(const auto &definition:stellar_object_definitions()){
    if(counts[static_cast<std::size_t>(definition.type)])continue;
    StellarSystem *best=nullptr;std::pair<int,std::uint64_t> best_rank{100,0};
    for(auto &candidate:staged){
      const auto &p=*candidate.stellar_object;const auto &current=stellar_object_definition(p.type);
      if(candidate.stellar_catalog_id||p.measured_anchor||current.hooks.is_rare_discovery||
         counts[static_cast<std::size_t>(p.type)]<=1||
         std::ranges::find(result.forced_system_ids,candidate.id)!=result.forced_system_ids.end())continue;
      const auto region=candidate.stellar_region.value_or(StellarRegion::Disk);
      const bool forming=region==StellarRegion::Arm||region==StellarRegion::StarForming||
          region==StellarRegion::IrregularClump||region==StellarRegion::Ring;
      const bool massive=definition.young||definition.type==StellarObjectType::RedSupergiant||
          definition.type==StellarObjectType::YellowSupergiant||definition.type==StellarObjectType::BlueSupergiant||
          definition.type==StellarObjectType::Hypergiant||definition.type==StellarObjectType::WolfRayet;
      Random stable{seed^0x636f766572616765ULL^(static_cast<std::uint64_t>(candidate.id+1)*0xd6e8feb86659fd93ULL)^static_cast<std::uint64_t>(definition.type)};
      const std::pair rank{massive&&!forming?1:0,stable.next()};
      if(!best||rank<best_rank||(rank==best_rank&&candidate.id<best->id)){best=&candidate;best_rank=rank;}
    }
    if(!best)throw std::invalid_argument("Insufficient safe procedural systems for complete stellar coverage.");
    --counts[static_cast<std::size_t>(best->stellar_object->type)];
    const auto key=seed^0x636f766572616765ULL^(static_cast<std::uint64_t>(best->id+1)*0xd6e8feb86659fd93ULL);
    best->stellar_object=generate_stellar_physics(key,definition.type);best->primary=coarse(definition.type);
    best->secondary.reset();best->tertiary.reset();
    best->archetype=definition.black_hole?StarArchetype::BlackHole:
      (definition.type==StellarObjectType::QuietNeutronStar||definition.type==StellarObjectType::Pulsar||definition.type==StellarObjectType::Magnetar)?StarArchetype::NeutronPulsar:StarArchetype::Standard;
    if(definition.habitability_modifier<.2||definition.evolved||definition.remnant||definition.type==StellarObjectType::BrownDwarf)best->has_habitable_world=false;
    ++counts[static_cast<std::size_t>(definition.type)];result.forced_system_ids.push_back(best->id);
  }
  std::sort(result.forced_system_ids.begin(),result.forced_system_ids.end());
  systems=std::move(staged);return result;
}
std::map<int,int> apply_stellar_planetary_physics(std::span<const StellarSystem> systems,std::vector<PlanetaryBody>& bodies) {
  std::unordered_map<int,const StellarPhysicalProperties*> physics;
  std::unordered_set<int> previously_conditioned;for(const auto& b:bodies)if(b.stellar_exposure)previously_conditioned.insert(b.id);
  std::unordered_map<int,int> counts,candidate_orbits;std::unordered_map<int,double> orbits;std::unordered_set<int> engulfed;
  for(const auto& s:systems)if(s.stellar_object)physics.emplace(s.id,&*s.stellar_object);
  for(const auto& b:bodies)if(!b.parent_body_id) {counts[b.system_id]=std::max(counts[b.system_id],b.orbit_index+1);if(b.legacy_colonization_candidate)candidate_orbits[b.system_id]=b.orbit_index;}
  constexpr double sol_orbits[]={.387098,.723332,1,1.523679,5.2044,9.5826,19.2184,30.1104};
  for(auto& b:bodies) {
    auto f=physics.find(b.system_id);if(f==physics.end()||b.parent_body_id)continue;
    const auto& p=*f->second;const auto& d=stellar_object_definition(p.type);
    double orbit=.12*std::pow(2.05,b.orbit_index);
    if(b.system_id==0) { if(b.id>=1&&b.id<=8)orbit=sol_orbits[b.id-1];else if(b.id==pluto_body_id)orbit=39.482; }
    else if(d.evolved) {
      const double step=std::pow(std::max(45.,p.outer_hz_au*1.1)/.12,1./std::max(1,counts[b.system_id]-1));orbit=.12*std::pow(step,b.orbit_index);
    } else if(!d.remnant&&d.habitability_modifier>=.2&&candidate_orbits.contains(b.system_id)) {
      orbit=std::sqrt(p.luminosity_solar)*std::pow(2.05,b.orbit_index-candidate_orbits[b.system_id]);
    }
    if(b.stellar_exposure)orbit=b.stellar_exposure->orbit_au;
    b.stellar_exposure=stellar_planet_exposure(p,orbit);orbits.emplace(b.id,orbit);
    if(orbit*(1-b.orbital_eccentricity)<=p.destruction_radius_au)engulfed.insert(b.id);
  }
  for(auto& b:bodies) {
    auto f=physics.find(b.system_id);if(f==physics.end())continue;const auto& p=*f->second;
    if(b.parent_body_id) {auto orbit=orbits.find(*b.parent_body_id);if(orbit==orbits.end()||engulfed.contains(*b.parent_body_id)){engulfed.insert(b.id);continue;}b.stellar_exposure=stellar_planet_exposure(p,orbit->second);}
    if(!b.stellar_exposure)continue;
    auto& exposure=*b.stellar_exposure;
    // Periapsis must also be safe: an eccentric orbit cannot hide its heating behind the semi-major axis.
    if(!b.parent_body_id&&b.orbital_eccentricity>0)exposure.baked=exposure.baked||stellar_planet_exposure(p,exposure.orbit_au*(1-b.orbital_eccentricity)).baked;
    if(b.system_id!=0&&!previously_conditioned.contains(b.id)) {
      const double equilibrium=278.5*std::pow(exposure.incident_flux,.25);
      if(exposure.baked||!b.legacy_colonization_candidate||p.habitability_modifier<.2)b.environment.temperature_kelvin=std::max(3.,equilibrium);
      b.environment.radiation_hazard=std::clamp(std::max(b.environment.radiation_hazard,p.radiation_modifier/(100*(1+exposure.orbit_au*exposure.orbit_au))),0.,1.);
    }
    if(exposure.baked||p.habitability_modifier<.2||!exposure.in_habitable_zone)b.legacy_colonization_candidate=false;
    if(exposure.baked) {if(!b.appearance){b.environment.temperature_kelvin=std::max(1200.,b.environment.temperature_kelvin);b.environment.available_solvent=PlanetarySolventRegime::None;}b.environment.radiation_hazard=1;}
  }
  std::map<int,int> removed;
  for(const auto& b:bodies)if(engulfed.contains(b.id))++removed[b.system_id];
  std::erase_if(bodies,[&](const auto& b){return engulfed.contains(b.id);});
  return removed;
}
std::string stellar_population_diagnostics(std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies,StellarPopulationOptions options,std::optional<CentralBlackHoleProperties> core,std::uint64_t seed) {
  std::array<int,stellar_object_type_count> count{};int generated=0,anchors=0,rare=0,baked=0,engulfed=0;std::unordered_set<int> hz;
  for(const auto& s:systems) {engulfed+=s.engulfed_planets;if(!s.stellar_object)continue;if(s.stellar_object->measured_anchor){++anchors;continue;}++generated;++count[static_cast<std::size_t>(s.stellar_object->type)];rare+=s.stellar_object->hooks.is_rare_discovery?1:0;}
  for(const auto& b:bodies)if(b.stellar_exposure){baked+=b.stellar_exposure->baked?1:0;if(b.stellar_exposure->in_habitable_zone)hz.insert(b.system_id);}
  std::ostringstream out;out<<"Developer-only population report\nSystems: "<<systems.size()<<"; procedural: "<<generated<<"; measured anchors: "<<anchors<<"\nType | Count | Actual % | Target %\n";
  StellarRegionCounts regions{};StellarRegionalObjectCounts regional_counts{};
  for(const auto& s:systems)if(s.stellar_object&&!s.stellar_object->measured_anchor&&s.stellar_region){const auto r=static_cast<std::size_t>(*s.stellar_region);++regions[r];++regional_counts[r][static_cast<std::size_t>(s.stellar_object->type)];}
  const auto calibrated=calibrate_stellar_population_profile(seed,options,regions);
  const auto& weights=calibrated.target_weights;out<<std::fixed<<std::setprecision(4);
  for(const auto& d:stellar_object_definitions()){const auto i=static_cast<std::size_t>(d.type);out<<d.name<<" | "<<count[i]<<" | "<<(generated?100.*count[i]/generated:0)<<" | "<<100*weights[i]<<'\n';}
  out<<"Rare: "<<rare<<"; central SMBH: "<<(core?1:0)<<"; state: "<<(core?static_cast<int>(core->state):-1)<<"; baked: "<<baked<<"; engulfed: "<<engulfed<<"; HZ systems: "<<hz.size()<<'\n';
  out<<stellar_profile_diagnostics(calibrated,regional_counts);return out.str();
}
} // namespace stellar::core
