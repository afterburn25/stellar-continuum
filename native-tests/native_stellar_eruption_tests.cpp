#include "native_stellar_eruptions.hpp"
#include "native_general_settings.hpp"
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/surface_attachment.hpp>
#include <stellar/engine/emissive_image.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace stellar::core;
using namespace stellar::native_map;
using stellar::native_stellar::EruptionArtwork;
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main(int argc,char** argv)try{
 check(argc>1,"Source root required");const std::filesystem::path root=argv[1];
 nlohmann::json manifest;std::ifstream(root/"assets/visual/stellar-eruptions/manifest.json")>>manifest;
 check(manifest.at("visualSets").size()==204,"Missing class/stage/variant mapping");
 for(const auto& set:manifest.at("visualSets")){
   const auto paths=set.at("textures").get<std::array<std::string,4>>();
   if(set.at("eruptionType")=="FLARE"||set.at("eruptionType")=="MAJOR_FLARE")check(std::set<std::string>(paths.begin(),paths.end()).size()==4,"Authored four-stage sequence reuses a frame");
   for(const auto& path:paths)for(const auto size:{256,512,1024})check(std::filesystem::is_regular_file(root/"assets/visual/stellar-eruptions"/std::to_string(size)/path),"Prepared texture missing");
 }
 FreshCampaignState world;world.seed=7;world.developer_provenance.emplace();
 const std::array types{StellarObjectType::OHotBlueStar,StellarObjectType::BBlueWhiteStar,StellarObjectType::AWhiteStar,StellarObjectType::FYellowWhiteStar,StellarObjectType::GYellowStar,StellarObjectType::KOrangeStar,StellarObjectType::MRedDwarf};
 for(int c=0;c<7;++c){StellarSystem s;s.id=c+1;s.stellar_object=generate_stellar_physics(c+1,types[c]);world.systems.push_back(s);}
 initialize_stellar_activity(world.seed,world.systems);StellarActivityScheduler scheduler;EruptionArtwork art(root);
 int covered=0;double seconds=1;
 const UiRect clip{0,0,800,600};
 for(auto& s:world.systems)for(int kind=0;kind<5;++kind)for(int variant=0;variant<12;++variant){
   StellarActivityCommand cmd;cmd.system_id=s.id;cmd.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,scheduler,1,cmd);
   cmd.action=StellarActivityAction::Force;cmd.type=static_cast<StellarEruptionType>(kind);cmd.variant=variant;cmd.longitude=1.35;cmd.orientation=1.57;
   const auto id=apply_developer_stellar_activity(world,scheduler,1,cmd);cmd.event_id=id;cmd.action=StellarActivityAction::Scrub;cmd.fraction=.35;(void)apply_developer_stellar_activity(world,scheduler,1,cmd);
   const auto before=*s.stellar_activity;
   for(int quality=0;quality<4;++quality){art.begin_frame(1,1,seconds++,true,quality);DrawList draw;art.append(draw,{400,300},110,s,0,clip);
     check(art.records().size()==1&&art.records()[0].id==id&&art.records()[0].variant==variant,"Coverage lost identity or variant");
     check(draw.world.size()==1&&std::holds_alternative<Scene3DView>(draw.world[0]),"Surface effect is not native 3D");
     const auto& material=std::get<Scene3DView>(draw.world[0]).scene->instances().at(0).material;
     check(material.texture->width()==std::array{256,512,1024,1024}[quality],"High-quality artwork was downsampled below the prepared close-up tier");
     check(*s.stellar_activity==before,"Graphics quality changed simulation");
   }
   ++covered;
 }
 auto& s=world.systems[4];auto& e=s.stellar_activity->front().events.back();
 e.paused=false;e.start_day=1;const auto duration=stellar_eruption_duration(e);double day=1+duration*.65;
 art.begin_frame(2,day,seconds++,true,2);DrawList map;art.append(map,{400,300},100,s,0,clip);const auto record=art.records().at(0);
 art.begin_frame(2,day,seconds,true,2);DrawList system;art.append(system,{250,250},160,s,0,clip);
 check(art.records()[0].id==record.id&&art.records()[0].progress==record.progress,"View switch restarted the eruption");
 auto restored=s;EruptionArtwork reloaded(root);reloaded.begin_frame(3,day,seconds,true,2);DrawList reload;reloaded.append(reload,{250,250},160,restored,0,clip);
 check(std::abs(reloaded.records()[0].progress-.65)<1e-10,"Reload does not reconstruct event at 65 percent");
 {
   const auto opacity=[&](double luminosity,double magnitude){
     auto host=s;host.stellar_object->luminosity_solar=luminosity;
     auto& event=host.stellar_activity->front().events.back();event.magnitude=magnitude;event.brightness=.5;
     reloaded.begin_frame(3,day,seconds,false,2);DrawList draw;reloaded.append(draw,{250,250},160,host,0,clip);
     return std::get<Scene3DView>(draw.world.at(0)).scene->instances().at(0).material.opacity;
   };
   const float dim=opacity(.001,.5),bright=opacity(100000.,.5),powerful=opacity(100000.,2.);
   check(dim>0&&bright>dim&&powerful>bright&&powerful<=1,"Host luminosity/magnitude exposure loses ordering or saturates faint detail");
 }
 art.begin_frame(2,1+duration*2,seconds+.05,true,2);DrawList fast;art.append(fast,{250,250},160,s,0,clip);
 check(art.records().size()==1&&art.records()[0].progress<.67,"Accelerated time discarded minimum display time");
 {
   auto skipped=s;auto& short_event=skipped.stellar_activity->front().events.back();short_event.id+=10000;short_event.start_day=day+.01;short_event.stage_days={.001,.001,.001,.001};
   EruptionArtwork crossing(root);crossing.begin_frame(1,day,0,true,2);DrawList before;crossing.append(before,{400,300},100,skipped,0,clip);
   crossing.begin_frame(1,day+.05,.016,true,2);DrawList after;crossing.append(after,{400,300},100,skipped,0,clip);
   check(crossing.records().size()==1&&crossing.records()[0].progress==0,"Event crossed entirely within an accelerated tick was invisible");
 }
 art.begin_frame(2,day,seconds+1,true,2);DrawList far;art.append(far,{400,300},4,s,0,clip);check(far.world.empty()&&art.records().empty(),"Distant view renders full eruptions");
 DrawList offscreen;art.append(offscreen,{-2000,-2000},100,s,0,clip);check(offscreen.world.empty(),"Offscreen effect consumed rendering");
 check(sphere_occludes_orthographic({0,0,-1.2f},{},1)&&!sphere_occludes_orthographic({1.2f,0,-.2f},{},1)&&!sphere_occludes_orthographic({0,0,1.2f},{},1),"Far-side/limb occlusion is wrong");
 for(double latitude:{-.8,0.,.8})for(double longitude:{-2.,0.,1.5}){
   MeshInstance3D i;i.mesh=curved_surface_ribbon(4);i.rotation=spherical_surface_rotation(latitude,longitude,.7);const auto prepared=prepare_instance3d(Camera3D{},i,1);
   const auto origin=transform(prepared.model_view,{0,0,0,1}),radial=transform(prepared.model_view,{0,1,0,1});const auto n=spherical_surface_normal(latitude,longitude);
   check(std::abs(radial[0]-origin[0]-n.x)<1e-5&&std::abs(radial[1]-origin[1]-n.y)<1e-5&&std::abs(radial[2]-origin[2]-n.z)<1e-5,"Ribbon is not radial to spherical surface");
 }
 // Preserve dim filaments and native transparency without cutting black holes.
 std::vector<std::uint8_t> pixels(16*16*4,255);for(int i=0;i<256;++i){pixels[i*4]=2;pixels[i*4+1]=4;pixels[i*4+2]=8;}
 auto dim=prepare_emissive_image(*RgbaImage::create(16,16,pixels),16);check(dim->pixels()[3]>0&&dim->pixels()[3]<16,"Faint radiance lost during alpha extraction");
 pixels[3]=73;auto authored=prepare_emissive_image(*RgbaImage::create(16,16,pixels),16);check(authored->pixels()[3]==73,"Authored alpha was replaced");
 const auto settings_path=root/"work/stellar-eruptions/test-settings.json";
 stellar::native_general::NativeGeneralSettings settings(settings_path);auto preferences=settings.saved();preferences.eruption_quality=3;check(settings.save(preferences),"Quality could not be saved");
 stellar::native_general::NativeGeneralSettings readback(settings_path);check(readback.saved().eruption_quality==3,"Quality setting lost on reload");
 if(argc>2){
   const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
   Window window("Stellar eruption GPU validation",1280,720,false,root/"assets/visual/fonts/Rajdhani-SemiBold.ttf");window.set_frame_cap(60);
   auto visual=s;visual.stellar_activity->front().profile.level=StellarActivityLevel::Active;
   auto& event=visual.stellar_activity->front().events.back();event.paused=true;event.scale=1.2;event.magnitude=1.5;event.orientation=1.57;
   EruptionArtwork gpu(root);
   for(int kind=0;kind<5;++kind){
     event.type=static_cast<StellarEruptionType>(kind);event.visual_variant=kind;event.paused_elapsed_days=.35*stellar_eruption_duration(event);
     DrawList baseline;for(int col=0;col<3;++col)baseline.world.emplace_back(Circle{{210.f+430*col,360},120,{35,35,35,255}});
     window.draw(baseline,output/("baseline-"+std::to_string(kind)+".bmp"));
     DrawList draw=baseline;gpu.begin_frame(1,day,seconds++,false,2);
     for(int col=0;col<3;++col){event.longitude=std::array{.6,3.141592653589793,1.35}[col];gpu.append(draw,{210.f+430*col,360},120,visual,0,{0,0,1280,720});}
     window.draw(draw,output/("effect-"+std::to_string(kind)+".bmp"));
   }
   // Exercise the actual application's material/geometry with fine filaments.
   // Uniform-color volume tests could not catch depth smearing. A face-on
   // emissive texture must retain both its bright strands and dark gaps.
   DrawList produced;gpu.begin_frame(2,day,seconds++,false,2);
   event.longitude=1.35;gpu.append(produced,{640,360},260,visual,0,{0,0,1280,720});
   auto instance=std::get<Scene3DView>(produced.world.at(0)).scene->instances().at(0);
   std::vector<std::uint8_t> filaments(256*256*4,255);
   for(int y=0;y<256;++y)for(int x=0;x<256;++x){
     const auto i=(y*256+x)*4;const std::uint8_t value=(x/4)%2?8:240;
     filaments[i]=value;filaments[i+1]=value;filaments[i+2]=value;
   }
   instance.material.texture=RgbaImage::create(256,256,std::move(filaments));
   instance.material.surface_effect->next_texture=instance.material.texture;
   instance.material.surface_effect->sphere_radius=0;instance.material.opacity=1;
   instance.rotation={};instance.position={};instance.scale=1;
   Camera3D camera;camera.projection=Projection3D::Orthographic;camera.position={0,.28,3};camera.orthographic_height=1;
   DrawList stripes;stripes.world.emplace_back(Scene3DView{Scene3D::create(camera,{instance}),{0,0,512,512}});
   const auto path=output/"filament-fidelity.bmp";window.draw(stripes,path);const auto rendered=decode_rgba_image(path);
   double bright=0,dark=0;int samples=0;
   for(int y=80;y<432;y+=31)for(int x=68;x<432;x+=16){
     bright+=rendered->pixels()[(y*rendered->width()+x)*4];
     dark+=rendered->pixels()[(y*rendered->width()+x+8)*4];++samples;
   }
   bright/=samples;dark/=samples;
   check(bright>230&&dark<18,"Eruption rendering smears fine bright filaments into their dark gaps");
   std::cout<<"filament_bright="<<bright<<" filament_dark="<<dark<<'\n';
 }
 std::cout<<"420 class/type/variant cases across four qualities; shared-view 65% continuity; LOD; surface normals; occlusion; alpha; settings persistence passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
