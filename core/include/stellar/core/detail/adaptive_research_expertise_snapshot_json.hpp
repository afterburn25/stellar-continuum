#pragma once

#include <stellar/core/adaptive_research_expertise_snapshot.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

[[nodiscard]] std::string encode_adaptive_research_snapshot_v2_dto(
    const AdaptiveResearchStateSnapshotV2 &snapshot);
[[nodiscard]] AdaptiveResearchStateSnapshotV2
decode_adaptive_research_snapshot_v2_dto(std::string_view json);

} // namespace stellar::core::detail
