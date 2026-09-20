#pragma once
#include "native_system_view.hpp"
#include "native_small_body_geometry.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <functional>
#include <map>

namespace stellar::native_system_ui {
using SmallBodyImageProvider=std::function<std::shared_ptr<const stellar::native_map::RgbaImage>(stellar::core::SmallBodyAssetPool,int)>;
struct SmallBodyRenderStatistics{std::size_t fields{},instances{},visible{},batches{},missing_images{},solid_bodies{},large_bodies{};};
struct SmallBodyHit{int field_id{};std::uint32_t body_index{};stellar::native_map::Point screen;float radius{};};
class NativeSmallBodyRenderer {
public:
  void set_images(SmallBodyImageProvider p){images_=std::move(p);}
  // Cosmetic, visible-frame time. It never feeds orbital or resource physics.
  // There are no per-body timers or updates for off-screen systems.
  void advance_tumble(double seconds,bool running){if(running&&std::isfinite(seconds)&&seconds>0)tumble_seconds_+=std::min(seconds,.25);}
  void clear(){cache_.clear();hits_.clear();generation_=0;system_=-1;}
  void render(stellar::native_map::DrawList&,const stellar::native_system::NativeSystemSnapshot&,
      const stellar::native_system::SystemSpatialSnapshot&,const stellar::native_system::SystemSpatialViewport&,
      stellar::native_map::UiRect,double days,bool debug);
  std::optional<SmallBodyHit> hit(stellar::native_map::Point)const;
  const auto& statistics()const{return statistics_;}
  static stellar::native_map::Point position(const stellar::core::SmallBodyField&,const stellar::core::SmallBodyInstance&,
      const stellar::native_system::SystemSpatialSnapshot&,const stellar::native_system::SystemSpatialViewport&,double);
private:
  struct Entry{stellar::core::SmallBodyField field;std::vector<stellar::core::SmallBodyInstance> bodies;};
  std::uint64_t generation_{};int system_{-1};std::map<int,Entry> cache_;
  std::vector<SmallBodyHit> hits_;SmallBodyImageProvider images_;SmallBodyRenderStatistics statistics_;
  NativeSmallBodyGeometry geometry_;
  double tumble_seconds_{};
};
}
