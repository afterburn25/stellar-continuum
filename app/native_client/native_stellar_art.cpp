#include "native_stellar_art.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace stellar::native_stellar {
using namespace stellar::native_map;
namespace {
std::uint8_t byte(float v){return static_cast<std::uint8_t>(std::clamp(std::lround(v),0l,255l));}
float fade(float low,float high,float v){const auto t=std::clamp((v-low)/(high-low),0.f,1.f);return t*t*(3-2*t);}
}
Artwork::Artwork(std::filesystem::path root):root_(std::move(root)/"assets/visual/stellar") {
  std::ifstream file(root_/"manifest.json");if(!file)throw std::runtime_error("Stellar artwork manifest is missing");nlohmann::json j;file>>j;
  for(const auto& e:j.at("objects")) {
    Asset a;a.id=e.at("objectType");a.close_asset=e.at("closeAsset");if(!e.at("distanceAsset").is_null())a.distance_asset=e.at("distanceAsset");
    a.fallback=e.at("fallbackDistanceMode");a.luminosity=e.at("luminosityProfile");a.scale=e.at("visualScaleProfile");const auto rgb=e.at("colorProfile").get<std::array<std::uint8_t,3>>();a.color={rgb[0],rgb[1],rgb[2],255};
    for(const auto& path:{a.close_asset,a.distance_asset})if(!path.empty()&&(std::filesystem::path(path).filename()!=path||!std::filesystem::exists(root_/path)))throw std::runtime_error("Invalid stellar artwork path");
    if(!assets_.emplace(a.id,a).second)throw std::runtime_error("Duplicate stellar artwork mapping");
  }
}
std::shared_ptr<const RgbaImage> Artwork::prepare(const std::filesystem::path& root,Asset a,bool close) {
  const bool supplied=close||!a.distance_asset.empty();std::shared_ptr<const RgbaImage> source;
  if(supplied)source=decode_rgba_image(root/(close?a.close_asset:a.distance_asset));
  const int size=close?1024:128;std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size)*size*4);
  const bool black=a.fallback=="gravitational-sensor"||a.fallback=="accretion-disk"||a.fallback=="bipolar-accretion";
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){
    const float nx=(x+.5f)/size*2-1,ny=(y+.5f)/size*2-1,r=std::hypot(nx,ny);const auto i=(static_cast<std::size_t>(y)*size+x)*4;
    const float edge=1-fade(.83f,.995f,r);if(edge==0)continue;
    float alpha=0,red=a.color.r,green=a.color.g,blue=a.color.b;
    if(source){
      // Center-crop distance art to make its luminous point occupy the sprite; never retain its baked border.
      const float span=close?1.f:.32f;
      const float side=static_cast<float>(std::max(source->width(),source->height()));
      const int sx=static_cast<int>(source->width()*.5f+nx*.5f*span*side),sy=static_cast<int>(source->height()*.5f+ny*.5f*span*side);
      if(sx<0||sy<0||sx>=source->width()||sy>=source->height())continue;
      const auto q=(static_cast<std::size_t>(sy)*source->width()+sx)*4;const auto& p=source->pixels();
      const float peak=std::max({p[q],p[q+1],p[q+2]});const float light=std::max(0.f,peak-9.f);
      const float opacity=light/246.f;alpha=opacity*edge;
      if(light>0){red=std::max(0.f,p[q]-9.f)/opacity;green=std::max(0.f,p[q+1]-9.f)/opacity;blue=std::max(0.f,p[q+2]-9.f)/opacity;}
    }else{
      const float core=std::exp(-std::pow(r/.135f,4.f)),halo=std::exp(-r*r/.055f)*.15f;
      const float ray=std::max(std::exp(-nx*nx/.00024f),std::exp(-ny*ny/.00024f))*std::pow(std::max(0.f,1-r),2.f)*.65f;
      alpha=std::max({core,halo,ray})*edge;
      if(a.fallback=="dim-point")alpha*=.24f;
      if(black){const float ellipse=std::hypot(nx,ny*2.8f);alpha=std::exp(-std::pow((ellipse-.30f)/.055f,2.f))*edge*(a.fallback=="gravitational-sensor"?.35f:1.f);if(r<.10f){red=green=blue=0;alpha=1;}}
      if(a.fallback=="polar-pulse"||a.fallback=="bipolar-accretion")alpha=std::max(alpha,std::exp(-nx*nx/.001f)*std::max(0.f,1-std::abs(ny))*edge*.8f);
      if(a.fallback=="wind-halo"||a.fallback=="magnetic-halo")alpha=std::max(alpha,std::exp(-std::pow((r-.32f)/.08f,2.f))*.14f*edge);
      const float white=core*.85f;if(!black){red=red*(1-white)+255*white;green=green*(1-white)+255*white;blue=blue*(1-white)+255*white;}
    }
    pixels[i]=byte(red);pixels[i+1]=byte(green);pixels[i+2]=byte(blue);pixels[i+3]=byte(alpha*255);
  }
  if(close&&black){
    // Keep the enclosed event horizon opaque without drawing a small artificial
    // black dot or retaining the source's rectangular black background.
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(size)*size);
    std::vector<int> interior;interior.push_back(size/2*size+size/2);visited[static_cast<std::size_t>(interior.front())]=1;
    bool escapes=false;
    for(std::size_t cursor=0;cursor<interior.size()&&!escapes;++cursor){
      const int index=interior[cursor],x=index%size,y=index/size;
      if(x==0||y==0||x==size-1||y==size-1||interior.size()>visited.size()/3){escapes=true;break;}
      for(int next:{index-1,index+1,index-size,index+size})if(!visited[static_cast<std::size_t>(next)]&&pixels[static_cast<std::size_t>(next)*4+3]<16){visited[static_cast<std::size_t>(next)]=1;interior.push_back(next);}
    }
    if(!escapes)for(int index:interior){const auto i=static_cast<std::size_t>(index)*4;pixels[i]=pixels[i+1]=pixels[i+2]=0;pixels[i+3]=255;}
  }
  return RgbaImage::create(size,size,std::move(pixels));
}
void Artwork::collect(){
  for(auto it=pending_.begin();it!=pending_.end();){if(!it->ticket.ready()){++it;continue;}auto image=it->ticket.take();auto& cache=it->close?close_:distance_;cache.insert_or_assign(it->key,Cached{std::move(image),++use_});it=pending_.erase(it);}
  while(close_.size()>maximum_close_images){auto oldest=std::min_element(close_.begin(),close_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});close_.erase(oldest);}
}
std::shared_ptr<const RgbaImage> Artwork::request(const Asset& a,bool close){
  auto& cache=close?close_:distance_;auto found=cache.find(a.id);if(found!=cache.end()){found->second.used=++use_;return found->second.image;}
  if(std::any_of(pending_.begin(),pending_.end(),[&](const auto& p){return p.key==a.id&&p.close==close;}))return {};
  if(queue_){if(pending_.size()>=maximum_pending)return {};auto root=root_;auto ticket=queue_->submit(close?1024u*1024u*4u:128u*128u*4u,[root,a,close]{return prepare(root,a,close);});if(ticket)pending_.push_back({a.id,close,std::move(*ticket)});return {};}
  auto image=prepare(root_,a,close);cache.emplace(a.id,Cached{image,++use_});collect();return image;
}
void Artwork::append(DrawList& out,Point p,float radius,const std::string& id,double seconds,std::optional<UiRect> clip){
  collect();const auto it=assets_.find(id);if(it==assets_.end())throw std::invalid_argument("Unmapped observed stellar type: "+id);const auto& a=it->second;
  if(!std::isfinite(radius)||radius<=0)return;
  if(clip&&(p.x+radius*4<clip->x||p.y+radius*4<clip->y||p.x-radius*4>clip->x+clip->width||p.y-radius*4>clip->y+clip->height))return;
  const float close_weight=fade(18,46,radius);
  bool admit_close=close_weight>0;
  if(admit_close&&frame_budgeted_&&std::find(visible_close_.begin(),visible_close_.end(),id)==visible_close_.end()){
    admit_close=visible_close_.size()<maximum_close_images;
    if(admit_close)visible_close_.push_back(id);
  }
  // A crowded, magnified map must not repeatedly evict and re-decode more
  // close types than its cache can retain. Remaining types keep their LOD.
  auto distant=request(a,false);auto near=admit_close?request(a,true):nullptr;
  // Screen-size blending also waits for asynchronous artwork to fade in. A quick
  // wheel movement must not cause a high-detail texture to pop in when decoding ends.
  float ready_weight=0;
  if(near){
    auto& cached=close_.at(a.id);
    if(!cached.first_visible||seconds<*cached.first_visible)cached.first_visible=seconds;
    ready_weight=fade(0,.45f,static_cast<float>(seconds-*cached.first_visible));
  }
  const float blend=close_weight*ready_weight;
  const float pulse=a.fallback=="polar-pulse"?.8f+.2f*static_cast<float>(std::sin(seconds*2.4)):1;
  if(distant&&blend<1){const float extent=radius*4;out.world.emplace_back(Image{distant,{p.x-extent,p.y-extent,extent*2,extent*2},{},{255,255,255,byte(255*(1-blend)*pulse)},clip});}
  if(near&&blend>0){const float extent=radius*1.45f;out.world.emplace_back(Image{near,{p.x-extent,p.y-extent,extent*2,extent*2},{},{255,255,255,byte(255*blend)},clip});}
  if(!distant&&blend<1){auto color=a.color;color.a=byte(255*(1-blend));out.world.emplace_back(Circle{p,radius*.8f,color});}
}
}
