# Territorial influence and supported expansion

Owner: feature/territorial-influence; coordination: #321 / #15. Base: integration 97091aee.

## Authority and units

Territory is a derived simulation read model. No influence currency is stored or spent.
Physical distances use the existing three-dimensional light-year coordinates; display conversion remains in the units layer.
Colony population, infrastructure, stability, administration tier and completed construction supply permanent anchors. Operational orbital installations extend those anchors. Fleets supply temporary military projection only. Existing Diplomacy claims remain separate political assertions, never automatic ownership.

Political influence uses bounded exponential distance falloff. Administration requires connection to a populated administrative anchor; a chain of relays can extend it, with attenuation on every segment. Operational supply follows actual travel lanes and functioning bases, independently of political influence. Trade reach reflects connected economic infrastructure, not assumed delivered freight. No cargo or colony sustenance is invented by this model.

## Expansion and consequences

Established systems have strong local administration and political support. Frontier systems can be settled with a support chain, at greater expedition cost and slower establishment. Remote settlement is rejected with the deficient scores and a suggested relay/depot step. Existing environmental suitability, surveys, foreign occupancy, ship fuel and passenger checks still apply.

Weak effective control reduces tax collection and increases administration expense. Installations have paid capital, industry, construction time and recurring upkeep. Funding shortages reduce their function. Passing fleets never establish sovereignty. Overlap retains an independent share and competing influence; close competitors produce contested control instead of arbitrary winner-by-ID ownership.

## Integration contract

`TerritorialRuntime` owns cached snapshots, initialized at campaign startup and advanced by simulation time. Rendering reads snapshots and does not calculate influence. Source changes invalidate on the next simulation step; periodic recomputation handles development/funding. Commands revalidate before mutation. Saves persist infrastructure, construction progress, expedition authorizations and the territorial clock; derived fields are reconstructed.

The feature consumes existing Research capabilities, AI traits, colony/outpost state, lane graph, economy and Diplomacy observer views. It does not replace those owners. Native orbital projects remain home-system infrastructure; new regional installations have explicit system identities and require a logistics vessel on site.

## Player flow

Select a surveyed system. The inspector groups influence share, effective controller, claims, administration, operational supply, trade, military support and expansion eligibility. It includes known competitors and the player's strongest supporting sources. Controlled systems without colonies also appear on the map.

Research Orbital Industry and move an idle logistics ship to a supportable system. Its arrival must finish, and it must have no cargo or assigned freight route. In Regional Infrastructure, select an installation to see its treasury price, industry, construction days, daily upkeep and blocking conditions before starting. Keep the ship on site; moving it pauses work. Another eligible ship can resume the paid site, while shortages slow construction. Cancellation returns 80% of the unfinished portion of paid capital and industry. Decommissioning a completed installation requires confirmation and provides no salvage refund.

Relays extend administration; depots and naval bases extend operational fleet support. These are different requirements. An unsupported isolated installation cannot create unlimited self-sustaining reach. Normal colony/outpost orders quote the actual expedition charge and establishment duration, including frontier penalties. A remote refusal identifies deficient support and the closest populated anchor. Existing biology, survey, passenger, fuel and foreign-occupancy checks remain in force.

The optional overlay starts enabled. Translucent fills and continuous exterior contours represent connected visible holdings, claims use separate dashed outlines, and contested systems use amber markers. Detailed frontier/control information stays in selection panels. Smoothing the map never grants authoritative ownership.

## Channels and balance

`src/Game/Simulation/Territory/` owns derived snapshots and installation/expedition facts. Existing subsystems still own population, economy, research capabilities, construction, travel, diplomacy and combat.

| Channel | Meaning |
| --- | --- |
| Political influence | Populated colonies, staffed outposts and funded infrastructure, with bounded distance decay |
| Influence share | Political strength divided by all civilization strengths plus independent-space weight |
| Administration | Populated roots and connected attenuating infrastructure, reduced by overextension |
| Operational supply | Actual lanes, active fleet range/fuel capabilities and reachable refueling bases |
| Trade | Connected economic infrastructure potential, not proof of delivered freight |
| Military | Active local fleet combat power and base projection; fleets do not create political ownership |
| Effective control | Influence share weighted by administration, supply, trade and military support |
| Claims | Observer-safe Diplomacy assertions, separate from control |

