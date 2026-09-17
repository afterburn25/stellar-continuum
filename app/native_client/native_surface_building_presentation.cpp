#include "native_surface_building_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace stellar::native_colony_ui {
using namespace stellar::native_surface_building;
using namespace stellar::native_colony;

NativeSurfaceBuildingPresentation::NativeSurfaceBuildingPresentation(
    std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue,
    NativeSurfaceBuildingAssets::Preparer prepare)
    : assets_(std::move(queue), std::move(prepare)) {}

void NativeSurfaceBuildingPresentation::clear() {
  bindings_.clear();
  preview_.reset();
  stats_ = {};
  assets_.clear();
}

void NativeSurfaceBuildingPresentation::update(
    const NativeColonyView &view, const SurfaceViewport &viewport,
    stellar::native_map::UiRect terrain,
    const std::optional<NativeSurfacePlacementQuote> &quote) {
  if (view.construction_sites.size() > 128)
    throw std::invalid_argument("Surface building presentation exceeds 128 sites.");
  std::vector<SurfaceBuildingAssetRequest> requests;
  std::vector<std::optional<int>> ids;
  std::optional<std::size_t> preview_index;
  SurfaceBuildingRasterSpec spec;
  // Two stable tiers avoid a new raster request for every wheel increment.
  spec.width = spec.height = viewport.pixels_per_unit > 4. ? 512 : 256;
  const auto visible = [&](float x, float z) {
    const auto point = viewport.world_to_screen(x, z, terrain);
    const auto margin = static_cast<float>(2. * spec.camera.half_extent *
                                            viewport.pixels_per_unit);
    // Conservative whole-image bounds also admit tall offscreen-ground sprites.
    return std::isfinite(point.x) && std::isfinite(point.y) &&
        std::isfinite(margin) && margin > 0 &&
        point.x + margin >= terrain.x && point.y + margin >= terrain.y &&
        point.x - margin <= terrain.x + terrain.width &&
        point.y - margin <= terrain.y + terrain.height;
  };
  const auto add = [&](SurfaceBuildingState state, std::optional<int> id) {
    requests.push_back({std::move(state), spec});
    ids.push_back(id);
  };
  // A detached, complete ghost shows what will be built, never a committed site.
  if (quote && quote->campaign_generation == view.campaign_generation &&
      quote->player_civilization_id == view.player_civilization_id &&
      quote->system_id == view.system_id && quote->body_id == view.body_id &&
      quote->colony_id == view.colony_id && quote->colony_revision == view.revision &&
      std::ranges::any_of(view.available_buildings, [&](const auto &option) {
        return option.type_id == quote->type_id;
      }) && visible(quote->x, quote->z)) {
    preview_index = requests.size();
    add({.type_id = quote->type_id,
         .rotation_degrees = quote->normalized_rotation_degrees,
         .complete = true, .progress_fraction = 1., .powered = true,
         .enabled = true, .staffed = true}, std::nullopt);
  }
  if (view.surface_hub_level > 0 && visible(0, 0))
    add({.complete = true, .progress_fraction = 1., .powered = true,
         .enabled = true, .staffed = true, .hub_level = view.surface_hub_level,
         .capital = view.homeworld, .outpost = view.resource_outpost}, std::nullopt);
  for (const auto &site : view.construction_sites) {
    if (!visible(site.x, site.z)) continue;
    add({.type_id = site.type_id, .rotation_degrees = site.rotation_degrees,
         .complete = site.complete, .progress_fraction = site.progress_fraction,
         .powered = site.powered, .enabled = site.enabled, .staffed = site.staffed,
         .condition = site.condition}, site.building_id);
  }
  bindings_.clear();
  preview_.reset();
  stats_ = {};
  const auto views = assets_.update({view.campaign_generation, view.system_id,
                                    view.body_id, view.colony_id}, requests);
  stats_.requested = views.size();
  for (std::size_t i = 0; i < views.size(); ++i) {
    const auto &result = views[i];
    switch (result.status) {
    case SurfaceBuildingAssetStatus::Ready: {
      const auto normalized = normalize_surface_building_state(requests[i].state);
      if (!normalized || !result.prepared) { ++stats_.failed; break; }
      ReadySurfaceBuildingImage image{*normalized.key, requests[i].spec, result.prepared};
      if (preview_index == i) preview_ = std::move(image);
      else bindings_.push_back({ids[i], std::move(image)});
      ++stats_.ready;
      break;
    }
    case SurfaceBuildingAssetStatus::Pending: ++stats_.pending; break;
    case SurfaceBuildingAssetStatus::Deferred: ++stats_.deferred; break;
    case SurfaceBuildingAssetStatus::Failed: ++stats_.failed; break;
    }
  }
}

SurfaceBuildingReadyProvider NativeSurfaceBuildingPresentation::provider() const {
  // A value snapshot cannot read mutable cache state from the draw path.
  return [bindings = bindings_](std::optional<int> id)
      -> std::optional<ReadySurfaceBuildingImage> {
    const auto found = std::ranges::find(bindings, id, &Binding::building_id);
    if (found == bindings.end()) return std::nullopt;
    return found->image;
  };
}

SurfaceBuildingPresentationStats NativeSurfaceBuildingPresentation::stats() const {
  auto result = stats_;
  result.cache = assets_.stats();
  return result;
}
} // namespace stellar::native_colony_ui
