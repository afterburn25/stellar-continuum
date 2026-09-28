#include <stellar/engine/spherical_material_preparation.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace stellar::native_map {
namespace {
constexpr double pi=std::numbers::pi;
constexpr double miss=std::numeric_limits<double>::infinity();
struct V {
  double x{},y{},z{};
  V operator+(V b)const{return {x+b.x,y+b.y,z+b.z};}
  V operator*(double s)const{return {x*s,y*s,z*s};}
};
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V v){const auto n=std::sqrt(dot(v,v));return v*(1/n);}
V rotate(Quaternion q,V v){const V axis{q.x,q.y,q.z};return v+cross(axis,cross(axis,v)+v*q.w)*2;}
using RGBA=std::array<double,4>; // premultiplied within the compositor/sampler
RGBA over(RGBA front,RGBA back){for(int c=0;c<4;++c)front[c]+=back[c]*(1-front[3]);return front;}
// Longitude wraps for spherical maps; the annulus instead clamps radial U
// and wraps azimuth V. Interpolate premultiplied colors to preserve clear edges.
RGBA sample(const RgbaImage& im,double u,double v,bool spherical){
  const auto wrap=[](double a){return a-std::floor(a);};
  u=spherical?wrap(u):std::clamp(u,0.,1.);v=spherical?std::clamp(v,0.,1.):wrap(v);
  const double x=u*im.width()-.5,y=v*im.height()-.5;
  const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
  const double fx=x-ix,fy=y-iy;RGBA out{};
  const auto coord=[](int i,int n,bool repeat){return repeat?(i%n+n)%n:std::clamp(i,0,n-1);};
  for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
    const auto at=(static_cast<std::size_t>(coord(iy+dy,im.height(),!spherical))*im.width()+coord(ix+dx,im.width(),spherical))*4;
    const double a=im.pixels()[at+3]/255.,weight=(dx?fx:1-fx)*(dy?fy:1-fy);
    for(int c=0;c<3;++c)out[c]+=im.pixels()[at+c]/255.*a*weight;
    out[3]+=a*weight;
  }return out;
}
// Positive entry along a ray, in ellipsoid-local coordinates. A caller on
// the boundary can see the exit root when testing an external blocker.
double ellipsoid(V origin,V direction,double polar){
  origin.y/=polar;direction.y/=polar;
  const double aa=dot(direction,direction),b=dot(origin,direction),c=dot(origin,origin)-1,d=b*b-aa*c;
  if(d<0)return miss;const double root=std::sqrt(d),near=(-b-root)/aa,far=(-b+root)/aa;
  return near>1e-7?near:far>1e-7?far:miss;
}
bool bounded(double x,double lo,double hi){return std::isfinite(x)&&x>=lo&&x<=hi;}
}