`data/territory/balance-v1.json` is the maintained tuning surface, embedded as `Game.Territory.Balance.json`. Loading validates finite positive inputs, fractions and complete unique installation definitions. It is independent of the working directory. Changing embedded balance requires rebuilding.

For source range `R`, political strength decays as `strength × exp(-1.6 × distance/R)`, bounded at four source ranges. Colony strength before stability/funding adjustment is `42 + 38 × log10(1 + populationMillions)/4 + 5 × infrastructure + 3 × hubTier`; outposts start at 18. Default colony/outpost ranges are 32/16 light years. Empty homeworld markers project nothing. Existing research networks, shipyards and mining programs strengthen a real home colony. Recognized claims modestly reinforce supported space; they cannot chain across the galaxy as new infrastructure.

Administrative links must be within 22 light years and lose strength (default factor 0.78, plus distance/receiving infrastructure effects). Maximum-product propagation prevents cycles from amplifying themselves. Local administration decays independently and is reduced by colony load relative to administrative capacity.

Operational supply follows real lanes from colonies. Each leg must fit operational range; accumulated unsupported distance is bounded at twice that range. Reachable refueling sites reset accumulated distance. Known foreign colony corridors require access permission. Support falls with unsupported distance and funding. It neither transports food/water nor fills cargo holds. Territorial depot/naval-base refueling requires a completed installation and at least 25% operational supply.

Default control is `share × (0.50 × administration + 0.30 × supply + 0.10 × trade + 0.10 × military)`. Independent space has strength 14. Controlled requires at least 55% share and 45% control. A second contender with at least 22% share within 20 percentage points of the leader makes the system Contested. Other statuses are Independent, Influenced and Dominant; only Controlled has a non-null effective controller.

| Expansion | Default support | Capital | Establishment time |
| --- | --- | --- | --- |
| Established | Political ≥30, administration ≥50%, supply ≥25% | Existing base price | Existing base duration |
| Frontier | Political ≥4, administration ≥12%, supply ≥12% | Base × `[1.35 + (1 − administration) × 0.65]` | Base × `[1.25 + (1 − supply) × 0.75]` |
| Remote | Below normal frontier support | Normal settlement refused | No instant settlement |

Staffed extraction outposts can bridge weaker support (administration ≥6%, supply ≥8%). Paid frontier authorizations store charge and duration. Retargeting only charges an additional required amount and cannot create money through repeated orders.

Tax collection ranges from 70% to 100%, improving with control; well administered/supplied own colonies retain full collection. Administration expense is multiplied by `1 + 0.6 × (1 − administration) + 0.4 × (1 − supply)`. The inspector's control-vulnerability index describes control/supply weakness; it is not a probability of a newly implemented piracy or rebellion event.

| Installation | Capital units* | Industry | Days | Upkeep units/day |
| --- | ---: | ---: | ---: | ---: |
| Communications relay | 45 | 90 | 15 | 0.045 |
| Supply depot | 80 | 160 | 25 | 0.080 |
| Trade station | 100 | 200 | 30 | 0.100 |
| Naval base | 150 | 300 | 40 | 0.180 |
| Research station | 90 | 180 | 28 | 0.090 |
| Regional administration | 130 | 240 | 35 | 0.120 |

*Internal economy units; players see the existing sovereign-currency formatter, not an invented dollar conversion. Unfinished sites pay 25% upkeep. These are initial balance values requiring campaign tuning.

## AI and diplomacy

`TerritorialStrategicPlanner` advises the existing scheduled strategic director. It evaluates at most 64 surveyed supportable systems, deficits, resources/anomalies, existing priorities and civilization traits. It reserves operating funds, refuses expansion during arrears/poor funding, and finishes an existing project or traveling builder assignment first. It can request a logistics vessel, route it through authoritative travel commands, then construct on arrival. Colony planning uses the same support gates and paid quote as the player.

`TerritorialDiplomacyBridge` binds existing Diplomacy state. A 30-day review can mildly strain relations for mutually observed contested systems with competing known claims. A 180-day pair cooldown prevents incident spam. Mutual access exempts cooperative frontiers. This adapter neither invents first contact nor declares war. Existing claim/access APIs are the extension points for negotiated borders and future agreements.

## Persistence, privacy and caches

