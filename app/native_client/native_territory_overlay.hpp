#pragma once

#include "map_camera.hpp"
#include "native_territory_projection.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace stellar::native_territory {

struct NativeTerritoryDrawStats final {
  std::size_t fill_images{};
  std::size_t contour_segments{};
  std::size_t claim_segments{};
  std::size_t fog_images{};
};

struct NativeTerritoryRenderStyle final {
  bool draw_labels{true};
};

// Presentation cache mirroring Main.StrategicTerritory.cs: the projection and
// its rasterized fill/fog images are rebuilt only when the observer-visible
// inputs change (fingerprint), never per frame. Drawing emits world-layer
// commands so the overlay stays under the catalog markers and the UI.
class NativeTerritoryOverlay final {
public:
  NativeTerritoryOverlay();
  ~NativeTerritoryOverlay();
  NativeTerritoryOverlay(const NativeTerritoryOverlay &) = delete;
  NativeTerritoryOverlay &operator=(const NativeTerritoryOverlay &) = delete;
  // Matches UiCatalogVisualCoordinateScale for the full-galaxy map.
  static constexpr float coordinate_scale = 14.f;
  // Fog plus one color-baked whole-grid fill atlas stay bounded even when a
  // campaign exposes many civilizations; every projected region remains filled.
  static constexpr std::size_t maximum_cached_image_bytes = 16u * 1024u * 1024u;

  // Rebuilds when the fingerprint of the observer-visible inputs changes.
  // observer_claims must come from an observer-filtered DiplomaticStateView.
  void update(const core::FreshCampaignState &world,
              int observer_civilization_id,
              std::span<const core::TerritorialClaimSnapshot> observer_claims);
  // Captures an immutable observer-filtered DTO and schedules at most one
  // replacement while a single worker prepares the previous request.
  void request_update(const core::FreshCampaignState &world,
                      int observer_civilization_id,
                      std::span<const core::TerritorialClaimSnapshot> observer_claims,
                      std::uint64_t generation);
  // Owner-thread, non-blocking collection of a completed worker result.
  void poll();
  [[nodiscard]] bool pending() const noexcept;
  // Deterministic native test seam. The hook runs on the preparation worker
  // immediately before pure DTO processing; production callers never set it.
  void set_preparation_hook_for_tests(std::function<void()> hook);
  void clear() noexcept;

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const NativeTerritoryProjection *projection() const noexcept {
    return valid_ ? &projection_ : nullptr;
  }
  [[nodiscard]] std::size_t cached_image_bytes() const noexcept {
    return cached_image_bytes_;
  }

  // fitted_pixels_per_world is the overview-fit zoom; it drives the same
  // overview→regional detail ramp as the C# PopulationOverviewBlend curve.
  NativeTerritoryDrawStats append(native_map::DrawList &out,
                                  const native_map::Camera &camera,
                                  int width, int height,
                                  float fitted_pixels_per_world,
                                  NativeTerritoryRenderStyle style = {}) const;

private:
  struct Preparation;
  std::unique_ptr<Preparation> preparation_;
  NativeTerritoryProjection projection_;
  std::shared_ptr<const native_map::RgbaImage> fog_image_;
  std::shared_ptr<const native_map::RgbaImage> fill_image_;
  std::uint64_t fingerprint_{};
  std::size_t cached_image_bytes_{};
  int observer_{};
  bool valid_{};
};

} // namespace stellar::native_territory
