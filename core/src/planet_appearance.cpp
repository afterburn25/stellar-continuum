#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planetary_satellites.hpp>
#include "planet_configuration_data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
constexpr double pi=std::numbers::pi;
constexpr std::array<std::string_view,planet_class_count> ids{"barren","desert","frozen","ocean","temperate","tundra","volcanic","greenhouse","carbon","super-earth","mini-neptune","gas-giant","ice-giant","hot-jupiter","chthonian","cracked"};
std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
double unit(std::uint64_t x){return static_cast<double>(mix(x)>>11)*0x1.0p-53;}
double choose(std::array<double,2> r,std::uint64_t seed){return r[0]+(r[1]-r[0])*unit(seed);}
bool inside(double x,std::array<double,2> r){return x>=r[0]&&x<=r[1];}
const nlohmann::json& config(){static const auto c=nlohmann::json::parse(planet_data::types);return c;}
bool safe_id(std::string_view value){return !value.empty()&&value.size()<160&&std::ranges::all_of(value,[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c==':';});}
double greenhouse(double equilibrium,double pressure,double coefficient){return equilibrium*(std::pow(1+coefficient*std::pow(pressure/101.3,.65),.25)-1);}
PlanetClass existing_class(const PlanetaryBody& b){
 using enum PlanetClass;const auto& e=b.environment;
 if(b.cracked_world)return Cracked;
 if(!e.has_solid_surface){if(e.temperature_kelvin>800)return HotJupiter;if(b.radius_earth<3.4)return MiniNeptune;if(e.temperature_kelvin<230&&b.mass_earth<55)return IceGiant;return GasGiant;}
 if(e.temperature_kelvin>=950)return Volcanic;
 if(e.temperature_kelvin>350&&e.pressure_kpa>500)return Greenhouse;
 if(e.available_solvent==PlanetarySolventRegime::Hydrocarbon)return Frozen;
 if(e.available_solvent==PlanetarySolventRegime::Water&&planet_liquid_water_possible(e.temperature_kelvin,e.pressure_kpa)){
  if(e.is_immersed_environment)return Ocean;if(b.mass_earth>=1.5)return SuperEarth;if(e.temperature_kelvin<282)return Tundra;return Temperate;
 }
 if(e.temperature_kelvin<240&&e.pressure_kpa>.1)return Frozen;
 if(b.mass_earth>=1.5&&b.radius_earth>=1)return SuperEarth;
 if(e.pressure_kpa>.1&&e.temperature_kelvin<720)return Desert;
 return Barren;
}
struct Proposal {PlanetClimate climate;double pressure{},temperature{},weight{};std::string reason;};
Proposal propose(const PlanetaryBody& b,const StellarPhysicalProperties& star,PlanetClass type){
 const auto& def=planet_class_definition(type);Proposal p;const auto seed=mix(static_cast<std::uint64_t>(b.id))^static_cast<std::uint64_t>(type)*0xd6e8feb86659fd93ULL;
 const auto reject=[&](std::string reason){p.reason=std::move(reason);return p;};
 if(!b.stellar_exposure)return reject("No stellar exposure");
 if(type==PlanetClass::Cracked&&b.kind!=PlanetaryBodyKind::Planet)return reject("Planet debris model requires a primary planet");
 if(!inside(b.mass_earth,def.mass)||!inside(b.radius_earth,def.radius))return reject("Mass/radius outside class range");
 if(def.solid!=b.environment.has_solid_surface)return reject("Bulk composition is incompatible");
 const double flux=b.stellar_exposure->incident_flux;
 p.climate.bond_albedo=def.albedo;p.climate.snow_line_au=2.7*std::sqrt(std::max(0.,star.luminosity_solar));
 p.climate.equilibrium_kelvin=planet_equilibrium_temperature(flux,def.albedo);
 p.pressure=choose(def.pressure,seed+1);
 const auto& atmosphere=def.atmosphere_rules;const double molecule=atmosphere.molecular_mass;
 p.climate.retention_parameter=planet_atmosphere_retention(b.mass_earth,b.radius_earth,p.climate.equilibrium_kelvin,molecule,star,flux);
 if(p.pressure>atmosphere.retention_check_above_kpa&&p.climate.retention_parameter<atmosphere.minimum_retention)return reject("Atmosphere cannot be retained under current gravity and irradiation");
 p.climate.greenhouse_kelvin=def.solid?greenhouse(p.climate.equilibrium_kelvin,p.pressure,def.greenhouse_coefficient):0;
 p.temperature=p.climate.equilibrium_kelvin+p.climate.greenhouse_kelvin;
 if(type==PlanetClass::Frozen&&b.parent_body_id&&b.orbital_eccentricity>.015){
  p.climate.heat_source="tidal";p.climate.history="tidal-flexing";p.climate.internal_flux_wm2=.2+std::min(2.,b.orbital_eccentricity*8);
  p.temperature=std::pow(std::pow(p.climate.equilibrium_kelvin,4)+p.climate.internal_flux_wm2/5.670374419e-8,.25)+p.climate.greenhouse_kelvin;
 }
 if(type==PlanetClass::Volcanic){
  // Local heat does not raise the entire globe to magma temperature. These
  // mechanisms are stored explicitly and influence eligible subclasses.
  if(b.parent_body_id&&b.orbital_eccentricity>.015){p.climate.heat_source="tidal";p.climate.internal_flux_wm2=.2+8*std::min(1.,b.orbital_eccentricity*10);}
  else if(star.age_myr<800){p.climate.heat_source="young-geology";p.climate.internal_flux_wm2=.1+2*(1-star.age_myr/800);}
  else if(unit(seed+5)<.06){p.climate.heat_source="recent-impact";p.climate.history="recent-impact";p.climate.internal_flux_wm2=10000;}
  else if(p.temperature<950)return reject("No sufficient stellar, tidal, young or impact heat source");
  const double radiative=std::pow(std::pow(p.climate.equilibrium_kelvin,4)+p.climate.internal_flux_wm2/5.670374419e-8,.25);
  p.temperature=radiative+p.climate.greenhouse_kelvin;
 }
 if(!inside(p.temperature,def.temperature))return reject("Temperature outside class range");
 if(type==PlanetClass::Ocean||type==PlanetClass::Temperate){if(!planet_liquid_water_possible(p.temperature,p.pressure))return reject("Surface liquid water is not stable");}
 const double peri_flux=flux/std::pow(std::max(.02,1-b.orbital_eccentricity),2);
 if(type==PlanetClass::Frozen&&!b.parent_body_id&&planet_equilibrium_temperature(peri_flux,0)>235)return reject("Frozen primary worlds require a cold stellar orbit");
 if(type==PlanetClass::Frozen&&planet_equilibrium_temperature(peri_flux,.55)>273)return reject("Periapsis heating melts the ordinary ice surface");
 if(type==PlanetClass::HotJupiter){p.climate.migrated=true;p.climate.history="inward-giant-migration";}
 if(!def.solid&&b.stellar_exposure->orbit_au<p.climate.snow_line_au){
  if(type!=PlanetClass::HotJupiter&&unit(seed+8)>(type==PlanetClass::IceGiant?.035:.3))return reject("Interior giant requires migration history");
  p.climate.migrated=true;p.climate.history=type==PlanetClass::IceGiant?"rare-warm-neptune-migration":"inward-giant-migration";
 }
 if(type==PlanetClass::Chthonian){p.climate.history="stripped-envelope";}
 if(type==PlanetClass::Cracked){p.climate.history="catastrophic-impact";}
 double orbital=1,thermal=1,atmospheric=1,age=1,history=1,star_modifier=1;
 if(type==PlanetClass::Frozen||type==PlanetClass::IceGiant)orbital=b.stellar_exposure->orbit_au>p.climate.snow_line_au?2.4:.65;
 if(type==PlanetClass::Ocean||type==PlanetClass::Temperate||type==PlanetClass::Tundra){orbital=b.stellar_exposure->in_habitable_zone?2:.4;star_modifier=std::clamp(star.habitability_modifier,.02,2.);atmospheric=std::clamp(p.climate.retention_parameter/50,.2,1.);}
 if(type==PlanetClass::Volcanic){age=star.age_myr<800?2.4:.6;thermal=p.temperature>950?2:1;}
 if(type==PlanetClass::Desert)thermal=p.temperature>285&&p.temperature<450?1.5:.6;
 if(type==PlanetClass::Greenhouse||type==PlanetClass::Chthonian)orbital=flux>2?2:.5;
 if(type==PlanetClass::Cracked)history=(star.age_myr<600||stellar_object_definition(star.type).evolved)?2.5:1;
 p.weight=def.weight*orbital*thermal*atmospheric*age*history*star_modifier;return p;
}
bool subclass_fits(const PlanetSubclassDefinition& s,const PlanetaryBody& b,const PlanetClimate& c){
 if(!inside(b.environment.temperature_kelvin,s.temperature))return false;
 if(std::ranges::find(s.valid_orbital_zones,planet_orbital_zone(c.equilibrium_kelvin))==s.valid_orbital_zones.end())return false;
 if(s.water>0&&!planet_liquid_water_possible(b.environment.temperature_kelvin,b.environment.pressure_kpa))return false;
 if(s.water>0&&b.environment.available_solvent!=PlanetarySolventRegime::Water)return false;
 if(s.atmosphere_rule=="oxygen-bearing"&&(b.environment.atmosphere!=PlanetaryAtmosphereRegime::OxygenNitrogen&&b.environment.atmosphere!=PlanetaryAtmosphereRegime::OxygenRich))return false;
 if(s.atmosphere_rule=="hydrocarbon-reducing"&&(b.environment.available_solvent!=PlanetarySolventRegime::Hydrocarbon||b.environment.atmosphere!=PlanetaryAtmosphereRegime::Reducing))return false;
 if(s.heat_source=="global-magma"&&b.environment.temperature_kelvin<950)return false;
 if(s.heat_source=="local-geology"&&b.environment.temperature_kelvin<950&&(c.heat_source=="stellar"||c.internal_flux_wm2<=0))return false;
 if(s.primary==PlanetClass::Volcanic&&b.environment.temperature_kelvin<950&&c.heat_source=="stellar")return false;
 return true;
}
bool compatible_surface(const PlanetArtDefinition& image,const PlanetAppearance& a){
 if(image.primary==a.primary_class)return image.subclass==a.subclass;
 if(std::ranges::find(image.compatible,a.primary_class)==image.compatible.end())return false;
 if(!planet_class_definition(a.primary_class).solid)return image.id.starts_with("giant-");
 // Secondary reuse must retain the visible surface state as well as bulk class.
 if(image.liquid!=(a.climate.surface_water>0)||image.ice!=(a.climate.surface_ice>0)||image.vegetation!=(a.climate.vegetation>0))return false;
 if(a.primary_class==PlanetClass::SuperEarth)return a.subclass=="rocky-high-gravity";
 if(a.primary_class==PlanetClass::Barren)return a.subclass=="mineral-highlands"||a.subclass=="iron-rich-rust";
 if(a.primary_class==PlanetClass::Desert)return a.subclass=="red-canyon";
 return false;
}
void assign_surface(PlanetAppearance& a,const PlanetaryBody& b,const PlanetSubclassDefinition* forced=nullptr){
 a.atmosphere.density=std::clamp(std::log1p(b.environment.pressure_kpa)/10,0.,1.);a.atmosphere.haze=a.atmosphere.density*.55;
 std::vector<const PlanetSubclassDefinition*> eligible;for(const auto& s:planet_subclass_definitions())if(s.primary==a.primary_class&&s.generation_enabled&&subclass_fits(s,b,a.climate))eligible.push_back(&s);
 if(eligible.empty()){
  // Existing exotic solvent saves keep their actual environment. Their material
  // is procedural, rather than selecting contradictory liquid-water artwork.
  a.subclass="physical-fallback";a.material_id="procedural:"+planet_class_definition(a.primary_class).id;a.source_asset_id.clear();
  a.climate.surface_ice=b.environment.temperature_kelvin<240&&a.primary_class==PlanetClass::Frozen?.8:0;
  a.atmosphere.cloud_opacity=b.environment.pressure_kpa>10?.2:0;return;
 }
 const PlanetSubclassDefinition* chosen=forced;
 if(!chosen&&a.primary_class==PlanetClass::Temperate){
  const auto& groups=config().at("temperateWeights");double total=0;std::vector<double> weights;
  for(const auto* s:eligible){double group_total=0;for(const auto* other:eligible)if(other->temperate_group==s->temperate_group)group_total+=other->generation_weight;const double weight=group_total>0?groups.at(s->temperate_group).get<double>()*s->generation_weight/group_total:0;weights.push_back(weight);total+=weight;}
  double roll=unit(a.visual_seed+19)*total;for(std::size_t n=0;n<eligible.size();++n){roll-=weights[n];if(roll<=0){chosen=eligible[n];break;}}
 }
 if(!chosen){
  // Keep the existing deterministic choice when weights are uniform.
  if(std::ranges::all_of(eligible,[&](const auto* s){return s->generation_weight==eligible.front()->generation_weight;}))chosen=eligible[mix(a.visual_seed+17)%eligible.size()];
  else {const auto weight=[&](const auto* s){
    if(a.primary_class!=PlanetClass::GasGiant&&a.primary_class!=PlanetClass::IceGiant)return s->generation_weight;
    const double temperature=a.climate.equilibrium_kelvin;
    const double middle=(s->temperature[0]+s->temperature[1])*.5,width=std::max(30.,(s->temperature[1]-s->temperature[0])*.5);
    const double thermal=.7+.6*std::exp(-std::pow((temperature-middle)/width,2));
    const double spin=std::clamp(.6/std::abs(a.rotation_period_days),.5,1.5);
    const double activity=.75+.5*(s->storm_activity*spin+(1-s->storm_activity)/spin);
    const double history=a.climate.migrated?(s->temperature[1]>350?1.3:.7):1.;
    return s->generation_weight*thermal*activity*history;
  };double total=0;for(const auto* s:eligible)total+=weight(s);double roll=unit(a.visual_seed+17)*total;for(const auto* s:eligible){roll-=weight(s);if(roll<=0){chosen=s;break;}}if(!chosen)chosen=eligible.back();}
 }
 const auto& s=*chosen;if(!s.generation_enabled)throw std::invalid_argument("Subclass awaits distinct approved artwork");a.subclass=s.id;a.climate.surface_water=s.water_allowed?s.water:0;a.climate.surface_ice=s.ice_allowed?s.ice:0;a.climate.vegetation=s.vegetation;
 if(a.climate.history=="undisturbed")a.climate.history=s.history;
 a.atmosphere.cloud_opacity=b.environment.pressure_kpa>.1?s.clouds:0;a.emission_strength=s.volcanism_allowed?s.emission:0;
 // Rings are generated independently from the atmospheric/surface artwork.
 a.rings.enabled=false;
 if(a.primary_class==PlanetClass::GasGiant||a.primary_class==PlanetClass::IceGiant)a.giant={2,s.composition,s.cloud_profile,s.storm_activity,a.climate.snow_line_au};
 a.atmosphere.density=std::clamp(std::log1p(b.environment.pressure_kpa)/10,0.,1.);a.atmosphere.haze=a.atmosphere.density*.55;
 if(a.primary_class==PlanetClass::Greenhouse)a.atmosphere.color={.72,.62,.31};
 if(a.primary_class==PlanetClass::Frozen||a.primary_class==PlanetClass::IceGiant)a.atmosphere.color={.39,.68,.84};
 a.material_id="procedural:"+planet_class_definition(a.primary_class).id+":"+a.subclass;a.source_asset_id.clear();
 const auto& record=planet_type_record(a.primary_class,a.subclass);
 static const auto lookup=[] {std::unordered_map<std::string,const PlanetArtDefinition*> result;for(const auto& image:planet_art_definitions())result.emplace(image.id,&image);return result;}();
 std::vector<const PlanetArtDefinition*> art;for(const auto* pool:{&record.accepted_image_pool,&record.compatible_image_pool})for(const auto& id:*pool){const auto& image=*lookup.at(id);
  if(image.liquid&&a.climate.surface_water<=0)continue;if(image.ice&&a.climate.surface_ice<=0)continue;if(image.vegetation&&a.climate.vegetation<=0)continue;
  if(image.clouds&&b.environment.pressure_kpa<10)continue;if(image.emission&&a.emission_strength<=0)continue;if(image.rings)continue;
  art.push_back(&image);
 }
 // Both pools reference the immutable art vector; retain original source order
 // so indexing the pools does not reroll an otherwise unchanged seeded choice.
 std::sort(art.begin(),art.end(),std::less<const PlanetArtDefinition*>{});
 if(!art.empty()){const auto& image=*art[mix(a.visual_seed+31)%art.size()];a.source_asset_id=image.id;a.material_id=image.material_id;a.compatible_classes=image.compatible;}
}
PlanetAppearance base_appearance(std::uint64_t seed,const PlanetaryBody& b){
 PlanetAppearance a;a.tidally_locked=b.parent_body_id.has_value();a.visual_seed=mix(seed^static_cast<std::uint64_t>(b.id));const double v=unit(a.visual_seed+1);
 a.axial_tilt_radians=(v<.94?v*.7:1+v)*pi/2;a.axis_node_radians=unit(a.visual_seed+2)*2*pi;
 a.rotation_period_days=b.environment.has_solid_surface?.6+unit(a.visual_seed+3)*2.5:.32+unit(a.visual_seed+3)*.6;
 if(unit(a.visual_seed+4)<.06)a.rotation_period_days=-a.rotation_period_days;
 a.initial_phase_radians=unit(a.visual_seed+5)*2*pi;a.atmosphere.cloud_period_days=a.rotation_period_days*(.97+unit(a.visual_seed+6)*.06);
 a.oblateness=b.environment.has_solid_surface?0:planet_rotational_oblateness(b.mass_earth,b.radius_earth,a.rotation_period_days);return a;
}
}
bool planet_is_tidally_locked(const PlanetaryBody& b,const StellarPhysicalProperties* star){
 if(b.system_id==sol_system_id&&(sol_moon_definition(b.id)||b.id==pluto_body_id))return true;
 if(b.appearance&&b.appearance->tidally_locked)return *b.appearance->tidally_locked;
 if(b.parent_body_id)return true; // legacy regular satellite population
 if(!b.appearance||!star||star->mass_solar<=0||!b.stellar_exposure)return false;
 const double radius=b.stellar_exposure->orbit_au;
 const double orbital_days=365.25*std::sqrt(radius*radius*radius/star->mass_solar);
 // Preserve an already synchronous saved spin; do not infer locking from class.
 return orbital_days>0&&std::abs(b.appearance->rotation_period_days-orbital_days)<orbital_days*1e-5;
}
PlanetClass planet_class_from_id(std::string_view id){for(std::size_t i=0;i<ids.size();++i)if(id==ids[i])return static_cast<PlanetClass>(i);throw std::invalid_argument("Unknown planet class: "+std::string(id));}
const std::vector<PlanetClassDefinition>& planet_class_definitions(){
 static const auto definitions=[] {std::vector<PlanetClassDefinition> result;for(const auto& j:config().at("classes")){
  PlanetClassDefinition d{planet_class_from_id(j.at("id").get<std::string>()),j.at("id"),j.at("name"),j.at("weight"),j.at("mass"),j.at("radius"),j.at("temperature"),j.at("pressure"),j.at("albedo"),j.at("greenhouseCoefficient"),j.at("solid")};
  const auto& rules=j.at("atmosphereRules");d.atmosphere_rules={d.pressure,rules.at("molecularMass"),rules.at("minimumRetention"),rules.at("retentionCheckAboveKpa"),rules.at("compositionRule")};
  if(d.atmosphere_rules.composition_rule!="carbon-dioxide-rich"&&d.atmosphere_rules.composition_rule!="hydrogen-helium-reducing"&&d.atmosphere_rules.composition_rule!="retained-surface-atmosphere")throw std::invalid_argument("Unknown base atmosphere composition rule");
  const auto& a=d.atmosphere_rules;for(double v:{a.molecular_mass,a.minimum_retention,a.retention_check_above_kpa})if(!std::isfinite(v)||v<=0)throw std::invalid_argument("Invalid planet atmosphere rule");
  for(auto r:{d.mass,d.radius,d.temperature,d.pressure})if(!std::isfinite(r[0])||!std::isfinite(r[1])||r[0]<0||r[1]<r[0])throw std::invalid_argument("Invalid planet class range");result.push_back(std::move(d));}
  if(result.size()!=planet_class_count)throw std::invalid_argument("Planet classes must be complete");double sum=0;for(std::size_t n=0;n<result.size();++n){const auto& d=result[n];if(static_cast<std::size_t>(d.type)!=n||!std::isfinite(d.weight)||d.weight<0)throw std::invalid_argument("Invalid planet class weights/order");sum+=d.weight;}
  if(std::abs(sum-100)>1e-8)throw std::invalid_argument("Planet baseline weights must total 100 percent");return result;}();return definitions;
}
const PlanetClassDefinition& planet_class_definition(PlanetClass type){return planet_class_definitions().at(static_cast<std::size_t>(type));}
const std::vector<PlanetOrbitalZoneDefinition>& planet_orbital_zone_definitions(){
 static const auto definitions=[] {
  constexpr std::array<std::string_view,5> expected{"deep-cold","cold","temperate","warm","hot"};
  std::vector<PlanetOrbitalZoneDefinition> result;double previous=0;
  for(const auto& j:config().at("orbitalZones")){
   PlanetOrbitalZoneDefinition d{static_cast<PlanetOrbitalZone>(result.size()),j.at("id"),j.at("name"),j.at("equilibriumKelvin")};
   if(result.size()>=expected.size()||d.id!=expected[result.size()]||d.equilibrium_kelvin[0]!=previous||!std::isfinite(d.equilibrium_kelvin[1])||d.equilibrium_kelvin[1]<=previous)throw std::invalid_argument("Invalid or overlapping orbital zones");
   previous=d.equilibrium_kelvin[1];result.push_back(std::move(d));
  }
  if(result.size()!=expected.size())throw std::invalid_argument("Incomplete orbital zone definitions");return result;
 }();return definitions;
}
const PlanetOrbitalZoneDefinition& planet_orbital_zone_definition(PlanetOrbitalZone zone){return planet_orbital_zone_definitions().at(static_cast<std::size_t>(zone));}
PlanetOrbitalZone planet_orbital_zone(double kelvin){
 if(!std::isfinite(kelvin)||kelvin<0)throw std::invalid_argument("Invalid orbital equilibrium temperature");
 for(const auto& zone:planet_orbital_zone_definitions())if(kelvin<zone.equilibrium_kelvin[1])return zone.zone;
 throw std::invalid_argument("Equilibrium temperature exceeds configured orbital zones");
}
const std::vector<PlanetSubclassDefinition>& planet_subclass_definitions(){
 static const auto definitions=[] {std::vector<PlanetSubclassDefinition> result;std::unordered_set<std::string> seen;
  for(const auto& j:config().at("subclasses")){PlanetSubclassDefinition s;s.id=j.at("id");s.name=j.at("name");s.primary=planet_class_from_id(j.at("primary").get<std::string>());s.temperature=j.at("temperature");s.composition=j.at("composition");s.heat_source=j.at("heatSource");s.history=j.at("history");s.temperate_group=j.at("temperateGroup");s.water=j.at("water");s.ice=j.at("ice");s.vegetation=j.at("vegetation");s.clouds=j.at("clouds");s.emission=j.at("emission");s.rings=j.at("rings");if(!safe_id(s.id)||!seen.insert(std::to_string(static_cast<int>(s.primary))+s.id).second)throw std::invalid_argument("Duplicate/unsafe planet subclass");
   s.water_allowed=j.at("waterAllowed");s.ice_allowed=j.at("iceAllowed");s.volcanism_allowed=j.at("volcanismAllowed");s.generation_weight=j.at("generationWeight");s.atmosphere_rule=j.at("atmosphereRule");s.generation_enabled=j.value("generationEnabled",true);s.cloud_profile=j.value("cloudProfile",std::string{});s.storm_activity=j.value("stormActivity",.5);
   for(const auto& id:j.at("validOrbitalZones")){const auto& zones=planet_orbital_zone_definitions();const auto it=std::ranges::find(zones,id.get<std::string>(),&PlanetOrbitalZoneDefinition::id);if(it==zones.end()||std::ranges::find(s.valid_orbital_zones,it->zone)!=s.valid_orbital_zones.end())throw std::invalid_argument("Unknown or repeated subclass orbital zone");s.valid_orbital_zones.push_back(it->zone);}
   if(s.valid_orbital_zones.empty()||!std::isfinite(s.generation_weight)||s.generation_weight<=0)throw std::invalid_argument("Missing subclass zones or generation weight");
   if(s.heat_source!="stellar"&&s.heat_source!="local-geology"&&s.heat_source!="global-magma")throw std::invalid_argument("Unknown subclass heat rule");
   if((s.water>0&&!s.water_allowed)||(s.ice>0&&!s.ice_allowed)||(s.emission>0&&!s.volcanism_allowed))throw std::invalid_argument("Subclass surface contradicts its permissions");
   for(double v:{s.water,s.ice,s.vegetation,s.clouds,s.emission})if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("Invalid subclass surface fraction");
   if(!std::isfinite(s.temperature[0])||!std::isfinite(s.temperature[1])||s.temperature[0]<0||s.temperature[1]<s.temperature[0])throw std::invalid_argument("Invalid subclass temperature range");
   if(s.atmosphere_rule!="oxygen-bearing"&&s.atmosphere_rule!="hydrocarbon-reducing"&&s.atmosphere_rule!="class-pressure-retention")throw std::invalid_argument("Unknown subclass atmosphere rule");
   if(s.vegetation>0&&s.atmosphere_rule!="oxygen-bearing")throw std::invalid_argument("Vegetated subclass requires oxygen-bearing atmosphere");
   if(s.primary==PlanetClass::Temperate)(void)config().at("temperateWeights").at(s.temperate_group);result.push_back(std::move(s));}
  double sum=0;for(const auto& w:config().at("temperateWeights")){const double value=w.get<double>();if(!std::isfinite(value)||value<=0)throw std::invalid_argument("Invalid Temperate group weight");sum+=value;}if(std::abs(sum-100)>1e-8)throw std::invalid_argument("Temperate weights must total 100 percent");return result;}();return definitions;
}
const PlanetSubclassDefinition& planet_subclass_definition(PlanetClass type,std::string_view id){for(const auto& s:planet_subclass_definitions())if(s.primary==type&&s.id==id)return s;throw std::invalid_argument("Unknown planet subclass");}
const std::vector<PlanetArtDefinition>& planet_art_definitions(){
 static const auto definitions=[] {std::vector<PlanetArtDefinition> result;std::unordered_set<std::string> seen,seen_ids,materials;const auto root=nlohmann::json::parse(planet_data::art);
  for(const auto& j:root.at("assets")){if(j.at("status")!="accepted"||j.at("earthGeography").get<bool>())throw std::invalid_argument("Rejected/Earth geography image entered planet registry");
   PlanetArtDefinition a;a.id=j.at("id");a.material_id=j.at("materialId");a.source_filename=j.at("filename");a.sha256=j.at("sha256");a.primary=planet_class_from_id(j.at("primaryClass").get<std::string>());a.subclass=j.at("subclass");
   for(const auto& c:j.at("compatibleClasses"))a.compatible.push_back(planet_class_from_id(c.get<std::string>()));
   a.liquid=j.at("liquid");a.ice=j.at("ice");a.vegetation=j.at("vegetation");a.clouds=j.at("clouds");a.emission=j.at("emission");a.rings=j.at("rings");
   if(!safe_id(a.id)||!safe_id(a.material_id)||!seen.insert(a.sha256).second||!seen_ids.insert(a.id).second||!materials.insert(a.material_id).second)throw std::invalid_argument("Duplicate or unsafe planet asset");
   const auto& s=planet_subclass_definition(a.primary,a.subclass);
   if((a.liquid&&!s.water_allowed)||(a.ice&&!s.ice_allowed)||(a.emission&&!s.volcanism_allowed))throw std::invalid_argument("Accepted artwork violates subclass permissions");result.push_back(std::move(a));
  }return result;}();return definitions;
}
const std::vector<RejectedPlanetArtDefinition>& rejected_planet_art_definitions(){
 static const auto definitions=[] {
  std::vector<RejectedPlanetArtDefinition> result;std::unordered_set<std::string> seen;
  for(const auto& a:planet_art_definitions())seen.insert(a.id);
  const auto root=nlohmann::json::parse(planet_data::art);
  for(const auto& j:root.at("rejectedAssets")){
   if(j.at("status")!="rejected")throw std::invalid_argument("Rejected image pool contains an accepted record");
   RejectedPlanetArtDefinition a;a.id=j.at("id");a.source_filename=j.at("filename");a.sha256=j.at("sha256");a.primary=planet_class_from_id(j.at("primaryClass").get<std::string>());a.subclass=j.at("subclass");a.reason=j.at("reason");a.duplicate_of=j.at("duplicateOf");a.audited_subclass=j.at("auditedSubclass");a.earth_geography=j.at("earthGeography");
   if(!safe_id(a.id)||!seen.insert(a.id).second||a.reason.empty())throw std::invalid_argument("Missing rejection reason or overlapping image pools");
   (void)planet_subclass_definition(a.primary,a.subclass);result.push_back(std::move(a));
  }
  for(const auto& a:result)if(!a.duplicate_of.empty()&&(a.duplicate_of==a.id||!seen.contains(a.duplicate_of)))throw std::invalid_argument("Rejected duplicate refers to an unknown image");return result;
 }();return definitions;
}
const std::vector<PlanetTypeRecord>& planet_type_registry(){
 static const auto records=[] {
  std::vector<PlanetTypeRecord> result;const auto& subclasses=planet_subclass_definitions();
  for(const auto& s:subclasses){
   const auto& c=planet_class_definition(s.primary);PlanetTypeRecord r;
   r.base_class=s.primary;r.subclass=s.id;r.name=s.name;r.valid_orbital_zones=s.valid_orbital_zones;
   r.surface_temperature_kelvin={std::max(c.temperature[0],s.temperature[0]),std::min(c.temperature[1],s.temperature[1])};
   if(r.surface_temperature_kelvin[1]<r.surface_temperature_kelvin[0])throw std::invalid_argument("Subclass temperature never fits base class");
   r.heat_rule=s.heat_source;r.atmosphere_rules=c.atmosphere_rules;if(s.atmosphere_rule!="class-pressure-retention")r.atmosphere_rules.composition_rule=s.atmosphere_rule;
   r.water_allowed=s.water_allowed;r.ice_allowed=s.ice_allowed;r.volcanism_allowed=s.volcanism_allowed;
   r.class_percentage=c.weight;r.subclass_weight=s.generation_weight;
   double total=0;for(const auto& other:subclasses)if(other.generation_enabled&&other.primary==s.primary&&(s.primary!=PlanetClass::Temperate||other.temperate_group==s.temperate_group))total+=other.generation_weight;
   r.within_class_percentage=(s.primary==PlanetClass::Temperate?config().at("temperateWeights").at(s.temperate_group).get<double>():100.)*(s.generation_enabled?s.generation_weight:0)/total;
   r.baseline_percentage=r.class_percentage*r.within_class_percentage/100;
   PlanetAppearance appearance;appearance.primary_class=s.primary;appearance.subclass=s.id;appearance.climate.surface_water=s.water;appearance.climate.surface_ice=s.ice;appearance.climate.vegetation=s.vegetation;
   for(const auto& image:planet_art_definitions()){
    if(image.primary==s.primary&&image.subclass==s.id)r.accepted_image_pool.push_back(image.id);
    else if(compatible_surface(image,appearance)&&(!image.emission||s.volcanism_allowed)&&image.rings==s.rings)r.compatible_image_pool.push_back(image.id);
   }
   for(const auto& image:rejected_planet_art_definitions())if(image.primary==s.primary&&image.subclass==s.id)r.rejected_image_pool.push_back(image.id);
   result.push_back(std::move(r));
  }
  for(const auto& c:planet_class_definitions()){double total=0;for(const auto& r:result)if(r.base_class==c.type)total+=r.within_class_percentage;if(std::abs(total-100)>1e-8)throw std::invalid_argument("Subclass baseline percentages do not cover the class");}
  return result;
 }();return records;
}
const PlanetTypeRecord& planet_type_record(PlanetClass type,std::string_view subclass){for(const auto& r:planet_type_registry())if(r.base_class==type&&r.subclass==subclass)return r;throw std::invalid_argument("Unknown planet type record");}
std::array<double,planet_class_count> planet_baseline_weights(){std::array<double,planet_class_count> result{};for(const auto& d:planet_class_definitions())result[static_cast<std::size_t>(d.type)]=d.weight;return result;}
double planet_equilibrium_temperature(double flux,double albedo){if(!std::isfinite(flux)||flux<0||!std::isfinite(albedo)||albedo<0||albedo>=1)throw std::invalid_argument("Invalid thermal inputs");return std::max(3.,278.5*std::pow(flux*(1-albedo),.25));}
double planet_atmosphere_retention(double mass,double radius,double temperature,double molecule,const StellarPhysicalProperties& star,double flux){
 // Dimensionless Jeans parameter at a conservative, irradiation-heated exobase.
 // Additional wind/age erosion is a game eligibility margin, not a loss-rate claim.
 const double exobase=std::max(temperature,250.+500*std::sqrt(std::max(0.,flux))*(1+std::min(5.,star.radiation_modifier/40)));
 const double jeans=7514.*mass/radius*molecule/exobase;
 return jeans/(1+.025*std::min(20.,star.wind_modifier)*std::clamp(star.age_myr/4600.,.1,3.));
}
bool planet_liquid_water_possible(double temperature,double pressure){
 if(!std::isfinite(temperature)||!std::isfinite(pressure)||temperature<273.16||temperature>=647.1||pressure<.611657)return false;
 // Clausius-Clapeyron saturation pressure, intentionally restricted to ordinary
 // surface-ocean gameplay; high-temperature supercritical fluid is not ocean art.
 const double saturation=.611657*std::exp(4890.*(1/273.16-1/temperature));return temperature<=350&&pressure>saturation*1.25;
}
std::vector<PlanetEligibility> planet_class_eligibility(const PlanetaryBody& b,const StellarPhysicalProperties& star){std::vector<PlanetEligibility> result;double total=0;for(const auto& d:planet_class_definitions()){auto p=propose(b,star,d.type);total+=p.weight;result.push_back({d.type,p.weight,std::move(p.reason)});}if(total>0)for(auto& e:result)e.weight/=total;return result;}
PlanetAppearance planet_appearance_for_existing(std::uint64_t seed,const PlanetaryBody& b,const StellarPhysicalProperties* star){
 auto a=base_appearance(seed,b);a.primary_class=existing_class(b);const auto& def=planet_class_definition(a.primary_class);a.climate.bond_albedo=def.albedo;
 a.climate.equilibrium_kelvin=b.stellar_exposure?planet_equilibrium_temperature(b.stellar_exposure->incident_flux,def.albedo):b.environment.temperature_kelvin;
 a.climate.greenhouse_kelvin=std::max(0.,b.environment.temperature_kelvin-a.climate.equilibrium_kelvin);
 if(star){a.climate.snow_line_au=2.7*std::sqrt(star->luminosity_solar);a.climate.retention_parameter=planet_atmosphere_retention(b.mass_earth,b.radius_earth,b.environment.temperature_kelvin,b.environment.has_solid_surface?28:2.3,*star,b.stellar_exposure?b.stellar_exposure->incident_flux:1);}
 if(a.primary_class==PlanetClass::Volcanic&&b.environment.temperature_kelvin>=950)a.climate.heat_source="stellar";
 assign_surface(a,b);
 if(b.system_id==sol_system_id){const std::array<std::string_view,11> keys{"","mercury","venus","earth","mars","jupiter","saturn","uranus","neptune","moon","pluto"};if(b.id>0&&b.id<static_cast<int>(keys.size())){
  a.source_asset_id="sol:"+std::string(keys[b.id]);a.material_id=a.source_asset_id;a.subclass="measured-sol";
  constexpr double periods[]{1,58.646,-243.025, .99727,1.02596,.41354,.444,-.71833,.67125,27.32166,-6.38723};
  constexpr double tilts[]{0,.034,177.36,23.439,25.19,3.13,26.73,97.77,28.32,6.68,122.53};a.rotation_period_days=periods[b.id];a.atmosphere.cloud_period_days=a.rotation_period_days*.98;a.axial_tilt_radians=tilts[b.id]*pi/180;a.rings.enabled=b.id==6||b.id==7;a.rings.density=b.id==6?.7:.25;
 }}
 if(b.system_id==sol_system_id&&b.id==pluto_body_id)a.tidally_locked=true;
 if(b.system_id==sol_system_id){if(const auto* m=sol_moon_definition(b.id)){
  if(b.kind!=PlanetaryBodyKind::Moon||b.parent_body_id!=m->parent||b.name!=m->name)throw std::invalid_argument("Invalid Sol moon identity");
  a.primary_class=m->id==11?PlanetClass::Volcanic:m->id==moon_body_id?PlanetClass::Barren:PlanetClass::Frozen;
  a.source_asset_id="sol:"+std::string(m->key);a.material_id=a.source_asset_id;a.subclass="measured-sol";
  a.tidally_locked=true;a.rotation_period_days=m->period_days;a.atmosphere.cloud_period_days=m->period_days;
  a.axial_tilt_radians=0;a.axis_node_radians=0;a.initial_phase_radians=0;a.rings={};
  a.climate.surface_water=0;a.climate.vegetation=0;a.climate.surface_ice=m->id==11||m->id==moon_body_id?0:.8;
  a.atmosphere.cloud_opacity=0;a.emission_strength=0;
  if(m->id==11){a.climate.heat_source="tidal";a.climate.internal_flux_wm2=2.;}
  if(m->id==20){a.atmosphere.color={.85,.48,.14};a.atmosphere.density=.8;a.atmosphere.haze=.25;}
  else {a.atmosphere.density=m->id==27?.002:0;a.atmosphere.haze=0;}
 }}
 if(!a.source_asset_id.starts_with("sol:"))a.rings=make_planet_ring(b,a,star);
 auto resolved=b;resolved.appearance=a;migrate_giant_appearance(resolved);return *resolved.appearance;
}
void generate_planet_appearances(std::int64_t seed,std::span<const StellarSystem> systems,std::vector<PlanetaryBody>& bodies){
 std::unordered_map<int,std::size_t> moons;for(const auto& b:bodies)if(b.parent_body_id)++moons[*b.parent_body_id];
 std::unordered_map<int,const StellarPhysicalProperties*> stars;for(const auto& s:systems)if(s.stellar_object)stars.emplace(s.id,&*s.stellar_object);
 for(auto& b:bodies){if(b.appearance){migrate_giant_appearance(b);continue;}const auto it=stars.find(b.system_id);
  // Canonical material identity is known even in legacy spectral-only Sol saves.
  if(b.system_id==sol_system_id){b.appearance=planet_appearance_for_existing(static_cast<std::uint64_t>(seed),b,it==stars.end()?nullptr:it->second);continue;}
  if(it==stars.end())continue;const auto& star=*it->second;
  const bool cold_hydrocarbon=b.environment.available_solvent==PlanetarySolventRegime::Hydrocarbon&&b.environment.temperature_kelvin>=70&&b.environment.temperature_kelvin<165;
  if(b.system_id==sol_system_id||b.legacy_colonization_candidate||b.has_pre_warp_civilization||cold_hydrocarbon){b.appearance=planet_appearance_for_existing(static_cast<std::uint64_t>(seed),b,&star);continue;}
  const auto choices=planet_class_eligibility(b,star);double total=0;for(const auto& c:choices)total+=c.weight;
  if(total<=0){
   // A new small, irradiated body may fall outside the broad class ranges.
   // Do not preserve its earlier provisional thick-air environment in that case.
   const double flux=b.stellar_exposure?b.stellar_exposure->incident_flux:0;
   if(b.environment.has_solid_surface&&planet_atmosphere_retention(b.mass_earth,b.radius_earth,b.environment.temperature_kelvin,28,star,flux)<12){
    b.environment.pressure_kpa=0;b.environment.atmosphere=PlanetaryAtmosphereRegime::Vacuum;
    b.environment.available_solvent=PlanetarySolventRegime::None;b.environment.is_immersed_environment=false;
    b.environment.temperature_kelvin=planet_equilibrium_temperature(flux,.12);
   }
   b.appearance=planet_appearance_for_existing(static_cast<std::uint64_t>(seed),b,&star);continue;
  }
  double roll=unit(static_cast<std::uint64_t>(seed)^mix(b.id));PlanetClass type=PlanetClass::Barren;
  for(const auto& choice:choices){roll-=choice.weight;if(roll<=0){type=choice.type;break;}}
  const auto proposal=propose(b,star,type);const auto& def=planet_class_definition(type);auto a=base_appearance(static_cast<std::uint64_t>(seed),b);a.primary_class=type;a.climate=proposal.climate;
  b.environment.temperature_kelvin=proposal.temperature;b.environment.pressure_kpa=proposal.pressure;b.environment.gravity_g=b.mass_earth/(b.radius_earth*b.radius_earth);
  b.environment.atmosphere=proposal.pressure<.1?PlanetaryAtmosphereRegime::Vacuum:def.atmosphere_rules.composition_rule=="hydrogen-helium-reducing"?PlanetaryAtmosphereRegime::Reducing:def.atmosphere_rules.composition_rule=="carbon-dioxide-rich"?PlanetaryAtmosphereRegime::CarbonDioxideRich:PlanetaryAtmosphereRegime::Inert;
  b.environment.available_solvent=PlanetarySolventRegime::None;b.environment.is_immersed_environment=false;
  if((type==PlanetClass::Ocean||type==PlanetClass::Temperate||type==PlanetClass::Tundra||type==PlanetClass::SuperEarth)&&planet_liquid_water_possible(proposal.temperature,proposal.pressure)){
   b.environment.available_solvent=PlanetarySolventRegime::Water;b.environment.atmosphere=PlanetaryAtmosphereRegime::OxygenNitrogen;b.environment.is_immersed_environment=type==PlanetClass::Ocean;
  }
  b.cracked_world=type==PlanetClass::Cracked;assign_surface(a,b);a.rings=make_planet_ring(b,a,&star,moons[b.id]);b.appearance=std::move(a);migrate_giant_appearance(b);
 }
}
void reconcile_frozen_planet_orbits(std::span<const StellarSystem> systems,std::vector<PlanetaryBody>& bodies){
 std::unordered_map<int,std::vector<std::size_t>> grouped;
 for(std::size_t i=0;i<bodies.size();++i)grouped[bodies[i].system_id].push_back(i);
 for(const auto& system:systems){
  if(system.id==sol_system_id||!system.stellar_object||system.stellar_object->luminosity_solar<=0)continue;
  const auto& local=grouped[system.id];
  for(const auto index:local){auto& b=bodies[index];
   const auto star=stellar_host_physics(system,planetary_stellar_host(system,b.id));
   if(b.parent_body_id||!b.appearance||(b.appearance->primary_class!=PlanetClass::Frozen&&(b.appearance->primary_class!=PlanetClass::IceGiant||b.appearance->climate.migrated))||!b.stellar_exposure)continue;
   // A high albedo alone must not place a globally frozen primary next to the
   // star. Legacy hydrocarbon homeworlds retain their inhabited environment.
   const double eccentricity=std::clamp(b.orbital_eccentricity,0.,.9);
   const double flux=b.stellar_exposure->incident_flux,albedo=b.appearance->climate.bond_albedo;
   const bool ice_giant=b.appearance->primary_class==PlanetClass::IceGiant;
   const bool warm_orbit=planet_equilibrium_temperature(flux/std::pow(1-eccentricity,2),0)>235.0001||(ice_giant&&b.stellar_exposure->orbit_au<2.7*std::sqrt(star.luminosity_solar));
   const bool impossible_temperature=planet_equilibrium_temperature(flux,albedo)>b.environment.temperature_kelvin+2;
   if(!warm_orbit&&!impossible_temperature)continue;
   const double required=std::sqrt(star.luminosity_solar)*std::max({ice_giant?2.7:0.,std::pow(278.5/235.,2)/(1-eccentricity),
       std::pow(278.5/std::max(3.,b.environment.temperature_kelvin),2)*std::sqrt(1-albedo)});
   if(b.stellar_exposure->orbit_au>=required*(1-1e-8))continue;
   double distance=required*1.02;
   // Search outwards past complete eccentric orbital envelopes. This runs
   // only at creation/load, not once per tick or renderer frame.
   for(std::size_t pass=0;pass<=local.size();++pass){double next=distance;
    for(auto other_index:local){const auto& other=bodies[other_index];if(other.id==b.id||other.parent_body_id||!other.stellar_exposure||planetary_stellar_host(system,other.id)!=planetary_stellar_host(system,b.id))continue;
     const double r=other.stellar_exposure->orbit_au,e=std::clamp(other.orbital_eccentricity,0.,.9);
     if(distance*(1+eccentricity)>r*(1-e)*.94&&distance*(1-eccentricity)<r*(1+e)*1.06)
      next=std::max(next,r*(1+e)*1.08/(1-eccentricity));
    }if(next==distance)break;distance=next;
   }
   b.stellar_exposure=stellar_planet_exposure(star,distance);
   auto& c=b.appearance->climate;c.equilibrium_kelvin=planet_equilibrium_temperature(b.stellar_exposure->incident_flux,c.bond_albedo);
   c.greenhouse_kelvin=std::max(0.,b.environment.temperature_kelvin-c.equilibrium_kelvin);
   for(auto moon_index:local){auto& moon=bodies[moon_index];if(moon.parent_body_id!=b.id)continue;
    moon.stellar_exposure=b.stellar_exposure;
    if(moon.appearance){auto& mc=moon.appearance->climate;
     const double eq=planet_equilibrium_temperature(b.stellar_exposure->incident_flux,mc.bond_albedo);
     // Keep established environments and artwork intact; update their explicit
     // equilibrium/atmospheric heat budget along with the parent's new orbit.
     mc.equilibrium_kelvin=eq;mc.greenhouse_kelvin=std::max(0.,moon.environment.temperature_kelvin-eq);
    }
   }
  }
 }
}
void validate_planet_appearance(const PlanetAppearance& a){
 validate_planet_ring(a.rings);
 if(a.giant.version!=0&&a.giant.version!=2)throw std::invalid_argument("Unsupported giant profile");
 if(!std::isfinite(a.giant.storm_activity)||a.giant.storm_activity<0||a.giant.storm_activity>1||!std::isfinite(a.giant.formation_snow_line_au))throw std::invalid_argument("Invalid giant atmospheric metadata");
 if(a.rings.version&&a.rings.enabled&&(std::abs(a.rings.plane_tilt_radians-a.axial_tilt_radians)>1e-8||std::abs(a.rings.plane_node_radians-a.axis_node_radians)>1e-8))throw std::invalid_argument("RING ORIENTATION INVALID");
 if(a.version!="planet-appearance-v1"||static_cast<std::size_t>(a.primary_class)>=planet_class_count||!safe_id(a.subclass)||!safe_id(a.material_id))throw std::invalid_argument("Invalid planet appearance identity");
 for(auto c:a.compatible_classes)if(static_cast<std::size_t>(c)>=planet_class_count)throw std::invalid_argument("Unknown compatible planet class");
 for(auto v:{a.axial_tilt_radians,a.axis_node_radians,a.rotation_period_days,a.initial_phase_radians,a.oblateness,a.terrain_height,a.emission_strength,a.atmosphere.density,a.atmosphere.haze,a.atmosphere.cloud_opacity,a.atmosphere.cloud_period_days,a.rings.inner_radius,a.rings.outer_radius,a.rings.density,a.climate.bond_albedo,a.climate.equilibrium_kelvin,a.climate.greenhouse_kelvin,a.climate.internal_flux_wm2,a.climate.snow_line_au,a.climate.retention_parameter,a.climate.surface_water,a.climate.surface_ice,a.climate.vegetation})if(!std::isfinite(v))throw std::invalid_argument("Non-finite planet appearance");
 if(std::abs(a.rotation_period_days)<.02||std::abs(a.atmosphere.cloud_period_days)<.02||a.oblateness<0||a.oblateness>.25||a.terrain_height<0||a.terrain_height>.02||a.rings.inner_radius<=1||a.rings.outer_radius<=a.rings.inner_radius||a.rings.outer_radius>5)throw std::invalid_argument("Invalid planetary rotation/shape/rings");
 for(auto v:{a.atmosphere.density,a.atmosphere.haze,a.atmosphere.cloud_opacity,a.rings.density,a.climate.bond_albedo,a.climate.surface_water,a.climate.surface_ice,a.climate.vegetation})if(v<0||v>1)throw std::invalid_argument("Invalid planet surface fraction");
 for(const auto& color:{a.atmosphere.color,a.rings.color})for(auto v:color)if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("Invalid planet color");
 if(!a.source_asset_id.empty()&&!a.source_asset_id.starts_with("sol:")){
  const auto& art=planet_art_definitions();const auto it=std::find_if(art.begin(),art.end(),[&](const auto& image){return image.id==a.source_asset_id;});if(it==art.end()||it->material_id!=a.material_id||!compatible_surface(*it,a))throw std::invalid_argument("Unapproved or mismatched planet source material");
  if((it->liquid&&a.climate.surface_water<=0)||(it->ice&&a.climate.surface_ice<=0)||(it->vegetation&&a.climate.vegetation<=0)||(it->emission&&a.emission_strength<=0)||it->rings)throw std::invalid_argument("Planet artwork contradicts its canonical surface state");
 }
 if(a.source_asset_id.empty()&&a.material_id!="procedural:"+planet_class_definition(a.primary_class).id&&a.material_id!="procedural:"+planet_class_definition(a.primary_class).id+":"+a.subclass)throw std::invalid_argument("Invalid procedural planet identity");
 if(a.subclass!="physical-fallback"&&!a.source_asset_id.starts_with("sol:"))(void)planet_subclass_definition(a.primary_class,a.subclass);
 if(a.source_asset_id.starts_with("sol:")){
  constexpr std::array<std::string_view,10> keys{"mercury","venus","earth","mars","jupiter","saturn","uranus","neptune","moon","pluto"};
  const auto key=a.source_asset_id.substr(4);
  const bool moon=std::ranges::any_of(sol_moon_definitions(),[&](const auto& m){return m.key==key;});
  if((std::ranges::find(keys,key)==keys.end()&&!moon)||a.material_id!=a.source_asset_id)throw std::invalid_argument("Invalid Sol material identity");
 }
}
std::string planet_appearance_display_name(const PlanetAppearance& a){
 if(a.subclass=="physical-fallback"||a.source_asset_id.starts_with("sol:"))return planet_class_definition(a.primary_class).name;
 return planet_subclass_definition(a.primary_class,a.subclass).name;
}
std::vector<std::string> planet_appearance_contradictions(const PlanetaryBody& b){
 std::vector<std::string> errors;if(!b.appearance)return errors;const auto& a=*b.appearance;const auto& e=b.environment;
 if(a.climate.surface_water>0&&!planet_liquid_water_possible(e.temperature_kelvin,e.pressure_kpa))errors.push_back("Liquid-water artwork contradicts surface temperature/pressure");
 if(a.climate.surface_ice>.5&&e.temperature_kelvin>273.16)errors.push_back("Extensive surface ice is too hot");
 const auto& atmosphere=planet_class_definition(a.primary_class).atmosphere_rules;
 if(!a.source_asset_id.starts_with("sol:")&&e.pressure_kpa>atmosphere.retention_check_above_kpa&&a.climate.retention_parameter>0&&a.climate.retention_parameter<atmosphere.minimum_retention)errors.push_back("Atmosphere retention is insufficient");
 if(a.emission_strength>.3&&e.temperature_kelvin<950&&a.climate.internal_flux_wm2<1000)errors.push_back("Global magma lacks a sufficient heat source");
 if(a.primary_class==PlanetClass::Cracked&&!b.cracked_world)errors.push_back("Shattered material lacks authoritative cracked-world state");
 if(a.primary_class==PlanetClass::IceGiant&&!a.climate.migrated&&!a.source_asset_id.starts_with("sol:")&&(a.climate.equilibrium_kelvin>230||(b.stellar_exposure&&a.climate.snow_line_au>0&&b.stellar_exposure->orbit_au<a.climate.snow_line_au)))errors.push_back("ICE GIANT PLACEMENT SUSPICIOUS: inner cold giant lacks migration history");
 if(a.rings.enabled&&a.rings.version&&b.stellar_exposure){const auto f=std::ranges::find(ring_family_definitions(),a.rings.family,&RingFamilyDefinition::id);if(f!=ring_family_definitions().end()&&!ring_family_thermally_valid(*f,b.stellar_exposure->incident_flux,b.orbital_eccentricity))errors.push_back("ICE RING INVALID: periapsis heating exceeds material limit");}
 return errors;
}
double planet_rotation_phase(const PlanetAppearance& a,double days,bool clouds){if(!std::isfinite(days))throw std::invalid_argument("Invalid planet clock");const double period=clouds?a.atmosphere.cloud_period_days:a.rotation_period_days;return std::remainder(a.initial_phase_radians+std::remainder(days,period)/period*2*pi,2*pi);}
PlanetaryBody make_planet_type_example(std::uint64_t seed,int id,int system_id,int orbit,const StellarPhysicalProperties& star,PlanetClass type,std::string_view subclass){
 const auto& d=planet_class_definition(type);const auto& s=planet_subclass_definition(type,subclass);
 if(star.luminosity_solar<=0||stellar_object_definition(star.type).black_hole)throw std::invalid_argument("Planet example requires a luminous star");
 PlanetaryBody b;b.id=id;b.system_id=system_id;b.orbit_index=orbit;b.name="QA "+s.name;b.kind=PlanetaryBodyKind::Planet;
 using enum PlanetClass;
 const bool giant=!d.solid;b.mass_earth=std::clamp(giant?(type==IceGiant?24.:type==MiniNeptune?18.:type==HotJupiter?950.:300.):type==SuperEarth||type==Chthonian?6.:1.5,d.mass[0],d.mass[1]);
 b.radius_earth=std::clamp(giant?(type==IceGiant?4.:type==MiniNeptune?3.:10.):type==SuperEarth||type==Chthonian?1.6:1.1,d.radius[0],d.radius[1]);
 auto& e=b.environment;e.has_solid_surface=d.solid;e.gravity_g=b.mass_earth/(b.radius_earth*b.radius_earth);
 double desired=type==HotJupiter?1400:giant?130:type==Greenhouse?550:type==Chthonian?1100:type==Frozen?160:s.heat_source=="global-magma"?1400:s.heat_source=="local-geology"?220:290;
 desired=std::clamp(desired,s.temperature[0]+.05,s.temperature[1]-.05);
 e.pressure_kpa=std::clamp(giant?10000.:type==Greenhouse?2500.:101.3,d.pressure[0],d.pressure[1]);
 e.atmosphere=e.pressure_kpa<.1?PlanetaryAtmosphereRegime::Vacuum:giant||s.id=="cryogenic-hydrocarbon"?PlanetaryAtmosphereRegime::Reducing:type==Greenhouse?PlanetaryAtmosphereRegime::CarbonDioxideRich:s.vegetation>0?PlanetaryAtmosphereRegime::OxygenNitrogen:PlanetaryAtmosphereRegime::Inert;
 e.available_solvent=s.water>0?PlanetarySolventRegime::Water:s.id=="cryogenic-hydrocarbon"?PlanetarySolventRegime::Hydrocarbon:PlanetarySolventRegime::None;e.is_immersed_environment=type==Ocean;e.temperature_kelvin=desired;
 auto a=base_appearance(seed,b);a.primary_class=type;a.developer_example=true;a.climate.bond_albedo=d.albedo;
 const double factor=giant?1:std::pow(1+d.greenhouse_coefficient*std::pow(e.pressure_kpa/101.3,.65),.25);
 const double equilibrium=desired/factor;
 a.climate.internal_flux_wm2=s.heat_source=="local-geology"?.3:0;
 a.climate.heat_source=s.heat_source=="local-geology"?"recent-impact-geology":"stellar";a.climate.history=s.heat_source=="local-geology"?"recent-impact":s.history;
 const double incoming=std::max(81.,std::pow(equilibrium,4)-a.climate.internal_flux_wm2/5.670374419e-8);
 const double flux=incoming/std::pow(278.5,4)/(1-d.albedo),distance=std::sqrt(star.luminosity_solar/flux);
 // The navigation safe-approach radius is a spacecraft heat/radiation limit,
 // not a planet survival limit. Hot Jupiters can orbit inside that boundary.
 const double roche_au=2.44*.00465047*std::cbrt(star.mass_solar*1410./(5514.*b.mass_earth/std::pow(b.radius_earth,3)));
 if(distance<=std::max(roche_au,star.destruction_radius_au*1.1))throw std::invalid_argument("Requested example is inside the stellar surface or tidal disruption limit");
 b.stellar_exposure=stellar_planet_exposure(star,distance);
 a.climate.equilibrium_kelvin=planet_equilibrium_temperature(flux,d.albedo);a.climate.greenhouse_kelvin=desired-equilibrium;a.climate.snow_line_au=2.7*std::sqrt(star.luminosity_solar);
 a.climate.retention_parameter=planet_atmosphere_retention(b.mass_earth,b.radius_earth,desired,d.atmosphere_rules.molecular_mass,star,flux);
 if(e.pressure_kpa>d.atmosphere_rules.retention_check_above_kpa&&a.climate.retention_parameter<d.atmosphere_rules.minimum_retention)throw std::invalid_argument("Requested example cannot retain its atmosphere at this star");
 a.climate.migrated=giant&&distance<a.climate.snow_line_au;if(a.climate.migrated)a.climate.history="inward-giant-migration";
 b.cracked_world=type==Cracked;if(!subclass_fits(s,b,a.climate))throw std::invalid_argument("Requested subclass is incompatible with example physics");
 assign_surface(a,b,&s);a.rings=make_planet_ring(b,a,&star);b.appearance=std::move(a);migrate_giant_appearance(b);validate_planetary_body(b);validate_planet_appearance(*b.appearance);
 if(!planet_appearance_contradictions(b).empty())throw std::invalid_argument("Requested example has contradictory surface physics");return b;
}
}
