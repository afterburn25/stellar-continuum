#include <stellar/engine/asset_registry.hpp>
#include "native_phenomena.hpp"
#include "native_menu_style.hpp"
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/engine/foundation.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace stellar::native_phenomena {
using namespace stellar::core;
using namespace stellar::engine;
namespace {
constexpr int local_width=1536,local_height=864;
constexpr std::size_t art_budget=96u*1024u*1024u;
std::uint8_t byte(double v){return static_cast<std::uint8_t>(std::clamp(std::lround(v),0l,255l));}
double fade(std::chrono::steady_clock::time_point start){return std::clamp(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/.65,0.,1.);}
const GalaxyPhenomenon& region(const GalaxyPhenomena& f,std::uint32_t id){return *std::ranges::find(f.regions,id,&GalaxyPhenomenon::id);}
struct LocalLayer{GalaxyPhenomenon region;PhenomenonOverlap overlap;PhenomenonVisualAsset asset;std::shared_ptr<const RgbaImage> texture;};
std::array<double,4> sample(const RgbaImage& image,double u,double v){
  if(u<0||v<0||u>1||v>1)return {};
  const double px=u*(image.width()-1),py=v*(image.height()-1);const int x=static_cast<int>(px),y=static_cast<int>(py);const double fx=px-x,fy=py-y;
  std::array<double,4> color{};double alpha=0;
  for(int j=0;j<2;++j)for(int i=0;i<2;++i){const auto at=(static_cast<std::size_t>(std::min(y+j,image.height()-1))*image.width()+std::min(x+i,image.width()-1))*4;const double weight=(i?fx:1-fx)*(j?fy:1-fy),a=image.pixels()[at+3]/255.;alpha+=a*weight;for(int k=0;k<3;++k)color[k]+=image.pixels()[at+k]*a*weight;}
  if(alpha>0)for(int k=0;k<3;++k)color[k]/=alpha;color[3]=alpha;return color;
}
std::shared_ptr<const RgbaImage> composite_local(const SystemPhenomenonContext& c,const std::vector<LocalLayer>& layers){
  std::vector<std::uint8_t> pixels(local_width*local_height*4u);
  double total=0;for(const auto& layer:layers)if(!layer.asset.obscuring)total+=.65*std::sqrt(layer.overlap.intensity*layer.region.opacity);
  const double normalization=total>0?std::min(1.,.28/total):0;
  struct Crop{Point center;double sx,sy,cs,sn,weight,mirror;bool dark;};std::vector<Crop> crops;
  for(const auto& layer:layers){const auto mapping=phenomenon_art_mapping(layer.region,layer.asset);const auto uv=mapping.uv(c.world_x,c.world_y);DeterministicRandom rng(c.local_seed^layer.region.shape.seed);
    const double crop=.30+.04*rng.unit_double();
    crops.push_back({uv,crop,crop*local_height/local_width*layer.asset.aspect_ratio,std::cos(layer.region.shape.rotation),std::sin(layer.region.shape.rotation),layer.asset.obscuring?4*layer.overlap.density*layer.region.opacity:.65*std::sqrt(layer.overlap.intensity*layer.region.opacity)*normalization,layer.region.mirrored?-1.:1.,layer.asset.obscuring});
  }
  for(int y=0;y<local_height;++y)for(int x=0;x<local_width;++x){double alpha=0;std::array<double,3> rgb{};
    for(std::size_t i=0;i<layers.size();++i){const auto& crop=crops[i];const double dx=(x/static_cast<double>(local_width)-.5)*crop.sx,dy=(y/static_cast<double>(local_height)-.5)*crop.sy;
      const auto color=sample(*layers[i].texture,crop.center.x+crop.mirror*(crop.cs*dx+crop.sn*dy),crop.center.y-crop.sn*dx+crop.cs*dy);
      const auto a=crop.dark?1-std::exp(-color[3]*crop.weight):color[3]*crop.weight;for(int k=0;k<3;++k)rgb[k]=rgb[k]*(1-a)+color[k]*a;alpha=alpha+(1-alpha)*a;
    }
    const auto at=(static_cast<std::size_t>(y)*local_width+x)*4;if(alpha>0)for(int k=0;k<3;++k)pixels[at+k]=byte(rgb[k]/alpha);pixels[at+3]=byte(std::min(.985,alpha)*255);
  }
  return RgbaImage::create(local_width,local_height,std::move(pixels));
}
}
DecalMapping phenomenon_art_mapping(const GalaxyPhenomenon& r,const PhenomenonVisualAsset& a){
  // A uniform fit covers the saved geometry without stretching the source art.
  const double half_width=std::max(r.shape.extent_x,r.shape.extent_y*a.aspect_ratio);
  return {r.shape.x,r.shape.y,half_width,half_width/a.aspect_ratio,r.shape.rotation,r.mirrored};
}
std::vector<PhenomenonOverlap> local_art_overlaps(const GalaxyPhenomena& f,const SystemPhenomenonContext& c){
  std::vector<PhenomenonOverlap> selected;for(const auto& overlap:c.overlaps)if(phenomenon_art(f,region(f,overlap.id)).system_view_eligible)selected.push_back(overlap);
  // Composite the strongest three local backgrounds; all overlaps still affect
  // authoritative gameplay and diagnostics. This keeps local preparation bounded.
  if(selected.size()>3)selected.resize(3);return selected;
}
PhenomenonAtlasLayout phenomenon_atlas_layout(std::size_t count){const int tile=count<=12?512:count<=48?256:count<=80?192:128;const int columns=count<=12?4:count<=80?8:16;return {tile,columns,static_cast<int>((count+columns-1)/columns)};}
double visual_multiplier(const VisualOptions& options){return options.developer_percent>=0?std::clamp(options.developer_percent/100.,0.,1.):std::array{.5,1.,1.3}.at(static_cast<std::size_t>(std::clamp(options.density,0,2)));}
double local_visual_multiplier(const VisualOptions& options,double zoom,bool combat){const double camera_density=std::clamp(1.05-std::max(0.,zoom-.2)*.13,.55,1.);return std::min(1.,std::min(1.3,visual_multiplier(options))/1.3*camera_density*(combat?.38:1.));}
std::shared_ptr<const RgbaImage> make_phenomenon_atlas(const GalaxyPhenomena& f){
  // Developer-only density visualization, never visible nebula artwork.
  if(f.regions.empty())return {};const auto [tile,columns,rows]=phenomenon_atlas_layout(f.regions.size());const int width=columns*tile,height=rows*tile;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
  for(std::size_t index=0;index<f.regions.size();++index){const auto& r=f.regions[index];const double bound=std::max(r.shape.extent_x,r.shape.extent_y)*1.04;
    for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){const auto s=sample_region(r.shape,r.shape.x+((x+.5)/tile*2-1)*bound,r.shape.y+((y+.5)/tile*2-1)*bound);const auto at=(static_cast<std::size_t>(index/columns*tile+y)*width+index%columns*tile+x)*4;
      pixels[at]=byte(s.density*255);pixels[at+1]=byte((1-std::abs(s.density*2-1))*255);pixels[at+2]=byte((1-s.density)*255);pixels[at+3]=byte(s.density*255);
    }
  }return RgbaImage::create(width,height,std::move(pixels));
}
std::shared_ptr<const RgbaImage> make_local_environment(const GalaxyPhenomena& f,const SystemPhenomenonContext& c,const std::filesystem::path& root){
  std::vector<LocalLayer> layers;std::map<std::string,std::shared_ptr<const RgbaImage>> images;
  for(const auto& o:local_art_overlaps(f,c)){const auto& r=region(f,o.id);const auto& a=phenomenon_art(f,r);auto& image=images[a.asset_id];if(!image)image=prepare_decal_texture(*decode_rgba_image(root/a.path),2944,a.obscuring?DecalBlendProfile::Obscuring:DecalBlendProfile::Luminous);layers.push_back({r,o,a,image});}
  return composite_local(c,layers);
}
void NativePhenomena::use_assets(std::filesystem::path root){asset_root_=std::filesystem::absolute(std::move(root));for(const auto& a:phenomenon_art_catalog())if(!stellar::engine::resource_exists(asset_root_/a.path))throw std::runtime_error("Missing phenomenon artwork: "+a.filename);stellar::engine::log("phenomenon-art",phenomenon_art_inventory());}
std::size_t NativePhenomena::cache_bytes()const{std::size_t total=(atlas_?atlas_->byte_size():0)+(local_?local_->byte_size():0);for(const auto& [key,entry]:art_)if(entry.image)total+=entry.image->byte_size();return total;}
void NativePhenomena::bind(const GalaxyPhenomena* f){atlas_job_.reset();local_job_.reset();atlas_.reset();local_.reset();art_.clear();contexts_.clear();field_=f?std::optional{*f}:std::nullopt;local_system_=-1;ready_=!f||f->regions.empty();previous_system_=false;transition_=std::chrono::steady_clock::now();index_.clear();if(f){for(std::size_t i=0;i<f->regions.size();++i){const auto& s=f->regions[i].shape;const auto b=std::max(s.extent_x,s.extent_y)*1.04;index_.insert(i,{s.x-b,s.y-b,s.x+b,s.y+b});}stellar::engine::log("phenomenon-art",phenomenon_art_usage(*f));}}
const SystemPhenomenonContext& NativePhenomena::context(int id,double x,double y){auto found=contexts_.find(id);if(found==contexts_.end())found=contexts_.emplace(id,phenomenon_context(field(),x,y,id)).first;return found->second;}
void NativePhenomena::poll(){
  for(auto& [key,entry]:art_)if(entry.job&&entry.job->ready()){entry.image=entry.job->take();entry.job.reset();}
  if(atlas_job_&&atlas_job_->ready()){atlas_=atlas_job_->take();atlas_job_.reset();}
  if(local_job_&&local_job_->ready()){local_=local_job_->take();local_job_.reset();transition_=std::chrono::steady_clock::now();}
}
void NativePhenomena::collect_art(){
  ++frame_;poll();
  while(cache_bytes()>art_budget){auto oldest=art_.end();for(auto it=art_.begin();it!=art_.end();++it)if(it->second.image&&it->second.touched+2<frame_&&(oldest==art_.end()||it->second.touched<oldest->second.touched))oldest=it;
    if(oldest==art_.end())break;art_.erase(oldest);
  }
}
std::shared_ptr<const RgbaImage> NativePhenomena::request_art(const PhenomenonVisualAsset& a,int lod){
  auto& entry=art_[{a.asset_id,lod}];entry.touched=frame_;
  if(!entry.image&&!entry.job){const auto root=asset_root_;const auto bytes=static_cast<std::size_t>(lod)*static_cast<std::size_t>(std::lround(lod/a.aspect_ratio))*4;
    auto factory=[root,a,lod]{return prepare_decal_texture(*decode_rgba_image(root/a.path),lod,a.obscuring?DecalBlendProfile::Obscuring:DecalBlendProfile::Luminous);};
    if(queue_){std::size_t pending=0;for(const auto& [key,e]:art_)if(e.job)pending+=static_cast<std::size_t>(key.second)*static_cast<std::size_t>(std::lround(key.second/a.aspect_ratio))*4;
      while(cache_bytes()+pending+bytes>art_budget){auto oldest=art_.end();for(auto it=art_.begin();it!=art_.end();++it)if(it->second.image&&it->second.touched+2<frame_&&(oldest==art_.end()||it->second.touched<oldest->second.touched))oldest=it;if(oldest==art_.end())break;art_.erase(oldest);}
      if(cache_bytes()+pending+bytes<=art_budget)entry.job=queue_->submit(bytes,factory);
    }else entry.image=factory();
  }return entry.image;
}
void NativePhenomena::request_atlas(){if(!field_||field_->regions.empty()||atlas_)return;if(atlas_job_&&atlas_job_->ready()){atlas_=atlas_job_->take();atlas_job_.reset();return;}if(!atlas_job_){const auto f=*field_;if(queue_)atlas_job_=queue_->submit(24u*1024u*1024u,[f]{return make_phenomenon_atlas(f);});else atlas_=make_phenomenon_atlas(f);}}
void NativePhenomena::append_map(DrawList& out,const Camera& camera,int w,int h,const VisualOptions& options,const std::set<std::uint32_t>& surveyed){
  if(previous_system_){previous_system_=false;transition_=std::chrono::steady_clock::now();}collect_art();ready_=true;if(!field_||field_->regions.empty())return;
  if(options.heatmap){request_atlas();ready_=atlas_!=nullptr;}
  const auto [tile,columns,rows]=phenomenon_atlas_layout(field_->regions.size());(void)rows;
  const UiRect screen{0,0,static_cast<float>(w),static_cast<float>(h)};const double transition=.35+.65*fade(transition_);
  std::vector<TriangleMesh> batches;DrawList diagnostics;int full_detail_used=0;
  const auto corner=camera.unproject({0,0},w,h),end=camera.unproject({static_cast<float>(w),static_cast<float>(h)},w,h);
  for(const auto i:index_.query({corner.x,corner.y,end.x,end.y})){const auto& r=field_->regions[i];const auto& a=phenomenon_art(*field_,r);const auto mapping=phenomenon_art_mapping(r,a);const auto p=camera.project({r.shape.x,r.shape.y},w,h);const float extent=static_cast<float>(std::max(r.shape.extent_x,r.shape.extent_y)*1.04*camera.pixels_per_world);
    if(p.x+extent<0||p.y+extent<0||p.x-extent>w||p.y-extent>h||extent<.35f)continue;
    const UiRect dest{p.x-extent,p.y-extent,extent*2,extent*2};const bool known=surveyed.contains(r.id);
    const double far_opacity=extent<60?.85:1.;const double strength=std::min(.68,r.opacity*(.6+r.intensity)*visual_multiplier(options))*far_opacity*transition;
    if(options.heatmap&&atlas_){const UiRect source{static_cast<float>(i%columns*tile),static_cast<float>(i/columns*tile),static_cast<float>(tile),static_cast<float>(tile)};out.world.emplace_back(Image{atlas_,dest,source,{255,255,255,200},screen});}
    else{
      int lod=decal_lod_width(mapping.half_width*2*camera.pixels_per_world,options.density);if(lod==2944&&full_detail_used++>=3)lod=768;
      auto image=request_art(a,256);auto detailed=lod==256?image:request_art(a,lod);if(detailed)image=detailed;
      if(!image){ready_=false;continue;}if(!detailed)ready_=false;
      const auto bound=std::max(r.shape.extent_x,r.shape.extent_y);std::vector<const GalaxyPhenomenon*> neighbors;for(auto n:index_.query({r.shape.x-bound,r.shape.y-bound,r.shape.x+bound,r.shape.y+bound}))neighbors.push_back(&field_->regions[n]);
      auto mesh=masked_decal_mesh(image,dest,screen,{255,255,255,byte(strength*255)},[&](Point pixel){const auto world=camera.unproject(pixel,w,h);const auto density=sample_region(r.shape,world.x,world.y).density;double sum=0;for(const auto* other:neighbors)sum+=std::min(.68,other->opacity*(.6+other->intensity)*visual_multiplier(options))*std::min(1.,sample_region(other->shape,world.x,world.y).density*3.);return std::min(1.,density*3.)*std::min(1.,.68/std::max(.001,sum));},[&](Point pixel){const auto world=camera.unproject(pixel,w,h);return mapping.uv(world.x,world.y);});
      // Only adjacent matching textures merge; retain dark/luminous layer order.
      if(batches.empty()||!append_decal_batch(batches.back(),std::move(mesh)))batches.push_back(std::move(mesh));
    }
    if(options.bounds){const Color line{90,220,255,130};diagnostics.world.emplace_back(Line{{dest.x,dest.y},{dest.x+dest.width,dest.y},line});diagnostics.world.emplace_back(Line{{dest.x+dest.width,dest.y},{dest.x+dest.width,dest.y+dest.height},line});diagnostics.world.emplace_back(Line{{dest.x+dest.width,dest.y+dest.height},{dest.x,dest.y+dest.height},line});diagnostics.world.emplace_back(Line{{dest.x,dest.y+dest.height},{dest.x,dest.y},line});}
    if(options.labels||options.region_bias||options.membership||options.filenames)diagnostics.world.emplace_back(Text{p,r.designation+" "+((known||options.labels)?phenomenon_definition(r.type).name:"Uncharted cloud")+(options.filenames?" | "+a.filename:"")+(options.region_bias?" | "+std::string(stellar_region_name(r.affinity)):"")+(options.membership?" | systems "+std::to_string(r.systems_contained.size()):""),{152,230,247,255},12,420,screen});
  }
  for(auto& mesh:batches)out.world.emplace_back(std::move(mesh));
  for(auto& command:diagnostics.world)out.world.emplace_back(std::move(command));
}
void NativePhenomena::append_system(DrawList& out,int id,double x,double y,int w,int h,double zoom,const VisualOptions& options,bool combat){
  collect_art();if(!field_){ready_=true;return;}const auto& c=context(id,x,y);DeterministicRandom starfield(static_cast<std::uint64_t>(id)^0x53595354454dULL);
  if(options.background_stars)for(int i=0;i<220;++i){const Point at{static_cast<float>(starfield.unit_double()*w),static_cast<float>(starfield.unit_double()*h)};out.world.emplace_back(Circle{at,static_cast<float>(.35+starfield.unit_double()*.5),{160,186,211,static_cast<std::uint8_t>(50+starfield.unit_double()*85)}});}
  if(id!=local_system_){local_job_.reset();local_.reset();local_system_=id;transition_=std::chrono::steady_clock::now();}if(!previous_system_){previous_system_=true;transition_=std::chrono::steady_clock::now();}
  // A clear local sky must not keep loading/fading a previous system's cloud,
  // or reserve work for a fully invisible layer. Map phenomena remain intact.
  if(!options.local_nebula||options.local_opacity<=0){local_job_.reset();local_.reset();ready_=true;return;}
  const auto overlaps=local_art_overlaps(*field_,c);if(overlaps.empty()){ready_=true;return;}
  if(local_job_&&local_job_->ready()){local_=local_job_->take();local_job_.reset();transition_=std::chrono::steady_clock::now();}
  if(!local_&&!local_job_){std::vector<LocalLayer> layers;for(const auto& o:overlaps){const auto& r=region(*field_,o.id);const auto& a=phenomenon_art(*field_,r);if(auto image=request_art(a,2944))layers.push_back({r,o,a,image});}
    if(layers.size()==overlaps.size()){if(queue_)local_job_=queue_->submit(local_width*local_height*4u,[c,layers]{return composite_local(c,layers);});else local_=composite_local(c,layers);}
  }
  ready_=local_!=nullptr&&fade(transition_)>=1.;if(!local_)return;
  const auto opacity=byte(255*std::clamp(options.local_opacity,0.,1.)*local_visual_multiplier(options,zoom,combat)*fade(transition_));const UiRect screen{0,0,static_cast<float>(w),static_cast<float>(h)};
  // Aspect-preserving viewport crop, independent of system camera zoom.
  const float aspect=w/static_cast<float>(h);UiRect source{0,0,static_cast<float>(local_->width()),static_cast<float>(local_->height())};
  if(source.width/source.height>aspect){const auto width=source.height*aspect;source.x=(source.width-width)*.5f;source.width=width;}else{const auto height=source.width/aspect;source.y=(source.height-height)*.5f;source.height=height;}
  out.world.emplace_back(Image{local_,screen,source,{255,255,255,opacity},screen});
}
void NativePhenomena::inspect(DrawList& out,const Camera& camera,Point pointer,int w,int h,const std::set<std::uint32_t>& surveyed,bool developer,bool pinned)const{
  if(!field_||pointer.x<60||pointer.y<110||pointer.x>w-330||pointer.y>h-105)return;
  const auto p=camera.unproject(pointer,w,h);const auto c=phenomenon_context(field(),p.x,p.y);if(!c.dominant)return;
  const auto& r=region(*field_,*c.dominant);const bool known=developer||surveyed.contains(r.id);const auto& d=phenomenon_definition(r.type);
  std::ostringstream text;text<<(known?r.designation+" · "+d.name:"Uncharted interstellar cloud")<<'\n';
  if(known){text<<d.description<<"\nSensor range "<<std::fixed<<std::setprecision(0)<<c.effects.sensor*100<<"% · Survey effort "<<std::setprecision(2)<<c.effects.scanning<<"x\n";
    text<<"Radiation potential "<<std::setprecision(2)<<c.effects.hazard<<" · "<<r.systems_contained.size()<<" intersecting systems";
    if(developer)text<<"\nID "<<r.id<<" · "<<c.overlaps.size()<<" overlaps · edge "<<c.overlaps.front().edge_distance<<" ly\nDensity "<<c.overlaps.front().density<<" · shape seed "<<r.shape.seed;
  }else text<<"Survey a system inside this cloud to identify its environment.";
  const float s=std::clamp(h/1080.f,.75f,1.6f);const UiRect panel{pinned?70.f:pointer.x+18*s,pinned?h-295*s:pointer.y+18*s,380*s,(developer?180.f:145.f)*s};
  auto bounded=panel;bounded.x=std::min(bounded.x,w-bounded.width-20);bounded.y=std::min(bounded.y,h-bounded.height-85*s);
  native_menu_style::panel(out,bounded,s);native_menu_style::text(out,{bounded.x+12*s,bounded.y+10*s,bounded.width-24*s,bounded.height-20*s},text.str(),static_cast<int>(15*s));
}
}

