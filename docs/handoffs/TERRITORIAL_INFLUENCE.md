# Territorial influence handoff — 2026-09-12

## Branch and coordination

- Repository: `afterburn25/stellar-continuum`.
- Branch: `feature/territorial-influence`, based on `integration` commit `97091aee84b782bdc917185307cd42b97b8fd7d0`.
- Source milestones: `40d73291` (runtime/expansion/AI), `6e9cad90` (balance/persistence/recovery), `f5d4a330` (native UI, continuous borders, tests and CI).
- Coordination: [#321](https://github.com/afterburn25/stellar-continuum/issues/321), project coordination [#15](https://github.com/afterburn25/stellar-continuum/issues/15).
- Feature changes are proposed for integration; no integration/main merge is authorized by this handoff.

## Delivered behavior

The player can inspect political influence separately from control, administration, operational supply, trade, military strength and diplomatic claims. Paid, timed relay/depot/trade/naval/research/administration installations require actual on-site logistics ships. Construction pauses when its builder leaves or funding fails, can resume with a replacement builder and supports proportional cancellation recovery. Sites extend reach without requiring a colony in every controlled system.

Established, Frontier and Remote expansion use existing colonization commands and safeguards. Frontier orders display and charge increased expedition costs and extend establishment time. Remote refusals give support thresholds and the nearest populated anchor. Weak control affects actual tax collection and administration expenses. AI uses the same reach, travel, budget and construction rules, with reserves and anti-spam gates. Known competing claims interact with existing diplomatic relations on a bounded schedule.

Saves persist sites/progress, expedition payments/durations, clocks, diplomatic cooldowns and observer reports, reconstructing derived scores on load. Legacy already-paid missions retain their prior authorization. Developer source-placement/removal commands are gated and recorded as Developer provenance.

Design, formulas, tuning, player controls and extension boundaries: [Territorial influence](../TERRITORIAL_INFLUENCE.md).

## Reported visual gaps: causes and repair

The user continued seeing disconnected/square influence during native tests. Three defects were addressed rather than changing authoritative control to hide them:

1. Full-galaxy positions were enlarged for the camera while territorial radii still used fixed visual units. Radii now use the same physical/legacy coordinate adapter as anchors. Same-owner local travel-lane corridors join their regions continuously.
2. Native fill rendering needed both full interior runs and clipped boundary triangles. Both now enter the same owner mesh; contours derive from the same field, with no interior cell outlines or seam-only outlines.
3. Regional culling tested anchor/label visibility and could omit a region crossing the screen when those points were off-screen. It now tests the entire projected contour bounds against the viewport.

The maintained regression uses a physical FullGalaxy500 map at scale 14 and verifies continuous coverage between six holdings, interpolated edges and a controlled empty system. Rival anchors and genuinely distant disconnections remain protected. Observer regressions cover hidden local changes, legitimate own source additions/removals and saved foreign reports.

Native scenario `territory-native-14` adds six lane-connected player colony systems and five rival holdings through actual campaign state, recomputes authoritative influence and exercises native mouse pan/zoom. The map and connected-pan screenshots show continuous rounded fill with no artificial interior holes/grid seams. The boundary-cull screenshot retains a crossing edge when its stars and label have left the viewport.

The later all-surveyed `territory-native-21` receipt exposes several smooth, enclosed dark pockets inside the large cyan player region. They are not square-grid seams or viewport-culling omissions, but they still read as influence gaps in the current presentation. This visual issue remains open and was not hidden by the renderer performance work.

## Validation receipts

Run all console suites from the repository root with `dotnet run --project tests/<suite>/<suite>.csproj -c Release`.

| Suite | Result | Local receipt |
| --- | --- | --- |
| Game.Territory.Validation | 17/17 | `logs/territory-final-optimized-validation.log` |
| Game.Quality.Validation | 20/20 | `logs/territory-quality-final-optimized-validation.log` |
| Game.CoreRuntime.Validation | 89/89 | `work/territory-core-final.log` |
| Game.Logistics.Validation | 4/4 | `logs/territory-logistics-validation.log` |
| Game.Simulation.Validation | 72/72 | `logs/territory-simulation-validation.log` |

Coverage includes source decay/loss, military versus political ownership, overlap/privacy, independent administration/supply, paid construction pause/resume/cancel, funding/freight constraints, frontier costs and saved duration, legacy paid migrations, malformed saves, deterministic reviews, AI fairness and Diplomacy observation/access/cooldown. CI includes the new maintained Territory suite. Game native builds passed without warnings/errors; Core test compilation retains existing nullability warnings.

Scripted simulation playtests: compact 36-system and 100-system profiles held infrastructure for 90 days without spam or insolvency; the 500-system 45-day profile started and completed one research station. These are observation results, not evidence that all long-term AI pacing is balanced. Core's existing full Player opening tests reached a real surveyed settlement through Adaptive Research in both legacy and ordinary Sandbox profiles, and the accelerated Developer scenario preserved the same rules.

Native receipts use isolated save/audio profiles and the existing capture protocol. Real Start/Cancel input passes, including stable controls while simulation refreshes; every installation action remains scroll-reachable at 1280×720. Native tests are scripted scenarios, not a completed unrestricted human campaign playthrough.

## Performance

Live native21 foreground measurements, actual 2560×1440, running simulation, 500 fully surveyed systems with added player/rival holdings:

| View | Overlay hidden FPS | Overlay shown FPS |
| --- | ---: | ---: |
| Overview | 134.8 | 129.8 |
| Regional | 63.1 | 60.6 |
| System | 141.2 | 136.5 |

This proves the target on this test machine/configuration, not a universal 60 FPS guarantee. Native15 on committed source exposed a focused regional regression at 48.0 FPS with the overlay shown. The correction culls individual contours and spatial fill chunks, batches claim/contested line segments and draws one antialiased 3.4-pixel border. Native21 passed the same developed scenario with empty stderr and both native UI checks. Evidence is under `work/territory-native-21/`; generated receipts are not committed.

Territory CPU profile, Release, generated scenarios (milliseconds; construction/rendering excluded):

| Systems | Initial runtime build | Warm review p95 | Retained bytes |
| --- | ---: | ---: | ---: |
| 100 | 57.448 | 0.420 | 247,064 |
| 500 | 27.957 | 2.731 | 613,744 |
| 1,000 | 93.774 | 8.083 | 1,991,296 |
| 2,500 | 401.562 | 24.002 | 5,016,240 |

Busy 500-system/eight-civilization/100-source review: 17.388 ms warm p95. Cold large-map work includes the existing lane graph; it is not charged each render frame. The 2,500-system cold allocation was ~235 MB and remains a startup optimization opportunity.

Fog nearest-system lookup was changed from every-cell/every-system scanning to a deterministic KD tree, preserving catalogue-order distance ties. Masks are compared byte-for-byte with the previous algorithm at 12 and 500 systems. Projection profile after this optimization:

| Physical map | Normal build ms | Unchanged cached build ms | All-revealed build ms |
| --- | ---: | ---: | ---: |
| 500 | 22.654 | 0.279 | 107.111 |
| 1,000 | 27.836 | 0.415 | 128.715 |
| 2,500 | 35.539 | 0.711 | 58.640 |

These are individual observations affected by runtime warmup, not directly comparable FPS or monotonic complexity measurements. Ownership/survey changes may still incur a one-off geometry build. Unchanged source reviews reuse projection and GPU meshes; panning only transforms cached geometry. Future work can move genuine expensive rebuilds outside the rendered frame. Do not restore the old quadratic fog scan or hide rival overlays to inflate benchmark results.

## Remaining constraints and next integration steps

- Operational supply is service reach. Existing freight remains responsible for actual materials/food/water delivery; no phantom cargo is created.
- Vulnerability is an exposed index. Piracy, rebellion, espionage, joint sovereignty, new treaties and station-target combat are future subsystem integrations, not completed mechanics here.
- AI pacing needs longer campaign balance work. Compact profiles may correctly hold; use decision reasons and meaningful deficits before loosening anti-spam/reserve gates.
- Observer reports update during projection construction. A shared sensor-event lifecycle is the future extension point.
- Future tuning that lowers installation prices must migrate saved paid-cap validation intentionally. Schema 1 should not silently reinterpret an already-paid expedition.
- Gameplay and territorial-expansion work is paused while the controlled engine migration is completed. Resume feature tuning only after the migration establishes its rendering and validation baseline.
- Review the feature PR against current integration, run its CI and resolve any ownership conflicts before merging. Main is untouched. A public downloadable release is a separate packaging milestone.

Final native source receipt, reviewed screenshots and PR/check status are appended by the completing coordinator.
