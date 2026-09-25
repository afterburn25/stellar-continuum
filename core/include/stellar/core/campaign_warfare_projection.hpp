#pragma once

#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/engine/warfare.hpp>

#include <span>

namespace stellar::core {

// Read-only projection of authoritative campaign fleets into an engine
// WarfareModel theater. Core combat, transit and military orders remain
// authoritative — the engine model supplies aggregate strength reports
// (`FleetReport`), deterministic Lanchester engagement previews
// (`resolve`) and cohort inspection for diagnostics and tooling. The
// projection never mutates campaign state and performs no simulation on
// its own.
//
// Fidelity contract (Core fleets are single vessels):
//  - one Core fleet -> one engine fleet holding one ShipCohort per the
//    fleet's combat profile (count 1; unarmed profiles still project —
//    transports have attack 0).
//  - `ShipClass` per `CombatProfileDefinition` id: attack =
//    `sustained_damage_per_day()`, hull = shields+armor+hull pool,
//    defense = 0 (Core has no flat per-ship mitigation analog),
//    interdiction = 0 (Core interdiction is tactical-only —
//    `MassiveVesselState::is_interdictor` — and defines no strategic
//    radius; the engine `interdicted()` query remains available to
//    consumers that define one). Speed binds per class id, so the first
//    fleet projecting a profile fixes its class speed — each fleet's
//    own `strategic_speed` stays authoritative in Core state.
//  - cohort condition = current hull fraction when a `FleetCombatState`
//    exists, else 1; experience = 0.15 per recorded battle fought
//    (tactical_vessel), capped at 1 — a documented display scale, not a
//    combat rule.
//  - order mapping: retreating/disengaged fleets -> Retreat; fleets in
//    transit or with a destination -> Move toward the destination
//    system's position; Attack orders with a resolvable target fleet ->
//    Move toward that fleet; otherwise Hold.
[[nodiscard]] engine::WarfareModel project_warfare_theater(
    std::span<const FleetState> fleets,
    std::span<const StellarSystem> systems);

// The engine FleetOrder a Core fleet projects to, exposed for tests and
// diagnostics that need the order without the whole theater.
[[nodiscard]] engine::FleetOrder project_fleet_order(
    const FleetState &fleet, std::span<const FleetState> fleets,
    std::span<const StellarSystem> systems);

} // namespace stellar::core
