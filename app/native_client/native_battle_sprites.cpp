#include "native_battle_sprites.hpp"
#include <stellar/engine/native_geometry3d.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::native_battle_art {
using namespace stellar::native_map;
NativeBattleSprites::NativeBattleSprites(std::filesystem::path root):root_(std::move(root)){}

void NativeBattleSprites::append(DrawList& out,std::span<const BattleArtSprite> sprites){
  if(sprites.empty())return;
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
    const float dx=std::cos(angle),dy=std::sin(angle);
    const auto at=[&](float x,float y){return Point{
      sprite.center.x+(x*dx-y*dy)*side,sprite.center.y+(x*dy+y*dx)*side};};
    // Cosmetic engine emission follows only observed motion. No hidden ship
    // state, attack target or presentation clock feeds the simulation.
    if(sprite.moving){
      for(const float nozzle:{-.125f,.125f}){
        out.overlay.emplace_back(TriangleMesh{{at(-.455f,nozzle-.025f),
          at(-.66f,nozzle),at(-.455f,nozzle+.025f)}, {0,1,2}, {40,148,255,160},sprite.clip});
        out.overlay.emplace_back(TriangleMesh{{at(-.455f,nozzle-.012f),
          at(-.59f,nozzle),at(-.455f,nozzle+.012f)}, {0,1,2}, {155,231,255,220},sprite.clip});
      }
    }
    const auto rotation=compose_rotation(rotation_axis_angle({0,0,1},-angle),
      compose_rotation(rotation_axis_angle({1,0,0},.40f),rotation_axis_angle({0,1,0},-sprite.pitch_radians)));
    const Position3 position{sprite.center.x-field.x-field.width*.5f,
      field.y+field.height*.5f-sprite.center.y,std::clamp(sprite.depth_pixels,-4000.f,4000.f)};
    Material3D metal;metal.tint={98,132,155,255};metal.ambient=.28f;metal.diffuse=.72f;
    instances.push_back({hull,position,rotation,side,metal});
    Material3D paint;paint.texture=image_;paint.ambient=.65f;paint.diffuse=.35f;paint.transparent=true;paint.double_sided=true;
    instances.push_back({deck,position,rotation,side,paint});
    ++rendered;
  }
  if(!instances.empty()){
    Camera3D camera;camera.projection=Projection3D::Orthographic;camera.position={0,0,10000};
    camera.orthographic_height=field.height;camera.near_plane=1;camera.far_plane=20000;
    out.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(instances)),field});
  }
}
} // namespace stellar::native_battle_art
