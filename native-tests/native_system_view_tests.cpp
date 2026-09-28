#include "native_system_view.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>

using namespace stellar::core;
using namespace stellar::native_system;
namespace fs = std::filesystem;
namespace {
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
CampaignFrame frame(const fs::path &research, const fs::path &catalog) {
  auto world = seed_persistable_fresh_campaign(118500, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  return {IntegratedAdaptiveCampaignRuntime::create_fresh(
              load_adaptive_research_strategic_runtime(research), std::move(world)),
          StrategicClock{}, CampaignFramePolicy::Player};
}
NativeSystemBody body(int id, std::optional<int> parent, int orbit,
                      std::string name, PlanetaryBodyKind kind, double radius) {
  return {.id=id,.parent_body_id=parent,.orbit_index=orbit,.name=std::move(name),
      .kind=kind,.radius_earth=radius,
      .visual_class=kind==PlanetaryBodyKind::Moon
          ? NativeSystemBodyVisualClass::unknown_moon
          : NativeSystemBodyVisualClass::unknown_planet};
}
NativeSystemSnapshot synthetic() {
  NativeSystemSnapshot value{.campaign_generation=1,.observer_civilization_id=1,
      .system_id=44,.catalog_name="Geometry",.survey_level=SystemSurveyLevel::fully_surveyed,
      .survey_progress=1};
  auto saturn=body(8401,std::nullopt,0,"Saturn",PlanetaryBodyKind::Planet,9);
  saturn.visual_class=NativeSystemBodyVisualClass::gas_giant;saturn.sol_texture_key="saturn";
  auto moon=body(8402,8401,0,"Moon",PlanetaryBodyKind::Moon,.27);
  moon.visual_class=NativeSystemBodyVisualClass::moon;moon.sol_texture_key="moon";
  auto pluto=body(8410,std::nullopt,9,"Pluto",PlanetaryBodyKind::DwarfPlanet,.186);
  pluto.orbital_eccentricity=pluto_orbital_eccentricity;
  pluto.orbital_inclination_degrees=pluto_orbital_inclination_degrees;
  value.bodies={std::move(saturn),std::move(moon),std::move(pluto)};
  return value;
}
void controller_gates(const fs::path &research,const fs::path &catalog) {
  auto campaign=frame(research,catalog);auto &world=campaign.runtime().world().campaign();
  const auto observer=world.player_civilization_id;
  const auto candidate=std::ranges::find_if(world.systems,[&](const auto &system){
    return world.knowledge.system_survey_level(observer,system.id)==SystemSurveyLevel::unknown &&
           std::ranges::any_of(world.bodies,[&](const auto &item){return item.system_id==system.id&&
             (item.has_rare_resource||item.has_anomaly||item.has_pre_warp_civilization);});});
  require(candidate!=world.systems.end(),"fresh campaign lacks an unknown body system");
  const auto system_id=candidate->id;
  NativeSystemViewController controller;
  auto denied=controller.build(campaign,1,system_id);
  require(!denied.snapshot&& !denied.denial.empty(),"unknown system exposed an orbital DTO");
  world.knowledge.reveal_system(observer,system_id);
  denied=controller.build(campaign,1,system_id);
  require(!denied.snapshot,"detected system exposed an orbital DTO");
  world.knowledge.record_reconnaissance(observer,system_id,.45);
  auto recon=controller.build(campaign,1,system_id);
  require(recon.snapshot&&recon.snapshot->archetype==std::nullopt&&
          recon.snapshot->primary_stellar_class==std::nullopt,"reconnaissance exposed stellar class");
  require(!recon.snapshot->bodies.empty(),"reconnaissance lost the orbital catalog");
  bool retained_positive=false;for(const auto &item:recon.snapshot->bodies) {
    require(!item.details&&!item.sol_texture_key&&!item.world_class,"reconnaissance exposed detailed body fields or textures");
    require(item.visual_class==NativeSystemBodyVisualClass::unknown_planet||
            item.visual_class==NativeSystemBodyVisualClass::unknown_moon,
            "reconnaissance inferred a hidden body class");
    const auto source=std::ranges::find(world.bodies,item.id,&PlanetaryBody::id);
    require(source!=world.bodies.end(),"reconnaissance body has no source record");
    const auto has=[&](NativePositiveSignature signature){return std::ranges::find(item.positive_signatures,signature)!=item.positive_signatures.end();};
    require(has(NativePositiveSignature::rare_resource)==source->has_rare_resource&&
            has(NativePositiveSignature::anomaly)==source->has_anomaly&&
            has(NativePositiveSignature::activity)==source->has_pre_warp_civilization,
            "positive-only reconnaissance signatures lost or invented evidence");
    retained_positive=retained_positive||!item.positive_signatures.empty();
  }
  require(retained_positive,"positive-signature test selected no observable evidence");
  auto raw=std::ranges::find(world.bodies,recon.snapshot->bodies.front().id,&PlanetaryBody::id);
  require(raw!=world.bodies.end(),"fixture body disappeared");
  const auto owned_name=recon.snapshot->bodies.front().name;
  raw->name="hidden mutation";raw->mass_earth*=2;raw->environment.temperature_kelvin=999;
  require(recon.snapshot->bodies.front().name==owned_name,"view retained a raw body reference");
  const auto refreshed=controller.build(campaign,1,system_id);
  require(refreshed.snapshot->bodies.front().visual_class==recon.snapshot->bodies.front().visual_class&&
          !refreshed.snapshot->bodies.front().details,"hidden physics altered reconnaissance class");
  world.knowledge.mark_system_fully_surveyed(observer,system_id);
  const auto full=controller.build(campaign,1,system_id);
  require(full.snapshot&&full.snapshot->archetype&&full.snapshot->bodies.front().details,
          "full survey withheld detailed observer-safe fields");
  const auto sol=controller.build(campaign,1,sol_system_id);
  require(sol.snapshot&&sol.snapshot->survey_level==SystemSurveyLevel::fully_surveyed,
          "fresh home Sol is not fully surveyed");
  for(const auto &item:sol.snapshot->bodies) {
    if(item.name == "Earth") require(item.world_class == PlanetaryWorldClass::Continental, "Earth is not classified Continental");
    if(item.name == "Mercury" || item.name == "Moon") require(item.world_class == PlanetaryWorldClass::Barren, "Airless Sol world is not Barren");
    if(item.name == "Venus") require(item.world_class == PlanetaryWorldClass::Greenhouse, "Venus lost greenhouse classification");
    if(item.id>=pluto_body_id) {
      require(item.appearance&&item.appearance->source_asset_id=="sol:"+std::string(item.id==pluto_body_id?"pluto":sol_moon_definition(item.id)->key),"Supplied Sol material identity missing for "+item.name+": "+(item.appearance?item.appearance->source_asset_id:"no appearance"));
      if(item.kind==PlanetaryBodyKind::Moon)require(item.satellite_orbit.has_value(),"Supplied moon has no physical orbit");
    }
    else require(item.sol_texture_key.has_value(),"fully surveyed approved Sol body lost texture eligibility");
  }
  (void)controller.build(campaign,2,sol_system_id);
  require(controller.is_current_generation(2)&&!controller.is_current_generation(1),
          "campaign replacement did not invalidate the old generation");
  bool stale=false;try{(void)controller.build(campaign,1,sol_system_id);}catch(const std::invalid_argument&){stale=true;}
  require(stale,"stale generation rebuilt a replaced system view");
  bool wrong_thread=false;std::thread worker([&]{try{(void)controller.build(campaign,3,sol_system_id);}catch(const std::logic_error&){wrong_thread=true;}});worker.join();
  require(wrong_thread&&controller.is_current_generation(2),"wrong-thread call mutated the bound generation");
}
void world_classes() {
  PlanetaryBody b; b.mass_earth=1; b.radius_earth=1;
  auto& e=b.environment; e.has_solid_surface=true; e.temperature_kelvin=288;
  e.pressure_kpa=101.3; e.atmosphere=PlanetaryAtmosphereRegime::OxygenNitrogen;
  e.available_solvent=PlanetarySolventRegime::Water;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Continental, "Temperate land/water class");
  e.is_immersed_environment=true;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Ocean, "Immersed world confused with Continental");
  e.pressure_kpa=250;
  require(classify_planetary_world(b)==PlanetaryWorldClass::HighPressureOcean, "High pressure ocean boundary");
  e.is_immersed_environment=false;e.available_solvent=PlanetarySolventRegime::None;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Desert, "Dry warm class");
  e.pressure_kpa=.09;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Barren, "Near-vacuum mislabeled Desert");
  e.temperature_kelvin=120;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Barren, "Cold vacuum inferred unmeasured ice");
  e.pressure_kpa=1;
  require(classify_planetary_world(b)==PlanetaryWorldClass::Frozen, "Cold volatile-bearing class");
  e.has_solid_surface=false;b.mass_earth=100;b.radius_earth=10;
  require(classify_planetary_world(b)==PlanetaryWorldClass::GasGiant, "Cold gas giant confused with ice giant");
  b.mass_earth=15;b.radius_earth=4;
  require(classify_planetary_world(b)==PlanetaryWorldClass::IceGiant, "Ice giant proxy");
}
void geometry() {
  require(std::abs(body_display_radius(std::numeric_limits<double>::quiet_NaN(),PlanetaryBodyKind::Planet)-4*small_body_configuration().planet_scale)<1e-5,
          "nonfinite radius was not sanitized");
  require(std::abs(body_display_radius(.01,PlanetaryBodyKind::Moon)-1.8*small_body_configuration().moon_scale)<1e-5&&
          std::abs(body_display_radius(100,PlanetaryBodyKind::Moon)-24*small_body_configuration().moon_scale)<1e-5,"moon radius clamps changed");
  const auto peri=orbit_point(100,.2444f,17.16f,0),apo=orbit_point(100,.2444f,17.16f,3.14159265358979323846f);
  require(std::hypot(peri.x,peri.y)<std::hypot(apo.x,apo.y),"Pluto focus ellipse lost eccentricity");
  require(orbit_path(100,.2444f,17.16f).size()==193,"orbit path point count changed");
  const auto snapshot=project_system(synthetic());
  const auto saturn=std::ranges::find(snapshot.bodies,8401,&SystemSpatialBodyMarker::body_id);
  const auto moon=std::ranges::find(snapshot.bodies,8402,&SystemSpatialBodyMarker::body_id);
  require(saturn!=snapshot.bodies.end()&&moon!=snapshot.bodies.end(),"family projection lost bodies");
  require(std::abs(std::hypot(moon->offset_x-saturn->offset_x,moon->offset_y-saturn->offset_y)-moon->orbit_radius)<.01,
          "moon orbit is not centered on its parent");
  for(const auto [width,height]:{std::pair{1280.f,720.f},std::pair{1920.f,1080.f},
                                std::pair{2560.f,1440.f},std::pair{3840.f,2160.f}}) {
    const auto viewport=SystemSpatialViewport::fit(snapshot,width,height);
    require(snapshot.design_radius*viewport.scale<=std::min({viewport.center_x-104.f,
        width-224.f-viewport.center_x,viewport.center_y-146.f,height-130.f-viewport.center_y})+.01f,
        "system family does not fit the safe viewport");
    for(const auto &marker:snapshot.bodies) if(viewport.is_body_visible(snapshot,marker)) {
      const auto point=viewport.world_to_screen(marker.offset_x,marker.offset_y);
      require(viewport.hit_body(snapshot,point.x,point.y)==marker.body_id,
              "draw and hit transforms disagree");
    }
    const auto point=viewport.world_to_screen(saturn->offset_x,saturn->offset_y);
    const auto zoomed=viewport.zoomed_at(1.7f,point.x,point.y,.01f,5.f);
    const auto anchored=zoomed.world_to_screen(saturn->offset_x,saturn->offset_y);
    require(std::abs(anchored.x-point.x)<.01f&&std::abs(anchored.y-point.y)<.01f,
            "pointer anchored zoom moved the selected body");
    const auto panned=zoomed.translated(37,-19);
    const auto moved=panned.world_to_screen(saturn->offset_x,saturn->offset_y);
    require(std::abs(moved.x-anchored.x-37)<.01f&&std::abs(moved.y-anchored.y+19)<.01f,
            "pan did not preserve the world transform");
  }
  SystemSpatialViewport close{600,400,4};
  require(close.is_body_visible(snapshot,*moon),"visible moon threshold hid a close moon");
  SystemSpatialViewport distant{600,400,.001f};
  require(!distant.is_body_visible(snapshot,*moon),"moon remained visible below the separation threshold");
  const auto moon_point=close.world_to_screen(moon->offset_x,moon->offset_y);
  require(close.hit_body(snapshot,moon_point.x,moon_point.y)==moon->body_id,
          "visible moon center was not selectable");
}
}
int main(int argc,char **argv)try{
  require(argc==3,"Usage: native_system_view_tests <research-root> <catalog>");
  controller_gates(fs::absolute(argv[1]),fs::absolute(argv[2]));world_classes();geometry();
  std::cout<<"native system observer and geometry cases passed\n";return 0;
}catch(const std::exception &error){std::cerr<<"native system view failed: "<<error.what()<<'\n';return 1;}
