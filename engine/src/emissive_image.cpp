#include <stellar/engine/emissive_image.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <numbers>
namespace stellar::native_map {
std::optional<EmissiveDisc> measure_emissive_disc(const RgbaImage& image){
 if(image.width()!=image.height()||image.width()<16)return {};
 const auto sample=[&](float x,float y){const int ix=std::clamp(static_cast<int>(x*image.width()),0,image.width()-1),iy=std::clamp(static_cast<int>(y*image.height()),0,image.height()-1);const auto p=(static_cast<std::size_t>(iy)*image.width()+ix)*4;
  return std::max({image.pixels()[p],image.pixels()[p+1],image.pixels()[p+2]})/255.f*image.pixels()[p+3]/255.f;};
 const auto median=[](auto values){std::ranges::sort(values);return values[values.size()/2];};
 EmissiveDisc disc;
 // Opposite limb midpoints remove small authored offsets. Median rays reject
 // individual protruding loops; no pixels are cropped or made opaque.
 for(int pass=0;pass<2;++pass){std::array<float,96> distances{},xs{},ys{};
  for(int ray=0;ray<96;++ray){const float angle=2*std::numbers::pi_v<float>*ray/96,dx=std::cos(angle),dy=std::sin(angle);float peak=0;
   for(int j=0;j<128;++j){const float r=j/256.f;peak=std::max(peak,sample(disc.center.x+dx*r,disc.center.y+dy*r));}
   if(peak<.08f)return {};
   for(int j=0;j<128;++j){const float r=j/256.f;if(sample(disc.center.x+dx*r,disc.center.y+dy*r)>=peak*.5f)distances[ray]=r;}
  }
  for(int ray=0;ray<96;++ray){const float angle=2*std::numbers::pi_v<float>*ray/96,d=(distances[ray]-distances[(ray+48)%96])*.5f;xs[ray]=d*std::cos(angle);ys[ray]=d*std::sin(angle);}
  disc.center.x+=2*median(xs);disc.center.y+=2*median(ys);disc.radius=median(distances);
 }
 if(disc.radius<.1f||disc.radius>.48f)return {};return disc;
}
std::shared_ptr<const RgbaImage> prepare_emissive_image(const RgbaImage& source,int size,int quarter_turns){
 if(size<16||size>2048||quarter_turns<0||quarter_turns>3)throw std::invalid_argument("Invalid emissive image preparation");
 const auto& pixels=source.pixels();bool authored_alpha=false;
 for(std::size_t i=3;i<pixels.size();i+=4)if(pixels[i]!=255){authored_alpha=true;break;}
 std::vector<std::uint8_t> out(static_cast<std::size_t>(size)*size*4);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){
  std::array<double,4> sum{};const int taps=std::max(1,std::max(source.width(),source.height())/size);
  for(int ty=0;ty<taps;++ty)for(int tx=0;tx<taps;++tx){
   double u=(x+(tx+.5)/taps)/size,v=(y+(ty+.5)/taps)/size;
   for(int turn=0;turn<quarter_turns;++turn){const double old=u;u=v;v=1-old;}
   const int sx=std::clamp(static_cast<int>(u*source.width()),0,source.width()-1),sy=std::clamp(static_cast<int>(v*source.height()),0,source.height()-1);
   const auto p=(static_cast<std::size_t>(sy)*source.width()+sx)*4;
   const double alpha=authored_alpha?pixels[p+3]/255.:std::max({pixels[p],pixels[p+1],pixels[p+2]})/255.;
   for(int c=0;c<3;++c)sum[c]+=pixels[p+c]*(authored_alpha?alpha:1.);sum[3]+=alpha;
  }
  const auto q=(static_cast<std::size_t>(y)*size+x)*4;
  if(sum[3]>0)for(int c=0;c<3;++c)out[q+c]=static_cast<std::uint8_t>(std::clamp(std::lround(sum[c]/sum[3]),0l,255l));
  out[q+3]=static_cast<std::uint8_t>(std::clamp(std::lround(sum[3]*255/(taps*taps)),0l,255l));
 }
 return RgbaImage::create(size,size,std::move(out));
}
}
