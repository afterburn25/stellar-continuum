#include "native_planet_disc_assets.hpp"

#include <stellar/engine/native_image_preparation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace stellar::native_system_ui {
namespace {
using stellar::native_map::RgbaImage;
using stellar::native_system::NativeSystemBodyVisualClass;
struct FloatColor {float r{},g{},b{},a{1.f};};
struct Vector3 {float x{},y{},z{};};
struct SourceDisc {float center_x{},center_y{},radius_x{},radius_y{};};

[[nodiscard]] std::optional<SourceDisc> source_disc(std::string_view key){
  if(key=="earth")return SourceDisc{.5280f,.5836f,.1851f,.1968f};
  if(key=="mercury")return SourceDisc{.5f,.5f,.454f,.454f};
  if(key=="uranus")return SourceDisc{.25f,.501f,.176f,.352f};
  if(key=="moon")return SourceDisc{.529f,.503f,.398f,.398f};
  if(key=="venus")return SourceDisc{.5f,.5f,.414f,.426f};
  return std::nullopt;
}
[[nodiscard]] bool canonical_key(std::string_view key){
  static constexpr std::array keys{"mercury","venus","earth","mars","jupiter","saturn","uranus","neptune","moon"};
  return std::ranges::find(keys,key)!=keys.end();
}
[[nodiscard]] bool unknown_class(NativeSystemBodyVisualClass value){return value==NativeSystemBodyVisualClass::unknown_planet||value==NativeSystemBodyVisualClass::unknown_moon;}
[[nodiscard]] FloatColor class_color(NativeSystemBodyVisualClass value){
  switch(value){
    case NativeSystemBodyVisualClass::rocky:return {143.f/255.f,137.f/255.f,127.f/255.f,1};
    case NativeSystemBodyVisualClass::oceanic:return {56.f/255.f,110.f/255.f,147.f/255.f,1};
    case NativeSystemBodyVisualClass::frozen:return {160.f/255.f,183.f/255.f,197.f/255.f,1};
    case NativeSystemBodyVisualClass::hot_rocky:return {164.f/255.f,123.f/255.f,89.f/255.f,1};
    case NativeSystemBodyVisualClass::gas_giant:return {184.f/255.f,160.f/255.f,127.f/255.f,1};
    case NativeSystemBodyVisualClass::ice_giant:return {106.f/255.f,159.f/255.f,171.f/255.f,1};
    case NativeSystemBodyVisualClass::moon:return {139.f/255.f,144.f/255.f,152.f/255.f,1};
    default:return {.22f,.28f,.32f,1};
  }
}
[[nodiscard]] float smoothstep(float low,float high,float value){const auto t=std::clamp((value-low)/(high-low),0.f,1.f);return t*t*(3.f-2.f*t);}
[[nodiscard]] FloatColor lerp(FloatColor a,FloatColor b,float amount){return {a.r+(b.r-a.r)*amount,a.g+(b.g-a.g)*amount,a.b+(b.b-a.b)*amount,a.a+(b.a-a.a)*amount};}
[[nodiscard]] float fract(float value){return value-std::floor(value);}
[[nodiscard]] float hash13(Vector3 cell,float seed){auto q=Vector3{fract(cell.x*.1031f),fract(cell.y*.11369f),fract(cell.z*.13787f)};const auto amount=q.x*(q.y+19.19f+seed*.071f)+q.y*(q.z+19.19f+seed*.071f)+q.z*(q.x+19.19f+seed*.071f);q.x+=amount;q.y+=amount;q.z+=amount;return fract((q.x+q.y)*q.z);}
[[nodiscard]] float value_noise(Vector3 point,float seed){const auto ix=std::floor(point.x),iy=std::floor(point.y),iz=std::floor(point.z);auto x=fract(point.x),y=fract(point.y),z=fract(point.z);x=x*x*(3.f-2.f*x);y=y*y*(3.f-2.f*y);z=z*z*(3.f-2.f*z);const auto at=[&](float dx,float dy,float dz){return hash13({ix+dx,iy+dy,iz+dz},seed);};const auto z0=std::lerp(std::lerp(at(0,0,0),at(1,0,0),x),std::lerp(at(0,1,0),at(1,1,0),x),y),z1=std::lerp(std::lerp(at(0,0,1),at(1,0,1),x),std::lerp(at(0,1,1),at(1,1,1),x),y);return std::lerp(z0,z1,z);}
[[nodiscard]] float terrain_variation(Vector3 normal,float seed){const Vector3 offset{seed*.037f,seed*-.053f,seed*.071f};const auto continental=value_noise({normal.x*2.15f+offset.x,normal.y*2.15f+offset.y,normal.z*2.15f+offset.z},seed),regional=value_noise({normal.x*6.4f+offset.y*1.7f,normal.y*6.4f+offset.z*1.7f,normal.z*6.4f+offset.x*1.7f},seed),mineral=value_noise({normal.x*17.3f+offset.z*2.3f,normal.y*17.3f+offset.x*2.3f,normal.z*17.3f+offset.y*2.3f},seed),ridge=1.f-std::abs(regional*2.f-1.f);return .54f+continental*.27f+ridge*.18f+mineral*.09f;}
[[nodiscard]] FloatColor sample(const RgbaImage &source,float u,float v,bool repeat_x){
  if(repeat_x)u-=std::floor(u);else u=std::clamp(u,0.f,1.f);v=std::clamp(v,0.f,1.f);
  const auto width=source.width(),height=source.height();const auto fx=u*static_cast<float>(width)-.5f,fy=v*static_cast<float>(height)-.5f;const auto floor_x=static_cast<int>(std::floor(fx)),floor_y=static_cast<int>(std::floor(fy));const auto tx=fx-std::floor(fx),ty=fy-std::floor(fy);
  const auto wrap=[&](int x){x%=width;if(x<0)x+=width;return x;};const auto x0=repeat_x?wrap(floor_x):std::clamp(floor_x,0,width-1),x1=repeat_x?wrap(floor_x+1):std::clamp(floor_x+1,0,width-1),y0=std::clamp(floor_y,0,height-1),y1=std::clamp(floor_y+1,0,height-1);
  const auto pixel=[&](int x,int y){const auto index=(static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x))*4u;const auto &bytes=source.pixels();return FloatColor{bytes[index]/255.f,bytes[index+1]/255.f,bytes[index+2]/255.f,bytes[index+3]/255.f};};
  return lerp(lerp(pixel(x0,y0),pixel(x1,y0),tx),lerp(pixel(x0,y1),pixel(x1,y1),tx),ty);
}
[[nodiscard]] std::uint8_t channel(float value){return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.f,1.f)*255.f));}
[[nodiscard]] std::shared_ptr<const RgbaImage> generate_disc(const SystemBodyAppearance &appearance,const RgbaImage *source,std::optional<SourceDisc> crop,int lighting_step){
  const auto resolution=source?256:96;std::vector<std::uint8_t> pixels(static_cast<std::size_t>(resolution)*resolution*4u);const auto angle=static_cast<float>(lighting_step)/4096.f;float light_x=std::cos(angle)*.85f,light_y=std::sin(angle)*.85f,light_z=.53f;const auto light_length=std::sqrt(light_x*light_x+light_y*light_y+light_z*light_z);light_x/=light_length;light_y/=light_length;light_z/=light_length;const auto base=class_color(appearance.visual_class);const auto surface_seed=(appearance.deterministic_seed&255u)*.137f;const auto edge=2.16f/static_cast<float>(resolution);
  for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x){const auto px=((x+.5f)/resolution-.5f)*2.f*1.08f,py=((y+.5f)/resolution-.5f)*2.f*1.08f;const auto radius=std::hypot(px,py);auto coverage=1.f-smoothstep(1.f-edge,1.f+edge,radius);if(coverage<=0.f)continue;const auto divisor=std::max(1.f,radius),nx=px/divisor,ny=py/divisor,nz=std::sqrt(std::max(0.f,1.f-nx*nx-ny*ny));auto color=base;
    if(source){float u{},v{};if(crop){const auto safe_x=std::max(.001f,crop->radius_x-.5f/source->width()),safe_y=std::max(.001f,crop->radius_y-.5f/source->height());u=crop->center_x+nx*safe_x;v=crop->center_y+ny*safe_y;}else{u=.5f+std::atan2(nx,nz)/(2.f*std::numbers::pi_v<float>);v=.5f+std::asin(std::clamp(ny,-1.f,1.f))/std::numbers::pi_v<float>;}color=sample(*source,u,v,!crop);coverage*=color.a;
    }else{const Vector3 normal{nx,ny,nz};float variation{};if(appearance.visual_class==NativeSystemBodyVisualClass::gas_giant||appearance.visual_class==NativeSystemBodyVisualClass::ice_giant){const auto drift=value_noise({normal.x*5.f+surface_seed,normal.y*5.f+surface_seed,normal.z*5.f+surface_seed},surface_seed)*7.f;variation=.72f+.28f*std::sin(normal.y*65.f+drift);}else{variation=terrain_variation(normal,surface_seed);if(appearance.visual_class==NativeSystemBodyVisualClass::oceanic)variation=std::lerp(.82f,variation,.42f);}color.r*=variation;color.g*=variation;color.b*=variation;}
    if(!crop){const auto incidence=nx*light_x+ny*light_y+nz*light_z,daylight=smoothstep(-.10f,.16f,incidence),illumination=.025f+daylight*(.17f+std::max(incidence,0.f)*.91f);color.r*=illumination;color.g*=illumination;color.b*=illumination;}
    const auto index=(static_cast<std::size_t>(y)*resolution+static_cast<std::size_t>(x))*4u;pixels[index]=channel(color.r);pixels[index+1]=channel(color.g);pixels[index+2]=channel(color.b);pixels[index+3]=channel(coverage);
  }
  return RgbaImage::create(resolution,resolution,std::move(pixels));
}
} // namespace

