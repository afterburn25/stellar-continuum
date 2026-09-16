#pragma once

#include "native_ui_layout.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <filesystem>
#include <memory>

namespace stellar::native_navigation {
class NativeNavigationArt final {
 public:
  explicit NativeNavigationArt(std::filesystem::path asset_root);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage> image(
      stellar::native_map::UiAction action) const noexcept;
 private:
  std::shared_ptr<const stellar::native_map::RgbaImage> map_, home_, inspect_, zoom_in_, zoom_out_, economy_, research_, shipyard_, construction_, explore_, colonies_, logistics_, relations_, menu_;
};
}
