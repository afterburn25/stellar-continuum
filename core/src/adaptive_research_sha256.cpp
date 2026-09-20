#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/engine/sha256.hpp>
namespace stellar::core::detail {
std::array<std::uint8_t, 32> adaptive_research_sha256(std::span<const std::uint8_t> bytes) {
  return stellar::engine::sha256(bytes);
}
}
