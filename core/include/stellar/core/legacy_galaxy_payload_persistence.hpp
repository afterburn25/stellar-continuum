#pragma once

#include <stellar/core/galaxy_payload_persistence.hpp>

namespace stellar::core {

// Owned decoded historical galaxy envelope. `galaxy` reuses the complete
// nullable DTO surface from the current typed boundary while retaining the
// pre-format-5 SimulationSeconds field separately.
struct LegacyGalaxyPayloadDto {
  GalaxyPayloadV16Dto galaxy;
  double simulation_seconds{};
};

using RestoredLegacyGalaxyPayload = RestoredGalaxyPayloadV16;

// Restores galaxy-only formats 1..8, 10 and 12. Wrapper formats 9/11/13/15/17,
// JSON parsing and Player/Developer mode validation belong to later codecs.
// The owned input is unchanged. Source-ordered migration may mutate only the
// local world under construction before a failure.
[[nodiscard]] RestoredLegacyGalaxyPayload
restore_legacy_galaxy_payload(const LegacyGalaxyPayloadDto &payload);

} // namespace stellar::core