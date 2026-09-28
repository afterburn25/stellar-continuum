#pragma once
#include "native_battle_art.hpp"
#include <stellar/core/stellar_object.hpp>
#include <filesystem>
#include <memory>
#include <span>

namespace stellar::native_battle_art {
// One transparent, nose-right hull resource. Portrait artwork is never used
// on the field. CPU/GPU pixels remain shared across all visible instances.
class NativeBattleSprites final {
public:
  explicit NativeBattleSprites(std::filesystem::path root);
  // `star` is the encounter system's primary when observed — its blackbody
  // color drives the key light so hulls match the environment lighting.
  void append(stellar::native_map::DrawList&, std::span<const BattleArtSprite>,
              const stellar::core::StellarPhysicalProperties* star = nullptr);
  void set_render_quality(stellar::native_map::RenderQuality3D value) noexcept {quality_ = value;}
  [[nodiscard]] const stellar::native_map::RgbaImage* resource() const noexcept {
    return image_.get();
  }
  [[nodiscard]] std::size_t transparent_pixels() const noexcept {return transparent_pixels_;}
private:
  std::filesystem::path root_;
  std::shared_ptr<const stellar::native_map::RgbaImage> image_;
  std::size_t transparent_pixels_{};
  stellar::native_map::RenderQuality3D quality_{stellar::native_map::RenderQuality3D::High};
};
} // namespace stellar::native_battle_art
