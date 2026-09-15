# Native local fleet and lane presentation

The native system workspace displays player-owned fleets at their canonical
local transit positions and exit arrows for the current system's actual
interstellar lanes. It consumes detached observer-bound data. Core remains
the owner of routes, travel phases, fuel, timing, mission orders and knowledge.

## Fleet movement and selection

The controller copies active owned fleets hosted in the open system, excluding
interstellar-warp fleets. It preserves source ordering and the 64-marker cap.
Each marker retains design identity, local chart position/target, movement
phase and hold state. The chart transform uses the source presentation factor
of system design radius times 1.65; it does not alter astronomical distances.

Local icons follow positions after actual campaign advancement. Unchanged
paused frames reuse their snapshot. Rendering never advances a separate ship
simulation. Held ships have no powered-thruster cue. Clicks select through the
existing fleet controller; repeated hits can cycle overlapping owned fleets.
Route issuance remains an explicit preview-and-confirm operation on the galaxy
map. This change adds no shortcut for remote surveying or teleportation.

## Connected exits and privacy

Exit IDs originate only from the retained canonical lane network. Self-edges,
missing endpoints and duplicate destinations are omitted. The arrow bearing is
the exact direction between connected system coordinates. Simulation arrival
and departure gates use the existing fleet_gate_towards calculation.

The dotted system boundary encloses the complete orbital layout. Decorative
arrows and labels move outward on their original bearing to avoid orbits and
neighboring labels. Their display size stays fixed during zoom. This cosmetic
placement never moves the simulation gate. Labels use the renderer's actual
font measurement, remain horizontal and sit outside the triangular base.

Known exits open the destination system through the same observer gate as
galaxy input. Unknown exits display ???? and a scout-telemetry notice. Clicking
either exit does not assign a mission or grant reconnaissance. Unknown names
and distances are absent from the detached lane data.

System input consumes its own gestures. Inspector and controls cannot issue
background orders. Pause and speed controls retain the current system view;
opening a different workspace explicitly closes it. Entering a system cancels
the preceding galaxy drag. Generation replacement discards all local selection
and snapshots.

## Evidence and scope

Controller/geometry tests cover observer and generation checks, owned-data
lifetime, fleet filters/caps, actual CampaignFrame position advancement, known
and unknown lanes, exact connected membership, and horizontal label/arrow
clearance at 720p and 4K. Platform text measurement shares the draw cache.
Exact combined/export results are recorded in the integration handoff after
they run; this document does not assert a planned check has passed.

The export scenario starts from an explicit Player17 fixture, issues a real
route through the existing fleet input test, then authors reconnaissance of
the first hop solely to exercise a known exit alongside an unknown exit.
The native travel proof uses actual clicks, advances the existing simulation,
pauses, and saves. Diagnostics must match saved fleet coordinates, mission
revision, destination and simulation time. A subsequent paused reload must
preserve the whole saved world except its timestamp. These setup facts are
test-authored, not evidence of natural research progression.

This is the 2D local travel migration. Detailed ship models, weapons/explosions,
foreign local contacts, rotated labels, orbital structures, richer stellar
rendering and audio remain separate presentation work. No gameplay or graphical
parity and no sustained 60-FPS guarantee is claimed.
