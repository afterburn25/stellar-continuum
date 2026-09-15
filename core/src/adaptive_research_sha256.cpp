#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace stellar::core::detail {
namespace {
constexpr std::array<std::uint32_t, 64> constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
}

std::array<std::uint8_t, 32>
adaptive_research_sha256(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > ((std::uint64_t{1} << 61) - 1))
    throw std::length_error("SHA-256 input is too large.");
  std::vector<std::uint8_t> padded(bytes.begin(), bytes.end());
  const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
  padded.push_back(0x80);
  while (padded.size() % 64 != 56)
    padded.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8)
    padded.push_back(static_cast<std::uint8_t>(bit_length >> shift));
  std::array<std::uint32_t, 8> hash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                       0xa54ff53a, 0x510e527f, 0x9b05688c,
                                       0x1f83d9ab, 0x5be0cd19};
  for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto base = offset + index * 4;
      words[index] = (static_cast<std::uint32_t>(padded[base]) << 24) |
                     (static_cast<std::uint32_t>(padded[base + 1]) << 16) |
                     (static_cast<std::uint32_t>(padded[base + 2]) << 8) |
                     padded[base + 3];
    }
    for (std::size_t index = 16; index < 64; ++index) {
      const auto x = words[index - 15], y = words[index - 2];
      const auto s0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
      const auto s1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    auto a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4],
         f = hash[5], g = hash[6], h = hash[7];
    for (std::size_t index = 0; index < 64; ++index) {
      const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto t1 = h + s1 + choose + constants[index] + words[index];
      const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto t2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  std::array<std::uint8_t, 32> result{};
  for (std::size_t index = 0; index < hash.size(); ++index)
    for (int byte = 0; byte < 4; ++byte)
      result[index * 4 + byte] =
          static_cast<std::uint8_t>(hash[index] >> (24 - byte * 8));
  return result;
}
} // namespace stellar::core::detail
