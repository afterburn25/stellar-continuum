<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Large campaign save preparation — 2026-09-18

This source/build follow-up reduces the pause while the game prepares a detached
save snapshot. It follows the deferred-array loading update and does not replace
the previously packaged 0.1.13 alpha ZIP.

## ENGINE CAPABILITIES ADDED / EXTENDED

Engine's new `ParentChainIndex` analyzes graphs with at most one parent per node.
Construction is linear in node count, queries are constant time, and the owned
result uses one byte per node. It uses neither recursion nor a separate visited
set for every chain. Nodes leading into a cycle are flagged along with the cycle
itself. Missing parents end chains; out-of-range input indices throw. Results
remain valid after input mutation or destruction and support concurrent const
queries without shared mutable traversal state.

Core's authoritative planetary persistence validator uses this index for both
capture and restore. A single ID-to-index map detects duplicate identities and
resolves parents. Game-specific environment, physical, kind, system and parent
rules remain in Core, including their existing error precedence and messages.
Unknown identities still produce the canonical missing-parent error. Validation
does not sort or mutate the input catalog.

Capture now projects directly into its final optional DTO vector instead of
building a second full planetary catalog. Restore validates the original DTO
entries in place before creating the final owned bodies. Galaxy reference
validation also reserves its identity indexes to avoid repeated growth.

Player and developer saves, autosaves, manual saves, load/recovery and the native
game session consume these existing Core entry points automatically. Live state
is still captured on the owning thread at its existing frame boundary; only the
detached immutable snapshot goes to the writer. No schema, game rule, save-field,
atomic replacement, backup or background-write ownership change was introduced.

## Measurement

`stellar_campaign_load_benchmark <save> <research-root> --capture-profile` loads
and activates a real campaign, times seven captures, then recaptures and streams
the saved output through a byte counter and FNV-1a digest. Timing includes the
full capture and validation call, but excludes snapshot destruction and disk
writing. Before and after were separate processes on this machine with no
concurrent build/test run. Both used the same existing 50,000-system,
353,781-body campaign from the alpha validation.

| Check | Before | After |
| --- | ---: | ---: |
| Median capture | 266.185 ms | 145.603 ms |
| Seven-capture range | 253.330–278.604 ms | 142.793–149.569 ms |
| Recaptured bytes | 439,218,730 | 439,218,730 |
| FNV-1a | 15530417222981827440 | 15530417222981827440 |

Median preparation time decreased **45.3%** in this local comparison. This is
not an FPS, total save-time or arbitrary late-game workload guarantee. Evidence:
`work/capture-profile-before.log` and `work/capture-profile-after.log`.

## Validation

- `engine_parent_chain_index` compares all 8,477 possible graphs of up to five
  nodes against independent parent walks. It also covers retained ownership,
  invalid input/query indices and 250,000-node acyclic and cyclic chains.
- Planetary persistence retains its 32 golden replay cases and detached ownership
  checks. Additional capture/restore regressions cover shuffled sparse IDs,
  shared parents, descendants leading into cycles, earlier kind/physical/system
  errors, missing parents, duplicate-ID precedence and null-entry precedence.
- The focused Engine, planetary persistence, galaxy references and player save
  controller tests passed. The save controller retains its existing owner-thread,
  asynchronous writing, autosave/manual sequencing and error behavior checks.
- After rebuilding every linked consumer, **212/212 native tests passed** in
  266.76 seconds, including 25k/50k campaigns, generation fingerprints,
  navigation/save roundtrips, native controllers and GPU rendering. Logs:
  `work/capture-all-build.log` and `work/capture-all-tests.log`.
- The rebuilt native game loaded and saved a copy of the 50k player campaign,
  then navigated overview, regional and system views successfully in 20.68
  seconds. The observed save-service update frame was 153.109 ms. All 50,000
  overview markers, unchanged paused campaign day, overview galaxy artwork,
  regional star background and system-view isolation were retained. Evidence:
  `work/capture-runtime/result.json`, `reload-navigation.log` and the three
  `galaxy*.bmp` captures in that directory. The original save was not modified.
- The 50k developer campaign also restored and captured successfully. Its median
  capture was 164.368 ms; its 465,908,254 bytes and FNV-1a
  `6392741197125724647` match the prior loading-run fingerprint. No developer
  before/after timing claim is made. Log: `work/capture-developer-profile.log`.

## ENGINE LIMITATIONS REMAINING

Snapshot capture is still synchronous and measured about 146 ms at this scale;
it is reduced, not eliminated. The complete world and one detached save DTO
remain resident, and saving still emits uncompressed JSON. Load input remains a
whole in-memory file despite deferred record decoding. Campaigns remain capped
at 50,000 systems. Full rigid-body contacts, physical terrain dynamics and broader
native migration/release gates remain unfinished.

Future Engine consumers can reuse the parent index for scene hierarchies or other
single-parent dependency catalogs. Callers must map their own identities and
enforce domain-specific parent rules; this is not a general multi-edge graph
validator or an incremental cache.
