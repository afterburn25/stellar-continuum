#pragma once
#include "native_starfield_style.hpp"
#include <stellar/core/system_background.hpp>
#include <stellar/engine/celestial_background.hpp>
#include <stellar/engine/native_image_preparation.hpp>
#include <stellar/engine/texture_compression.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <map>
#include <tuple>

namespace stellar::native_map {
struct SystemBackgroundOptions {
 std::optional<stellar::core::BackgroundCategory> category;
 int variant{};bool nebula{true},dark_test{},blend_test{};double nebula_opacity{1},blend_scale{1},exposure_scale{1};
};
class NativeSystemBackground {
 struct Entry {std::shared_ptr<const RgbaImage> image;std::optional<ImagePreparationQueue::Ticket> job;std::uint64_t touched{};};
 using Key=std::tuple<std::string,int,int>;
 std::filesystem::path root_;std::shared_ptr<ImagePreparationQueue> queue_;std::map<Key,Entry> images_;std::uint64_t serial_{};
 std::shared_ptr<const Mesh3D> mesh_;std::tuple<float,float,float,float,float,bool> mesh_key_{};bool ready_{true};
 std::shared_ptr<const RgbaImage> request(std::string_view id,int quality,int density){
  const auto& asset=stellar::core::background_asset(id);const int width=std::array{768,1280,4096,4096}.at(static_cast<std::size_t>(std::clamp(quality,0,3)));
  const Key key{std::string(id),width,density};
  if(!images_.contains(key)){
   // Three source/LOD variants maximum, including jobs. Eviction cancels without
   // joining workers; immutable resources already in a frame remain valid.
   if(images_.size()>=3){auto i=std::min_element(images_.begin(),images_.end(),[](const auto& a,const auto& b){return a.second.touched<b.second.touched;});images_.erase(i);}
   images_.try_emplace(key);
  }
  auto& entry=images_.at(key);entry.touched=++serial_;
  if(entry.job&&entry.job->ready()){entry.image=entry.job->take();entry.job.reset();}
  if(!entry.image&&!entry.job){const auto path=root_/asset.path;const auto factory=[path,width,density,quality]{
   // This plate generates its final mip chain on the GPU; load only CPU pixels.
   auto source=decode_rgba_image(path,0,ImageDecodeUsage::PixelsOnly);
   if(quality>=2&&density==1&&source->width()<=width)return source;
   auto image=prepare_celestial_plate(*source,width,density);return quality<=1?compress_opaque_texture(*image):image;};
   if(queue_)entry.job=queue_->submit(std::min<std::size_t>(32u*1024u*1024u,static_cast<std::size_t>(width)*width*4),factory);else entry.image=factory();
  }
  return entry.image;
 }
public:
 stellar::core::SystemBackgroundCatalog catalog;
 SystemBackgroundOptions options;
 void configure(std::filesystem::path root,std::shared_ptr<ImagePreparationQueue> queue){root_=std::move(root);queue_=std::move(queue);}
 void bind(const stellar::core::FreshCampaignState& world){catalog.bind(world);images_.clear();mesh_.reset();ready_=true;}
 // Completed jobs must release shared admission even if the user leaves System
 // View before its first draw. Otherwise a dormant sky can starve Galaxy art.
 void poll(){for(auto& [key,entry]:images_)if(entry.job&&entry.job->ready()){entry.image=entry.job->take();entry.job.reset();}}
 bool ready()const{return ready_;}
 std::size_t cache_bytes()const{std::size_t n=0;for(const auto& [k,e]:images_)if(e.image)n+=e.image->byte_size();return n;}
 stellar::core::SystemBackgroundProfile resolved(int id){
  auto p=catalog.profile(id);if(options.category)p.category=*options.category;
  if(options.category||options.variant){std::vector<const stellar::core::BackgroundAsset*> pool;for(const auto& a:stellar::core::background_assets())if(a.type==p.category)pool.push_back(&a);
   auto i=std::ranges::find_if(pool,[&](const auto* a){return a->id==p.asset_id;});const auto start=i==pool.end()?0:std::distance(pool.begin(),i);
   // Retired bright/dense debug categories cannot reintroduce rejected art.
   if(pool.empty()){p.category=stellar::core::BackgroundCategory::Faint;for(const auto& a:stellar::core::background_assets())if(a.type==p.category)pool.push_back(&a);}
   if(pool.empty())throw std::logic_error("No approved faint system backgrounds");
   p.asset_id=pool[(start+static_cast<std::size_t>(std::max(0,options.variant)))%pool.size()]->id;
   if(options.category){p.blend_asset_id.clear();p.blend_strength=0;}
  }
  if(options.blend_test&&p.blend_asset_id.empty()){
   const auto& base=stellar::core::background_asset(p.asset_id);
   const auto& all=stellar::core::background_assets();
   auto next=std::ranges::find_if(all,[&](const auto& a){return a.type==p.category&&a.id!=p.asset_id&&a.band==base.band;});
   if(next!=all.end()){p.blend_asset_id=next->id;p.blend_strength=.1;}
  }
  // Faintness comes from the approved image; retain its original contrast.
  p.exposure=std::clamp(p.exposure*options.exposure_scale,.05,1.);
  p.blend_strength=std::clamp(p.blend_strength*options.blend_scale,0.,.2);
  return p;
 }
 void preload(int id,int quality,int density){auto p=resolved(id);ready_=request(p.asset_id,quality,density)!=nullptr;if(!p.blend_asset_id.empty()&&!request(p.blend_asset_id,quality,density))ready_=false;}
 void append(DrawList& out,int id,int width,int height,int quality,int density,float camera_roll=0){
  poll();
  auto p=resolved(id);auto image=request(p.asset_id,quality,density);auto blend=p.blend_asset_id.empty()?nullptr:request(p.blend_asset_id,quality,density);ready_=image&& (p.blend_asset_id.empty()||blend);if(!ready_)return;
  const float source_aspect=static_cast<float>(image->width())/image->height(),aspect=static_cast<float>(width)/height,roll=static_cast<float>(p.orientation)+camera_roll;
  const auto key=std::tuple{source_aspect,aspect,roll,static_cast<float>(p.crop_x),static_cast<float>(p.crop_y),p.mirror};
  if(!mesh_||key!=mesh_key_){mesh_=celestial_plate_mesh(source_aspect,aspect,roll,{static_cast<float>(p.crop_x),static_cast<float>(p.crop_y)},p.mirror);mesh_key_=key;}
  MeshInstance3D dome;dome.mesh=mesh_;auto& m=dome.material;m.texture=image;m.ambient=1;m.diffuse=0;m.double_sided=true;m.anisotropic_texture=true;m.cubic_magnification=quality>=2;
  const auto byte=[](double x){return static_cast<std::uint8_t>(std::clamp(std::lround(x),0l,255l));};
  const double transmission=p.local_nebula&&options.nebula?std::exp(-p.environment.dark_optical_depth*options.nebula_opacity):1;
  const double e=p.exposure*transmission*(options.dark_test?.025:1);m.tint={byte(faint_starfield_tint.r*e*(1+p.color_temperature)),byte(faint_starfield_tint.g*e),byte(faint_starfield_tint.b*e*(1-p.color_temperature)),255};
  if(blend){m.surface_effect=SurfaceEffect3D{};m.surface_effect->next_texture=blend;m.surface_effect->blend=static_cast<float>(p.blend_strength);}
  Camera3D camera;camera.position={};camera.near_plane=.01f;camera.far_plane=2;
  out.world.emplace_back(Scene3DView{Scene3D::create(camera,{std::move(dome)}),{0,0,static_cast<float>(width),static_cast<float>(height)}});
 }
};
}
