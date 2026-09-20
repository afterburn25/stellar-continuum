#include <stellar/core/small_body_fields.hpp>
#include <stellar/engine/foundation.hpp>
#include <algorithm>
#include <numeric>
#include <numbers>

namespace stellar::core {
namespace {
constexpr double tau=2*std::numbers::pi;
std::array<double,9> initial_resources(const SmallBodyField& f,std::uint32_t index){
  const auto b=small_body_instance(f,index);const double amount=b.scale*b.scale*120;
  std::array<double,9> result{};
  switch(b.material){
  case SmallBodyMaterial::Rock:result={.8,.18,0,0,0,0,0,0,.02};break;
  case SmallBodyMaterial::Metal:result={.18,.8,0,0,0,0,0,0,.02};break;
  case SmallBodyMaterial::Carbon:result={.2,.1,.45,.05,.05,0,.15,0,0};break;
  case SmallBodyMaterial::WaterIce:result={.04,0,0,.7,.2,.01,.05,0,0};break;
  case SmallBodyMaterial::MethaneIce:result={.02,0,.2,0,.28,0,.5,0,0};break;
  case SmallBodyMaterial::AmmoniaIce:result={.02,0,0,.05,.23,0,.7,0,0};break;
  case SmallBodyMaterial::RockIce:result={.35,.1,0,.35,.1,0,.1,0,0};break;
  case SmallBodyMaterial::FrozenVolatiles:result={.02,0,.08,.1,.2,.02,.58,0,0};break;
  default:throw std::invalid_argument("Invalid small-body material");
  }
  for(auto& v:result)v*=amount;
  if(f.type==SmallBodyFieldType::CrackedCluster||f.type==SmallBodyFieldType::Shattered||f.type==SmallBodyFieldType::PlanetaryHalo){result[7]=amount*.12;result[8]+=amount*.06;}
  return result;
}
}
SmallBodyInstance small_body_instance(const SmallBodyField& f,std::uint32_t index){
  if(index>=f.body_count||f.version!=1)throw std::out_of_range("Small-body identity or version is invalid");
  stellar::engine::DeterministicRandom r(f.seed^(std::uint64_t(index)+1)*0x9e3779b97f4a7c15ULL);
  SmallBodyInstance b;b.id=index;
  double roll=r.unit_double(),sum=0;std::size_t material=0;
  for(;material<7;++material){sum+=f.composition[material];if(roll<sum)break;}
  b.material=static_cast<SmallBodyMaterial>(material);b.asset_pool=material>=3?SmallBodyAssetPool::IcyBody:f.body_pool;
  b.asset_variant_id=f.asset_variants[r.next_u64()%4];b.local_cluster_id=static_cast<std::uint32_t>(r.next_u64()%11);
  const double u=r.unit_double();double radial=u;
  // Two narrow resonance gaps plus seeded radial clumps. No rejection loops.
  if(radial>.28&&radial<.34)radial-=.065;if(radial>.65&&radial<.70)radial+=.055;
  const double e=f.eccentricity*r.unit_double()*.5;
  double a=std::lerp(f.inner_radius_au,f.outer_radius_au,radial);
  // Bound eccentric excursions to the field whenever its width permits.
  const double max_e=std::min(.8,(f.outer_radius_au-f.inner_radius_au)/(f.outer_radius_au+f.inner_radius_au)*.9);
  b.orbit.eccentricity=std::min(e,max_e);
  a=std::clamp(a,f.inner_radius_au/(1-b.orbit.eccentricity),f.outer_radius_au/(1+b.orbit.eccentricity));
  b.orbit.radius=a;b.orbit.inclination=f.tilt+(r.unit_double()-.5)*std::min(.4,f.thickness_au/a);
  b.orbit.ascending_node=r.unit_double()*tau;b.orbit.periapsis=r.unit_double()*tau;
  double angle=(r.unit_double()-.5)*tau*f.arc_fraction;
  if(r.unit_double()<f.clustering){const double center=(double(b.local_cluster_id)+.5)/11.-.5;angle=(center+(r.unit_double()-.5)*.035)*tau*f.arc_fraction;}
  // Compensate node/periapsis so partial arcs share the authored orbital sector.
  b.orbit.phase=f.phase_origin+angle-b.orbit.ascending_node-b.orbit.periapsis;
  b.orbit.angular_speed=tau/365.25*std::sqrt(f.central_mass_solar/(a*a*a))*(r.unit_double()<f.retrograde_fraction?-1:1);
  b.scale=.5+std::pow(r.unit_double(),3)*3.5;b.material_brightness=.78+r.unit_double()*.35;
  const double z=r.unit_double()*2-1,azimuth=r.unit_double()*tau;
  b.spin.axis={std::sqrt(1-z*z)*std::cos(azimuth),std::sqrt(1-z*z)*std::sin(azimuth),z};
  b.spin.phase=r.unit_double()*tau;b.spin.rate=std::lerp(f.spin_min,f.spin_max,r.unit_double())*(r.unit_double()<.5?-1:1);
  if(r.unit_double()<f.irregular_fraction){b.spin.wobble=.4+r.unit_double()*2.6;b.spin.precession=std::abs(b.spin.rate)*(.11+r.unit_double()*.3);}
  return b;
}
std::vector<SmallBodyInstance> small_body_instances(const SmallBodyField& f,std::size_t limit){
  validate_small_body_field(f);limit=std::min({limit,std::size_t(f.visible_count),std::size_t(2048)});
  std::vector<SmallBodyInstance> result;result.reserve(limit);for(std::size_t i=0;i<limit;++i)result.push_back(small_body_instance(f,static_cast<std::uint32_t>(i)));return result;
}
SmallBodyEnvironment small_body_environment(const SmallBodyField& f,std::array<double,3> p,double days){
  validate_small_body_field(f);for(double v:{p[0],p[1],p[2],days})if(!std::isfinite(v))throw std::invalid_argument("Invalid field query");
  const double radius=std::hypot(p[0],p[1]),u=(radius-f.inner_radius_au)/(f.outer_radius_au-f.inner_radius_au);
  if(u<0||u>1||std::abs(p[2]-radius*std::sin(f.tilt))>f.thickness_au+radius*std::abs(std::sin(f.tilt)))return {};
  const double speed=tau/365.25*std::sqrt(f.central_mass_solar/std::pow((f.inner_radius_au+f.outer_radius_au)*.5,3));
  const double angle=std::remainder(std::atan2(p[1],p[0])-f.phase_origin-speed*(days-f.epoch_days),tau);
  if(std::abs(angle)>std::numbers::pi*f.arc_fraction)return {};
  const double gap=(u>.28&&u<.34)||(u>.65&&u<.70)?.08:1;
  const double density=f.density*gap*(1-f.clustering*.45+f.clustering*.45*std::cos(angle*11));
  const bool debris=static_cast<int>(f.type)>=5;
  return {density,1+density*2.5,std::clamp(density*2,0.,.8),std::clamp(density*(debris?2.5:1),0.,1.),1+density*3};
}
std::array<double,9> small_body_resources(const SmallBodyField& f,std::uint32_t index){
  auto result=initial_resources(f,index);const auto it=std::ranges::find(f.extraction,index,&SmallBodyExtraction::body_index);
  if(it!=f.extraction.end())for(std::size_t i=0;i<result.size();++i)result[i]=std::max(0.,result[i]-it->extracted[i]);return result;
}
double extract_small_body_resource(SmallBodyField& f,std::uint32_t index,SmallBodyResource resource,double requested){
  validate_small_body_field(f);if(!std::isfinite(requested)||requested<=0||static_cast<unsigned>(resource)>=9)throw std::invalid_argument("Invalid extraction request");
  const auto slot=static_cast<std::size_t>(resource);const double amount=std::min(requested,small_body_resources(f,index)[slot]);
  if(amount==0)return 0;auto it=std::ranges::find(f.extraction,index,&SmallBodyExtraction::body_index);
  if(it==f.extraction.end()){f.extraction.push_back({index,{}});it=std::prev(f.extraction.end());}it->extracted[slot]+=amount;return amount;
}
}
