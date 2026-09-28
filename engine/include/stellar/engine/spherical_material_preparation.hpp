#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace stellar::native_map {
// Offline/worker-side preparation of an isolated, visually audited globe.
// No Core classes or file-name heuristics enter this reusable image operation.
struct SphericalSourceOptions {
  // Rotate source coordinates into an authored equatorial frame before mapping.
  // This is camera roll in the source, never the planet's physical axial tilt.
  double source_roll_degrees{};
  // Conservative limit for surfaces with large genuine albedo differences.
  double maximum_light_gradient{.7};
  double maximum_dark_fraction{.08};
  bool preserve_zonal_detail{};
  int width{1024};
  std::uint64_t seed{};
  bool opaque_clouds{}, separate_clouds{}, liquid{}, ice{}, emissive{}, flatten_canopy{}, rings{}, zonal_clouds{};
};
struct SphericalMaterialImages {
  std::shared_ptr<const RgbaImage> albedo, properties, clouds, emission, normal, thumbnail;
  // properties: R roughness, G liquid mask, B ice mask, A relative height.
  std::array<double,3> disc{}; // center X/Y, radius in source pixels
  std::array<double,3> removed_light_gradient{};
  double observed_surface_fraction{}, black_fraction{}, clipped_fraction{}, seam_error{};
  bool usable{};
  std::string rejection_reason;
};
[[nodiscard]] SphericalMaterialImages prepare_spherical_material(
    const RgbaImage&, const SphericalSourceOptions&);
// Stable, lit orthographic preview from the same spherical maps used by Scene3D.
[[nodiscard]] std::shared_ptr<const RgbaImage> spherical_material_thumbnail(
    const SphericalMaterialImages&, int size=192,double longitude=0,
    double cloud_opacity=1,double emission_strength=1);
// Worker-safe reference portrait of an oriented ellipsoid and optional XZ
// annulus. Uses Scene3D's spherical UV convention and straight-alpha output.
// The existing import preview above is retained for reproducible art exports.
struct SphericalThumbnailOptions {
  int size{96};
  Quaternion orientation;
  double polar_radius{1},cloud_opacity{1},emission_strength{1};
  Vec3 light_direction{.42f,.2f,.87f};
  std::shared_ptr<const RgbaImage> rings;
  double ring_inner_radius{1.2},ring_outer_radius{2.4};
  bool shadows{true};
  bool linear_light{};
};
[[nodiscard]] std::shared_ptr<const RgbaImage> spherical_material_thumbnail(
    const SphericalMaterialImages&, const SphericalThumbnailOptions&);
void encode_rgba_png(const RgbaImage&, const std::filesystem::path&);
}
