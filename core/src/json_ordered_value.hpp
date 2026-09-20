#pragma once

#include <stellar/engine/json_document.hpp>

// Compatibility seam: schemas remain in Core, the ordered parser is reusable
// Engine infrastructure. Opt-in deferred arrays borrow immutable input bytes.
namespace stellar::core::json_detail {
using stellar::engine::json::Value;
using stellar::engine::json::ParseFailure;
using stellar::engine::json::parse_ordered_json;
using stellar::engine::json::array_size;
using stellar::engine::json::visit_array;
}
