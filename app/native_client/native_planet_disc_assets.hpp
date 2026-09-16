#pragma once

#include "native_system_workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace stellar::native_system_ui {
inline constexpr std::size_t maximum_planet_disc_entries=96;
inline constexpr std::size_t maximum_planet_disc_bytes=16u*1024u*1024u;

// Application asset adapter. It consumes only observer-safe appearance data and
// returns immutable Engine image resources; neither Engine nor Core knows paths.
class NativePlanetDiscAssets final {
public:
  explicit NativePlanetDiscAssets(std::filesystem::path visual_asset_root);
  ~NativePlanetDiscAssets();
  NativePlanetDiscAssets(const NativePlanetDiscAssets&)=delete;
  NativePlanetDiscAssets&operator=(const NativePlanetDiscAssets&)=delete;
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image(const SystemBodyAppearance&);
  void discard_campaign() noexcept;
  [[nodiscard]] std::size_t cache_entries()const noexcept;
  [[nodiscard]] std::size_t cache_bytes()const noexcept;
  [[nodiscard]] std::uint64_t source_decode_count()const noexcept;
  [[nodiscard]] std::uint64_t generated_disc_count()const noexcept;
private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
} // namespace stellar::native_system_ui
