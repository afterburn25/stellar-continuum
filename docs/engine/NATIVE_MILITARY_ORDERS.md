# Native strategic fleet orders and Locate

The native C++ fleet panel selectively adapts Devin `e8672801` and the preserved
`Main.PlayerCommands.cs` behavior. It dispatches Hold, Defend and Retreat through
the existing Core military-order command. It does not change combat rules,
fleet movement, fleet power, C# sources or the Player17 schema.

## Player behavior

- Selecting an owned armed fleet exposes its current combat order and three
  stance controls. Hovering an action explains its consequence before release,
  even when a previous command notice is present.
- Hold changes combat stance; it does not cancel or stop an existing travel
  order. Defend binds to the fleet's current system. Retreat requests combat
  disengagement; it does not route the fleet home. These commands apply no
  immediate movement or resource charge. Core remains responsible for eligibility
  and subsequent simulation effects.
- Locate centers the chart on the selected owned fleet's authoritative position,
  preserving zoom and fleet selection. It also works for civilian ships, retaining
  their separate Hold/Resume and Return to Base controls.
- Military orders and Locate require a press and release inside the same control.
  Navigation, menu/focus cancellation, route preview, changed selection and a
  changed displayed order invalidate a pending gesture. Travel confirmation and
  paid civilian return review take precedence.
- The order rail, Locate and Engage controls have distinct hit regions. Details
  and ship artwork remain above the order rail at 720p; 1080p and 4K layouts retain
  the same action ownership. Accepted orders provide native feedback and audio.

## Authority and lifetime

The controller requires a unique actual player civilization and unique active
owned fleet ID. It only issues a military quote for an armed selected fleet when
no unreconciled tactical encounter is active. The quote is stored by the
controller and carries a monotonically increasing token; a caller cannot forge a
fresh quote merely by filling in current fields.

Dispatch compares the stored quote and current generation, observer, selected
fleet, mission revision, role, current/destination/defended system, transit phase,
route, current combat order, target, retreat start, combat
eligibility and disengagement state. Changed, replayed, foreign, removed, duplicate,
unarmed and tactical-conflicting orders are rejected. Attack and invalid enums
cannot reach this three-action surface. Accepted same-state orders consume their
token too; unchanged refreshes preserve a valid token. Continuous travel and retreat
progress do not invalidate the token: a normal simulation tick must not swallow a
button click. Changed transit phases, targets and disengagement still invalidate it.

Locate binds generation, observer, fleet and mission revision, rechecks ownership
and returns current position without issuing any Core command. Foreign live
positions remain absent from the panel. Returned command messages use the existing
observer-safe system-name filter.

## Maintained proof

Controller tests cover canonical Defend, stale target/retreat state, moving-fleet click continuity, replay,
generation/observer/selection drift, unarmed/foreign/inactive/duplicate fleets and
tactical conflict. Workspace tests cover responsive containment, distinct action
regions, retained quotes, civilian Locate, cancellation and hover-help precedence.

`native_military_runtime.py` authors an isolated owned corvette from the maintained
20-system Player17 fixture, then runs the relocated Vulkan executable in a Unicode
working directory with a restricted system PATH. Actual application input routing
at 720p and 1080p selects the fleet, issues Hold/Defend/Retreat, reverses the order,
locates the fleet and saves through the normal menu. Whole canonical Player17
comparison permits only the final Defend order and defended-system field to change;
no simulation time, travel, cost or other campaign mutation is allowed. The second
process loads the first save and preserves the complete payload except timestamp.
Strict proof parsing rejects missing, duplicate, false and non-boolean evidence;
captured BMPs must have the requested drawable dimensions and valid pixel payloads.

The existing fleet runtime additionally proves civilian Locate preserves the
whole campaign and zoom before continuing its real travel/save/reload test.
These are application-input replays, not physical mouse injection or a sustained
performance benchmark. Captures retain approved artwork. The validation package
is unsealed and is not a release download.
