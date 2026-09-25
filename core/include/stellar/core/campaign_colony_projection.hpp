#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/engine/colony.hpp>

namespace stellar::core {

// Read-only projection of an authoritative Core colony into an engine
// Colony settlement. Core surface economy (surface_economy.hpp) stays
// authoritative — the engine model supplies aggregate structure,
// utility and workforce accounting (`utility_balance`, `jobs_total`,
// `housing_capacity`) plus per-structure condition/operating inspection
// for diagnostics and tooling. The projection never mutates campaign
// state and performs no simulation on its own.
//
// Fidelity contract:
//  - one SurfaceBuilding -> one standalone engine Structure (Core has
//    no districts; `district_id` is always 0). Building ids map
//    directly, matching the engine's caller-supplied save-identity
//    contract.
//  - `type_id` -> StructureSpec "surface.<type_id>" synthesized from the
//    authoritative surface_building_catalog() row: power supply/demand
//    -> "power" utility supply/demand, workforce_required_millions ->
//    jobs, housing_capacity_millions -> housing, science/industry/
//    credits_per_day -> outputs_per_day, upkeep_credits_per_day ->
//    "credits" upkeep, industry_cost -> "industry" build cost. Food and
//    water capacities have no engine analog and are not projected.
//    Unknown type ids project under a bare "surface.unknown" spec so
//    the structure still occupies its slot.
//  - `is_complete` -> `complete`; `is_enabled` -> `enabled`;
//    `condition` -> `condition`. In-progress buildings carry
//    `construction_remaining = industry_cost - industry_progress` in
//    Core industry units — the engine counts remaining *days*, the
//    projection never advances it, and the value is informational.
//  - `operating` = 1 when the authoritative surface_colony_output()
//    allocation lists the building as powered (the powered set is a
//    subset of staffed), else 0 — Core's actual satisfaction result,
//    not an engine re-evaluation. When any building's type id is
//    unknown the authoritative allocator cannot run (it throws), so
//    all structures project with `operating = 0`.
//  - standalone_slots = surface_building_capacity(colony); when the
//    colony's capacity is 0 (no hub) but buildings exist the bound is
//    raised to the structure count so presence is preserved — engine
//    standalone_slots 0 means unlimited, not none.
[[nodiscard]] engine::Colony project_colony_settlement(const Colony &colony);

} // namespace stellar::core
