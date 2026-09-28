# Galaxy / Star-System Visuals handoff

Date: 2026-09-08

## Playable demo graphics milestone

The user asked for cleaner, sharper graphics and graphical navigation. This continuation uses the existing `work/galaxy-star-system-visuals` branch. Its prior spatial foundation and fixes are already included in the accepted playable demo. The clean worktree was merged with accepted Core `d06e26e` as `24a9c62`; the merge retains Core's modal input guards and survey entry gate. No branch history was reset and no source was deleted.

The regional renderer is now a complete map pass: deterministic space ambience, crisp star cores and survey arcs, segmented selection reticles, selected/home labels, exact-own colony markers and role-colored fleet silhouettes with route arrows. Unknown catalog stars keep neutral colors; stellar archetype colors require full survey. Fleets remain exact-own. Foreign colony/home markers retain the accepted full-survey and known-civilization gates, with a subdued distinct tint and contact glyph for foreign homes. Exact-own co-located ships of the same role use a count badge, backed by a reused dictionary; their actual courses remain separately visible. Public catalog coordinates and `Main.ToScreen` are unchanged.

The orbital canvas adds cached, deterministic shaded sphere illustrations for confirmed broad environment classes, atmosphere rims for appropriate known classes, gas bands, a luminous stellar corona, crisp orbital lines, and neutral silhouettes for unconfirmed worlds/stars. Cosmetic surface patterns do not represent simulation terrain. Hover and click highlights target only existing observer-safe snapshot bodies, without modifying simulation state. Unknown bodies never acquire a surface texture.

The shared viewport now reserves room for the navigation rail, system title and 120-pixel command dock. Its visible planet radii and nearest-body targeting share the same calculations. Selected-world captions sit above the dock. Surface textures are reused while their marker is unchanged and disposed when removed, confidence changes, the system closes or the canvas leaves the scene.

## Core integration requirements

- Remove `base._Draw()` from `IntegratedMain.Visuals`: `DrawVisualMapOverlay` now supplies the entire regional background and objects.
- Disable the separate layer-20 science marker; the regional renderer owns every own-fleet role.
- Hide the compact demo objective strip in orbital view to avoid the system heading at x112/y89–139. Core and UI agreed to this arrangement.
- Keep UI controls above the system canvas and preserve modal/GUI input guards. No new command API is introduced by this branch.

## Validation

- All production `src/**/*.cs` compile successfully against the actual GodotSharp assembly from the previously delivered Windows demo and the installed .NET 8 reference assemblies. Four existing nullable warnings remain in Colonization/Exploration, with no new warnings or errors.
- Thirteen actual-source spatial validation groups pass. Added checks cover visible planet-rim targeting, nearest-body selection in overlapping hit areas, empty-space behavior and orbital-field clearance from navigation controls. Existing checks still cover confidence boundaries, hidden-environment perturbations, deterministic projection, moon parentage, campaign/observer invalidation and bounded refresh.
- Validation ran as a caught managed DLL entry point through `dotnet`, never a scratch apphost executable. Exceptions report type, message, working directory and complete stack trace before returning nonzero.
- Scratch response files, managed host and reference assembly remain outside the repository in workspace `work/celestial-graphics-checks/`.
- `git diff --check` passes. No binary or generated art files are committed.

Actual Godot screenshots, mouse interaction and combined UI rendering remain Core/Testing integration checks. Compilation and geometry tests do not prove rendered appearance. Dense moon/planet label placement remains schematic; astronomy expansion, foreign-object overlays, system-local ship coordinates and new simulation systems are outside this milestone. No push, main promotion or integration acceptance was performed by this specialist.

## Human / Earth origin follow-up

The user extended the graphical demo milestone to a true Earth/Sol start and clarified that humans always begin on Earth. Fresh generation now implements the bounded one-human origin policy, distinct deterministic nonhuman faction homes, a versioned physical solar catalog and explicit save-version protection. See `docs/SOL_STARTING_CATALOG.md` for physical references, exact IDs, compatibility and validation evidence. No presentation files changed in this follow-up; Core owns confirmed solar materials and the real-pointer scene tests. Existing saves retain their original catalogs and physiology assignments.
