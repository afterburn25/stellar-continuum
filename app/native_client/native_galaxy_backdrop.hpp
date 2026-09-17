#pragma once

#include "map_camera.hpp"

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_image_preparation.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace stellar::native_galaxy_ui {

struct GalaxyBackdropCatalog {
  std::uint64_t campaign_generation{};
  std::int64_t campaign_seed{};
  std::vector<stellar::native_map::WorldPoint> system_positions;
  std::optional<stellar::native_map::WorldPoint> galactic_core;
  double galactic_core_exclusion_radius{};
  bool galactic_core_discovered{};
  bool use_spiral_artwork{true};
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
  // Each retained or prepared source image is bounded to this decoded size.
  static constexpr std::size_t maximum_prepared_image_bytes=8u*1024u*1024u;
  explicit NativeGalaxyBackdropAssets(std::filesystem::path asset_root);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  deep_field();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  galaxy_layer();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  regional_nebula();
  void use_background_preparation(std::shared_ptr<stellar::native_map::ImagePreparationQueue>);
  // A null request result means the image is pending or the bounded queue is
  // full; callers may safely retry on a later owner-thread frame.
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  request_deep_field();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  request_galaxy_layer();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  request_regional_nebula();
  // Cancels pending source work without clearing reusable generic artwork.
  void cancel_preparation() noexcept;
  [[nodiscard]] std::size_t pending_count()const noexcept;
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  undisclosed_core_fog();
  [[nodiscard]] std::size_t decoded_count() const noexcept;

 private:
  enum class ArtworkKind {deep_field,galaxy_layer,regional_nebula};
  struct ArtworkSource {std::filesystem::path path;const char *label;};
  struct Pending {ArtworkKind kind;stellar::native_map::ImagePreparationQueue::Ticket ticket;};
  void require_owner()const;
  void collect_ready();
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  request(ArtworkKind);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  synchronous(ArtworkKind);
  [[nodiscard]] ArtworkSource source(ArtworkKind)const;
  [[nodiscard]] static std::shared_ptr<const stellar::native_map::RgbaImage>
  decode_source(const ArtworkSource&);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>& slot(ArtworkKind);
  std::filesystem::path asset_root_;
  std::thread::id owner_{std::this_thread::get_id()};
  std::shared_ptr<stellar::native_map::ImagePreparationQueue> preparation_;
  std::vector<Pending> pending_;
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
  [[nodiscard]] bool artwork_ready() const noexcept;

 private:
  struct RegionalPoint {
    double unit_x{}, unit_y{};
    float radius{}, alpha{};
    stellar::native_map::Color color;
  };
  NativeGalaxyBackdropAssets *assets_{};
  std::optional<GalaxyBackdropCatalog> catalog_;
  std::optional<GalaxyBackdropFrame> artwork_frame_;
  std::shared_ptr<const stellar::native_map::RgbaImage> density_layer_;
  std::vector<RegionalPoint> regional_points_;
  GalaxyBackdropRenderStats last_stats_{};
  bool artwork_ready_{true};
};

// The Engine draws legacy batches before ordered world commands. Call this once
// at the end of a galaxy scene. Legacy geometry is inserted after the backdrop
// and before already ordered marker images; labels are appended after markers.
void promote_legacy_galaxy_foreground(stellar::native_map::DrawList &out,
                                      std::size_t insertion_index);

}  // namespace stellar::native_galaxy_ui
