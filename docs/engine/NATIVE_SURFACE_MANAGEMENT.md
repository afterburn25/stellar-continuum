# Native surface building management

The C++ surface inspector exposes Upgrade, Repair, Shut Down/Restart and
Prioritize/Normal Priority for the selected owned building. With no building or
palette item selected, it exposes the colony hub's next upgrade. This selectively
adapts Devin `1c8e9744`; immediate upstream mutations are replaced by the existing
paused review/confirmation pattern.

## Simulation and authorization

`NativeColonyController` projects canonical upgrade costs, research locks,
affordability, repair materials, essential-service priority and hub progress.
Those values participate in the view signature. Pending upgrades cannot be
started twice. Rendering and hit testing consume this detached projection.

`NativeSurfaceConstructionController::preview_management` validates campaign
generation, actual player, known/partially-surveyed system, owned colony, actual
planetary-body membership and selected site. It runs the corresponding Core
command against copied construction-world vectors. A preview changes no live
resources, site state or completion timer. Its actual resource deltas become
the displayed sovereign authorization and industry cost; the consequence text
comes from the accepted outcome, including canonical upgrade duration.

Confirmation requires an owned single-use token. It compares every displayed
quote field, repeats live ownership/knowledge/body binding, compares the target
snapshot and reruns the copied Core assessment. Changed costs, prerequisites,
condition, enabled/priority state, pending upgrades or hub state reject the old
quote. A rejected confirmation consumes the token; the player must review again.
Only the original Core command mutates the authoritative campaign after these
checks. Preview/confirmation/cancellation stay on the simulation owner thread.

Building and hub upgrades use Core's existing timers and resource charges.
Repair currently restores condition immediately after its industry charge;
this is explicitly stated in the confirmation. A timed repair mechanic remains
separate gameplay work. No Core rule, Player17 format or C# source changed.

## Interface

All five management actions pause for an explicit cost/consequence review.
Back/Escape cancels. Successful or rejected confirmation retains the selected
site so its status and outcome remain visible. Existing placement and demolition
reviews remain separate. Pending upgrades show remaining game days; disabled,
unpowered and unstaffed facilities retain the existing truthful Core status.

The inspector pins its header, notice and action buttons. Measured UTF-8 rows
scroll inside their own clipped body; cached measurements are bounded and
invalidated by changed content, viewport width or measurer. Wheel events on the
inspector cannot zoom the surface. Identity replacement clears selection,
gestures, images and quotes; ordinary revisions invalidate open reviews.

## Maintained evidence

- C++ controller tests cover all five actions, canonical resource charges,
  preview/cancel nonmutation, stale generation, tampering, replay, foreign or
  missing targets, deleted/moved bodies, changed building state, changed funds
  and changed prerequisites.
- Projection tests compare Core costs/names/locks and revision invalidation.
  Workspace tests cover routed action values, disabled/incomplete controls,
  confirmation/cancellation, selected-site retention, changed identities,
  measured scrolling and pinned controls at 720p, 1080p and 4K.
- `native_surface_runtime.py` uses the actual native application input route.
  Standard fresh construction/cancellation and paused reload remain checked.
  Authored populated fixtures at 720p/1080p and reduced-workforce 720p exercise
  management preview/cancel, operation change/reversal and priority
  change/reversal. All complete paused Player17 payloads must match except
  `SavedAtUtc`; the original art/focus/status/pixel gates remain active.
- The validator requires strict structured management evidence and review/result
  screenshots. Missing, duplicated, false, wrong-target or malformed evidence
  fails. Paid upgrade/hub/repair execution is controller-test evidence, not yet
  a separately claimed graphical paid-action playthrough.

Final local evidence: `work/native-management-{build,ctest,python}.log`,
`work/native-management-{runtime,colony,navigation}.json` and
`work/native-audio-validation/package-surface-*-management-*.bmp`. These are
unsealed development artifacts. Building artwork is the existing bounded
presentation; this work does not complete 3D cities, manual roads or final art.
