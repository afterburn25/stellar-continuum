#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_image_preparation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace stellar::native_system_ui {

struct NativeStellarDiscAppearance {
  stellar::native_map::Color spectral_color{213, 217, 214, 255};
  bool black_hole{};
  std::uint32_t deterministic_seed{};
};

struct NativeCelestialAppearanceStats {
  std::size_t cached_resources{}, cached_bytes{}, generated_resources{};
};

class NativeCelestialAppearanceRenderer final {
public:
  static constexpr int stellar_texture_size = 1024;
  static constexpr int ring_texture_size = 512;
  static constexpr std::size_t maximum_cached_resources = 18;
  static constexpr std::size_t maximum_cached_bytes = 32u * 1024u * 1024u;

  NativeCelestialAppearanceRenderer();
  ~NativeCelestialAppearanceRenderer();
  NativeCelestialAppearanceRenderer(NativeCelestialAppearanceRenderer &&) noexcept;
  NativeCelestialAppearanceRenderer &
  operator=(NativeCelestialAppearanceRenderer &&) noexcept;
  NativeCelestialAppearanceRenderer(const NativeCelestialAppearanceRenderer &) = delete;
  NativeCelestialAppearanceRenderer &
  operator=(const NativeCelestialAppearanceRenderer &) = delete;

  void append_stellar_disc(
      stellar::native_map::DrawList &, stellar::native_map::Point center,
      float photosphere_radius, const NativeStellarDiscAppearance &,
      double presentation_seconds,
      std::optional<stellar::native_map::UiRect> clip = std::nullopt);
  // Reuse the same intermittent prominence animation with supplied stellar art.
  void append_stellar_activity(
      stellar::native_map::DrawList &, stellar::native_map::Point center,
      float photosphere_radius, const NativeStellarDiscAppearance &,
      double presentation_seconds,
      std::optional<stellar::native_map::UiRect> clip = std::nullopt);
  void append_ring_back(
      stellar::native_map::DrawList &, stellar::native_map::Point center,
      float planet_radius,
      std::optional<stellar::native_map::UiRect> clip = std::nullopt);
  void append_ring_front(
      stellar::native_map::DrawList &, stellar::native_map::Point center,
      float planet_radius,
      std::optional<stellar::native_map::UiRect> clip = std::nullopt);

  [[nodiscard]] NativeCelestialAppearanceStats stats() const noexcept;
  void use_background_preparation(std::shared_ptr<stellar::native_map::ImagePreparationQueue>);
  void begin_frame() noexcept;
  [[nodiscard]] bool preparation_pending() const noexcept;
  void cancel_preparation() noexcept;
  void clear() noexcept;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
} // namespace stellar::native_system_ui