struct NativePlanetDiscAssets::Storage {
  struct Key {std::uint64_t generation{};int body_id{};bool fully_surveyed{};NativeSystemBodyVisualClass visual_class{};std::optional<std::string> texture_key;std::uint32_t seed{};int lighting_step{};bool operator==(const Key&)const=default;};
  struct Entry {Key key;std::shared_ptr<const RgbaImage> image;std::uint64_t last_use{};};
  struct Pending {Key key;stellar::native_map::ImagePreparationQueue::Ticket ticket;};
  explicit Storage(std::filesystem::path root){if(root.empty())throw std::invalid_argument("A Sol appearance asset directory is required.");asset_root=std::filesystem::absolute(std::move(root));}
  std::filesystem::path asset_root;std::thread::id owner{std::this_thread::get_id()};std::optional<std::uint64_t> generation;std::vector<Entry> cache;std::vector<Pending> pending;std::shared_ptr<stellar::native_map::ImagePreparationQueue> preparation;std::size_t bytes{};std::uint64_t use{},decodes{},generated{};
  void require_owner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Planet disc assets must be used on their owner thread.");}
  void clear_pending()noexcept{pending.clear();}
  void bind(std::uint64_t value){if(generation&&value<*generation)throw std::invalid_argument("A stale campaign generation cannot replace planet disc assets.");if(!generation||*generation!=value){clear_pending();cache.clear();bytes=0;generation=value;}}
  void evict(std::size_t incoming){if(incoming>maximum_planet_disc_bytes)throw std::length_error("A generated planet disc exceeds the cache byte limit.");while(cache.size()>=maximum_planet_disc_entries||bytes+incoming>maximum_planet_disc_bytes){const auto oldest=std::ranges::min_element(cache,{},&Entry::last_use);if(oldest==cache.end())break;bytes-=oldest->image->byte_size();cache.erase(oldest);}}
  void cache_image(Key key,std::shared_ptr<const RgbaImage> result){evict(result->byte_size());cache.push_back({std::move(key),std::move(result),++use});bytes+=cache.back().image->byte_size();++generated;}
  void collect_ready(){for(std::size_t index{};index<pending.size();){if(!pending[index].ticket.ready()){++index;continue;}auto key=std::move(pending[index].key);auto ticket=std::move(pending[index].ticket);pending.erase(pending.begin()+static_cast<std::ptrdiff_t>(index));auto result=ticket.take();if(key.texture_key)++decodes;cache_image(std::move(key),std::move(result));}}
};

