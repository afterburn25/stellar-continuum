#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace stellar::native_startup_ui {

enum class StartupArtworkKind {
  ApplicationStartup,
  MainMenu,
  NewGalaxyGeneration,
  SaveRestoration
};

using StartupArtworkProvider = std::function<std::shared_ptr<
    const stellar::native_map::RgbaImage>(StartupArtworkKind)>;

[[nodiscard]] double startup_boot_progress(
    std::size_t ready_assets, std::size_t total_assets,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds minimum_display);

// Fully covers the viewport while preserving the source aspect ratio. The
// caller clips the returned destination to the viewport.
[[nodiscard]] stellar::native_map::UiRect startup_artwork_destination(
    int image_width, int image_height, int viewport_width, int viewport_height);

class NativeStartupArtworkAssets final {
public:
  explicit NativeStartupArtworkAssets(std::filesystem::path asset_root);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image(StartupArtworkKind);
  [[nodiscard]] std::size_t decoded_count() const noexcept;

private:
  std::filesystem::path asset_root_;
  std::array<std::shared_ptr<const stellar::native_map::RgbaImage>, 4> cache_{};
  std::size_t decoded_{};
};

} // namespace stellar::native_startup_ui
