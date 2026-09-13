#pragma once

#include <stellar/core/adaptive_research_snapshot.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

// Internal, side-effect-free DTO conversion for composing later snapshot
// envelopes. Decode performs JSON shape/default conversion only; Restore owns
// all catalog and semantic validation.
[[nodiscard]] std::string encode_adaptive_research_snapshot_dto(
    const AdaptiveResearchStateSnapshot &snapshot);
[[nodiscard]] AdaptiveResearchStateSnapshot
decode_adaptive_research_snapshot_dto(std::string_view json);

} // namespace stellar::core::detail
