# Combat in the actual star system

Target: the next Alpha after the validated 0.1.0 playthrough checkpoint. Battles must show
ships moving and firing among the encounter's actual star, planets, moons and stations.
The tactical view must remain controllable and responsive while zooming from formations
to detailed vessels.

## Authoritative boundary

`CampaignMassiveEncounter.SystemId` identifies the real system. The observer-safe system
projection supplies its surroundings. Combat positions, headings and velocities remain
simulation-owned kilometres; a stable documented mapping places that plane in the system
scene. Camera projection and unprojection must use the same transform for display,
selection and orders. Presentation never changes movement, damage, range or outcomes.

The renderer consumes safe formation/cohort snapshots, missile progress and combat events.
Beam and kinetic effects represent resolved aggregate volleys; missile visuals follow actual
in-flight aggregate salvos. Impacts and destruction effects follow observed damage/loss
events. Hidden attackers must not gain visible positions, trajectories, designs or composition
through effects. A known owned target can show an incoming impact without disclosing its
unknown source. No decorative firing or destruction is allowed to imply a gameplay result.

## Visual implementation

- One shared 3D scene and camera show the real system environment and combat ships.
- The presentation mapping for a system-design coordinate `(x_km, y_km)` is
  `world = (0.36 * systemDesignRadius, primaryStarRadius + 38, -0.23 * systemDesignRadius) + (0.07 * x_km, 0, 0.07 * y_km)`.
  This is a presentation placement and scale mapping only: it does not assign physical
  planet or ship scale and does not add a new 3D simulation. The battle remains the
  authoritative kilometre plane, with the same shared camera projection/unprojection
  used for display, selection and orders. Sky ray misses reject orders rather than
  inventing a target.
- Up to 32 selected/important vessels receive detailed hulls, armour, nacelles, visible
  weapons and emissive nozzles; their motion follows interpolated authoritative snapshots.
- Distant cohorts use bounded cached 3D MultiMeshes, with at most 4,096 representatives.
  Unknown composition uses a generic silhouette. The 100k path does not create 100k nodes.
- At most 192 pooled weapon/impact/destruction effects animate at once. Pause freezes
  battle effects; resume and selected speed remain consistent with the simulation clock.
- Only selected, hovered and important formations get persistent detailed labels in dense
  views. Left selection/drag selection, right orders, orbit/pan/zoom and menu recovery remain.

The repository currently provides procedural `ShipGeometry`, not imported production ship
assets. This milestone improves those actual game models and puts them in the battle scene;
it must not be advertised as finished photorealistic ship art. A later asset pass can replace
the geometry without replacing the simulation or safe presentation contracts.

## Acceptance

Show a small real system encounter at native 720p and 1080p, including a close ship view,
movement, weapon travel, impact and observed destruction. Verify real typed orders, input
alignment, pause/resume/speed, menu/reload and repeated scene teardown. Test observer
privacy and unchanged combat outcomes. Re-run the maintained 100k inventory/conservation
and bounded renderer sample, recording frame-time percentiles and resource counts.

Do not infer commercial performance or finished art quality from a short deterministic
sample. Record remaining visual limitations and the exact source of every native receipt.