`Initialize` creates a per-campaign runtime. `Advance` applies construction time and reviews influence every five simulation days or on source-signature change. `Peek`/`Read` never recalculate. Source signatures cover additions/removals/ownership, population becoming empty, hub changes, site completion, fleet location and completed programs. Periodic reviews cover funding/development. Commands revalidate before spending; source removal recomputes support/control.

Save format 16 gains optional territory schema 1: installations, progress, expedition authorizations, clocks, incident cooldowns and observer reports. Scores, lane caches and meshes are reconstructed. Validation rejects malformed values and dangling references. Old saves without territory derive it at startup; previously paid in-flight colony/outpost missions retain their base authorization/timer instead of being charged twice. Player/Developer separation stays enforced.

Observer reports preserve only legitimately known foreign control at that system. Hidden local activity cannot expose a new civilization or freeze unrelated own expansion/source loss. Fully surveyed controlled empty systems appear without colonies. Reports currently update when an observer projection is built; a future shared sensor-event service can own this lifecycle.

Current physical maps use three-dimensional light-year distances, with metric-primary UI through the units layer. Legacy pre-astronomy saves with arbitrary coordinates use a bounded range adapter and preserve their saved positions. Camera scale affects presentation only.

`StrategicTerritoryProjection` caches actual visual inputs per campaign/observer. A bounded raster samples analytic influence disks and corridors along genuine local same-owner travel lanes, clips boundary triangles and stitches continuous contours. Rival fields compete before tessellation. Camera-scaled positions and radii use the same scale. Fill and outline come from the same field; no interior grid outlines are drawn. Distant disconnected regions are not joined across an unsupported gulf.

The renderer partitions cached owner fills into spatial chunks, caches projected contours under a stable camera and batches claim/contested line segments. Viewport culling checks region, chunk and contour bounds, preserving a crossing border even when all anchors are off-screen. Unchanged numerical reviews reuse geometry. Fog uses a deterministic spatial nearest-system lookup and cached blended mask; full-survey views skip the empty texture. Regression checks compare its mask byte-for-byte with the old brute-force lookup. Full-survey overview omits redundant completed-survey circles and nonessential labels, retaining regional detail while zooming in.

## Validation and development

From the repository root:

```powershell
dotnet build Game.csproj --no-restore
dotnet run --project tests/Game.Territory.Validation/Game.Territory.Validation.csproj -c Release
dotnet run --project tests/Game.Quality.Validation/Game.Quality.Validation.csproj -c Release
dotnet run --project tests/Game.CoreRuntime.Validation/Game.CoreRuntime.Validation.csproj -c Release
dotnet run --project tests/Game.Logistics.Validation/Game.Logistics.Validation.csproj -c Release
dotnet run --project tests/Game.Simulation.Validation/Game.Simulation.Validation.csproj -c Release
```

The maintained Territory console suite is included in CI and uses the shared regression runner. Do not generate or retry scratch executables.

Native UI/input screenshots use the established `STELLAR_CAPTURE_FOCUS=territory` protocol in an isolated profile. `STELLAR_TERRITORY_PERFORMANCE=1` measures live overlay-off/on views; `STELLAR_TERRITORY_PERFORMANCE_FRESH=1` retains starting fog. Require actual 2560×1440 screenshots and foreground focus; background-capped measurements are not gameplay comparisons. The fixture checks real start/cancel input and all actions at 1280×720, plus connected player/rival holdings and panning with off-screen anchors.

Developer selected-system commands are `territory_recompute`, `territory_relay`, `territory_depot`, `territory_remove_sites` and `territory_remove_colony`. The last evacuates population while preserving body/building/freight references for a valid source-loss scenario. Commands record Developer provenance and are unavailable in Player mode.

## Handoff and limits

[Branch handoff](handoffs/TERRITORIAL_INFLUENCE.md) records tests, native receipts, performance and integration status. This feature supplies operational reach and real control effects. It does not replace freight, add a station-combat model, or implement piracy, rebellions and joint sovereignty. Future balance changes must deliberately migrate saved paid-authorization caps. A scripted native scenario is not an unrestricted human campaign playthrough.

Further gameplay and territorial-expansion work is paused for the controlled engine migration. The native21 all-surveyed screenshot also retains smooth enclosed dark pockets inside the large player region; these remain a visible follow-up rather than a renderer-culling defect.
