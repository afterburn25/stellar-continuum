# Visual Style / Assets handoff — 2026-09-08

Owner: `work/visual-style-assets` · workstream #217 · PR #218 to `integration`.

## Recovered state and unique work

- Accepted baseline: `c529a1a765776c0940f88002410bc70db740d05a`.
- Recovered branch head: `2a9124d666fcc024cbe549d1aa12dc92329838eb`, 30 commits ahead and zero behind that baseline. No branch history was reset or replaced.
- #217 records the original 16-vector milestone (`297fd40`), project-wide Theme/resource expansion (`acf4a36`), and the 46-icon milestone (`c1c1c43`). PR #218 is open, targeting `integration` and still carries some stale pre-map-consumption wording.
- Unique milestone: 46 SVGs in seven families; Deep-Space Instrumentation guide/manifest/tokens; shared Godot Theme/project binding; runtime palette/icon loader; deterministic procedural menu; startup CanvasLayer 100; shape-based strategic overlays; icon consumption in existing controls; asset validation and screenshot workflow.
- Provenance is recorded as original project-authored vectors/code in the manifest and issue history. No third-party image/font, generated raster, new artwork or alternate icon family was introduced during recovery.

## Bounded recovery changes

The previous gate passed without comparing Theme/palette values and allowed CSS,
relative hrefs and stroke-style overrides to evade the stated SVG contract. The
validator now checks canonical RGB parity for all palette roles; exact Theme color,
alpha, geometry, font-size and focus bindings; selected actual text contrast pairings;
registered runtime icon paths; and unregistered SVGs outside known families.

SVGs now use an explicit compact vector element/attribute allowlist, which rejects
references, CSS, scripts/events, animation, external namespaces and declaration-based
content, and enforces rounded caps/joins and the 1.8 stroke. The sole runtime addition
is `VisualPalette.Exploration`, exposing the existing token without changing colors.
Eight regression tests, including malformed candidate subcases, run in the normal
build workflow. Assets, layout, commands, simulation, saves and map behavior are unchanged
by this recovery commit.

## Validation and limitations

- `python scripts/validate_visual_assets.py`: PASS, all 46 icons / seven families, actual palette/Theme parity, contrast and loader registration.
- `python scripts/test_validate_visual_assets.py`: PASS, eight tests including SVG/style/reference bypasses, root style drift, unknown family, missing icon, palette/Theme/focus drift and coordinated low-contrast changes.
- `git diff --check`: PASS.
- Computed text contrast on primary surface: primary 16.185:1; secondary 9.474:1; muted 4.817:1; focus/pressed text 12.980:1. These are opaque token pairings, not claims about arbitrary composited backgrounds.
- Recovered head's remote build #1869 (`34254977881`) and research-outcomes #53 (`34254977756`) report success. Testing/Core has identified a false-positive runtime smoke issue in the baseline pipeline; those results do not independently establish rendered readiness.
- Local Release builds were attempted before and after the change but stopped before compilation on denied access to the host NuGet.Config, followed by unresolved `Godot.NET.Sdk/4.7.2`. Core can rerun with its granted environment permissions; logs are in the orchestration workspace `work/visual-build-baseline.log` and `work/visual-build-candidate.log`.
- Existing real screenshot record references run `34253094688`, captured before the latest map/button integration. No new real Godot capture was inspected in this recovery; all visuals retain **Production candidate** status.
- Current map icons draw at 9/11/12.5 px (selected Unknown 14 px), below the recorded 16/20/24/32/48 px proof sizes. Small-marker readability, legacy-marker overlap, the known Shipbuilding HUD collision, modal opacity/input behavior and keyboard navigation remain rendered/UI checks.
- Static validation does not establish exact path bounds/safe padding, optical recognition, renderer fidelity, accessibility compliance or provenance beyond the recorded project history. No new font or branding clearance is claimed.

## Integration request

Core should review this focused validator commit together with the existing PR #218
milestone, then push the existing branch and run the repaired Testing/Release gate.
Merge only after independent validation; preserve Galaxy's system-view input/render
guards when combining PR #220. In particular, the visual overlay added by
`IntegratedMain.Visuals.cs` must not draw strategic stars/fleets over a system view.
Update PR #218's obsolete remaining-map-consumption note to reflect the actual tree.
No push or integration was performed by this specialist.

Stable consumption paths remain `assets/visual/ui/visual_tokens.json`,
`assets/visual/ui/stellar_continuum_theme.tres`, `VisualPalette`, `VisualIconLibrary`,
core Colony/Scout/Unknown/Hostile, the three survey icons and the three ship-role icons.
Galaxy #219's request is partly covered; no distinct Outpost icon exists yet and no
authoritative outpost concept or ship role should be invented merely to fill that slot.

## Next recommended scope

After integration, have UI/Testing capture menu, campaign, colony, relations and Galaxy
system view with keyboard focus and dense markers at 1280x720 and 1920x1080. Resolve
small-size/overlap findings with Galaxy and UI owners before promoting any family to
Production ready. Defer new art and research-specific icon expansion until consuming
contracts and demonstrated screen needs stabilize.
