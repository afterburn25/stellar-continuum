#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace stellar::engine {
// Immutable, bounded scalar field shared by procedural placement and previews.
// Coordinates outside the unit rectangle have zero density; no image I/O here.
class DensityMask final {
public:
  DensityMask(int width,int height,std::vector<std::uint8_t> samples):width_(width),height_(height),samples_(std::move(samples)) {
    if(width<2||height<2||width>512||height>512||samples_.size()!=static_cast<std::size_t>(width)*height)
      throw std::invalid_argument("Invalid bounded density mask");
  }
  [[nodiscard]] double sample(double u,double v)const noexcept {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||v<0||u>1||v>1)return 0;
    const double x=u*(width_-1),y=v*(height_-1);const int ix=static_cast<int>(x),iy=static_cast<int>(y);
    const int nx=std::min(ix+1,width_-1),ny=std::min(iy+1,height_-1);
    const auto at=[&](int a,int b){return samples_[b*width_+a]/255.;};
    return std::lerp(std::lerp(at(ix,iy),at(nx,iy),x-ix),std::lerp(at(ix,ny),at(nx,ny),x-ix),y-iy);
  }
private:int width_,height_;std::vector<std::uint8_t> samples_;
};
}
