# Free-placement colony surface

`PlanetSurfaceView` is a full-screen Control with a separate 3D SubViewport, terrain mesh, sun,
sky, camera, colony hub and building models. It is presentation only. `Configure` accepts the
root-owned read snapshot and place-order callbacks; `Open`, `Close`, `IsOpen`, and
`ReturnToOrbit` provide the integration boundary. The bridge must block underlying Main input
while `IsOpen`; this view consumes surface mouse, wheel and camera/placement keys while normal
buttons retain keyboard focus and tooltips.

Set `IsInputBlocked` to the menu-open predicate so the surface also ignores input beneath the
menu, including continuous movement polling and keyboard-activated buttons. Wire `SaveRequested`
and `PauseRequested` to the existing campaign commands; `ReadTimeLabel` supplies the current
date/simulation-speed text. These controls remain available in the surface header.

The build palette shows rendered miniature models of the power generator, science lab and
fabricator, with authoritative cost/output descriptions. A selected model follows a continuous
camera/terrain ray intersection. X/Z are never snapped. Preview validation calls
`SurfaceConstruction.PlacementError` using snapshot buildings mapped to the shared state type.
The order callback remains the authority, including placement, ownership and construction cost.
Both ray intersection and mesh vertices use `SurfaceConstruction.TerrainHeight`.

Controls: WASD pans the camera target, Shift increases movement speed, right-drag orbits,
middle-drag pans, wheel zooms. Click places the selected building. R rotates 15 degrees per
press. Escape cancels the preview, or returns to orbit when no preview is selected. Orbit,
center hub, rotate and cancel are also visible buttons. Boundary beacons mark the build area;
decorative rocks are outside it. There is no construction grid or fixed set of slots.

Pending buildings have scaffolding, a moving construction scanner, an eased rising model and
the current percentage. Completed models show a cyan operational beacon or amber power warning.
Progress/power come directly from the snapshot. No industry is deducted by this view. Closing
disables surface viewport rendering and child processing; meshes are built once, and snapshots
are read at most every 0.15 seconds. The terrain material performs shading on the GPU. The
landscape and architecture are demo illustrations, not measured Earth terrain or NASA imagery.

Stable QA node names (search recursively below `PlanetSurfaceView`):

- `SurfaceViewportContainer/SurfaceViewport/ColonyLandscape`
- `SurfaceCamera`, `Terrain`, `ColonyHub`, `PlacementPreview`, `SurfaceBuilding_<id>`
- `SurfaceHeader`, `SurfaceBack`, `SurfaceCenterHub`, `SurfaceSave`, `SurfacePause`, `SurfaceTime`
- `SurfaceBuildPalette`, `SurfaceStatus`, `SurfaceRotate`, `SurfaceCancel`
- `SurfaceBuild_power_generator`, `SurfaceBuild_science_lab`, `SurfaceBuild_fabricator`

Read-only QA observability: `GetSurfaceScreenPosition(x,z)` projects the shared terrain position
through the actual camera to main-viewport coordinates (null if outside/behind); `CameraPosition`,
`SelectedBuildingType`, `PlacementErrorText`, and `HasGroundPreview` expose current visual state.
Use these to locate and verify real input, without bypassing production event routing.

Validation: all production C# compiled successfully against the actual GodotSharp assembly,
including the root's current UiSurfaceSnapshot, SurfaceConstruction and ColonyState sources.
Only four pre-existing nullable warnings in exploration/colonization code remain. No executable
was launched locally. Shader rendering, screenshots, real pointer input and end-to-end
construction/persistence must still be checked by the integrated Godot validation run.
