#include "native_system_travel.hpp"
#include <stellar/core/fleet_transit.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_travel;
int main(int argc,char** argv)try{
  if(argc!=3)throw std::invalid_argument("Expected font and capture directory");
  const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
  Window window("Stellar navigation visual validation",1280,720,false,argv[1]);
  DrawList draw;
  const UiRect clip{0,0,1280,720};
  draw.world.emplace_back(Text{{24,16},"SYSTEM NAMES ACROSS THE ARROW BASE",{215,230,225,255},20});
  for(int i=0;i<8;++i){
    const float angle=(i-2)*std::numbers::pi_v<float>/4;
    const Point center{160.f+(i%4)*320,215.f+(i/4)*320};
    const auto direction=stellar::core::Vec2{std::cos(angle),std::sin(angle)};
    const std::string name=i%2?"Alpha Centauri | 4.37 ly":"Sol | 4.37 ly";
    const auto extent=window.measure_text(Text{{},name,{210,226,218,245},15});
    const std::array lanes{NativeLocalLaneMarker{.destination_system_id=i,.direction=direction}};
    const std::array metrics{NativeLaneLabelMetrics{i,static_cast<float>(extent.width),static_cast<float>(extent.height)}};
    SystemSpatialSnapshot spatial{.design_radius=20};
    const auto geometry=layout_local_lanes(spatial,{0,0,1},{-10000,-10000,20000,20000},lanes,metrics).front();
    const Point base{(geometry.base_a.x+geometry.base_b.x)*.5f,(geometry.base_a.y+geometry.base_b.y)*.5f};
    const auto move=[&](Point p){return Point{p.x-base.x+center.x,p.y-base.y+center.y};};
    const Color color{142,220,166,255};
    draw.world.emplace_back(TriangleMesh{{move(geometry.base_a),move(geometry.base_b),move(geometry.apex)},{0,1,2},color,clip});
    const auto label=move(geometry.label_center);
    draw.world.emplace_back(Text{{label.x,label.y-extent.height*.5f},name,color,15,0,clip,TextAlign::Center,FontFace::Interface,geometry.label_rotation_radians*180.f/std::numbers::pi_v<float>});
    // Validate against the actual broad edge, rather than repeating angle math.
    const float bx=geometry.base_a.x-geometry.base_b.x,by=geometry.base_a.y-geometry.base_b.y;
    if(std::abs(bx*std::sin(geometry.label_rotation_radians)-by*std::cos(geometry.label_rotation_radians))>.001f)
      throw std::runtime_error("Name baseline does not follow the arrow base");
  }
  window.draw(draw,output/"arrows-eight-directions.bmp");
  std::cout<<"Eight directional arrow baselines and native text capture passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
