# System combat visuals handoff

## Source and current receipt

This 0.1.1 stream follows released 0.1.0 Alpha at `f9c57dbf` and the accepted combat
checkpoint `a67f6901`. Presentation consumes the real system context and observer-filtered
combat snapshots without changing authoritative outcomes or disclosing hidden knowledge.

The local native receipts are recorded in `work/combat-system-3d-a67-accepted` and
`work/combat-system-menu-accepted`. Both exit 0 with empty stderr; the presentation/capture
content digest is `88b61bc299f822290ce4dcf3f788f195b3a9c045d9c9fe4392e4d655e062cc48`.

The authoritative battle kilometre plane maps to the system scene as:

`world = (0.36 * systemDesignRadius, primaryStarRadius + 38, -0.23 * systemDesignRadius) + (0.07 * x_km, 0, 0.07 * y_km)`.

This is presentation placement and scale, not physical planet or ship scale and not a new
3D simulation. Projection and unprojection share one camera transform for display, selection
and orders; a sky ray miss rejects an order. Cohort representatives remain bounded and
observer-safe. Observer snapshots expose only safe headings, impact positions and salvos.
Persisted launch positions and initial durations are nullable; legacy unknown trajectories
preserve null source, target and counts when hidden. Snapshots are read-only projections.

## Validated behavior and limits

- Release build: 0 warnings, 0 errors.
- MassiveCombat validation: 17/17 passed.
- MassiveCombat.Persistence validation: 6/6 passed.
- Stress fixture: 100,000 disposable `FleetState` records reconciled without injected damage,
  outcomes, combat events or save state.
- Live-fire fixture: real loadout, UI-routed order, normal engine ticks, and observed
  `FormationDestroyed` event.
- Accepted native checkpoint: exit 0, empty stderr, 35 checks and seven PNGs at native 720p/1080p;
  integrated menu capture passed at both resolutions.
- Performance: 17.90 ms average, 16.80 ms p95, 201.72 ms maximum, 58 FPS, and 100 far-LOD
  representatives over 240 rendered frames.

Detailed vessels use expanded procedural geometry because imported production ship meshes are
not present. Limits are 32 detailed vessels, 4,096 representatives, and 192 pooled effects.
No audio audibility verification is claimed; visuals remain Alpha-quality.
