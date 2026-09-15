# Colony commands and establishment migration boundary

The authority is `src/Game/Simulation/Colonization/ColonizationSimulation.cs`. Gate 031 composes accepted settlement knowledge (028), opportunity planning (029), fleet routes, species biology, currency and operating funding. It ports colony/outpost commands and timed establishment, not terrain, rendering, terraforming or a new cost model. Source expedition costs remain 120 for a colony and 90 for an outpost; these are legacy simulation units.

## Views and source ordering

Borrow systems, bodies, civilizations, construction/economy inputs, fleet state, knowledge and the lane graph. Colony storage must be a mutable vector reference because successful establishment appends a colony. Rebuild planning/read spans after each append; never retain a colony reference or span across another settlement. Fleets do not grow in this subsystem. All returned plans, messages and events own their data.

Advance rejects nonfinite or negative days, returns empty for zero, then visits active colony-role fleets in source order. Skip Hold, operating funding at or below 1e-7, and PreventAutomaticSettlement before requiring the first matching civilization. Outpost behavior is determined by its design and the existing eligibility helper. It does not fall through to normal settlement or AI destination selection.

A populated stationary normal colony fleet resolves its explicit body only in its current system; an invalid explicit target does not silently select another world. A body-less legacy mission uses the accepted species-relative resolver. Founding requires full survey, current biological viability, and no settlement by any owner in that system. Keep the current single-settlement model. Preserve species validation and failure order.

The first establishment call for a different body records the body and zero work, then returns without earning work for arrival. Later calls add days times current operating funding, capped at 30 colony days or 20 outpost days, with the source completion epsilon. Do not convert this to instant settlement. Successful founding appends the new colony before consuming the fleet and emitting the event. Use source default fields, sustenance reserves, infrastructure, stability, population identity, outpost deposit reserve, ID and naming rules. Consumption clears the route, population and settlement fields that the source clears; it does not invent a refund or reset unrelated tactical history.

For nonplayer uncommitted colony fleets, AI first creates unique system and body dictionaries, then asks the canonical 64-candidate planner. Rank only accepted candidates using source viability/habitability/rare-resource/personality value minus squared distance, with system and body ID tie-breaks. Legacy 2D squared distance uses float arithmetic; depth-aware distance uses the physical distance result. Reset old settlement work before route assignment, then set destination body. This AI path does not deduct expedition money in the source: preserve that behavior and document it separately from paid player orders.

## Command mutation boundaries

Transit selects the first active owned colony-role fleet, assesses reach, assigns its route, and only then abandons the four settlement target/work/automatic-settlement fields. Route failure therefore must not clear those fields early. The standalone abandon helper changes only those four fields and retains passengers and financial state.

Explicit colony/outpost orders use the owning planner's assessment before finding the operative fleet and economy. Preserve the source's precise fleet predicates and duplicate lookup behavior. A new authorization is identified by PreventAutomaticSettlement or all three destination/settlement fields being absent. Deduct the source expedition cost before route assignment; a later route failure preserves the deduction. Retargeting retains its paid authorization. Reset work only when changing the destination body, set the body, and enable automatic settlement after successful assignment.

Civilization-scoped compatibility orders validate destination existence, detection, full survey and occupancy in that order. Choose the lowest-ID eligible uncommitted populated ordinary colony fleet at a friendly founded settlement. No-fleet rejection precedes passenger-species/body selection. The body-less overload resolves a viable body and then calls the explicit-body compatibility overload, retaining its repeated validation. Planning/assessment forwarding methods retain their canonical results without recomputing a different eligibility policy.

ResolveCompatibilityColonyWorld honors explicit body ID plus system membership; only absent IDs use the lowest-ID solid legacy compatibility candidate. Source-null contracts that cannot be represented by the native value API remain separately reported C# cases. Checked native integer and revision exhaustion boundaries must fail without undefined behavior and be distinguished from source parity.

## Acceptance evidence

Generate expectations by invoking the actual C# class, not a rewritten oracle. Compare full fleet/economy/colony state, ordered events, rejection messages, callback order and state after every operation in multistep sequences. Cover partial funding, arrival without free work, colony/outpost completion, same-step occupancy conflicts, invalid passenger species, explicit versus body-less targets, exact-cost and retargeting charges, low funds, hidden/unsurveyed/occupied systems, route errors after charges, AI ranking and no-charge behavior, duplicate-ID failures, and preservation of unrelated combat/tactical fields. Include real operational reach as well as injected failures. Harness decoding, encoding and assertions stay outside expected-operation catches.

Require strict Release and Debug builds plus the maintained export checks after promotion. A passing colony gate is not proof of the full campaign coordinator, player-save migration, modern Adaptive Research, diplomacy or graphical parity.
