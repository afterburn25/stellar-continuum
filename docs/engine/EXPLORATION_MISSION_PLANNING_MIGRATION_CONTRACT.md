# Exploration mission planning boundary

Authority is `ExplorationMissionPlanner.cs` and
`ExplorationAiMissionCoordinator.cs`. This gate builds read-only plans, order
assessments, and AI selections. It does not assign routes, mutate fleets,
advance travel or surveys, reveal knowledge, or produce discoveries.

The planner resolves the first active fleet with the requested ID. Missing or
inactive fleets and non-scout/science roles return unavailable values rather
than throwing. Candidate input follows system source order, includes only work
needed by the fleet role, sorts supported before blocked targets, then priority,
current fleet distance, and system ID, and finally applies the requested limit
clamped to 1..64. Ordering is stable for complete ties. Source `Double.CompareTo`
sorts NaN distances before finite distances; native ordering must preserve that
without violating the sort comparator's strict weak ordering.

Only the acting civilization's survey knowledge is read. Unknown and detected
targets do not expose operational hazard or estimated survey duration. A
partially surveyed target may expose both through the reviewed survey profiler;
remaining science days are clamped to zero after applying progress. Scouts need
work below partial coverage; science fleets need work below full coverage.
Knowledge can contain a NaN partial progress through the existing source API.
`Math.Max` preserves the NaN remaining duration and invariant `P0` renders the
progress as `NaN`; finite midpoint percentages retain source rounding.

Operational reach is Logistics-owned. The default adapter calls the reviewed
lane reach assessment with the exact role-to-mission mapping. Tests may inject a
read-only reach callback to isolate candidate sorting and rejection messages;
the callback result remains fully visible and is never treated as route state.
Distance is the reviewed current fleet distance, including in-flight position.

Order assessment preserves source validation order: active fleet, eligible
role, target existence, optional need-for-work rejection, candidate/reach, then
local-versus-travel wording. Disabling the need-for-work requirement still
builds and returns a candidate before choosing the already-on-station or course
message.

AI coordination accepts only active scout/science fleets. It requests the hard
64-target window, removes blocked work, then reserves destinations and local
unfinished work held by other active same-civilization scout/science fleets.
Destination reservation takes precedence over local reservation. Reservation
IDs are returned sorted. The first unreserved supported candidate wins; the
first supported candidate is shared only when every supported target in the
bounded window is reserved.

The native coordinator borrows a planner that must outlive it. Construction
from planner rvalues is deleted so the API cannot retain a dangling reference.

Native views contain typed spans and references. Null `GalaxyState`, fleet, or
planner references accepted by C# signatures are not representable and remain
documented source-only boundaries rather than fabricated native pass cases.
