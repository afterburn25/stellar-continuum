#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace stellar::native_map {
// Portable opaque BC1 encoder with a full mip chain. Intended for reduced
// quality distant scenery; authored high-detail materials retain RGBA pixels.
// The retained base pixels also allow an unsupported-device fallback.
inline std::shared_ptr<const RgbaImage> compress_opaque_texture(const RgbaImage& source){
 const int width=(source.width()+3)/4*4,height=(source.height()+3)/4*4;
 std::vector<std::uint8_t> base(static_cast<std::size_t>(width)*height*4);
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto from=(static_cast<std::size_t>(std::min(y,source.height()-1))*source.width()+std::min(x,source.width()-1))*4,to=(static_cast<std::size_t>(y)*width+x)*4;
  if(source.pixels()[from+3]!=255)throw std::invalid_argument("BC1 opaque texture requires opaque pixels");for(int k=0;k<4;++k)base[to+k]=source.pixels()[from+k];}
 auto pixels=base;int w=width,h=height;std::vector<Bc1MipLevel> levels;
 for(;;){Bc1MipLevel level{w,h,{}};level.blocks.reserve(static_cast<std::size_t>((w+3)/4)*((h+3)/4)*8);
  for(int y=0;y<h;y+=4)for(int x=0;x<w;x+=4){
   std::array<std::array<int,3>,16> rgb{};int dark=0,light=0;
   for(int j=0;j<16;++j){const auto p=(static_cast<std::size_t>(std::min(h-1,y+j/4))*w+std::min(w-1,x+j%4))*4;
    for(int k=0;k<3;++k)rgb[j][k]=pixels[p+k];const auto l=[](auto c){return c[0]*3+c[1]*6+c[2];};if(l(rgb[j])<l(rgb[dark]))dark=j;if(l(rgb[j])>l(rgb[light]))light=j;}
   const auto pack=[](auto c){return static_cast<std::uint16_t>(((c[0]*31+127)/255)<<11|((c[1]*63+127)/255)<<5|((c[2]*31+127)/255));};
   auto a=pack(rgb[light]),b=pack(rgb[dark]);if(a<b)std::swap(a,b);if(a==b){if(a<65535)++a;else --b;}
   const auto unpack=[](std::uint16_t c){return std::array<int,3>{((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};};
   std::array<std::array<int,3>,4> colors{unpack(a),unpack(b),{}, {}};for(int k=0;k<3;++k){colors[2][k]=(2*colors[0][k]+colors[1][k])/3;colors[3][k]=(colors[0][k]+2*colors[1][k])/3;}
   std::uint32_t indices=0;for(int j=0;j<16;++j){int best=0,distance=1000000;for(int c=0;c<4;++c){int error=0;for(int k=0;k<3;++k){const int d=rgb[j][k]-colors[c][k];error+=d*d;}if(error<distance){distance=error;best=c;}}indices|=static_cast<std::uint32_t>(best)<<(j*2);}
   for(auto c:{a,b}){level.blocks.push_back(static_cast<std::uint8_t>(c));level.blocks.push_back(static_cast<std::uint8_t>(c>>8));}for(int i=0;i<4;++i)level.blocks.push_back(static_cast<std::uint8_t>(indices>>(8*i)));
  }levels.push_back(std::move(level));if(w==1&&h==1)break;
  const int nw=std::max(1,w/2),nh=std::max(1,h/2);std::vector<std::uint8_t> next(static_cast<std::size_t>(nw)*nh*4);
  for(int y=0;y<nh;++y)for(int x=0;x<nw;++x)for(int k=0;k<4;++k){int sum=0;for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=pixels[(static_cast<std::size_t>(std::min(h-1,y*2+dy))*w+std::min(w-1,x*2+dx))*4+k];next[(static_cast<std::size_t>(y)*nw+x)*4+k]=static_cast<std::uint8_t>((sum+2)/4);}
  pixels=std::move(next);w=nw;h=nh;
 }
 return RgbaImage::create(width,height,std::move(base),std::move(levels));
}
}
