# Cinematic spatial navigation

The presentation now connects Milky Way overview, stellar region, orbital system and planet focus. No catalog positions, observer rules, simulation state or save data change.

## Navigation behavior

- Wheel zoom anchors the world point under the pointer throughout interpolation. The dock zoom buttons use the selected star or body as their anchor. Wheel sensitivity is 1.22 and button sensitivity is 1.35; each outward gesture uses the exact reciprocal.
- At far overview, only the local-region locator within 48 pixels of the map origin is interactive. Clicking it reframes the stellar region. Decorative galaxy stars never select hidden catalog systems or issue fleet orders.
- Zooming toward a selected star at regional scale 2.7 or above enters its orbital view only after the existing reconnaissance gate passes. Buttons and double-click use that same gate. Entry fades from the selected star's actual screen position.
- The orbital camera pans with middle-drag and zooms at the pointer. After zoom reaches 1.9 times the fit scale, another zoom toward the selected body opens planet focus. Double-clicking a body or its breadcrumb also focuses it.
- Planet focus uses the GPU `FocusedPlanetView` supplied by the material implementation. The sphere occupies the central viewport; Saturn's rings are included in the initial fit. No CPU image creation occurs during camera movement.
- Back and Backspace step from planet focus to the saved orbital camera, from system to the preserved regional camera, then to galaxy overview. Outward wheel gestures follow the same route. The established dock “Back to Region” command remains a direct escape.
- Navigation keeps animating while simulation time is paused. The campaign menu blocks gestures and camera animation. Pointer commands remain behind GUI event consumption; Backspace is handled only after GUI controls.
- Window resizing translates both current and target origins, preserving the gesture. Drawing, body-position queries and hit testing share the same interpolated camera transform.

## Integration contracts

`Main.UiOverviewBlend` is 1 below zoom .08 and 0 above .22. `UiMapOriginScreen` and `UiMapZoom` expose the actual unchanged catalog transform. The decorative art rectangle is 32000 by 18000 world units, with Sol at UV (.68,.60). `SpatialNavigationLayout.FitGalaxyOverview` reserves the top 112 and bottom 128 pixels, keeping the whole artwork above the command dock. At 1280×720 its art rectangle is (247.33,112,853.33,480).

Breadcrumb node names are `SpatialBack`, `SpatialOverview`, `SpatialRegion`, `SpatialSystem`, `SpatialPlanet` and `SpatialSurface`. Dock zoom node names are `MapZoomIn` and `MapZoomOut`. Breadcrumbs start at (118,78); the demo objective strip is at (120,112) and appears only in regional view. The orbital header starts at y135.

`PlanetSurfaceRequested` is an `Action<int>` event. The separate surface owner sets `PlanetSurfaceAvailable` (`Func<int,bool>`) to its authoritative availability predicate and subscribes to the event. Surface stays disabled without both contracts. `UiFocusedPlanetBodyId` exposes the observer-visible focused body. This change does not implement surface construction.

Read-only acceptance queries:

- `UiCameraSnapshot`: `Level` (`GalaxyOverview`, `StellarRegion`, `StarSystem`, `PlanetFocus`), `Zoom`, `Pan`, `TargetZoom`, `TargetPan`, `FocusedBodyId`, `IsTransitioning`. Pan fields are absolute screen origins, not offsets from viewport center.
- `UiGetCatalogScreenPosition` and `UiGetBodyScreenPosition`: actual rendered positions at the current interpolated transform.
- `UiSpatialCatalog`: public system IDs and the player's survey level. It exposes no hidden physical detail.
- `UiSystemBodies`: the current observer-filtered projection, including `HasDetailedEnvironment` and `SurfaceKey`.
- `UiCachedPlanetMaterialCount`: live orbital surface textures plus active GPU focus view. Closing a system removes that view, clears its surface textures and empties its body projection.

## Validation

The complete production source compiled against the delivered GodotSharp assembly and .NET 8 references. There were no errors; four existing nullable warnings remain in colonization/exploration code. The supported Quality validation source was compiled and executed through the caught managed DLL host: 8/8 quality checks passed, including all 20 spatial contracts.

Four new contracts exercise pointer-anchor preservation at every animation frame, target preservation across resize and cancellation on drag, invalid/bounded camera scales, and inverse hit transforms during zoom plus resize. Existing knowledge/projection checks still pass.

The visual follow-up adds three contracts: confirmed canonical Sol appearance with procedural/reconnaissance counterexamples, galaxy fit and fixed Sol art anchoring at four window sizes, and orbital-context retention through approach plus reverse restoration. Earth remains terrestrial `Rocky` with `IsImmersedEnvironment=false`; `HasIllustratedOcean` permits blue atmospheric shading and ocean glint only for its confirmed canonical surface or a confirmed oceanic class. Only known `sol-v1` Jupiter/Saturn use gas-giant appearance; Uranus/Neptune use ice-giant appearance. Ordinary generated worlds retain the existing class heuristic.

The orbital field now fades according to the focused disc's actual interpolated radius. It remains visible at the beginning of the approach and returns along the same scale path on Back. The focused GPU disc and navigation header do not inherit that fade. Root's separate surface integration must still add the surface/menu blocking predicate in `Main.SpatialPresentation` and its own lifecycle hooks; this follow-up does not edit those files.

Local validation does not prove Godot shader compilation or final rendered layout. The combined candidate still requires actual Godot real-pointer screenshot acceptance for overview, zoomed region, orbital pan/zoom, Earth focus, reverse restoration, resize and unknown-system rejection. No scratch executable apphost was launched and no binary is included in this change.
