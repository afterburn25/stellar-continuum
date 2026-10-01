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
  // Per-civilization state rows are 1:1 with their owner, so their
  // legacy key reuses the civilization id inside each domain's own
  // namespace.
  Economy,
  Technology,
  Construction,
  Shipyard,
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
  // Member order keeps the layout padding-free — the snapshot codec
  // memcpy's the object representation and padding bytes would leak
  // uninitialized memory into byte-compared snapshots.
  double x{}, y{};
  int id{};
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
// Civilization-owned state rows: identity is the owner id.
struct CampaignEconomyTag {
  int civilization_id{};
};
struct CampaignTechnologyTag {
  int civilization_id{};
};
struct CampaignConstructionTag {
  int civilization_id{};
};
struct CampaignShipyardTag {
  int civilization_id{};
};

// Registers the snapshot codecs for the nine tag components under
// canonical names ("campaign.system", ...). Consumers that snapshot a
// projected world must register these first; components are POD and
// serialize as raw bytes (same-build snapshots only — campaign
// authority stays in the Player17 save path, never here).
void register_campaign_world_components(engine::World &world);

// Projects every authoritative domain row into the store in fixed
// domain order (systems, bodies, civilizations, civilization-owned
// economy/technology/construction/shipyard rows, colonies, fleets)
// and container order — the same state projects the same store.
//
// Hierarchy mirrors real containment: bodies parent to their system
// (moons to their parent body when it resolves), civilization-owned
// state rows to their civilization, colonies to their occupied body —
// verified in-system, matching the authoritative exact_body rule —
// else their system, fleets to their current system. Refs that
// cannot resolve (absent or cross-domain ids, or a parent cycle)
// leave the entity unparented; the projection never invents entities
// for dangling ids.
[[nodiscard]] engine::World
project_campaign_world(const FreshCampaignState &state);

// Incremental refresh of a previously projected world: rows are
// matched by namespaced legacy id, so surviving entities keep their
// `EntityId` across syncs — a consumer can hold stable handles
// between simulation days instead of rebuilding (and invalidating)
// the whole store. New rows create entities, rows absent from the
// state destroy their entity (stale handles retire per generational
// id rules), tags refresh in place, and parents re-resolve under the
// same rules as the initial projection.
//
// Only entities bound under `CampaignDomain` namespaces are
// reconciled; entities a consumer added with other (or no) legacy
// bindings are left alone. Returns the reconcile counts so callers
// can measure drift between refreshes.
struct CampaignWorldProjectionSync {
  int created{}, updated{}, destroyed{}, reparented{};
};

CampaignWorldProjectionSync
sync_campaign_world(engine::World &world, const FreshCampaignState &state);

// Fidelity census over a projected store: per-domain tag counts, the
// alive/legacy-bound entity totals, and how many entities carry a
// resolved parent. Counts reflect the projection contract above —
// an entity count below the row count means the state itself has no
// rows, not that the store dropped anything.
struct CampaignWorldProjectionCensus {
  int systems{}, bodies{}, civilizations{}, colonies{}, fleets{};
  int economies{}, technologies{}, construction{}, shipyards{};
  int entities{}, legacy_bound{};
  int parented{}, unparented{};
  // Projected store's container-storage footprint — the projection scales
  // with galaxy size, so its occupancy is worth measuring.
  std::size_t estimated_memory_bytes{};
};

[[nodiscard]] CampaignWorldProjectionCensus
campaign_world_projection_census(const FreshCampaignState &state);

} // namespace stellar::core
