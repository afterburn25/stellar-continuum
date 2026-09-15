#include "native_surface_building_assets.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace stellar::native_surface_building {
namespace {
constexpr std::size_t maximum_error_bytes = 384;
std::string bounded_error(std::string value) {
  if (value.empty()) value = "Surface building preparation failed.";
  if (value.size() > maximum_error_bytes) value.resize(maximum_error_bytes);
  return value;
}
bool valid_coordinate(float value) {
  return std::isfinite(value) && std::abs(value) <= 1'000'000.f;
}
bool valid_projection(const SurfaceBuildingProjection &p, const SurfaceBuildingRasterSpec &spec) {
  return valid_coordinate(p.ground_anchor_pixels.x) &&
         valid_coordinate(p.ground_anchor_pixels.y) &&
         valid_coordinate(p.projected_bounds.x) && valid_coordinate(p.projected_bounds.y) &&
         valid_coordinate(p.projected_bounds.width) && valid_coordinate(p.projected_bounds.height) &&
         p.projected_bounds.x >= 0 && p.projected_bounds.y >= 0 &&
         p.projected_bounds.width > 0 && p.projected_bounds.height > 0 &&
         static_cast<double>(p.projected_bounds.x) + p.projected_bounds.width <= spec.width &&
         static_cast<double>(p.projected_bounds.y) + p.projected_bounds.height <= spec.height &&
         p.ground_anchor_pixels.x >= 0 && p.ground_anchor_pixels.x <= spec.width &&
         p.ground_anchor_pixels.y >= 0 && p.ground_anchor_pixels.y <= spec.height &&
         std::isfinite(p.canonical_footprint_radius) &&
         p.canonical_footprint_radius > 0 && p.canonical_footprint_radius <= 4096;
}
} // namespace

