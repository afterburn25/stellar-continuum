#include <stellar/engine/emissive_image.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <array>
int main(int argc,char** argv)try{
 if(argc!=3)throw std::invalid_argument("Usage: stellar_eruption_import matched-assets.json output-directory");
 using namespace stellar::native_map;using nlohmann::json;
 json manifest;std::ifstream(argv[1])>>manifest;const auto source=std::filesystem::u8path(manifest.at("sourceRoot").get<std::string>()),output=std::filesystem::u8path(argv[2]);
 int count=0;
 for(auto& item:manifest.at("files"))if(item.at("used")){
  const auto input=decode_rgba_image(source/std::filesystem::u8path(item.at("path").get<std::string>()));
  const auto relative=std::filesystem::u8path(item.at("runtimePath").get<std::string>());
  int turns=0;if(item.at("eruptionType")=="SMALL_PROMINENCE")turns=std::array{3,3,3,1,1,1,3,1,1,1,3,3}.at(item.at("sequenceVariant").get<int>()-1);
  for(int size:{256,512,1024}){const auto image=prepare_emissive_image(*input,size,turns);const auto target=output/std::to_string(size)/relative;
   std::filesystem::create_directories(target.parent_path());encode_rgba_png(*image,target);
  }
  item["transparencyPreprocessed"]=true;item["preparation"]="Radiance-preserving black-to-alpha; existing alpha preserved; premultiplied filtering, straight-alpha PNG.";item["quarterTurnsClockwise"]=turns;
  ++count;if(count%24==0)std::cout<<count<<" images prepared\n";
 }
 std::filesystem::create_directories(output);std::ofstream(output/"manifest.json")<<manifest.dump(2)<<'\n';
 std::cout<<count<<" distinct eruption sources prepared at 256, 512 and 1024 pixels\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
