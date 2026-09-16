#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace stellar::native_surface {

// Renders the reference client's surface building grammar (SurfaceBuildingVisuals)
// as bounded software sprites: octagonal pad, per-type silhouette, scaffold
// overlay while under construction, powered/offline and priority state rings.
// Textures are cached and keyed by the authoritative building state.
class NativeSurfaceSceneRenderer final {
public:
  inline static constexpr int texture_size = 128;
  inline static constexpr std::size_t maximum_cached_images = 48;

  // phase: 1 = pad + scaffold only, 2 = structure + scaffold, 3 = complete.
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
  image(std::string_view type_id, int phase, bool powered, bool prioritized);
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
  hub_image(int level, bool capital, bool outpost);
  [[nodiscard]] static int phase_for_progress(double progress) noexcept;
  [[nodiscard]] std::size_t cached_images() const noexcept {
    return cache_.size();
  }

private:
  std::map<std::string, std::shared_ptr<const native_map::RgbaImage>> cache_;
};

} // namespace stellar::native_surface
