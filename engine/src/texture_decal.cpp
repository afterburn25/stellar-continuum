#include <stellar/engine/texture_decal.hpp>
#include <array>
#include <stdexcept>
namespace stellar::native_map {
std::shared_ptr<const RgbaImage> prepare_decal_texture(const RgbaImage& source,int requested,DecalBlendProfile profile){
  if(requested<=0)throw std::invalid_argument("Invalid decal LOD width");
  const int width=std::min(requested,source.width()),height=std::max(1,static_cast<int>(std::lround(width*static_cast<double>(source.height())/source.width())));
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
  // Remove only edge-connected near-black background. Interior black shadows
  // remain opaque, unlike luminous dust. All work stays on the image worker.
  std::vector<std::uint8_t> background;
  if(profile==DecalBlendProfile::OpaqueCutout){
    const int sw=source.width(),sh=source.height();background.resize(static_cast<std::size_t>(sw)*sh);
    std::vector<std::uint32_t> queue;queue.reserve(background.size());
    const auto add=[&](int x,int y){const auto i=static_cast<std::size_t>(y)*sw+x;const auto& p=source.pixels();
      if(!background[i]&&std::max({p[i*4],p[i*4+1],p[i*4+2]})<=18){background[i]=1;queue.push_back(static_cast<std::uint32_t>(i));}};
    for(int x=0;x<sw;++x){add(x,0);add(x,sh-1);}for(int y=0;y<sh;++y){add(0,y);add(sw-1,y);}
    for(std::size_t at=0;at<queue.size();++at){const int x=static_cast<int>(queue[at]%sw),y=static_cast<int>(queue[at]/sw);if(x)add(x-1,y);if(x+1<sw)add(x+1,y);if(y)add(x,y-1);if(y+1<sh)add(x,y+1);}
  }
  const auto smooth=[](double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);};
  const auto byte=[](double v){return static_cast<std::uint8_t>(std::clamp(std::lround(v*255),0l,255l));};
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){
    const double sx0=x*static_cast<double>(source.width())/width,sx1=(x+1)*static_cast<double>(source.width())/width;
    const double sy0=y*static_cast<double>(source.height())/height,sy1=(y+1)*static_cast<double>(source.height())/height;
    std::array<double,3> premult{};double alpha=0,area=0;
    for(int sy=static_cast<int>(sy0);sy<std::min(source.height(),static_cast<int>(std::ceil(sy1)));++sy)
    for(int sx=static_cast<int>(sx0);sx<std::min(source.width(),static_cast<int>(std::ceil(sx1)));++sx){
      const double weight=(std::min(sx1,sx+1.)-std::max(sx0,static_cast<double>(sx)))*(std::min(sy1,sy+1.)-std::max(sy0,static_cast<double>(sy)));
      const auto at=(static_cast<std::size_t>(sy)*source.width()+sx)*4;const auto& p=source.pixels();
      const double r=p[at]/255.,g=p[at+1]/255.,b=p[at+2]/255.;const double light=std::max({r,g,b});
      const double u=(sx+.5)/source.width()*2-1,v=(sy+.5)/source.height()*2-1;
      const double edge=profile==DecalBlendProfile::OpaqueCutout?1:smooth((1-std::hypot(u,v))/.22);
      // Obscuring dust keeps black opaque. Luminous black is removed without
      // thresholding away faint outer haze. Alpha profiles share normal blending.
      const double intrinsic=profile==DecalBlendProfile::OpaqueCutout?(background[at/4]?0.:1.):(profile==DecalBlendProfile::Obscuring?.28+.68*(1-light):std::pow(light,.72));
      const double a=intrinsic*edge*(p[at+3]/255.);alpha+=a*weight;area+=weight;
      const double divisor=profile==DecalBlendProfile::Luminous?std::max(intrinsic,1e-8):1.;
      premult[0]+=r/divisor*a*weight;premult[1]+=g/divisor*a*weight;premult[2]+=b/divisor*a*weight;
    }
    const auto at=(static_cast<std::size_t>(y)*width+x)*4;
    if(alpha>0)for(int c=0;c<3;++c)pixels[at+c]=byte(premult[c]/alpha);
    pixels[at+3]=byte(alpha/std::max(area,1e-8));
    if(x==0||y==0||x==width-1||y==height-1)pixels[at+3]=0;
  }
  return RgbaImage::create(width,height,std::move(pixels));
}
} // namespace stellar::native_map
