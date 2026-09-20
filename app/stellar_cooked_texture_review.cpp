#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace stellar::native_map;
using namespace stellar::engine;
namespace fs=std::filesystem;
using Json=nlohmann::json;
int main(int argc,char**argv)try{
 if(argc!=5)throw std::runtime_error("Usage: texture_review SOURCE_ROOT PACKAGE_ROOT COOK_REPORT OUTPUT_DIR");
 const fs::path root=fs::absolute(argv[1]),package=fs::absolute(argv[2]),output=fs::absolute(argv[4]);
 fs::create_directories(output);std::ifstream input(argv[3]);const auto report=Json::parse(input);
 const std::vector<std::pair<std::string,std::string>> selections{
  {"earth","sol/earth-map.png"},{"barren","sol/mercury-map.png"},{"gas-giant","giant-cream-band-01/albedo.png"},
  {"ice-giant","giant-blue-mist-01/albedo.png"},{"moon","moons/sol-europa/albedo.png"},{"ring","rings/ring-bright-ice-ring-01/radial.png"},
  {"star","stellar/G-Class Yellow.png"},{"flare","1024/A/flare-01-peak.png"},{"cme","1024/generic/cme-01.png"},
  {"nebula","phenomena/Blue Reflection Nebula 1.png"},{"starfield","starfields/faint-map-reference.png"},
  {"galaxy","galaxies/"},{"ship","ships/"},{"ui","hud/galaxy-view.png"}
 };
 Json results=Json::array();
 for(const auto&[name,match]:selections){
  const auto it=std::find_if(report.at("assets").begin(),report.at("assets").end(),[&](const auto&item){return item.at("format")!="bytes"&&item.at("id").template get<std::string>().find(match)!=std::string::npos;});
  if(it==report.at("assets").end())throw std::runtime_error("No QA asset matched "+name+": "+match);
  const auto id=it->at("id").get<std::string>(),source_path=it->at("source").get<std::string>();
  unmount_asset_registry();auto source=decode_rgba_image(root/fs::u8path(source_path));
  mount_asset_registry(package,false);auto cooked=decode_rgba_image(package/fs::u8path(id));unmount_asset_registry();
  if(source->width()!=cooked->width()||source->height()!=cooked->height())throw std::runtime_error("Base resolution changed: "+id);
  double squares=0,max_error=0;std::uint64_t n=0;const auto&sp=source->pixels();const auto&cp=cooked->pixels();
  for(std::size_t i=0;i<sp.size();i+=4)for(int c=0;c<4;++c){const double d=c==3?double(sp[i+c])-cp[i+c]:sp[i+c]*sp[i+3]/255.-cp[i+c]*cp[i+3]/255.;squares+=d*d;max_error=std::max(max_error,std::abs(d));++n;}
  // Original and cooked center crops at exactly 1:1, composited over black.
  const int w=std::min(640,source->width()),h=std::min(640,source->height()),x0=(source->width()-w)/2,y0=(source->height()-h)/2;
  std::vector<std::uint8_t> pair(static_cast<std::size_t>(w*2+8)*h*4,255);
  for(int y=0;y<h;++y)for(int x=0;x<w*2+8;++x){const auto d=(static_cast<std::size_t>(y)*(w*2+8)+x)*4;for(int c=0;c<3;++c)pair[d+c]=35;}
  for(int side=0;side<2;++side){const auto&p=side?cp:sp;for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto s=(static_cast<std::size_t>(y+y0)*source->width()+x+x0)*4,d=(static_cast<std::size_t>(y)*(w*2+8)+x+side*(w+8))*4;for(int c=0;c<3;++c)pair[d+c]=static_cast<std::uint8_t>(p[s+c]*p[s+3]/255);}}
  encode_rgba_png(*RgbaImage::create(w*2+8,h,std::move(pair)),output/(name+"-source-left-cooked-right.png"));
  results.push_back({{"category",name},{"id",id},{"format",it->at("format")},{"width",source->width()},{"height",source->height()},{"visibleRmse",std::sqrt(squares/n)},{"visibleMaxError",max_error}});
 }
 std::ofstream(output/"quality.json")<<results.dump(2);std::cout<<results.size()<<" original/cooked comparisons generated\n";return 0;
}catch(const std::exception&e){unmount_asset_registry();std::cerr<<e.what()<<'\n';return 1;}
