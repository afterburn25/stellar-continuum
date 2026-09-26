#include <stellar/engine/native_scene3d.hpp>
#include <stellar/engine/native_solid_mesh.hpp>
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/surface_attachment.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace stellar::native_map;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}catch(const std::length_error&){rejected=true;}check(rejected,"Malformed 3D input was accepted");}
bool close(float a,float b,float epsilon=1e-5f){return std::abs(a-b)<epsilon;}
int main()try{
  const auto slab=annulus_mesh(1.2f,2.5f,64,.0002f);
  for(const auto& v:slab->vertices())check(close(std::abs(v.position.y),.0001f),"Ring lost its physical thickness");
  for(std::size_t i=0;i<slab->indices().size();i+=3){const auto& a=slab->vertices()[slab->indices()[i]];const auto b=slab->vertices()[slab->indices()[i+1]].position,c=slab->vertices()[slab->indices()[i+2]].position;
    const Vec3 u{b.x-a.position.x,b.y-a.position.y,b.z-a.position.z},v{c.x-a.position.x,c.y-a.position.y,c.z-a.position.z};
    check((u.y*v.z-u.z*v.y)*a.normal.x+(u.z*v.x-u.x*v.z)*a.normal.y+(u.x*v.y-u.y*v.x)*a.normal.z>0,"Ring thickness has inward faces");}
  rejects([]{(void)annulus_mesh(1,2,64,-.1f);});
  const auto solid=directional_solid_mesh([](Vec3 p){return Vec3{p.x*2,p.y*.6f,p.z};},32,16);
  for(const auto& v:solid->vertices()){
    const Vec3 expected{v.position.x/4,v.position.y/.36f,v.position.z};const float norm=std::hypot(expected.x,expected.y,expected.z);
    check(v.normal.x*expected.x/norm+v.normal.y*expected.y/norm+v.normal.z*expected.z/norm>.999f,"Deformed solid normal ignores its volume");
  }
  check(solid->bounding_radius()>1.99f,"Solid discarded its long axis");
  rejects([]{(void)directional_solid_mesh({});});
  rejects([]{(void)directional_solid_mesh([](Vec3){return Vec3{};});});
  const auto volume=surface_emission_volume(.3f);
  check(volume->vertices().size()==24&&volume->indices().size()==36,"Emission volume is not a closed six-face solid");
  for(std::size_t n=0;n<volume->indices().size();n+=3){const auto& a=volume->vertices()[volume->indices()[n]];const auto b=volume->vertices()[volume->indices()[n+1]].position,c=volume->vertices()[volume->indices()[n+2]].position;
    const Vec3 u{b.x-a.position.x,b.y-a.position.y,b.z-a.position.z},v{c.x-a.position.x,c.y-a.position.y,c.z-a.position.z};
    check((u.y*v.z-u.z*v.y)*a.normal.x+(u.z*v.x-u.x*v.z)*a.normal.y+(u.x*v.y-u.y*v.x)*a.normal.z>0,"Volume has inward winding");}
  rejects([]{(void)surface_emission_volume(0);});rejects([]{(void)surface_emission_volume(.8f);});
  const auto sphere=Mesh3D::uv_sphere(32,16);
  check(sphere->vertices().size()==561&&sphere->indices().size()==2880,"Sphere topology or pole triangles invalid");
  for(const auto& vertex:sphere->vertices())check(close(std::hypot(vertex.normal.x,vertex.normal.y,vertex.normal.z),1),"Sphere normals not normalized");
  for(std::size_t n=0;n<sphere->indices().size();n+=3){
    const auto a=sphere->vertices()[sphere->indices()[n]].position,b=sphere->vertices()[sphere->indices()[n+1]].position,c=sphere->vertices()[sphere->indices()[n+2]].position;
    const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
    check((u.y*v.z-u.z*v.y)*a.x+(u.z*v.x-u.x*v.z)*a.y+(u.x*v.y-u.y*v.x)*a.z>0,"Sphere winding faces inward");
  }
  Camera3D camera;MeshInstance3D instance;instance.mesh=sphere;
  const auto p=prepare_instance3d(camera,instance,1);
  check(p.visible,"Visible sphere was culled");
  const auto point=transform(p.model_view_projection,{1,0,0,1});
  check(close(point[0]/point[3],std::sqrt(3.f)/3)&&close(point[1],0),"Perspective projection incorrect");
  const auto near=transform(p.model_view_projection,{0,0,3-camera.near_plane,1}),far=transform(p.model_view_projection,{0,0,3-camera.far_plane,1});
  check(close(near[2]/near[3],0,3e-5f)&&close(far[2]/far[3],1),"Depth range is not zero to one");
  camera.position={1e12,1e12,1e12+3};instance.position={1e12+.25,1e12,1e12};
  const auto large=prepare_instance3d(camera,instance,1);check(large.visible&&close(large.model_view.values[12],.25f)&&close(large.model_view.values[14],-3),"Camera-relative double precision lost local position");
  instance.position.x=0;check(!prepare_instance3d(camera,instance,1).visible,"Astronomically distant mesh was not culled");
  camera={};instance.position={};instance.rotation=rotation_axis_angle({0,1,0},std::numbers::pi_v<float>*.5f);
  const auto rotated=prepare_instance3d(camera,instance,1);
  const auto origin=transform(rotated.model_view,{0,0,0,1}),tip=transform(rotated.model_view,{0,0,1,1});
  check(close(tip[0]-origin[0],1)&&close(tip[2]-origin[2],0),"Quaternion model rotation incorrect");
  camera.orientation=rotation_axis_angle({0,1,0},std::numbers::pi_v<float>);check(!prepare_instance3d(camera,instance,1).visible,"Camera orientation did not change viewing direction");
  camera={};camera.projection=Projection3D::Orthographic;camera.orthographic_height=2000;instance.position={500,0,0};
  check(prepare_instance3d(camera,instance,1).visible,"Wide orthographic frustum falsely culled visible geometry");
  camera={};instance.position={1000,0,0};check(!prepare_instance3d(camera,instance,1).visible,"Offscreen sphere was not culled");
  camera.vertical_fov_radians=3;camera.far_plane=1000;instance.position={9000,0,-900};check(prepare_instance3d(camera,instance,1).visible,"Wide perspective frustum falsely culled visible geometry");
  camera={};instance.position={};
  rejects([&]{auto c=camera;c.far_plane=c.near_plane;(void)Scene3D::create(c,{instance});});
  rejects([&]{auto c=camera;c.position.x=std::numeric_limits<double>::infinity();(void)Scene3D::create(c,{instance});});
  rejects([&]{auto c=camera;c.near_plane=std::numeric_limits<float>::denorm_min();c.far_plane=c.near_plane*2;(void)Scene3D::create(c,{instance});});
  rejects([&]{auto c=camera;c.orthographic_height=std::numeric_limits<float>::denorm_min();(void)Scene3D::create(c,{instance});});
  rejects([&]{auto i=instance;i.scale=0;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.scale=std::numeric_limits<float>::denorm_min();(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.rotation={0,0,0,0};(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.opacity=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.light_direction=Vec3{};(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.light_direction=Vec3{0,0,12};const auto s=Scene3D::create(camera,{i});check(close(s->instances()[0].material.light_direction->z,1),"Per-object light was not normalized");}
  rejects([&]{(void)Scene3D::create(camera,std::vector<MeshInstance3D>(maximum_scene3d_instances+1,instance));});
  rejects([&]{(void)prepare_instance3d(camera,instance,0);});
  rejects([]{(void)Mesh3D::uv_sphere(100000,100000);});
  rejects([]{(void)Mesh3D::create({},{});});
  auto vertices=sphere->vertices();auto indices=sphere->indices();indices[0]=999999;
  rejects([&]{(void)Mesh3D::create(vertices,indices);});indices=sphere->indices();vertices[0].normal={};
  rejects([&]{(void)Mesh3D::create(vertices,indices);});vertices=sphere->vertices();vertices[0].position.x=std::numeric_limits<float>::infinity();
  rejects([&]{(void)Mesh3D::create(vertices,indices);});
  auto scene=Scene3D::create(camera,{instance});check(scene->instances()[0].mesh==sphere,"Scene copied immutable mesh storage");
  std::vector<MeshInstance3D> resources;
  for(std::size_t n=0;n<maximum_scene3d_resource_entries;++n){auto i=instance;i.mesh=Mesh3D::uv_sphere(3,2);resources.push_back(i);}
  (void)Scene3D::create(camera,resources);
  resources.push_back(instance);rejects([&]{(void)Scene3D::create(camera,resources);});
  // Blocker-relative doubles must survive a world translation and camera pose.
  auto receiver=instance;receiver.rotation={};receiver.position={1e12+.25,1e12,1e12};receiver.scale=.5f;
  AnalyticShadow3D shadow;shadow.position={1e12,1e12,1e12};shadow.scale=.25f;
  shadow.rotation=rotation_axis_angle({0,0,1},std::numbers::pi_v<float>*.5f);receiver.material.shadow=shadow;
  camera.orientation=shadow.rotation;
  const auto relative=prepare_shadow3d(camera,receiver,{1,0,0});
  const auto shadow_point=transform(relative.from_model,{0,1,0,1});
  check(close(shadow_point[0],2)&&close(shadow_point[1],-1)&&close(shadow_point[2],0),"Shadow lost blocker rotation, scale or double precision");
  check(close(relative.light.x,1)&&close(relative.light.y,0),"Shadow light did not transform from camera through world to blocker");
  // New material blocks reject malformed input at scene validation.
  rejects([&]{auto i=instance;i.material.alpha_threshold=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.alpha_threshold=-.1f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.texture_tiling={0.f,1.f};(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.texture_tiling={1.f,100.f};(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.pbr=PbrSurface3D{};i.material.pbr->metallic=2.f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.pbr=PbrSurface3D{};i.material.pbr->roughness=0.f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.pbr=PbrSurface3D{};i.material.pbr->night_emissive=2.f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.atmosphere=Atmosphere3D{};i.material.atmosphere->power=.1f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.atmosphere=Atmosphere3D{};i.material.atmosphere->strength=-1.f;(void)Scene3D::create(camera,{i});});
  // Surface response accepts any subset of maps — a cloud-only material is
  // legal — but still requires at least one and bounds every scalar.
  {auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});
   const auto clouded=Scene3D::create(camera,{i});check(clouded->instances()[0].material.surface_response.has_value(),"Cloud-only surface response was rejected");}
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->normal=RgbaImage::create(1,1,{128,128,255,255});i.material.surface_response->normal_strength=2.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});i.material.surface_response->cloud_opacity=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});i.material.surface_response->cloud_albedo=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});i.material.surface_response->cloud_height=.2f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});i.material.surface_response->cloud_height=-.01f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.surface_response=SurfaceResponse3D{};i.material.surface_response->properties=RgbaImage::create(1,1,{255,0,0,128});i.material.surface_response->cloud_offset={2.5f,0};(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.terminator_wrap=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.terminator_wrap=-.1f;(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.terminator_wrap=.6f;const auto wrapped=Scene3D::create(camera,{i});
   check(close(wrapped->instances()[0].material.terminator_wrap,.6f),"Terminator wrap did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.limb_darkening=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.limb_darkening=-.1f;(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.limb_darkening=.6f;const auto darkened=Scene3D::create(camera,{i});
   check(close(darkened->instances()[0].material.limb_darkening,.6f),"Limb darkening did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.limb_darkening_q=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.limb_darkening_q=-.1f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.limb_darkening_q=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.limb_darkening_q=.4f;const auto quad=Scene3D::create(camera,{i});
   check(close(quad->instances()[0].material.limb_darkening_q,.4f),"Quadratic limb coefficient did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.band_shear=.6f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_shear=-.6f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_shear=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.band_shear=-.25f;const auto sheared=Scene3D::create(camera,{i});
   check(close(sheared->instances()[0].material.band_shear,-.25f),"Band shear did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.band_waves=1.2f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_waves=-.1f;(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.band_shear=.2f;i.material.band_waves=.8f;const auto waved=Scene3D::create(camera,{i});
   check(close(waved->instances()[0].material.band_waves,.8f),"Band waves did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.band_drift=.3f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_drift=-.3f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_drift=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.band_drift=.1f;const auto drifted=Scene3D::create(camera,{i});
   check(close(drifted->instances()[0].material.band_drift,.1f),"Band drift did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.band_turbulence=9.f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_turbulence=-9.f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.band_turbulence=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.band_turbulence=1.5f;const auto turbed=Scene3D::create(camera,{i});
   check(close(turbed->instances()[0].material.band_turbulence,1.5f),"Band turbulence did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.orbital_beaming=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.orbital_beaming=-1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.orbital_beaming=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.orbital_beaming=.8f;const auto beamed=Scene3D::create(camera,{i});
   check(close(beamed->instances()[0].material.orbital_beaming,.8f),"Orbital beaming did not survive scene creation");}
  rejects([&]{auto i=instance;i.material.forward_scatter=1.5f;(void)Scene3D::create(camera,{i});});
  rejects([&]{auto i=instance;i.material.forward_scatter=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
  {auto i=instance;i.material.forward_scatter=-.6f;const auto phased=Scene3D::create(camera,{i});
   check(close(phased->instances()[0].material.forward_scatter,-.6f),"Forward scatter did not survive scene creation");}
  {const auto tex=RgbaImage::create(1,1,{255,255,255,255});
   MeshInstance3D plasma;plasma.mesh=volume;plasma.material.transparent=true;plasma.material.texture=tex;
   SurfaceEffect3D effect;effect.next_texture=tex;effect.volume_depth=.3f;
   plasma.material.surface_effect=effect;
   rejects([&]{auto i=plasma;i.material.surface_effect->volume_scatter=-.1f;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=plasma;i.material.surface_effect->volume_scatter=1.1f;(void)Scene3D::create(camera,{i});});
   plasma.material.surface_effect->volume_scatter=.8f;
   rejects([&]{auto i=plasma;i.material.surface_effect->flow_rate=70.f;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=plasma;i.material.surface_effect->flow_rate=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
   plasma.material.surface_effect->flow_rate=.5f;
   check(Scene3D::create(camera,{plasma})!=nullptr,"Legal volume scatter rejected");}
  rejects([&]{(void)star_photosphere3d(50);});
  rejects([&]{(void)star_photosphere3d(2e5);});
  rejects([&]{(void)star_photosphere3d(std::numeric_limits<double>::quiet_NaN());});
  {const auto sun=star_photosphere3d(5778);
   check(sun.ambient==1.f&&sun.diffuse==0.f&&sun.linear_light,"Star preset is not emissive-dominant");
   check(sun.limb_darkening>.6f&&sun.limb_darkening<.75f,"Solar limb coefficient off the observed envelope");
   check(sun.tint.r>=sun.tint.b,"Solar tint should not be blue");
   const auto dwarf=star_photosphere3d(3200);
   check(dwarf.tint.r>dwarf.tint.b&&dwarf.limb_darkening>sun.limb_darkening,"Cool star lost its red tint or stronger limb darkening");
   const auto ostar=star_photosphere3d(30000);
   check(ostar.tint.b>ostar.tint.r&&ostar.limb_darkening<sun.limb_darkening,"Hot star lost its blue tint or weaker limb darkening");}
  rejects([&]{(void)accretion_disc_material3d(0,1,8000);});
  rejects([&]{(void)accretion_disc_material3d(2,1,8000);});
  rejects([&]{(void)accretion_disc_material3d(0.5f,1,50);});
  rejects([&]{(void)accretion_disc_material3d(0.5f,1,8000,2.f);});
  {const auto disc=accretion_disc_material3d(0.5f,1.f,8000);
   check(disc.texture&&disc.texture->width()==256&&disc.texture->height()==1,
       "Accretion disc did not generate its radial texture");
   check(disc.ambient==1.f&&disc.diffuse==0.f&&disc.linear_light,
       "Accretion disc is not emissive-dominant");
   check(disc.double_sided&&disc.orbital_beaming>.5f&&disc.anisotropic_texture,
       "Accretion disc lost its sheet/beaming/minification settings");
   const auto &px=disc.texture->pixels();
   const auto lum=[&](int u){return px[u*4]*3+px[u*4+1]*4+px[u*4+2];};
   check(lum(4)>lum(250)*2,"Shakura-Sunyaev profile lost its inner-edge luminance");
   check(px[250*4+2]<px[4*4+2]&&px[250*4+0]>px[250*4+2],
       "Accretion outer rim did not cool redward of the inner edge");
   const auto repeat=accretion_disc_material3d(0.5f,1.f,8000);
   check(repeat.texture->pixels()==disc.texture->pixels(),"Accretion texture is not deterministic");}
  {PointLight3D light;light.position={0,0,1};light.intensity=2;light.range=50;
   const auto lit=Scene3D::create(camera,{instance},{0,0,1},{light});
   check(lit->point_lights().size()==1,"Scene dropped its point light");}
  rejects([&]{std::vector<PointLight3D> too_many(maximum_scene3d_point_lights+1);(void)Scene3D::create(camera,{instance},{0,0,1},too_many);});
  rejects([&]{PointLight3D l;l.position={std::numeric_limits<double>::infinity(),0,0};(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  rejects([&]{PointLight3D l;l.intensity=-1;(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  {PointLight3D l;l.position={0,0,1};l.spot_direction={0,0,-1};l.spot_inner=.97f;l.spot_outer=.9f;
   const auto spotted=Scene3D::create(camera,{instance},{0,0,1},{l});
   check(spotted->point_lights().size()==1&&close(spotted->point_lights()[0].spot_inner,.97f),
       "Scene dropped a valid spot light");}
  rejects([&]{PointLight3D l;l.spot_direction={0,0,-1};l.spot_inner=.9f;l.spot_outer=.95f;(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  rejects([&]{PointLight3D l;l.spot_direction={0,0,-1};l.spot_inner=1.f;l.spot_outer=1.f;(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  rejects([&]{PointLight3D l;l.spot_direction={0,0,-1};l.spot_outer=-.1f;l.spot_inner=.5f;(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  rejects([&]{PointLight3D l;l.spot_inner=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{instance},{0,0,1},{l});});
  // Directional shadow map settings validate bounds; a valid map round-trips.
  {ShadowMap3D config;config.extent=4;config.distance=2;config.depth=8;config.resolution=512;
   const auto mapped=Scene3D::create(camera,{instance},{0,0,1},{},config);
   check(mapped->shadow_map()&&mapped->shadow_map()->extent==4&&mapped->shadow_map()->resolution==512,"Scene dropped its shadow map settings");}
  rejects([&]{ShadowMap3D s;s.extent=0;(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  rejects([&]{ShadowMap3D s;s.depth=-1;(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  rejects([&]{ShadowMap3D s;s.strength=1.5f;(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  rejects([&]{ShadowMap3D s;s.bias=-.001f;(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  rejects([&]{ShadowMap3D s;s.distance=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  rejects([&]{ShadowMap3D s;s.resolution=32;(void)Scene3D::create(camera,{instance},{0,0,1},{},s);});
  // Distance culling: visible_range bounds the camera-to-surface distance;
  // 0 leaves the instance visible at any range.
  {auto ranged=instance;ranged.position={};ranged.visible_range=4;
   check(prepare_instance3d(camera,ranged,1).visible,"Instance inside its visible range was culled");
   ranged.visible_range=1.5f;check(!prepare_instance3d(camera,ranged,1).visible,"Instance beyond its visible range stayed visible");
   ranged.visible_range=0;check(prepare_instance3d(camera,ranged,1).visible,"Zero visible range culled the instance");
   rejects([&]{auto i=instance;i.visible_range=-1;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.visible_range=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.visible_fade=.6f;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.visible_fade=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});}
  // Screen-space LOD: the level is a pure function of projected diameter
  // — each chain step halves the switch threshold.
  {auto loded=instance;loded.lod_meshes={sphere,sphere};
   check(select_lod3d_level(loded,64)==0,"Full-size instance picked a LOD mesh");
   check(select_lod3d_level(loded,31)==1,"First LOD level did not engage at the switch size");
   check(select_lod3d_level(loded,15)==2,"Second LOD level did not halve the switch size");
   check(select_lod3d_level(loded,1)==2,"LOD selection ran past the end of the chain");
   const auto flat=instance;check(select_lod3d_level(flat,1)==0,"Empty LOD chain picked a level");
   rejects([&]{auto i=instance;i.lod_meshes.assign(9,sphere);(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_meshes={nullptr};(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_meshes={sphere};i.lod_pixels=0;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_meshes={sphere};i.lod_pixels=8192;(void)Scene3D::create(camera,{i});});
   const auto lscene=Scene3D::create(camera,{loded});
   check(lscene->instances()[0].lod_meshes.size()==2,"Scene dropped its LOD chain");}
  // Screen-door LOD fade: the coarser level's share ramps 0->1 across the
  // band immediately above each switch threshold; 0 disables it outright.
  {auto fading=instance;fading.lod_meshes={sphere,sphere};fading.lod_pixels=100;fading.lod_fade=.2f;
   check(lod3d_fade_share(fading,140)==0.f,"LOD fade leaked above the transition band");
   check(std::abs(lod3d_fade_share(fading,110)-.5f)<1e-6f,"LOD fade midpoint share was not .5");
   check(lod3d_fade_share(fading,100)==1.f,"LOD fade did not reach the full coarse share at the switch");
   check(lod3d_fade_share(fading,90)==0.f,"LOD fade engaged below the switch threshold");
   check(std::abs(lod3d_fade_share(fading,55)-.5f)<1e-6f,"LOD fade did not scale to the second-level band");
   auto off=fading;off.lod_fade=0;
   check(lod3d_fade_share(off,110)==0.f,"Zero fade width still produced a share");
   rejects([&]{auto i=instance;i.lod_fade=.6f;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_fade=std::numeric_limits<float>::quiet_NaN();(void)Scene3D::create(camera,{i});});}
  // Group proxy LOD: a grouped instance needs a resolved proxy mesh and
  // a bounded collapse size; ungrouped instances keep the feature off.
  {auto grouped=instance;grouped.lod_group="fleet";grouped.lod_group_proxy=sphere;grouped.lod_group_pixels=24;
   check(Scene3D::create(camera,{grouped})->instances()[0].lod_group=="fleet","Scene dropped the LOD group");
   rejects([&]{auto i=instance;i.lod_group="fleet";(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_group="fleet";i.lod_group_proxy=sphere;i.lod_group_pixels=0;(void)Scene3D::create(camera,{i});});
   rejects([&]{auto i=instance;i.lod_group="fleet";i.lod_group_proxy=sphere;i.lod_group_pixels=8192;(void)Scene3D::create(camera,{i});});}
  // Billboard cards: a camera-facing quad for LOD impostors and sprite
  // markers — the renderer drops its view-space rotation at draw time.
  {const auto card=Mesh3D::billboard_card(2.f,1.f);
   check(card->billboard()&&!sphere->billboard(),"Billboard flag did not distinguish the card mesh");
   check(std::abs(card->bounding_radius()-std::hypot(1.f,.5f))<1e-5f,"Billboard card bounds did not match its dimensions");
   check(card->vertices().size()==4&&card->indices().size()==6,"Billboard card is not a single quad");
   rejects([]{(void)Mesh3D::billboard_card(0,1);});
   rejects([]{(void)Mesh3D::billboard_card(1,std::numeric_limits<float>::quiet_NaN());});}
  for(int field=0;field<8;++field){auto invalid=receiver;auto& s=*invalid.material.shadow;
    if(field==0)s.scale=0;if(field==1)s.position.x=std::numeric_limits<double>::infinity();
    if(field==2)s.rotation={0,0,0,0};if(field==3)s.radii.y=0;
    if(field==4)s.opacity=1.1f;if(field==5)s.shape=static_cast<AnalyticShadowShape3D>(-1);
    if(field==6){s.shape=AnalyticShadowShape3D::Annulus;s.outer_radius=s.inner_radius;}
    if(field==7)s.position.x+=1e9;
    rejects([&]{(void)Scene3D::create(camera,{invalid});});
  }
  std::cout<<"3D mesh validation, winding, cameras, depth, rotations, culling and large-coordinate precision passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
