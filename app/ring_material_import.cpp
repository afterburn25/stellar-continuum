#include <stellar/engine/ring_material_preparation.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv)try{
 if(argc!=3)throw std::invalid_argument("Usage: stellar_ring_material_import audit.json output-directory");nlohmann::json audit,results=nlohmann::json::array();std::ifstream(argv[1])>>audit;const auto root=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(root);
 for(const auto& item:audit.at("images")){if(item.at("status")!="candidate")continue;const auto id=item.at("id").get<std::string>();nlohmann::json record{{"id",id}};try{const auto source=stellar::native_map::decode_rgba_image(std::filesystem::u8path(item.at("source").get<std::string>()));stellar::native_map::RingSourceOptions options;options.fragmented=item.value("fragmented",false);auto maps=stellar::native_map::prepare_ring_material(*source,options);record.update({{"usable",maps.usable},{"reason",maps.rejection_reason},{"ellipse",maps.ellipse},{"innerFraction",maps.inner_fraction},{"outerFraction",maps.outer_fraction},{"meanOpacity",maps.mean_opacity},{"gapFraction",maps.gap_fraction}});if(maps.usable){std::filesystem::create_directories(root/id);stellar::native_map::encode_rgba_png(*maps.material,root/id/"radial.png");stellar::native_map::encode_rgba_png(*maps.preview,root/id/"preview.png");}}
 catch(const std::exception& e){record["usable"]=false;record["reason"]=e.what();}results.push_back(record);std::ofstream(root/"conversion.json")<<results.dump(2)<<'\n';std::cout<<id<<" "<<record.at("usable")<<'\n';}
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
