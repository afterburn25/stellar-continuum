#pragma once
#include "native_planet_surface_assets.hpp"
#include "native_planet_lighting.hpp"
#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/stellar_object.hpp>
#include <algorithm>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <filesystem>
#include <future>
#include <map>
#include <chrono>
#include <functional>

namespace stellar::native_planets {
using namespace stellar::native_map;
using stellar::core::PlanetAppearance;
struct MaterialSet {
  std::string identity;std::shared_ptr<const RgbaImage> albedo,normal,properties,emission,night,rings,portrait,cloud,metallic_roughness;
  std::size_t bytes()const{std::size_t n=0;for(const auto& p:{albedo,normal,properties,emission,night,rings,portrait,cloud,metallic_roughness})if(p)n+=p->byte_size();return n;}
};
inline std::string material_cache_identity(const PlanetAppearance&);
using MaterialProvider=std::function<std::shared_ptr<const MaterialSet>(const PlanetAppearance&,int)>;
inline float planet_polar_radius(double flattening){return 1-std::clamp(static_cast<int>(std::lround(flattening*100)),0,25)*.01f;}
inline auto planet_ring_dimensions(const PlanetAppearance& a){return std::pair{static_cast<int>(a.rings.inner_radius*100),static_cast<int>(a.rings.outer_radius*100)};}
// Imported renders were projected onto the +Z hemisphere. Present that authored
// face by default, instead of freezing a random reconstructed back toward the
// viewer. Physical spin metadata stays in the save; manual inspection still
// composes a rotation over this static pose. Ring giants retain a readable tilt.
inline Quaternion orientation(const PlanetAppearance& a,double){
  if(a.source_asset_id=="sol:pluto")return {}; // New source is upright on the +Z hemisphere.
  if(a.source_asset_id.starts_with("sol:")){
    // Sol's saved node/phase are simulation metadata, not camera roll. Random
    // nodes could invert Earth, Mars and Neptune differently in each campaign.
    // Share the established reference views with the legacy globe/disc path.
    const auto pose=stellar::native_system_ui::planet_presentation_pose(a.source_asset_id.substr(4));
    return compose_rotation(rotation_axis_angle({0,0,1},pose.roll),compose_rotation(rotation_axis_angle({1,0,0},pose.pitch),rotation_axis_angle({0,1,0},-pose.yaw)));
  }
  if(!a.source_asset_id.empty()&&!a.source_asset_id.starts_with("sol:"))
    return a.rings.enabled?rotation_axis_angle({1,0,0},.42f):Quaternion{};
  return compose_rotation(rotation_axis_angle({0,0,1},static_cast<float>(a.axis_node_radians)),compose_rotation(rotation_axis_angle({1,0,0},static_cast<float>(a.axial_tilt_radians)),rotation_axis_angle({0,1,0},static_cast<float>(stellar::core::planet_rotation_phase(a,0)))));
}
// The saved spin supplies each world's axis/direction. Real-time presentation
// is bounded to 8-14 minutes per turn, never multiplied by strategic speed.
inline Quaternion display_orientation(const PlanetAppearance& a,double visual_seconds,double parent_bearing=0){
  if(a.tidally_locked.value_or(a.source_asset_id=="sol:moon"))return compose_rotation(rotation_axis_angle({0,0,1},static_cast<float>(parent_bearing)),orientation(a,0));
  const double period=480.+360.*std::clamp(std::log2(1.+std::abs(a.rotation_period_days))/8.,0.,1.);
  const auto angle=static_cast<float>(std::fmod(std::max(0.,visual_seconds),period)*2.*std::numbers::pi/period*(a.rotation_period_days<0?-1.:1.));
    const float tilt=static_cast<float>(a.axial_tilt_radians),node=static_cast<float>(a.axis_node_radians);
    const Vec3 axis{std::sin(tilt)*std::cos(node),std::cos(tilt),std::sin(tilt)*std::sin(node)};
    const auto reference=orientation(a,0);
    const Vec3 north{2*(reference.x*reference.y-reference.w*reference.z),1-2*(reference.x*reference.x+reference.z*reference.z),2*(reference.y*reference.z+reference.w*reference.x)};
    const float dot=std::clamp(north.x*axis.x+north.y*axis.y+north.z*axis.z,-1.f,1.f);
    Quaternion alignment;
    if(dot<-.99999f){
      const Vec3 tangent=std::abs(north.x)<.9f?Vec3{0,north.z,-north.y}:Vec3{-north.z,0,north.x};
      alignment=rotation_axis_angle(tangent,std::numbers::pi_v<float>);
    }else{
      const float length=std::sqrt(2*(1+dot));
      alignment={(north.y*axis.z-north.z*axis.y)/length,(north.z*axis.x-north.x*axis.z)/length,(north.x*axis.y-north.y*axis.x)/length,(1+dot)/length};
    }
    // Align the mesh's own north pole first. Spinning an unaligned mesh around
    // a world-space axis would precess the globe and its equatorial ring plane.
    return compose_rotation(rotation_axis_angle(axis,angle),compose_rotation(alignment,reference));
}
inline SphericalThumbnailOptions portrait_options(const PlanetAppearance& a,std::shared_ptr<const RgbaImage> rings){
  SphericalThumbnailOptions o;o.orientation=orientation(a,0);o.polar_radius=planet_polar_radius(a.oblateness);
  o.linear_light=true;o.cloud_opacity=0;o.emission_strength=a.emission_strength;
  if(a.rings.enabled){const auto radii=planet_ring_dimensions(a);o.rings=std::move(rings);o.ring_inner_radius=radii.first*.01f;o.ring_outer_radius=radii.second*.01f;}
  return o;
}
inline std::shared_ptr<const RgbaImage> resize_map(std::shared_ptr<const RgbaImage> p,int width){
    if(!p||p->width()<=width)return p;
    if(auto mip=select_texture_mip(*p,width))return mip;
    const int height=std::max(1,p->height()*width/p->width());
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int x0=x*p->width()/width,x1=(x+1)*p->width()/width,y0=y*p->height()/height,y1=(y+1)*p->height()/height;
    for(int c=0;c<4;++c){unsigned sum=0;for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx)sum+=p->pixels()[(static_cast<std::size_t>(yy)*p->width()+xx)*4+c];pixels[(static_cast<std::size_t>(y)*width+x)*4+c]=static_cast<std::uint8_t>(sum/((x1-x0)*(y1-y0)));}}
  return RgbaImage::create(width,height,std::move(pixels));
}
inline float surface_noise(float x,float y,float z,float s){return std::sin(x*7+s+std::sin(z*9))*std::cos(y*8-s)+.38f*std::sin(x*21+y*17+z*19+s)+.14f*std::cos(x*57-y*69+z*43-s);}
inline MaterialSet procedural(const PlanetAppearance& a,int width){
  if(!stellar::core::planet_class_definition(a.primary_class).solid)throw std::invalid_argument("Giant requires an approved replacement material");
  using enum stellar::core::PlanetClass;const int height=width/2;const float pi=std::numbers::pi_v<float>,seed=static_cast<float>(a.visual_seed%65521)*.007f;
  std::vector<std::uint8_t> color(width*height*4),props(color.size()),emission(color.size());
  const bool gas=a.primary_class==GasGiant||a.primary_class==IceGiant||a.primary_class==MiniNeptune||a.primary_class==HotJupiter;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){const float lat=(.5f-(y+.5f)/height)*pi,lon=((x+.5f)/width-.5f)*2*pi,nx=std::cos(lat)*std::sin(lon),ny=std::sin(lat),nz=std::cos(lat)*std::cos(lon);
    const float f=surface_noise(nx,ny,nz,seed),detail=surface_noise(nx*3,ny*3,nz*3,seed+2),band=.5f+.5f*std::sin(ny*32+f*.8f);
    Color base{107,98,86,255};float liquid=0,ice=0,lava=0;
    if(gas){base=a.primary_class==IceGiant?Color{64,131,163,255}:a.primary_class==HotJupiter?Color{174,99,52,255}:Color{167,144,109,255};}
    else if(a.primary_class==Carbon)base={52,54,58,255};
    else if(a.primary_class==Desert)base={166,118,73,255};
    else if(a.primary_class==Greenhouse)base={145,125,66,255};
    else if(a.primary_class==Volcanic)base={69,58,53,255};
    if(!gas&&a.climate.surface_water>0&&f<static_cast<float>(a.climate.surface_water)*2-1){base={18,54,82,255};liquid=1;}
    else if(!gas&&a.climate.vegetation>0&&f<.5f){base={48,89,51,255};}
    if(!gas&&a.climate.surface_ice>0&&(std::abs(ny)>1-static_cast<float>(a.climate.surface_ice)||a.climate.surface_ice>.7)){base={161,195,205,255};ice=1;}
    if(a.primary_class==Cracked&&std::abs(std::sin(f*3+ny*4))<.11f)base={32,28,27,255};
    const bool local_hotspot=a.primary_class!=Frozen||surface_noise(nx*.6f,ny*.6f,nz*.6f,seed+17)>1.08f;
    if(a.emission_strength>0&&local_hotspot&&std::abs(std::sin(f*4+detail))<(a.primary_class==Frozen?.07f:.15f)){lava=1;base={205,72,14,255};ice=0;}
    const float shade=gas?.74f+.30f*band:.86f+.12f*f+.05f*detail;const auto at=(static_cast<std::size_t>(y)*width+x)*4;
    for(int c=0;c<3;++c)color[at+c]=static_cast<std::uint8_t>(std::clamp((c==0?base.r:c==1?base.g:base.b)*shade,0.f,255.f));color[at+3]=255;
    props[at]=static_cast<std::uint8_t>(255*(liquid?.16f:ice?.4f:.83f));props[at+1]=static_cast<std::uint8_t>(liquid*255);props[at+2]=static_cast<std::uint8_t>(ice*255);props[at+3]=static_cast<std::uint8_t>(std::clamp(.5f+f*.24f,0.f,1.f)*255);
    emission[at]=255;emission[at+1]=103;emission[at+2]=14;emission[at+3]=static_cast<std::uint8_t>(lava*255);
  }
  MaterialSet m;m.albedo=RgbaImage::create(width,height,std::move(color));m.properties=RgbaImage::create(width,height,std::move(props));m.normal=RgbaImage::create(1,1,{128,128,255,255});
  if(a.emission_strength>0)m.emission=RgbaImage::create(width,height,std::move(emission));
  // Cloud deck for atmosphere-bearing procedurals: alpha is cover fraction,
  // RGB a neutral sunlit deck — the renderer reads the same contract as the
  // authored clouds.png maps. Driven by the authoritative cloud_opacity.
  if(!gas&&a.atmosphere.cloud_opacity>0){
    std::vector<std::uint8_t> clouds(static_cast<std::size_t>(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
      const float lat=(.5f-(y+.5f)/height)*pi,lon=((x+.5f)/width-.5f)*2*pi,nx=std::cos(lat)*std::sin(lon),ny=std::sin(lat),nz=std::cos(lat)*std::cos(lon);
      const float n=surface_noise(nx*1.7f,ny*1.7f,nz*1.7f,seed+9.f)+.45f*surface_noise(nx*4.3f,ny*4.3f,nz*4.3f,seed+23.f);
      const float cover=std::clamp((n-.38f)*2.4f,0.f,1.f);const auto at=(static_cast<std::size_t>(y)*width+x)*4;
      clouds[at]=clouds[at+1]=clouds[at+2]=236;clouds[at+3]=static_cast<std::uint8_t>(cover*255);
    }
    m.cloud=RgbaImage::create(width,height,std::move(clouds));
  }
  return m;
}
// Repack the authored properties map's roughness channel into the glTF
// metallic-roughness layout (G=roughness, B=metallic): worlds are
// dielectric, so B stays 0 while per-pixel roughness survives the PBR path.
inline std::shared_ptr<const RgbaImage> pbr_metallic_roughness(
    const std::shared_ptr<const RgbaImage>& properties){
  if(!properties)return {};
  const int w=properties->width(),h=properties->height();
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4);
  const auto& src=properties->pixels();
  for(std::size_t i=0;i<static_cast<std::size_t>(w)*h;++i){
    pixels[i*4]=0;pixels[i*4+1]=src[i*4];pixels[i*4+2]=0;pixels[i*4+3]=255;
  }
  return RgbaImage::create(w,h,std::move(pixels));
}
inline MaterialSet load_material(const std::filesystem::path& root,const PlanetAppearance& a,int width){
  stellar::core::validate_planet_appearance(a);MaterialSet m;
  if(a.source_asset_id=="sol:pluto"){
    const auto dir=root/"planets"/"sol-pluto-v2";
    m.albedo=resize_map(decode_rgba_image(dir/"albedo.png",width),width);m.normal=resize_map(decode_rgba_image(dir/"normal.png",width),width);m.properties=resize_map(decode_rgba_image(dir/"properties.png",width),width);
  }else if(a.source_asset_id.starts_with("sol:")&&std::ranges::any_of(stellar::core::sol_moon_definitions(),[&](const auto& m){return m.id!=stellar::core::moon_body_id&&a.source_asset_id.substr(4)==m.key;})){
    const auto dir=root/"moons"/("sol-"+a.source_asset_id.substr(4));
    m.albedo=resize_map(decode_rgba_image(dir/"albedo.png",width),width);m.normal=resize_map(decode_rgba_image(dir/"normal.png",width),width);m.properties=resize_map(decode_rgba_image(dir/"properties.png",width),width);
  }else if(a.source_asset_id.starts_with("sol:")){
    const auto key=a.source_asset_id.substr(4);
    for(int layer=0;layer<3;++layer){const auto index=stellar::native_system_ui::planet_surface_asset_index(key,layer);if(!index)continue;
      auto image=resize_map(decode_rgba_image(root/"sol"/stellar::native_system_ui::planet_surface_assets[*index].filename,width),width);
      if(layer){auto pixels=image->pixels();for(std::size_t i=0;i<pixels.size();i+=4){const float v=std::max({pixels[i],pixels[i+1],pixels[i+2]})/255.f;pixels[i+3]=static_cast<std::uint8_t>(std::clamp((v-.1f)*1.08f,0.f,1.f)*255);}if(layer==2)for(std::size_t i=0;i<pixels.size();i+=4)pixels[i]=pixels[i+1]=pixels[i+2]=240;image=RgbaImage::create(image->width(),image->height(),std::move(pixels));}
      (layer==0?m.albedo:layer==1?m.night:m.cloud)=std::move(image);
    }
    if(!m.albedo)m=procedural(a,width);
  }else if(a.source_asset_id.empty())m=procedural(a,width);
  else{const auto dir=root/"planets"/a.material_id;
    const auto read=[&](const char* name){return resize_map(decode_rgba_image(dir/(std::string(name)+".png"),width),width);};
    m.albedo=read("albedo");m.normal=read("normal");m.properties=read("properties");
    // Canonical cloud decks ship with every authored material; the alpha
    // channel carries cover and the RGB the lit deck the renderer shows.
    m.cloud=read("clouds");
    if(a.emission_strength>0)m.emission=read("emission");
  }
  m.metallic_roughness=pbr_metallic_roughness(m.properties);
  if(a.rings.enabled&&a.rings.version==1){
    const auto source=resize_map(decode_rgba_image(root/"rings"/a.rings.asset_id/"radial.png",std::max(256,width)),std::max(256,width));auto pixels=source->pixels();
    const double reflectance=.35+.65*std::sqrt(a.rings.reflectivity);
    for(std::size_t i=0;i<pixels.size();i+=4){for(int c=0;c<3;++c)pixels[i+c]=static_cast<std::uint8_t>(std::clamp(pixels[i+c]*a.rings.color[c]*reflectance,0.,255.));
      pixels[i+3]=static_cast<std::uint8_t>(255.*(1-std::exp(-a.rings.optical_depth*pixels[i+3]/255.)));}
    m.rings=RgbaImage::create(source->width(),source->height(),std::move(pixels));
  }else if(a.rings.enabled){std::vector<std::uint8_t> pixels(256*4);for(int i=0;i<256;++i){for(int c=0;c<3;++c)pixels[i*4+c]=static_cast<std::uint8_t>(a.rings.color[c]*255);const float f=i/255.f;const float density=(f>.55f&&f<.60f)?.03f:.45f+.5f*std::abs(std::sin(i*.72f+static_cast<float>(a.visual_seed%37)));pixels[i*4+3]=static_cast<std::uint8_t>(density*a.rings.density*255);}m.rings=RgbaImage::create(256,1,std::move(pixels));}
  // Static UI portraits are a reference pose of the canonical surface, prepared
  // with the same worker and residency budget as the smallest sphere LOD.
  if(width<=128){SphericalMaterialImages preview;preview.albedo=m.albedo;preview.emission=m.emission;
    m.portrait=spherical_material_thumbnail(preview,portrait_options(a,m.rings));}
  m.identity=a.material_id+":"+std::to_string(a.visual_seed);return m;
}
// Owner-thread LRU with one background job and bounded pending queue. Source
// renders never remain resident. Error messages are retained for inspection.
class MaterialCache {
  struct Entry{std::shared_ptr<const MaterialSet> value;std::uint64_t use{};};
  struct Request{std::string key;PlanetAppearance appearance;int width;};
  std::filesystem::path root_;std::map<std::string,Entry> cache_;std::vector<Request> pending_;
  std::map<std::string,std::string> failures_;std::optional<Request> active_;std::future<std::shared_ptr<const MaterialSet>> job_;std::future<void> job_status_;
  // One persistent tagged worker replaces a fresh std::async thread per decode;
  // per-tag stats flow to diagnostics through stats().
  stellar::engine::JobSystem jobs_{1};
  std::uint64_t serial_{};std::size_t bytes_{};
 public:
  static constexpr std::size_t budget=96u*1024u*1024u;
  void set_root(std::filesystem::path root){if(job_.valid())job_.wait();root_=std::move(root);cache_.clear();failures_.clear();pending_.clear();active_.reset();bytes_=0;}
  void poll(){
    if(active_&&job_.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{auto value=job_.get();while(!cache_.empty()&&(bytes_+value->bytes()>budget||cache_.size()>=80)){auto old=std::min_element(cache_.begin(),cache_.end(),[](const auto& a,const auto& b){return a.second.use<b.second.use;});bytes_-=old->second.value->bytes();cache_.erase(old);}bytes_+=value->bytes();cache_[active_->key]={std::move(value),++serial_};}catch(const std::exception& e){failures_[active_->key]=e.what();}active_.reset();}
    if(!active_&&!pending_.empty()){active_=std::move(pending_.front());pending_.erase(pending_.begin());const auto r=*active_;const auto root=root_;
      const auto promise=std::make_shared<std::promise<std::shared_ptr<const MaterialSet>>>();job_=promise->get_future();
      job_status_=jobs_.submit("planet-material",stellar::engine::JobPriority::Normal,stellar::engine::JobCancelToken{},[r,root,promise]{
        try{promise->set_value(std::make_shared<const MaterialSet>(load_material(root,r.appearance,r.width)));}catch(...){try{promise->set_exception(std::current_exception());}catch(...){}}
      });}
  }
  std::shared_ptr<const MaterialSet> request(const PlanetAppearance& a,int width){poll();width=width<=128?128:width<=256?256:width<=1024?1024:2048;const auto key=material_cache_identity(a)+":"+std::to_string(width);
    if(auto it=cache_.find(key);it!=cache_.end()){it->second.use=++serial_;return it->second.value;}
    if(!failures_.contains(key)&&(!active_||active_->key!=key)&&std::ranges::none_of(pending_,[&](const auto& r){return r.key==key;})&&pending_.size()<64)pending_.push_back({key,a,width});poll();return {};
  }
  bool ready()const{return !active_&&pending_.empty();}
  std::shared_ptr<const RgbaImage> portrait(const PlanetAppearance& a){const auto value=request(a,128);return value?value->portrait:nullptr;}
  const auto& errors()const{return failures_;}std::size_t resident_bytes()const{return bytes_;}
  stellar::engine::JobStats job_stats()const{return jobs_.stats();}
};
inline std::shared_ptr<const Mesh3D> planet_mesh(double flattening,int lod){
  static std::map<std::pair<int,int>,std::shared_ptr<const Mesh3D>> meshes;const int shape=std::clamp(static_cast<int>(std::lround(flattening*100)),0,25),level=lod<=128?0:lod<=256?1:2;
  const auto key=std::pair{shape,level};if(const auto it=meshes.find(key);it!=meshes.end())return it->second;
  auto sphere=Mesh3D::uv_sphere(level==0?32:level==1?64:128,level==0?16:level==1?32:64);auto vertices=sphere->vertices();const float polar=planet_polar_radius(flattening);
  for(auto& v:vertices){v.position.y*=polar;v.normal.y/=polar;const float n=std::hypot(v.normal.x,v.normal.y,v.normal.z);v.normal={v.normal.x/n,v.normal.y/n,v.normal.z/n};}return meshes[key]=Mesh3D::create(std::move(vertices),sphere->indices());
}
inline std::string material_cache_identity(const PlanetAppearance& a){
  std::string key=a.material_id+":"+std::to_string(a.visual_seed)+":"+a.rings.asset_id+":"+std::to_string(a.rings.enabled);
  for(double v:{a.rings.optical_depth,a.rings.density,a.rings.reflectivity,a.rings.inner_radius,a.rings.outer_radius,a.rings.color[0],a.rings.color[1],a.rings.color[2],a.oblateness,a.axial_tilt_radians,a.axis_node_radians})key+=":"+std::to_string(v);return key;
}

