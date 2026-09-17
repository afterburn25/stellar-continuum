#include "native_surface_relief.hpp"

#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stellar::native_surface {
namespace {
using native_map::RgbaImage;

std::shared_ptr<const RgbaImage> make_relief() {
  constexpr auto half = static_cast<double>(core::surface_area_half_size);
  constexpr auto span = half * 2.;
  constexpr double light_x = -.55, light_y = .78, light_z = -.48;
  constexpr double sample_step = 1.;
  std::vector<std::uint8_t> pixels(NativeSurfaceRelief::image_bytes);
  for (int row = 0; row < NativeSurfaceRelief::image_resolution; ++row)
    for (int column = 0; column < NativeSurfaceRelief::image_resolution;
         ++column) {
      const auto x = -half +
                     (static_cast<double>(column) + .5) * span /
                         NativeSurfaceRelief::image_resolution;
      const auto z = -half +
                     (static_cast<double>(row) + .5) * span /
                         NativeSurfaceRelief::image_resolution;
      const auto height = [](double px, double pz) {
        return static_cast<double>(core::surface_terrain_height(
            static_cast<float>(px), static_cast<float>(pz)));
      };
      const auto nx = height(x - sample_step, z) - height(x + sample_step, z);
      const auto nz = height(x, z - sample_step) - height(x, z + sample_step);
      const auto length = std::sqrt(nx * nx + 4. + nz * nz);
      const auto shade = (nx * light_x + 2. * light_y + nz * light_z) / length;
      const auto contrast = std::clamp(std::abs(shade - .78) * 190., 0., 54.);
      const auto radius = std::hypot(x, z);
      const auto hub_fade = std::clamp(
          (radius - (core::surface_hub_radius + 8.)) / 32., 0., 1.);
      // Fade at the fixed world patch edge instead of drawing a rectangular seam.
      const auto edge_fade=std::clamp((half-std::max(std::abs(x),std::abs(z))-1.)/31.,0.,1.);
      const auto alpha = static_cast<std::uint8_t>(std::lround(contrast * hub_fade * edge_fade));
      const auto offset =
          (static_cast<std::size_t>(row) * NativeSurfaceRelief::image_resolution +
           column) *
          4u;
      const auto value = static_cast<std::uint8_t>(shade >= .78 ? 255 : 0);
      pixels[offset] = value;
      pixels[offset + 1] = value;
      pixels[offset + 2] = value;
      pixels[offset + 3] = alpha;
    }
  return RgbaImage::create(NativeSurfaceRelief::image_resolution,
                           NativeSurfaceRelief::image_resolution,
                           std::move(pixels));
}
} // namespace

void NativeSurfaceRelief::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native surface relief must be used on its owner thread.");
}

void NativeSurfaceRelief::use_background_preparation(
    std::shared_ptr<native_map::ImagePreparationQueue> value) {
  require_owner();
  clear();
  preparation_ = std::move(value);
}

void NativeSurfaceRelief::clear() noexcept {
  pending_.reset();
  image_.reset();
  identity_.reset();
  requested_ = deferred_ = failed_ = false;
  failure_.clear();
}

void NativeSurfaceRelief::bind(const NativeSurfaceReliefIdentity value) {
  if (identity_ && *identity_ != value) clear();
  identity_ = value;
}

void NativeSurfaceRelief::collect_ready() {
  if (!pending_ || !pending_->ready()) return;
  auto ticket = std::move(*pending_);
  pending_.reset();
  try {
    auto result = ticket.take();
    if (!result || result->byte_size() != image_bytes)
      throw std::length_error("Native surface relief did not match its 4 MiB output budget.");
    image_ = std::move(result);
    ++generated_;
  } catch (const std::exception &error) {
    failed_ = true;
    failure_ = error.what();
  } catch (...) {
    failed_ = true;
    failure_ = "Native surface relief preparation failed with an unknown error.";
  }
}

std::shared_ptr<const RgbaImage> NativeSurfaceRelief::request(
    const NativeSurfaceReliefIdentity value) {
  require_owner();
  bind(value);
  requested_ = true;
  collect_ready();
  if (image_ || pending_ || failed_ || !preparation_) return image_;
  auto ticket = preparation_->submit(image_bytes, [] { return make_relief(); });
  if (ticket) {
    pending_ = std::move(*ticket);
    deferred_ = false;
  }
  else deferred_ = true;
  return {};
}

std::size_t NativeSurfaceRelief::cache_bytes() const noexcept {
  return image_ ? image_->byte_size() : 0;
}

std::size_t NativeSurfaceRelief::pending_count() const noexcept {
  return pending_ ? 1u : 0u;
}

NativeSurfaceReliefStats NativeSurfaceRelief::stats() const noexcept {
  return {requested_, static_cast<bool>(image_), static_cast<bool>(pending_),
          deferred_, failed_, cache_bytes(), pending_ ? image_bytes : 0u,
          generated_};
}

} // namespace stellar::native_surface
