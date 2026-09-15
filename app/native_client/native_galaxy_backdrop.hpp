#pragma once

#include "map_camera.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace stellar::native_galaxy_ui {

struct GalaxyBackdropCatalog {
  std::uint64_t campaign_generation{};
  std::int64_t campaign_seed{};
  std::vector<stellar::native_map::WorldPoint> system_positions;
  std::optional<stellar::native_map::WorldPoint> galactic_core;
  double galactic_core_exclusion_radius{};
  bool galactic_core_discovered{};
};

struct GalaxyBackdropFrame {
  double left{}, top{}, width{}, height{};
};

struct GalaxyBackdropView {
  std::uint64_t campaign_generation{};
  int viewport_width{}, viewport_height{};
  stellar::native_map::Camera camera;
  double fitted_pixels_per_world{};
  bool galaxy_view{true};
};

struct GalaxyBackdropRenderStats {
  int deep_field_images{};
  int galaxy_layer_images{};
  int regional_nebula_images{};
  int undisclosed_core_fog_images{};
  int regional_points{};
  double overview_blend{};
  double regional_opacity{};
};

[[nodiscard]] double galaxy_overview_blend(double pixels_per_world,
                                            double fitted_pixels_per_world);
[[nodiscard]] GalaxyBackdropFrame galaxy_artwork_world_frame(
    std::span<const stellar::native_map::WorldPoint> systems,
    std::optional<stellar::native_map::WorldPoint> core,
    double core_exclusion_radius);
[[nodiscard]] stellar::native_map::Camera galaxy_artwork_fit_camera(
    const GalaxyBackdropFrame &frame, int viewport_width, int viewport_height,
    double fill_fraction = .88);

class NativeGalaxyBackdropAssets final {
 public:
  explicit NativeGalaxyBackdropAssets(std::filesystem::path asset_root);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  deep_field();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  galaxy_layer();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  regional_nebula();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  undisclosed_core_fog();
  [[nodiscard]] std::size_t decoded_count() const noexcept;

 private:
  std::filesystem::path asset_root_;
  std::shared_ptr<const stellar::native_map::RgbaImage> deep_field_;
  std::shared_ptr<const stellar::native_map::RgbaImage> galaxy_layer_;
  std::shared_ptr<const stellar::native_map::RgbaImage> regional_nebula_;
  std::shared_ptr<const stellar::native_map::RgbaImage> core_fog_;
  std::size_t decoded_{};
};

class NativeGalaxyBackdrop final {
 public:
  explicit NativeGalaxyBackdrop(NativeGalaxyBackdropAssets &assets);
  void bind(GalaxyBackdropCatalog catalog);
  void set_galactic_core_discovered(std::uint64_t campaign_generation,
                                    bool discovered);
  void discard_campaign() noexcept;
  void append(stellar::native_map::DrawList &out,
              const GalaxyBackdropView &view);
  void clear_render_stats() noexcept;
  [[nodiscard]] GalaxyBackdropRenderStats last_render_stats() const noexcept;

  [[nodiscard]] std::optional<GalaxyBackdropFrame> artwork_frame() const;
  [[nodiscard]] stellar::native_map::Camera fit_camera(int viewport_width,
                                                       int viewport_height) const;
  [[nodiscard]] std::size_t regional_point_count() const noexcept;

 private:
  struct RegionalPoint {
    double unit_x{}, unit_y{};
    float radius{}, alpha{};
    stellar::native_map::Color color;
  };
  NativeGalaxyBackdropAssets *assets_{};
  std::optional<GalaxyBackdropCatalog> catalog_;
  std::optional<GalaxyBackdropFrame> artwork_frame_;
  std::vector<RegionalPoint> regional_points_;
  GalaxyBackdropRenderStats last_stats_{};
};

// The Engine draws legacy batches before ordered world commands. Call this once
// at the end of a galaxy scene. Legacy geometry is inserted after the backdrop
// and before already ordered marker images; labels are appended after markers.
void promote_legacy_galaxy_foreground(stellar::native_map::DrawList &out,
                                      std::size_t insertion_index);

}  // namespace stellar::native_galaxy_ui
