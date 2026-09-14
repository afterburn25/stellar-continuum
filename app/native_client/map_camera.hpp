#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cmath>
namespace stellar::native_map {
struct WorldPoint { double x{},y{}; };
struct Camera {
  WorldPoint center{};
  double pixels_per_world{1.};
  [[nodiscard]] Point project(WorldPoint world,int width,int height)const{
    // Preserve precision by casting only after camera-relative subtraction.
    return {static_cast<float>((world.x-center.x)*pixels_per_world+static_cast<double>(width)*.5),
            static_cast<float>((world.y-center.y)*pixels_per_world+static_cast<double>(height)*.5)};
  }
  [[nodiscard]] WorldPoint unproject(Point pixel,int width,int height)const{
    return {(static_cast<double>(pixel.x)-static_cast<double>(width)*.5)/pixels_per_world+center.x,
            (static_cast<double>(pixel.y)-static_cast<double>(height)*.5)/pixels_per_world+center.y};
  }
  void pan_pixels(float dx,float dy){if(pixels_per_world>0.){center.x-=static_cast<double>(dx)/pixels_per_world;center.y-=static_cast<double>(dy)/pixels_per_world;}}
  void zoom_at(float wheel,Point pointer,int width,int height){if(width<=0||height<=0||!std::isfinite(wheel))return;const auto before=unproject(pointer,width,height);pixels_per_world=std::clamp(pixels_per_world*std::pow(1.16,static_cast<double>(wheel)),.01,100.);const auto after=unproject(pointer,width,height);center.x+=before.x-after.x;center.y+=before.y-after.y;}
};
} // namespace stellar::native_map
