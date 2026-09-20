#include "native_planet_materials.hpp"
#include <stellar/core/planetary_catalog.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <set>
#include <iostream>
using namespace stellar::native_planets;
using namespace stellar::core;
void check(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
int main(int argc,char** argv)try{
 check(argc==2,"Expected repository root");const std::filesystem::path root=argv[1];
 nlohmann::json audit;std::ifstream(root/"docs/planet-art/full-audit.json")>>audit;
 std::set<std::string> folders,accepted,hashes;int rejected=0,duplicates=0,earth=0,corrections=0;
 for(const auto& record:audit.at("images")){
  folders.insert(record.at("folder"));if(record.at("status")=="accepted"){
   check(!record.at("earthGeography").get<bool>(),"Earth geography was accepted");check(record.at("materialCreated")&&record.at("systemThumbnailCreated"),"Accepted art has no material/thumbnail");
   accepted.insert(record.at("id"));check(hashes.insert(record.at("sha256")).second,"Duplicate content entered runtime art");
  }else{check(record.at("status")=="rejected"&&!record.at("rejectionReason").get<std::string>().empty(),"Unresolved source image");++rejected;}
  if(!record.at("duplicateOf").is_null()&&record.at("duplicateOf")!="")++duplicates;
  if(record.at("earthGeography").get<bool>())++earth;if(!record.at("correctionReason").get<std::string>().empty())++corrections;
 }
 check(folders.size()==47&&audit.at("images").size()==1087&&accepted.size()==665&&rejected==422,"Full import inventory differs from reviewed set");
 check(duplicates==17&&earth==70&&corrections==168,"Audit exclusions/corrections are incomplete");
 auto star=generate_stellar_physics(12,StellarObjectType::GYellowStar);star.luminosity_solar=1;star.age_myr=4600;star.radiation_modifier=star.wind_modifier=1;
 // Retired ordinary giant sources remain only in historical provenance.
 nlohmann::json retired,new_audit;std::ifstream(root/"data/planets/deprecated-giant-art-v1.json")>>retired;
 std::ifstream(root/"data/planets/giant-asset-audit-v1.json")>>new_audit;
 for(const auto& art:retired.at("records"))accepted.erase(art.at("asset").get<std::string>());
 // The later all-gaseous-worlds replacement also retired five mini-Neptunes.
 nlohmann::json current_art;std::ifstream(root/"data/planets/planet-art-v1.json")>>current_art;
 for(const auto& art:current_art.at("rejectedAssets"))if(art.at("status")=="rejected"&&art.value("reason",std::string{}).starts_with("Retired:"))accepted.erase(art.at("id").get<std::string>());
 for(const auto& art:new_audit.at("images"))if(art.at("status")=="accepted")accepted.insert(art.at("id"));
 for(const auto& art:planet_art_definitions()){
  check(accepted.erase(art.id)==1,"Runtime selected an unaudited or rejected source");
  const auto& sub=planet_subclass_definition(art.primary,art.subclass);
  check((!art.liquid||sub.water>0)&&(!art.ice||sub.ice>0)&&(!art.vegetation||sub.vegetation>0)&&(!art.emission||sub.emission>0)&&art.rings==sub.rings,"Source flags contradict subtype: "+art.id);
  for(const char* name:{"albedo","normal","properties","clouds","emission","thumbnail"})check(std::filesystem::file_size(root/"assets/visual/planets"/art.material_id/(std::string(name)+".png"))>50,"Missing material map: "+art.id);
 }
 check(accepted.empty(),"Reviewed material missing from runtime registry");
 auto b=make_planet_type_example(81,10000,1,2,star,PlanetClass::Temperate,"gaia-islands");auto a=*b.appearance;
 check(!a.source_asset_id.empty(),"Reviewed habitable artwork was not selected");
 const auto close=load_material(root/"assets/visual",a,1024),system=load_material(root/"assets/visual",a,128);
 check(close.identity==system.identity&&resize_map(close.albedo,128)->pixels()==system.albedo->pixels(),"System and planetary LOD disagree on surface identity");
 SphericalMaterialImages preview;preview.albedo=resize_map(close.albedo,128);preview.emission=resize_map(close.emission,128);
 check(system.portrait&&system.portrait->pixels()==spherical_material_thumbnail(preview,portrait_options(a,{}))->pixels(),"Portrait must be projected from the canonical globe material");
 auto turned=a;turned.initial_phase_radians+=1.2;
 check(load_material(root/"assets/visual",turned,128).portrait->pixels()==system.portrait->pixels(),"Saved random spin hid the authored hemisphere");
 const auto light=lighting(10,3,&star,1);std::vector<MeshInstance3D> front,later,paused;
 append_instances(front,a,close,{},1,1024,2,light);append_instances(later,a,close,{},1,1024,2.2,light);append_instances(paused,a,close,{},1,1024,2.2,light);
 check(front[0].material.linear_light&&front[0].material.diffuse>.9f&&front[0].material.ambient<.1f,"Planet relighting bypassed linear light or erased night side");
 check(lighting(10,3,&star,.00001).intensity>=.75f&&light.direction.z>.6f,"Distant planet face is underexposed");
 const auto components=[](Quaternion q){return std::array{q.x,q.y,q.z,q.w};};
 const auto north_y=[](Quaternion q){return 1-2*(q.x*q.x+q.z*q.z);};
 check(components(front[0].rotation)==components(later[0].rotation)&&components(later[0].rotation)==components(paused[0].rotation),"Simulation time rotated a planet");
 check(front[0].mesh==later[0].mesh&&front[0].material.texture==later[0].material.texture,"Time changed the imported surface resource");
 check(close.albedo->pixels()==decode_rgba_image(root/"assets/visual/planets"/a.material_id/"albedo.png")->pixels(),"Imported surface pixels were modified");
 // All subclasses, including barren, cloudy, gas and procedural fallbacks,
 // ignore old saved cloud opacity and spin speed in every display LOD.
 for(const auto& subtype:planet_subclass_definitions()){if(!subtype.generation_enabled)continue;
  auto example=*make_planet_type_example(81,11000,1,2,star,subtype.primary,subtype.id).appearance;
  const auto original=example;example.atmosphere.cloud_opacity=1;
  if(!example.source_asset_id.empty())check(north_y(orientation(example,0))>.9f,"Imported artwork was inverted: "+subtype.id);
  auto clear=example;clear.atmosphere.cloud_opacity=0;
  const auto maps=load_material(root/"assets/visual",example,128),clear_maps=load_material(root/"assets/visual",clear,128);
  check(maps.albedo->pixels()==clear_maps.albedo->pixels()&&maps.portrait->pixels()==clear_maps.portrait->pixels(),"Saved cloud opacity changed planet artwork/portrait: "+subtype.id);
  for(int lod:{128,256,1024}){
   std::vector<MeshInstance3D> first,last;append_instances(first,example,maps,{},1,lod,0,light);append_instances(last,example,maps,{},1,lod,1234.56,light);
   check(first.size()==last.size(),"Simulation time changed planet layers");
   for(std::size_t i=0;i<first.size();++i){const auto& x=first[i];
    check(components(x.rotation)==components(last[i].rotation),"Planet layer rotates automatically: "+subtype.id);
    check(!x.material.surface_response||(!x.material.surface_response->cloud_shadow&&x.material.surface_response->cloud_opacity==0),"Added cloud shadow remained: "+subtype.id);
    check(x.material.texture==maps.albedo||x.material.texture==maps.emission||x.material.texture==maps.rings||!x.material.texture,"Extra textured layer remained: "+subtype.id);
   }
  }
  check(example.source_asset_id==original.source_asset_id&&example.material_id==original.material_id,"Visual correction changed source selection");
 }
 auto sol=a;sol.source_asset_id=sol.material_id="sol:earth";sol.atmosphere.cloud_opacity=1;
 const auto earth_cloudy=load_material(root/"assets/visual",sol,128);sol.atmosphere.cloud_opacity=0;
 check(earth_cloudy.portrait->pixels()==load_material(root/"assets/visual",sol,128).portrait->pixels(),"Sol retained extra cloud cover");
 // Reproduce saved random rolls (Earth was 140.82 degrees in the affected save).
 // North-up map rows must remain above south in the reference globe and portrait,
 // for every Sol body and independent of saved phase, obliquity and elapsed time.
 std::vector<std::uint8_t> hemisphere_pixels(32*16*4);
 for(int y=0;y<16;++y)for(int x=0;x<32;++x){const auto at=(y*32+x)*4;
  hemisphere_pixels[at]=y<8?255:0;hemisphere_pixels[at+2]=y<8?0:255;hemisphere_pixels[at+3]=255;}
 SphericalMaterialImages hemispheres;hemispheres.albedo=RgbaImage::create(32,16,std::move(hemisphere_pixels));
 for(const auto key:{"mercury","venus","earth","mars","jupiter","saturn","uranus","neptune","moon","pluto"}){
  auto sample=sol;sample.source_asset_id=sample.material_id="sol:"+std::string(key);
  const auto reference=components(orientation(sample,0));
  for(const double roll:{0.,140.82,196.37,283.,359.})for(const double tilt:{0.,23.439,97.77,177.36}){
   sample.axis_node_radians=roll*std::numbers::pi/180;sample.axial_tilt_radians=tilt*std::numbers::pi/180;
   sample.initial_phase_radians=roll*.02;
   check(components(orientation(sample,123456))==reference,"Saved Sol spin/node altered the reference view: "+std::string(key));
  }
  const auto rotation=orientation(sample,0);
  check(north_y(rotation)>(std::string_view(key)=="uranus"?.05f:.9f),"Sol reference north pole inverted: "+std::string(key));
  const auto portrait=spherical_material_thumbnail(hemispheres,portrait_options(sample,{}));
  const int size=portrait->width();const auto top=(size/4*size+size/2)*4,bottom=(size*3/4*size+size/2)*4;
  if(std::string_view(key)!="uranus")check(portrait->pixels()[top]>portrait->pixels()[top+2]&&portrait->pixels()[bottom+2]>portrait->pixels()[bottom],"Portrait UV rows flipped north/south: "+std::string(key));
  std::vector<MeshInstance3D> instances;append_instances(instances,sample,earth_cloudy,{},1,256,8000,light);
  check(std::abs(north_y(instances.front().rotation)-std::cos(sample.axial_tilt_radians))<.00001,"Live globe lost its physical pole");
 }
 // Slow visual spin uses real presentation time and each world's physical axis.
 {
  auto axial=a;axial.axial_tilt_radians=0;axial.axis_node_radians=0;axial.rotation_period_days=1;
  const auto first=display_orientation(axial,0),moving=display_orientation(axial,10);
  check(components(first)!=components(moving),"Visual clock did not rotate the globe");
  check(std::abs(moving.y)>.02f&&std::abs(moving.y)<.08f,"Planet visual spin is not slow");
  auto sideways=axial;sideways.axial_tilt_radians=std::numbers::pi/2;
  const auto pole=[](Quaternion q){return Vec3{2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)};};
  const auto tilted=pole(display_orientation(sideways,10));
  check(tilted.x>.9999f&&std::abs(tilted.y)<.0001f,"World axis was ignored");
  for(double tilt:{0.,.41,1.7,3.141592653589793})for(double node:{0.,1.,4.}){
   sideways.axial_tilt_radians=tilt;sideways.axis_node_radians=node;
   const auto initial=pole(display_orientation(sideways,0));
   for(double seconds:{10.,180.,700.}){const auto current=pole(display_orientation(sideways,seconds));
    check(std::hypot(current.x-initial.x,current.y-initial.y,current.z-initial.z)<.00001f,"Surface spin made the polar axis wobble");}
  }
  auto retro=axial;retro.rotation_period_days=-1;
  check(display_orientation(retro,10).y*moving.y<0,"Retrograde spin direction lost");
  std::vector<MeshInstance3D> normal,fast;
  append_instances(normal,axial,close,{},1,1024,0,light,{},false,false,10);
  append_instances(fast,axial,close,{},1,1024,1e8,light,{},false,false,10);
  check(components(normal.front().rotation)==components(fast.front().rotation),"Game speed accelerated visual spin");
  check(normal.front().material.texture==fast.front().material.texture,"Spin regenerated source art");
  axial.tidally_locked=true;
  check(components(display_orientation(axial,0,.3))==components(display_orientation(axial,100,.3)),"Visual spin unlocked a synchronous body");
  check(components(display_orientation(axial,0,.3))!=components(display_orientation(axial,0,.4)),"Locked body failed to follow parent bearing");
  const auto local_parent=[&](double bearing){
   auto q=display_orientation(axial,100,bearing);q.x=-q.x;q.y=-q.y;q.z=-q.z;
   const Vec3 v{static_cast<float>(std::cos(bearing)),static_cast<float>(std::sin(bearing)),0};
   const Vec3 t{-2*q.z*v.y,2*q.z*v.x,2*(q.x*v.y-q.y*v.x)};
   return Vec3{v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,q.w*t.z+q.x*t.y-q.y*t.x};
  };
  const auto face=local_parent(0);
  for(double bearing:{.2,1.5,3.,4.8,6.2}){const auto current=local_parent(bearing);
   check(std::hypot(face.x-current.x,face.y-current.y,face.z-current.z)<.00001f,"Locked surface changed its parent-facing hemisphere");}
 }
 check(!front[0].material.shadow,"Unringed world received a ring shadow");
 auto host=make_planet_type_example(21,10010,1,2,star,PlanetClass::GasGiant,"cream-band");auto giant=*host.appearance;giant.rings=make_planet_ring(host,giant,&star,0,"dense-banded-ring");
 giant.oblateness=.123;giant.atmosphere.cloud_opacity=.5;giant.emission_strength=.1;
 auto ring_maps=load_material(root/"assets/visual",giant,256);ring_maps.emission=close.albedo;
 const auto ring_portrait=load_material(root/"assets/visual",giant,128);
 const auto pose=portrait_options(giant,ring_portrait.rings);
 check(pose.rings==ring_portrait.rings&&std::abs(pose.polar_radius-.88)<1e-6&&components(pose.orientation)==components(orientation(giant,0)),"Portrait lost canonical rings, oblateness or reference axis/spin");
 SphericalMaterialImages ring_preview;ring_preview.albedo=ring_portrait.albedo;ring_preview.emission=ring_portrait.emission;
 check(ring_portrait.portrait->pixels()==spherical_material_thumbnail(ring_preview,pose)->pixels(),"Portrait disagrees with canonical ringed appearance");
 auto no_rings=pose;no_rings.rings.reset();
 check(ring_portrait.portrait->pixels()!=spherical_material_thumbnail(ring_preview,no_rings)->pixels(),"Ringed portrait omitted ring geometry");
 const auto captures=root/"work/planet-portraits";std::filesystem::create_directories(captures);
 encode_rgba_png(*ring_portrait.portrait,captures/"ringed-portrait.png");
 encode_rgba_png(*system.portrait,captures/"gaia-portrait.png");
 for(const auto subtype:{"cream-band","amber-storm","cyan-haze"}){
   const auto example=*make_planet_type_example(81,10015,1,2,star,std::string_view(subtype)=="cyan-haze"?PlanetClass::IceGiant:PlanetClass::GasGiant,subtype).appearance;
   const auto material=load_material(root/"assets/visual",example,128);
   encode_rgba_png(*material.portrait,captures/(std::string(subtype)+".png"));
 }
 for(int lod:{128,256,1024}){
  std::vector<MeshInstance3D> world;const Position3 place{1e10+.25,0,0};const float size=lod==128?.001f:1.f;
  const auto view=rotation_axis_angle({0,1,0},.7f);
  append_instances(world,giant,ring_maps,place,size,lod,12,light,view);
  const auto& rings=world.back();check(rings.material.shadow&&rings.material.shadow->shape==AnalyticShadowShape3D::Ellipsoid,"Ring lacks a solid planet blocker");
  check(rings.material.two_sided_diffuse&&!world.front().material.two_sided_diffuse,"Thin ring lighting must not light the night side of solid planets");
  check(std::abs(rings.material.shadow->radii.y-.88f)<1e-6f,"Shadow silhouette disagrees with flattened mesh");
  check(world.front().material.shadow->inner_radius==pose.ring_inner_radius&&world.front().material.shadow->outer_radius==pose.ring_outer_radius,"Portrait framing disagrees with rendered ring radii");
  check(components(rings.material.shadow->rotation)==components(world.front().rotation),"Planet shadow lost viewer/axis pose");
  check(world.front().material.shadow->opacity_map==rings.material.texture,"Shadow transparency differs from visible rings");
  for(const auto& i:world){
   if(i.material.diffuse==0){check(!i.material.shadow,"Emission layer has a reflected-light shadow");continue;}
   check(i.material.shadow&&i.material.shadow->scale==size&&i.material.shadow->position.x==place.x,"Shadow lost canonical body position or presentation scale");
   if(i.material.shadow->shape==AnalyticShadowShape3D::Annulus)check(components(i.material.shadow->rotation)==components(rings.rotation),"Cloud motion rotated the shadow-casting ring plane");
  }
  Camera3D camera;camera.position={place.x,0,3};(void)Scene3D::create(camera,world);
 }
 check(planet_mesh(0,1024)==planet_mesh(0,1024)&&planet_mesh(0,128)->vertices().size()<planet_mesh(0,1024)->vertices().size(),"Sphere LOD resources are not shared/reduced");
 const auto other_light=lighting(-10,-3,&star,1);check(light.direction.x*other_light.direction.x<0&&light.direction.y*other_light.direction.y<0,"Star-facing light failed to follow orbital position");
 const auto solar=blackbody_light_color(5772),cool=blackbody_light_color(3200),blue=blackbody_light_color(12000);
 check(solar.x==1&&solar.y==1&&solar.z==1,"Reference illumination should preserve source albedo");
 check(cool.x>cool.y&&cool.y>cool.z&&cool.y>.45f&&blue.z>blue.y&&blue.y>blue.x,"Star temperature should warm/cool the whole material without marker-color tinting");
 auto icy=*make_planet_type_example(2,10009,1,2,star,PlanetClass::Frozen,"icy-hotspots").appearance;
 const auto ice_map=procedural(icy,256);std::size_t hot_pixels=0;for(std::size_t i=3;i<ice_map.emission->pixels().size();i+=4)hot_pixels+=ice_map.emission->pixels()[i]>0;
 check(hot_pixels<256*128/50,"Localized ice hotspots must not become global lava coverage");
 MaterialCache cache;cache.set_root(root/"assets/visual");
 const auto fetch=[&](const PlanetAppearance& appearance,int lod){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);std::shared_ptr<const MaterialSet> value;while(!(value=cache.request(appearance,lod))){check(cache.errors().empty(),"Asynchronous material failed");check(std::chrono::steady_clock::now()<deadline,"Streaming timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}return value;};
 const auto same=fetch(a,1024);check(fetch(a,1024)==same,"Resident material was decoded twice");
 const auto portrait_pack=fetch(a,128);check(cache.portrait(a)==portrait_pack->portrait&&cache.portrait(a)==cache.portrait(a),"Portrait did not share the bounded canonical material cache");
 // Distinct generated worlds exceed the residency allowance; eviction must
 // not invalidate an immutable material still held by a submitted view.
 auto hot=*make_planet_type_example(2,10001,1,2,star,PlanetClass::Volcanic,"magma-ocean").appearance;hot.source_asset_id.clear();hot.material_id="procedural:volcanic:magma-ocean";
 for(int i=0;i<16;++i){hot.visual_seed=static_cast<std::uint64_t>(i);(void)fetch(hot,1024);check(cache.resident_bytes()<=MaterialCache::budget,"Planet streaming exceeded its CPU budget");}
 check(same->identity==close.identity&&same->albedo->pixels()==close.albedo->pixels(),"Eviction invalidated a live planet view");
 std::cout<<"Historical 47-folder audit plus reviewed replacement giants; 710 runtime materials, shared identity/LOD, static planets, no extra clouds for all 65 subclasses, star lighting and bounded streaming passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
