#pragma once
#include "native_battle_art.hpp"
#include <filesystem>
#include <memory>
#include <span>

namespace stellar::native_battle_art {
// One transparent, nose-right hull resource. Portrait artwork is never used
// on the field. CPU/GPU pixels remain shared across all visible instances.
class NativeBattleSprites final {
public:
  explicit NativeBattleSprites(std::filesystem::path root);
  void append(stellar::native_map::DrawList&, std::span<const BattleArtSprite>);
  [[nodiscard]] const stellar::native_map::RgbaImage* resource() const noexcept {
    return image_.get();
  }
  [[nodiscard]] std::size_t transparent_pixels() const noexcept {return transparent_pixels_;}
private:
  std::filesystem::path root_;
  std::shared_ptr<const stellar::native_map::RgbaImage> image_;
  std::size_t transparent_pixels_{};
};
} // namespace stellar::native_battle_art