NativePlanetDiscAssets::NativePlanetDiscAssets(std::filesystem::path root):storage_(std::make_unique<Storage>(std::move(root))){}
NativePlanetDiscAssets::~NativePlanetDiscAssets()=default;
namespace {
void validate(const SystemBodyAppearance &appearance){
  if(appearance.body_id<0)throw std::invalid_argument("Planet disc body identity must be nonnegative.");if(!std::isfinite(appearance.lighting_longitude))throw std::invalid_argument("Planet disc lighting must be finite.");if(!appearance.fully_surveyed&&!unknown_class(appearance.visual_class))throw std::invalid_argument("Reconnaissance bodies cannot request a known visual class.");if(appearance.texture_key&&(!appearance.fully_surveyed||unknown_class(appearance.visual_class)))throw std::invalid_argument("Observer-hidden bodies cannot request a Sol appearance asset.");if(appearance.texture_key&&!canonical_key(*appearance.texture_key))throw std::invalid_argument("The observer-safe Sol appearance key is not approved.");
}
int lighting_step_for(const SystemBodyAppearance &appearance){return static_cast<int>(std::lround(std::remainder(appearance.lighting_longitude,2.f*std::numbers::pi_v<float>)*4096.f));}
std::shared_ptr<const RgbaImage> prepare_disc(SystemBodyAppearance appearance,std::filesystem::path root,int lighting_step){
  std::shared_ptr<const RgbaImage> source;if(appearance.texture_key){const auto path=root/(*appearance.texture_key+".jpg");try{source=stellar::native_map::decode_rgba_image(path);}catch(const std::exception &error){const auto value=path.u8string();throw std::runtime_error("Planet appearance asset failed to decode: "+std::string(reinterpret_cast<const char*>(value.data()),value.size())+": "+error.what());}}
  return generate_disc(appearance,source.get(),appearance.texture_key?source_disc(*appearance.texture_key):std::nullopt,lighting_step);
}
}
std::shared_ptr<const RgbaImage> NativePlanetDiscAssets::image(const SystemBodyAppearance &appearance){
  storage_->require_owner();storage_->bind(appearance.campaign_generation);validate(appearance);if(unknown_class(appearance.visual_class))return {};const auto lighting_step=lighting_step_for(appearance);Storage::Key key{appearance.campaign_generation,appearance.body_id,appearance.fully_surveyed,appearance.visual_class,appearance.texture_key,appearance.deterministic_seed,lighting_step};if(const auto found=std::ranges::find(storage_->cache,key,&Storage::Entry::key);found!=storage_->cache.end()){found->last_use=++storage_->use;return found->image;}
  if(const auto pending=std::ranges::find(storage_->pending,key,&Storage::Pending::key);pending!=storage_->pending.end())storage_->pending.erase(pending);
  const auto result=prepare_disc(appearance,storage_->asset_root,lighting_step);if(appearance.texture_key)++storage_->decodes;storage_->cache_image(std::move(key),result);return result;
}
void NativePlanetDiscAssets::use_background_preparation(std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue){storage_->require_owner();storage_->clear_pending();storage_->preparation=std::move(queue);}
std::shared_ptr<const RgbaImage> NativePlanetDiscAssets::request_image(const SystemBodyAppearance &appearance){
  storage_->require_owner();storage_->bind(appearance.campaign_generation);validate(appearance);if(unknown_class(appearance.visual_class))return {};storage_->collect_ready();const auto lighting_step=lighting_step_for(appearance);Storage::Key key{appearance.campaign_generation,appearance.body_id,appearance.fully_surveyed,appearance.visual_class,appearance.texture_key,appearance.deterministic_seed,lighting_step};if(const auto found=std::ranges::find(storage_->cache,key,&Storage::Entry::key);found!=storage_->cache.end()){found->last_use=++storage_->use;return found->image;}
  if(!storage_->preparation)return image(appearance);if(std::ranges::find(storage_->pending,key,&Storage::Pending::key)!=storage_->pending.end())return {};
  auto copied=appearance;auto root=storage_->asset_root;const auto reservation=256u*256u*4u;auto ticket=storage_->preparation->submit(reservation,[copied=std::move(copied),root=std::move(root),lighting_step]{return prepare_disc(copied,root,lighting_step);});if(!ticket)return {};storage_->pending.push_back({std::move(key),std::move(*ticket)});return {};
}
void NativePlanetDiscAssets::discard_campaign()noexcept{storage_->clear_pending();storage_->cache.clear();storage_->bytes=0;storage_->generation.reset();}
std::size_t NativePlanetDiscAssets::cache_entries()const noexcept{return storage_->cache.size();}std::size_t NativePlanetDiscAssets::cache_bytes()const noexcept{return storage_->bytes;}std::uint64_t NativePlanetDiscAssets::source_decode_count()const noexcept{return storage_->decodes;}std::uint64_t NativePlanetDiscAssets::generated_disc_count()const noexcept{return storage_->generated;}
std::size_t NativePlanetDiscAssets::pending_count()const noexcept{return storage_->pending.size();}
} // namespace stellar::native_system_ui
