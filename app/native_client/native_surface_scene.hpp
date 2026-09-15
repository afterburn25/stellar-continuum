#pragma once

#include "native_colony_controller.hpp"
#include "native_surface_viewport.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace stellar::native_colony_ui {

struct NativeSurfaceSceneDiagnostics {
  std::size_t sites{}, meshes{}, triangles{}, road_segments{};
};

// Projects canonical surface construction data into bounded top-down artwork.
// It owns no campaign state and never generates images or textures.
class NativeSurfaceScene final {
public:
  [[nodiscard]] static float
  footprint_radius(const stellar::native_colony::NativeSurfaceSite &) noexcept;
  [[nodiscard]] static bool
  contains_site(const stellar::native_colony::NativeSurfaceSite &,
                stellar::native_map::Point point, const SurfaceViewport &,
                stellar::native_map::UiRect terrain) noexcept;
  [[nodiscard]] NativeSurfaceSceneDiagnostics
  append(stellar::native_map::DrawList &, const SurfaceViewport &,
         stellar::native_map::UiRect terrain,
         const std::vector<stellar::native_colony::NativeSurfaceSite> &,
         std::optional<int> selected_building_id, int hub_level = 0) const;

private:
  struct CachedRoad {
    int building_id{};
    std::vector<std::pair<float, float>> points;
  };
  void refresh_roads(
      const std::vector<stellar::native_colony::NativeSurfaceSite> &) const;
  mutable std::vector<std::byte> route_key_;
  mutable std::vector<CachedRoad> roads_;
};

} // namespace stellar::native_colony_ui
