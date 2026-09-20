#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace stellar::native_system_ui {
// Only full equirectangular maps belong here. Isolated planet photographs must
// keep their disc crops; wrapping those photographs would bake in a black limb.
struct PlanetSurfaceAsset {
  std::string_view body_key;
  int layer; // 0: albedo, 1: night emission, 2: clouds
  std::string_view filename;
};
inline constexpr std::array planet_surface_assets{
  PlanetSurfaceAsset{"earth",0,"earth-map.png"},
  PlanetSurfaceAsset{"earth",1,"earth-night-map.jpg"},
  PlanetSurfaceAsset{"earth",2,"earth-clouds.jpg"},
  PlanetSurfaceAsset{"mars",0,"mars-map.png"},
  PlanetSurfaceAsset{"jupiter",0,"jupiter-map.png"},
  PlanetSurfaceAsset{"mercury",0,"mercury-map.png"},
  PlanetSurfaceAsset{"venus",0,"venus-map.png"},
  PlanetSurfaceAsset{"saturn",0,"saturn-map.png"},
  PlanetSurfaceAsset{"uranus",0,"uranus-map.png"},
  PlanetSurfaceAsset{"neptune",0,"neptune-map.png"},
  PlanetSurfaceAsset{"moon",0,"moon-map.png"},
};
// Shared display orientation; it never changes authoritative orbital elements.
struct PlanetPresentationPose { float yaw{},pitch{},roll{}; };
[[nodiscard]] inline PlanetPresentationPose planet_presentation_pose(std::string_view key){
  if(key=="earth")return {-1.30f,.20f,0};
  if(key=="saturn")return {0,.34f,0};
  if(key=="uranus")return {0,.16f,1.47f};
  return {};
}
[[nodiscard]] inline std::optional<std::size_t> planet_surface_asset_index(std::string_view key,int layer){
  for(std::size_t i=0;i<planet_surface_assets.size();++i)
    if(planet_surface_assets[i].body_key==key&&planet_surface_assets[i].layer==layer)return i;
  return std::nullopt;
}
} // namespace stellar::native_system_ui
