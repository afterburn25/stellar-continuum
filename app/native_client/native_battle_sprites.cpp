#include "native_battle_sprites.hpp"
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
  std::size_t rendered=0;
  for(const auto& sprite:sprites){
    if(rendered>=maximum_battle_art_sprites)break;
    const auto side=sprite.size.x;
    if(!std::isfinite(side)||side<=0.f||side>512.f||sprite.size.y!=side||
       !std::isfinite(sprite.center.x)||!std::isfinite(sprite.center.y)||
       !std::isfinite(sprite.heading_degrees))continue;
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
    out.overlay.emplace_back(Image{image_,{sprite.center.x-side*.5f,sprite.center.y-side*.5f,side,side},
      std::nullopt,{255,255,255,255},sprite.clip,sprite.heading_degrees});
    ++rendered;
  }
}
} // namespace stellar::native_battle_art
