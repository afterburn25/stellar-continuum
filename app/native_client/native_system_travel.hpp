#pragma once

#include "native_system_view.hpp"
#include <stellar/core/campaign_frame.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_system_travel {

inline constexpr float local_chart_render_radius_factor=1.65F;

struct NativeLocalFleetMarker {
  int fleet_id{};
  std::string name;
  stellar::core::FleetRole role{};
  std::optional<std::string> design_id;
  stellar::core::Vec2 chart_position{},chart_target{};
  stellar::core::FleetTransitPhase phase{};
  bool moving{},held{};
  int mission_order_revision{};
  bool foreign_inspection{};
};

struct NativeLocalLaneMarker {
  int destination_system_id{};
  std::optional<std::string> known_label;
  stellar::core::Vec2 direction{},transit_gate{};
  std::optional<double> known_length_light_years;
};

struct NativeSystemTravelSnapshot {
  std::uint64_t campaign_generation{};
  int observer_civilization_id{},system_id{};
  std::vector<NativeLocalFleetMarker> fleets;
  std::vector<NativeLocalLaneMarker> lanes;
};

struct NativeSystemTravelBuildResult {
  std::optional<NativeSystemTravelSnapshot> snapshot;
  std::string denial;
};

class NativeSystemTravelController final {
public:
  [[nodiscard]] NativeSystemTravelBuildResult build(
      stellar::core::CampaignFrame&,std::uint64_t campaign_generation,
      const stellar::native_system::NativeSystemSnapshot&);
  [[nodiscard]] bool is_current_generation(std::uint64_t)const noexcept;
  void set_localization(
      const stellar::engine::LocalizationTable* table)noexcept{locale_=table;}
private:
  void require_owner()const;
  [[nodiscard]] std::string tr(std::string_view,std::string_view)const;
  const stellar::engine::LocalizationTable* locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
};

struct NativeLaneLabelMetrics {
  int destination_system_id{};
  float width{},height{};
};

struct NativeLocalLaneGeometry {
  int destination_system_id{};
  stellar::native_map::Point transit_gate{},center{},base_a{},base_b{},apex{},label_center{};
  stellar::native_map::UiRect bounds{},label_bounds{};
  float label_rotation_radians{};
};

[[nodiscard]] stellar::native_map::Point local_fleet_anchor(
    const NativeLocalFleetMarker&,
    const stellar::native_system::SystemSpatialSnapshot&,
    const stellar::native_system::SystemSpatialViewport&)noexcept;
[[nodiscard]] std::vector<int> hit_local_fleets(
    std::span<const NativeLocalFleetMarker>,
    const stellar::native_system::SystemSpatialSnapshot&,
    const stellar::native_system::SystemSpatialViewport&,
    stellar::native_map::Point,float hit_radius=15.F);
[[nodiscard]] float local_orbital_boundary_radius(
    const stellar::native_system::SystemSpatialSnapshot&,
    const stellar::native_system::SystemSpatialViewport&)noexcept;
[[nodiscard]] std::vector<NativeLocalLaneGeometry> layout_local_lanes(
    const stellar::native_system::SystemSpatialSnapshot&,
    const stellar::native_system::SystemSpatialViewport&,
    std::span<const NativeLocalLaneMarker>,
    std::span<const NativeLaneLabelMetrics>);
[[nodiscard]] bool hit_local_lane(const NativeLocalLaneGeometry&,
                                  stellar::native_map::Point)noexcept;
[[nodiscard]] bool local_lane_visible(const NativeLocalLaneGeometry&,
                                      stellar::native_map::UiRect viewport,
                                      float margin=8.F)noexcept;

} // namespace stellar::native_system_travel
