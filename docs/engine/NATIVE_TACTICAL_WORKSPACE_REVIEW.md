# Tactical workspace integration review

Reviewed 2026-09-15: Devin `357872e87c3882211f1940f82160cb6acccb51fa`
against Codex `c398a539` and the retained C# `MassiveCombatView.cs`.
Historical source review at that checkpoint. The port has since been selectively
adapted and tested in PR #332; see `NATIVE_TACTICAL_WORKSPACE.md` for the current
corrections, actual rendering evidence and remaining visual work. Findings below
describe the original incoming code, not the repaired implementation.

The Core order/snapshot adapters are useful: observed combat data and scanner
knowledge remain authoritative. Reuse those contracts while correcting the
presentation and input issues below; do not create another combat simulation.

## Incoming defects corrected in the integration

| Finding at reviewed commit | Player consequence | Required correction and evidence |
| --- | --- | --- |
| `native_battle_workspace.cpp:659` appends an opaque full-surface background to `out.overlay`, while ship markers at `:828,830,862` and several effects at `:709,725,733,751` go to `out.circles`. Engine `native_map_platform.cpp:220-222` draws legacy circles before the ordered overlay. | The battle background covers those ships and effects. Nonempty primitive counts do not prove visible ships. | Put battlefield geometry after its background in the ordered, clipped presentation layer. Verify pixels in the ship area, not only screenshot variation from text/borders. Inspect actual 720p/1080p captures. |
| `issue_context` at workspace `:479-480` and non-targeted orders at `:576` act on `selected.front()` only. C# `MassiveCombatView.cs:311,316-334` issues these orders for every selected owned formation. | Box-selected fleets appear commanded together, but only the first formation responds. | Carry a batch or emit individual Core orders for all eligible selected formations; test at least two formations and mixed ownership. Targeted orders intentionally use the first source in the reference and should retain that behavior. |
| Pointer movement at workspace `:515` can start box selection without an active left press. Release processing lacks press ownership; cancellation clears panning but an orphan right release can still issue a context order. | Ordinary hovering can draw a selection box; focus loss or UI gestures can create unintended orders. | Track active gesture ownership explicitly. Test hover without a press, release without a press, cancellation/focus loss, and a UI press released over the battlefield. |
| Host `main.cpp:2143-2148` always sets tactical speed when cycling it. Reference `MassiveCombatView.cs:273-278` changes only resume speed while paused. | Choosing the next speed unexpectedly resumes a paused battle. | Use the existing Core resume-speed setter while paused. Verify no tick advances before the player resumes at the chosen speed. |
| Workspace `to_screen`/`to_world` at `:384` onward treats a camera coordinate at or below zero as uninitialized. | Panning through the coordinate origin snaps the camera. | Use explicit initialization state and test round trips with zero and negative camera coordinates. |
| `dashed` at workspace `:100` iterates the full projected segment before clipping individual dashes. | Distant endpoints and high zoom can cause excessive drawing work. This is a static risk, not a measured slowdown. | Clip the segment first, reject nonfinite coordinates and bound generated segments. Add large-distance/high-zoom work-budget tests. |
| `native_campaign_session.cpp` admits manual capture for every tactical route, widening the prior save boundary. The new runtime checks establish encounter/formation presence but do not compare exact paused recapture or deterministic continuation. | Mid-battle save behavior has insufficient retained evidence. The widened boundary is not assumed incorrect solely because it changed. | Add session tests for paused/running tactical saves, exact paused reload, matched continuation, encounter completion/reconciliation and load failures before accepting this change. |

## Scope of the next checkpoint

The original integration gate required these corrections and a validated
checkpoint; that functional gate now passes. Preserve the existing campaign input, observer secrecy, Core combat
rules and Player17 recovery. The current tactical grid and schematic circles do
not establish the requested detailed ships battling inside the system scene.
Approved ship/system art and appropriate effects remain presentation work after
functional visibility and control parity are proven.

The proposed tests currently check primitive counts and broad screenshot
variation. Those are useful structural checks, but cannot detect an opaque
overlay hiding the battle. Acceptance must include actual visible battlefield
content, group-command behavior, cancelled input, paused speed changes and
session persistence; avoid claiming visual or performance parity from counts.
