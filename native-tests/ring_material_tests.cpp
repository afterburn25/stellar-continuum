#include <stellar/engine/ring_material_preparation.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace stellar::native_map;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main()try{
 constexpr int size=1024;
 std::vector<std::uint8_t> pixels(size*size*4,0);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){
  const double nx=(x-512.)/450.,ny=(y-512.)/290.,r=std::hypot(nx,ny),angle=std::atan2(ny,nx);
  // A narrow authored gap is not exactly concentric. The former radial
  // quantile erased it by mixing different radii around the circumference.
  const double gap=.81+.025*std::sin(angle*5);
  const bool material=r>.58&&r<1&&std::abs(r-gap)>.007;
  const double fine=std::sin(angle*73+r*97)>.0?1.:.40;
  const auto at=(static_cast<std::size_t>(y)*size+x)*4;
  for(int c=0;c<3;++c)pixels[at+c]=material?static_cast<std::uint8_t>(220*fine):0;
  pixels[at+3]=255;
 }
 const auto source=RgbaImage::create(size,size,std::move(pixels));
 RingSourceOptions options;options.azimuth_samples=1024;
 const auto converted=prepare_ring_material(*source,options);
 check(converted.usable&&converted.material->height()==1024,"Ring did not retain two-dimensional authored material");
 const auto& texture=*converted.material;
 int clear_rows=0;double detail=0;
 const auto alpha=[&](int x,int y){return texture.pixels()[(static_cast<std::size_t>(y)*texture.width()+x)*4+3];};
 for(int y=0;y<texture.height();++y){
  int low=255,high=0;
  for(int x=texture.width()*45/100;x<texture.width()*68/100;++x){low=std::min(low,int(alpha(x,y)));high=std::max(high,int(alpha(x,y)));}
  if(low<30&&high>160)++clear_rows;
  if(y)detail+=std::abs(int(alpha(texture.width()/3,y))-int(alpha(texture.width()/3,y-1)));
 }
 check(clear_rows>texture.height()*95/100,"Azimuthal averaging blurred a narrow authored gap");
 check(detail/texture.height()>12,"Fine angular texture was averaged away");
 for(int x=0;x<texture.width();++x)check(std::abs(int(alpha(x,0))-int(alpha(x,texture.height()-1)))<=1,"Rectified ring has an angular seam");
 check(converted.material->byte_size()==1024u*1024u*4u,"Unexpected close texture allocation");
 check(!prepare_ring_material(*RgbaImage::create(size,size,std::vector<std::uint8_t>(size*size*4))).usable,"Empty artwork became a ring");
 std::cout<<"Authored gap retained in "<<clear_rows<<"/"<<texture.height()<<" rays; angular detail "<<detail/texture.height()<<" alpha levels; seam and allocation passed.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
