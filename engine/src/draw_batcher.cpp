#include <stellar/engine/draw_batcher.hpp>

#include <algorithm>
#include <numeric>

namespace stellar::engine {

void DrawBatcher::begin_frame() {
  items_.clear();
  batches_.clear();
  culled_.clear();
  submission_count_ = 0;
}

void DrawBatcher::submit(const DrawItem &item) {
  items_.push_back(item);
  culled_.push_back(false);
  ++submission_count_;
}

void DrawBatcher::cull(std::uint32_t submission_index) {
  if (submission_index < culled_.size())
    culled_[submission_index] = true;
}

void DrawBatcher::build() {
  // Separate culled items first so sorting never sees them.
  std::vector<DrawItem> visible;
  visible.reserve(items_.size());
  for (std::size_t i = 0; i < items_.size(); ++i)
    if (!culled_[i])
      visible.push_back(items_[i]);
  items_ = std::move(visible);

  // Transparent: back-to-front by depth (larger depth = farther), then
  // (layer, material, mesh) so equal-depth draws still batch. Opaque:
  // (layer, material, mesh, depth) — material grouping dominates.
  std::stable_sort(items_.begin(), items_.end(), [](const DrawItem &a,
                                                    const DrawItem &b) {
    if (a.transparent != b.transparent)
      return !a.transparent; // opaque first
    if (a.transparent) {
      if (a.layer != b.layer)
        return a.layer < b.layer;
      if (a.depth != b.depth)
        return a.depth > b.depth;
      if (a.material_id != b.material_id)
        return a.material_id < b.material_id;
      return a.mesh_id < b.mesh_id;
    }
    if (a.layer != b.layer)
      return a.layer < b.layer;
    if (a.material_id != b.material_id)
      return a.material_id < b.material_id;
    if (a.mesh_id != b.mesh_id)
      return a.mesh_id < b.mesh_id;
    return a.depth < b.depth;
  });

  // Group consecutive items sharing (layer, transparent, material, mesh).
  batches_.clear();
  for (std::size_t i = 0; i < items_.size();) {
    const auto &first = items_[i];
    std::size_t j = i + 1;
    while (j < items_.size() && items_[j].transparent == first.transparent &&
           items_[j].layer == first.layer &&
           items_[j].material_id == first.material_id &&
           items_[j].mesh_id == first.mesh_id)
      ++j;
    batches_.push_back(DrawBatch{first.mesh_id, first.material_id,
                                 first.layer, first.transparent,
                                 static_cast<std::uint32_t>(i),
                                 static_cast<std::uint32_t>(j - i)});
    i = j;
  }
}

} // namespace stellar::engine
