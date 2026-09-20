<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Territory and save scaling follow-up

This pass continues the 10,000-system work documented in
[LARGE_GALAXY_ENGINE_REPORT.md](LARGE_GALAXY_ENGINE_REPORT.md). It removes the
4,096-anchor/claim presentation cutoff and reduces full-campaign save encoding
work. The separate overview and stationary regional star-map backgrounds remain
as implemented in that pass.

## ENGINE CAPABILITIES ADDED / EXTENDED

### Exact spatial queries for territory and fog

Engine's header-only `SpatialPointIndex` owns an immutable, balanced 2D tree.
`nearest` returns squared float distance; `maximum_influence` returns weight
minus Euclidean distance, including negative results. Both return original input
indices, break exact ties by input order, accept self/partition exclusions, and
optionally report evaluated candidate counts. Nodes carry bounds, maximum
weight and homogeneous partition information. Queries allocate no scratch,
permit concurrent reads, and reject non-finite or out-of-range coordinates and
weights (absolute maximum 10 million). Storage is linear in the input size;
degenerate overlapping points can still require linear query work.

Native territory uses this generic Engine primitive for friendly-neighbor
radii, cell ownership, continuous border fields, owner preservation and nearest
system fog. Core still owns settlements, claims and knowledge. Capture still
filters the observer's visible state before a worker receives it. The direct
projection entry point now uses the same detached-input path as the worker.
There is no simulation change and no dropped anchor or competing claim to meet
a display quota. Catalogs still obey Core's 10,000-system maximum.

The projection keeps its existing 160-cell axis resolution and 16 MiB image
cache budget. The index and geometry are additional memory. Coordinates are
checked before grid construction against the contour key range (one million
scaled units); malformed input fails explicitly. Preparation still allows one
active worker and one coalesced replacement, with stale publication prevention
and one-time failure reporting. An unchanged source fingerprint avoids building
a fresh detached DTO on every frame. Its dependencies include observer,
coordinates, knowledge, survey level, civilization names/home systems,
settlement ownership and the supplied claims.

The new Engine query test compares both metrics with exhaustive float scans,
including ties, self/partition exclusions, empty/all-excluded sets, negative
fields, duplicate points and invalid coordinates. A candidate-count check
guards against restoring exhaustive scans on an ordinary 10,000-point field.
Three native fixtures pin every pre-change contour/fill vertex, label, anchor,
claim and fog texel. Existing visibility, takeover, cache, worker snapshot,
coalescing, failure and cancellation checks remain active. An additional
worker test retains all 10,000 anchors and 20,000 competing visible claims in
a six-empire synthetic territory fixture. This is a presentation stress test,
not a simulated late-game campaign.

### Single-document campaign save composition

Core's private `detail::encode_galaxy_payload_v16_document` builds and validates
an ordered galaxy JSON document. The player encoder extends it directly;
the developer encoder extends the player document directly. Public encoders
retain their signatures and output strings. They no longer serialize and parse
entire intermediate galaxy/player documents. UTF-8 text validation keeps the
strict serializer's errors without producing an intermediate full document.
Existing non-finite numeric and timestamp validation retain their stages and
paths. There is no schema, ordering, whitespace or persistence policy change.

Normal saves, prepared background saves and developer checkpoints consume these
same Core encoders. Immutable snapshot capture, worker ownership, atomic file
replacement, backups, recovery and final-save durability are unchanged. The
full JSON tree and output string still exist in memory: this is not streaming
persistence.

Player tests compare all successful fixtures byte-for-byte with the original
serialize/parse composition. They also cover Unicode, escapes, negative zero,
null optional sections and invalid UTF-8. Developer tests compare the embedded
campaign with the former serialized player envelope, alongside existing fixed
tick/AI continuation save-reload checks. Generic Engine point queries can be
reused for other spatial fields and nearest-site overlays; the private Core
document seam supports future save-envelope composition without another
simulation or public dependency on a JSON implementation.

## Measurements on this host

