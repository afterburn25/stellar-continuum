# Civilian fleet hold and recovery orders

Source authority is `Exploration/CivilianFleetHoldOrders.cs`,
`Exploration/CivilianFleetReturnOrders.cs`, and only
`ColonizationSimulation.AbandonMissionForTransit`. This boundary issues commands and
forecasts return reach. It does not advance local or interstellar travel, consume or
create fuel, refuel on arrival, reveal systems, or refund colony authorization.

## Native surface and dependencies

Expose hold and resume by civilization/fleet identity; request and preview return;
activate a previously queued return on a supplied fleet after physical lane arrival;
and the four-field colony-mission abandonment helper. Preserve both result shapes:
hold has `accepted` and `message`; return has `accepted`,
`requires_confirmation`, and `message`.

The command view borrows ordered systems, ordered colonies, mutable full fleets, and
the campaign-owned `InterstellarLaneNetwork` for those systems. It depends on the
reviewed `FleetState`, `MissionReachAssessment`, `assess_operational_reach`,
`assign_fleet_route`, `clear_fleet_route`, lane graph, and local-transit behavior.
Paid-work confirmation uses the reviewed invariant `0.#` compatibility formatter.
It has no economy, knowledge, research, design-catalog, or species dependency.

Native references and spans make the source's null `GalaxyState` and null fleet
records unrepresentable. An external fixture/import adapter must reject required null
records explicitly; parser failures stay outside command exception catches. Do not
invent a successful provisional reach fallback.

## Controlled civilian selection and ordering

Hold, resume, request, and preview select the first fleet in campaign insertion order
whose ID matches, is active, belongs to the requested civilization, and has role
Scout, Science, or Colony. A same-ID row that is inactive, foreign, military, or
logistics is skipped; the first row satisfying every predicate wins. Preserve this
behavior when duplicate fleet IDs are imported rather than replacing it with a unique
ID map. `ActivateQueuedReturnAtSystem` receives a fleet directly and deliberately does
not repeat ownership, activity, or civilian-role validation.

## Hold and resume

After selection, reject an already-equal hold state without mutation. Hold then sets
`HoldRequested=true`. With a current system, report the first matching system name or
`the current system`; the transit phase does not change that branch. Without a current
system, report the first planned waypoint, otherwise the final destination, otherwise
ID -1, using the first matching system name or `the next system`. The route, local
position, fuel, return order, failure reason, colony work, and mission revision remain
unchanged.

Resume first sets `HoldRequested=false`, then clears
`ReturnToBaseFailureReason`, and returns the source message. It does not cancel a
pending return or replace existing route orders. Holding during warp is interpreted by
the separate exploration advancement loop: the current lane completes before the hold
takes effect at the inbound gate.

## Paid colony confirmation

A Colony-role fleet has a paid commitment when either body ID is present or
`SettlementDaysCompleted > 0`. Zero, negative, and NaN progress alone are not paid;
positive infinity is paid. Request checks an existing return order before this test.
Preview checks paid work before current-system and reach checks. When confirmation is
required, return `accepted=false`, `requires_confirmation=true`, format the exact
progress value with source `0.#`, and mutate nothing.

Confirmation authorizes abandonment if and when a reachable return route is activated.
It does not refund credits. `AbandonMissionForTransit` clears
`DestinationPlanetaryBodyId`, `SettlementBodyId`, and establishment progress, and sets
`PreventAutomaticSettlement=true`. It preserves embarked population/species, cargo,
fuel, all economy state, and every unrelated mission and tactical field.

If a confirmed return is requested while `CurrentSystemId` is absent, queue it without
abandoning colony work yet: clear hold, set the return flag, clear the return failure,
and leave the active lane, destination, route, body IDs, and paid progress intact. This
is required because the ship must physically reach the next system before return reach
can be evaluated using arrival fuel.

## Return preview and candidate selection

Preview is read-only. An in-lane fleet without paid work reports that the lane must
finish and performs no reach assessment. Otherwise enumerate owned colony and outpost
rows in campaign order, project their system IDs, and apply source `Distinct`, which
keeps the first occurrence of each ID. Assess every distinct candidate using the
fleet's civilization and Colony mission kind only for a Colony-role fleet; Scout and
Science use ScoutReconnaissance. Keep supported assessments, then select the smallest
`RouteDistanceLightYears`, breaking ties by system ID. Do not prefer colonies over
outposts, use population, or use insertion order as a final tie-breaker.

Assessment remains a fuel forecast. At an owned serviced origin it replaces forecast
fuel with full or half capacity, and at later serviced systems it forecasts refueling,
but it never changes `FuelRemainingLightYears`. The shortest geometric lane route is
evaluated as-is; do not search for a longer fuel-feasible route. If no candidate is
supported, return the exact no-reachable-settlement result without mutation.

## Request and queued activation

Request validation order is: required galaxy, controlled civilian lookup, existing
return flag, paid-work confirmation, then current-system state. An in-lane accepted
request only queues the flag changes described above. At a system it evaluates return
candidates before changing the fleet. A nonqueued failure to find a base is a complete
no-op.

Queued activation validates the required galaxy, then rejects a missing pending flag
before inspecting the current system. A pending fleet still lacking a current system
is rejected without mutation. The actual exploration arrival hook is outside this
boundary: it completes the physical warp leg, sets current/local-arrival state, applies
real arrival refueling and knowledge effects, and only then calls queued activation.

If queued activation finds no reachable base, clear the return flag, set the exact
stored failure `No owned refuelling settlement is reachable with current fuel.`, set
hold, and preserve the existing destination, route, transit phase, local position,
fuel, and colony work. This leaves the vessel held safely at its inbound local gate.

