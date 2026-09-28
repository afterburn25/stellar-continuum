# Native tactical workspace

The native C++ client presents active campaign encounters through the existing
`CampaignFrame` and `CampaignMassiveCombat` owner. It consumes
`MassiveCombatSnapshot`; presentation never reads hidden hostile formations to
render their positions, names, strength or ships. Host smoke assertions inspect
the authoritative fixture only to prove that the hidden picket remains hidden.

## Player behavior

- A stationed, armed military fleet exposes ENGAGE HOSTILES in the fleet panel's
  existing action slot. A travel preview retains that slot; clicking a blocked
  preview cannot accidentally start combat. Core checks actual hostility and
  participation, reporting rejection through the normal fleet notice.
- Active encounters open the full-screen tactical view. Left-click selects an
  owned formation; left-drag selects owned formations in a box. Right-click
  issues Engage at a formation or Advance at open space; right-drag pans and
  the wheel zooms around the pointer. FIT keeps formations outside the reports
  and command areas. This follows the reference tactical control scheme.
- Context and non-targeted orders dispatch to every selected owned formation.
  Targeted commands intentionally use the first selected source, as in the
  retained reference. Each Core result remains authoritative; a batch can
  partially succeed and reports its accepted count.
- Play/pause and speed are separate controls. Changing speed while paused
  updates resume speed only. The menu pauses the encounter and restores its
  prior tactical speed on return. F6 uses the existing save request path.
- Reports and formation labels occupy separate clipped rows. Cancelled,
  orphaned and cross-panel gestures cannot issue orders. Hidden strategic
  workspaces do not receive battlefield input.

## Presentation and bounds

Devin `357872e8` supplied the port's starting point. Ships and effects now use
ordered, clipped triangle geometry after the opaque background, fixing the
incoming layering error. The incoming single-source group commands, hover
selection, invalid camera initialization, paused speed behavior and save-proof
gaps are corrected. The historical findings remain in
`NATIVE_TACTICAL_WORKSPACE_REVIEW.md`.

The host refreshes observer snapshots at 10 Hz. Geometry rejects nonfinite or
extreme positions; dashed segments are clipped before subdivision and limited
to 256 dashes. Tokens remain capped at 4096; transient events at 192; formation
labels have a bounded six-position collision search. Campaign replacement
discards selection, targeting, camera state and transient effects.

This is a schematic tactical view with observer-filtered beam/missile/damage
effects. Detailed approved ship artwork, atmospheric system scenery and combat
presentation inside the actual solar-system view remain work to do. The grid
and markers are not a claim of full graphical parity or sustained 60 FPS.

## Manual save boundary

`NativeCampaignSession` admits a manual capture after a successfully returned
tactical frame, including a reconciliation frame. An exception leaves capture
unavailable. Core's existing frame-save flag is unchanged; strategic autosave
cadence remains unchanged. Player17 already contains tactical orders, pending
time, events, salvos, bindings and reconciliation state; no schema field was added.

The maintained session test starts combat through the production command,
rejects a foreign order and accepts an owned order. It proves paused and running
manual saves, exact paused startup reload, deterministic matched continuation,
failed primary/backup load without live replacement, and a durable completion
frame after canonical reconciliation.

## Explicit zero-fleet identity repair

That lifecycle exposed a pre-existing Core/reference defect: strategic fleet ID
0 is valid, but tactical vessel ID 0 is reserved as unset. Beginning or
reconciling a battle could produce an unsaveable vessel, rejected with
`Important vessel identity is invalid.`

Native fleet 0 now maps to reserved tactical vessel ID 4294967296 (2^32), outside
the entire strategic `int` domain. Every nonzero fleet keeps its existing ID.
Begin, campaign encounter validation, important-vessel accounting,
reconciliation and galaxy reference validation share that mapping. The observer
keeps the corresponding positive vessel identity; fleet bindings remain ID 0.
Other overflow and invalid-identity cases retain rejection.

**C# parity exception:** the preserved C# constructor still assigns `fleet.Id`
directly and its validator rejects zero. Existing valid positive-ID saves retain
their identities and parity checks. A native save containing the repaired
zero-fleet tactical identity will not pass the old C# binding validator. The C#
reference was not edited as part of this C++ migration; cross-runtime acceptance
of this newly valid edge case is not claimed. This compatibility limit must
remain in the release notes until explicitly resolved.

## Validation at this checkpoint

MSVC builds with warnings as errors. Twelve affected CTests pass:
`native_battle_workspace`, `native_fleet_workspace`, `native_campaign_session`,
`campaign_frame_parity`, `campaign_massive_combat_parity`,
`massive_combat_engine_parity`, `massive_combat_persistence_parity`,
`galaxy_reference_validation_parity`, `player_campaign_persistence_parity`,
`player_campaign_json_parity`, `player_campaign_recovery_parity`, and
`player_campaign_save`. Existing source cases retain their expected results;
the zero-ID lifecycle is a separately authored native regression.

Five Python evidence tests reject false, duplicate, malformed and mistyped
proofs, leaked enemy detail, missing hidden formations, canonical drift and
captures whose counters claim ships while the token area is blank.

Two actual relocated Vulkan runs at 1280x720 and 1920x1080 use an isolated
20-system fixture and restricted Windows PATH. The ordered run selects a
formation, sends Hold, advances eight tactical seconds (80 ticks), pauses and
saves using real native F6 input routing with no fallback save request. The
reloaded run remains paused. Complete canonical payload equality excludes only
`SavedAtUtc`; own and observed hostile formations remain visible, enemy detail
remains inexact, and the third unobserved formation persists without appearing.
The evidence gate validates actual green pixels around a projected own token,
not only primitive counts or general screenshot variation. Final label-placement changes were followed by the workspace CTest and both
Vulkan runs again (`work/native-battle-final-workspace.log`). Final screenshots
were inspected for row clipping and overlap. Tests replay native input events;
they do not inject physical mouse or keyboard input.

Four neighboring fleet-travel and 500-system navigation runs pass, retaining
travel progress, paused reload, keyboard/modal guards and normal F6 behavior.

Local evidence:

- `work/native-battle-final-build.log`, `work/native-battle-persistence-build.log`
- `work/native-battle-focused.log`, `work/native-battle-python.log`
- `work/native-battle-runtime.json`, `work/native-battle-neighbor-runtime.json`
- `work/native-audio-validation/package-battle-ordered.bmp` and `package-battle-reload.bmp`
- Matching `.log` and `.player17.json` files beside those captures.

Reproduce the graphical gate with `validate_native_battle_export` from
`tools/stellar-export/native_battle_runtime.py`, pointing it at a native package,
an environment mapping and `native-tests/fixtures/player-campaign-json.json`.
The maintained export runner invokes it automatically for native exports;
`test_native_battle_runtime.py -v` tests evidence rejection without a GPU.
Use `stellar.build_environment()` for the Windows CMake/CTest toolchain.

PR #332 remains an unmerged candidate. The local validation folder is unsealed
and is not a newly released download.
