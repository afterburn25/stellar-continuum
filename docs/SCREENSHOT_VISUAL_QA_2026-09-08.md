# Visual screenshot QA — 2026-09-08

Source: real Godot 4.7.2 screenshot workflow run `34253094688`, artifact `stellar-continuum-screenshots-16`.

## Captured screens

- `01-main-menu.png`
- `02-campaign-overview.png`
- `03-colony-sites.png`
- `04-relations.png`

## Confirmed findings

### Main menu

- Procedural star field, distant stellar focus/orbital arcs and lower-left planetary limb render correctly.
- Shared Theme is visible on the menu panel/buttons.
- Defect: the dynamic Shipbuilding HUD uses CanvasLayer 20 while the menu had CanvasLayer 10, causing the cyan shipyard status line to render across the menu title/panel.
- Visual workstream correction: `MainMenuLayer` raised to CanvasLayer 100 so gameplay HUD layers remain behind the startup overlay.

### Campaign / colony / relations

- Shared Theme is consistently applied to the current programmatic panels and buttons.
- The galaxy background still communicates most catalog systems as tiny color/brightness-only dots; the new survey-state vocabulary is not yet present in the captured build.
- The Shipbuilding HUD text at y≈198 intersects the upper PlayerControls panel/header region. This is a layout/ownership issue for UI #27; visual work should not silently reposition another workstream's control surface.
- Relations overlay retains readable contrast, but the underlying right-side panels remain faintly visible beneath it. This is acceptable as modal context only if UI #27 intends the panel to be non-modal; otherwise opacity/input treatment should be decided there.

## Visual workstream actions from this capture

1. keep startup menu above all gameplay CanvasLayers;
2. overlay shape-based survey/colony/fleet SVGs in the integrated galaxy renderer;
3. begin consuming core icons in existing buttons without changing layout/commands;
4. rerun the four-frame screenshot suite after these changes;
5. promote assets from Production candidate to Production ready only after actual captured-context review.