The full native build and **210/210 native regression tests** passed (243.07
seconds), including save/recovery, developer continuation, 2,500/5,000/10,000
catalog round trips and background rendering checks. Build logs are
`work/engine-scale-final-build.log` and `work/engine-scale-test-build.log`;
full regression evidence is `work/engine-scale-final-regression.log`.
The indexed-query regression evaluated 2,532 radial candidates across its 300
ordinary 10,000-point queries, compared with 3,000,000 for exhaustive scans.

These are isolated observations, not hardware-independent thresholds.

| Territory fixture | Before cold update | After cold update |
| --- | ---: | ---: |
| 500 systems, 3 empires, 6 colonies | 168.237 ms | 150.282 ms |
| 500 systems, 3 empires, 100 colonies | 341.159 ms | 202.805 ms |
| 2,500 systems, 6 empires, 30 colonies | 573.111 ms | 343.161 ms |
| 2,500 systems, 6 empires, 500 colonies | 2,903.880 ms | 462.452 ms |
| 10,000 systems, 6 empires, 9,994 colonies plus 6 homes | Rejected by cutoff | 1,000.860 ms |

The last fixture used 23,040 grid cells and 1,914,880 cached image bytes. Its
asynchronous initial request took 6.587 ms; unchanged requests took 1.340 ms,
down from 5.140 ms before moving fingerprint checks ahead of DTO capture.
Worker completion took 993.248 ms. Those timings exclude the separate
20,000-claim completeness fixture.

The same saved 10,000-system day-85 campaign was restored and captured before
timing three encoder calls. Median encoding fell from **2,967.08 ms to
1,584.74 ms**, a **46.6% reduction**. Every call before and after produced
88,059,463 bytes and SHA-256
`248CA6DDD896948091105EEAD7B9C5813BB20CFAD93589215C3A9A9326EB3D88`.
These measurements exclude capture, disk flush and recovery. Peak memory was
not measured; fewer temporary documents are established by the code path.

A native 1080p, 1,200-frame run loaded that campaign and used the real 8X,
resume and pause controls. It advanced from day 85.2812404 to 165.7570068,
completed a mid-run save, continued advancing, and completed its final save.
The save file's day and 10,000-system count match the validated runtime report.
Steady frame interval averaged 16.766 ms, p95 17.079 ms; update cost averaged
2.765 ms, p95 3.447 ms, with a 42.902 ms worst update. These are fresh-campaign
measurements with one visible territory, not the synthetic fully settled map.
The campaign then passed a separate 720p paused reload and overview/regional/
system transition check. Reviewed captures retain the original galaxy overview
and the stationary star-map image beneath the foreground.

- [Native active campaign after save](../work/engine-scale-captures/active-10000.png)
- [Reloaded galaxy overview](../work/engine-scale-captures/reload-10000.png)
- [Reloaded regional star map](../work/engine-scale-captures/reload-10000-regional.png)

Evidence logs: `work/territory-scale-baseline.log`,
`work/territory-baseline-fixtures.log`, `work/territory-scale-after.log`,
`work/territory-scale-async-after.log`, `work/territory-scale-tests.log`,
`work/save-composition-{before,after}.log` and `work/save-composition-tests.log`.
Native run evidence: `work/engine-scale-active-10000.log`,
`work/engine-scale-active-validation.log`, `work/engine-scale-reload-10000.log`.
The 17 profile-validator unit checks pass in
`work/engine-scale-profile-tools-tests.log`.

## ENGINE LIMITATIONS REMAINING

- Campaigns remain capped at 10,000 systems. Exact lane backbone generation
  remains quadratic; no 100,000-system campaign certification is implied.
- Saves still capture the full world and materialize a full JSON document and
  output string. Disk flush/replacement and capture can still take time.
- Territory work still scales with owner count and fixed-grid sampling. Many
  thousands of separate empires or coincident sites are not performance-certified;
  fixed-grid border detail is unchanged. No universal 60 FPS or long late-game
  guarantee follows from a synthetic fully settled territory test.
- Nebula layering/detail budgets and missing gameplay modifier consumers
  documented in the capability registry remain separate work. This pass does
  not complete the Engine migration or release certification.
