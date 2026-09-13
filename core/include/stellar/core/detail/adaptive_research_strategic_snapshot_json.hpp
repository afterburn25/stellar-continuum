#pragma once

#include <stellar/core/adaptive_research_strategic_snapshot.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

[[nodiscard]] std::string encode_adaptive_research_snapshot_v3_dto(
    const AdaptiveResearchStateSnapshotV3 &snapshot);
[[nodiscard]] AdaptiveResearchStateSnapshotV3
decode_adaptive_research_snapshot_v3_dto(std::string_view json);

} // namespace stellar::core::detail
