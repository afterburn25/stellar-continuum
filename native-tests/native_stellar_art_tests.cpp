#include "native_stellar_art.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <variant>
using namespace stellar::native_map;
using stellar::native_stellar::Artwork;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
const Image* detailed(const DrawList& draw){for(const auto& item:draw.world)if(const auto* image=std::get_if<Image>(&item);image&&image->resource->width()==1024)return image;return nullptr;}
int main(int argc,char** argv)try {
  check(argc>=2,"Source root required");Artwork art(argv[1]);check(art.manifest().size()==25,"Every close asset mapped");
  int pairs=0;for(const auto& [id,asset]:art.manifest()){
    if(!asset.distance_asset.empty()){++pairs;auto expected=std::filesystem::path(asset.close_asset);expected.replace_filename(expected.stem().string()+" Distance"+expected.extension().string());check(expected.string()==asset.distance_asset,"Distance suffix pairing");}
    DrawList far;art.append(far,{500,500},10,id,0);check(!far.world.empty()&&!detailed(far),"Small stars never need close textures");
    for(const auto& item:far.world)if(const auto* image=std::get_if<Image>(&item)){const auto& pixels=image->resource->pixels();const int n=image->resource->width();for(int k=0;k<n;++k)check(pixels[(k*4)+3]==0&&pixels[((n-1)*n+k)*4+3]==0,"Distance sprites have transparent edges");}
    DrawList initial;art.append(initial,{500,500},60,id,1);check(!detailed(initial),"Just-loaded detail must start invisible");
    DrawList middle;art.append(middle,{500,500},60,id,1.225);const auto* mid=detailed(middle);check(mid&&mid->tint.a>90&&mid->tint.a<180,"Asynchronous-ready detail fades gradually");
    DrawList close;art.append(close,{500,500},60,id,2);const auto* near=detailed(close);check(near&&near->tint.a==255,"Resolved close artwork becomes fully visible");
    const auto& pixels=near->resource->pixels();check(pixels[3]==0&&pixels[pixels.size()-1]==0,"Close artwork has transparent corners");
    if(argc>2){std::filesystem::create_directories(argv[2]);std::ofstream raw(std::filesystem::path(argv[2])/(id+".rgba"),std::ios::binary);raw.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));}
    DrawList transition;art.append(transition,{500,500},32,id,3);const auto* blended=detailed(transition);check(blended&&blended->tint.a>0&&blended->tint.a<255,"Zoom interpolates distance and close detail");
    check(art.close_count()<=Artwork::maximum_close_images,"Close cache bounded");
  }
  check(pairs==8,"All eight supplied Distance pairs");check(art.distance_count()==25,"Shared distance representations stay bounded");
  Artwork crowded(argv[1]);std::vector<std::shared_ptr<const RgbaImage>> retained;
  for(int frame=0;frame<4;++frame){
    crowded.begin_frame();int detailed_types=0;std::vector<std::shared_ptr<const RgbaImage>> current;
    for(const auto& [id,asset]:crowded.manifest()){
      DrawList draw;crowded.append(draw,{200,200},60,id,static_cast<double>(frame));
      if(const auto* image=detailed(draw)){++detailed_types;current.push_back(image->resource);}
    }
    check(detailed_types<=4,"Crowded viewport respects high-detail budget");
    if(frame==2)retained=current;
    if(frame==3)check(current==retained&&current.size()==4,"Crowded viewport does not thrash its close cache");
  }
  Artwork background(argv[1]);background.use_queue(std::make_shared<ImagePreparationQueue>());
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool found=false;double time=0;
  while(!found){DrawList draw;background.append(draw,{200,200},60,art.manifest().begin()->first,time);found=detailed(draw)!=nullptr;time+=.01;check(std::chrono::steady_clock::now()<deadline,"Background decoding completed");check(background.pending_count()<=Artwork::maximum_pending,"Queue admission bounded");std::this_thread::yield();}
  if(argc>3){
    Window window("Stellar Continuum - stellar artwork validation",1280,720,false,std::filesystem::path(argv[1])/"assets/visual/fonts/Rajdhani-SemiBold.ttf");
    window.set_frame_cap(60);Artwork visual(argv[1]);
    const std::array<std::string,4> ids{"g-yellow","m-red-dwarf","hypergiant","jet-black-hole"};
    const std::array<float,5> radii{8,18,26,36,60};
    for(int frame=0;frame<120;++frame){
      const auto input=window.poll();if(input.quit_requested)throw std::runtime_error("Visual validation closed before capture");
      if(!input.renderable()){--frame;std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
      DrawList draw;visual.begin_frame();
      draw.world.emplace_back(Text{{25,12},"STELLAR ARTWORK - DISTANT LIGHT TO CLOSE DETAIL",{205,227,249,255},23});
      for(int row=0;row<4;++row){
        draw.world.emplace_back(Text{{20,65.f+row*159},ids[static_cast<std::size_t>(row)],{145,180,215,255},15});
        for(int column=0;column<5;++column)visual.append(draw,{180.f+column*226,135.f+row*159},radii[static_cast<std::size_t>(column)],ids[static_cast<std::size_t>(row)],frame/60.);
      }
      window.draw(draw,frame==119?std::optional<std::filesystem::path>{argv[3]}:std::nullopt);
    }
  }
  std::cout<<"33 assets, eight pairs, fallback transparency, smooth zoom/readiness blending and bounded non-thrashing caches passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
