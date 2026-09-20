#include <stellar/engine/spherical_material_preparation.hpp>
#include <stellar/engine/sha256.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
using namespace stellar::native_map;
namespace {
std::string utf8(const std::filesystem::path& p){auto s=p.generic_u8string();return {s.begin(),s.end()};}
void number(std::vector<std::uint8_t>& p,int width,int x,int y,std::size_t n){
 constexpr unsigned digits[]{0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
 for(char c:std::to_string(n)){auto bits=digits[c-'0'];for(int v=0;v<5;++v)for(int u=0;u<3;++u)if(bits&(1u<<(14-v*3-u)))for(int j=0;j<3;++j)for(int i=0;i<3;++i){auto at=((y+v*3+j)*width+x+u*3+i)*4;p[at]=p[at+1]=p[at+2]=240;}x+=12;}
}
}
int main(int argc,char** argv)try{
 if(argc<3)throw std::invalid_argument("Usage: stellar_planet_asset_audit source-directory output-directory [filename-filter]");
 auto root=std::filesystem::u8path(argv[1]),out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
 std::vector<std::filesystem::path> files;for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file()&&(argc<4||entry.path().filename()==argv[3])){auto ext=entry.path().extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".webp")files.push_back(entry.path());}std::sort(files.begin(),files.end());
 nlohmann::json records=nlohmann::json::array();constexpr int cell=288,footer=24,cols=4,rows=4,width=cell*cols,height=(cell+footer)*rows;std::vector<std::uint8_t> pixels;
 for(std::size_t i=0;i<files.size();++i){if(i%16==0){pixels.assign(width*height*4,16);for(std::size_t k=3;k<pixels.size();k+=4)pixels[k]=255;}
  nlohmann::json record{{"index",i+1},{"source",utf8(files[i])},{"filename",utf8(files[i].filename())},{"folder",utf8(files[i].parent_path().lexically_relative(root))},{"status","unreviewed"}};
  try{std::ifstream f(files[i],std::ios::binary);std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};std::ostringstream hash;for(auto b:stellar::engine::sha256(bytes))hash<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(b);record["sha256"]=hash.str();const auto image=decode_rgba_image(files[i]);record["width"]=image->width();record["height"]=image->height();
   const double scale=std::min(double(cell)/image->width(),double(cell)/image->height());const int w=static_cast<int>(image->width()*scale),h=static_cast<int>(image->height()*scale),ox=static_cast<int>(i%cols)*cell+(cell-w)/2,oy=static_cast<int>((i%16)/cols)*(cell+footer)+(cell-h)/2;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto src=(static_cast<std::size_t>(y/scale)*image->width()+static_cast<int>(x/scale))*4;auto dst=((oy+y)*width+ox+x)*4;for(int k=0;k<3;++k)pixels[dst+k]=image->pixels()[src+k];}
  }catch(const std::exception& e){record["status"]="rejected";record["reason"]=e.what();}
  number(pixels,width,static_cast<int>(i%cols)*cell+12,static_cast<int>((i%16)/cols)*(cell+footer)+cell+3,i+1);records.push_back(record);
  if(i%16==15||i+1==files.size()){std::ostringstream name;name<<"sheet-"<<std::setw(2)<<std::setfill('0')<<(i/16+1)<<".png";encode_rgba_png(*RgbaImage::create(width,height,std::move(pixels)),out/name.str());}
 }
 std::ofstream(out/"inventory.json")<<nlohmann::json{{"sourceRoot",utf8(root)},{"images",records}}.dump(2)<<'\n';std::cout<<records.size()<<" files inventoried and contact sheets rendered.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