For a selected base equal to the current system, abandon Colony work first, invoke
`clear_fleet_route`, then clear return/failure again. Clearing during a non-None phase
begins a physical local arrival from the actual local position to system center; it
does not teleport the ship or immediately set phase None.

For a remote selected base, abandon Colony work first, then invoke
`assign_fleet_route`, and finally set `ReturnToBaseRequested=true`. Assignment clears
hold and the old return fields, increments the revision, installs the selected ordered
route, and, when invoked during local arrival, begins a local departure from the
actual inbound position toward the new outbound gate. Thus a queued warp return can
reverse course only after reaching a real system.

## Failure order and native revision boundary

Reach enumeration is lazy until sorting materializes it, but all distinct candidate
assessments needed by the ordering are evaluated before any fleet mutation. Preserve
exceptions from invalid system geometry, duplicate system IDs, invalid route origins,
and distance formatting at that point. Unknown candidate system IDs yield unsupported
assessments; they are not route fallbacks. Duplicate settlement rows for one system do
not cause a second assessment, while all owned rows still participate in reach's
full-versus-half refueling service.

Source mission revision increments are unchecked and wrap Int32 maximum to minimum.
Native `assign_fleet_route` and `clear_fleet_route` reject maximum with
`Fleet mission revision space is exhausted.` before their own mutations. Do not move
that check to the start of recovery activation: for a Colony fleet, source order calls
`AbandonMissionForTransit` first. A native overflow rejection therefore retains the
four abandonment mutations while leaving route/revision/return state as it was at the
point of the call. For a Scout or Science fleet, the same rejection has no preceding
abandonment and is a full no-op. Hold, resume, preview, and an in-lane queued request do
not increment revision and remain valid at Int32 maximum.

## Actual-source case matrix

| Area | Required actual C# cases and assertions |
|---|---|
| Controlled lookup | Missing ID; inactive, foreign, military, and logistics rows; invalid same-ID row before a valid row; two valid duplicate IDs proving first-match mutation and message. |
| Hold no-ops | Already held and already proceeding, with complete fleet before/after equality and exact names/messages. |
| Hold location | Current known system; current unknown fallback; no-current planned head; destination fallback; neither route nor destination; duplicate system IDs proving first matching name. |
| Hold mutation | Local departure, local arrival, and warp-shaped fleet; only hold changes. Confirm no fuel, route, position, colony-work, return, or revision mutation. |
| Resume | Clears an existing return failure, preserves pending return and route; empty failure; Int32-max revision remains unchanged. |
| Paid detection | Each body ID independently; positive/positive-infinite progress; zero, negative, and NaN progress; Colony versus Scout/Science with the same fields. Verify exact `0.#` confirmation text and no mutation. |
| Request ordering | Missing fleet; already-returning paid Colony (already-returning wins); paid confirmation before in-lane queue; confirmed and unconfirmed variants. |
| Queue in lane | Scout and confirmed paid Colony with no current system. Clear hold/failure and set pending return while preserving destination, planned route, transit progress/positions, fuel, and all colony authorization fields. |
| Preview lane | No paid work returns finish-lane guidance without graph evaluation; paid work requests confirmation first. Preview never mutates fleet, fuel, systems, or colonies. |
| Candidate set | No colonies; foreign-only; Colony and outpost candidates; duplicate rows/system IDs; input permutations; equal-distance tie by system ID; nearest geometric candidate that is fuel-infeasible versus a farther supported candidate; first source exception during materialized assessment. |
| Fuel forecast | Zero/exact/short current fuel; serviced origin full and outpost half; intermediate service; foreign service ignored. Every preview/request assessment leaves actual fuel unchanged. |
| Immediate failure | At-system request with no reachable base is a no-op, preserving prior hold/failure/route and paid work even when abandonment was confirmed. |
| Same-system base | Scout and Colony; zero fuel; idle and local-arrival phases. Colony fields clear with no refund, route clears, revision increments, and an active local phase returns physically to center. |
| Remote base | Scout/Science/Colony; local arrival and idle state; selected route/order, exact message, return flag restored after assignment, real local position retained, and paid Colony work cleared with colonists aboard. |
| Queued activation no-ops | No pending flag; pending but still no current system. Test direct inactive/noncivilian input too, proving this entry point performs no control validation. |
| Queued arrival success | Arrival hook shape at an intermediate system, followed by remote reroute; selected same-system base; assert no route activation before `CurrentSystemId` is present and no invented fuel in the recovery call. |
| Queued arrival failure | Pending return at an inbound local gate with no reachable base: pending false, exact stored failure, hold true, old route/destination/transit/local/fuel/colony fields retained. |
| Revision maximum | Hold/resume/preview/in-lane queue succeed without revision use. C# same-base clear and remote assign wrap; native Scout/Science reject without mutation, while native Colony rejects after the exact four abandonment mutations. Compare immutable deep snapshots rather than recomputing expected state. |
| Error boundary | Null required source arguments captured by the C# oracle; malformed fixture data rejected before operation catches; graph/route exceptions and exact categories/messages compared outside catches. |

Every command case records full input fleet, systems, colonies, result or exact error,
and full after-state. Unknown operation kinds and unsupported expected error categories
must fail the consumer. Message construction and JSON comparison occur outside the
expected operation catch. This gate proves only command and recovery semantics; a
separate exploration-advancement gate must prove fuel debit, physical warp completion,
arrival refueling, knowledge events, and subsequent local transit.
