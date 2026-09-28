#include <stellar/engine/texture_cook.hpp>
#include <bc7enc/bc7enc.h>
#include <bcdec/bcdec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <span>
#include <stdexcept>

namespace stellar::native_map {
std::string texture_format_name(TextureFormat f){switch(f){case TextureFormat::Bc7:return "BC7";case TextureFormat::Bc5:return "BC5";case TextureFormat::Bc4:return "BC4";default:return "RGBA8";}}
TextureFormat texture_format_from_name(std::string_view f){if(f=="BC7")return TextureFormat::Bc7;if(f=="BC5")return TextureFormat::Bc5;if(f=="BC4")return TextureFormat::Bc4;if(f=="RGBA8")return TextureFormat::Rgba8;throw std::runtime_error("Unsupported cooked texture format");}
std::vector<std::uint8_t> decode_texture_level(TextureFormat f,const Bc1MipLevel&m){
 if(m.width<1||m.height<1||m.width>8192||m.height>8192||static_cast<std::uint64_t>(m.width)*m.height*4>maximum_rgba_image_bytes)throw std::runtime_error("Invalid cooked texture size");
 const auto pixels=static_cast<std::size_t>(m.width)*m.height*4;
 if(f==TextureFormat::Rgba8){if(m.blocks.size()!=pixels)throw std::runtime_error("Invalid RGBA mip length");return m.blocks;}
 const int blockbytes=f==TextureFormat::Bc4?8:16;
 if(m.blocks.size()!=static_cast<std::size_t>((m.width+3)/4)*((m.height+3)/4)*blockbytes)throw std::runtime_error("Invalid BC mip length");
 std::vector<std::uint8_t> out(pixels);std::size_t offset=0;
 for(int y=0;y<m.height;y+=4)for(int x=0;x<m.width;x+=4){std::array<std::uint8_t,64> block{};
  if(f==TextureFormat::Bc7)bcdec_bc7(m.blocks.data()+offset,block.data(),16);
  else {std::array<std::uint8_t,32> data{};if(f==TextureFormat::Bc5)bcdec_bc5(m.blocks.data()+offset,data.data(),8);else bcdec_bc4(m.blocks.data()+offset,data.data(),4);
   for(int i=0;i<16;++i){const auto r=data[i*(f==TextureFormat::Bc5?2:1)];const auto g=f==TextureFormat::Bc5?data[i*2+1]:r;float nx=r/127.5f-1,ny=g/127.5f-1;block[i*4]=r;block[i*4+1]=g;block[i*4+2]=f==TextureFormat::Bc5?static_cast<std::uint8_t>(std::lround((std::sqrt(std::max(0.f,1-nx*nx-ny*ny))+1)*127.5f)):r;block[i*4+3]=255;}}
  offset+=blockbytes;for(int v=0;v<4&&y+v<m.height;++v)for(int u=0;u<4&&x+u<m.width;++u)std::memcpy(out.data()+(static_cast<std::size_t>(y+v)*m.width+x+u)*4,block.data()+(v*4+u)*4,4);
 }return out;
}
namespace {
void bc4_encode(const std::uint8_t* rgba,int channel,std::uint8_t*out){std::uint8_t lo=255,hi=0;for(int i=0;i<16;++i){lo=std::min(lo,rgba[i*4+channel]);hi=std::max(hi,rgba[i*4+channel]);}out[0]=hi;out[1]=lo;std::array<int,8> palette{hi,lo};if(hi>lo)for(int i=1;i<=6;++i)palette[i+1]=((7-i)*hi+i*lo)/7;else{for(int i=1;i<=4;++i)palette[i+1]=((5-i)*hi+i*lo)/5;palette[6]=0;palette[7]=255;}std::uint64_t bits=0;for(int i=0;i<16;++i){int best=0,error=1000;for(int p=0;p<8;++p){const int e=std::abs(static_cast<int>(rgba[i*4+channel])-palette[p]);if(e<error){best=p;error=e;}}bits|=static_cast<std::uint64_t>(best)<<(i*3);}for(int b=0;b<6;++b)out[b+2]=static_cast<std::uint8_t>(bits>>(b*8));}
Bc1MipLevel compress_level(const Bc1MipLevel&in,TextureFormat f,int uber_level){if(f==TextureFormat::Rgba8)return in;static std::once_flag initialized;std::call_once(initialized,bc7enc_compress_block_init);bc7enc_compress_block_params params;bc7enc_compress_block_params_init(&params);bc7enc_compress_block_params_init_linear_weights(&params);params.m_uber_level=uber_level;
 const int stride=f==TextureFormat::Bc4?8:16;Bc1MipLevel out{in.width,in.height,std::vector<std::uint8_t>(static_cast<std::size_t>((in.width+3)/4)*((in.height+3)/4)*stride)};std::size_t offset=0;
 for(int y=0;y<in.height;y+=4)for(int x=0;x<in.width;x+=4){std::array<std::uint8_t,64> block{};for(int v=0;v<4;++v)for(int u=0;u<4;++u)std::memcpy(block.data()+(v*4+u)*4,in.blocks.data()+(static_cast<std::size_t>(std::min(y+v,in.height-1))*in.width+std::min(x+u,in.width-1))*4,4);
  if(f==TextureFormat::Bc7){
   // Undefined invisible RGB must not consume endpoint precision or create a
   // visible alpha fringe in otherwise empty blocks.
   for(int i=0;i<16;++i)if(block[i*4+3]==0)block[i*4]=block[i*4+1]=block[i*4+2]=0;
   (void)bc7enc_compress_block(out.blocks.data()+offset,block.data(),&params);
  }else {bc4_encode(block.data(),0,out.blocks.data()+offset);if(f==TextureFormat::Bc5)bc4_encode(block.data(),1,out.blocks.data()+offset+8);}offset+=stride;
 }return out;
}
std::uint8_t byte(double v){return static_cast<std::uint8_t>(std::clamp(std::lround(v),0l,255l));}
Bc1MipLevel downsample(const Bc1MipLevel&in,std::string_view category){const int w=std::max(1,in.width/2),h=std::max(1,in.height/2);Bc1MipLevel out{w,h,std::vector<std::uint8_t>(static_cast<std::size_t>(w)*h*4)};const bool normal=category=="normal",data=normal||category=="mask"||category=="properties";
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){std::array<double,4> sum{};int n=0;const int x0=x*in.width/w,x1=(x+1)*in.width/w,y0=y*in.height/h,y1=(y+1)*in.height/h;
  for(int iy=y0;iy<y1;++iy)for(int ix=x0;ix<x1;++ix){const auto i=(static_cast<std::size_t>(iy)*in.width+ix)*4;const double a=in.blocks[i+3]/255.;for(int c=0;c<3;++c){double v=in.blocks[i+c]/255.;if(!data)v=(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4))*a;sum[c]+=v;}sum[3]+=a;++n;}
  const auto o=(static_cast<std::size_t>(y)*w+x)*4;
  for(int c=0;c<3;++c){double v=sum[c]/n;if(!data){v=sum[3]>0?sum[c]/sum[3]:0;v=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}out.blocks[o+c]=byte(v*255);}out.blocks[o+3]=byte(sum[3]*255/n);
  if(normal){double nx=sum[0]/n*2-1,ny=sum[1]/n*2-1,nz=sum[2]/n*2-1;const auto len=std::sqrt(nx*nx+ny*ny+nz*nz);if(len>1e-8){out.blocks[o]=byte((nx/len+1)*127.5);out.blocks[o+1]=byte((ny/len+1)*127.5);out.blocks[o+2]=byte((nz/len+1)*127.5);}}
 }return out;
}
}
CookedTexture cook_texture(const RgbaImage&image,std::string_view category){
 CookedTexture result;result.format=category=="normal"?TextureFormat::Bc5:category=="mask"?TextureFormat::Bc4:TextureFormat::Bc7;
 if(category=="ui"||image.width()%4||image.height()%4)result.format=TextureFormat::Rgba8;
 std::vector<Bc1MipLevel> originals;originals.push_back({image.width(),image.height(),image.pixels()});while(originals.back().width>1||originals.back().height>1)originals.push_back(downsample(originals.back(),category));
 const double max_error=category=="background"?4:category=="properties"?8:category=="normal"?8:32;
 const double max_rmse=category=="background"?.5:category=="normal"?1.4:category=="properties"?1.5:2.;
 // Compare visible compositing error, not undefined RGB beneath zero alpha.
 // Testing over both black and white also measures loss of faint translucent
 // wisps; an encoder cannot hide an alpha error by changing the RGB value.
 const auto measure_quality=[&]{
  double sq=0,maximum=0;std::uint64_t samples=0;
  for(std::size_t l=0;l<originals.size();++l){
   const auto decoded=decode_texture_level(result.format,result.mips[l]);const auto&original=originals[l].blocks;
   const auto measure=[&](double d){d=std::abs(d);sq+=d*d;maximum=std::max(maximum,d);++samples;};
   for(std::size_t i=0;i<original.size();i+=4){
    if(category=="normal"||category=="mask"||category=="properties"){
     const int channels=category=="normal"?2:category=="mask"?1:4;
     for(int c=0;c<channels;++c)measure(static_cast<double>(original[i+c])-decoded[i+c]);
    }else{
     const double a=original[i+3]/255.,b=decoded[i+3]/255.;
     measure(static_cast<double>(original[i+3])-decoded[i+3]);
     for(int c=0;c<3;++c){const auto d=original[i+c]*a-decoded[i+c]*b;measure(d);measure(d+255*(b-a));}
    }
   }
  }
  result.rmse=samples?std::sqrt(sq/static_cast<double>(samples)):0;result.max_error=maximum;
  return result.rmse<=max_rmse&&maximum<=max_error;
 };
 if(result.format!=TextureFormat::Rgba8){
  // Encode at the fast level first; only gate failures pay for the slower
  // high-effort pass, which rescues hard blocks that would otherwise store raw.
  // BC4/BC5 have no effort knob, so they get a single pass.
  const std::array<int,2> all_efforts{1,BC7ENC_MAX_UBER_LEVEL};
  const auto efforts=result.format==TextureFormat::Bc7?std::span(all_efforts):std::span(all_efforts).first(1);
  for(const int effort:efforts){
   result.mips.clear();
   for(const auto&m:originals)result.mips.push_back(compress_level(m,result.format,effort));
   if(measure_quality())return result;
  }
 }else{
  result.mips=std::move(originals);
  return result;
 }
 result.format=TextureFormat::Rgba8;result.mips=std::move(originals);result.lossless_fallback=true;
 return result;
}
std::shared_ptr<const RgbaImage> select_texture_mip(const RgbaImage&image,int width){if(image.cooked_mips().empty())return {};std::size_t level=0;while(level+1<image.cooked_mips().size()&&image.cooked_mips()[level].width>width)++level;return RgbaImage::create_cooked(image.cooked_format(),{image.cooked_mips().begin()+level,image.cooked_mips().end()});}
}
