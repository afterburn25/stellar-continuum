#pragma once

#include <stellar/core/adaptive_research_outcome_snapshot.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

[[nodiscard]] std::string encode_adaptive_research_snapshot_v5_dto(
    const AdaptiveResearchStateSnapshotV5 &snapshot);
[[nodiscard]] AdaptiveResearchStateSnapshotV5
decode_adaptive_research_snapshot_v5_dto(std::string_view json);

} // namespace stellar::core::detail
