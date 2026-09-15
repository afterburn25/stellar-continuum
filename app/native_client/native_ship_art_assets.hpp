#pragma once

#include <stellar/core/fleet_role.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace stellar::native_ship_ui {
inline constexpr std::size_t maximum_ship_art_entries=6;
inline constexpr std::size_t maximum_ship_art_bytes=4u*1024u*1024u;
inline constexpr int ship_art_pixels=224;

enum class ShipArtwork {
  pathfinder_scout,
  science_vessel,
  patrol_corvette,
  colony_ship,
  resource_outpost_ship,
  bulk_freighter,
};

// Approved production ship artwork resolution mirrors the preserved game's
// ShipArtworkLibrary: a registered design resolves its own artwork, anything
// else falls back to the fleet role baseline.
[[nodiscard]] ShipArtwork ship_artwork_for(
    std::optional<std::string_view> design_id, stellar::core::FleetRole role);

// Application asset adapter. It decodes each approved source once, keeps a
// bounded thumbnail cache and returns immutable Engine image resources;
// neither Engine nor Core knows the artwork paths.
class NativeShipArtAssets final {
 public:
  explicit NativeShipArtAssets(std::filesystem::path asset_root);
  ~NativeShipArtAssets();
  NativeShipArtAssets(const NativeShipArtAssets &)=delete;
  NativeShipArtAssets &operator=(const NativeShipArtAssets &)=delete;
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image(ShipArtwork artwork);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image_for(std::optional<std::string_view> design_id,
            stellar::core::FleetRole role);
  [[nodiscard]] std::size_t cached_count() const noexcept;
  [[nodiscard]] std::size_t decoded_count() const noexcept;
  [[nodiscard]] std::size_t cache_bytes() const noexcept;

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
} // namespace stellar::native_ship_ui
