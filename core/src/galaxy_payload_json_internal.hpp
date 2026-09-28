#pragma once

#include "json_ordered_value.hpp"
#include "json_stream_writer.hpp"

#include <stellar/core/galaxy_payload_persistence.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace stellar::core::detail {

// Validated ordered document for composing player/developer envelopes without
// serializing and reparsing intermediate whole-galaxy strings.
[[nodiscard]] nlohmann::ordered_json encode_galaxy_payload_v16_document(
    const GalaxyPayloadV16Dto &payload);

void stream_galaxy_members(JsonStreamWriter&, const GalaxyPayloadV16Dto&, int format_version);

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
