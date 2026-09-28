#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <filesystem>
#include <string>
namespace stellar::engine {
class Sha256 {
 public:
  void update(std::span<const std::uint8_t>);
  [[nodiscard]] std::array<std::uint8_t,32> finish() const;
 private:
  void block(const std::uint8_t*);
  std::array<std::uint32_t,8> state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<std::uint8_t,64> tail_{};
  std::uint64_t bytes_{};
  std::size_t used_{};
};
[[nodiscard]] std::string digest_hex(std::span<const std::uint8_t>);
[[nodiscard]] std::string sha256_file(const std::filesystem::path&);
// Shared byte digest. Research's existing fingerprint API delegates here;
// checkpoints, build manifests and diagnostic bundles can use the same code.
[[nodiscard]] std::array<std::uint8_t,32> sha256(std::span<const std::uint8_t>);
}
