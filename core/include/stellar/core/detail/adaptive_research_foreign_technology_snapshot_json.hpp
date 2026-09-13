#pragma once

#include <string>
#include <string_view>

namespace stellar::core {
struct AdaptiveResearchStateSnapshotV4;
namespace detail {
[[nodiscard]] std::string encode_adaptive_research_snapshot_v4_dto(
    const AdaptiveResearchStateSnapshotV4 &snapshot);
[[nodiscard]] AdaptiveResearchStateSnapshotV4
decode_adaptive_research_snapshot_v4_dto(std::string_view json);
} // namespace detail
} // namespace stellar::core
