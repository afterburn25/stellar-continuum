#include "native_celestial_appearance.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_system_ui;
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
const Image&image_at(const DrawList&draw,std::size_t index){const auto*value=std::get_if<Image>(&draw.world.at(index));if(!value)throw std::runtime_error("expected ordered image command");return *value;}
std::vector<std::shared_ptr<const RgbaImage>> images_in_order(const DrawList&draw){std::vector<std::shared_ptr<const RgbaImage>> images;for(const auto&command:draw.world)if(const auto*image=std::get_if<Image>(&command))images.push_back(image->resource);return images;}
std::uint8_t alpha(const RgbaImage&image,int x,int y){return image.pixels().at(static_cast<std::size_t>((y*image.width()+x)*4+3));}
namespace fs=std::filesystem;
void write_capture(const fs::path&directory,std::ofstream&profile,std::string_view name,const RgbaImage&image,std::size_t generated,const std::chrono::steady_clock::duration elapsed){
  const auto raw=directory/(std::string(name)+".rgba");
  std::ofstream output(raw,std::ios::binary|std::ios::trunc);
  if(!output)throw std::runtime_error("could not open celestial capture "+raw.string());
  const auto&pixels=image.pixels();
  output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
  if(!output)throw std::runtime_error("could not write celestial capture "+raw.string());
  const auto milliseconds=std::chrono::duration<double,std::milli>(elapsed).count();
  profile<<name<<" width="<<image.width()<<" height="<<image.height()<<" bytes="<<pixels.size()<<" generated_resources="<<generated<<" cold_ms="<<milliseconds<<'\n';
  std::cout<<"celestial capture "<<name<<" "<<image.width()<<'x'<<image.height()<<" generated="<<generated<<" cold_ms="<<milliseconds<<'\n';
}
void capture_stellar_disc(const fs::path&directory,std::ofstream&profile,std::string_view name,NativeStellarDiscAppearance style){
  NativeCelestialAppearanceRenderer renderer;DrawList draw;
  const auto started=std::chrono::steady_clock::now();
  renderer.append_stellar_disc(draw,{128,128},32,style,0,UiRect{0,0,256,256});
  write_capture(directory,profile,name,*image_at(draw,0).resource,renderer.stats().generated_resources,std::chrono::steady_clock::now()-started);
}
void capture_cold_resources(const fs::path&directory){
  fs::create_directories(directory);
  std::ofstream profile(directory/"profile.log",std::ios::trunc);
  if(!profile)throw std::runtime_error("could not open celestial capture profile");
  profile<<"native celestial appearance cold resource capture\n";
  capture_stellar_disc(directory,profile,"star_warm_seed17",{{255,220,130,255},false,17});
  capture_stellar_disc(directory,profile,"star_blue_seed9001",{{110,170,255,255},false,9001});
  capture_stellar_disc(directory,profile,"star_red_seed424242",{{255,105,60,255},false,424242});
  capture_stellar_disc(directory,profile,"black_hole_seed1",{{126,92,178,255},true,1});
  NativeCelestialAppearanceRenderer renderer;DrawList back,front;
  auto started=std::chrono::steady_clock::now();renderer.append_ring_back(back,{128,128},32,UiRect{0,0,256,256});
  write_capture(directory,profile,"ring_back",*image_at(back,0).resource,renderer.stats().generated_resources,std::chrono::steady_clock::now()-started);
  started=std::chrono::steady_clock::now();renderer.append_ring_front(front,{128,128},32,UiRect{0,0,256,256});
  write_capture(directory,profile,"ring_front",*image_at(front,0).resource,renderer.stats().generated_resources,std::chrono::steady_clock::now()-started);
}
class PreparationGate final {
 public:
  struct State final {std::mutex mutex;std::condition_variable started_ready,released;bool started{},released_value{};};
  ~PreparationGate(){release();}
  [[nodiscard]]std::shared_ptr<State> state()const{return state_;}
  static void wait(const std::shared_ptr<State>&state){std::unique_lock lock(state->mutex);state->started=true;state->started_ready.notify_all();state->released.wait(lock,[&]{return state->released_value;});}
  void wait_started()const{std::unique_lock lock(state_->mutex);require(state_->started_ready.wait_until(lock,std::chrono::steady_clock::now()+std::chrono::seconds(15),[this]{return state_->started;}),"celestial preparation worker did not reach its gate");}
  void release()const{{std::lock_guard lock(state_->mutex);state_->released_value=true;}state_->released.notify_all();}
 private:std::shared_ptr<State> state_{std::make_shared<State>()};
};
void wait_for_queue(const ImagePreparationQueue&queue){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(queue.outstanding_jobs()!=0&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();require(queue.outstanding_jobs()==0,"image preparation worker did not drain");}
void wait_for_ticket(const ImagePreparationQueue::Ticket&ticket){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!ticket.ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();require(ticket.ready(),"celestial preparation ticket did not become ready");}
void async_preparation_contract(){
  auto preparation=std::make_shared<ImagePreparationQueue>();PreparationGate gate;auto held=*preparation->submit(4,[gate_state=gate.state()]{PreparationGate::wait(gate_state);return RgbaImage::create(1,1,{1,2,3,255});});gate.wait_started();
  NativeCelestialAppearanceRenderer renderer;renderer.use_background_preparation(preparation);renderer.begin_frame();const NativeStellarDiscAppearance style{{255,220,130,255},false,17};DrawList first;renderer.append_stellar_disc(first,{100,100},20,style,0);renderer.append_stellar_disc(first,{100,100},20,style,0);renderer.append_ring_back(first,{100,100},20);renderer.append_ring_front(first,{100,100},20);
  require(first.world.size()==2&&std::holds_alternative<Circle>(first.world.front())&&std::holds_alternative<Circle>(first.world.at(1)),"unprepared stellar discs did not return bounded fallback geometry");require(renderer.preparation_pending()&&preparation->outstanding_jobs()==4,"duplicate celestial preparation was not coalesced while the worker was held");
  renderer.clear();require(renderer.stats().cached_resources==0,"clear retained prepared celestial resources");gate.release();wait_for_ticket(held);(void)held.take();wait_for_queue(*preparation);require(renderer.stats().cached_resources==0&&renderer.stats().generated_resources==0,"clear allowed canceled stale preparation to repopulate the cache");
}
void saturated_preparation_matches_sync(){
  NativeCelestialAppearanceRenderer synchronous;const NativeStellarDiscAppearance style{{110,170,255,255},false,9001};DrawList expected; synchronous.append_stellar_disc(expected,{100,100},20,style,0);synchronous.append_ring_back(expected,{100,100},20);synchronous.append_ring_front(expected,{100,100},20);const auto expected_images=images_in_order(expected);require(expected_images.size()==3,"synchronous celestial fixture did not retain ordered source images");const auto star_pixels=expected_images[0]->pixels(),back_pixels=expected_images[1]->pixels(),front_pixels=expected_images[2]->pixels();
  auto preparation=std::make_shared<ImagePreparationQueue>(1,8u*1024u*1024u);PreparationGate gate;auto held=*preparation->submit(4,[gate_state=gate.state()]{PreparationGate::wait(gate_state);return RgbaImage::create(1,1,{1,2,3,255});});gate.wait_started();NativeCelestialAppearanceRenderer renderer;renderer.use_background_preparation(preparation);renderer.begin_frame();DrawList saturated;renderer.append_stellar_disc(saturated,{100,100},20,style,0);renderer.append_ring_back(saturated,{100,100},20);renderer.append_ring_front(saturated,{100,100},20);require(saturated.world.size()==1&&std::holds_alternative<Circle>(saturated.world.front())&&renderer.preparation_pending(),"saturated image preparation did not use the stellar fallback");gate.release();wait_for_ticket(held);(void)held.take();
  DrawList resolved;std::vector<std::shared_ptr<const RgbaImage>> resolved_images;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(std::chrono::steady_clock::now()<deadline){renderer.begin_frame();resolved.world.clear();renderer.append_stellar_disc(resolved,{100,100},20,style,0);renderer.append_ring_back(resolved,{100,100},20);renderer.append_ring_front(resolved,{100,100},20);resolved_images=images_in_order(resolved);if(resolved_images.size()==3)break;std::this_thread::yield();}require(resolved_images.size()==3,"celestial preparation did not retry after saturation");require(resolved_images[0]->pixels()==star_pixels&&resolved_images[1]->pixels()==back_pixels&&resolved_images[2]->pixels()==front_pixels,"asynchronous celestial preparation changed source RGBA pixels");
}
void star_cache_and_transience(){NativeCelestialAppearanceRenderer renderer;NativeStellarDiscAppearance style{{255,220,130,255},false,17};DrawList first;double active=-1,quiet=-1;for(int i=0;i<300&&(active<0||quiet<0);++i){DrawList probe;renderer.append_stellar_disc(probe,{300,220},24,style,i*.1,UiRect{0,0,640,480});if(probe.world.size()>1)active=i*.1;else quiet=i*.1;}require(active>=0&&quiet>=0,"stellar prominence did not expose both active and quiet intervals");renderer.append_stellar_disc(first,{300,220},24,style,active,UiRect{0,0,640,480});require(std::holds_alternative<Image>(first.world.front()),"stellar disc was not an immutable image");require(first.world.size()<=37,"stellar flare geometry exceeded its command budget");DrawList clipped;renderer.append_stellar_disc(clipped,{1,10},24,style,active,UiRect{0,0,20,20});for(const auto&item:clipped.world)if(const auto*line=std::get_if<Line>(&item))require(line->from.x>=0&&line->from.x<=20&&line->from.y>=0&&line->from.y<=20&&line->to.x>=0&&line->to.x<=20&&line->to.y>=0&&line->to.y<=20,"stellar flare escaped its world clip");DrawList quiet_draw;renderer.append_stellar_disc(quiet_draw,{300,220},24,style,quiet,UiRect{0,0,640,480});require(quiet_draw.world.size()==1,"quiet stellar interval retained flare geometry");const auto initial=renderer.stats();require(initial.cached_resources==1&&initial.generated_resources==1,"stellar texture did not cache once");DrawList repeated;renderer.append_stellar_disc(repeated,{320,240},48,style,active,UiRect{0,0,640,480});require(renderer.stats().generated_resources==1&&image_at(first,0).resource==image_at(repeated,0).resource,"camera-only frame regenerated the stellar resource");DrawList second;style.deterministic_seed=9001;renderer.append_stellar_disc(second,{300,220},24,style,active,UiRect{0,0,640,480});require(renderer.stats().generated_resources==2,"a distinct system appearance was not cached separately");require(image_at(first,0).resource!=image_at(second,0).resource,"different system seed reused the same stellar surface");const auto&disc=*image_at(first,0).resource;require(disc.width()==1024&&disc.height()==1024,"stellar close-up tier lost its source resolution");require(alpha(disc,0,0)==0&&alpha(disc,disc.width()/2,disc.height()/2)>240,"stellar texture lacks transparent corona bounds or opaque photosphere");int darkest=765,brightest=0;for(int y=disc.height()*3/8;y<disc.height()*5/8;++y)for(int x=disc.width()*3/8;x<disc.width()*5/8;++x){const auto at=static_cast<std::size_t>((y*disc.width()+x)*4);const int value=disc.pixels()[at]+disc.pixels()[at+1]+disc.pixels()[at+2];darkest=std::min(darkest,value);brightest=std::max(brightest,value);}require(brightest-darkest>90,"stellar surface lost multi-scale granulation or spot contrast");}
void black_hole_and_limits(){NativeCelestialAppearanceRenderer renderer;DrawList draw;renderer.append_stellar_disc(draw,{20,20},8,{{126,92,178,255},true,1},2.0);require(draw.world.size()==1,"black hole emitted photospheric flares");const auto&image=*image_at(draw,0).resource;const auto at=static_cast<std::size_t>(((image.height()/2)*image.width()+image.width()/2)*4);require(image.pixels()[at]<20&&image.pixels()[at+1]<20&&alpha(image,image.width()/2,image.height()/2)>240,"black-hole horizon presentation is not dark and opaque");bool rejected=false;try{renderer.append_stellar_disc(draw,{0,0},std::numeric_limits<float>::infinity(),{{},false,0},0);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"nonfinite stellar geometry was accepted");for(int i=0;i<9;++i){DrawList temporary;renderer.append_stellar_disc(temporary,{0,0},5,{{static_cast<std::uint8_t>(20+i*17),static_cast<std::uint8_t>(40+i*13),static_cast<std::uint8_t>(80+i*9),255},false,static_cast<std::uint32_t>(i+2)},0);}const auto stats=renderer.stats();require(stats.generated_resources==10,"byte-pressure fixture did not create distinct large resources");require(stats.cached_resources<10&&stats.cached_resources<=NativeCelestialAppearanceRenderer::maximum_cached_resources&&stats.cached_bytes<=NativeCelestialAppearanceRenderer::maximum_cached_bytes,"celestial resource cache did not enforce its byte ceiling");}
void ring_layers_occlude(){NativeCelestialAppearanceRenderer renderer;DrawList draw;renderer.append_ring_back(draw,{100,100},20,UiRect{0,0,200,200});auto globe=RgbaImage::create(2,2,std::vector<std::uint8_t>(16,255));draw.world.emplace_back(Image{globe,{80,80,40,40}});renderer.append_ring_front(draw,{100,100},20,UiRect{0,0,200,200});require(draw.world.size()==3,"ring/globe ordering changed");const auto&back=*image_at(draw,0).resource;const auto&front=*image_at(draw,2).resource;require(back.width()==NativeCelestialAppearanceRenderer::ring_texture_size&&front.width()==back.width()&&back.pixels()!=front.pixels(),"ring halves were not distinct immutable layers");std::size_t back_count=0,front_count=0,overlap=0;for(int y=0;y<back.height();++y)for(int x=0;x<back.width();++x){const int a=alpha(back,x,y),b=alpha(front,x,y);back_count+=a>0;front_count+=b>0;overlap+=a>0&&b>0;const int mirrored_back=alpha(back,front.width()-1-x,front.height()-1-y),mirrored_front=alpha(front,front.width()-1-x,front.height()-1-y);const int composite=b+(a*(255-b)+127)/255,mirrored_composite=mirrored_front+(mirrored_back*(255-mirrored_front)+127)/255;require(std::abs(composite-mirrored_composite)<=2,"complementary ring masks introduced a diagonal compositing seam");}require(back_count+front_count>35000&&overlap>100,"continuous ring annulus lost its bounded compositing overlap");require(alpha(back,0,0)==0&&alpha(front,0,0)==0,"ring texture corners are not transparent");const auto stats=renderer.stats();require(stats.cached_resources==2&&stats.generated_resources==2,"ring layers were regenerated");DrawList again;renderer.append_ring_back(again,{30,30},5);renderer.append_ring_front(again,{30,30},5);require(renderer.stats().generated_resources==2,"camera scaling regenerated ring resources");}
}
int main(int argc,char**argv)try{
  if(argc!=1&&(argc!=3||std::string_view(argv[1])!="--capture"))throw std::runtime_error("Usage: native_celestial_appearance_tests [--capture <directory>]");
  star_cache_and_transience();black_hole_and_limits();ring_layers_occlude();async_preparation_contract();saturated_preparation_matches_sync();
  NativeCelestialAppearanceRenderer activity;bool quiet=false,flare=false;
  for(int frame=0;frame<900;++frame){
    DrawList draw;activity.append_stellar_activity(draw,{120,120},60,{{255,211,98,255},false,17},10000000.+frame/30.);
    quiet|=draw.world.empty();flare|=!draw.world.empty();
    require(draw.world.size()<=36,"supplied artwork activity exceeded its geometry budget");
  }
  require(quiet&&flare&&activity.stats().generated_resources==0,"supplied art lost intermittent flares or generated a replacement surface");
  if(argc==3)capture_cold_resources(fs::absolute(argv[2]));
  std::cout<<"native celestial appearance tests passed\n";return 0;
}catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}

