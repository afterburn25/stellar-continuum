#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>

namespace stellar::core::json_detail {

// Validate text at a composition boundary without serializing an entire DOM.
// ASCII is always valid UTF-8; delegate non-ASCII strings to the same strict
// serializer used by the public writer, retaining its diagnostics exactly.
inline void validate_encoded_text(const nlohmann::ordered_json &value) {
  const auto validate = [](const std::string &text) {
    if (std::ranges::any_of(text, [](unsigned char c) { return c >= 0x80; }))
      (void)nlohmann::ordered_json(text).dump(
          -1, ' ', true, nlohmann::json::error_handler_t::strict);
  };
  if (value.is_string()) {
    validate(value.get_ref<const std::string &>());
  } else if (value.is_object()) {
    for (const auto &[key, child] : value.items()) {
      validate(key);
      validate_encoded_text(child);
    }
  } else if (value.is_array()) {
    for (const auto &child : value) validate_encoded_text(child);
  }
}

} // namespace stellar::core::json_detail