NativeSurfaceBuildingAssets::NativeSurfaceBuildingAssets(
    std::shared_ptr<native_map::ImagePreparationQueue> queue, Preparer prepare)
    : queue_(std::move(queue)), prepare_(std::move(prepare)) {
  if (!queue_) throw std::invalid_argument("Surface building assets require an Engine image queue.");
  if (!prepare_) prepare_ = [](const auto &key, const auto &spec) {
    return prepare_surface_building_raster(key, spec);
  };
  entries_.reserve(maximum_entries);
}
void NativeSurfaceBuildingAssets::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Surface building assets must be used on their owner thread.");
}
void NativeSurfaceBuildingAssets::cancel(Entry &entry) {
  if (!entry.ticket) return;
  entry.ticket.reset(); // Running work finishes privately; no waiting.
  reserved_bytes_ -= entry.reservation;
  entry.reservation = 0;
  entry.cell.reset();
  ++totals_.canceled;
}
void NativeSurfaceBuildingAssets::clear() {
  require_owner();
  for (auto &entry : entries_) cancel(entry);
  entries_.clear();
  cached_bytes_ = 0;
  scope_.reset();
}
void NativeSurfaceBuildingAssets::retry_failed() {
  require_owner();
  std::erase_if(entries_, [](const Entry &entry) { return !entry.failure.empty(); });
}
void NativeSurfaceBuildingAssets::collect(Entry &entry) {
  if (!entry.ticket || !entry.ticket->ready()) return;
  auto ticket = std::move(*entry.ticket);
  entry.ticket.reset();
  const auto reservation = std::exchange(entry.reservation, 0);
  reserved_bytes_ -= reservation;
  auto cell = std::move(entry.cell);
  try {
    const auto image = ticket.take(); // Completion synchronization precedes metadata access.
    const auto prepared = cell->result.prepared;
    if (!prepared || !prepared->image || prepared->image != image ||
        !(prepared->state == entry.key.state) ||
        image->width() != entry.key.spec.width || image->height() != entry.key.spec.height ||
        image->byte_size() > reservation || !valid_projection(prepared->projection, entry.key.spec) ||
        prepared->input_triangles > maximum_geometry_triangles ||
        prepared->rasterized_triangles > prepared->input_triangles ||
        prepared->pixel_tests > maximum_raster_pixel_tests)
      throw std::runtime_error("Surface building preparation returned inconsistent image metadata.");
    entry.prepared = prepared;
    cached_bytes_ += image->byte_size();
    ++totals_.completed;
  } catch (const std::exception &error) {
    entry.failure = bounded_error(error.what());
    ++totals_.failed;
  } catch (...) {
    entry.failure = "Surface building preparation failed with an unknown exception.";
    ++totals_.failed;
  }
}
bool NativeSurfaceBuildingAssets::make_room(std::size_t bytes, const std::vector<Key> &desired) {
  while (entries_.size() >= maximum_entries ||
         cached_bytes_ + reserved_bytes_ > maximum_cached_bytes - bytes) {
    auto oldest = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->ticket || !it->failure.empty() ||
          std::ranges::find(desired, it->key) != desired.end()) continue;
      if (oldest == entries_.end() || it->used < oldest->used) oldest = it;
    }
    if (oldest == entries_.end()) return false;
    if (oldest->prepared) cached_bytes_ -= oldest->prepared->image->byte_size();
    entries_.erase(oldest);
    ++totals_.evicted;
  }
  return true;
}
std::vector<SurfaceBuildingAssetView> NativeSurfaceBuildingAssets::update(
    SurfaceBuildingAssetScope scope, std::span<const SurfaceBuildingAssetRequest> requests) {
  require_owner();
  if (requests.size() > maximum_requests)
    throw std::length_error("Surface building visible requests exceed the 130-request limit.");
  std::vector<Key> desired;
  std::vector<std::optional<Key>> keys;
  std::vector<SurfaceBuildingAssetView> views(requests.size());
  desired.reserve(requests.size()); keys.reserve(requests.size());
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto normalized = normalize_surface_building_state(requests[i].state);
    const auto checked = validate_surface_building_raster_spec(requests[i].spec);
    if (!normalized || !checked) {
      keys.emplace_back();
      views[i].status = SurfaceBuildingAssetStatus::Failed;
      views[i].message = bounded_error(!normalized ? normalized.message : checked.message);
      continue;
    }
    Key key{*normalized.key, *checked.spec};
    if (std::ranges::find(desired, key) == desired.end()) desired.push_back(key);
    keys.push_back(std::move(key));
  }
  if (!scope_ || *scope_ != scope) { clear(); scope_ = scope; }
  // Cancel obsolete pending jobs before collecting: an old planet, rotation or
  // construction stage cannot briefly replace the current requested image.
  for (auto &entry : entries_)
    if (entry.ticket && std::ranges::find(desired, entry.key) == desired.end()) cancel(entry);
  std::erase_if(entries_, [](const Entry &entry) {
    return !entry.ticket && !entry.prepared && entry.failure.empty();
  });
  for (auto &entry : entries_) collect(entry);
  std::size_t admitted{};
  auto pending = static_cast<std::size_t>(std::ranges::count_if(entries_, [](const auto &entry) {
    return entry.ticket.has_value();
  }));
  for (const auto &key : desired) {
    auto entry = std::ranges::find(entries_, key, &Entry::key);
    if (entry != entries_.end()) { entry->used = ++serial_; continue; }
    if (pending >= maximum_pending || admitted >= maximum_admissions_per_update) continue;
    const auto bytes = static_cast<std::size_t>(key.spec.width) * key.spec.height * 4;
    if (!make_room(bytes, desired)) continue;
    auto cell = std::make_shared<ResultCell>();
    const auto prepare = prepare_;
    std::optional<native_map::ImagePreparationQueue::Ticket> ticket;
    try {
      ticket = queue_->submit(bytes, [cell, prepare, key] {
        cell->result = prepare(key.state, key.spec);
        if (!cell->result || !cell->result.prepared->image)
          throw std::runtime_error(bounded_error(cell->result.message));
        return cell->result.prepared->image;
      });
    } catch (const std::exception &error) {
      Entry failed;
      failed.key = key; failed.failure = bounded_error(error.what());
      failed.used = ++serial_; entries_.push_back(std::move(failed));
      ++totals_.failed;
      continue;
    }
    if (!ticket) continue; // Shared queue pressure never causes synchronous preparation.
    Entry added;
    added.key = key; added.cell = std::move(cell); added.ticket = std::move(ticket);
    added.reservation = bytes; added.used = ++serial_;
    entries_.push_back(std::move(added));
    reserved_bytes_ += bytes; ++admitted; ++pending; ++totals_.admitted;
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (!keys[i]) continue;
    const auto entry = std::ranges::find(entries_, *keys[i], &Entry::key);
    if (entry == entries_.end()) continue;
    if (entry->prepared) {
      views[i].status = SurfaceBuildingAssetStatus::Ready;
      views[i].prepared = entry->prepared;
    } else if (!entry->failure.empty()) {
      views[i].status = SurfaceBuildingAssetStatus::Failed;
      views[i].message = entry->failure;
    } else views[i].status = SurfaceBuildingAssetStatus::Pending;
  }
  return views;
}
SurfaceBuildingAssetStats NativeSurfaceBuildingAssets::stats() const {
  require_owner();
  auto result = totals_;
  result.entries = entries_.size(); result.cached_bytes = cached_bytes_;
  result.reserved_bytes = reserved_bytes_;
  result.pending = static_cast<std::size_t>(std::ranges::count_if(entries_, [](const auto &entry) {
    return entry.ticket.has_value();
  }));
  return result;
}
} // namespace stellar::native_surface_building
