#include <stellar/engine/asset_registry.hpp>
#include "native_stellar_eruptions.hpp"
#include <stellar/engine/surface_attachment.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
namespace stellar::native_stellar {
using namespace stellar::native_map;using namespace stellar::core;
EruptionArtwork::EruptionArtwork(std::filesystem::path root):root_(std::move(root)/"assets/visual/stellar-eruptions"){
 nlohmann::json j;auto input=stellar::engine::resource_stream(root_/"manifest.json");if(!input)throw std::runtime_error("Stellar eruption manifest is missing");input>>j;
 for(const auto& row:j.at("visualSets")){const auto cls=row.at("starClass").get<std::string>(),type=row.at("eruptionType").get<std::string>();
  const int c=cls=="generic"?7:static_cast<int>(std::string("OBAFGKM").find(cls));const int t=type=="SMALL_PROMINENCE"?0:type=="FLARE"?1:type=="MAJOR_FLARE"?2:type=="SUPERFLARE"?3:4;
  const auto paths=row.at("textures").get<std::array<std::string,4>>();for(const auto& path:paths)if(path.find("..")!=std::string::npos||std::filesystem::path(path).is_absolute())throw std::invalid_argument("Invalid eruption asset path");
  if(!sequences_.emplace(std::tuple{c,t,row.at("variantId").get<int>()-1},paths).second)throw std::invalid_argument("Duplicate eruption sequence");
 }
 // Sample the authored filaments once on curved geometry. Integrating offset
 // copies through a thick proxy smeared their fine detail into fog.
 const auto ribbon=curved_surface_ribbon(48);
 for(auto& mesh:meshes_)mesh=ribbon;
}
void EruptionArtwork::collect(){
 for(auto p=pending_.begin();p!=pending_.end();)if(p->ticket.ready()){images_[p->path]={p->ticket.take(),serial_};p=pending_.erase(p);}else ++p;
 const std::size_t limit=quality_>=2?12:24;
 while(images_.size()>limit){auto old=std::min_element(images_.begin(),images_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});images_.erase(old);}
}
std::shared_ptr<const RgbaImage> EruptionArtwork::request(const std::string& path){
 const auto key=std::to_string(std::array{256,512,1024,1024}[quality_])+"/"+path;
 if(auto i=images_.find(key);i!=images_.end()){i->second.used=serial_;return i->second.image;}
 if(std::any_of(pending_.begin(),pending_.end(),[&](const auto& p){return p.path==key;}))return {};
 if(queue_){if(pending_.size()>=4)return {};const auto full=root_/key;const auto size=std::array{256,512,1024,1024}[quality_];
   const auto bytes=image_decode_output_bytes(full,static_cast<std::size_t>(size)*size*4);
   auto ticket=queue_->submit(bytes,[full]{try{return decode_rgba_image(full);}catch(const std::exception& e){throw std::runtime_error("Stellar eruption "+full.generic_string()+": "+e.what());}});if(ticket)pending_.push_back({key,std::move(*ticket)});return {};}
 auto image=decode_rgba_image(root_/key);images_[key]={image,serial_};return image;
}
void EruptionArtwork::begin_frame(std::uint64_t generation,double day,double seconds,bool running,int quality){
 if(generation!=generation_){playback_.clear();observations_.clear();generation_=generation;}
 day_=day;seconds_=seconds;running_=running;quality_=std::clamp(quality,0,3);++serial_;drawn_=0;records_.clear();collect();
 std::erase_if(playback_,[&](const auto& item){return seconds_-item.second.seconds>120;});
 while(playback_.size()>256)playback_.erase(std::min_element(playback_.begin(),playback_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;}));
 while(observations_.size()>256)observations_.erase(std::min_element(observations_.begin(),observations_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;}));
}
void EruptionArtwork::append(DrawList& out,Point center,float radius,const StellarSystem& system,int component,UiRect clip){
 if(!detailed_lod(radius)||!system.stellar_activity||component<0||static_cast<std::size_t>(component)>=system.stellar_activity->size()||drawn_>=6)return;
 if(center.x+radius*3<clip.x||center.y+radius*3<clip.y||center.x-radius*3>clip.x+clip.width||center.y-radius*3>clip.y+clip.height)return;
 const auto& state=system.stellar_activity->at(component);if(state.profile.spectral==EruptionSpectralClass::Unsupported)return;
 const auto key=std::pair{system.id,component};const auto last=observations_.find(key);
 const double crossed_from=last!=observations_.end()&&seconds_-last->second.seconds<1.?last->second.day:day_;
 observations_[key]={day_,seconds_,serial_};
 const float left=std::max(clip.x,center.x-radius*3.5f),top=std::max(clip.y,center.y-radius*3.5f),right=std::min(clip.x+clip.width,center.x+radius*3.5f),bottom=std::min(clip.y+clip.height,center.y+radius*3.5f);
 if(right-left<1||bottom-top<1)return;const UiRect destination{left,top,right-left,bottom-top};
 Camera3D camera;camera.projection=Projection3D::Orthographic;camera.position={(left+right-2*center.x)/(2*radius),-(top+bottom-2*center.y)/(2*radius),10};camera.orthographic_height=destination.height/radius;camera.far_plane=30;
 std::vector<MeshInstance3D> instances;const auto& config=stellar_activity_configuration();int count=0;
 // Compress the enormous physical luminosity range into a modest display
 // exposure adjustment, preserving faint dwarf activity and hot-star filaments.
 const double luminosity=stellar_host_physics(system,component).luminosity_solar;
 const float exposure=static_cast<float>(std::clamp(.9+.04*std::log1p(std::max(0.,luminosity)),.9,1.15));
 for(auto it=state.events.rbegin();it!=state.events.rend();++it){const auto& e=*it;if(e.start_day>day_&&!e.paused)continue;
  auto physical=stellar_eruption_sample(e,day_);auto prior=playback_.find(e.id);
  const bool crossed_between_frames=prior==playback_.end()&&e.start_day>crossed_from&&e.start_day<=day_;
  if(physical.finished&&!crossed_between_frames&&(prior==playback_.end()||prior->second.fraction>=1))continue;
  if(count>=std::min(std::array{1,2,4,6}[quality_],config.simultaneous_caps[static_cast<int>(state.profile.level)])||drawn_+count>=6)break;
  double progress=crossed_between_frames?0.:physical.fraction;
  if(prior!=playback_.end()&&!e.paused&&e.start_day==prior->second.start){const auto& p=prior->second;
   progress=running_?std::min(progress,p.fraction+std::clamp(seconds_-p.seconds,0.,.1)/config.timings[static_cast<int>(e.type)].min_display_seconds):p.fraction;}
  auto sample=stellar::engine::sample_timeline(e.stage_days,progress*stellar_eruption_duration(e));
  if(sample.finished){playback_[e.id]={seconds_,1,e.start_day,serial_};continue;}
  const int kind=static_cast<int>(e.type),cls=(kind==1||kind==2)?static_cast<int>(state.profile.spectral):7;
  const auto& sequence=sequences_.at({cls,kind,e.visual_variant});const int next=std::min(3,static_cast<int>(sample.stage)+1);
  // Remember first observation while assets load, so a short accelerated event
  // cannot disappear before its first prepared frame reaches the GPU.
  if(prior==playback_.end())playback_[e.id]={seconds_,progress,e.start_day,serial_};
  auto image=request(sequence[sample.stage]),following=request(sequence[next]);if(!image||!following)continue;
  playback_[e.id]={seconds_,progress,e.start_day,serial_};
  // Distinct source images are not motion-corresponding frames. Keep each
  // readable and crossfade only near the stage boundary, without UV warping.
  const float blend=static_cast<float>(stellar::engine::smooth_timeline_curve(std::clamp((sample.stage_fraction-.8)/.2,0.,1.)));
  const float rise=static_cast<float>(std::clamp(progress/.2,0.,1.)),decay=static_cast<float>(1-stellar::engine::smooth_timeline_curve(std::clamp((progress-.55)/.45,0.,1.)));
  const float amplitude=(.12f+.88f*rise)*(.1f+.9f*decay),lod=std::clamp((radius-22)/26.f,0.f,1.f);
  const float extent=std::array{.52f,.72f,1.0f,1.45f,1.25f}[kind]*static_cast<float>(e.scale*std::sqrt(e.magnitude))*(.55f+.45f*rise);
  const Vec3 normal=spherical_surface_normal(e.latitude,e.longitude);const float travel=kind==4&&e.cme_escaped?static_cast<float>(progress*progress*1.8):0.f;
  MeshInstance3D instance;instance.mesh=meshes_[kind];instance.scale=extent;
  instance.position={normal.x*(1+travel),normal.y*(1+travel),normal.z*(1+travel)};instance.rotation=spherical_surface_rotation(e.latitude,e.longitude,e.orientation);
  auto& m=instance.material;m.texture=image;m.ambient=1;m.diffuse=0;m.transparent=true;m.double_sided=true;
  m.opacity=std::clamp(amplitude*lod*exposure*static_cast<float>(e.brightness*(.75+.25*std::sqrt(e.magnitude)))*(.95f+.05f*std::sin(static_cast<float>(progress)*150.f+e.visual_variant)),0.f,1.f);
  // Dedicated class artwork retains authored color. Generic gold effects also
  // retain their supplied palette; no blue/red replacement artwork is invented.
  SurfaceEffect3D effect;effect.next_texture=following;effect.blend=blend;effect.view_sphere_center={static_cast<float>(-camera.position.x),static_cast<float>(-camera.position.y),-10};effect.sphere_radius=1;
  m.surface_effect=std::move(effect);instances.push_back(std::move(instance));records_.push_back({e.id,e.visual_variant,static_cast<int>(sample.stage),progress});++count;
 }
 if(!instances.empty()){drawn_+=count;out.world.emplace_back(Scene3DView{Scene3D::create(camera,std::move(instances)),destination});}
}
}

