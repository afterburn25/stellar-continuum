#include "native_giant_test_panel.hpp"
#include "native_small_body_renderer.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/ring_material_preparation.hpp>
#include <iostream>
using namespace stellar::core;using namespace stellar::native_map;using namespace stellar::native_planets;
void check(bool v,const std::string& m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv)try{
 check(argc==3,"Repository and output paths required");const std::filesystem::path root=argv[1],folder=argv[2];std::filesystem::create_directories(folder);
 Window window("Giant and ring visual validation",1280,720,false,root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");window.set_vsync(0);window.set_frame_cap(0);(void)window.poll();
 auto star=generate_stellar_physics(12,StellarObjectType::GYellowStar);star.luminosity_solar=1;
 Lighting light;light.direction={.7f,.2f,.9f};light.color=blackbody_light_color(5772);
 const auto draw_world=[&](DrawList& draw,UiRect area,const PlanetAppearance& a,Quaternion view,const Lighting& illumination,std::string caption){
  const int lod=area.width<640?256:1024;
  const auto maps=load_material(root/"assets/visual",a,lod);std::vector<MeshInstance3D> objects;append_instances(objects,a,maps,{},1,lod,0,illumination,view);
  Camera3D camera;camera.position={0,0,8};camera.projection=Projection3D::Perspective;camera.near_plane=.1f;camera.far_plane=20;
  camera.vertical_fov_radians=2*std::atan((a.rings.enabled?a.rings.outer_radius:1)*1.16/8);
  draw.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(objects)),area});
  stellar::native_menu_style::text(draw,{area.x+3,area.y+area.height-31,area.width-6,28},caption,16,stellar::native_menu_style::ink,TextAlign::Center);
 };
 int index=0;DrawList sheet;
 for(const auto& sub:planet_subclass_definitions())if(sub.generation_enabled&&(sub.primary==PlanetClass::GasGiant||sub.primary==PlanetClass::IceGiant)){
  auto body=make_planet_type_example(54,90001,1,2,star,sub.primary,sub.id);body.appearance->rings.enabled=false;body.appearance->axial_tilt_radians=0;body.appearance->axis_node_radians=0;
  const auto& a=*body.appearance;draw_world(sheet,{float(index%4)*320,float((index%8)/4)*360,320,340},a,{},light,sub.name);
  DrawList close;draw_world(close,{0,0,1280,700},a,{},light,sub.name+" / "+a.source_asset_id);window.draw(close,folder/(sub.id+"-close.png"));
  if(++index%8==0){window.draw(sheet,folder/"giants-overview.png");sheet={};}
 }
 if(index%8)window.draw(sheet,folder/"giants-overview-2.png");
 StellarSystem sol;sol.id=sol_system_id;sol.catalog_preset_id=std::string(sol_catalog_preset_id);sol.stellar_object=star;
 auto sol_bodies=create_sol_catalog(sol);DrawList sol_sheet;int solar_index=0;
 for(auto& b:sol_bodies){b.appearance=planet_appearance_for_existing(42,b,&star);if(b.environment.has_solid_surface)continue;
  check(b.appearance->source_asset_id.starts_with("giant-")&&b.appearance->atmosphere.cloud_opacity==0,"Sol uses a retired giant or extra cloud layer");
  DrawList close;draw_world(close,{0,0,1280,700},*b.appearance,{},light,b.name+" / "+b.appearance->source_asset_id);window.draw(close,folder/(b.name+"-replacement.png"));
  draw_world(sol_sheet,{float(solar_index++)*320,0,320,340},*b.appearance,{},light,b.name);
 }
 window.draw(sol_sheet,folder/"sol-four-giants.png");
 {
  stellar::native_system::NativeSystemSnapshot s;s.campaign_generation=1;s.system_id=0;s.survey_level=SystemSurveyLevel::fully_surveyed;s.small_body_fields=generate_small_body_fields(75,sol,sol_bodies);
  for(const auto& b:sol_bodies)s.bodies.push_back({.id=b.id,.parent_body_id=b.parent_body_id,.orbit_index=b.orbit_index,.name=b.name,.kind=b.kind,.radius_earth=b.radius_earth,.orbital_eccentricity=b.orbital_eccentricity,.orbital_inclination_degrees=b.orbital_inclination_degrees,.orbit_au=planetary_orbit_au(sol,b)});
  const auto spatial=stellar::native_system::project_system(s);const auto view=stellar::native_system::SystemSpatialViewport::fit(spatial,1280,720);
  stellar::native_system_ui::NativeSmallBodyRenderer rocks;
  auto prior=s;prior.small_body_fields[0].visible_count=700;prior.small_body_fields[1].visible_count=720;
  DrawList before;rocks.render(before,prior,spatial,view,{0,0,1280,720},0,false);const auto previous=rocks.statistics();window.draw(before,folder/"belts-before.png");
  DrawList after;rocks.render(after,s,spatial,view,{0,0,1280,720},0,false);const auto current=rocks.statistics();window.draw(after,folder/"belts-denser.png");
  check(current.instances==4096&&current.visible>previous.visible*2.7,"Denser Sol representatives did not reach the renderer");
  std::cout<<"Sol belt visible representatives: "<<previous.visible<<" -> "<<current.visible<<"; solids="<<current.solid_bodies<<'\n';
 }
 auto body=make_planet_type_example(54,90001,1,2,star,PlanetClass::GasGiant,"cream-band");body.stellar_exposure=stellar_planet_exposure(star,20);body.mass_earth=300;body.radius_earth=10;
 body.appearance->axial_tilt_radians=.55;body.appearance->axis_node_radians=.3;
 index=0;sheet={};
 for(const auto& f:ring_family_definitions()){
  // Select a naturally prominent saved ring so radial features remain inspectable.
  for(int seed=0;seed<1000;++seed){body.appearance->visual_seed=seed;body.appearance->rings=make_planet_ring(body,*body.appearance,&star,0,f.id,0);if(body.appearance->rings.significance=="spectacular")break;}
  const auto view=rotation_axis_angle({1,0,0},.65f);
  draw_world(sheet,{float(index%4)*320,float((index%8)/4)*360,320,340},*body.appearance,view,light,f.name);
  DrawList close;draw_world(close,{0,0,1280,700},*body.appearance,view,light,f.name);window.draw(close,folder/(f.id+"-close.png"));
  if(f.id=="bright-ice-ring"||f.id=="dense-banded-ring"){
   DrawList tilted;draw_world(tilted,{0,0,1280,700},*body.appearance,rotation_axis_angle({1,0,0},-.25f),light,f.name+" · shallow view");window.draw(tilted,folder/(f.id+"-shallow.png"));
  }
  if(++index%8==0){window.draw(sheet,folder/"rings-overview.png");sheet={};}
 }
 window.draw(sheet,folder/"rings-overview-2.png");
 {auto a=*body.appearance;a.source_asset_id=a.material_id="sol:pluto";a.rings.enabled=false;a.atmosphere.density=0;a.axial_tilt_radians=0;a.axis_node_radians=0;a.oblateness=0;DrawList pluto;draw_world(pluto,{0,0,1280,700},a,{},light,"Replacement Pluto · canonical 3D material");window.draw(pluto,folder/"pluto-close.png");}
 auto world=seed_persistable_fresh_campaign(42,load_nearby_catalog(root/"data/astronomy/hyg-nearby-500-v1.json"),{"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},true});
 CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root/"data/research/v1"),std::move(world)),{},CampaignFramePolicy::Developer);
 NativeGiantTestPanel panel;panel.open(frame);
 const MaterialProvider materials=[&](const auto& a,int lod){return std::make_shared<const MaterialSet>(load_material(root/"assets/visual",a,lod));};
 const auto render=[&](){DrawList draw;panel.render(draw,1280,720,frame,materials);return draw;};
 auto panel_draw=render();window.draw(panel_draw,folder/"giant-test-panel.png");
 const auto press=[&](std::string_view label){auto draw=render();for(const auto& cmd:draw.overlay)if(const auto* t=std::get_if<Text>(&cmd);t&&t->value==label&&t->clip){Point p{t->clip->x+t->clip->width/2,t->clip->y+t->clip->height/2};panel.handle({InputEventType::LeftPressed,p},1280,720,frame);panel.handle({InputEventType::LeftReleased,p},1280,720,frame);return;}throw std::runtime_error("Missing panel control "+std::string(label));};
 for(auto action:{"ICE GIANTS","NEXT SUBCLASS","NEXT PLANET IMAGE","NEXT RING IMAGE","TILT +15°","CAMERA LEFT","CAMERA HIGHER","STAR DIRECTION +45°","STAR SPECTRUM","MOVE FARTHER","RING SHADOW: ON","PLANET SHADOW: ON"})press(action);
 window.draw(render(),folder/"giant-test-panel-adjusted.png");
 const auto& live=frame.runtime().world().campaign();const auto lab=build_developer_giant_test(live);const auto& planet=lab.body;
 check(live.systems.size()==250,"QA changed generated system count");
 check(planet.appearance->primary_class==PlanetClass::IceGiant&&planet.appearance->subclass=="deep-azure","Panel failed to change authoritative subclass");
 const auto saved=capture_developer_campaign_json(frame.runtime(),{0,"giant-test","2050-03-21T00:00:00Z"});
 auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root/"data/research/v1"),saved);
 check(build_developer_giant_test(restored.galaxy()).body.appearance==planet.appearance,"Full campaign round-trip changed QA appearance");
 // Synthetic ring extraction catches background leakage, missing gaps and aliasing.
 constexpr int n=512;std::vector<std::uint8_t> pixels(n*n*4,0);for(int y=0;y<n;++y)for(int x=0;x<n;++x){double r=std::hypot((x-256)/210.,(y-256)/130.);auto i=(y*n+x)*4;pixels[i+3]=255;if(r>.58&&r<1&&(r<.76||r>.82)){pixels[i]=190;pixels[i+1]=160;pixels[i+2]=120;}}
 auto ring=prepare_ring_material(*RgbaImage::create(n,n,std::move(pixels)));check(ring.usable&&ring.material->height()==2048&&ring.gap_fraction>.015,"Elliptical ring extraction lost its transparent radial gap");
 check(!prepare_ring_material(*RgbaImage::create(n,n,std::vector<std::uint8_t>(n*n*4,0))).usable,"Blank ring was accepted");
 std::cout<<"Nine giant subclasses, ten ring families, Pluto and live panel captured; full campaign save and ring extraction passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
