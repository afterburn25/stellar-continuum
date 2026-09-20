#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <string>
namespace stellar::native_map {
enum class TextureFormat { Rgba8, Bc7, Bc5, Bc4 };
struct CookedTexture {
  TextureFormat format{TextureFormat::Rgba8};
  std::vector<Bc1MipLevel> mips;
  double rmse{},max_error{};
  bool lossless_fallback{};
};
[[nodiscard]] std::string texture_format_name(TextureFormat);
[[nodiscard]] TextureFormat texture_format_from_name(std::string_view);
[[nodiscard]] CookedTexture cook_texture(const RgbaImage&,std::string_view category);
[[nodiscard]] std::vector<std::uint8_t> decode_texture_level(TextureFormat,const Bc1MipLevel&);
[[nodiscard]] std::shared_ptr<const RgbaImage> select_texture_mip(const RgbaImage&,int maximum_width);
}
