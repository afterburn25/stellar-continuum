#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {
struct TextFitExtent { float width{}, height{}; };
// Renderer-independent UTF-8 label fitting. Measure must use the caller's real
// font and wrap width. Full text remains in the authoritative view/inspector.
template<class Measure>
[[nodiscard]] std::string fit_text_to_box(std::string_view text, float width,
                                         float height, Measure measure) {
  if (text.empty() || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0 || height <= 0) return {};
  const auto fits = [&](std::string_view value) {
    const auto size = measure(value);
    return std::isfinite(size.width) && std::isfinite(size.height) &&
        size.width >= 0 && size.height >= 0 && size.width <= width && size.height <= height;
  };
  if (fits(text)) return std::string(text);
  constexpr std::string_view ellipsis = "\xe2\x80\xa6";
  if (!fits(ellipsis)) return {};
  std::vector<std::size_t> boundaries{0};
  for (std::size_t i=1;i<text.size();++i)
    if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) boundaries.push_back(i);
  const auto candidate = [&](std::size_t bytes) {
    auto prefix = text.substr(0,bytes);
    while (!prefix.empty() && (prefix.back()==' ' || prefix.back()=='\n' || prefix.back()=='\t' || prefix.back()=='\r'))
      prefix.remove_suffix(1);
    return std::string(prefix)+std::string(ellipsis);
  };
  std::size_t low=0,high=boundaries.size();
  while (low+1<high) {
    const auto mid=low+(high-low)/2;
    if (fits(candidate(boundaries[mid]))) low=mid; else high=mid;
  }
  auto bytes=boundaries[low];
  // Prefer whole words. Long names/identifiers and languages without spaces
  // still fall back to a complete UTF-8 code point.
  const auto word=text.substr(0,bytes).find_last_of(" \t\r\n");
  if (word!=std::string_view::npos && word>0 &&
      text[bytes]!=' ' && text[bytes]!='\n' && text[bytes]!='\t' && text[bytes]!='\r') bytes=word;
  auto result=candidate(bytes);
  if (fits(result)) return result;
  return std::string(ellipsis);
}
} // namespace stellar::engine
