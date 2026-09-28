#pragma once
#include <stellar/engine/native_map_platform.hpp>
namespace stellar::native_map {
// Straight-alpha storage, intended for the renderer's premultiplied compositor.
// Black RGB is interpreted as zero radiance. No cutoff discards dim filaments.
std::shared_ptr<const RgbaImage> prepare_emissive_image(const RgbaImage&,int size,int quarter_turns=0);
// Measured luminous disc, independent of transparent canvas/outer glow. Values
// are normalized to the image width; intended for roughly circular photospheres.
struct EmissiveDisc {Point center{.5f,.5f};float radius{};};
std::optional<EmissiveDisc> measure_emissive_disc(const RgbaImage&);
}
