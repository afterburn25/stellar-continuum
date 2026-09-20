#include <stellar/core/small_body_fields.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/engine/foundation.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
using Random=stellar::engine::DeterministicRandom;
constexpr double tau=2*std::numbers::pi;
template<std::size_t N> std::size_t choose(Random& r,const std::array<double,N>& weights){
  double roll=r.unit_double()*std::accumulate(weights.begin(),weights.end(),0.);
  for(std::size_t i=0;i<N;++i){if(roll<weights[i])return i;roll-=weights[i];}return N-1;
}
bool disturbed(const StellarSystem& s){return s.archetype==StarArchetype::Dangerous||s.archetype==StarArchetype::AncientRuin||s.engulfed_planets>0;}
double stable_phase(int id){auto v=(static_cast<std::uint32_t>(id)+1u)*2654435761u;v^=v>>16u;return double(v&0xffffffu)/16777216.*tau;}
void restore_sol_belt_detail(const StellarSystem& s,SmallBodyField& f){
  if(s.catalog_preset_id==sol_catalog_preset_id&&!f.planet_centered&&(f.type==SmallBodyFieldType::Mixed||f.type==SmallBodyFieldType::Ice))
    f.visible_count=std::min<std::uint32_t>(2048,f.body_count);
}
}
double planetary_orbit_au(const StellarSystem& s,const PlanetaryBody& b){
  if(s.catalog_preset_id==sol_catalog_preset_id){constexpr std::array radii{.387,.723,1.,1.524,5.203,9.537,19.191,30.069,39.482};return radii.at(std::min<std::size_t>(b.orbit_index,8));}
  if(b.stellar_exposure)return b.stellar_exposure->orbit_au;
  return .4*std::pow(1.85,b.orbit_index);
}
void validate_small_body_field(const SmallBodyField& f){
  auto positive=[](double v){return std::isfinite(v)&&v>0;};
  auto unit=[](double v){return std::isfinite(v)&&v>=0&&v<=1;};
  if(f.version!=1||f.id<0||static_cast<unsigned>(f.type)>=9||!positive(f.inner_radius_au)||!positive(f.outer_radius_au)||f.inner_radius_au>=f.outer_radius_au||!positive(f.thickness_au)||!unit(f.density)||f.body_count==0||f.body_count>1000000||f.visible_count==0||f.visible_count>2048||f.visible_count>f.body_count)
    throw std::invalid_argument("Invalid small-body field region/version/count");
  double sum=0;for(double w:f.composition){if(!unit(w))throw std::invalid_argument("Invalid small-body composition");sum+=w;}
  if(std::abs(sum-1)>1e-6||!unit(f.clustering)||!unit(f.eccentricity)||f.eccentricity>=.8||!positive(f.arc_fraction)||f.arc_fraction>1||!positive(f.central_mass_solar)||!unit(f.irregular_fraction)||!positive(f.spin_min)||!positive(f.spin_max)||f.spin_max<f.spin_min||!unit(f.retrograde_fraction)||!std::isfinite(f.tilt)||!std::isfinite(f.phase_origin)||!std::isfinite(f.epoch_days)||f.epoch_days<0||f.rarity_profile.empty()||f.rarity_profile.size()>64||f.associated_planet_id< -1||f.associated_belt_parent_id< -1||f.associated_belt_parent_id==f.id||(f.planet_centered&&f.associated_planet_id<0))
    throw std::invalid_argument("Invalid small-body field profile");
  for(auto p:{f.body_pool,f.dust_pool,f.far_pool})if(static_cast<unsigned>(p)>=9)throw std::invalid_argument("Invalid small-body asset pool");
  for(int v:f.asset_variants)if(v<1||v>4)throw std::invalid_argument("Invalid named asset variant");
  std::unordered_set<std::uint32_t> ids;
  for(const auto& e:f.extraction){if(e.body_index>=f.body_count||!ids.insert(e.body_index).second)throw std::invalid_argument("Invalid small-body depletion identity");for(double v:e.extracted)if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid small-body depletion");}
}
void place_small_body_field(const StellarSystem& s,std::span<const PlanetaryBody> bodies,SmallBodyField& f){
  if(f.planet_centered)return;
  std::vector<std::pair<double,double>> blocked;
  const int host=s.stellar_orbits?s.stellar_orbits->belt_host:0;
  const auto physics=stellar_host_physics(s,host);
  double boundary=s.stellar_object?physics.destruction_radius_au*1.3:.01;
  if(host==3)boundary=std::max(boundary,1.5*circumbinary_limit(s.stellar_orbits->relative_orbits[0].radius,s.stellar_orbits->relative_orbits[0].eccentricity,s.stellar_orbits->companions[0].mass_solar/physics.mass_solar));
  if(f.type==SmallBodyFieldType::Ice&&s.stellar_object)
    boundary=std::max(boundary,2.7*std::sqrt(std::max(0.,physics.luminosity_solar)));
  bool conflict=f.inner_radius_au<boundary;
  for(const auto& b:bodies)if(b.system_id==s.id&&!b.parent_body_id){
    if(planetary_stellar_host(s,b.id)!=host)continue;
    if(b.cracked_world&&f.associated_planet_id==b.id)continue;
    const double a=planetary_orbit_au(s,b),ecc=std::clamp(b.orbital_eccentricity,0.,.9);
    const double mass=s.stellar_object?std::max(.001,physics.mass_solar):1.;
    const double margin=a*std::max(.045,3*std::cbrt(b.mass_earth*3.00349e-6/(3*mass)));
    blocked.emplace_back(std::max(boundary,a*(1-ecc)-margin),a*(1+ecc)+margin);
    conflict|=f.outer_radius_au>blocked.back().first&&f.inner_radius_au<blocked.back().second;
  }
  if(!conflict)return;
  std::ranges::sort(blocked);
  const double width=f.outer_radius_au-f.inner_radius_au,preferred=(f.inner_radius_au+f.outer_radius_au)*.5;
  double best=1e100,chosen_inner=0,chosen_outer=0;
  const auto candidate=[&](double low,double high){
    low=std::max(low,boundary);if(high<=low)return;
    const double available=(high-low)*.86,w=std::min(width,available);
    if(w<width*.18)return;
    const double center=std::clamp(preferred,low+(high-low)*.07+w*.5,high-(high-low)*.07-w*.5);
    const double score=std::abs(std::log(center/preferred))+.22*(1-w/width);
    if(score<best){best=score;chosen_inner=center-w*.5;chosen_outer=center+w*.5;}
  };
  double low=boundary;
  for(const auto& [a,b]:blocked){candidate(low,a);low=std::max(low,b);}
  candidate(low,std::max(low+width*2,f.outer_radius_au*1.7));
  const double thickness_ratio=f.thickness_au>0?f.thickness_au/width:.04;
  f.inner_radius_au=chosen_inner;f.outer_radius_au=chosen_outer;
  f.thickness_au=(chosen_outer-chosen_inner)*thickness_ratio;
}
void reconcile_small_body_orbits(std::vector<StellarSystem>& systems,std::span<const PlanetaryBody> bodies){
  std::unordered_map<int,std::vector<PlanetaryBody>> grouped;
  for(const auto& b:bodies)grouped[b.system_id].push_back(b);
  for(auto& s:systems)if(s.small_body_fields)for(auto& f:*s.small_body_fields){place_small_body_field(s,grouped[s.id],f);restore_sol_belt_detail(s,f);}
}
void validate_small_body_catalog(std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies){
  std::unordered_map<int,int> parents;parents.reserve(bodies.size());for(const auto& b:bodies)parents.emplace(b.id,b.system_id);
  for(const auto& s:systems)if(s.small_body_fields){
    if(s.small_body_fields->size()>32)throw std::invalid_argument("Too many small-body fields in system");
    std::unordered_set<int> ids;for(const auto& f:*s.small_body_fields){validate_small_body_field(f);if(!ids.insert(f.id).second)throw std::invalid_argument("Duplicate small-body field ID");}
    for(const auto& f:*s.small_body_fields){if(f.associated_planet_id>=0&&(!parents.contains(f.associated_planet_id)||parents.at(f.associated_planet_id)!=s.id))throw std::invalid_argument("Small-body field parent is outside system");
      if(f.associated_belt_parent_id>=0&&!ids.contains(f.associated_belt_parent_id))throw std::invalid_argument("Missing small-body parent field");
      int parent=f.associated_belt_parent_id;std::size_t steps=0;while(parent>=0){if(++steps>s.small_body_fields->size())throw std::invalid_argument("Cyclic small-body parent fields");const auto it=std::ranges::find(*s.small_body_fields,parent,&SmallBodyField::id);if(it==s.small_body_fields->end())throw std::invalid_argument("Missing small-body parent field");parent=it->associated_belt_parent_id;}
    }
  }
}
SmallBodyField make_small_body_field(const StellarSystem& s,std::span<const PlanetaryBody> bodies,SmallBodyFieldType type,int id,std::uint64_t seed,int parent,const SmallBodyConfiguration& config){
  const auto& rule=config.types.at(static_cast<std::size_t>(type));Random r(seed);SmallBodyField f;
  f.id=id;f.type=type;f.seed=seed;f.density=std::lerp(rule.density_min,rule.density_max,r.unit_double());
  f.body_count=rule.body_count;f.visible_count=rule.visible_count;f.composition=rule.composition;
  f.tilt=(r.unit_double()-.5)*.12;f.eccentricity=rule.eccentricity;f.clustering=rule.clustering;f.arc_fraction=rule.arc_fraction;f.phase_origin=r.unit_double()*tau;
  f.irregular_fraction=config.irregular_fraction;f.spin_min=config.spin_min;f.spin_max=config.spin_max;
  f.body_pool=rule.body_pool;f.dust_pool=rule.dust_pool;f.far_pool=rule.far_pool;
  f.central_mass_solar=s.stellar_object?s.stellar_object->mass_solar:1.;
  const double age=s.stellar_object?s.stellar_object->age_myr:4600.;f.rarity_profile=age<500?"young":(disturbed(s)?"disturbed":(age>8000?"old":"mature"));
  double outer=.9,giant=0,inner=.4;const PlanetaryBody* associated=nullptr;
  for(const auto& b:bodies)if(b.system_id==s.id&&b.kind!=PlanetaryBodyKind::Moon){const double a=planetary_orbit_au(s,b);outer=std::max(outer,a);if(!b.environment.has_solid_surface&&(giant==0||a<giant))giant=a;if(b.id==parent)associated=&b;}
  if(giant>0)for(const auto& b:bodies)if(b.system_id==s.id&&b.kind!=PlanetaryBodyKind::Moon&&b.environment.has_solid_surface){const double a=planetary_orbit_au(s,b);if(a<giant)inner=std::max(inner,a);}
  f.inner_radius_au=giant>inner*1.35?std::lerp(inner,giant,.22):outer*.43;
  f.outer_radius_au=giant>inner*1.35?std::lerp(inner,giant,.63):outer*.65;
  if(type==SmallBodyFieldType::Ice){f.inner_radius_au=outer*1.1;f.outer_radius_au=outer*1.6;}
  if(type==SmallBodyFieldType::DebrisDisk){f.inner_radius_au=outer*.25;f.outer_radius_au=outer*1.25;}
  if(type==SmallBodyFieldType::Shattered){f.inner_radius_au=outer*.7;f.outer_radius_au=outer*1.05;}
  if(s.catalog_preset_id==sol_catalog_preset_id){if(type==SmallBodyFieldType::Mixed){f.inner_radius_au=2.1;f.outer_radius_au=3.3;}if(type==SmallBodyFieldType::Ice){f.inner_radius_au=32;f.outer_radius_au=50;}}
  if(type==SmallBodyFieldType::CrackedCluster||type==SmallBodyFieldType::PlanetaryHalo){
    if(!associated||!associated->cracked_world)throw std::invalid_argument("Cracked debris requires a cracked parent world");
    f.associated_planet_id=associated->id;f.phase_origin=stable_phase(associated->id);
    const double a=planetary_orbit_au(s,*associated);f.inner_radius_au=a*.93;f.outer_radius_au=a*1.08;
    if(type==SmallBodyFieldType::PlanetaryHalo){f.planet_centered=true;const double radius=associated->radius_earth*4.26352e-5;f.inner_radius_au=radius*3;f.outer_radius_au=radius*10;f.central_mass_solar=associated->mass_earth*3.00349e-6;}
  }else if(associated){f.associated_planet_id=associated->id;const double a=planetary_orbit_au(s,*associated);f.inner_radius_au=a*.8;f.outer_radius_au=a*1.2;}
  const double destruction=s.stellar_object&&!f.planet_centered?s.stellar_object->destruction_radius_au:0;
  if(f.inner_radius_au<=destruction){const double shift=destruction*1.3-f.inner_radius_au;f.inner_radius_au+=shift;f.outer_radius_au+=shift;}
  f.thickness_au=(f.outer_radius_au-f.inner_radius_au)*rule.thickness;
  place_small_body_field(s,bodies,f);
  restore_sol_belt_detail(s,f);
  validate_small_body_field(f);return f;
}
std::vector<SmallBodyField> generate_small_body_fields(std::int64_t seed,const StellarSystem& s,std::span<const PlanetaryBody> bodies,const SmallBodyConfiguration& c){
  Random r(static_cast<std::uint64_t>(seed)^((std::uint64_t(s.id)+1)*0x9e3779b97f4a7c15ULL)^0x4649454c445631ULL);
  std::vector<SmallBodyField> result;const auto add=[&](SmallBodyFieldType t,int parent=-1){result.push_back(make_small_body_field(s,bodies,t,static_cast<int>(result.size()),r.next_u64(),parent,c));};
  if(s.catalog_preset_id==sol_catalog_preset_id){add(SmallBodyFieldType::Mixed);add(SmallBodyFieldType::Ice);return result;}
  const bool giants=std::ranges::any_of(bodies,[&](const auto& b){return b.system_id==s.id&&!b.environment.has_solid_surface;});
  auto weights=c.belt_weights;if(giants)for(std::size_t i=1;i<5;++i)weights[i]*=c.giant_belt_multiplier;
  const auto category=choose(r,weights);auto rock_weights=c.rocky_weights;if(s.archetype==StarArchetype::ResourceRich)rock_weights[1]*=1.8;
  auto rocky=[&]{add(static_cast<SmallBodyFieldType>(choose(r,rock_weights)));};
  if(category==1||category==3||category==4)rocky();if(category>=2)add(SmallBodyFieldType::Ice);
  if(category==4){rocky();auto& f=result.back();const double shift=f.outer_radius_au*1.15;f.inner_radius_au+=shift;f.outer_radius_au+=shift;}
  const double age=s.stellar_object?s.stellar_object->age_myr:4600.;
  const bool young=age<500||s.primary==StellarClass::Protostar;
  if(r.unit_double()<c.disk_chance*(young?c.young_disk_multiplier:1))add(SmallBodyFieldType::DebrisDisk);
  if(r.unit_double()<c.shattered_chance*(disturbed(s)?c.disturbed_multiplier:1)*(age>8000?c.old_shattered_multiplier:1))add(SmallBodyFieldType::Shattered);
  for(const auto& b:bodies)if(b.system_id==s.id&&b.cracked_world){
    if(result.size()>27)break;
    if(r.unit_double()<std::min(1.,c.local_cluster_chance*c.cracked_multiplier))add(SmallBodyFieldType::CrackedCluster,b.id);
    if(r.unit_double()<c.cracked_shattered_chance)add(SmallBodyFieldType::Shattered,b.id);
    if(r.unit_double()<c.halo_chance)add(SmallBodyFieldType::PlanetaryHalo,b.id);
  }
  for(auto& f:result)place_small_body_field(s,bodies,f);
  return result;
}
void initialize_small_body_fields(std::int64_t seed,std::vector<StellarSystem>& systems,std::vector<PlanetaryBody>& bodies,bool generate_cracked){
  std::unordered_map<int,std::vector<std::size_t>> grouped;grouped.reserve(systems.size());for(std::size_t i=0;i<bodies.size();++i)grouped[bodies[i].system_id].push_back(i);
  for(auto& s:systems)if(!s.small_body_fields){std::vector<PlanetaryBody> local;const auto& c=small_body_configuration();
    Random r(static_cast<std::uint64_t>(seed)^std::uint64_t(s.id)*0xd1b54a32d192ed03ULL);
    for(auto i:grouped[s.id]){auto& b=bodies[i];if(generate_cracked&&!b.appearance&&s.catalog_preset_id!=sol_catalog_preset_id&&b.kind==PlanetaryBodyKind::Planet&&b.environment.has_solid_surface&&!b.legacy_colonization_candidate&&!b.has_pre_warp_civilization&&r.unit_double()<c.cracked_chance*(disturbed(s)?c.disturbed_multiplier:1))b.cracked_world=true;local.push_back(b);}
    s.small_body_fields=generate_small_body_fields(seed,s,local);
  }
}
}
