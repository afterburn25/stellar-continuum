#include <stellar/core/developer_planet_index.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
namespace stellar::core {
namespace {
DeveloperGiantLaboratory make_lab(const FreshCampaignState& w,const DeveloperGiantTestRequest& q){
 if(!w.developer_provenance)throw std::invalid_argument("Giant testing requires an isolated developer campaign");
 if((q.type!=PlanetClass::GasGiant&&q.type!=PlanetClass::IceGiant)||q.planet_variant<0||q.ring_variant<0||!std::isfinite(q.axial_tilt_degrees)||q.axial_tilt_degrees<0||q.axial_tilt_degrees>180||!std::isfinite(q.distance_scale)||q.distance_scale<.1||q.distance_scale>10)throw std::invalid_argument("Invalid giant test controls");
 constexpr int sid=1,bid=1;
 StellarSystem system;system.id=sid;system.name="Giant Planet & Ring Laboratory";system.catalog_preset_id="developer-giant-material-lab";
 system.position=decltype(system.position){0,0};system.primary=q.star_type==StellarObjectType::MRedDwarf?StellarClass::MRedDwarf:q.star_type==StellarObjectType::BBlueWhiteStar?StellarClass::HotBlueStar:StellarClass::GYellowDwarf;
 system.stellar_object=generate_stellar_physics(static_cast<std::uint64_t>(w.seed)^0x6769616e74ULL,q.star_type);
 auto& star=*system.stellar_object;
 auto body=make_planet_type_example(static_cast<std::uint64_t>(w.seed),bid,sid,0,star,q.type,q.subclass);auto& a=*body.appearance;
 body.stellar_exposure=stellar_planet_exposure(star,body.stellar_exposure->orbit_au*q.distance_scale);
 a.climate.migrated=body.stellar_exposure->orbit_au<a.climate.snow_line_au;
 a.climate.history=a.climate.migrated?"inward-migration":"in-situ-formation";
 a.climate.equilibrium_kelvin=planet_equilibrium_temperature(body.stellar_exposure->incident_flux,a.climate.bond_albedo);body.environment.temperature_kelvin=a.climate.equilibrium_kelvin;
 const auto& sub=planet_subclass_definition(q.type,q.subclass);
 if(body.environment.temperature_kelvin<sub.temperature[0]||body.environment.temperature_kelvin>sub.temperature[1])throw std::invalid_argument(q.type==PlanetClass::IceGiant?"ICE GIANT PLACEMENT SUSPICIOUS: atmosphere outside subclass temperature range":"Giant atmosphere outside subclass temperature range");
 if(q.type==PlanetClass::IceGiant&&body.stellar_exposure->orbit_au<a.climate.snow_line_au)throw std::invalid_argument("ICE GIANT PLACEMENT SUSPICIOUS: ordinary test world is inside snow line");
 a.axial_tilt_radians=q.axial_tilt_degrees*std::numbers::pi/180;a.axis_node_radians=1.2;
 std::vector<const PlanetArtDefinition*> pool;for(const auto& x:planet_art_definitions())if(x.primary==q.type&&x.subclass==q.subclass)pool.push_back(&x);
 if(pool.empty())throw std::invalid_argument("No approved planet artwork");const auto& art=*pool[q.planet_variant%pool.size()];a.source_asset_id=art.id;a.material_id=art.material_id;a.compatible_classes=art.compatible;
 if(q.rings)a.rings=make_planet_ring(body,a,&star,0,q.ring_family,q.ring_variant);else{a.rings={};a.rings.version=1;}
 validate_planetary_body(body);validate_planet_appearance(a);const auto errors=planet_appearance_contradictions(body);if(!errors.empty())throw std::invalid_argument(errors.front());
 return {std::move(system),std::move(body)};
}
}
DeveloperGiantLaboratory build_developer_giant_test(const FreshCampaignState& w){
 if(!w.developer_provenance||!w.developer_provenance->giant_test)throw std::invalid_argument("Giant laboratory is not initialized");
 const auto& saved=*w.developer_provenance->giant_test;
 auto lab=make_lab(w,saved.controls);lab.body.appearance=saved.appearance;
 validate_planetary_body(lab.body);validate_planet_appearance(saved.appearance);
 const auto errors=planet_appearance_contradictions(lab.body);if(!errors.empty())throw std::invalid_argument(errors.front());
 return lab;
}
std::pair<int,int> apply_developer_giant_test(FreshCampaignState& w,const DeveloperGiantTestRequest& q){
 auto lab=make_lab(w,q);
 w.developer_provenance->giant_test=DeveloperGiantTestState{q,*lab.body.appearance};
 w.developer_provenance->tools_used=true;return {lab.system.id,lab.body.id};
}
}
