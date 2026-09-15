#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace stellar::core::detail {

// Portable SHA-256 used for source-compatible deterministic simulation rolls.
[[nodiscard]] std::array<std::uint8_t, 32>
adaptive_research_sha256(std::span<const std::uint8_t> bytes);

} // namespace stellar::core::detail
