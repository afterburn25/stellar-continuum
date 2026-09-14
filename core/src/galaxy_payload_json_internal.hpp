#pragma once

#include "json_ordered_value.hpp"

#include <stellar/core/galaxy_payload_persistence.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace stellar::core::detail {

struct GalaxyPayloadOrderedRootTransform {
  std::optional<int> format_version;
  std::span<const std::string_view> excluded_members;
};

// Internal composition seam. Values retain the parser's original byte/line
// locations; the transform only changes wrapper-level routing.
[[nodiscard]] GalaxyPayloadV16Dto decode_galaxy_payload_v16_ordered(
    const json_detail::Value &root,
    GalaxyPayloadOrderedRootTransform transform = {});

} // namespace stellar::core::detail
