#include "native_battle_sprites.hpp"
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::native_battle_art {
using namespace stellar::native_map;
NativeBattleSprites::NativeBattleSprites(std::filesystem::path root):root_(std::move(root)){}

void NativeBattleSprites::append(DrawList& out,std::span<const BattleArtSprite> sprites,
    const stellar::core::StellarPhysicalProperties* star){
  if(sprites.empty())return;
  // Top-down key light: the encounter star supplies color; a fixed elevated
  // direction keeps hulls readable against the system backdrop.
  const Vec3 key_direction{-.38f,-.52f,.76f};
  const Vec3 key_color=star?blackbody_light_color(std::clamp(star->effective_temperature_kelvin,100.,100000.)):Vec3{1.f,1.f,1.f};
  if(!image_){
    auto source=decode_rgba_image(root_/"assets/visual/ships/patrol-corvette-tactical-v1.png");
    if(source->width()>2048||source->height()>2048||source->width()!=source->height()||
       source->byte_size()>16u*1024u*1024u)
      throw std::runtime_error("Tactical corvette artwork exceeds its square 2048-pixel resource budget.");
    std::size_t transparent=0;
    for(std::size_t i=3;i<source->pixels().size();i+=4)
      transparent+=source->pixels()[i]==0?1u:0u;
    const auto count=source->pixels().size()/4;
    if(transparent<count/5||transparent>count*95/100)
      throw std::runtime_error("Tactical corvette artwork must have a transparent background and a visible hull.");
    transparent_pixels_=transparent;image_=std::move(source);
  }
  static const auto hull = extruded_convex_mesh(std::array<Point,8>{{{.48f,0},{.16f,.15f},{-.29f,.22f},{-.45f,.14f},{-.48f,0},{-.45f,-.14f},{-.29f,-.22f},{.16f,-.15f}}},.11f);
  static const auto deck = Mesh3D::create({{{-.5f,-.5f,.058f},{0,0,1},{0,1}},{{.5f,-.5f,.058f},{0,0,1},{1,1}},{{.5f,.5f,.058f},{0,0,1},{1,0}},{{-.5f,.5f,.058f},{0,0,1},{0,0}}},{0,1,2,0,2,3});
  // Two tapered nozzle flames per hull, authored in the same unit space as
  // the deck so one uniform scale fits every sprite size. Outer and inner
  // meshes separate the cool fringe from the hot core.
  static const auto flame_outer = []{
    std::vector<Vertex3D> v;std::vector<std::uint32_t> idx;
    for(const float nozzle:{-.125f,.125f}){const auto base=static_cast<std::uint32_t>(v.size());
      v.push_back({{-.455f,nozzle-.025f,.05f},{0,0,1},{0,0}});v.push_back({{-.68f,nozzle,.05f},{0,0,1},{.5f,0}});v.push_back({{-.455f,nozzle+.025f,.05f},{0,0,1},{1,0}});
      idx.insert(idx.end(),{base,base+1,base+2});}
    return Mesh3D::create(std::move(v),std::move(idx));}();
  static const auto flame_inner = []{
    std::vector<Vertex3D> v;std::vector<std::uint32_t> idx;
    for(const float nozzle:{-.125f,.125f}){const auto base=static_cast<std::uint32_t>(v.size());
      v.push_back({{-.455f,nozzle-.012f,.052f},{0,0,1},{0,0}});v.push_back({{-.59f,nozzle,.052f},{0,0,1},{.5f,0}});v.push_back({{-.455f,nozzle+.012f,.052f},{0,0,1},{1,0}});
      idx.insert(idx.end(),{base,base+1,base+2});}
    return Mesh3D::create(std::move(v),std::move(idx));}();
  static const std::shared_ptr<const RgbaImage> white=RgbaImage::create(1,1,{255,255,255,255});
  std::vector<MeshInstance3D> instances;
  const auto field=sprites.front().clip;
  if(field.width<=0||field.height<=0)return;
  std::size_t rendered=0;
  for(const auto& sprite:sprites){
    if(rendered>=maximum_battle_art_sprites)break;
    const auto side=sprite.size.x;
    if(!std::isfinite(side)||side<=0.f||side>512.f||sprite.size.y!=side||
       !std::isfinite(sprite.center.x)||!std::isfinite(sprite.center.y)||
       !std::isfinite(sprite.heading_degrees)||!std::isfinite(sprite.pitch_radians)||!std::isfinite(sprite.depth_pixels))continue;
    const auto angle=sprite.heading_degrees*.01745329252f;
    const auto rotation=compose_rotation(rotation_axis_angle({0,0,1},-angle),
      compose_rotation(rotation_axis_angle({1,0,0},.40f),rotation_axis_angle({0,1,0},-sprite.pitch_radians)));
    const Position3 position{sprite.center.x-field.x-field.width*.5f,
      field.y+field.height*.5f-sprite.center.y,std::clamp(sprite.depth_pixels,-4000.f,4000.f)};
    const auto lit=[&](Material3D& m){m.linear_light=true;m.light_direction=key_direction;m.light_color=key_color;m.light_intensity=1.05f;};
    Material3D metal;metal.tint={98,132,155,255};metal.ambient=.14f;metal.diffuse=.8f;lit(metal);
    PbrSurface3D metal_pbr;metal_pbr.metallic=.85f;metal_pbr.roughness=.38f;metal.pbr=metal_pbr;
    instances.push_back({hull,position,rotation,side,metal});
    Material3D paint;paint.texture=image_;paint.ambient=.5f;paint.diffuse=.5f;paint.transparent=true;paint.double_sided=true;lit(paint);
    instances.push_back({deck,position,rotation,side,paint});
    // Cosmetic engine emission follows only observed motion. No hidden ship
    // state, attack target or presentation clock feeds the simulation. HDR
    // emissive strength pushes the flames past the bloom threshold.
    if(sprite.moving){
      Material3D fringe;fringe.tint={40,148,255,160};fringe.ambient=1.f;fringe.diffuse=0;fringe.transparent=true;fringe.opacity=.62f;fringe.double_sided=true;
      Material3D core;core.tint={155,231,255,220};core.ambient=1.f;core.diffuse=0;core.transparent=true;core.opacity=.85f;core.double_sided=true;
      PbrSurface3D glow;glow.emissive=white;glow.emissive_tint={.3f,.62f,1.f};glow.emissive_strength=2.6f;core.pbr=glow;
      instances.push_back({flame_outer,position,rotation,side,fringe});
      instances.push_back({flame_inner,position,rotation,side,core});
    }
    ++rendered;
  }
  if(!instances.empty()){
    Camera3D camera;camera.projection=Projection3D::Orthographic;camera.position={0,0,10000};
    camera.orthographic_height=field.height;camera.near_plane=1;camera.far_plane=20000;
    Scene3DView scene{Scene3D::create(camera,std::move(instances)),field};
    scene.options.quality=quality_;scene.options.exposure=1.05f;scene.options.bloom_strength=.35f;scene.options.bloom_threshold=.8f;
    out.overlay.emplace_back(std::move(scene));
  }
}
} // namespace stellar::native_battle_art
