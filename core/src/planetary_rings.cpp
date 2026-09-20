#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planet_appearance.hpp>
#include "planet_configuration_data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_set>
namespace stellar::core {
namespace {
const auto& config(){static const auto j=nlohmann::json::parse(planet_data::rings);return j;}
std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
double unit(std::uint64_t s){return static_cast<double>(mix(s)>>11)*0x1.0p-53;}
bool safe(std::string_view s){return !s.empty()&&s.size()<120&&std::ranges::all_of(s,[](char c){return c=='-'||(c>='a'&&c<='z')||(c>='0'&&c<='9');});}
}
const std::vector<RingFamilyDefinition>& ring_family_definitions(){
 static const auto data=[] {std::vector<RingFamilyDefinition> out;std::unordered_set<std::string> ids;
 for(const auto& j:config().at("families")){RingFamilyDefinition d{j.at("id"),j.at("name"),j.at("composition"),j.at("bondAlbedo"),j.at("particleDensity"),j.at("maxKelvin"),j.at("weight"),j.at("fragmented")};
 if(!safe(d.id)||!ids.insert(d.id).second||d.bond_albedo<0||d.bond_albedo>=1||d.particle_density<=0||d.maximum_kelvin<=0||d.weight<=0)throw std::invalid_argument("Invalid ring family");out.push_back(d);}return out;}();return data;
}
const std::vector<RingArtDefinition>& ring_art_definitions(){
 static const auto data=[] {std::vector<RingArtDefinition> out;std::unordered_set<std::string> ids,hashes;
 for(const auto& j:config().at("assets")){RingArtDefinition a{j.at("id"),j.at("family"),j.at("filename"),j.at("sha256"),j.at("reason"),j.at("variant"),j.at("status")=="accepted",j.at("sourceInnerFraction"),j.at("sourceOuterFraction")};
 if(!safe(a.id)||!ids.insert(a.id).second||std::ranges::find(ring_family_definitions(),a.family,&RingFamilyDefinition::id)==ring_family_definitions().end())throw std::invalid_argument("Invalid ring artwork identity");
 if(a.accepted&&(!hashes.insert(a.sha256).second||a.source_inner_fraction<=0||a.source_outer_fraction<=a.source_inner_fraction))throw std::invalid_argument("Invalid accepted ring material");out.push_back(a);}return out;}();return data;
}
double planet_rotational_oblateness(double mass,double radius,double days){
 if(!std::isfinite(mass)||!std::isfinite(radius)||!std::isfinite(days)||mass<=0||radius<=0||std::abs(days)<.02)throw std::invalid_argument("Invalid rotational shape inputs");
 // Bounded rotating-fluid approximation f = 0.75 q, q = omega^2 R^3 / GM.
 // Internal density profiles are unresolved; this is not an interior-structure solver.
 const double omega=2*std::numbers::pi/(days*86400.),r=radius*6371000.;
 return std::clamp(.75*omega*omega*r*r*r/(3.986004418e14*mass),0.,.14);
}
double planetary_roche_radius(double mass,double radius,double rho){
 if(!std::isfinite(mass)||!std::isfinite(radius)||!std::isfinite(rho)||mass<=0||radius<=0||rho<=0)throw std::invalid_argument("Invalid Roche calculation");
 return 2.44*std::cbrt(5514.*mass/(radius*radius*radius*rho));
}
bool ring_family_thermally_valid(const RingFamilyDefinition& family,double flux,double eccentricity){
 if(!std::isfinite(eccentricity)||eccentricity<0||eccentricity>=1)throw std::invalid_argument("Invalid ring-host eccentricity");
 return planet_equilibrium_temperature(flux/((1-eccentricity)*(1-eccentricity)),family.bond_albedo)<=family.maximum_kelvin;
}
double planet_ring_probability(const PlanetaryBody& b,const StellarPhysicalProperties* star,std::size_t moons){
 const auto type=b.appearance?b.appearance->primary_class:(!b.environment.has_solid_surface?PlanetClass::GasGiant:b.cracked_world?PlanetClass::Cracked:PlanetClass::Barren);
 const auto& chances=config().at("classChances");const auto& id=planet_class_definition(type).id;
 double p=chances.value(id,chances.at("terrestrial").get<double>());
 const bool giant=!planet_class_definition(type).solid;
 const double age=star?std::clamp(1.+(4600.-star->age_myr)/23000.,.8,1.2):1.;
 const double mass=giant?std::clamp(std::pow(b.mass_earth/(type==PlanetClass::IceGiant?24.:300.),.04),.9,1.1):std::clamp(std::pow(b.mass_earth/1.5,.04),.85,1.1);
 const double history=b.appearance&&(b.appearance->climate.history=="recent-impact"||b.appearance->climate.history=="catastrophic-impact")?1.15:1.;
 const double temperature=b.environment.temperature_kelvin>800?.7:1.;
 p*=age*mass*history*temperature*(1.+.02*std::min<std::size_t>(moons,5));
 return std::clamp(p,0.,type==PlanetClass::Cracked?.15:giant?.40:type==PlanetClass::SuperEarth?.02:.01);
}
PlanetRingAppearance make_planet_ring(const PlanetaryBody& b,const PlanetAppearance& a,const StellarPhysicalProperties* star,std::size_t moons,std::string_view forced,int variant){
 PlanetRingAppearance r;r.version=1;r.seed=mix(a.visual_seed+0x52494e47);r.plane_tilt_radians=a.axial_tilt_radians;r.plane_node_radians=a.axis_node_radians;
 auto physical=b;physical.appearance=a;
 if(forced.empty()&&unit(r.seed)>planet_ring_probability(physical,star,moons))return r;
 const auto weights=config().at("significanceWeights").get<std::array<double,4>>();double total=0;for(auto w:weights)total+=w;double roll=unit(r.seed+1)*total;int significance=0;for(int i=0;i<4;++i){roll-=weights[i];if(roll<=0){significance=i;break;}}
 if(planet_class_definition(a.primary_class).solid)significance=std::min(significance,1);
 constexpr std::array labels{"faint","modest","prominent","spectacular"};r.significance=labels[significance];
 const double flux=b.stellar_exposure?b.stellar_exposure->incident_flux:std::pow(a.climate.equilibrium_kelvin/278.5,4)/std::max(.01,1-a.climate.bond_albedo);
 std::vector<const RingFamilyDefinition*> eligible;std::vector<double> candidates;total=0;
 for(const auto& f:ring_family_definitions()){
  if(!forced.empty()&&f.id!=forced)continue;
  if(!ring_family_thermally_valid(f,flux,b.orbital_eccentricity)||planetary_roche_radius(b.mass_earth,b.radius_earth,f.particle_density)<1.22)continue;
  if(std::ranges::none_of(ring_art_definitions(),[&](const auto& art){return art.accepted&&art.family==f.id;}))continue;
  const bool narrow=f.id.starts_with("thin-")||f.id=="faint-debris-ring";
  double w=f.weight*(significance==0?(narrow?3.:.12):significance>=2?(narrow?.15:2.):1.);
  if(f.fragmented)w*=b.cracked_world?6.:.5;
  if(planet_class_definition(a.primary_class).solid&&f.composition=="water-ice")w*=.04;
  eligible.push_back(&f);candidates.push_back(w);total+=w;
 }
 if(eligible.empty()){if(!forced.empty())throw std::invalid_argument("Ring family invalid at this temperature or Roche limit");return r;}
 std::size_t pick=0;roll=unit(r.seed+2)*total;for(std::size_t i=0;i<candidates.size();++i){roll-=candidates[i];if(roll<=0){pick=i;break;}}
 const auto& f=*eligible[pick];std::vector<const RingArtDefinition*> art;for(const auto& x:ring_art_definitions())if(x.accepted&&x.family==f.id)art.push_back(&x);
 const auto& image=*art[variant>=0?static_cast<std::size_t>(variant)%art.size():mix(r.seed+3)%art.size()];
 r.enabled=true;r.family=f.id;r.asset_id=image.id;r.composition=f.composition;r.particle_density=f.particle_density;r.reflectivity=f.bond_albedo;
 r.roche_radius=planetary_roche_radius(b.mass_earth,b.radius_earth,f.particle_density);
 r.equilibrium_kelvin=planet_equilibrium_temperature(flux/std::pow(1-b.orbital_eccentricity,2),f.bond_albedo);
 const double limit=std::min(4.5,r.roche_radius*.96);r.inner_radius=std::min(limit-.05,1.12+unit(r.seed+4)*.22);
 const double width=significance==0?.07:significance==1?.3:significance==2?.8:1.3;
 r.outer_radius=std::min(limit,r.inner_radius+width*(.8+unit(r.seed+5)*.4));
 r.optical_depth=(significance==0?.12:significance==1?.4:significance==2?.8:1.3)*(.85+.3*unit(r.seed+6));
 r.density=1-std::exp(-r.optical_depth);r.thickness=.00002+unit(r.seed+7)*.00025;
 const double tint=.94+.06*unit(r.seed+8);r.color={tint,tint,tint};r.origin=b.cracked_world?"catastrophic-debris":"primordial-or-disrupted-satellite";
 validate_planet_ring(r);return r;
}
void validate_planet_ring(const PlanetRingAppearance& r){
 if(r.version==0)return;
 if(r.version!=1)throw std::invalid_argument("Unsupported ring version");
 for(double x:{r.inner_radius,r.outer_radius,r.density,r.thickness,r.optical_depth,r.particle_density,r.reflectivity,r.equilibrium_kelvin,r.roche_radius,r.plane_tilt_radians,r.plane_node_radians})if(!std::isfinite(x))throw std::invalid_argument("Non-finite ring metadata");
 if(!r.enabled)return;
 const auto& art=ring_art_definitions();auto i=std::ranges::find(art,r.asset_id,&RingArtDefinition::id);
 const auto f=std::ranges::find(ring_family_definitions(),r.family,&RingFamilyDefinition::id);
 if(i==art.end()||!i->accepted||i->family!=r.family||f==ring_family_definitions().end()||f->composition!=r.composition)throw std::invalid_argument("Unapproved ring identity");
 if((r.significance!="faint"&&r.significance!="modest"&&r.significance!="prominent"&&r.significance!="spectacular")||r.equilibrium_kelvin<0||r.density<0||r.density>1||r.plane_tilt_radians<0||r.plane_tilt_radians>std::numbers::pi)throw std::invalid_argument("Invalid ring metadata");
 if(r.inner_radius<=1||r.outer_radius<=r.inner_radius||r.outer_radius>r.roche_radius||r.outer_radius>5||r.thickness<0||r.thickness>.01||r.optical_depth<=0||r.optical_depth>3||r.particle_density<=0||r.reflectivity<0||r.reflectivity>1)throw std::invalid_argument("RING GEOMETRY INVALID");
 if(r.equilibrium_kelvin>f->maximum_kelvin)throw std::invalid_argument("ICE RING INVALID: thermal survival limit exceeded");
}
void migrate_giant_appearance(PlanetaryBody& b){
 if(!b.appearance)return;auto& a=*b.appearance;if(planet_class_definition(a.primary_class).solid)return;
 // Supplied atmospheres already contain their clouds. Never add a second,
 // generated cloud layer, including when loading otherwise-current saves.
 a.atmosphere.cloud_opacity=0;
 const auto& approved=planet_art_definitions();
 const auto current=std::ranges::find(approved,a.source_asset_id,&PlanetArtDefinition::id);
 if(a.giant.version==2&&current!=approved.end()&&current->id.starts_with("giant-")&&current->material_id==a.material_id)return;
 const auto old=a;static const auto manifest=nlohmann::json::parse(planet_data::retired_giants);
 const bool ordinary=a.primary_class==PlanetClass::GasGiant||a.primary_class==PlanetClass::IceGiant;
 const auto art_class=a.primary_class==PlanetClass::MiniNeptune?PlanetClass::IceGiant:a.primary_class==PlanetClass::HotJupiter?PlanetClass::GasGiant:a.primary_class;
 std::string target;std::string fixed_asset;
 // Canonical Sol now uses the replacement pool too, with stable reviewed
 // palettes independent of campaign seed. Its measured physical data stays put.
 if(b.system_id==sol_system_id){
  if(b.id==5){target="amber-storm";fixed_asset="giant-amber-storm-02";}
  if(b.id==6){target="cream-band";fixed_asset="giant-cream-band-01";}
  if(b.id==7){target="cyan-haze";fixed_asset="giant-cyan-haze-03";}
  if(b.id==8){target="deep-azure";fixed_asset="giant-deep-azure-01";}
 }
 for(const auto& row:manifest.at("migration"))if(row.at("baseClass")==planet_class_definition(a.primary_class).id&&row.at("oldSubclass")==a.subclass){target=row.at("newSubclass");break;}
 if(target.empty()&&std::ranges::any_of(planet_subclass_definitions(),[&](const auto& s){return s.primary==a.primary_class&&s.id==a.subclass&&s.generation_enabled;}))target=a.subclass;
 if(!ordinary){
  const std::array gas{"cream-band","amber-storm","dark-cyclone"};const std::array ice{"cyan-haze","deep-azure","teal-storm","dark-polar","frost-veil"};
  target=art_class==PlanetClass::GasGiant?gas[mix(a.visual_seed+17)%gas.size()]:ice[mix(a.visual_seed+17)%ice.size()];
 }
 if(target.empty())target=art_class==PlanetClass::GasGiant?"cream-band":"cyan-haze";
 const auto& s=planet_subclass_definition(art_class,target);std::vector<const PlanetArtDefinition*> pool;for(const auto& art:approved)if(art.primary==art_class&&art.subclass==target&&art.id.starts_with("giant-")&&(fixed_asset.empty()||art.id==fixed_asset))pool.push_back(&art);
 if(pool.empty())throw std::invalid_argument("Giant migration has no approved artwork");
 const auto& art=*pool[mix(old.visual_seed+31)%pool.size()];if(ordinary)a.subclass=target;a.source_asset_id=art.id;a.material_id=art.material_id;a.compatible_classes=art.compatible;
 a.giant={2,s.composition,s.cloud_profile,s.storm_activity,a.climate.snow_line_au};a.climate.surface_water=a.climate.surface_ice=a.climate.vegetation=0;a.emission_strength=0;
 a.oblateness=planet_rotational_oblateness(b.mass_earth,b.radius_earth,a.rotation_period_days);
 if(old.rings.version==1){a.rings=old.rings;}
 else if(old.rings.enabled){
  // Preserve ring presence where the new material can physically survive.
  try{a.rings=make_planet_ring(b,a,nullptr,0,old.rings.composition.find("ice")!=std::string::npos?"broad-ice-ring":"dense-banded-ring");}
  catch(const std::invalid_argument&){try{a.rings=make_planet_ring(b,a,nullptr,0,"thin-dust-ring");}catch(const std::invalid_argument&){a.rings={};a.rings.version=1;a.rings.origin="legacy-ring-outside-material-survival-limits";}}
 }else{a.rings={};a.rings.version=1;}
}
}
