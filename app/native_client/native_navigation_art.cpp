#include "native_navigation_art.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <stdexcept>

namespace stellar::native_navigation {
using stellar::native_map::RgbaImage;
NativeNavigationArt::NativeNavigationArt(std::filesystem::path root) {
  const auto load = [&](const char *name) {
    auto image = stellar::native_map::decode_rgba_image(root / "assets/visual/ui/navigation" / name);
    if (!image || image->width() != 256 || image->height() != 256 || image->byte_size() != 256u * 256u * 4u)
      throw std::runtime_error(std::string("Native navigation icon violates the bounded image contract: ") + name);
    return image;
  };
  research_ = load("nav_research.png"); shipyard_ = load("nav_shipyard.png");
  construction_ = load("nav_construction.png"); relations_ = load("nav_relations.png");
}
std::shared_ptr<const RgbaImage> NativeNavigationArt::image(const stellar::native_map::UiAction action) const noexcept {
  switch (action) {
    case stellar::native_map::UiAction::Research: return research_;
    case stellar::native_map::UiAction::Shipyard: return shipyard_;
    case stellar::native_map::UiAction::Construction: return construction_;
    case stellar::native_map::UiAction::Diplomacy: return relations_;
    default: return {};
  }
}
}
