#pragma once

#include "map_camera.hpp"
#include "native_territory_projection.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace stellar::native_territory {

// Presentation cache mirroring Main.StrategicTerritory.cs: the projection and
// its rasterized fill/fog images are rebuilt only when the observer-visible
// inputs change (fingerprint), never per frame. Drawing emits world-layer
// commands so the overlay stays under the catalog markers and the UI.
class NativeTerritoryOverlay final {
public:
  // Matches UiCatalogVisualCoordinateScale for the full-galaxy map.
  static constexpr float coordinate_scale = 14.f;

  // Rebuilds when the fingerprint of the observer-visible inputs changes.
  // observer_claims must come from an observer-filtered DiplomaticStateView.
  void update(const core::FreshCampaignState &world,
              int observer_civilization_id,
              std::span<const core::TerritorialClaimSnapshot> observer_claims);
  void clear() noexcept;

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const NativeTerritoryProjection *projection() const noexcept {
    return valid_ ? &projection_ : nullptr;
  }

  // fitted_pixels_per_world is the overview-fit zoom; it drives the same
  // overview→regional detail ramp as the C# PopulationOverviewBlend curve.
  void append(native_map::DrawList &out, const native_map::Camera &camera,
              int width, int height, float fitted_pixels_per_world) const;

private:
  NativeTerritoryProjection projection_;
  std::shared_ptr<const native_map::RgbaImage> fog_image_;
  struct CivFill {
    int civilization_id{};
    std::shared_ptr<const native_map::RgbaImage> image;
  };
  std::vector<CivFill> fill_images_;
  std::uint64_t fingerprint_{};
  int observer_{};
  bool valid_{};
};

} // namespace stellar::native_territory
