#pragma once

#include "native_surface_building_raster.hpp"
#include <stellar/engine/native_image_preparation.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace stellar::native_surface_building {

struct SurfaceBuildingAssetScope {
  std::uint64_t campaign_generation{};
  int system_id{}, body_id{}, colony_id{};
  bool operator==(const SurfaceBuildingAssetScope &) const = default;
};
struct SurfaceBuildingAssetRequest {
  SurfaceBuildingState state;
  SurfaceBuildingRasterSpec spec;
};
enum class SurfaceBuildingAssetStatus { Deferred, Pending, Ready, Failed };
struct SurfaceBuildingAssetView {
  SurfaceBuildingAssetStatus status{SurfaceBuildingAssetStatus::Deferred};
  std::shared_ptr<const PreparedSurfaceBuildingRaster> prepared;
  std::string message;
};
struct SurfaceBuildingAssetStats {
  std::size_t entries{}, pending{}, cached_bytes{}, reserved_bytes{};
  std::uint64_t admitted{}, completed{}, failed{}, canceled{}, evicted{};
};

// Owner-thread admission/collection only. Geometry and rasterization both run
// through the existing Engine queue. Pending/failed/deferred views let the host
// retain its current meshes; this class never draws or accesses a campaign.
class NativeSurfaceBuildingAssets final {
public:
  static constexpr std::size_t maximum_requests = 130;
  static constexpr std::size_t maximum_entries = 48;
  static constexpr std::size_t maximum_pending = 4;
  static constexpr std::size_t maximum_admissions_per_update = 2;
  static constexpr std::size_t maximum_cached_bytes = 24u * 1024u * 1024u;
  using Preparer = std::function<SurfaceBuildingRasterResult(
      const SurfaceBuildingStateKey &, const SurfaceBuildingRasterSpec &)>;

  explicit NativeSurfaceBuildingAssets(
      std::shared_ptr<native_map::ImagePreparationQueue>, Preparer = {});
  NativeSurfaceBuildingAssets(const NativeSurfaceBuildingAssets &) = delete;
  NativeSurfaceBuildingAssets &operator=(const NativeSurfaceBuildingAssets &) = delete;

  // Call once per owner frame with the complete visible request set. Results
  // match input order, including duplicates. Missing requests cancel pending
  // work before collection; scope changes discard all previous state.
  [[nodiscard]] std::vector<SurfaceBuildingAssetView>
  update(SurfaceBuildingAssetScope, std::span<const SurfaceBuildingAssetRequest>);
  // Failures remain latched even if a key temporarily leaves the view. Only
  // explicit retry or scope change permits another try. Failed entries are not
  // evicted automatically; capacity pressure defers work until explicit retry.
  void retry_failed();
  void clear();
  [[nodiscard]] SurfaceBuildingAssetStats stats() const;

private:
  struct Key {
    SurfaceBuildingStateKey state;
    SurfaceBuildingRasterSpec spec;
    bool operator==(const Key &) const = default;
  };
  struct ResultCell { SurfaceBuildingRasterResult result; };
  struct Entry {
    Key key;
    std::shared_ptr<const PreparedSurfaceBuildingRaster> prepared;
    std::optional<native_map::ImagePreparationQueue::Ticket> ticket;
    std::shared_ptr<ResultCell> cell;
    std::string failure;
    std::size_t reservation{};
    std::uint64_t used{};
  };
  void require_owner() const;
  void cancel(Entry &);
  void collect(Entry &);
  bool make_room(std::size_t, const std::vector<Key> &);
  std::thread::id owner_{std::this_thread::get_id()};
  std::shared_ptr<native_map::ImagePreparationQueue> queue_;
  Preparer prepare_;
  std::optional<SurfaceBuildingAssetScope> scope_;
  std::vector<Entry> entries_;
  std::size_t cached_bytes_{}, reserved_bytes_{};
  std::uint64_t serial_{};
  SurfaceBuildingAssetStats totals_;
};
} // namespace stellar::native_surface_building
