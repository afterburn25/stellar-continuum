#pragma once

#include <stellar/core/galaxy_payload_persistence.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::core {

enum class GalaxyPayloadJsonErrorPhase {
  Parse,
  RequiredMember,
  DateTimeOffset,
  Representability,
  Encode,
};

class GalaxyPayloadJsonError final : public std::runtime_error {
public:
  GalaxyPayloadJsonError(GalaxyPayloadJsonErrorPhase phase,
                         std::string message, std::string path = {},
                         std::optional<std::size_t> line = std::nullopt,
                         std::optional<std::size_t> byte = std::nullopt);

  [[nodiscard]] GalaxyPayloadJsonErrorPhase phase() const noexcept;
  [[nodiscard]] const std::string &path() const noexcept;
  [[nodiscard]] const std::optional<std::size_t> &line() const noexcept;
  [[nodiscard]] const std::optional<std::size_t> &byte() const noexcept;

private:
  GalaxyPayloadJsonErrorPhase phase_;
  std::string path_;
  std::optional<std::size_t> line_;
  std::optional<std::size_t> byte_;
};

// Diagnostic line numbers and byte offsets are zero-based. Byte is an
// absolute UTF-8 input offset; it is deliberately not System.Text.Json's
// BytePositionInLine.

// Encodes the exact Galaxy16 envelope shape using UTF-8. Whitespace is an
// implementation detail; property values, explicit nulls, and source omission
// rules match CampaignSaveService's System.Text.Json contract.
[[nodiscard]] std::string
encode_galaxy_payload_v16_json(const GalaxyPayloadV16Dto &payload);

// Decodes one case-sensitive Galaxy16 envelope into detached owned values.
// Source-accepted null elements that the maintained typed DTO cannot retain
// produce a named Representability error instead of being discarded.
[[nodiscard]] GalaxyPayloadV16Dto
decode_galaxy_payload_v16_json(std::string_view utf8_json);

} // namespace stellar::core
