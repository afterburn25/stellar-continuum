#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stellar::engine {

// Draw-call batching and GPU-oriented submission prep. Submitters describe
// instances (mesh + material + transform index + sort hints); the batcher
// produces contiguous, deterministically-ordered DrawBatch records the
// backend can turn into indirect draw calls. Sorting is stable: depth first
// for transparent work, (material, mesh) for opaque batching.

struct DrawItem {
  std::uint32_t mesh_id{};
  std::uint32_t material_id{};
  // Index into the caller's instance-transform buffer.
  std::uint32_t instance_index{};
  float depth{};
  std::uint32_t layer{};   // render layer / pass bucket
  bool transparent{};
};

struct DrawBatch {
  std::uint32_t mesh_id{};
  std::uint32_t material_id{};
  std::uint32_t layer{};
  bool transparent{};
  // Indices into the (sorted) item list — contiguous instance group.
  std::uint32_t first_item{};
  std::uint32_t count{};
};

class DrawBatcher {
public:
  void begin_frame();
  void submit(const DrawItem &item);

  // Sorts and groups submissions. Opaque items sort by (layer, material,
  // mesh) to maximize batching; transparent items sort back-to-front by
  // depth then (material, mesh). Returns the sorted item list; batches()
  // indexes into it.
  const std::vector<DrawItem> &sorted_items() const { return items_; }
  const std::vector<DrawBatch> &batches() const { return batches_; }
  void build();

  // Frustum culling helper: marks items by index as culled before build().
  void cull(std::uint32_t submission_index);
  std::uint32_t submission_count() const { return submission_count_; }

private:
  std::vector<DrawItem> items_;
  std::vector<DrawBatch> batches_;
  std::vector<bool> culled_;
  std::uint32_t submission_count_{};
};

} // namespace stellar::engine
