#include "native_startup_artwork.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace stellar::native_startup_ui {
namespace {
using namespace stellar::native_map;

constexpr std::array<const char *, 6> paths{
    "assets/visual/loading/stellar-loading-splash.png",
    "assets/visual/loading/stellar-continuum-splash.png",
    "assets/visual/loading/stellar-galaxy-generation.png",
    "assets/visual/loading/stellar-save-loading.png",
    "assets/visual/branding/stellar-continuum-title-v1.png",
    "assets/visual/space/campaign-galaxy-four-arm-v1.png"};

std::string utf8(const std::filesystem::path &path) {
  const auto encoded = path.u8string();
  return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}
} // namespace

UiRect startup_artwork_destination(const int image_width,
                                   const int image_height,
                                   const int viewport_width,
                                   const int viewport_height) {
  if (image_width <= 0 || image_height <= 0 || viewport_width <= 0 ||
      viewport_height <= 0)
    throw std::invalid_argument("Startup artwork dimensions must be positive.");
  const float scale = std::max(static_cast<float>(viewport_width) /
                                   static_cast<float>(image_width),
                               static_cast<float>(viewport_height) /
                                   static_cast<float>(image_height));
  const float width = static_cast<float>(image_width) * scale;
  const float height = static_cast<float>(image_height) * scale;
  return {(static_cast<float>(viewport_width) - width) * .5f,
          (static_cast<float>(viewport_height) - height) * .5f, width, height};
}

double startup_boot_progress(const std::size_t ready_assets,
                             const std::size_t total_assets,
                             const std::chrono::milliseconds elapsed,
                             const std::chrono::milliseconds minimum_display) {
  if (total_assets == 0 || ready_assets > total_assets ||
      elapsed < std::chrono::milliseconds::zero() ||
      minimum_display < std::chrono::milliseconds::zero())
    throw std::invalid_argument("Startup progress inputs are invalid.");
  const double asset_fraction = static_cast<double>(ready_assets) /
                                static_cast<double>(total_assets);
  const double time_fraction = minimum_display == std::chrono::milliseconds::zero()
                                   ? 1.
                                   : std::clamp(
                                         static_cast<double>(elapsed.count()) /
                                             minimum_display.count(),
                                         0., 1.);
  return std::min(asset_fraction, time_fraction);
}

NativeStartupArtworkAssets::NativeStartupArtworkAssets(
    std::filesystem::path asset_root)
    : asset_root_(std::move(asset_root)) {
  if (asset_root_.empty())
    throw std::invalid_argument("Startup artwork asset root is required.");
}

std::shared_ptr<const RgbaImage>
NativeStartupArtworkAssets::image(const StartupArtworkKind kind) {
  const auto index = static_cast<std::size_t>(kind);
  if (index >= cache_.size())
    throw std::invalid_argument("Unknown startup artwork kind.");
  auto &cached = cache_[index];
  if (cached) return cached;
  const auto path = asset_root_ / paths[index];
  try {
    cached = decode_rgba_image(path);
  } catch (const std::exception &error) {
    throw std::runtime_error("Startup artwork failed to decode: " + utf8(path) +
                             ": " + error.what());
  }
  ++decoded_;
  return cached;
}

std::size_t NativeStartupArtworkAssets::decoded_count() const noexcept {
  return decoded_;
}
} // namespace stellar::native_startup_ui
