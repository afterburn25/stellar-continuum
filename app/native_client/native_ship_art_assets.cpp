#include "native_ship_art_assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stellar::native_ship_ui {
namespace {
using namespace stellar::native_map;

[[nodiscard]] std::string utf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

[[nodiscard]] std::shared_ptr<const RgbaImage>
downsample(const RgbaImage &source, const int target) {
  if (source.width() < 1 || source.height() < 1 || target < 1)
    throw std::invalid_argument("Ship artwork cannot be reduced to the working size.");
  if (source.width() <= target && source.height() <= target)
    return RgbaImage::create(source.width(), source.height(), source.pixels());
  const auto &pixels = source.pixels();
  std::vector<std::uint8_t> reduced(static_cast<std::size_t>(target * target * 4));
  for (int y = 0; y < target; ++y) {
    const int top = y * source.height() / target;
    const int bottom = std::max(top + 1, (y + 1) * source.height() / target);
    for (int x = 0; x < target; ++x) {
      const int left = x * source.width() / target;
      const int right = std::max(left + 1, (x + 1) * source.width() / target);
      std::uint32_t channels[4]{};
      std::size_t count{};
      for (int row = top; row < bottom; ++row)
        for (int column = left; column < right; ++column) {
          const auto offset = static_cast<std::size_t>((row * source.width() + column) * 4);
          for (int channel = 0; channel < 4; ++channel)
            channels[channel] += pixels[offset + channel];
          ++count;
        }
      const auto offset = static_cast<std::size_t>((y * target + x) * 4);
      for (int channel = 0; channel < 4; ++channel)
        reduced[offset + channel] = static_cast<std::uint8_t>(channels[channel] / count);
    }
  }
  return RgbaImage::create(target, target, std::move(reduced));
}

[[nodiscard]] const char *file_name(const ShipArtwork artwork) noexcept {
  switch (artwork) {
  case ShipArtwork::pathfinder_scout: return "pathfinder-scout.jpg";
  case ShipArtwork::science_vessel: return "deep-space-science-vessel.jpg";
  case ShipArtwork::patrol_corvette: return "patrol-corvette.jpg";
  case ShipArtwork::colony_ship: return "interstellar-colony-ship.jpg";
  case ShipArtwork::resource_outpost_ship: return "resource-outpost-ship.png";
  case ShipArtwork::bulk_freighter: return "interstellar-bulk-freighter.png";
  }
  return "";
}
} // namespace

ShipArtwork ship_artwork_for(std::optional<std::string_view> design_id,
                             const stellar::core::FleetRole role) {
  if (design_id) {
    const auto id = *design_id;
    if (id == "warp_scout") return ShipArtwork::pathfinder_scout;
    if (id == "science_vessel") return ShipArtwork::science_vessel;
    if (id == "patrol_corvette") return ShipArtwork::patrol_corvette;
    if (id == "colony_ship") return ShipArtwork::colony_ship;
    if (id == "resource_outpost_ship") return ShipArtwork::resource_outpost_ship;
    if (id == "bulk_freighter") return ShipArtwork::bulk_freighter;
  }
  using stellar::core::FleetRole;
  switch (role) {
  case FleetRole::Scout: return ShipArtwork::pathfinder_scout;
  case FleetRole::Science: return ShipArtwork::science_vessel;
  case FleetRole::Colony: return ShipArtwork::colony_ship;
  case FleetRole::Military: return ShipArtwork::patrol_corvette;
  case FleetRole::Logistics: return ShipArtwork::bulk_freighter;
  }
  throw std::invalid_argument(
      "No approved ship artwork is registered for this fleet role.");
}

struct NativeShipArtAssets::Storage {
  struct Entry {
    ShipArtwork artwork{};
    std::shared_ptr<const RgbaImage> image;
  };
  std::filesystem::path asset_root;
  std::vector<Entry> entries;
  std::size_t bytes{}, decoded{};
};

NativeShipArtAssets::NativeShipArtAssets(std::filesystem::path asset_root)
    : storage_(std::make_unique<Storage>()) {
  if (asset_root.empty())
    throw std::invalid_argument("Ship artwork requires the application asset root.");
  storage_->asset_root = std::move(asset_root);
}
NativeShipArtAssets::~NativeShipArtAssets() = default;

std::shared_ptr<const RgbaImage>
NativeShipArtAssets::image(const ShipArtwork artwork) {
  const auto found = std::ranges::find(storage_->entries, artwork, &Storage::Entry::artwork);
  if (found != storage_->entries.end()) return found->image;
  const auto path = storage_->asset_root / "assets/visual/ships" / file_name(artwork);
  std::shared_ptr<const RgbaImage> decoded;
  try { decoded = decode_rgba_image(path,0,stellar::native_map::ImageDecodeUsage::PixelsOnly); }
  catch (const std::exception &error) {
    throw std::runtime_error("Approved ship artwork failed to decode: " + utf8(path) + ": " + error.what());
  }
  ++storage_->decoded;
  auto thumbnail = downsample(*decoded, ship_art_pixels);
  if (storage_->entries.size() >= maximum_ship_art_entries ||
      storage_->bytes + thumbnail->byte_size() > maximum_ship_art_bytes)
    throw std::logic_error("The fixed ship artwork palette exceeded its budget.");
  storage_->bytes += thumbnail->byte_size();
  storage_->entries.push_back({artwork, thumbnail});
  return thumbnail;
}

std::shared_ptr<const RgbaImage>
NativeShipArtAssets::image_for(std::optional<std::string_view> design_id,
                               const stellar::core::FleetRole role) {
  return image(ship_artwork_for(design_id, role));
}

std::size_t NativeShipArtAssets::cached_count() const noexcept {
  return storage_->entries.size();
}
std::size_t NativeShipArtAssets::decoded_count() const noexcept {
  return storage_->decoded;
}
std::size_t NativeShipArtAssets::cache_bytes() const noexcept {
  return storage_->bytes;
}
} // namespace stellar::native_ship_ui
