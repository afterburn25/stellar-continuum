#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace stellar::native_map;
int main(int argc,char** argv)try{
 if(argc!=2)throw std::invalid_argument("Usage: stellar_planet_material_review prepared-directory");
 const std::filesystem::path root=argv[1];nlohmann::json conversion;std::ifstream(root/"conversion.json")>>conversion;
 for(const auto& entry:conversion){if(!entry.at("usable").get<bool>())continue;const auto folder=root/entry.at("id").get<std::string>();SphericalMaterialImages maps;
  maps.albedo=decode_rgba_image(folder/"albedo.png");maps.clouds=decode_rgba_image(folder/"clouds.png");maps.emission=decode_rgba_image(folder/"emission.png");
  constexpr int size=160;std::vector<std::uint8_t> pixels(size*size*4*4);
  for(int view=0;view<4;++view){const auto image=spherical_material_thumbnail(maps,size,view*std::numbers::pi/2);for(int y=0;y<size;++y)std::copy_n(image->pixels().data()+y*size*4,size*4,pixels.data()+(y*size*4+view*size)*4);}
  encode_rgba_png(*RgbaImage::create(size*4,size,std::move(pixels)),folder/"review.png");
 }
 std::cout<<"Four longitudes rendered for every prepared candidate.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
