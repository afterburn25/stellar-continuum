#pragma once
#include <stellar/core/small_body_fields.hpp>
#include <stellar/engine/native_solid_mesh.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <numbers>

namespace stellar::native_system_ui {
// Presentation only: the version-one seed stream, scale and mining yield remain
// unchanged. Recover its uniform quantile to give rare giants a visible tail.
inline float small_body_display_radius(const stellar::core::SmallBodyInstance& b){
  const auto q=std::cbrt(std::clamp((b.scale-.5)/3.5,0.,1.));
  if(q<.70)return static_cast<float>(std::lerp(1.3,4.,q/.70));
  if(q<.92)return static_cast<float>(std::lerp(4.,13.,(q-.70)/.22));
  if(q<.985)return static_cast<float>(std::lerp(13.,36.,(q-.92)/.065));
  return static_cast<float>(std::lerp(36.,78.,(q-.985)/.015));
}
inline const char* small_body_size_name(const stellar::core::SmallBodyInstance& b){
  const auto radius=small_body_display_radius(b);
  return radius>=36?"Huge":radius>=13?"Large":radius>=4?"Medium":"Small";
}

// Bounded immutable resources: 24 shapes at two detail levels and 32 seamless
// albedo maps. Rotations use the saved body's full three-axis analytic spin.
class NativeSmallBodyGeometry {
  using Vec3=stellar::native_map::Vec3;
  using Mesh=stellar::native_map::Mesh3D;
  using Image=stellar::native_map::RgbaImage;
  std::map<int,std::shared_ptr<const Mesh>> meshes_;
  std::map<int,std::shared_ptr<const Image>> images_;
  std::map<int,std::shared_ptr<const Image>> optical_images_;
  std::shared_ptr<const Image> environment_;
  static float hash(int x,int y,int z,std::uint32_t seed){
    auto h=std::uint32_t(x)*0x8da6b343u^std::uint32_t(y)*0xd8163841u^std::uint32_t(z)*0xcb1ab31fu^seed;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return static_cast<float>(h&0xffffffu)/16777215.f;
  }
  static float noise(Vec3 p,float frequency,std::uint32_t seed){
    p={p.x*frequency,p.y*frequency,p.z*frequency};
    const int x=static_cast<int>(std::floor(p.x)),y=static_cast<int>(std::floor(p.y)),z=static_cast<int>(std::floor(p.z));
    const auto smooth=[](float t){return t*t*(3-2*t);};
    const float u=smooth(p.x-x),v=smooth(p.y-y),w=smooth(p.z-z);
    return std::lerp(std::lerp(std::lerp(hash(x,y,z,seed),hash(x+1,y,z,seed),u),std::lerp(hash(x,y+1,z,seed),hash(x+1,y+1,z,seed),u),v),
                     std::lerp(std::lerp(hash(x,y,z+1,seed),hash(x+1,y,z+1,seed),u),std::lerp(hash(x,y+1,z+1,seed),hash(x+1,y+1,z+1,seed),u),v),w);
  }
public:
  std::shared_ptr<const Mesh> mesh(const stellar::core::SmallBodyInstance& b,bool detailed){
    const int shape=static_cast<int>((b.id*17u+std::uint32_t(b.asset_variant_id)*7u+b.local_cluster_id*3u)%24u);
    const int key=shape*2+int(detailed);if(auto it=meshes_.find(key);it!=meshes_.end())return it->second;
    const auto seed=std::uint32_t(shape)*7919u+347u;
    Vec3 axes{.65f+hash(1,0,0,seed)*.35f,.58f+hash(2,0,0,seed)*.42f,.60f+hash(3,0,0,seed)*.40f};
    // Six of 24 forms are long fractured bodies, with varying long axes.
    // Shape selection never consumes or changes the saved generator stream.
    if(shape%4==0){axes={1.f,.27f+hash(2,0,0,seed)*.12f,.30f+hash(3,0,0,seed)*.13f};
      if(shape%3==1)std::swap(axes.x,axes.y);else if(shape%3==2)std::swap(axes.x,axes.z);}
    struct Crater{Vec3 center;float width,depth;};std::array<Crater,9> craters;
    for(int i=0;i<9;++i){const float y=hash(i,1,0,seed)*2-1,a=hash(i,2,0,seed)*2*std::numbers::pi_v<float>,r=std::sqrt(1-y*y);
      craters[i]={{r*std::cos(a),y,r*std::sin(a)},.12f+hash(i,3,0,seed)*.28f,.04f+hash(i,4,0,seed)*.10f};}
    auto result=stellar::native_map::directional_solid_mesh([=](Vec3 n){
      float r=.84f+.30f*(noise(n,2.2f,seed)-.5f)+.13f*(noise(n,5.8f,seed)-.5f)+.055f*(noise(n,16.f,seed)-.5f);
      for(const auto& c:craters){const float d=std::hypot(n.x-c.center.x,n.y-c.center.y,n.z-c.center.z)/c.width;
        if(d<1.4f)r+=-c.depth*std::exp(-d*d*3)+c.depth*.25f*std::exp(-(d-1)*(d-1)*45);}
      return Vec3{n.x*r*axes.x,n.y*r*axes.y,n.z*r*axes.z};
    },detailed?96:24,detailed?48:12);
    // Unit bounds make drawing, focus and selection use the same size.
    auto vertices=result->vertices();const float radius=result->bounding_radius();
    for(auto& v:vertices){v.position.x/=radius;v.position.y/=radius;v.position.z/=radius;}
    result=Mesh::create(std::move(vertices),result->indices());meshes_.emplace(key,result);return result;
  }
  std::shared_ptr<const Image> texture(const stellar::core::SmallBodyInstance& b){
    const int key=static_cast<int>(b.material)*4+b.asset_variant_id-1;if(auto it=images_.find(key);it!=images_.end())return it->second;
    constexpr int width=512,height=256;std::vector<std::uint8_t> pixels(width*height*4);
    constexpr std::array<std::array<float,3>,8> palette{{{.60f,.56f,.50f},{.66f,.65f,.61f},{.33f,.32f,.30f},{.85f,.89f,.91f},{.72f,.79f,.81f},{.83f,.83f,.78f},{.65f,.69f,.70f},{.77f,.82f,.83f}}};
    const auto base=palette[static_cast<int>(b.material)];const auto seed=std::uint32_t(key)*1543u+17u;
    const bool icy=b.material>=stellar::core::SmallBodyMaterial::WaterIce;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
      const float lat=std::numbers::pi_v<float>*(.5f-float(y)/(height-1)),lon=std::numbers::pi_v<float>*(2.f*x/(width-1)-1);
      const Vec3 n{std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon)};
      const float coarse=noise(n,3.4f,seed),medium=noise(n,24,seed),fine=noise(n,125,seed);
      float shade=.60f+.38f*coarse+.23f*(medium-.5f)+.20f*(fine-.5f);
      const float clear=icy?std::clamp((.59f-coarse)*7,0.f,1.f):0;
      if(icy){const float inclusions=std::clamp((.46f-coarse)*7,0.f,1.f);shade=shade*(1-inclusions*.35f);}
      const auto at=(y*width+x)*4;
      for(int c=0;c<3;++c){const float ice_tint=icy?std::lerp(1.f,std::array{.68f,.86f,1.f}[c],clear):1.f;
        pixels[at+c]=static_cast<std::uint8_t>(std::clamp(base[c]*shade*ice_tint*255,0.f,255.f));}pixels[at+3]=255;
    }
    auto result=Image::create(width,height,std::move(pixels));images_.emplace(key,result);return result;
  }
  std::optional<stellar::native_map::Dielectric3D> optics(const stellar::core::SmallBodyInstance& b){
    if(b.material<stellar::core::SmallBodyMaterial::WaterIce)return std::nullopt;
    if(!environment_){
      constexpr int w=1024,h=512;std::vector<std::uint8_t> pixels(w*h*4);
      for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const float lat=std::numbers::pi_v<float>*(.5f-float(y)/(h-1)),lon=std::numbers::pi_v<float>*(2.f*x/(w-1)-1);
        const Vec3 n{std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon)};
        const float band=std::exp(-std::pow((n.y+.3f*n.x)*6,2.f));
        const float star=hash(x==w-1?0:x,y,0,6541)>.999f?.65f:0.f;
        const auto at=(y*w+x)*4;
        pixels[at]=static_cast<std::uint8_t>(2+band*8+star*230);
        pixels[at+1]=static_cast<std::uint8_t>(3+band*9+star*235);
        pixels[at+2]=static_cast<std::uint8_t>(5+band*12+star*240);pixels[at+3]=255;
      }
      environment_=Image::create(w,h,std::move(pixels));
    }
    const int key=static_cast<int>(b.material)*4+b.asset_variant_id-1;
    if(!optical_images_.contains(key)){
      constexpr int w=512,h=256;std::vector<std::uint8_t> pixels(w*h*4);const auto seed=std::uint32_t(key)*1543u+17u;
      for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const float lat=std::numbers::pi_v<float>*(.5f-float(y)/(h-1)),lon=std::numbers::pi_v<float>*(2.f*x/(w-1)-1);
        const Vec3 n{std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon)};
        const float coarse=noise(n,3.4f,seed),fine=noise(n,80,seed);
        const float clear=std::clamp((.59f-coarse)*7.f,0.f,1.f);
        const auto at=(y*w+x)*4;
        pixels[at]=static_cast<std::uint8_t>(255*std::clamp(.87f-.73f*clear+.08f*(fine-.5f),.10f,1.f));
        pixels[at+1]=static_cast<std::uint8_t>(255*(.04f+.86f*clear));
        pixels[at+2]=static_cast<std::uint8_t>(255*(.3f+.7f*coarse));
        pixels[at+3]=static_cast<std::uint8_t>(255*(.65f*fine+.35f*noise(n,34,seed)));
      }
      optical_images_[key]=Image::create(w,h,std::move(pixels));
    }
    stellar::native_map::Dielectric3D d;d.environment=environment_;d.surface=optical_images_.at(key);
    d.roughness=1;d.transmission=b.material==stellar::core::SmallBodyMaterial::RockIce?.24f:.58f;
    d.thickness=2.4f;d.absorption={.45f,.16f,.07f};d.specular_strength=2.5f;d.surface_relief=.006f;
    return d;
  }
};
}
