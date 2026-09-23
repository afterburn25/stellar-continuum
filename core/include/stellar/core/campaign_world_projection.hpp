#pragma once
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/world.hpp>

#include <cstdint>

namespace stellar::core {

// Read-only bridge from the authoritative campaign state into the
// engine's typed `World` store (space-strategy roadmap row 1). Core
// domain containers remain the simulation authority; this is a
// query/inspection projection — no state is simulated, mutated or
// persisted by the store, and the DECISION_LOG governs any future
// authority adoption.
//
// Components are plain-data copies: the projection never borrows into
// Core containers, so the projected World may outlive the state span
// safely. Legacy bindings carry the authoritative integer ids inside
// per-domain namespaces — Core reuses small integer ids across
// domains (system 5 vs colony 5), and World's legacy map is flat.

enum class CampaignDomain : std::int64_t {
  System = 1,
  Body,
  Civilization,
  Colony,
  Fleet,
};

// The namespaced identity key used for World::bind_legacy /
// entity_for_legacy lookups. Deterministic for every int32 id
// (negative ids wrap, matching SettlementBodyIndex's key discipline).
[[nodiscard]] inline std::int64_t campaign_legacy_id(CampaignDomain domain,
                                                     int id) {
  return (static_cast<std::int64_t>(domain) << 32) |
         static_cast<std::int64_t>(static_cast<std::uint32_t>(id));
}

// Query tags — one per projected domain, carrying the authoritative
// refs a consumer needs to correlate entities back to Core rows.
struct CampaignSystemTag {
  int id{};
  double x{}, y{};
};
struct CampaignBodyTag {
  int id{}, system_id{};
};
struct CampaignCivilizationTag {
  int id{}, home_system_id{};
};
struct CampaignColonyTag {
  int id{}, civilization_id{}, system_id{};
};
struct CampaignFleetTag {
  int id{}, civilization_id{};
};

// Registers the snapshot codecs for the five tag components under
// canonical names ("campaign.system", ...). Consumers that snapshot a
// projected world must register these first; components are POD and
// serialize as raw bytes (same-build snapshots only — campaign
// authority stays in the Player17 save path, never here).
void register_campaign_world_components(engine::World &world);

// Projects every authoritative domain row into the store in fixed
// domain order (systems, bodies, civilizations, colonies, fleets)
// and container order — the same state projects the same store.
//
// Hierarchy mirrors real containment: bodies parent to their system
// (moons to their parent body when it resolves), colonies to their
// occupied body — verified in-system, matching the authoritative
// exact_body rule — else their system, fleets to their current
// system. Refs that cannot resolve (absent or cross-domain ids, or a
// parent cycle) leave the entity unparented; the projection never
// invents entities for dangling ids.
[[nodiscard]] engine::World
project_campaign_world(const FreshCampaignState &state);

// Fidelity census over a projected store: per-domain tag counts, the
// alive/legacy-bound entity totals, and how many entities carry a
// resolved parent. Counts reflect the projection contract above —
// an entity count below the row count means the state itself has no
// rows, not that the store dropped anything.
struct CampaignWorldProjectionCensus {
  int systems{}, bodies{}, civilizations{}, colonies{}, fleets{};
  int entities{}, legacy_bound{};
  int parented{}, unparented{};
};

[[nodiscard]] CampaignWorldProjectionCensus
campaign_world_projection_census(const FreshCampaignState &state);

} // namespace stellar::core
