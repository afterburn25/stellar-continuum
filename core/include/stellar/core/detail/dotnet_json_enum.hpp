#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace stellar::core::detail {
namespace enum_detail {
inline bool whitespace(std::string_view unit) noexcept {
  if (unit.size() == 1) {
    const auto c = static_cast<unsigned char>(unit.front());
    return c == 0x20 || (c >= 0x09 && c <= 0x0d);
  }
  return unit == "\xc2\x85" || unit == "\xc2\xa0" || unit == "\xe1\x9a\x80" ||
         (unit.size() == 3 && unit[0] == '\xe2' &&
          ((unit[1] == '\x80' && static_cast<unsigned char>(unit[2]) >= 0x80 &&
            static_cast<unsigned char>(unit[2]) <= 0x8a) ||
           unit == "\xe2\x80\xa8" || unit == "\xe2\x80\xa9" ||
           unit == "\xe2\x80\xaf" || unit == "\xe2\x81\x9f")) ||
         unit == "\xe3\x80\x80";
}
inline std::string trim(std::string text) {
  auto width = [](unsigned char c) {
    return c < 0x80             ? 1U
           : (c & 0xe0) == 0xc0 ? 2U
           : (c & 0xf0) == 0xe0 ? 3U
           : (c & 0xf8) == 0xf0 ? 4U
                                : 1U;
  };
  while (!text.empty()) {
    const auto n = width(static_cast<unsigned char>(text.front()));
    if (n > text.size() || !whitespace(std::string_view(text).substr(0, n)))
      break;
    text.erase(0, n);
  }
  while (!text.empty()) {
    std::size_t p = text.size() - 1;
    while (p > 0 && (static_cast<unsigned char>(text[p]) & 0xc0) == 0x80)
      --p;
    if (!whitespace(std::string_view(text).substr(p)))
      break;
    text.erase(p);
  }
  return text;
}
inline bool same(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    auto lower = [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c;
    };
    if (lower(static_cast<unsigned char>(a[i])) !=
        lower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}
inline std::string trim_ascii(std::string text) {
  const auto space = [](unsigned char c) {
    return c == 0x20 || (c >= 0x09 && c <= 0x0d);
  };
  while (!text.empty() && space(static_cast<unsigned char>(text.front())))
    text.erase(text.begin());
  while (!text.empty() && space(static_cast<unsigned char>(text.back())))
    text.pop_back();
  return text;
}
} // namespace enum_detail

template <class Enum>
std::optional<Enum> try_parse_dotnet_json_enum_string(
    std::string_view raw,
    std::span<const std::pair<std::string_view, Enum>> names) {
  auto text = enum_detail::trim(std::string(raw));
  int numeric{};
  const auto number = text.starts_with('+') ? std::string_view(text).substr(1)
                                            : std::string_view(text);
  const auto parsed =
      std::from_chars(number.data(), number.data() + number.size(), numeric);
  if (!number.empty() && parsed.ec == std::errc{} &&
      parsed.ptr == number.data() + number.size() &&
      text == enum_detail::trim_ascii(std::string(raw)))
    return static_cast<Enum>(numeric);
  int combined{};
  std::size_t start{};
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto part = enum_detail::trim(text.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start));
    const auto found = std::ranges::find_if(
        names, [&](const auto &e) { return enum_detail::same(part, e.first); });
    if (found == names.end())
      return std::nullopt;
    combined |= static_cast<int>(found->second);
    if (comma == std::string::npos)
      return static_cast<Enum>(combined);
    start = comma + 1;
  }
  return std::nullopt;
}
} // namespace stellar::core::detail
