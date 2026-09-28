#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace stellar::engine {
// Release precedence is numeric first, then dev < beta < stable at the same
// numeric version. A newer dev build can never be replaced by an older stable.
struct ProductVersion {
  std::array<std::uint32_t, 4> number{};
  enum class Channel { dev, beta, stable } channel{Channel::dev};
  auto operator<=>(const ProductVersion&) const = default;
  static ProductVersion parse(std::string_view);
  std::string string() const;
  std::string channel_name() const;
};
}
