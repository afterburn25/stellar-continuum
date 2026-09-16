#pragma once

#include "native_surface_building_assets.hpp"
#include "native_surface_scene.hpp"
#include "native_surface_construction_controller.hpp"

namespace stellar::native_colony_ui {

struct SurfaceBuildingPresentationStats {
  std::size_t requested{}, ready{}, pending{}, deferred{}, failed{};
  stellar::native_surface_building::SurfaceBuildingAssetStats cache;
};

// App-owner bridge from an observer-safe colony DTO to immutable ready images.
// update admits/collects jobs; provider and preview only read completed results.
class NativeSurfaceBuildingPresentation final {
public:
  explicit NativeSurfaceBuildingPresentation(
      std::shared_ptr<stellar::native_map::ImagePreparationQueue>,
      stellar::native_surface_building::NativeSurfaceBuildingAssets::Preparer = {});
  void update(const stellar::native_colony::NativeColonyView &,
              const SurfaceViewport &, stellar::native_map::UiRect terrain,
              const std::optional<stellar::native_colony::NativeSurfacePlacementQuote> &);
  void clear();
  [[nodiscard]] SurfaceBuildingReadyProvider provider() const;
  [[nodiscard]] const std::optional<ReadySurfaceBuildingImage> &preview() const {
    return preview_;
  }
  [[nodiscard]] SurfaceBuildingPresentationStats stats() const;
  [[nodiscard]] bool ready() const noexcept {
    return stats_.requested > 0 && stats_.ready == stats_.requested;
  }

private:
  struct Binding {
    std::optional<int> building_id;
    ReadySurfaceBuildingImage image;
  };
  stellar::native_surface_building::NativeSurfaceBuildingAssets assets_;
  std::vector<Binding> bindings_;
  std::optional<ReadySurfaceBuildingImage> preview_;
  SurfaceBuildingPresentationStats stats_;
};
} // namespace stellar::native_colony_ui
