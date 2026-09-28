#include <stellar/engine/spherical_material_preparation.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace stellar::native_map;
int main(){try{
 constexpr int size=384;std::vector<std::uint8_t> pixels(size*size*4);
 for(int y=0;y<size;++y)for(int x=0;x<size;++x){const double nx=(x-191.5)/166,ny=(y-191.5)/166;if(nx*nx+ny*ny>=1)continue;const double shade=.7+.2*nx;const auto i=(y*size+x)*4;pixels[i]=static_cast<std::uint8_t>(120*shade);pixels[i+1]=static_cast<std::uint8_t>(170*shade);pixels[i+2]=static_cast<std::uint8_t>(220*shade);pixels[i+3]=255;}
 const auto source=RgbaImage::create(size,size,std::move(pixels));SphericalSourceOptions options;options.width=256;options.seed=42;
 const auto a=prepare_spherical_material(*source,options),b=prepare_spherical_material(*source,options);
 const auto require=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
 require(a.usable,"Neutralizable full globe should prepare");require(std::abs(a.disc[2]-166)<5,"Disc fit differs from known silhouette");
 require(a.albedo->pixels()==b.albedo->pixels(),"Preparation must be deterministic");require(a.albedo->width()==256&&a.albedo->height()==128,"Spherical map dimensions");
 require(a.seam_error<.03,"Longitude seam should be continuous");require(a.black_fraction<.005,"Black backdrop must not wrap onto globe");
 const auto& p=a.albedo->pixels();const auto at=[&](int x){return p[(64*256+x)*4];};require(std::abs(int(at(100))-int(at(156)))<12,"Fitted directional shading should be neutralized");
 require(a.thumbnail&&a.normal&&a.properties&&a.clouds&&a.emission,"All material layers and spherical thumbnail required");
 SphericalMaterialImages small;small.albedo=RgbaImage::create(2,1,{40,70,100,255,40,70,100,255});small.clouds=RgbaImage::create(2,1,{240,240,240,255,240,240,240,255});
 const auto bare=spherical_material_thumbnail(small,32,0,0,0),cloudy=spherical_material_thumbnail(small,32,0,1,0);
 require(cloudy->pixels()[(16*32+16)*4]>bare->pixels()[(16*32+16)*4],"Thumbnail must honor the material cloud opacity");
 small.clouds=RgbaImage::create(1,1,{240,240,240,255});bool mismatch=false;try{(void)spherical_material_thumbnail(small,32);}catch(const std::invalid_argument&){mismatch=true;}
 require(mismatch,"Mismatched thumbnail layers must be rejected before sampling");
 // Oriented runtime portraits: known silhouettes and ray ordering, independent
 // of source extraction or any particular planet classification.
 SphericalMaterialImages portrait;portrait.albedo=RgbaImage::create(1,1,{255,0,0,255});
 SphericalThumbnailOptions view;view.size=128;view.shadows=false;
 const auto bounds=[](const RgbaImage& im){std::array<int,4> b{im.width(),im.height(),-1,-1};
   for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)if(im.pixels()[(y*im.width()+x)*4+3]>128){b[0]=std::min(b[0],x);b[1]=std::min(b[1],y);b[2]=std::max(b[2],x);b[3]=std::max(b[3],y);}return b;};
 view.polar_radius=.6;const auto flat=spherical_material_thumbnail(portrait,view);const auto fb=bounds(*flat);
 require(std::abs(double(fb[3]-fb[1])/(fb[2]-fb[0])-.6)<.025,"Portrait silhouette must preserve polar flattening");
 view.orientation=rotation_axis_angle({0,0,1},static_cast<float>(std::acos(-1.)*.5));const auto sideways=spherical_material_thumbnail(portrait,view);const auto sb=bounds(*sideways);
 require(sb[2]-sb[0]==fb[3]-fb[1]&&sb[3]-sb[1]==fb[2]-fb[0],"Saved axis orientation must rotate the flattened silhouette");
 view.polar_radius=1;view.orientation=rotation_axis_angle({1,0,0},static_cast<float>(std::acos(-1.)/6));
 view.ring_inner_radius=1.15;view.ring_outer_radius=2;view.rings=RgbaImage::create(1,1,{0,0,255,128});
 const auto rings=spherical_material_thumbnail(portrait,view);
 require(rings->pixels()==spherical_material_thumbnail(portrait,view)->pixels(),"Oriented portrait must be deterministic");
 // At this 30-degree tilt, local ring Z>0 projects below the center and is in
 // front of the planet. The upper half is correctly hidden behind the globe.
 const auto pixel_at=[](const RgbaImage& im,double x,double y){const double scale=im.width()*.47/2;
   const int px=static_cast<int>(im.width()*.5+x*scale),py=static_cast<int>(im.height()*.5-y*scale);
   return std::array<int,4>{im.pixels()[(py*im.width()+px)*4],im.pixels()[(py*im.width()+px)*4+1],im.pixels()[(py*im.width()+px)*4+2],im.pixels()[(py*im.width()+px)*4+3]};};
 const auto front=pixel_at(*rings,0,-.72),back=pixel_at(*rings,0,.72),outside=pixel_at(*rings,1.6,0);
 require(front[0]>5&&front[2]>5&&front[3]==255,"Front ring must blend over the opaque globe");
 require(back[0]>5&&back[2]==0&&back[3]==255,"Rear ring must be hidden by the globe");
 require(outside[0]==0&&outside[2]>0&&outside[3]==128,"Isolated ring opacity must remain straight alpha");
 std::vector<std::uint8_t> radial(128*4);for(int x=0;x<128;++x){radial[x*4+2]=255;radial[x*4+3]=x>29&&x<58?0:255;}
 view.rings=RgbaImage::create(128,1,std::move(radial));const auto gap=spherical_material_thumbnail(portrait,view);
 require(pixel_at(*gap,0,-.72)[2]==0&&pixel_at(*gap,1.45,0)[3]==0,"Radial gaps must expose the globe and transparent backdrop");
 for(int i=0;i<view.size;++i)for(const int edge:{i,view.size*(view.size-1)+i,i*view.size,i*view.size+view.size-1})
   require(gap->pixels()[edge*4+3]==0,"Automatic framing must not clip rotated rings");
 view.rings=RgbaImage::create(1,1,{255,255,255,255});view.light_direction={.45f,-.7f,.84f};
 const auto unshadowed=spherical_material_thumbnail(portrait,view);view.shadows=true;const auto shadowed=spherical_material_thumbnail(portrait,view);
 int planet_shadow=0,ring_shadow=0;for(std::size_t i=0;i<shadowed->pixels().size();i+=4){
   if(int(unshadowed->pixels()[i])-shadowed->pixels()[i]>10){if(unshadowed->pixels()[i+2]==0)++planet_shadow;else ++ring_shadow;}}
 std::filesystem::create_directories("portrait-test-captures");encode_rgba_png(*unshadowed,"portrait-test-captures/unshadowed.png");encode_rgba_png(*shadowed,"portrait-test-captures/shadowed.png");
 std::cout<<"Portrait shadow pixels: surface="<<planet_shadow<<" ring="<<ring_shadow<<'\n';
 require(planet_shadow>20&&ring_shadow>20,"Portrait must cast rings onto the globe and the globe onto rings");
 auto invalid=view;invalid.orientation.w=std::numeric_limits<float>::quiet_NaN();bool invalid_rejected=false;
 try{(void)spherical_material_thumbnail(portrait,invalid);}catch(const std::invalid_argument&){invalid_rejected=true;}
 require(invalid_rejected,"Invalid portrait transforms must be rejected before rendering");
 invalid=view;invalid.ring_inner_radius=invalid.ring_outer_radius;invalid_rejected=false;
 try{(void)spherical_material_thumbnail(portrait,invalid);}catch(const std::invalid_argument&){invalid_rejected=true;}
 require(invalid_rejected,"Degenerate ring dimensions must be rejected");
 view.orientation={};const auto edge_on=spherical_material_thumbnail(portrait,view);
 require(pixel_at(*edge_on,1.6,0)[3]==0&&pixel_at(*edge_on,0,0)[3]==255,"Exactly edge-on rings must not create NaNs or a solid strip");
 portrait.clouds=RgbaImage::create(2,1,{255,255,255,255,255,255,255,255});invalid_rejected=false;
 try{(void)spherical_material_thumbnail(portrait,view);}catch(const std::invalid_argument&){invalid_rejected=true;}
 require(invalid_rejected,"Oriented portraits must reject mismatched layer dimensions");
 options.zonal_clouds=true;const auto bands=prepare_spherical_material(*source,options);
 require(bands.usable&&bands.seam_error<.03,"Zonal reconstruction must retain a usable continuous sphere");
 const auto& zonal=bands.albedo->pixels();
 for(int y=25;y<100;y+=15){int lo=255,hi=0;for(int x=0;x<256;++x){lo=std::min(lo,int(zonal[(y*256+x)*4]));hi=std::max(hi,int(zonal[(y*256+x)*4]));}require(hi-lo<30,"Zonal reconstruction retained directional hemisphere lighting");}
 std::cout<<"Spherical preparation: silhouette, light removal, determinism, seams and material outputs passed.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
