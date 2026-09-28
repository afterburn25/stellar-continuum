#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace stellar::native_orbital {

// Renders the same staged orbital infrastructure primitives the reference
// client builds as Godot meshes, rasterized here into a bounded CPU texture so
// the 2D map pipeline can present them unchanged. Phases match the reference:
// ceil(progress * 4) clamped to [1, 4].
class NativeOrbitalStructureRenderer final {
public:
  inline static constexpr int texture_size = 160;
  inline static constexpr std::size_t maximum_cached_images = 16;
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
  image(std::string_view project_id, int phase);
  [[nodiscard]] static int phase_for_progress(double progress) noexcept;
  [[nodiscard]] std::size_t cached_images() const noexcept {
    return cache_.size();
  }

private:
  std::map<std::string, std::shared_ptr<const native_map::RgbaImage>> cache_;
};

} // namespace stellar::native_orbital
