#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <array>
namespace stellar::native_map {
struct RingSourceOptions {int radial_samples{1024},azimuth_samples{2048};bool fragmented{};};
struct RingMaterialImages {
 std::shared_ptr<const RgbaImage> material,preview;
 // Source ellipse center, principal semiaxes and camera roll (radians).
 std::array<double,5> ellipse{};
 double inner_fraction{},outer_fraction{},mean_opacity{},gap_fraction{};
 bool usable{};std::string rejection_reason;
};
// Rectify an isolated projected ellipse. Preserve authored bands and azimuthal
// detail for every family; robust radial statistics only locate the bounds.
// Remove broad source illumination without averaging fine material features.
// No game taxonomy or generated physics belongs here.
RingMaterialImages prepare_ring_material(const RgbaImage&,const RingSourceOptions& = {});
}
