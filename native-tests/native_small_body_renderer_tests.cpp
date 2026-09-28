#include "native_small_body_renderer.hpp"
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/engine/native_triangle_mesh.hpp>
#include <stellar/engine/texture_decal.hpp>
#include <iostream>
#include <set>
using namespace stellar::native_map;using namespace stellar::native_system;using namespace stellar::native_system_ui;using namespace stellar::core;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
  // Edge-connected black is transparent; an enclosed black shadow stays opaque.
  std::vector<std::uint8_t> pixels(9*9*4,0);for(int y=0;y<9;++y)for(int x=0;x<9;++x){const int at=(y*9+x)*4;pixels[at+3]=255;if(x>1&&x<7&&y>1&&y<7)pixels[at]=pixels[at+1]=pixels[at+2]=150;}
  pixels[(4*9+4)*4]=pixels[(4*9+4)*4+1]=pixels[(4*9+4)*4+2]=0;
  auto cutout=prepare_decal_texture(*RgbaImage::create(9,9,pixels),9,DecalBlendProfile::OpaqueCutout);
  check(cutout->pixels()[3]==0&&cutout->pixels()[(4*9+4)*4+3]==255,"Cutout removed shadow or retained background");
  StellarSystem sol;sol.id=0;sol.catalog_preset_id=std::string(sol_catalog_preset_id);auto planets=create_sol_catalog(sol);
  NativeSystemSnapshot s;s.campaign_generation=1;s.system_id=0;s.survey_level=SystemSurveyLevel::fully_surveyed;s.small_body_fields=generate_small_body_fields(75,sol,planets);
  for(const auto& p:planets)s.bodies.push_back({.id=p.id,.parent_body_id=p.parent_body_id,.orbit_index=p.orbit_index,.name=p.name,.kind=p.kind,.radius_earth=p.radius_earth,.orbital_eccentricity=p.orbital_eccentricity,.orbital_inclination_degrees=p.orbital_inclination_degrees,.orbit_au=planetary_orbit_au(sol,p)});
  const auto spatial=project_system(s);float previous=0;double au=0;
  for(const auto& anchor:spatial.orbit_anchors){check(anchor.first>au&&anchor.second>previous,"Orbit transform reversed order");au=anchor.first;previous=anchor.second;}
  check(system_display_orbit_radius(spatial,2.1)>system_display_orbit_radius(spatial,1.524)&&system_display_orbit_radius(spatial,3.3)<system_display_orbit_radius(spatial,5.203),"Belt presentation outside Mars/Jupiter");
  for(const auto& planet:planets)if(!planet.parent_body_id){
    for(const auto& f:s.small_body_fields)if(!f.planet_centered){
      const double a=planetary_orbit_au(sol,planet),e=planet.orbital_eccentricity;
      for(float anomaly:{0.f,1.f,3.14159265f,4.f}){
        const auto point=projected_orbit_point(spatial,a,static_cast<float>(e),static_cast<float>(planet.orbital_inclination_degrees),anomaly);
        const float radius=std::hypot(point.x,point.y),extent=body_display_radius(planet.radius_earth,planet.kind)+78;
        if(a*(1+e)<f.inner_radius_au)check(radius+extent<system_display_orbit_radius(spatial,f.inner_radius_au),"Belt intersects displayed planet despite a physical gap");
        if(a*(1-e)>f.outer_radius_au)check(radius-extent>system_display_orbit_radius(spatial,f.outer_radius_au),"Outer planet overlaps displayed belt");
      }
    }
  }
  check(body_display_radius(11.2,PlanetaryBodyKind::Planet)>body_display_radius(1,PlanetaryBodyKind::Planet)&&body_display_radius(1,PlanetaryBodyKind::Planet)>14,"Enlarged planet ordering");
  NativeSmallBodyRenderer renderer;std::set<std::pair<int,int>> assets;
  const auto texture=RgbaImage::create(1,1,{255,255,255,255});renderer.set_images([&](SmallBodyAssetPool pool,int variant){assets.emplace(static_cast<int>(pool),variant);return texture;});
  for(float scale:{.025f,.10f,.17f}){
    DrawList overview;renderer.render(overview,s,spatial,{960,540,scale},{0,0,1920,1080},0,false);
    for(const auto& command:overview.world)if(const auto* m=std::get_if<TriangleMesh>(&command))
      check(!m->texture,"Rocky belt still contains flat photographed asteroids");
  }
  check(assets.empty(),"Overview still requests flat belt pictures");
  auto view=SystemSpatialViewport::fit(spatial,1920,1080);DrawList far;renderer.render(far,s,spatial,view,{0,0,1920,1080},0,true);
  check(renderer.statistics().fields==2&&renderer.statistics().visible>0&&renderer.statistics().solid_bodies<=768,"Far LOD draw budget");
  for(const auto& command:far.world)if(const auto* m=std::get_if<TriangleMesh>(&command))validate_triangle_mesh(*m);
  const auto& field=s.small_body_fields.front();const auto instance=small_body_instance(field,0);SystemSpatialViewport unit{0,0,1};
  const auto p=NativeSmallBodyRenderer::position(field,instance,spatial,unit,0);view={960-p.x*5,540-p.y*5,5};
  DrawList close;renderer.render(close,s,spatial,view,{0,0,1920,1080},0,false);check(renderer.hit({960,540}).has_value(),"Close body cannot be selected");
  const auto scene_of=[](const DrawList& draw){for(const auto& c:draw.world)if(const auto* scene=std::get_if<Scene3DView>(&c))return scene->scene;return std::shared_ptr<const Scene3D>{};};
  const auto scene=scene_of(close);check(scene&&!scene->instances().empty(),"Close bodies still use flat images");
  const auto& solid=scene->instances().front();float min_z=1e9f,max_z=-1e9f;bool non_radial=false;
  for(const auto& v:solid.mesh->vertices()){min_z=std::min(min_z,v.position.z);max_z=std::max(max_z,v.position.z);
    non_radial|=std::abs(v.normal.x*v.position.y-v.normal.y*v.position.x)>.02f;}
  check(max_z-min_z>.4f&&non_radial&&solid.material.diffuse>.7f,"Solid has no mass or surface lighting");
  DrawList paused;renderer.render(paused,s,spatial,view,{0,0,1920,1080},0,false);
  const auto repeat=scene_of(paused);check(repeat->instances().front().mesh==solid.mesh&&repeat->instances().front().material.texture==solid.material.texture,"Paused rendering regenerated immutable geometry");
  check(repeat->instances().front().rotation.w==solid.rotation.w,"Paused spin drifted");
  // Keep a single identified body in view so changed orientation cannot be
  // confused with a different selected instance after sorting by size.
  auto one=s;one.small_body_fields.resize(1);one.small_body_fields[0].visible_count=1;
  DrawList before,after;renderer.render(before,one,spatial,view,{0,0,1920,1080},0,false);
  const auto moved=NativeSmallBodyRenderer::position(field,instance,spatial,unit,1);
  for(int frame=0;frame<240;++frame)renderer.advance_tumble(1./60,true);
  renderer.render(after,one,spatial,{960-moved.x*5,540-moved.y*5,5},{0,0,1920,1080},1,false);
  const auto a=scene_of(before)->instances().front().rotation,b=scene_of(after)->instances().front().rotation;
  check(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)+std::abs(a.w-b.w)>.01f,"Full 3D spin did not turn the solid");
  const auto far_future=NativeSmallBodyRenderer::position(field,instance,spatial,unit,100000);
  DrawList faster;renderer.render(faster,one,spatial,{960-far_future.x*5,540-far_future.y*5,5},{0,0,1920,1080},100000,false);
  const auto fast_rotation=scene_of(faster)->instances().front().rotation;
  check(fast_rotation.x==b.x&&fast_rotation.y==b.y&&fast_rotation.z==b.z&&fast_rotation.w==b.w,"Strategic time accelerated cosmetic tumble");
  renderer.advance_tumble(30,false);DrawList stopped;renderer.render(stopped,one,spatial,{960-far_future.x*5,540-far_future.y*5,5},{0,0,1920,1080},100000,false);
  check(scene_of(stopped)->instances().front().rotation.w==b.w,"Paused cosmetic tumble advanced");
  const double dot=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
  check(dot>.97,"Normal-speed tumble is too fast for four seconds");
  int small=0,large=0,huge=0;float smallest=100,largest=0;
  for(std::uint32_t i=0;i<2000;++i){const auto body=small_body_instance(field,i);const auto r=small_body_display_radius(body);small+=r<4;large+=r>=13;huge+=r>=36;smallest=std::min(smallest,r);largest=std::max(largest,r);}
  check(small>1000&&large>80&&huge>10&&largest/smallest>40,"Field lacks a broad size distribution with rare huge bodies");
  auto ice=s;ice.small_body_fields={s.small_body_fields.back()};const auto ib=small_body_instance(ice.small_body_fields[0],0);
  const auto ip=NativeSmallBodyRenderer::position(ice.small_body_fields[0],ib,spatial,unit,0);DrawList icy;
  renderer.render(icy,ice,spatial,{960-ip.x*5,540-ip.y*5,5},{0,0,1920,1080},0,false);
  check(scene_of(icy)!=nullptr,"Ice is not solid geometry");
  const double ice_period=2*std::numbers::pi/std::abs(ib.orbit.angular_speed);
  const auto iq=NativeSmallBodyRenderer::position(ice.small_body_fields[0],ib,spatial,unit,ice_period*.25);
  check(std::hypot(iq.x-ip.x,iq.y-ip.y)>system_display_orbit_radius(spatial,ice.small_body_fields[0].inner_radius_au),"Ice failed to advance along its orbit");
  check(ice_period>365.25*300,"Outer ice field orbit is implausibly fast");
  const auto one_day=NativeSmallBodyRenderer::position(ice.small_body_fields[0],ib,spatial,unit,1);
  check(std::hypot(one_day.x-ip.x,one_day.y-ip.y)>0,"Ice position is frozen while time advances");
  for(const auto& c:icy.world)if(const auto* mesh=std::get_if<TriangleMesh>(&c))check(!mesh->texture,"Ice field still contains a stretched dust/cluster image");
  // Albedo wraps continuously and retains dark inclusions without a black
  // rectangular backdrop or baked directional illumination from source photos.
  NativeSmallBodyGeometry geometry;const auto tex=geometry.texture(ib);
  int elongated=0;
  for(std::uint32_t i=0;i<48;++i){auto sample=instance;sample.id=i;const auto mesh=geometry.mesh(sample,true);
    Vec3 low{9,9,9},high{-9,-9,-9};for(const auto& v:mesh->vertices()){
      low.x=std::min(low.x,v.position.x);low.y=std::min(low.y,v.position.y);low.z=std::min(low.z,v.position.z);
      high.x=std::max(high.x,v.position.x);high.y=std::max(high.y,v.position.y);high.z=std::max(high.z,v.position.z);}
    const auto extent=std::array{high.x-low.x,high.y-low.y,high.z-low.z};
    elongated+=*std::max_element(extent.begin(),extent.end())/ *std::min_element(extent.begin(),extent.end())>2.3f;
  }check(elongated>=8&&elongated<30,"Shape library does not mix long asteroids with rounded bodies");
  for(const auto& body:scene_of(icy)->instances())if(body.material.dielectric){
    check(body.material.dielectric->environment&&body.material.dielectric->surface,"Icy body lacks real optical maps");}
  auto frozen=ib;frozen.material=SmallBodyMaterial::WaterIce;const auto optical=geometry.optics(frozen);
  check(optical&&optical->transmission>0&&optical->specular_strength>0,"Ice reflections/refraction are disabled");
  frozen.material=SmallBodyMaterial::Rock;check(!geometry.optics(frozen),"Rock was turned into glass");
  const auto& mask=optical->surface;int least=255,most=0;
  for(std::size_t i=1;i<mask->pixels().size();i+=4){least=std::min(least,int(mask->pixels()[i]));most=std::max(most,int(mask->pixels()[i]));}
  check(most-least>100,"Frost and clear ice have no material variation");
  // Tilted ice centers must stay within the same projected radial band as its
  // guides, at multiple phases, including the eccentric inner/outer edges.
  auto tilted=ice.small_body_fields[0];tilted.tilt=.38;
  for(int i=0;i<160;++i)for(double day:{0.,800.,24000.}){
    const auto sample=small_body_instance(tilted,i);const auto pos=NativeSmallBodyRenderer::position(tilted,sample,spatial,unit,day);
    const auto radius=std::hypot(pos.x,pos.y);
    check(radius>=system_display_orbit_radius(spatial,tilted.inner_radius_au)-.01f&&radius<=system_display_orbit_radius(spatial,tilted.outer_radius_au)+.01f,"Tilted ice moved beyond its chart orbital band");
  }
  for(int y=0;y<tex->height();++y)for(int c=0;c<3;++c)
    check(std::abs(int(tex->pixels()[(y*tex->width())*4+c])-int(tex->pixels()[(y*tex->width()+tex->width()-1)*4+c]))<=1,"Solid material has a visible longitude seam");
  const auto q=NativeSmallBodyRenderer::position(field,instance,spatial,view,1);check(q.x!=960||q.y!=540,"Rendered orbit stationary");
  check(NativeSmallBodyRenderer::position(field,instance,spatial,view,1).x==q.x,"Paused rendered orbit drifted");
  const auto old_stats=renderer.statistics();renderer.render(close,s,spatial,{1e6,1e6,5},{0,0,1920,1080},0,false);check(renderer.statistics().visible==0&&old_stats.visible>0,"Viewport culling failed");
  s.survey_level=SystemSurveyLevel::partially_surveyed;DrawList hidden;renderer.render(hidden,s,spatial,view,{0,0,1920,1080},0,false);check(hidden.world.empty()&&renderer.statistics().fields==0,"Survey gate leaked belts");
  std::cout<<"small-body renderer: solid volume, lit normals, full-axis spin, immutable caches, small/large/huge distribution, seamless ice without haze, picking, culling and survey gate passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
