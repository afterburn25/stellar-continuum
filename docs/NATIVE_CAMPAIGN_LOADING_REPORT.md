<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Large campaign loading follow-up — 2026-09-18

This source/build update follows the validated 0.1.13 alpha. It does not replace
the previous local ZIP or declare a new packaged release.

## ENGINE CAPABILITIES ADDED / EXTENDED

The Engine now owns the ordered JSON parser in `json_document.hpp` and
`json_document.cpp`. It preserves the original parser's validation rules and adds
explicit deferred-array selection by JSON pointer. Parsing checks the **entire**
input for UTF-8, syntax and the 64-level nesting limit before any schema decoder
runs. Selected arrays keep a view of their source and record count, rather than
allocating every child object. `visit_array` then presents one temporary owned
element at a time, retaining original byte/line locations.

Input must remain alive and immutable until the last deferred visit. Parsed
documents have no shared mutable traversal state. Callbacks run synchronously;
exceptions propagate unchanged. Default parsing remains fully owned. Object
member order and duplicate keys remain available to schema-specific validators.

Core opts in for `Galaxy.Systems` and `Galaxy.PlanetaryBodies` in Galaxy16 and
Player17 saves, including those inside developer envelopes. The native game's
asynchronous startup and backup-recovery paths automatically use these loaders.
Developer loads reuse the parsed campaign subtree, eliminating a whole-document
serialization/reparse. Intermediate galaxy DTO memory is released before
research finalization. Game state and commands remain authoritative in Core.

There is no save migration or schema change. Developer validation errors now
refer to bytes in the original developer file, improving corrupt-save diagnosis.

## Measurement

`stellar_campaign_load_benchmark <save> <research-root>` loads through the real
recovery service, activates the restored campaign, captures it, and streams its
serialization through a byte counter and FNV-1a digest. Peak working set was
sampled using Windows `GetProcessMemoryInfo` in separate processes before and
after the change. The timer covers recovery/restore, while peak memory covers
the whole process including verification. Both runs used the same existing
50,000-system, 353,781-body save from the 0.1.13 release validation.

| Check | Previous loader | Deferred arrays |
| --- | ---: | ---: |
| Peak working set | 2,545,926,144 bytes | 821,202,944 bytes |
| Restore time | 6,159.58 ms | 6,151.73 ms |
| Recaptured bytes | 439,218,730 | 439,218,730 |
| FNV-1a | 15530417222981827440 | 15530417222981827440 |

Peak memory decreased **67.7%**. Load time was effectively unchanged in this
single local comparison; no general speed or FPS claim is made. Raw measurements
are in `work/load-memory-baseline.json` and `work/load-memory-deferred.json`.

## Validation

- Engine tests compare materialized/deferred values and source positions,
  validate malformed input before traversal, enforce depth and Unicode rules,
  preserve duplicate keys and callback exceptions, and traverse 10,000 records.
- Galaxy/Player JSON oracle replay and recovery tests cover schema validation,
  error precedence, BOM/invalid UTF-8, corrupt/missing primary saves, backups,
  unchanged input files and callback failure identity.
- Developer fixed-simulation tests retain pending strategic/tactical ticks,
  provenance and resume behavior; a new invalid-system case checks the original
  developer file's byte offset.
- **211/211 native tests passed** in 260.23 seconds after rebuilding all linked
  consumers. The final developer regression also passed independently. Logs:
  `work/deferred-load-all-build.log`, `work/deferred-load-all-tests.log` and
  `work/deferred-developer-final-tests.log`. The complete suite includes 25k/50k
  generation/navigation/save tests, native game controllers and GPU checks.
- A 50,000-system developer envelope also loaded/activated/recaptured with
  821,288,960 bytes peak working set, 6,404.47 ms restoration, and 465,908,254
  serialized developer bytes. This is a successful large developer load, not a
  before/after developer speed comparison. Evidence:
  `work/load-memory-developer-deferred.json`.
- The actual rebuilt native client loaded a copy of the 439 MB player save,
  saved successfully and navigated overview → regional → system views in an
  18.44-second automated run. It retained all 50,000 overview markers, unchanged
  paused campaign day, overview galaxy artwork and regional fixed starfield,
  with zero disclosed unknown-system labels or reported label/HUD overlaps.
  The regional capture was visually inspected: nebulae draw above the full-view
  star background. Evidence and screenshots:
  `work/deferred-load-runtime/result.json`, `reload-navigation.log`,
  `galaxy.bmp`, `galaxy-regional.bmp` and `galaxy-system.bmp` in that directory.
  The existing alpha release files were not modified by this test.

## ENGINE LIMITATIONS REMAINING

The input file, final campaign state and DTO restoration still occupy memory.
Selected arrays are validated then decoded in a second scan; this is not a
fully streaming file reader. Other arrays and individual large records can still
be expensive. Saves remain uncompressed, and snapshot capture can still hitch.
The supported campaign maximum remains 50,000 systems. Full physical contacts,
ground terrain dynamics and broader migration gates are unchanged.

Future consumers can reuse this parser for ordered catalogs and replay data;
they must keep input ownership explicit and define their own schema validation.