std::shared_ptr<const RgbaImage> spherical_material_thumbnail(const SphericalMaterialImages& maps,const SphericalThumbnailOptions& o){
  const auto& q=o.orientation;
  const double qn=static_cast<double>(q.x)*q.x+static_cast<double>(q.y)*q.y+static_cast<double>(q.z)*q.z+static_cast<double>(q.w)*q.w;
  V light{o.light_direction.x,o.light_direction.y,o.light_direction.z};
  if(!maps.albedo||o.size<16||o.size>1024||!bounded(o.polar_radius,.5,1)||
     !bounded(o.cloud_opacity,0,1)||!bounded(o.emission_strength,0,16)||!bounded(qn,1e-12,1e12)||!bounded(dot(light,light),1e-12,1e12)||
     (o.rings&&(!bounded(o.ring_inner_radius,1,1e4)||!bounded(o.ring_outer_radius,o.ring_inner_radius+.001,1e4))))
    throw std::invalid_argument("Invalid oriented spherical thumbnail descriptor.");
  for(const auto& layer:{maps.clouds,maps.emission})if(layer&&(layer->width()!=maps.albedo->width()||layer->height()!=maps.albedo->height()))
    throw std::invalid_argument("Spherical thumbnail layers must share their projection size.");
  const double inv=1/std::sqrt(qn);
  const Quaternion pose{static_cast<float>(q.x*inv),static_cast<float>(q.y*inv),static_cast<float>(q.z*inv),static_cast<float>(q.w*inv)},inverse{-pose.x,-pose.y,-pose.z,pose.w};
  light=unit(rotate(inverse,unit(light)));
  const V right=rotate(inverse,{1,0,0}),up=rotate(inverse,{0,1,0}),forward=rotate(inverse,{0,0,1});
  const auto silhouette=[&](V v){return std::sqrt(v.x*v.x+v.y*v.y*o.polar_radius*o.polar_radius+v.z*v.z);};
  double extent=std::max(silhouette(right),silhouette(up));
  if(o.rings)extent=std::max({extent,o.ring_outer_radius*std::hypot(right.x,right.z),o.ring_outer_radius*std::hypot(up.x,up.z)});
  extent/=.94;const double pixel=extent*2/o.size,depth=std::max(2.,o.rings?o.ring_outer_radius+1:2.);
  const V ray=forward*-1;
  const auto ring_sample=[&](V p,double footprint){
    RGBA result{};if(!o.rings)return result;
    const double radius=std::hypot(p.x,p.z),v=std::atan2(p.z,p.x)/(2*pi),span=o.ring_outer_radius-o.ring_inner_radius;
    // Fixed work bounds even for very narrow rings; integrated alpha retains
    // gaps and avoids turning finely spaced bands into moire at icon sizes.
    for(int tap=0;tap<8;++tap){const double r=radius+((tap+.5)/8-.5)*footprint;
      if(r<o.ring_inner_radius||r>o.ring_outer_radius)continue;
      const auto s=sample(*o.rings,(r-o.ring_inner_radius)/span,v,false);
      for(int c=0;c<4;++c)result[c]+=s[c]/8;
    }return result;
  };
  const auto surface_visibility=[&](V p){
    if(!o.shadows||!o.rings||std::abs(light.y)<1e-8)return 1.;
    const double t=-p.y/light.y;if(t<=1e-7)return 1.;
    return 1-ring_sample(p+light*t,pixel/std::max(.15,std::abs(light.y)))[3];
  };
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(o.size)*o.size*4);
  for(int y=0;y<o.size;++y)for(int x=0;x<o.size;++x){
    RGBA total{};
    // Four subpixels resolve the globe silhouette and thin, tilted ring edges.
    for(int sy=0;sy<2;++sy)for(int sx=0;sx<2;++sx){
      const V origin=right*((x+(sx+.5)/2-o.size*.5)*pixel)+up*((o.size*.5-y-(sy+.5)/2)*pixel)+forward*depth;
      const double globe_t=ellipsoid(origin,ray,o.polar_radius);RGBA color{};
      if(std::isfinite(globe_t)){
        const V p=origin+ray*globe_t,normal=unit({p.x,p.y/(o.polar_radius*o.polar_radius),p.z});
        const double u=std::atan2(p.x,p.z)/(2*pi)+.5,v=std::acos(std::clamp(p.y/o.polar_radius,-1.,1.))/pi;
        color=sample(*maps.albedo,u,v,true);
        const double direct=std::max(0.,dot(normal,light))*surface_visibility(p),illumination=(o.linear_light?.045:.075)+(o.linear_light?.95:.9)*direct;
        for(int c=0;c<3;++c){if(o.linear_light){const double linear=color[c]<=.04045?color[c]/12.92:std::pow((color[c]+.055)/1.055,2.4);const double lit=linear*illumination;color[c]=lit<=.0031308?lit*12.92:1.055*std::pow(lit,1/2.4)-.055;}else color[c]*=illumination;}
        if(maps.emission){auto e=sample(*maps.emission,u,v,true);const double strength=std::min(o.emission_strength,e[3]>0?1/e[3]:0.);
          for(auto& c:e)c*=strength;color=over(e,color);}
        if(maps.clouds){auto cloud=sample(*maps.clouds,u,v,true);for(int c=0;c<3;++c)cloud[c]*=o.cloud_opacity*(.13+.84*direct);cloud[3]*=o.cloud_opacity;color=over(cloud,color);}
      }
      if(o.rings&&std::abs(ray.y)>1e-8){const double ring_t=-origin.y/ray.y;
        if(ring_t>0&&ring_t<globe_t){const V p=origin+ray*ring_t;
          // Radial footprint from the exact plane derivatives, not the highly
          // elongated tangential footprint of an almost edge-on ring.
          const V dx=right+ray*(-right.y/ray.y),dy=up+ray*(-up.y/ray.y);const double radius=std::max(1e-8,std::hypot(p.x,p.z));
          const double footprint=pixel*.5*(std::abs(p.x*dx.x+p.z*dx.z)+std::abs(p.x*dy.x+p.z*dy.z))/radius;
          auto ring=ring_sample(p,footprint);const bool shadow=o.shadows&&std::isfinite(ellipsoid(p,light,o.polar_radius));
          const double illumination=.17+(shadow?0:.8*std::abs(light.y));
          for(int c=0;c<3;++c)ring[c]*=illumination;color=over(ring,color);
        }
      }
      for(int c=0;c<4;++c)total[c]+=color[c]*.25;
    }
    const auto at=(static_cast<std::size_t>(y)*o.size+x)*4;
    for(int c=0;c<4;++c){const double value=c==3?total[3]:total[3]>0?total[c]/total[3]:0;
      pixels[at+c]=static_cast<std::uint8_t>(std::clamp(std::lround(value*255),0L,255L));}
  }
  return RgbaImage::create(o.size,o.size,std::move(pixels));
}
}
