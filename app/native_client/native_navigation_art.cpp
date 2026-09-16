#include "native_navigation_art.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace stellar::native_navigation {
using stellar::native_map::RgbaImage;
namespace {
constexpr int thumbnail_pixels = 256;
std::shared_ptr<const RgbaImage> thumbnail(const RgbaImage &source) {
  if (source.width() < 1 || source.height() < 1)
    throw std::invalid_argument("Navigation photo has invalid dimensions.");
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(thumbnail_pixels * thumbnail_pixels * 4));
  const auto &input = source.pixels();
  for (int y = 0; y < thumbnail_pixels; ++y) for (int x = 0; x < thumbnail_pixels; ++x) {
    const int left=x*source.width()/thumbnail_pixels,right=std::max(left+1,(x+1)*source.width()/thumbnail_pixels);
    const int top=y*source.height()/thumbnail_pixels,bottom=std::max(top+1,(y+1)*source.height()/thumbnail_pixels);
    std::uint32_t channels[4]{};std::size_t count{};
    for(int row=top;row<bottom;++row)for(int column=left;column<right;++column){const auto at=static_cast<std::size_t>((row*source.width()+column)*4);for(int channel=0;channel<4;++channel)channels[channel]+=input[at+channel];++count;}
    const auto at=static_cast<std::size_t>((y*thumbnail_pixels+x)*4);for(int channel=0;channel<4;++channel)pixels[at+channel]=static_cast<std::uint8_t>(channels[channel]/count);
  }
  return RgbaImage::create(thumbnail_pixels,thumbnail_pixels,std::move(pixels));
}
} // namespace
NativeNavigationArt::NativeNavigationArt(std::filesystem::path root) {
  const auto load = [&](const char *name) {
    auto image = stellar::native_map::decode_rgba_image(root / name);
    if (!image) throw std::runtime_error(std::string("Native navigation photo could not be decoded: ") + name);
    return thumbnail(*image);
  };
  // The cache is built once at startup. image() only returns these immutable pointers.
  // These high-resolution transparent PNGs are exact rasters of the Godot
  // semantic navigation SVGs, listed and hash-pinned by the export manifest.
  map_ = load("assets/visual/native-navigation/nav_galaxy.png");
  home_ = load("assets/visual/native-navigation/nav_home.png");
  inspect_ = load("assets/visual/native-navigation/nav_inspection.png");
  zoom_in_ = load("assets/visual/native-navigation/nav_zoom_in.png");
  zoom_out_ = load("assets/visual/native-navigation/nav_zoom_out.png");
  economy_ = load("assets/visual/native-navigation/nav_economy.png");
  research_ = load("assets/visual/native-navigation/nav_research.png");
  shipyard_ = load("assets/visual/native-navigation/nav_shipyard.png");
  construction_ = load("assets/visual/native-navigation/nav_construction.png");
  explore_ = load("assets/visual/native-navigation/nav_exploration.png");
  colonies_ = load("assets/visual/native-navigation/nav_colonization.png");
  logistics_ = load("assets/visual/native-navigation/nav_logistics.png");
  relations_ = load("assets/visual/native-navigation/nav_relations.png");
  menu_ = load("assets/visual/native-navigation/nav_settings.png");
}
std::shared_ptr<const RgbaImage> NativeNavigationArt::image(const stellar::native_map::UiAction action) const noexcept {
  switch (action) {
    case stellar::native_map::UiAction::Map:return map_;
    case stellar::native_map::UiAction::Home:return home_;
    case stellar::native_map::UiAction::Inspect:return inspect_;
    case stellar::native_map::UiAction::ZoomIn:return zoom_in_;
    case stellar::native_map::UiAction::ZoomOut:return zoom_out_;
    case stellar::native_map::UiAction::Economy:return economy_;
    case stellar::native_map::UiAction::Research: return research_;
    case stellar::native_map::UiAction::Shipyard: return shipyard_;
    case stellar::native_map::UiAction::Construction: return construction_;
    case stellar::native_map::UiAction::Explore:return explore_;
    case stellar::native_map::UiAction::Colonies:return colonies_;
    case stellar::native_map::UiAction::Supply:return logistics_;
    case stellar::native_map::UiAction::Diplomacy: return relations_;
    case stellar::native_map::UiAction::Menu:return menu_;
    default: return {};
  }
}
}
