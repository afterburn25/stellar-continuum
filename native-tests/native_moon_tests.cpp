#include "native_planet_materials.hpp"
#include "native_menu_style.hpp"
#include "native_system_workspace.hpp"
#include "native_system_background.hpp"
#include <stellar/core/planetary_body_persistence.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_planets;
using namespace stellar::native_system;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
double norm(const std::array<double,3>& v){return std::hypot(v[0],v[1],v[2]);}
double delta(const std::array<double,3>& a,const std::array<double,3>& b){return norm({a[0]-b[0],a[1]-b[1],a[2]-b[2]});}
int main(int argc,char** argv)try{
 check(argc==3,"Repository and capture directory required");const std::filesystem::path root=argv[1],folder=argv[2];std::filesystem::create_directories(folder);
 auto star=generate_stellar_physics(42,StellarObjectType::GYellowStar);star.mass_solar=1;star.luminosity_solar=1;
 StellarSystem sol;sol.id=0;sol.name="Sol";sol.primary=StellarClass::GYellowDwarf;sol.catalog_preset_id=std::string(sol_catalog_preset_id);sol.stellar_object=star;
 auto bodies=create_sol_catalog(sol);check(bodies.size()==28&&sol_moon_definitions().size()==19,"Sol must contain all 18 supplied moons plus Earth's Moon");
 for(auto& b:bodies)b.appearance=planet_appearance_for_existing(42,b,&star);
 const auto body=[&](int id)->const PlanetaryBody& {auto it=std::ranges::find(bodies,id,&PlanetaryBody::id);check(it!=bodies.end(),"Missing canonical body");return *it;};
 auto legacy=bodies;legacy.resize(9);legacy[2].has_rare_resource=true;legacy[2].appearance->initial_phase_radians=.12;
 const std::array systems{sol};const auto upgraded=upgrade_saved_sol_catalog(legacy,systems);
 check(upgraded.size()==28&&upgraded[2].has_rare_resource&&upgraded[2].appearance==legacy[2].appearance,"Migration changed an existing planet");
 check(std::ranges::find(upgraded,10,&PlanetaryBody::id)->appearance.has_value(),"Migrated Pluto has no surface material");
 auto again=upgrade_saved_sol_catalog(upgraded,systems);check(again.size()==upgraded.size(),"Migration duplicated moons");
 for(std::size_t i=0;i<again.size();++i)check(again[i].id==upgraded[i].id&&again[i].appearance==upgraded[i].appearance,"Migration changed saved moon identity");
 const auto restored=restore_planetary_bodies(capture_planetary_bodies(bodies,systems),systems);check(restored.size()==28,"Persistence dropped moons");
 for(std::size_t i=0;i<bodies.size();++i)check(restored[i].appearance==bodies[i].appearance&&restored[i].parent_body_id==bodies[i].parent_body_id,"Persistence changed moon art or parent");
 NativeSystemSnapshot snapshot;snapshot.system_id=0;snapshot.catalog_name="Sol";snapshot.survey_level=SystemSurveyLevel::fully_surveyed;snapshot.stellar_object=star;
 for(const auto& b:bodies){NativeSystemBody v;v.id=b.id;v.name=b.name;v.kind=b.kind;v.parent_body_id=b.parent_body_id;v.orbit_index=b.orbit_index;v.radius_earth=b.radius_earth;v.appearance=b.appearance;v.orbit_au=planetary_orbit_au(sol,b);v.orbital_eccentricity=b.orbital_eccentricity;v.orbital_inclination_degrees=b.orbital_inclination_degrees;
  if(b.parent_body_id)v.satellite_orbit=planetary_satellite_orbit(body(*b.parent_body_id),b);else v.stellar_orbit=planetary_stellar_orbit(sol,b);
  snapshot.bodies.push_back(std::move(v));
 }
 auto spatial=project_system(snapshot);
 const auto marker=[&](int id)->const SystemSpatialBodyMarker& {return *std::ranges::find(spatial.bodies,id,&SystemSpatialBodyMarker::body_id);};
 std::set<std::string> identities;
 for(const auto& m:sol_moon_definitions()){
  const auto& b=body(m.id);check(b.name==m.name&&b.parent_body_id==m.parent&&b.appearance->source_asset_id=="sol:"+std::string(m.key),"Canonical filename/body/parent mismatch");
  check(identities.insert(b.appearance->source_asset_id).second&&planet_is_tidally_locked(b,&star),"Moon identity reused or unlocked");
  auto orbit=planetary_satellite_orbit(body(m.parent),b);
  for(double day:{0.,.123,10000.,1e7}){
   const auto p=satellite_relative_position(orbit,day),p2=satellite_relative_position(orbit,day+m.period_days);
   check(delta(p,p2)<m.semimajor_km*1e-6,"Analytic satellite fails period closure");
   check(norm(p)>=m.semimajor_km*(1-m.eccentricity)-.001&&norm(p)<=m.semimajor_km*(1+m.eccentricity)+.001,"Satellite left its physical ellipse");
   snapshot.simulation_days=day;const auto& view=*std::ranges::find(snapshot.bodies,m.id,&NativeSystemBody::id);const auto l=system_lighting(snapshot,view);
   check(l.locked_rotation.has_value(),"Moon has no parent-facing 3D frame");const auto facing=rotate_light(*l.locked_rotation,{0,0,1});
   check(std::hypot(facing.x+p[0]/norm(p),facing.y-p[1]/norm(p),facing.z+p[2]/norm(p))<1e-5,"Tidal face lost its parent");
  }
  snapshot.simulation_days=0;update_system_motion(snapshot,spatial);const auto& v=marker(m.id);const auto& parent=marker(m.parent);
  const auto relative=satellite_relative_position(orbit,0);const double scale=v.orbit_radius/orbit.relative.radius;
  check(std::hypot(v.offset_x-parent.offset_x-relative[0]*scale,v.offset_y-parent.offset_y-relative[1]*scale)<.1,"Display moon not on physical relative orbit");
  auto path=projected_orbit_path(spatial,v);check(path.size()>60,"Satellite has no separate orbit guide");
  const double factor=orbit.binary?1-orbit.parent_mass_fraction:1;auto guide_orbit=orbit;guide_orbit.relative.phase=0;const auto guide=satellite_relative_position(guide_orbit,0);
  check(std::hypot(path.front().x-guide[0]*scale*factor,path.front().y-guide[1]*scale*factor)<.1,"Moon guide differs from moon position");
 }
 // Triton is retrograde relative to Neptune; Uranian orbits follow its tilted equator.
 auto triton=planetary_satellite_orbit(body(8),body(27));triton.frame_inclination=0;
 const auto t0=satellite_relative_position(triton,0),t1=satellite_relative_position(triton,.001);check(t0[0]*t1[1]-t0[1]*t1[0]<0,"Triton lost retrograde motion");
 auto ariel=planetary_satellite_orbit(body(7),body(23));ariel.relative.phase=std::numbers::pi/2;ariel.relative.periapsis=0;ariel.relative.ascending_node=0;const auto pole=satellite_relative_position(ariel,0);
 check(std::abs(pole[2])/norm(pole)>.98,"Uranian satellite frame is not tilted with its parent");
 snapshot.simulation_days=12.5;update_system_motion(snapshot,spatial);const auto first=spatial;for(int i=0;i<100;++i)update_system_motion(snapshot,spatial);
 for(std::size_t i=0;i<spatial.bodies.size();++i)check(first.bodies[i].offset_x==spatial.bodies[i].offset_x&&first.bodies[i].offset_y==spatial.bodies[i].offset_y,"Repeated update drifted a satellite or barycentre");
 auto no_charon=snapshot;no_charon.bodies.pop_back();auto bary=project_system(no_charon);const auto& bary_pluto=*std::ranges::find(bary.bodies,10,&SystemSpatialBodyMarker::body_id);const auto f=planetary_satellite_orbit(body(10),body(28)).parent_mass_fraction;
 check(std::hypot((1-f)*marker(10).offset_x+f*marker(28).offset_x-bary_pluto.offset_x,(1-f)*marker(10).offset_y+f*marker(28).offset_y-bary_pluto.offset_y)<.1,"Pluto/Charon barycentre not mass weighted");
 auto spectral_only=snapshot;spectral_only.stellar_object.reset();
 check(system_lighting(spectral_only,spectral_only.bodies.back()).locked_rotation.has_value(),"Legacy spectral-only Sol lost synchronous moon orientation");
 // Alignment cases use the public snapshot and physical blocker radii.
 auto alignment=snapshot;alignment.simulation_days=0;alignment.bodies={snapshot.bodies[2],snapshot.bodies[8]};
 alignment.bodies[0].stellar_orbit=stellar::engine::AnalyticOrbit{1,0,0,0,0,0,.01};
 alignment.bodies[1].satellite_orbit=SatelliteOrbit{stellar::engine::AnalyticOrbit{384400,0,0,0,0,0,1}};
 check(system_lighting(alignment,alignment.bodies[1]).eclipse.has_value(),"Parent does not eclipse its aligned moon");
 check(!system_lighting(alignment,alignment.bodies[0]).eclipse,"Moon behind its parent incorrectly shadows the planet");
 alignment.bodies[1].satellite_orbit->relative.phase=std::numbers::pi;
 check(system_lighting(alignment,alignment.bodies[0]).eclipse.has_value(),"Foreground moon does not shadow its parent");
 check(!system_lighting(alignment,alignment.bodies[1]).eclipse,"Sunward moon incorrectly eclipsed by its parent");
 alignment.bodies[1].satellite_orbit->relative.phase=std::numbers::pi/2;
 check(!system_lighting(alignment,alignment.bodies[1]).eclipse&&!system_lighting(alignment,alignment.bodies[0]).eclipse,"Unaligned moon casts an eclipse");
 // Plane alignment: the mesh pole stays fixed while longitude changes slowly.
 auto earth=*body(3).appearance;const auto q0=display_orientation(earth,0),q1=display_orientation(earth,60);
 const auto north0=rotate_light(q0,{0,1,0}),north1=rotate_light(q1,{0,1,0});const auto face0=rotate_light(q0,{0,0,1}),face1=rotate_light(q1,{0,0,1});
 check(std::hypot(north0.x-north1.x,north0.y-north1.y,north0.z-north1.z)<1e-5,"Rotating planet precessed off its axis");
 const double turn=std::acos(std::clamp(double(face0.x*face1.x+face0.y*face1.y+face0.z*face1.z),-1.,1.));check(turn>.2&&turn<.9,"Planet rotation stopped or is too fast");
 // The public campaign/controller path must actually publish the new bodies.
 auto world=seed_persistable_fresh_campaign(42,load_nearby_catalog(root/"data/astronomy/hyg-nearby-500-v1.json"),{"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},true});
 CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root/"data/research/v1"),std::move(world)),{},CampaignFramePolicy::Developer);
 NativeSystemViewController controller;auto live=controller.build(frame,1,0);check(live.snapshot&&live.snapshot->bodies.size()==28,"Live campaign/controller dropped canonical moons");
 for(const auto& b:live.snapshot->bodies)if(b.kind==PlanetaryBodyKind::Moon)check(b.satellite_orbit&&b.appearance,"Live moon lost dynamics or artwork");
 Window window("Sol moon material and orbit validation",1280,720,false,root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");window.set_vsync(0);window.set_frame_cap(0);(void)window.poll();
 Lighting light;light.direction={.5f,.3f,.9f};light.color=blackbody_light_color(5772);
 int index=0;DrawList sheet;nlohmann::json report=nlohmann::json::array();
 const auto draw=[&](DrawList& list,UiRect area,const PlanetAppearance& a,const MaterialSet& maps,const Lighting& l,std::string caption){std::vector<MeshInstance3D> instances;append_instances(instances,a,maps,{},1,1024,0,l);
  Camera3D camera;camera.position={0,0,8};camera.projection=Projection3D::Perspective;camera.near_plane=.1f;camera.far_plane=20;camera.vertical_fov_radians=static_cast<float>(2*std::atan(1.12/8));
  list.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(instances)),area});stellar::native_menu_style::text(list,{area.x,area.y+area.height-28,area.width,26},caption,16,stellar::native_menu_style::ink,TextAlign::Center);
 };
 for(const auto& m:sol_moon_definitions()){if(m.id==9)continue;auto a=*body(m.id).appearance;const auto maps=load_material(root/"assets/visual",a,2048),small=load_material(root/"assets/visual",a,128);
  check(maps.identity==small.identity&&maps.albedo->width()==2048&&small.portrait,"Moon quality or shared identity lost");check(!maps.emission&&a.atmosphere.cloud_opacity==0,"Moon gained invented effects");
  check(maps.albedo->pixels()==decode_rgba_image(root/"assets/visual/moons"/("sol-"+std::string(m.key))/"albedo.png")->pixels(),"Runtime altered supplied albedo");
  const auto overview=load_material(root/"assets/visual",a,512);
  draw(sheet,{float(index%4)*320,float((index%8)/4)*360,320,340},a,overview,light,std::string(m.name));
  DrawList close;draw(close,{0,0,1280,700},a,maps,light,std::string(m.name));window.draw(close,folder/(std::string(m.key)+"-close.png"));
  report.push_back({{"id",m.id},{"name",m.name},{"parent",m.parent},{"source",a.source_asset_id},{"periodDays",m.period_days},{"radiusKm",m.radius_km},{"semiMajorKm",m.semimajor_km},{"tidallyLocked",true}});
  if(++index%8==0){window.draw(sheet,folder/("moons-overview-"+std::to_string(index/8)+".png"));sheet={};}
 }
 if(index%8)window.draw(sheet,folder/"moons-overview-3.png");
 // Render the production system workspace, with its real paths, occlusion,
 // selection, tracking, material LOD and faint background, not a contact sheet.
 NativeSystemBackground sky;sky.configure(root,{});sky.bind(frame.runtime().world().campaign());
 for(const int parent_id:{3,5,6,7,8,10}){
  std::map<std::pair<std::string,int>,std::shared_ptr<const MaterialSet>> family_maps;
  stellar::native_system_ui::NativeSystemWorkspace workspace;
  workspace.set_planet_materials([&](const PlanetAppearance& a,int width){auto key=std::pair{material_cache_identity(a),width};auto& maps=family_maps[key];if(!maps)maps=std::make_shared<const MaterialSet>(load_material(root/"assets/visual",a,width));return maps;});
  auto live_snapshot=*live.snapshot;live_snapshot.simulation_days=2.;workspace.open(live_snapshot,1280,720);
  check(workspace.select_body(parent_id),"Cannot select moon parent in live workspace");workspace.focus_selected_body(1280,720);
  auto live_spatial=project_system(live_snapshot);double extent=1.;
  for(const auto& m:live_spatial.bodies)if(m.parent_body_id==parent_id)extent=std::max(extent,double(m.orbit_radius)*1.08);
  const auto field=stellar::native_system_ui::SystemWorkspaceLayout::for_viewport(1280,720).world_field;
  const Point center{field.x+field.width*.5f,field.y+field.height*.5f};
  const double target=.40*std::min(field.width,field.height)/extent;
  (void)workspace.handle({InputEventType::Wheel,center,{},static_cast<float>(std::log(target/workspace.viewport()->scale)/std::log(1.16))},1280,720);
  for(const double day:{2.,2.+1./24.,22.}){
   workspace.set_simulation_days(day);live_snapshot.simulation_days=day;live_spatial=project_system(live_snapshot);
   const auto parent=std::ranges::find(live_spatial.bodies,parent_id,&SystemSpatialBodyMarker::body_id);
   const auto screen=workspace.viewport()->world_to_screen(parent->offset_x,parent->offset_y);
   check(std::hypot(screen.x-center.x,screen.y-center.y)<.05,"Focused planet escaped while game time advanced");
  }
  DrawList family;sky.append(family,0,1280,720,3,1);workspace.render(family,1280,720,false);
  int resolved=0;for(const auto& m:live_spatial.bodies)if(m.parent_body_id==parent_id){check(workspace.viewport()->is_body_visible(live_spatial,m),"A moon disappears at its family's viewing scale");++resolved;}
  check(resolved>0&&workspace.artwork_ready(),"Family view lost moon artwork");
  window.draw(family,folder/(std::string(body(parent_id).name)+"-family.png"));
 }
 std::ofstream(folder/"validation.json")<<nlohmann::json{{"suppliedMoons",18},{"allMoons",19},{"solBodies",28},{"migration",true},{"tidalFacing",true},{"retrograde",true},{"barycentre",true},{"axisStable",true},{"moons",report}}.dump(2);
 std::cout<<"18 supplied 3D moons, 19 nested orbits, persistence/migration, retrograde motion, synchronous facing, stable axes and GPU captures passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
