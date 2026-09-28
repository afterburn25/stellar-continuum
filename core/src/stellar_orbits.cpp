#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <set>
namespace stellar::core {
namespace {
constexpr double tau=2*std::numbers::pi;
std::uint64_t mix(std::uint64_t n){n+=0x9e3779b97f4a7c15ULL;n=(n^(n>>30))*0xbf58476d1ce4e5b9ULL;n=(n^(n>>27))*0x94d049bb133111ebULL;return n^(n>>31);}
StellarObjectType type(StellarClass c){switch(c){
case StellarClass::MRedDwarf:return StellarObjectType::MRedDwarf;
case StellarClass::KOrangeDwarf:return StellarObjectType::KOrangeStar;
case StellarClass::FYellowWhiteDwarf:return StellarObjectType::FYellowWhiteStar;
case StellarClass::AWhiteStar:return StellarObjectType::AWhiteStar;
case StellarClass::HotBlueStar:return StellarObjectType::BBlueWhiteStar;
case StellarClass::Giant:return StellarObjectType::RedGiant;
case StellarClass::WhiteDwarf:return StellarObjectType::WhiteDwarf;
case StellarClass::NeutronStar:return StellarObjectType::QuietNeutronStar;
case StellarClass::BlackHole:return StellarObjectType::QuiescentBlackHole;
case StellarClass::Pulsar:return StellarObjectType::Pulsar;
default:return StellarObjectType::GYellowStar;}}
StellarPosition scaled(StellarPosition p,double s){for(auto& x:p)x*=s;return p;}
StellarPosition plus(StellarPosition a,const StellarPosition& b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
}
double kepler_rate(double a,double mass){if(!std::isfinite(a)||a<=0||!std::isfinite(mass)||mass<=0)throw std::invalid_argument("Invalid Kepler mass/radius");return tau/365.25*std::sqrt(mass/(a*a*a));}
// Holman & Wiegert (1999), with additional conservative margins applied at
// placement. Outside the calibrated mass-ratio range use the less permissive
// edge for S-type orbits, and the maximum P-type coefficient for tiny companions.
double circumstellar_limit(double a,double e,double mu){const double original=std::clamp(mu,1e-9,1-1e-9);mu=std::clamp(mu,.1,.9);const double fit=std::max(.02,.464-.380*mu-.631*e+.586*mu*e+.150*e*e-.198*mu*e*e);const double hill=.3*(1-e)*std::cbrt((1-original)/(3*original));return a*std::min(fit,hill);}
double circumbinary_limit(double a,double e,double mu){mu=std::clamp(std::min(mu,1-mu),.1,.5);return a*(1.60+5.10*e-2.22*e*e+4.12*mu-4.27*e*mu-5.09*mu*mu+4.61*e*e*mu*mu);}
int planetary_stellar_host(const StellarSystem& s,int id){if(s.stellar_orbits)for(const auto& p:s.stellar_orbits->planets)if(p.body_id==id)return p.host;return 0;}
StellarPhysicalProperties stellar_host_physics(const StellarSystem& s,int host){
 auto p=s.stellar_object.value_or(generate_stellar_physics(static_cast<unsigned>(s.id),type(s.primary.value_or(StellarClass::GYellowDwarf))));
 if(!s.stellar_orbits||host==0)return p;
 const auto& c=s.stellar_orbits->companions;
 if(host==1||host==2){if(static_cast<std::size_t>(host)>c.size())throw std::invalid_argument("Missing stellar host");return c[host-1];}
 if(host!=3||c.empty())throw std::invalid_argument("Invalid stellar host");
 p.mass_solar+=c[0].mass_solar;p.luminosity_solar+=c[0].luminosity_solar;
 auto hz=stellar_habitable_zone(p.luminosity_solar);p.inner_hz_au=hz.first;p.outer_hz_au=hz.second;
 p.safe_approach_au=std::max(p.safe_approach_au,c[0].safe_approach_au);return p;
}
std::array<StellarPosition,4> stellar_positions(const StellarSystem& s,double days){
 std::array<StellarPosition,4> p{};if(!s.stellar_orbits)return p;
 const auto& a=*s.stellar_orbits;const double ma=stellar_host_physics(s,0).mass_solar,mb=a.companions[0].mass_solar,mab=ma+mb;
 const auto relative=stellar::engine::analytic_orbit_position(a.relative_orbits[0],days);
 p[0]=scaled(relative,-mb/mab);p[1]=scaled(relative,ma/mab);
 if(a.companions.size()==2){const double mc=a.companions[1].mass_solar;const auto outer=stellar::engine::analytic_orbit_position(a.relative_orbits[1],days);
  p[3]=scaled(outer,-mc/(mab+mc));p[0]=plus(p[0],p[3]);p[1]=plus(p[1],p[3]);p[2]=scaled(outer,mab/(mab+mc));}
 return p;
}
std::string stellar_host_name(int host){return host==3?"Stars A+B":std::string("Star ")+char('A'+std::clamp(host,0,2));}
stellar::engine::AnalyticOrbit planetary_stellar_orbit(const StellarSystem& s,const PlanetaryBody& b){
 const int host=planetary_stellar_host(s,b.id);const double a=planetary_orbit_au(s,b);
 stellar::engine::AnalyticOrbit o{a,std::clamp(b.orbital_eccentricity,0.,.9),b.orbital_inclination_degrees*std::numbers::pi/180,b.orbital_eccentricity>0?.62:0,0,static_cast<double>(mix(static_cast<unsigned>(b.id))%1000000)/1000000*tau,kepler_rate(a,stellar_host_physics(s,host).mass_solar)};
 return o;
}
StellarPosition stellar_planet_position(const StellarSystem& s,const PlanetaryBody& b,double days){
 return plus(stellar_positions(s,days)[planetary_stellar_host(s,b.id)],stellar::engine::analytic_orbit_position(planetary_stellar_orbit(s,b),days));
}
void validate_stellar_orbits(const StellarSystem& s){
 if(!s.stellar_orbits)return;const auto& a=*s.stellar_orbits;
 if(a.version!=1||!s.stellar_object||!s.secondary||a.companions.size()!=(s.tertiary?2u:1u)||a.relative_orbits.size()!=a.companions.size()||(a.belt_host!=0&&a.belt_host!=3))throw std::invalid_argument("Invalid stellar orbit architecture");
 double mass=stellar_host_physics(s,0).mass_solar;
 for(std::size_t i=0;i<a.companions.size();++i){validate_stellar_physics(a.companions[i]);mass+=a.companions[i].mass_solar;const auto& o=a.relative_orbits[i];stellar::engine::validate_orbit(o);
  if(o.eccentricity>.6||std::abs(o.angular_speed-kepler_rate(o.radius,mass))>1e-10)throw std::invalid_argument("Stellar period disagrees with mass and separation");}
 if(a.companions.size()==2&&a.relative_orbits[1].radius*(1-a.relative_orbits[1].eccentricity)<10*a.relative_orbits[0].radius*(1+a.relative_orbits[0].eccentricity))throw std::invalid_argument("Nonhierarchical triple orbit");
 std::set<int> ids;for(auto p:a.planets)if(p.body_id<0||p.host<0||p.host>3||(p.host==2&&a.companions.size()<2)||!ids.insert(p.body_id).second)throw std::invalid_argument("Invalid stellar orbit binding");
}
void validate_stellar_orbit_catalog(std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies){
 std::unordered_map<int,const PlanetaryBody*> lookup;for(const auto& b:bodies)lookup.emplace(b.id,&b);
 for(const auto& s:systems)if(s.stellar_orbits){validate_stellar_orbits(s);for(const auto& p:s.stellar_orbits->planets){
  const auto it=lookup.find(p.body_id);if(it==lookup.end()||it->second->system_id!=s.id)throw std::invalid_argument("Stellar binding refers outside its system");
  if(it->second->parent_body_id&&planetary_stellar_host(s,*it->second->parent_body_id)!=p.host)throw std::invalid_argument("Moon and parent have different stellar hosts");
 }}
}
bool stellar_host_accepts_orbit(const StellarSystem& s,int host,double r,double e){
 if(!std::isfinite(r)||r<=0||!std::isfinite(e)||e<0||e>=.95)return false;
 if(!s.stellar_orbits)return host==0;
 const auto& a=*s.stellar_orbits;const auto& inner=a.relative_orbits[0];const double ma=s.stellar_object->mass_solar,mb=a.companions[0].mass_solar,mu=mb/(ma+mb);
 if(host==3&&r*(1-e)<=1.2*circumbinary_limit(inner.radius,inner.eccentricity,mu))return false;
 if(host<2&&r*(1+e)>=.8*circumstellar_limit(inner.radius,inner.eccentricity,host==0?mu:1-mu))return false;
 if(a.companions.size()==2){const auto& outer=a.relative_orbits[1];const double mc=a.companions[1].mass_solar,om=mc/(ma+mb+mc);
  const double extent=r*(1+e)+(host<2?inner.radius*(1+inner.eccentricity):0);
  if(extent>=.8*circumstellar_limit(outer.radius,outer.eccentricity,host==2?1-om:om))return false;
 }else if(host==2)return false;
 return host>=0&&host<=3;
}
void reconcile_stellar_orbit_clearance(StellarSystem& s,std::span<const PlanetaryBody> bodies){
 if(!s.stellar_orbits)return;
 auto& a=*s.stellar_orbits;const double ma=s.stellar_object->mass_solar,mb=a.companions[0].mass_solar,mu=mb/(ma+mb);
 double extent=1,flux=std::numeric_limits<double>::max();
 for(const auto& b:bodies)if(b.system_id==s.id&&!b.parent_body_id){extent=std::max(extent,planetary_orbit_au(s,b)*(1+b.orbital_eccentricity));if(b.stellar_exposure&&b.stellar_exposure->incident_flux>0)flux=std::min(flux,b.stellar_exposure->incident_flux);}
 if(s.small_body_fields)for(const auto& f:*s.small_body_fields)if(!f.planet_centered)extent=std::max(extent,f.outer_radius_au);
 flux=std::max(1e-10,std::min(flux,s.stellar_object->luminosity_solar/(extent*extent)));
 auto& inner=a.relative_orbits[0];
 if(a.belt_host==0){const double stable=extent/(.65*std::min(circumstellar_limit(1,inner.eccentricity,mu),circumstellar_limit(1,inner.eccentricity,1-mu)));
  const double thermal=(extent+std::sqrt(std::max(s.stellar_object->luminosity_solar,a.companions[0].luminosity_solar)/(.01*flux)))/(1-inner.eccentricity);
  inner.radius=std::max({inner.radius,stable,thermal});inner.angular_speed=kepler_rate(inner.radius,ma+mb);
 }
 if(a.companions.size()==2){auto& outer=a.relative_orbits[1];const double mc=a.companions[1].mass_solar,om=mc/(ma+mb+mc);
  const double stable=(extent+inner.radius*(1+inner.eccentricity))/(.55*std::min(circumstellar_limit(1,outer.eccentricity,om),circumstellar_limit(1,outer.eccentricity,1-om)));
  const double light=std::max(a.companions[1].luminosity_solar,s.stellar_object->luminosity_solar+a.companions[0].luminosity_solar);
  outer.radius=std::max({outer.radius,20*inner.radius,stable,(extent+inner.radius+std::sqrt(light/(.01*flux)))/(1-outer.eccentricity)});outer.angular_speed=kepler_rate(outer.radius,ma+mb+mc);
 }
}
void initialize_stellar_orbits(std::int64_t seed,std::vector<StellarSystem>& systems,std::vector<PlanetaryBody>& bodies,bool fresh){
 std::unordered_map<int,std::vector<PlanetaryBody*>> grouped;for(auto& b:bodies)grouped[b.system_id].push_back(&b);
 for(auto& s:systems){if(!s.secondary||s.stellar_orbits||!s.stellar_object)continue;
  StellarOrbitArchitecture a;const auto key=mix(static_cast<std::uint64_t>(seed)^static_cast<unsigned>(s.id));
  a.companions.push_back(generate_stellar_physics(mix(key),type(*s.secondary)));
  if(s.tertiary)a.companions.push_back(generate_stellar_physics(mix(key+1),type(*s.tertiary)));
  const double ma=s.stellar_object->mass_solar,mb=a.companions[0].mass_solar,mu=mb/(ma+mb),e=.05+(key%15)*.01;
  double inner=std::numeric_limits<double>::max(),outer=1;
  for(const auto* b:grouped[s.id])if(!b->parent_body_id){double r=planetary_orbit_au(s,*b);inner=std::min(inner,r*(1-b->orbital_eccentricity));outer=std::max(outer,r*(1+b->orbital_eccentricity));}
  if(s.small_body_fields)for(const auto& f:*s.small_body_fields)if(!f.planet_centered){inner=std::min(inner,f.inner_radius_au);outer=std::max(outer,f.outer_radius_au);}
  const double physical_min=std::max(.025,8*(s.stellar_object->radius_solar+a.companions[0].radius_solar)*solar_radius_au/(1-e));
  // Existing campaigns retain every established planetary host and distance.
  // Fresh systems may have P-type planets if the binary fits inside the stable
  // inner edge; wide binaries otherwise provide S-type planetary families.
  bool compact=fresh&&s.stellar_object->luminosity_solar>1e-6&&key%3==0&&inner<1e10&&inner>1.5*circumbinary_limit(physical_min,e,mu);
  a.belt_host=compact?3:0;
  double separation=compact?std::max(physical_min,std::min(inner/(1.5*circumbinary_limit(1,e,mu)),.25)):std::max(physical_min,outer/(.65*circumstellar_limit(1,e,mu)));
  a.relative_orbits.push_back({separation,e,.025,static_cast<double>(key%360)*std::numbers::pi/180,.3,static_cast<double>(key%1000)*tau/1000,kepler_rate(separation,ma+mb)});
  s.stellar_orbits=a;
  for(auto* b:grouped[s.id])if(!b->parent_body_id){int host=compact?3:0;
   if(fresh&&a.companions[0].luminosity_solar>1e-6&&!compact&&!b->legacy_colonization_candidate&&!b->cracked_world&&b->orbit_index%4==3)host=1;
   if(fresh&&a.companions.size()==2&&a.companions[1].luminosity_solar>1e-6&&!compact&&!b->legacy_colonization_candidate&&!b->cracked_world&&b->orbit_index%8==5)host=2;
   s.stellar_orbits->planets.push_back({b->id,host});
   if(host&&b->stellar_exposure){const auto hp=stellar_host_physics(s,host);const double factor=std::sqrt(std::max(.000001,hp.luminosity_solar)/std::max(.000001,s.stellar_object->luminosity_solar));
    const auto radius=b->stellar_exposure->orbit_au*factor;
    // A flux-equivalent orbit inside the companion's destruction radius is not a
    // survivable S-type orbit; keep the body bound to the primary instead.
    if(radius*(1-std::clamp(b->orbital_eccentricity,0.,.9))<=hp.destruction_radius_au){s.stellar_orbits->planets.back().host=0;}
    else{b->stellar_exposure=stellar_planet_exposure(hp,radius);
    if(b->appearance){b->appearance->climate.snow_line_au=2.7*std::sqrt(hp.luminosity_solar);if(b->appearance->giant.version)b->appearance->giant.formation_snow_line_au=b->appearance->climate.snow_line_au;}
    outer=std::max(outer,radius*(1+b->orbital_eccentricity));}}
  }
  for(auto* b:grouped[s.id])if(b->parent_body_id){const int host=planetary_stellar_host(s,*b->parent_body_id);s.stellar_orbits->planets.push_back({b->id,host});
   for(auto* parent:grouped[s.id])if(parent->id==*b->parent_body_id)b->stellar_exposure=parent->stellar_exposure;}
  double minimum_flux=std::max(1e-10,s.stellar_object->luminosity_solar/(outer*outer));
  for(const auto* b:grouped[s.id])if(b->stellar_exposure&&b->stellar_exposure->incident_flux>0)minimum_flux=std::min(minimum_flux,b->stellar_exposure->incident_flux);
  if(!compact){auto& o=s.stellar_orbits->relative_orbits[0];
   o.radius=std::max(o.radius,(outer+std::sqrt(std::max(s.stellar_object->luminosity_solar,a.companions[0].luminosity_solar)/(.01*minimum_flux)))/(1-e));
   o.radius=std::max(o.radius,outer/(.65*std::min(circumstellar_limit(1,e,mu),circumstellar_limit(1,e,1-mu))));o.angular_speed=kepler_rate(o.radius,ma+mb);separation=o.radius;}
  if(s.tertiary){const double mc=a.companions[1].mass_solar,oe=.1,om=mc/(ma+mb+mc);const double oa=std::max({20*separation,outer/(.55*circumstellar_limit(1,oe,om)),(outer+separation+std::sqrt(a.companions[1].luminosity_solar/(.01*minimum_flux)))/(1-oe)});
   s.stellar_orbits->relative_orbits.push_back({oa,oe,.06,static_cast<double>((key>>12)%360)*std::numbers::pi/180,.15,.7,kepler_rate(oa,ma+mb+mc)});}
  if(s.small_body_fields)for(auto& f:*s.small_body_fields)if(!f.planet_centered){f.central_mass_solar=stellar_host_physics(s,s.stellar_orbits->belt_host).mass_solar;
   if(compact){const double factor=std::sqrt(stellar_host_physics(s,3).luminosity_solar/s.stellar_object->luminosity_solar);f.inner_radius_au*=factor;f.outer_radius_au*=factor;f.thickness_au*=factor;}}
  validate_stellar_orbits(s);
 }
 for(auto& s:systems)if(s.stellar_orbits){std::vector<PlanetaryBody> local;local.reserve(grouped[s.id].size());for(const auto* b:grouped[s.id])local.push_back(*b);reconcile_stellar_orbit_clearance(s,local);}
}
}
