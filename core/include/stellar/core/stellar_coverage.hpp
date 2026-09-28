#pragma once
#include <stellar/core/galaxy_catalog.hpp>
namespace stellar::core {
inline constexpr const char *stellar_coverage_version="stellar-coverage-v1";
struct StellarCoverageResult {std::vector<int> forced_system_ids;};
// Scenario-generation pass: fills missing primary categories at distinct
// procedural locations. Preserves measured anchors, positions, system count,
// all existing rare objects and the normal generator's probability tables.
// Call before planets and civilization founding; never mutate a live world.
[[nodiscard]] StellarCoverageResult ensure_stellar_coverage(
    std::uint64_t seed,std::vector<StellarSystem>& systems);
}
