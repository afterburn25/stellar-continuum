#include "native_background_debug.hpp"
#include "native_phenomena.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
using namespace stellar::core;using namespace stellar::native_map;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
double brightness(const RgbaImage& image){double sum=0;for(std::size_t i=0;i<image.pixels().size();i+=4)sum+=image.pixels()[i]+image.pixels()[i+1]+image.pixels()[i+2];return sum/(image.width()*image.height()*3.);}
std::filesystem::path utf8path(const std::string& text){return std::filesystem::path(std::u8string(text.begin(),text.end()));}
int main(int argc,char** argv)try{
 check(argc==3||argc==4,"Repository, output and optional cooked root required");const std::filesystem::path root=argv[1],output=argv[2];std::filesystem::create_directories(output);
 nlohmann::json audit;std::ifstream(root/"data/stellar/starfield-asset-audit-v1.json")>>audit;
 std::set<std::string> folders,approved,used;std::size_t count=0,rejected=0,reclassified=0;
 // Audit sources are absolute artwork-workstation paths. The source tree is
 // verified on the machine that holds it; CI checkouts intentionally lack it.
 const bool sources_present=std::filesystem::is_regular_file(utf8path(audit.at("images").front().at("source").get<std::string>()));
 if(!sources_present)std::cout<<"Audit source tree absent; verifying coverage without source files\n";
 for(const auto& r:audit.at("images")){folders.insert(r.at("folder"));check(!sources_present||std::filesystem::is_regular_file(utf8path(r.at("source").get<std::string>())),"Audit references a missing source");++count;if(r.at("accepted")){approved.insert(r.at("id"));reclassified+=r.at("reclassified").get<bool>();}else ++rejected;}
 check(folders.size()==10&&count==199&&approved.size()==1&&rejected==198&&reclassified==0,"Audit coverage changed");
 for(const auto& a:background_assets())check(approved.contains(a.id)&&std::filesystem::is_regular_file(root/a.path),"Manifest contains rejected or missing art");
 BackgroundEnvironment dense;dense.stellar_density=.88;dense.radial_fraction=.25;dense.radius=25;dense.region=StellarRegion::InnerDisk;
 auto sparse=dense;sparse.stellar_density=.08;sparse.radial_fraction=1.15;sparse.region=StellarRegion::Halo;
 auto old=dense;old.population=PopulationState::Quiescent;old.morphology=GalaxyMorphology::Elliptical;
 auto young=dense;young.population=PopulationState::Starburst;young.region=StellarRegion::StarForming;young.star_formation=.9;
 for(int n=0;n<12000;++n){const auto a=select_system_background(dense,95,n,n),b=select_system_background(sparse,95,n,n),c=select_system_background(old,95,n,n),d=select_system_background(young,95,n,n);
  check(a==select_system_background(dense,95,n,n),"Seeded identity changed");
  for(const auto* p:{&a,&b,&c,&d}){
   check(approved.contains(p->asset_id),"Rejected image generated");used.insert(p->asset_id);
   check(p->category==BackgroundCategory::Faint&&p->blend_asset_id.empty()&&p->exposure==1,"Bright or layered sky escaped the faint-only policy");
  }
  check(a.environment==dense&&b.environment==sparse&&c.environment==old&&d.environment==young,"Visual curation changed physical environment");
 }
 check(used==approved,"Approved pool membership changed");
 const auto reference=decode_rgba_image(root/"assets/visual/space/star-background.png");
 const auto accepted=decode_rgba_image(root/background_assets().front().path);
 check(reference->pixels()==accepted->pixels()&&reference->width()==accepted->width()&&reference->height()==accepted->height(),"System reference differs from the faint star-map original");
 const auto stars=load_nearby_catalog(root/"data/astronomy/hyg-nearby-500-v1.json");
 GalaxyGenerationConfig config;config.base_seed=81427;config.system_count=250;config.pre_warp_count=3;config.ancient_count=0;config.developer_full_coverage=true;config=resolve_galaxy_configuration(config);
 auto world=seed_persistable_fresh_campaign(config.base_seed,stars,{"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{config.morphology,config.resolved_population},true,config});
 SystemBackgroundCatalog first;first.bind(world);
 check(first.profile(sol_system_id).asset_id=="starfield-map-reference"&&first.profile(sol_system_id).category==BackgroundCategory::Faint,"Sol must use its distant-star plate");
 check(first.profile(sol_system_id).blend_asset_id.empty()&&!first.profile(sol_system_id).mirror&&first.profile(sol_system_id).orientation==0,"Sol reference must not reroll/blend/rotate");
 for(int seed:{1,19,10000}){auto sol=dense;sol.canonical_sol=true;sol.dark_optical_depth=8;const auto p=select_system_background(sol,seed,0,seed);check(p.asset_id==first.profile(0).asset_id&&p.exposure==1&&p.crop_x==0&&p.crop_y==0&&!p.local_nebula,"Sol clear reference varies with seed/environment");}
 auto renamed=world;renamed.systems.front().catalog_preset_id.reset();renamed.systems.front().name="Sol";SystemBackgroundCatalog impostor;impostor.bind(renamed);check(!impostor.profile(renamed.systems.front().id).environment.canonical_sol,"A display name claimed canonical Sol policy");
 const auto saved=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(world,{0,"sky-test","2050-03-21T00:00:00Z",true}));
 auto restored=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(saved)).galaxy;SystemBackgroundCatalog second;second.bind(restored);
 for(const auto& system:world.systems)check(first.profile(system.id)==second.profile(system.id),"Save/load changed background identity");
 for(const auto& [label,id]:first.region_examples())check(std::ranges::any_of(world.systems,[&](const auto& s){return s.id==id;}),"QA invented a region example");
 for(float aspect:{.56f,1.f,1.78f,2.4f,3.6f})for(float roll:{0.f,1.3f,3.14f}){
  auto mesh=celestial_plate_mesh(1.78f,aspect,roll,{.025f,-.025f},true);
  for(const auto& v:mesh->vertices())check(v.uv.x>=0&&v.uv.y>=0&&v.uv.x<=1&&v.uv.y<=1,"Sky projection leaked an edge");
 }
 Window window("Native sky validation",1280,720,false,root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");window.set_vsync(0);window.set_frame_cap(0);(void)window.poll();
 NativeSystemBackground sky;auto queue=std::make_shared<ImagePreparationQueue>();sky.configure(root,queue);sky.bind(world);int id=world.systems.front().id;
 // High preserves every native pixel of the 2944x1648 faint map reference.
 // The earlier resolution cap and reduced exposure escaped visual tests.
 for(const auto& a:background_assets()){
  const auto original=decode_rgba_image(root/a.path),prepared=prepare_celestial_plate(*original,4096,1);
  check(original->width()==prepared->width()&&original->height()==prepared->height()&&original->pixels()==prepared->pixels(),"High-quality preparation altered source pixels");
 }
 check(sky.resolved(sol_system_id).exposure==1,"Sol reference is artificially dimmed");
 for(const auto& s:world.systems)check(sky.resolved(s.id).exposure>=.88,"Clear starfield retains excessive exposure reduction");
 // Leave System View immediately after preloading. An inactive consumer still
 // has to collect its completed result so the next screen can use the queue.
 sky.preload(id,2,1);check(queue->outstanding_jobs()>0,"Sky preload did not submit work");
 for(int n=0;n<3000&&queue->outstanding_jobs();++n){sky.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(queue->reserved_bytes()==0&&sky.cache_bytes()>0,"Inactive sky retained queue capacity");
 auto other=queue->submit(ImagePreparationQueue::default_max_reserved_output_bytes,[]{return RgbaImage::create(1,1,{0,0,0,255});});
 check(other.has_value(),"Inactive sky starved the next screen's artwork");
 for(int n=0;n<3000&&!other->ready();++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 check(other->ready(),"Other consumer did not finish");(void)other->take();
 const auto draw=[&](int quality=2,int density=1){DrawList result;for(int n=0;n<3000;++n){result={};sky.append(result,id,1280,720,quality,density);if(sky.ready())return result;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Sky streaming did not finish");};
 window.draw(draw(),output/"sol-distant.png");
 {const auto scene=draw();const auto& material=std::get<Scene3DView>(scene.world.front()).scene->instances().front().material;
  const auto source=decode_rgba_image(root/background_asset("starfield-map-reference").path);
  check(material.texture->width()==source->width()&&material.texture->bc1_mips().empty()&&material.cubic_magnification,"High sky lost source resolution or filtering");}
 for(int c=0;c<10;++c){sky.options.category=static_cast<BackgroundCategory>(c);sky.options.variant=0;auto scene=draw();check(scene.world.size()==1&&std::holds_alternative<Scene3DView>(scene.world.front()),"Sky is not a deepest 3D layer");window.draw(scene,output/("category-"+std::to_string(c)+".png"));}
 sky.options.category=BackgroundCategory::Dense;sky.options.dark_test=false;window.draw(draw(),output/"clear.png");const double clear=brightness(*decode_rgba_image(output/"clear.png"));
 sky.options.dark_test=true;window.draw(draw(),output/"dark.png");check(brightness(*decode_rgba_image(output/"dark.png"))<clear*.08,"Dark cloud failed to attenuate stars");sky.options.dark_test=false;
 auto scene=draw();scene.world.emplace_back(Circle{{640,360},40,{240,140,80,255}});scene.overlay.emplace_back(FilledRectangle{{0,0,140,80},{20,220,100,255}});window.draw(scene,output/"layer-order.png");const auto image=decode_rgba_image(output/"layer-order.png");check(image->pixels()[(360*1280+640)*4]>200&&image->pixels()[(40*1280+50)*4+1]>200,"Objects or UI were obscured by sky");
 const auto identity=sky.catalog.profile(id);for(int q=0;q<4;++q)for(int d=0;d<3;++d){window.draw(draw(q,d),output/("quality-"+std::to_string(q)+"-density-"+std::to_string(d)+".png"));check(sky.catalog.profile(id)==identity,"Graphics preference changed generated identity");check(sky.cache_bytes()<=64u*1024*1024,"Unbounded sky cache");}
 sky.options.blend_test=true;sky.options.blend_scale=2;check(sky.resolved(id).blend_asset_id.empty(),"QA blend reintroduced a rejected bright secondary");window.draw(draw(),output/"blend-test.png");sky.options.blend_test=false;
 NativeBackgroundDebug panel;panel.toggle(sky);panel.data(sky,id);scene=draw();panel.render(scene,1280,720,sky);window.draw(scene,output/"debug-panel.png");
 // Exercise the actual generated gas/dust contexts through the same native
 // environment assembly used in System View, not only a synthetic overlay.
 sky.options={};double deepest=0;for(const auto& s:world.systems){const double depth=sky.catalog.profile(s.id).environment.dark_optical_depth;if(depth>deepest){deepest=depth;id=s.id;}}
 check(deepest>.5,"Generated QA campaign lacks an obscuring environment");
 sky.options.nebula=false;window.draw(draw(),output/"generated-dark-disabled.png");const auto unobscured=brightness(*decode_rgba_image(output/"generated-dark-disabled.png"));
 sky.options.nebula=true;window.draw(draw(),output/"generated-dark-transmission.png");check(brightness(*decode_rgba_image(output/"generated-dark-transmission.png"))<unobscured*std::exp(-deepest)*1.2+.15,"Actual generated dark depth did not attenuate sky");
 stellar::native_phenomena::NativePhenomena nebula;nebula.use_assets(root);nebula.use_queue(queue);nebula.bind(&*world.generation_metadata->phenomena);
 stellar::native_phenomena::VisualOptions visual;visual.background_stars=false;
 const auto& star=*std::ranges::find(world.systems,id,&StellarSystem::id);
 for(int n=0;n<3000;++n){scene=draw();nebula.append_system(scene,id,star.position.x,star.position.y,1280,720,1,visual);if(nebula.ready())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(nebula.ready()&&scene.world.size()>1&&std::holds_alternative<Scene3DView>(scene.world.front())&&std::holds_alternative<Image>(scene.world.back()),"Real nebula failed to load above the celestial dome");
 scene.world.emplace_back(Circle{{640,360},35,{240,140,80,255}});window.draw(scene,output/"generated-dark-nebula.png");const auto dark_scene=decode_rgba_image(output/"generated-dark-nebula.png");check(dark_scene->pixels()[(360*1280+640)*4]>200,"Real cloud obscured a later system object");
 // Put the canonical preset at a known generated cloud location. Its clear
 // local policy must skip both absorption and imagery in System and battle,
 // including a transition from the fully loaded cloudy system above.
 auto overlapped_sol=world;std::ranges::find(overlapped_sol.systems,sol_system_id,&StellarSystem::id)->position=star.position;
 sky.bind(overlapped_sol);id=sol_system_id;const auto& solar_profile=sky.catalog.profile(id);
 check(solar_profile.environment.dark_optical_depth>.5&&!solar_profile.local_nebula,"Sol test did not exercise a real overlapping cloud");
 visual.local_nebula=solar_profile.local_nebula;
 for(bool combat:{false,true}){scene=draw();nebula.append_system(scene,id,star.position.x,star.position.y,1280,720,1,visual,combat);
  check(nebula.ready()&&scene.world.size()==1,"Sol retained a local nebula layer or loading gate");
  const auto tint=std::get<Scene3DView>(scene.world.front()).scene->instances().front().material.tint;
  check(tint.r==faint_starfield_tint.r&&tint.g==faint_starfield_tint.g&&tint.b==faint_starfield_tint.b,"Sol differs from faint map brightness or is dimmed by a generated cloud");
 }
 window.draw(scene,output/"sol-clear-after-cloud.png");
 // Disable a layer after submission as well as after completion. The shared
 // queue must reclaim a discarded composite without drawing it on a later poll.
 visual.local_nebula=true;scene={};nebula.append_system(scene,id,star.position.x,star.position.y,1280,720,1,visual);
 visual.local_nebula=false;scene={};nebula.append_system(scene,id,star.position.x,star.position.y,1280,720,1,visual);
 for(int n=0;n<3000&&queue->reserved_bytes();++n){nebula.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(scene.world.empty()&&nebula.ready()&&queue->reserved_bytes()==0,"Clear sky retained stale cloud work");
 check(nebula.field()->regions.size()==world.generation_metadata->phenomena->regions.size(),"Clear local policy removed galaxy phenomena");
 // Quantified source audit: connected components above 220/255, at least
 // three pixels. This measures bright image features, not physical stars.
 nlohmann::json metrics=nlohmann::json::array();for(const auto& r:audit.at("images")){
  const auto src=decode_rgba_image(utf8path(r.at("source").get<std::string>()));const int w=src->width(),h=src->height();std::vector<bool> seen(static_cast<std::size_t>(w)*h);int features=0;std::size_t largest=0;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int i=y*w+x;const auto bright=[&](int p){return std::max({src->pixels()[p*4],src->pixels()[p*4+1],src->pixels()[p*4+2]})>=220;};if(seen[i]||!bright(i))continue;
   std::vector<int> todo{i};seen[i]=true;for(std::size_t k=0;k<todo.size();++k){const int p=todo[k],px=p%w,py=p/w;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int xx=px+dx,yy=py+dy;if(xx<0||yy<0||xx>=w||yy>=h)continue;const int j=yy*w+xx;if(!seen[j]&&bright(j)){seen[j]=true;todo.push_back(j);}}}
   if(todo.size()>=3)++features;largest=std::max(largest,todo.size());
  }metrics.push_back({{"id",r.at("id")},{"brightFeatureCount",features},{"largestBrightFeaturePixels",largest}});
 }
 std::ofstream(output/"bright-feature-audit.json")<<metrics.dump(2);
 if(argc==4){
  stellar::engine::mount_asset_registry(argv[3],false);
  sky.configure(std::filesystem::absolute(argv[3]),queue);sky.bind(world);sky.options={};id=sol_system_id;
  const auto cooked_scene=draw();
  const auto& texture=std::get<Scene3DView>(cooked_scene.world.front()).scene->instances().front().material.texture;
  check(texture->pixels()==reference->pixels()&&texture->byte_size()==reference->byte_size(),"Cooked faint sky changed pixels or exceeded source residency");
  check(queue->reserved_bytes()==0,"Cooked faint sky retained its preparation reservation");
  window.draw(cooked_scene,output/"cooked-sol-distant.png");
  stellar::engine::unmount_asset_registry();
 }
 std::cout<<"198 supplied plates rejected for local skies; exact faint map reference approved. 48000 profiles, persistence, projection, streaming, graphics combinations, dark attenuation and GPU layering passed.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
