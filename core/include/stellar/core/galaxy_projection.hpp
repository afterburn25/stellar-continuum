#pragma once

#include <stellar/engine/galaxy_map.hpp>

namespace stellar::core {

struct FreshCampaignState;

// Read-only projection of authoritative campaign geography into the
// engine GalaxyMap model (engine/galaxy_map.hpp). Core systems, the
// interstellar lane network, colonies and fleets are re-shaped into
// map systems/lanes/markers — nothing here writes back and no gameplay
// rules are duplicated.
//
// The projection exposes FULL authoritative state (every system, lane,
// colony and fleet). It is intended for debugger/tool surfaces and
// backend consumers that already operate on authoritative data; a
// player-facing map must apply the observer's knowledge/visibility
// rules before handing markers to a UI.
//
// Mapping:
//   system  -> GalaxySystem (id, name, ly position, stellar class label,
//              tags: surveyed/habitable/anomaly/rare/pre-warp)
//   lane    -> GalaxyLane (InterstellarLaneNetwork::build() output,
//              stable id lane_index+1)
//   colony  -> GalaxyMarker{Colony} (anchored to its system, owner = civ)
//   fleet   -> GalaxyMarker{Fleet} (position or anchored system,
//              owner = civ, destination preserved)
[[nodiscard]] engine::GalaxyMap
project_galaxy_map(const FreshCampaignState &state);

} // namespace stellar::core
