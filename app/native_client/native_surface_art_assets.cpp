#include "native_surface_art_assets.hpp"

#include <stdexcept>
#include <utility>

namespace stellar::native_surface_ui {

NativeSurfaceArtAssets::NativeSurfaceArtAssets(std::filesystem::path asset_root) {
  if (asset_root.empty())
    throw std::invalid_argument("A native surface art asset root is required.");
  source_path_ = std::move(asset_root) / "assets" / "visual" / "surface" /
                 "temperate-ground-albedo-v1.png";
}

void NativeSurfaceArtAssets::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native surface art assets must be used on their owner thread.");
}

void NativeSurfaceArtAssets::use_background_preparation(
    std::shared_ptr<stellar::native_map::ImagePreparationQueue> queue) {
  require_owner();
  pending_.reset();
  // Rebinding the app-owned queue is the explicit recovery boundary.
  failure_ = {};
  preparation_ = std::move(queue);
}

void NativeSurfaceArtAssets::collect_ready() {
  if (!pending_ || !pending_->ready()) return;
  // Release the completed ticket before taking its result. If the worker
  // failed, that error reaches the app boundary once rather than leaving a
  // ready failed ticket to be retried on later polls.
  auto ticket = std::move(*pending_);
  pending_.reset();
  try {
    auto decoded = ticket.take();
    if (!decoded || decoded->byte_size() > maximum_cached_bytes)
      throw std::length_error("Native surface terrain art exceeds the 16 MiB cache limit.");
    image_ = std::move(decoded);
    ++decode_count_;
  } catch (...) {
    failure_ = std::current_exception();
    throw;
  }
}

std::shared_ptr<const stellar::native_map::RgbaImage>
NativeSurfaceArtAssets::request_image() {
  require_owner();
  if (failure_) std::rethrow_exception(failure_);
  collect_ready();
  if (image_ || pending_ || !preparation_) return image_;
  const auto source = source_path_;
  auto ticket = preparation_->submit(maximum_cached_bytes, [source] {
    auto decoded = stellar::native_map::decode_rgba_image(source);
    if (!decoded || decoded->byte_size() > maximum_cached_bytes)
      throw std::length_error("Native surface terrain art exceeds the 16 MiB cache limit.");
    return decoded;
  });
  if (ticket) pending_ = std::move(*ticket);
  return {};
}

std::shared_ptr<const stellar::native_map::RgbaImage>
NativeSurfaceArtAssets::image() const {
  require_owner();
  return image_;
}

std::size_t NativeSurfaceArtAssets::cache_bytes() const noexcept {
  return image_ ? image_->byte_size() : 0;
}

std::size_t NativeSurfaceArtAssets::decode_count() const noexcept {
  return decode_count_;
}

} // namespace stellar::native_surface_ui
