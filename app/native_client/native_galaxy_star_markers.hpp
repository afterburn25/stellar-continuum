#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <memory>
#include <optional>

namespace stellar::native_galaxy_ui {

// This presentation enum receives only observer-filtered knowledge. It is
// intentionally independent of Core's stellar catalog type.
enum class GalaxyStarVisualClass {
  unknown,
  m_red_dwarf,
  k_orange_dwarf,
  g_yellow_dwarf,
  f_yellow_white_dwarf,
  a_white_star,
  hot_blue_star,
  giant,
  white_dwarf,
  neutron_star,
  black_hole,
  protostar,
  pulsar,
};

struct NativeGalaxyStarAppearance {
  GalaxyStarVisualClass primary{GalaxyStarVisualClass::unknown};
  std::optional<GalaxyStarVisualClass> secondary;
  std::optional<GalaxyStarVisualClass> tertiary;
};

// Screen-space light profile grows with magnification, independently of the
// simulation's physical stellar radius and without allocating per-star assets.
[[nodiscard]] float galaxy_star_core_radius(double relative_zoom, int viewport_height,
    GalaxyStarVisualClass visual = GalaxyStarVisualClass::unknown);

struct NativeGalaxyStarMarkerStats {
  std::size_t cached_resources{};
  std::size_t cached_bytes{};
  std::size_t generated_resources{};
};

class NativeGalaxyStarMarkerRenderer final {
public:
  // Shared high-resolution light profiles retain a sharp core at close zoom.
  // The entire palette is bounded at 3.25 MiB, independent of system count.
  static constexpr int texture_size = 256;
  static constexpr std::size_t maximum_cached_resources = 13;
  static constexpr std::size_t maximum_cached_bytes =
      maximum_cached_resources * texture_size * texture_size * 4u;

  NativeGalaxyStarMarkerRenderer();
  ~NativeGalaxyStarMarkerRenderer();
  NativeGalaxyStarMarkerRenderer(NativeGalaxyStarMarkerRenderer &&) noexcept;
  NativeGalaxyStarMarkerRenderer &
  operator=(NativeGalaxyStarMarkerRenderer &&) noexcept;
  NativeGalaxyStarMarkerRenderer(const NativeGalaxyStarMarkerRenderer &) = delete;
  NativeGalaxyStarMarkerRenderer &
  operator=(const NativeGalaxyStarMarkerRenderer &) = delete;

  // alpha dims the whole marker for observer-unexplored systems, whose
  // neutral appearance never discloses their unobserved spectral type.
  void append(stellar::native_map::DrawList &, stellar::native_map::Point center,
              float core_radius, const NativeGalaxyStarAppearance &,
              bool selected,
              std::optional<stellar::native_map::UiRect> clip = std::nullopt,
              float alpha = 1.f);
  [[nodiscard]] NativeGalaxyStarMarkerStats stats() const noexcept;
  void clear() noexcept;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
} // namespace stellar::native_galaxy_ui