inline Lighting lighting(double x,double y,const stellar::core::StellarPhysicalProperties* star,double flux){
  Lighting l;const double n=std::hypot(x,y);if(n>0)l.direction={static_cast<float>(-x/n),static_cast<float>(y/n),.65f};
  if(star&&star->effective_temperature_kelvin>=100)l.color=blackbody_light_color(std::clamp(star->effective_temperature_kelvin,100.,100000.));
  // Exposure compression keeps distant worlds inspectable; flux still affects light.
  l.intensity=static_cast<float>(std::clamp(std::pow(std::max(.00001,flux),.12),.75,1.3));return l;
}
inline void append_instances(std::vector<MeshInstance3D>& out,const PlanetAppearance& a,const MaterialSet& maps,Position3 p,float radius,int lod,double /*simulation_days*/,const Lighting& light,Quaternion view={},bool night=false,bool inhabited=false,double visual_seconds=0,double parent_bearing=0){
  const auto rotation=compose_rotation(view,light.locked_rotation.value_or(display_orientation(a,visual_seconds,parent_bearing)));
  const auto ring_rotation=compose_rotation(view,display_orientation(a,0,parent_bearing));const auto mesh=planet_mesh(a.oblateness,lod);
  const auto ring_key=planet_ring_dimensions(a);
  std::optional<AnalyticShadow3D> ring_shadow;
  if(maps.rings&&a.rings.enabled){
    AnalyticShadow3D s;s.shape=AnalyticShadowShape3D::Annulus;s.position=p;s.rotation=ring_rotation;s.scale=radius;
    s.inner_radius=ring_key.first*.01f;s.outer_radius=ring_key.second*.01f;s.opacity_map=maps.rings;ring_shadow=s;
  }
  const auto lit=[&](Material3D& m){m.linear_light=true;m.light_direction=rotate_light(view,light.direction);m.light_color=light.color;m.light_intensity=light.intensity;m.additional_lights=light.additional;for(auto& l:m.additional_lights)l.direction=rotate_light(view,l.direction);};
  auto external_shadow=light.eclipse;if(external_shadow){auto& s=*external_shadow;const auto offset=rotate_light(view,{static_cast<float>(s.position.x),static_cast<float>(s.position.y),static_cast<float>(s.position.z)});s.position={p.x+offset.x*radius,p.y+offset.y*radius,p.z+offset.z*radius};s.scale*=radius;s.rotation=compose_rotation(view,s.rotation);}
  using enum stellar::core::PlanetClass;
  const bool giant=a.primary_class==GasGiant||a.primary_class==IceGiant||a.primary_class==MiniNeptune||a.primary_class==HotJupiter;
  Material3D material;material.texture=maps.albedo;material.ambient=night?.008f:.045f;material.diffuse=night?.12f:.95f;lit(material);material.shadow=external_shadow?external_shadow:ring_shadow;
  // Giant band textures stretch along longitude — anisotropic sampling keeps
  // the authored stripes resolved at the grazing limb instead of blurring
  // into mush (engine drops the sampler at Low quality anyway).
  material.anisotropic_texture=giant;
  // Wrap-diffuse terminator: deep atmospheres soften the day/night edge,
  // airless bodies keep the hard Lambert falloff.
  material.terminator_wrap=giant?.42f:std::clamp(static_cast<float>(a.atmosphere.density)*.45f,0.f,.38f);
  // Dynamic cloud decks are intentionally not rendered here: authored
  // albedo already carries the canonical cloud cover and the material
  // tests require append_instances to emit no extra cloud geometry/shadow
  // in any display LOD (see native_planet_materials "no extra clouds").
  if(lod>128&&maps.normal&&maps.properties){SurfaceResponse3D s;s.normal=maps.normal;s.properties=maps.properties;s.normal_strength=.35f;s.relief=std::clamp(static_cast<float>(a.terrain_height),0.f,.02f);s.cloud_opacity=0;material.surface_response=s;}
  {
    PbrSurface3D pbr;pbr.metallic_roughness=maps.metallic_roughness;
    // Scalar factor multiplies the packed map's authored roughness;
    // metallic stays 0 — every world shades as a dielectric.
    pbr.roughness=giant?.8f:.95f;
    // Opt in to the scene environment slot: when the host binds a local
    // nebula composite, surrounding clouds wash the diffuse/specular IBL
    // response. Restrained so the star key light stays dominant.
    pbr.environment_strength=.32f;
    if(maps.night&&inhabited){pbr.emissive=maps.night;pbr.emissive_strength=1.35f;pbr.night_emissive=1.f;}
    material.pbr=pbr;
  }
  if(a.atmosphere.density>.01&&lod>128){
    const auto& c=a.atmosphere.color;
    Atmosphere3D atmo;
    atmo.tint={static_cast<float>(c[0]),static_cast<float>(c[1]),static_cast<float>(c[2])};
    atmo.strength=std::clamp(static_cast<float>(a.atmosphere.density)*1.5f,.15f,2.6f);
    atmo.power=3.2f;atmo.night_floor=.045f;
    // Terminator-transmitted dusk tint follows the key light's own color.
    atmo.sunset={.92f*light.color.x+.08f,.45f*light.color.y+.05f,.14f*light.color.z+.02f};
    atmo.sunset_strength=.5f;
    material.atmosphere=atmo;
  }
  if(giant){
    // Per-class zonal-band animation; the authoritative storm activity
    // scales wave and turbulence terms on top of the class baseline.
    const float storm=std::clamp(.5f+static_cast<float>(a.giant.storm_activity),0.f,1.3f);
    switch(a.primary_class){
    case HotJupiter:material.band_shear=-.3f;material.band_waves=.55f;material.band_drift=.11f;material.band_diff=2.6f;material.band_turbulence=1.5f;break;
    case IceGiant:material.band_shear=-.09f;material.band_waves=.3f;material.band_drift=.02f;material.band_diff=.7f;material.band_turbulence=.35f;break;
    case MiniNeptune:material.band_shear=-.14f;material.band_waves=.45f;material.band_drift=.035f;material.band_diff=1.1f;material.band_turbulence=.55f;break;
    default:material.band_shear=-.24f;material.band_waves=.8f;material.band_drift=.055f;material.band_diff=1.8f;material.band_turbulence=.8f;break;
    }
    material.band_waves=std::clamp(material.band_waves*storm,0.f,1.f);material.band_turbulence=std::clamp(material.band_turbulence*storm,0.f,8.f);
  }
  out.push_back({mesh,p,rotation,radius,material});
  if(maps.emission){Material3D m;m.texture=maps.emission;m.ambient=1;m.diffuse=0;m.transparent=true;m.opacity=static_cast<float>(a.emission_strength);out.push_back({mesh,p,rotation,radius*1.0001f,m});}
  if(ring_shadow){
    static std::map<std::array<int,4>,std::shared_ptr<const Mesh3D>> rings;
    const int segments=lod<=128?64:lod<=256?128:512,depth=std::max(1,static_cast<int>(std::lround(a.rings.thickness*1000000)));const std::array cache_key{ring_key.first,ring_key.second,segments,depth};
    if(!rings.contains(cache_key)){if(rings.size()>=64)rings.erase(rings.begin());rings[cache_key]=annulus_mesh(ring_key.first*.01f,ring_key.second*.01f,segments,depth*.000001f);}
    AnalyticShadow3D s;s.position=p;s.rotation=rotation;s.scale=radius;
    s.radii={1,planet_polar_radius(a.oblateness),1};
    Material3D m;m.texture=maps.rings;m.ambient=.17f;m.diffuse=.8f;m.transparent=true;m.double_sided=false;m.two_sided_diffuse=true;m.anisotropic_texture=true;
    // Dusty sheets brighten when backlit — the HG forward lobe scaled by the
    // ring's authoritative reflectivity (icy stays near neutral, dusty
    // diffuse rings pick up the Saturn E-ring look).
    m.forward_scatter=.2f+.45f*std::clamp(static_cast<float>(a.rings.reflectivity),0.f,1.f);
    // Real dust phase functions carry a second, broader lobe — icy sheets
    // pick up a mild opposition component away from the forward peak.
    m.forward_scatter_back=-.3f*std::clamp(static_cast<float>(a.rings.reflectivity),0.f,1.f);
    m.forward_scatter_back_mix=.4f;
    // Rayleigh weight: small ice grains scatter blue — bright icy sheets
    // pick up the (450/lambda)^4 tilt while dark rocky rings stay near
    // achromatic. Luminance is preserved; only the hue redistributes.
    m.forward_scatter_hue=.15f+.5f*std::clamp(static_cast<float>(a.rings.reflectivity),0.f,1.f);
    // Ring grains orbit the planet: a restrained doppler asymmetry plus its
    // paired tint gives the approaching lane a subtle bright/blue edge.
    m.orbital_beaming=.08f;m.orbital_beaming_tint=.4f;
    lit(m);m.shadow=s;
    out.push_back({rings[cache_key],p,ring_rotation,radius,m});
  }
}
}
