#include "native_battle_sprites.hpp"
#include "native_battle_workspace.hpp"
#include <stellar/engine/native_scene3d.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char**argv){
  try{
    using namespace stellar::native_map;
    using namespace stellar::native_battle_art;
    require(argc==2,"Supply repository asset root.");
    NativeBattleSprites assets(argv[1]);
    BattleArtSprite sprite{1,4294967296,{500,400},{128,128},90.f,true,{0,60,1280,540}};
    DrawList draw;assets.append(draw,std::span{&sprite,1});
    require(assets.resource()&&assets.resource()->width()==1254&&assets.resource()->height()==1254,
        "Original tactical sprite resolution was lost.");
    require(assets.transparent_pixels()>1'000'000,"Tactical sprite has lost its alpha channel.");
    require(draw.overlay.size()==1,"Moving corvette emits a single lit scene.");
    const auto& image=std::get<Scene3DView>(draw.overlay.back());
    require(image.scene->instances().size()==4&&image.destination.height==540,
        "Ship orientation, flames or field clipping was discarded.");
    const auto& hull=image.scene->instances()[0];
    require(hull.material.pbr&&hull.material.pbr->metallic>.5f&&hull.material.linear_light,
        "Hull lost its lit PBR metal.");
    require(hull.material.pbr->environment_strength>0.f,"Hull metal does not opt in to the environment probe.");
    require(!image.scene->environment(),"Unbound battle scene must not fabricate an environment.");
    const auto probe=RgbaImage::create(4,2,std::vector<std::uint8_t>(4*2*4,128));
    assets.set_environment(probe);
    DrawList lit;assets.append(lit,std::span{&sprite,1});
    require(std::get<Scene3DView>(lit.overlay.back()).scene->environment()==probe,
        "Encounter environment probe was not bound to the battle scene.");
    assets.set_environment(nullptr);
    const auto& plume=image.scene->instances()[2];
    const auto& core=image.scene->instances()[3];
    require(plume.material.transparent&&core.material.transparent&&core.material.pbr&&core.material.pbr->emissive&&core.material.pbr->emissive_strength>1.f,
        "Engine flames lost their HDR emissive.");
    require(plume.rotation.x==hull.rotation.x&&plume.rotation.y==hull.rotation.y&&plume.rotation.z==hull.rotation.z&&plume.rotation.w==hull.rotation.w,"Engine plume no longer follows the hull heading.");
    for(const auto& vertex:plume.mesh->vertices())
      require(vertex.position.x<-.4f,"Engine plume did not follow the stern.");
    sprite.moving=false;DrawList parked;assets.append(parked,std::span{&sprite,1});
    require(parked.overlay.size()==1,"Stationary corvette must not emit propulsion flames.");
    require(std::get<Scene3DView>(parked.overlay.front()).scene->instances().front().mesh==image.scene->instances().front().mesh,"Ship instances decoded duplicate resources.");
    std::vector<BattleArtSprite> fleet(100,sprite);DrawList bounded;assets.append(bounded,fleet);
    require(bounded.overlay.size()==1&&std::get<Scene3DView>(bounded.overlay.front()).scene->instances().size()==64,"Sprite render budget was not enforced.");
    sprite.heading_degrees=std::numeric_limits<float>::quiet_NaN();DrawList invalid;assets.append(invalid,std::span{&sprite,1});
    require(invalid.overlay.empty(),"Invalid orientation reached the renderer.");
    stellar::native_battle_ui::NativeBattleWorkspace workspace;
    stellar::core::MassiveCombatSnapshot snapshot;
    workspace.open(snapshot,0,1280,720);
    DrawList ordered;std::size_t inserted=0;
    workspace.render(ordered,1280,720,[&](DrawList& layer,const UiRect&,float,float){
      inserted=layer.overlay.size();layer.overlay.push_back(image);
    });
    require(inserted>0&&inserted+1<ordered.overlay.size()&&std::holds_alternative<Scene3DView>(ordered.overlay[inserted]),
      "Artwork must render above opaque field and below UI chrome.");
    std::cout<<"native battle sprite assets, alpha, clipping, headings, budgets and layering passed\n";
    return 0;
  }catch(const std::exception&error){std::cerr<<"native battle sprite tests failed: "<<error.what()<<'\n';return 1;}
}
