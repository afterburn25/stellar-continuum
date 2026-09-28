#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include "../core/src/planet_appearance_json.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
using namespace stellar::core;
void check(bool x,const std::string& why){if(!x)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
int main(int argc,char** argv)try{
 check(argc==2,"Repository path required");auto star=generate_stellar_physics(12,StellarObjectType::GYellowStar);star.luminosity_solar=1;star.age_myr=4600;
 auto base=make_planet_type_example(54,90001,1,2,star,PlanetClass::GasGiant,"cream-band");base.mass_earth=300;base.radius_earth=10;base.orbital_eccentricity=.05;
 base.stellar_exposure=stellar_planet_exposure(star,15);base.environment.temperature_kelvin=90;
 check(ring_family_definitions().size()==10,"Exactly the ten supplied ring families");
 int accepted=0,rejected=0;for(const auto& x:ring_art_definitions())(x.accepted?accepted:rejected)++;
 check(accepted==162&&rejected==28,"Reviewed ring pool count");
 check(planet_type_record(PlanetClass::GasGiant,"emerald-cloud").accepted_image_pool.empty(),"Emerald art must be excluded");
 for(const auto& f:ring_family_definitions()){
  auto a=*base.appearance;a.rings=make_planet_ring(base,a,&star,0,f.id,0);validate_planet_ring(a.rings);
  check(a.rings.enabled&&a.rings.family==f.id,"Forced family did not resolve");
  check(a.rings.outer_radius<=a.rings.roche_radius*.96+1e-8,"Roche bound exceeded");
  check(a.rings==make_planet_ring(base,a,&star,0,f.id,0),"Ring generation not deterministic");
  check(nlohmann::json(a).get<PlanetAppearance>()==a,"Ring metadata does not round-trip");
  auto bad=a.rings;bad.outer_radius=bad.roche_radius+1;rejects([&]{validate_planet_ring(bad);},"Roche violation accepted");
  bad=a.rings;bad.inner_radius=std::numeric_limits<double>::quiet_NaN();rejects([&]{validate_planet_ring(bad);},"NaN radii accepted");
  bad=a.rings;bad.asset_id="rejected-ring";rejects([&]{validate_planet_ring(bad);},"Unapproved ring accepted");
  if(f.composition.find("ice")!=std::string::npos)check(!ring_family_thermally_valid(f,20),"Ice survived hot inner orbit");
 }
 std::map<std::string,int> significance;
 for(const auto type:{PlanetClass::GasGiant,PlanetClass::IceGiant,PlanetClass::Barren,PlanetClass::SuperEarth,PlanetClass::Cracked}){
  auto b=base;b.appearance->primary_class=type;b.mass_earth=type==PlanetClass::IceGiant?24:type==PlanetClass::GasGiant?300:1.5;b.radius_earth=type==PlanetClass::IceGiant?4:type==PlanetClass::GasGiant?10:1;
  int rings=0;constexpr int n=12000;for(int i=0;i<n;++i){b.appearance->visual_seed=i;const auto r=make_planet_ring(b,*b.appearance,&star);if(r.enabled){++rings;if(type==PlanetClass::GasGiant)++significance[r.significance];if(planet_class_definition(type).solid)check(r.significance=="faint"||r.significance=="modest","Terrestrial rings too dominant");}}
  const auto expected=planet_ring_probability(b,&star);check(std::abs(rings/double(n)-expected)<.017,"Ring frequency departed from policy: "+planet_class_definition(type).id);
  std::cout<<planet_class_definition(type).id<<": "<<rings<<" / "<<n<<" rings, expected "<<expected*100<<"%\n";
 }
 const double total=significance["faint"]+significance["modest"]+significance["prominent"]+significance["spectacular"];
 for(const auto& [name,expected]:std::map<std::string,double>{{"faint",.4},{"modest",.35},{"prominent",.2},{"spectacular",.05}})check(std::abs(significance[name]/total-expected)<.03,"Conditional ring significance weights");
 nlohmann::json retired;std::ifstream(std::filesystem::path(argv[1])/"data/planets/deprecated-giant-art-v1.json")>>retired;
 int migrated=0;for(const auto& old:retired.at("records")){
  auto b=base;auto& a=*b.appearance;a.primary_class=planet_class_from_id(old.at("baseClass").get<std::string>());a.subclass=old.at("oldSubclass");a.source_asset_id=old.at("asset");a.material_id=old.at("material");a.giant={};
  a.visual_seed=++migrated;a.rings.version=0;a.rings.enabled=migrated%2==0;const auto original=b;auto copy=b;
  migrate_giant_appearance(b);migrate_giant_appearance(copy);check(b.appearance==copy.appearance,"Legacy migration differs between equal seeds");
  check(b.appearance->subclass==old.at("newSubclass").get<std::string>()&&b.appearance->giant.version==2,"Legacy mapping not applied");
  check(b.mass_earth==original.mass_earth&&b.radius_earth==original.radius_earth&&b.id==original.id&&b.stellar_exposure==original.stellar_exposure,"Migration changed physical identity");
  validate_planet_appearance(*b.appearance);const auto migrated_a=b.appearance;migrate_giant_appearance(b);check(b.appearance==migrated_a,"Repeated migration rerolled art");
 }
 FreshCampaignState world;world.seed=123;DeveloperGiantTestRequest q;
 // Current saves may have v2 metadata but still use a procedural hot giant,
 // an old mini-Neptune cloud image, or the former hard-coded Sol textures.
 for(auto type:{PlanetClass::GasGiant,PlanetClass::IceGiant,PlanetClass::HotJupiter,PlanetClass::MiniNeptune}){
  auto b=base;auto& a=*b.appearance;a.primary_class=type;a.subclass="physical-fallback";a.source_asset_id.clear();a.material_id="procedural:"+planet_class_definition(type).id;a.giant.version=2;a.atmosphere.cloud_opacity=.8;
  const auto before=b;migrate_giant_appearance(b);validate_planet_appearance(*b.appearance);
  check(b.appearance->source_asset_id.starts_with("giant-")&&b.appearance->atmosphere.cloud_opacity==0,"Current fallback escaped replacement policy");
  check(b.appearance->primary_class==type&&b.mass_earth==before.mass_earth&&b.environment.temperature_kelvin==before.environment.temperature_kelvin&&b.environment.pressure_kpa==before.environment.pressure_kpa&&b.environment.has_solid_surface==before.environment.has_solid_surface&&b.stellar_exposure==before.stellar_exposure,"Art migration altered the physical world");
  const auto once=b.appearance;migrate_giant_appearance(b);check(b.appearance==once,"Current migration is not idempotent");
 }
 StellarSystem sol;sol.id=sol_system_id;sol.catalog_preset_id=std::string(sol_catalog_preset_id);sol.stellar_object=star;
 auto catalog=create_sol_catalog(sol);
 for(auto& b:catalog)if(!b.environment.has_solid_surface){
  const auto before=b;b.appearance=planet_appearance_for_existing(42,b,&star);
  check(b.appearance->source_asset_id.starts_with("giant-")&&b.appearance->atmosphere.cloud_opacity==0,"Sol still uses old textures or a cloud layer");
  validate_planet_appearance(*b.appearance);
  const auto different_seed=planet_appearance_for_existing(901,b,&star);
  check(b.appearance->source_asset_id==different_seed.source_asset_id,"Sol palette changed with campaign seed");
  check(b.radius_earth==before.radius_earth&&b.mass_earth==before.mass_earth&&b.orbit_index==before.orbit_index,"Sol migration altered physical data");
 }
 rejects([&]{apply_developer_giant_test(world,q);},"QA command allowed in player campaign");world.developer_provenance.emplace();
 const auto target=apply_developer_giant_test(world,q);check(world.systems.empty()&&world.bodies.empty()&&world.developer_provenance->giant_test.has_value(),"QA changed gameplay galaxy");
 for(const auto& sub:planet_subclass_definitions())if(sub.generation_enabled&&(sub.primary==PlanetClass::GasGiant||sub.primary==PlanetClass::IceGiant)){
  q.type=sub.primary;q.subclass=sub.id;q.planet_variant=3;q.axial_tilt_degrees=87;
  check(apply_developer_giant_test(world,q)==target,"QA replaced stable IDs");
  const auto lab=build_developer_giant_test(world);const std::vector systems{lab.system};const std::vector bodies{lab.body};const auto snapshot=capture_planetary_bodies(bodies,systems);auto restored=restore_planetary_bodies(snapshot,systems);
  check(*restored[0].appearance==world.developer_provenance->giant_test->appearance,"Authoritative save path lost QA planet/ring");
 }
 const auto before=world.developer_provenance->giant_test->appearance;q.distance_scale=.1;rejects([&]{apply_developer_giant_test(world,q);},"Invalid ice placement accepted");check(before==world.developer_provenance->giant_test->appearance,"Rejected QA transaction altered saved planet");
 q.type=PlanetClass::GasGiant;q.subclass="emerald-cloud";rejects([&]{apply_developer_giant_test(world,q);},"Disabled Emerald forced into game");
 check(planet_rotational_oblateness(317.8,11.209,.4135)>.04&&planet_rotational_oblateness(317.8,11.209,.4135)<.1,"Jupiter-scale fluid oblateness implausible");
 std::cout<<migrated<<" retired records migrated; ring distributions, thermal/Roche rules, persistence, and transactional giant lab passed.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
