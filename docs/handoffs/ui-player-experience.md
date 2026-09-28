# UI / Player Experience handoff

Date: 2026-09-08
Branch: `work/ui-player-experience`
Accepted demo baseline: `dc1df68d5d472ce9b9132bdc79af3a469f5d4578`
Tracking: UI #27; Visual #217; Galaxy #219

## Current milestone: graphical playable demo

The map is the primary surface. A narrow icon-and-label navigation rail opens one
right drawer; it is closed initially. Research, Industry and Ships each have a
vector focal illustration, current project title, actual progress bar, details
and existing authoritative commands. The top strip shows resources and time;
the bottom target dock retains Home, Scout, Science, orbital/region navigation,
inspection and graphical zoom controls.

Inspection, logistics, exploration, colony opportunities and relations share the
same scrolling drawer. Their CanvasLayer scripts remain direct Main children,
but their content controls are registered into CampaignSidebar. Colony and
Explore sections reuse the existing exploration presenter and selection state.
No simulation rules, saves, information boundaries or order validation changed.

Demo guidance is a compact three-step milestone strip with full details on demand.
Its steps use the typed read-only dashboard state. The strip is hidden in orbital
view so the system renderer's title remains unobstructed. The campaign menu keeps
existing save, new-game confirmation, Continue Demo and Play Demo behavior.

## Integration contract

Requires Core's Main.UiDashboard display snapshot, nine Nav icon properties, and
UiZoomIn/UiZoomOut commands from `08b4d90`. Core removes the old immediate-mode
text HUD and owns the diplomacy-to-drawer adapter. Galaxy owns map/orbit rendering.

Stable runtime nodes:

- CampaignSidebar/NavigationRail/NavigationScroll/Items/NavResearch (and other Nav labels).
- CampaignSidebar/DetailDrawer/Body/Header/DrawerClose.
- CampaignSidebar/DetailDrawer/Body/DetailScroll/Panels contains the selected
  Research, Industry, Ships, Exploration, Inspection, Logistics, Relations, Menu,
  or Demo panel. Colonies uses Exploration with the colonies section key.
- PlayerControls/ResourceBar and PlayerControls/MapToolbar.

Public section keys: research, industry, ships, explore, colonies, inspection,
logistics, relations, menu, demo. `ShowSection` toggles an already-open section;
`CloseDrawer` hides all registered panels. Actual panel/rail/dock bounds stop mouse
input, while uncovered map areas retain normal navigation. Buttons have keyboard
focus and labels/tooltips; drawer and rail scroll with focused content.

## Validation and remaining gate

`git diff --check` passes. Real rendered and pointer-driven validation is owned by
Testing on the combined Core candidate; no local visual-pass claim is made.
Inspect default and resized viewport, all section switching, close-to-map, dock
commands, colony actions, menu confirmation/cancel, and click shielding. No binary
artifacts are tracked and no scratch executable was launched.

No branch publication or change to main/integration was performed by this workstream.

## Pending integration: observer-safe strategic territory projection

`Main.StrategicTerritory.cs` adds a cached presentation projection for the galaxy/regional
map. It unions actual, visible homes and colony holdings into bounded grid patches with exterior
contours; disconnected holdings stay separate and competing owners are assigned disjoint cells.
Visible diplomatic claims are dashed secondary outlines only, never filled ownership. A current
colony owner replaces an older natal-home marker at that system; there is no sovereignty-transfer
model beyond the authoritative colony record, and fleet presence is deliberately not treated as
ownership. Foreign names, owners, claims, and holdings remain absent until both the civilization
is known and the anchor system is fully surveyed. Unknown space receives a contiguous
presentation-only fog veil; the map owner may use the cached unexplored-system set to attenuate
public coordinate stars.

The one draw hook sits after regional space and before lanes/stars in `Main.VisualMap.cs`; it
does not alter the map owner's star helpers or simulation state. `StrategicTerritoryProjectionValidation`
covers unknown-owner suppression, discovery changes, connected own holdings, separate claims,
non-overlapping visible empires, fog geometry, and deterministic repeated projection. Native
screenshot review remains pending the map owner's GPU slot.
