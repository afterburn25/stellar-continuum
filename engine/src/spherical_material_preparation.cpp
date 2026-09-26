#include <stellar/engine/spherical_material_preparation.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace stellar::native_map {
namespace {
constexpr double pi=std::numbers::pi;
using Pixel=std::array<double,3>;
double luminance(Pixel p){return .2126*p[0]+.7152*p[1]+.0722*p[2];}
double smooth(double a,double b,double x){x=std::clamp((x-a)/(b-a),0.,1.);return x*x*(3-2*x);}
std::uint64_t hash(std::uint64_t x){x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
double random(std::uint64_t x){return static_cast<double>(hash(x)>>11)*0x1.0p-53;}
double noise(double x,double y,double z,std::uint64_t seed){
 const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y)),iz=static_cast<int>(std::floor(z));
 const double fx=smooth(0,1,x-ix),fy=smooth(0,1,y-iy),fz=smooth(0,1,z-iz);double value=0;
 for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
  const auto key=seed^hash(static_cast<std::uint64_t>(ix+dx))^hash(static_cast<std::uint64_t>(iy+dy)+0x93283)^hash(static_cast<std::uint64_t>(iz+dz)+0x793759);
  value+=random(key)*(dx?fx:1-fx)*(dy?fy:1-fy)*(dz?fz:1-fz);
 }return value;
}
Pixel sample(const RgbaImage& im,double x,double y){
 x=std::clamp(x,0.,static_cast<double>(im.width()-1));y=std::clamp(y,0.,static_cast<double>(im.height()-1));
 const int x0=static_cast<int>(x),y0=static_cast<int>(y),x1=std::min(x0+1,im.width()-1),y1=std::min(y0+1,im.height()-1);
 Pixel p{};for(int c=0;c<3;++c){const auto at=[&](int xx,int yy){return im.pixels()[(static_cast<std::size_t>(yy)*im.width()+xx)*4+c]/255.;};
  p[c]=((1-(x-x0))*at(x0,y0)+(x-x0)*at(x1,y0))*(1-(y-y0))+((1-(x-x0))*at(x0,y1)+(x-x0)*at(x1,y1))*(y-y0);
 }return p;
}
std::uint8_t byte(double x){return static_cast<std::uint8_t>(std::clamp(std::lround(x*255),0L,255L));}
void put(std::vector<std::uint8_t>& p,std::size_t i,Pixel v,double a=1){for(int c=0;c<3;++c)p[i*4+c]=byte(v[c]);p[i*4+3]=byte(a);}
double median(std::vector<double> v){if(v.empty())return 0;auto m=v.begin()+static_cast<std::ptrdiff_t>(v.size()/2);std::nth_element(v.begin(),m,v.end());return *m;}
// Fit a circle to actual colour edges, rather than to a brightness threshold.
// Multiple candidates per ray let RANSAC distinguish the globe from rings.
std::array<double,4> fit_disc(const RgbaImage& source){
 struct Edge{double x,y,strength;};std::vector<Edge> edges;
 const double cx=(source.width()-1)*.5,cy=(source.height()-1)*.5,extent=std::min(source.width(),source.height());
 for(int k=0;k<240;++k){const double angle=k*2*pi/240,dx=std::cos(angle),dy=std::sin(angle);std::vector<std::pair<double,double>> peaks;
  for(double r=extent*.20;r<extent*.495;r+=2){const auto in=sample(source,cx+dx*(r-3),cy+dy*(r-3)),out=sample(source,cx+dx*(r+3),cy+dy*(r+3));
   double delta=0;for(int c=0;c<3;++c)delta+=(in[c]-out[c])*(in[c]-out[c]);
   if(delta>.002)peaks.emplace_back(delta,r);
  }
  std::sort(peaks.rbegin(),peaks.rend());std::vector<double> selected;
  for(const auto& [strength,r]:peaks){bool close=false;for(auto previous:selected)close|=std::abs(previous-r)<extent*.035;
   if(close)continue;edges.push_back({cx+dx*r,cy+dy*r,std::sqrt(strength)});selected.push_back(r);if(selected.size()==4)break;
  }
 }
 std::array<double,4> best{cx,cy,0,0};double best_score=0;
 for(std::uint64_t trial=0;trial<5500&&edges.size()>10;++trial){
  const auto& a=edges[hash(trial*3+1)%edges.size()];const auto& b=edges[hash(trial*3+2)%edges.size()];const auto& c=edges[hash(trial*3+3)%edges.size()];
  const double d=2*(a.x*(b.y-c.y)+b.x*(c.y-a.y)+c.x*(a.y-b.y));if(std::abs(d)<1)continue;
  const double aa=a.x*a.x+a.y*a.y,bb=b.x*b.x+b.y*b.y,cc=c.x*c.x+c.y*c.y;
  const double x=(aa*(b.y-c.y)+bb*(c.y-a.y)+cc*(a.y-b.y))/d,y=(aa*(c.x-b.x)+bb*(a.x-c.x)+cc*(b.x-a.x))/d,r=std::hypot(x-a.x,y-a.y);
  if(std::abs(x-cx)>extent*.12||std::abs(y-cy)>extent*.12||r<extent*.20||r>extent*.495)continue;
  double score=0;int matches=0;std::array<bool,36> sectors{};
  const double tolerance=std::max(4.,extent*.01);
  for(const auto& edge:edges){const double error=std::abs(std::hypot(edge.x-x,edge.y-y)-r);if(error>tolerance)continue;
   score+=(.12+std::min(.3,edge.strength))*(1-error/(tolerance*1.25));++matches;
   const auto sector=std::clamp(static_cast<int>((std::atan2(edge.y-y,edge.x-x)+pi)*36/(2*pi)),0,35);sectors[sector]=true;
  }
  const auto coverage=std::count(sectors.begin(),sectors.end(),true);score*=coverage/36.;
  if(score>best_score&&matches>=55&&coverage>=23){best_score=score;best={x,y,r,coverage/36.};}
 }
 return best;
}
}
SphericalMaterialImages prepare_spherical_material(const RgbaImage& source,const SphericalSourceOptions& o){
 if(o.width<128||o.width>4096||o.width%2||!std::isfinite(o.source_roll_degrees))throw std::invalid_argument("Spherical material width must be even and within 128..4096; source roll must be finite.");
 SphericalMaterialImages result;
 const auto disc=fit_disc(source);const double cx=disc[0],cy=disc[1],radius=disc[2];result.disc={cx,cy,radius};
 if(radius<96||disc[3]<.64){result.rejection_reason="No complete, sufficiently resolved globe silhouette was recovered.";return result;}
 // Fit a bounded low-order illumination field. Huber reweighting prevents
 // bright clouds/lava and dark continents from dominating the lighting fit.
 struct Probe{double x,y,z,log_luma;};std::vector<Probe> probes;
 for(int y=-48;y<=48;++y)for(int x=-48;x<=48;++x){const double nx=x/52.,ny=y/52.,rr=nx*nx+ny*ny;if(rr>.82)continue;
  auto p=sample(source,cx+nx*radius,cy-ny*radius);const auto l=luminance(p);
  if(l<.035||l>.92||(o.emissive&&p[0]>p[1]*1.7&&p[0]>.55))continue;
  probes.push_back({nx,ny,std::sqrt(1-rr),std::log(l)});
 }
 if(probes.size()<500){result.rejection_reason="Insufficient recoverable unlit surface detail.";return result;}
 std::array<double,4> fit{};
 for(int pass=0;pass<4;++pass){double matrix[4][5]{};for(const auto& p:probes){const double f[]={1,p.x,p.y,p.z};double prediction=0;for(int i=0;i<4;++i)prediction+=f[i]*fit[i];
   const double w=pass?std::min(1.,.32/std::max(.001,std::abs(p.log_luma-prediction))):1.;
   for(int i=0;i<4;++i){for(int j=0;j<4;++j)matrix[i][j]+=w*f[i]*f[j];matrix[i][4]+=w*f[i]*p.log_luma;}
  }
  for(int i=0;i<4;++i){matrix[i][i]+=.001;const double divisor=matrix[i][i];for(int j=i;j<=4;++j)matrix[i][j]/=divisor;
   for(int k=0;k<4;++k)if(k!=i){const double factor=matrix[k][i];for(int j=i;j<=4;++j)matrix[k][j]-=factor*matrix[i][j];}
  }for(int i=0;i<4;++i)fit[i]=matrix[i][4];
 }
 if(!std::isfinite(o.maximum_light_gradient)||o.maximum_light_gradient<0||o.maximum_light_gradient>2)throw std::invalid_argument("Invalid source lighting correction limit");
 for(int i=0;i<3;++i)result.removed_light_gradient[i]=std::clamp(fit[i+1],-o.maximum_light_gradient,o.maximum_light_gradient);
 const auto clean=[&](double x,double y){
  const double roll=o.source_roll_degrees*pi/180.,sx=x*std::cos(roll)-y*std::sin(roll),sy=x*std::sin(roll)+y*std::cos(roll);
  x=sx;y=sy;
  const double r2=x*x+y*y,z=std::sqrt(std::max(0.,1-r2));
  auto p=sample(source,cx+x*radius,cy-y*radius);
  if(o.flatten_canopy){p={};constexpr int range=4;for(int yy=-range;yy<=range;++yy)for(int xx=-range;xx<=range;++xx){
    const auto v=sample(source,cx+x*radius+xx*radius*.0025,cy-y*radius+yy*radius*.0025);for(int k=0;k<3;++k)p[k]+=v[k]/81.;
   }}
  const double factor=std::exp(-result.removed_light_gradient[0]*x-result.removed_light_gradient[1]*y-result.removed_light_gradient[2]*(z-.72));
  for(auto& c:p)c=std::clamp(c*factor,0.,1.);return p;
 };
 // Continuous source-detail synthesis on a spherical domain. No palette-index
 // discontinuities, repeated full globe, or copied black backdrop. Unobserved
 // geography is explicitly synthetic; the visible front remains the source.
 std::array<Pixel,256> bands{};
 std::vector<Pixel> palette,land_palette;Pixel water_color{};int water_samples=0;
 for(const auto& probe:probes){const auto p=clean(probe.x,probe.y);palette.push_back(p);
  if(o.liquid&&p[2]-p[0]>.055&&p[2]>p[1]*1.05){for(int k=0;k<3;++k)water_color[k]+=p[k];++water_samples;}
  else if(luminance(p)<.8)land_palette.push_back(p);
 }
 if(land_palette.size()<20)land_palette=palette;
 for(auto& k:water_color)k/=std::max(1,water_samples);
 const auto make_palette=[](std::vector<Pixel> values){std::sort(values.begin(),values.end(),[](Pixel a,Pixel b){return luminance(a)<luminance(b);});std::array<Pixel,64> bins{};
  for(int j=0;j<64;++j){const auto lo=static_cast<std::size_t>((.04+.91*j/64)*values.size()),hi=std::min(values.size(),static_cast<std::size_t>((.04+.91*(j+1)/64)*values.size())+1);
   for(auto n=lo;n<hi;++n)for(int c=0;c<3;++c)bins[j][c]+=values[n][c]/(hi-lo);
  }return bins;
 };
 const auto palette_bins=make_palette(o.liquid?land_palette:palette);
 const double water_fraction=static_cast<double>(water_samples)/probes.size();
 if(o.rings||o.zonal_clouds){for(int row=0;row<256;++row){const double lat=(row+.5)/256*pi-pi*.5,ny=std::sin(lat)*.70;std::array<std::vector<double>,3> values;
   for(int j=-12;j<=12;++j){const auto p=clean(j*.018,ny);for(int k=0;k<3;++k)values[k].push_back(p[k]);}
   for(int k=0;k<3;++k)bands[row][k]=median(values[k]);
  }}
 const int w=o.width,h=w/2;const auto count=static_cast<std::size_t>(w)*h;
 std::vector<std::uint8_t> albedo(count*4),properties(count*4),clouds(count*4),emission(count*4),normal(count*4);
 std::vector<double> cloud_mask(count),height(count);std::vector<Pixel> colors(count);
 double observed=0,surface_area=0;std::size_t black=0,clipped=0;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;
  const double lon=(x+.5)*2*pi/w-pi,lat=pi*.5-(y+.5)*pi/h,nx=std::cos(lat)*std::sin(lon),ny=std::sin(lat),nz=std::cos(lat)*std::cos(lon);
  const double a=noise(nx*3.1,ny*3.1,nz*3.1,o.seed),b=noise(nx*18,ny*18,nz*18,o.seed+1),c=noise(nx*93,ny*93,nz*93,o.seed+2);
  double terrain=0,amplitude=.52,frequency=3.7;for(int octave=0;octave<7;++octave){terrain+=amplitude*noise(nx*frequency,ny*frequency,nz*frequency,o.seed+10+octave);frequency*=2.07;amplitude*=.49;}
  const double index=std::clamp((.5+(terrain-.5)*2.5)*63,0.,62.999);const int bin=static_cast<int>(index);Pixel p{};
  for(int k=0;k<3;++k)p[k]=(palette_bins[bin][k]*(1-(index-bin))+palette_bins[bin+1][k]*(index-bin))*(.94+.12*c);
  if(o.liquid&&water_samples>20){const double water=1-smooth(.29+.42*water_fraction-.015,.29+.42*water_fraction+.015,terrain);
   for(int k=0;k<3;++k)p[k]=p[k]*(1-water)+water_color[k]*water*(.88+.24*b);
  }
  // Blend three continuous planar source patches on the sphere. Each patch
  // stays inside the recovered disc, retaining crater, fissure and coastline
  // detail without copying a globe outline or inventing a blurry far side.
  // High-power normal weights keep texture sharp away from patch transitions.
  Pixel detail{};double total_weight=0;
  const double axes[]{nx,ny,nz};
  for(int axis=0;axis<3;++axis){
   const double weight=std::pow(std::abs(axes[axis]),10);
   if(weight<.00001)continue;
   const double angle=random(o.seed+150+axis)*2*pi;
   const double u=axes[(axis+1)%3],v=axes[(axis+2)%3];
   const double pu=(u*std::cos(angle)-v*std::sin(angle))*.64+(a-.5)*.08;
   const double pv=(u*std::sin(angle)+v*std::cos(angle))*.64+(noise(nx*4+7,ny*4,nz*4,o.seed+155)-.5)*.08;
   const auto patch=clean(pu,pv);
   for(int k=0;k<3;++k)detail[k]+=patch[k]*weight;
   total_weight+=weight;
  }
  const double preserve=o.liquid||o.emissive?.98:.94;
  for(int k=0;k<3;++k)p[k]=detail[k]/total_weight*preserve+p[k]*(1-preserve);
  const double visible=smooth(.30,.53,nz);observed+=visible*std::cos(lat);surface_area+=std::cos(lat);
  if(visible>0){auto original=clean(nx,ny);for(int k=0;k<3;++k)p[k]=p[k]*(1-visible)+original[k]*visible;}
  const double high=*std::max_element(p.begin(),p.end()),low=*std::min_element(p.begin(),p.end());
  double cm=o.separate_clouds?smooth(.47,.87,luminance(p))*(1-smooth(.05,.30,high-low)):0;
  if(o.ice)cm*=.2; // Do not turn an ice sheet into moving clouds.
  // Reconstruct zonal clouds from the unoccluded central strip. Ring geometry
  // never enters the spherical map; the source rings are a separate component.
  if(o.preserve_zonal_detail){
   // Keep the photographed hemisphere, including storm longitude/latitude.
   // The unseen hemisphere reuses latitude-aligned source detail with a smooth
   // longitude continuation. It is explicitly reconstruction, not new geography.
   // Stay just inside the silhouette to avoid importing the black backdrop.
   p=clean(nx*.985,ny*.985);
   observed+=std::max(0.,nz)*std::cos(lat);surface_area+=std::cos(lat);
  }else if(o.rings||o.zonal_clouds){const double row=std::clamp((lat/pi+.5)*255+(a-.5)*3*std::cos(lat),0.,254.);const int lowrow=static_cast<int>(row);for(int k=0;k<3;++k)p[k]=std::clamp((bands[lowrow][k]*(1-(row-lowrow))+bands[lowrow+1][k]*(row-lowrow))*(.94+.12*b),0.,1.);}
  cloud_mask[i]=cm;colors[i]=p;black+=luminance(p)<.018;clipped+=*std::max_element(p.begin(),p.end())>.995;
  const double water=o.liquid?smooth(.04,.19,p[2]-p[0])*(1-cm):0;
  const double ice=o.ice?smooth(.25,.7,luminance(p))*(1-smooth(.12,.4,p[0]-p[2])):0;
  const double lava=o.emissive?smooth(.12,.5,p[0]-p[2])*smooth(.1,.4,p[0]-p[1])*(1-cm):0;
  height[i]=o.opaque_clouds?.5:std::clamp(.5+(luminance(p)-.4)*.12*(1-water)*(o.flatten_canopy?.15:1)+(b-.5)*.025,0.,1.);
  put(properties,i,{std::clamp(.83-.68*water-.34*ice,.08,.96),water,ice},height[i]);
  put(clouds,i,{.93,.96,1},cm);
  put(emission,i,{1.,.23+.4*lava,.035},lava);
 }
 // Inpaint only cloud-occluded terrain. A small bounded iterative diffusion
 // preserves exposed islands and biomes, while clouds use independent alpha.
 if(o.separate_clouds){auto underlying=colors;for(int pass=0;pass<18;++pass){auto previous=underlying;
   for(int y=1;y<h-1;++y)for(int x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;if(cloud_mask[i]<.15)continue;
    const std::size_t neighbors[]={i-w,i+w,static_cast<std::size_t>(y)*w+(x+w-1)%w,static_cast<std::size_t>(y)*w+(x+1)%w};
    Pixel sum{};double total=0;for(auto j:neighbors){const double weight=.1+1-cloud_mask[j];for(int k=0;k<3;++k)sum[k]+=previous[j][k]*weight;total+=weight;}
    for(int k=0;k<3;++k)underlying[i][k]=sum[k]/total;
   }
  }for(std::size_t i=0;i<count;++i)for(int k=0;k<3;++k)colors[i][k]=colors[i][k]*(1-cloud_mask[i])+underlying[i][k]*cloud_mask[i];
 }
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;put(albedo,i,colors[i]);
  const auto left=static_cast<std::size_t>(y)*w+(x+w-1)%w,right=static_cast<std::size_t>(y)*w+(x+1)%w;
  const auto up=static_cast<std::size_t>(std::max(0,y-1))*w+x,down=static_cast<std::size_t>(std::min(h-1,y+1))*w+x;
  double nx=-(height[right]-height[left])*4,ny=-(height[down]-height[up])*4,nz=1,len=std::sqrt(nx*nx+ny*ny+1);put(normal,i,{nx/len*.5+.5,ny/len*.5+.5,nz/len*.5+.5});
 }
 result.observed_surface_fraction=o.preserve_zonal_detail?.5:(o.rings||o.zonal_clouds)?0:observed/surface_area;result.black_fraction=static_cast<double>(black)/count;result.clipped_fraction=static_cast<double>(clipped)/count;
 for(int y=0;y<h;++y)for(int k=0;k<3;++k)result.seam_error+=std::abs(colors[static_cast<std::size_t>(y)*w][k]-colors[static_cast<std::size_t>(y)*w+w-1][k])/(h*3);
 if(!std::isfinite(o.maximum_dark_fraction)||o.maximum_dark_fraction<0||o.maximum_dark_fraction>.5)throw std::invalid_argument("Invalid dark albedo admission limit");
 result.usable=result.black_fraction<o.maximum_dark_fraction&&result.clipped_fraction<.055;
 if(!result.usable)result.rejection_reason="Material preparation retained excessive unrecoverable darkness or clipped highlights.";
 result.albedo=RgbaImage::create(w,h,std::move(albedo));result.properties=RgbaImage::create(w,h,std::move(properties));result.clouds=RgbaImage::create(w,h,std::move(clouds));result.emission=RgbaImage::create(w,h,std::move(emission));result.normal=RgbaImage::create(w,h,std::move(normal));
 result.thumbnail=spherical_material_thumbnail(result);return result;
}
std::shared_ptr<const RgbaImage> spherical_material_thumbnail(const SphericalMaterialImages& maps,int size,double longitude,double cloud_opacity,double emission_strength){
 if(!maps.albedo||size<16||size>1024||!std::isfinite(longitude)||!std::isfinite(cloud_opacity)||!std::isfinite(emission_strength)||cloud_opacity<0||cloud_opacity>1||emission_strength<0)throw std::invalid_argument("Invalid spherical thumbnail source/size/response.");
 for(const auto& layer:{maps.clouds,maps.emission})if(layer&&(layer->width()!=maps.albedo->width()||layer->height()!=maps.albedo->height()))throw std::invalid_argument("Spherical thumbnail layers must share their projection size.");
 std::vector<std::uint8_t> p(static_cast<std::size_t>(size)*size*4);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){const double nx=(x+.5-size*.5)/(size*.47),ny=-(y+.5-size*.5)/(size*.47),rr=nx*nx+ny*ny;if(rr>=1)continue;
  const double nz=std::sqrt(1-rr),raw_u=(std::atan2(nx,nz)+longitude)/(2*pi)+.5,u=raw_u-std::floor(raw_u),v=std::acos(ny)/pi;
  auto color=sample(*maps.albedo,u*maps.albedo->width()-.5,v*maps.albedo->height()-.5);
  const double light=.1+.9*std::max(0.,nx*.45+ny*.3+nz*.84);
  const int sx=std::clamp(static_cast<int>(u*maps.albedo->width()),0,maps.albedo->width()-1),sy=std::clamp(static_cast<int>(v*maps.albedo->height()),0,maps.albedo->height()-1);const auto at=(static_cast<std::size_t>(sy)*maps.albedo->width()+sx)*4;
  const auto cloud=maps.clouds?maps.clouds->pixels()[at+3]/255.*cloud_opacity:0,emissive=maps.emission?maps.emission->pixels()[at+3]/255.*emission_strength:0;
  for(int k=0;k<3;++k)color[k]=(color[k]*(1-cloud)+cloud*.96)*light+(maps.emission?maps.emission->pixels()[at+k]/255.:0)*emissive;
  put(p,static_cast<std::size_t>(y)*size+x,color,smooth(0,.03,1-rr));
 }return RgbaImage::create(size,size,std::move(p));
}
Material3D accretion_disc_material3d(float inner,float outer,double kelvin,float beaming){
 if(!std::isfinite(inner)||!std::isfinite(outer)||inner<=0.f||outer<=inner)
  throw std::invalid_argument("Accretion disc radii must satisfy 0<inner<outer.");
 if(!std::isfinite(kelvin)||kelvin<100||kelvin>100000)
  throw std::invalid_argument("Accretion disc temperature must be between 100 and 100000 kelvin.");
 if(!std::isfinite(beaming)||std::abs(beaming)>1.f)
  throw std::invalid_argument("Accretion disc beaming must be in [-1,1].");
 // Radial column: Shakura–Sunyaev thin-disc T ∝ r^(-3/4); emitted flux
 // ∝ T^4 dims the outer rim while the blackbody curve shifts its hue.
 // linear_light decodes texels as sRGB, so encode gamma here.
 const auto srgb=[](double linear){
  linear=std::clamp(linear,0.,1.);
  const double s=linear<=0.0031308?12.92*linear:1.055*std::pow(linear,1./2.4)-0.055;
  return static_cast<std::uint8_t>(std::lround(s*255.));
 };
 std::vector<std::uint8_t> pixels(256*4);
 for(int i=0;i<256;++i){
  const double r=inner+(outer-inner)*(i+.5)/256.;
  const double t=kelvin*std::pow(r/inner,-.75);
  const auto c=blackbody_light_color(std::clamp(t,100.,100000.));
  const double flux=std::pow(std::clamp(t,100.,100000.)/kelvin,4.);
  pixels[i*4+0]=srgb(c.x*flux);pixels[i*4+1]=srgb(c.y*flux);pixels[i*4+2]=srgb(c.z*flux);pixels[i*4+3]=255;
 }
 Material3D m;
 m.texture=RgbaImage::create(256,1,std::move(pixels));
 m.ambient=1.f;m.diffuse=0.f; // self-luminous plasma
 m.light_color=blackbody_light_color(kelvin);
 m.linear_light=true;
 m.double_sided=true;         // the sheet reads from below the plane too
 m.orbital_beaming=beaming;
 m.anisotropic_texture=true;  // radial streaks minify to arcs
 return m;
}
}
