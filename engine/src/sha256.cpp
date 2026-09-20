#include <stellar/engine/sha256.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <algorithm>

namespace stellar::engine {
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

void Sha256::block(const std::uint8_t* padded) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto base = index * 4;
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
    auto a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4],
         f = state_[5], g = state_[6], h = state_[7];
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
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}
void Sha256::update(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > ((std::uint64_t{1} << 61)-1)-bytes_)
    throw std::length_error("SHA-256 input too large");
  bytes_ += bytes.size();
  for (const auto byte : bytes) {
    tail_[used_++] = byte;
    if (used_ == 64) { block(tail_.data()); used_=0; }
  }
}
std::array<std::uint8_t,32> Sha256::finish() const {
  auto copy=*this;
  const auto bits=bytes_*8;
  copy.tail_[copy.used_++]=0x80;
  if(copy.used_>56) {
    std::fill(copy.tail_.begin()+copy.used_,copy.tail_.end(),0);
    copy.block(copy.tail_.data()); copy.used_=0;
  }
  std::fill(copy.tail_.begin()+copy.used_,copy.tail_.end(),0);
  for(int i=0;i<8;++i) copy.tail_[63-i]=static_cast<std::uint8_t>(bits>>(i*8));
  copy.block(copy.tail_.data());
  std::array<std::uint8_t,32> result{};
  for(std::size_t i=0;i<8;++i)
    for(int b=0;b<4;++b) result[i*4+b]=static_cast<std::uint8_t>(copy.state_[i]>>(24-b*8));
  return result;
}
std::array<std::uint8_t,32> sha256(std::span<const std::uint8_t> bytes) {
  Sha256 digest; digest.update(bytes); return digest.finish();
}
std::string digest_hex(std::span<const std::uint8_t> bytes) {
  constexpr char hex[]="0123456789abcdef";
  std::string result; result.reserve(bytes.size()*2);
  for(auto b:bytes) {result+=hex[b>>4];result+=hex[b&15];}
  return result;
}
std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  if(!input) throw std::runtime_error("Cannot hash: "+path.string());
  std::array<std::uint8_t,65536> buffer{}; Sha256 digest;
  while(input) {
    input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
    digest.update(std::span(buffer.data(),static_cast<std::size_t>(input.gcount())));
  }
  if(!input.eof()) throw std::runtime_error("Failed reading: "+path.string());
  return digest_hex(digest.finish());
}
} // namespace stellar::engine
