# Branch recovery inventory — 2026-09-08

Snapshot baseline: `integration` at `c529a1a765776c0940f88002410bc70db740d05a`. All 96 remote branches were fetched and inspected before new development. Counts below are ancestry counts, not counts of missing features. A branch can retain unique merge/reconciliation commits after equivalent work was accepted.

Core was safely fast-forwarded from `6b50f879fd834a07ba123d3e6da056436d213dd9` (0 ahead / 285 behind). No branch was deleted or rewritten. `main` was not modified. Later candidate work is recorded in per-lead handoffs and PRs; this table is the recovery snapshot.

ACTIVE means continuing ownership, including workstreams whose previous milestone is already accepted. INTEGRATED means the tip is an ancestor of the baseline. SPECIALIST CHILD retains unique history requiring its family lead review. SUPERSEDED and HISTORICAL / ARCHIVE preserve evidence and are not automatic merge candidates. UNCERTAIN requires investigation.

| Branch | Classification | Ahead / behind | Snapshot SHA | Evidence / disposition |
|---|---|---:|---|---|
| `archive/adaptive-research-mixed-integration-20260908-1507` | HISTORICAL / ARCHIVE | 15 / 101 | `75b4cb1eff39` | Explicit archive; preserve |
| `archive/adaptive-research-mixed-latest-20260908` | HISTORICAL / ARCHIVE | 16 / 65 | `3c70f9ddfca4` | Explicit archive; preserve |
| `archive/adaptive-research-outcome-frozen-358792` | HISTORICAL / ARCHIVE | 14 / 101 | `358792a960ca` | Explicit archive; preserve |
| `dev/0.0.1-foundation` | INTEGRATED | 0 / 1186 | `c2087f1a3548` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.2-civilizations` | INTEGRATED | 0 / 1182 | `d78a9421f148` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.3-exploration` | INTEGRATED | 0 / 1179 | `1bbda5870621` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.4-colonies-economy` | INTEGRATED | 0 / 1176 | `61c825c5f26d` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.5-prewarp-dawn` | INTEGRATED | 0 / 1171 | `84952c0bf412` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.6-construction` | INTEGRATED | 0 / 1169 | `0fdebe512b16` | Tip is an ancestor of integration; preserve branch history |
| `dev/0.0.7-shipbuilding` | SUPERSEDED | 2 / 1168 | `cb553e5b22bc` | Physical shipyard persistence accepted through dev/0.0.7-shipbuilding-reconcile; retain original two commits |
| `dev/0.0.7-shipbuilding-reconcile` | INTEGRATED | 0 / 1051 | `c89b09d6e895` | Tip is an ancestor of integration; preserve branch history |
| `dev/adaptive-research` | SUPERSEDED | 17 / 65 | `c980be4526e9` | Issue #179 designates research/adaptive-research canonical |
| `dev/adaptive-research-m19-clean` | SUPERSEDED | 2 / 807 | `10041338bf47` | Issue #179 designates research/adaptive-research canonical |
| `dev/adaptive-research-m19-clean-2` | SUPERSEDED | 0 / 809 | `c5568960f9e2` | Issue #179 designates research/adaptive-research canonical |
| `docs/adaptive-research-emergence` | INTEGRATED | 0 / 1088 | `01c9618cc395` | Tip is an ancestor of integration; preserve branch history |
| `docs/adaptive-research-expansion` | INTEGRATED | 0 / 1100 | `55c902a4505f` | Tip is an ancestor of integration; preserve branch history |
| `docs/adaptive-research-maturation` | INTEGRATED | 0 / 1078 | `ae4242b67010` | Tip is an ancestor of integration; preserve branch history |
| `docs/adaptive-research-system` | INTEGRATED | 0 / 1118 | `dff937561169` | Tip is an ancestor of integration; preserve branch history |
| `docs/project-continuity` | INTEGRATED | 0 / 1160 | `9cf9a3e79ceb` | Tip is an ancestor of integration; preserve branch history |
| `docs/stellar-continuum-name` | INTEGRATED | 0 / 1152 | `05da5431958e` | Tip is an ancestor of integration; preserve branch history |
| `integration` | ACTIVE | 0 / 0 | `c529a1a76577` | Authoritative shared baseline |
| `main` | ACTIVE | 2 / 806 | `3b216497463a` | Protected release branch; no changes authorized |
| `noop` | INTEGRATED | 0 / 545 | `75ad9a9e7b64` | Tip is an ancestor of integration; preserve branch history |
| `noop-temp` | INTEGRATED | 0 / 351 | `cd995802ac1b` | Tip is an ancestor of integration; preserve branch history |
| `reconcile/adaptive-research-m18-integration` | INTEGRATED | 0 / 811 | `ff925c652b95` | Tip is an ancestor of integration; preserve branch history |
| `reconcile/adaptive-research-m19-integration` | INTEGRATED | 0 / 805 | `bc36694c3703` | Tip is an ancestor of integration; preserve branch history |
| `research/adaptive-research` | ACTIVE | 32 / 806 | `32953eae4daf` | Established continuing workstream; unique history requires review |
| `research/adaptive-research-m19` | INTEGRATED | 0 / 806 | `01fbd4c9a4a2` | Tip is an ancestor of integration; preserve branch history |
| `sync/main-competence-to-integration` | INTEGRATED | 0 / 1042 | `64a4aaa74ca5` | Tip is an ancestor of integration; preserve branch history |
| `sync/main-research-transfer-to-integration` | INTEGRATED | 0 / 1029 | `d7bdaa8ee674` | Tip is an ancestor of integration; preserve branch history |
| `work/civilization-ai` | ACTIVE | 23 / 340 | `bc87c4bc7c1d` | Established continuing workstream; unique history requires review |
| `work/civilization-ai-exploration-role-quality` | SPECIALIST CHILD | 1 / 379 | `f08fe2fcaf41` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-own-combat-readiness` | SPECIALIST CHILD | 6 / 438 | `254e6294aba1` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-own-combat-readiness-clean` | SPECIALIST CHILD | 1 / 427 | `f5a9b4a22611` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-own-combat-readiness-current` | INTEGRATED | 0 / 411 | `b4de515f36e1` | Tip is an ancestor of integration; preserve branch history |
| `work/civilization-ai-shipbuilding-preference-clean` | SPECIALIST CHILD | 6 / 412 | `673d7ffe857e` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-shipbuilding-preference-current` | INTEGRATED | 0 / 392 | `201b0c9f3bf0` | Tip is an ancestor of integration; preserve branch history |
| `work/civilization-ai-supported-exploration-work` | SPECIALIST CHILD | 2 / 379 | `f311e12f5d27` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-supported-exploration-work-current` | SPECIALIST CHILD | 1 / 379 | `f08fe2fcaf41` | Unique history; family lead must inspect before reuse or integration |
| `work/civilization-ai-v9-knowledge-clean` | INTEGRATED | 0 / 428 | `3321dec64a26` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-friendly-reservations` | INTEGRATED | 0 / 370 | `973ccc9a2896` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-legacy-order-availability` | INTEGRATED | 0 / 366 | `747c8bd6ed66` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-local-reservation-viability` | INTEGRATED | 0 / 362 | `68755db826d9` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-mission-deconfliction` | INTEGRATED | 0 / 376 | `c86a425ab779` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-opportunity-actions` | INTEGRATED | 0 / 478 | `85de5ddf9fd2` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-opportunity-planner` | INTEGRATED | 0 / 509 | `58b6f0236c7d` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-opportunity-ui` | INTEGRATED | 0 / 484 | `efcb8102b3c9` | Tip is an ancestor of integration; preserve branch history |
| `work/colonization-shared-body-resolver` | SPECIALIST CHILD | 9 / 309 | `08b846311a1c` | Unique history; family lead must inspect before reuse or integration |
| `work/combat-military` | ACTIVE | 0 / 0 | `c529a1a76577` | Established continuing workstream; prior milestone integrated |
| `work/combat-pressure-estimate-availability` | SPECIALIST CHILD | 23 / 438 | `e3d3977fc89e` | Unique history; family lead must inspect before reuse or integration |
| `work/combat-pressure-estimate-availability-clean` | INTEGRATED | 0 / 426 | `264ff276736b` | Tip is an ancestor of integration; preserve branch history |
| `work/core-adaptive-research-outcomes-m19` | SPECIALIST CHILD | 1 / 33 | `9dbcdd7665df` | Unique history; family lead must inspect before reuse or integration |
| `work/core-ai-canonical-exploration-work` | INTEGRATED | 0 / 339 | `3e78e54351bc` | Tip is an ancestor of integration; preserve branch history |
| `work/core-backup-save-recovery` | INTEGRATED | 0 / 296 | `7684599eeba9` | Tip is an ancestor of integration; preserve branch history |
| `work/core-combat-order-preview-integration` | INTEGRATED | 0 / 349 | `1667132c3a21` | Tip is an ancestor of integration; preserve branch history |
| `work/core-diplomacy-action-read-model` | SPECIALIST CHILD | 1 / 489 | `abc17f01d554` | Unique history; family lead must inspect before reuse or integration |
| `work/core-diplomacy-agreement-command-gateway` | INTEGRATED | 0 / 450 | `2eae7e261c54` | Tip is an ancestor of integration; preserve branch history |
| `work/core-diplomacy-command-gateway` | SPECIALIST CHILD | 1 / 477 | `803d431c978a` | Unique history; family lead must inspect before reuse or integration |
| `work/core-diplomacy-live-maintenance` | INTEGRATED | 0 / 516 | `0d29e55c3efb` | Tip is an ancestor of integration; preserve branch history |
| `work/core-diplomacy-relations-panel` | INTEGRATED | 0 / 407 | `480602a29111` | Tip is an ancestor of integration; preserve branch history |
| `work/core-game-integration` | ACTIVE | 0 / 0 | `c529a1a76577` | Established continuing workstream; prior milestone integrated |
| `work/core-initial-campaign-checkpoint` | INTEGRATED | 0 / 22 | `5288f5be146f` | Tip is an ancestor of integration; preserve branch history |
| `work/core-preserve-recovered-backup` | SPECIALIST CHILD | 1 / 17 | `aaf6af801d54` | Unique history; family lead must inspect before reuse or integration |
| `work/core-relations-ai-diplomacy-aggregate` | SPECIALIST CHILD | 22 / 424 | `698aa06c8fb6` | Unique history; family lead must inspect before reuse or integration |
| `work/core-save-v9-diplomacy-persistence` | INTEGRATED | 0 / 534 | `4376dda0829d` | Tip is an ancestor of integration; preserve branch history |
| `work/core-scheduled-autosave` | INTEGRATED | 0 / 352 | `6f41fa24a59d` | Tip is an ancestor of integration; preserve branch history |
| `work/diplomacy-first-contact` | ACTIVE | 1 / 22 | `166746e9a496` | Established continuing workstream; unique history requires review |
| `work/exploration-ai-deconfliction` | INTEGRATED | 0 / 560 | `78c64b8604e8` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-arrived-colony-body-view` | INTEGRATED | 0 / 347 | `18d47981200b` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-colonization` | ACTIVE | 0 / 844 | `a213cc771bf2` | Established continuing workstream; prior milestone integrated |
| `work/exploration-local-survey-contact` | INTEGRATED | 0 / 382 | `a3ebd304c963` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-mission-planning` | INTEGRATED | 0 / 785 | `53fc4422c79e` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-mission-status` | INTEGRATED | 0 / 753 | `a0346d8b0009` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-observation-confidence` | INTEGRATED | 0 / 358 | `e2b41ec923f8` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-planetary-bodies` | INTEGRATED | 0 / 844 | `a213cc771bf2` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-science-signature-events` | INTEGRATED | 0 / 421 | `7308916c76ab` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-survey-coverage` | INTEGRATED | 0 / 448 | `0debabd609fc` | Tip is an ancestor of integration; preserve branch history |
| `work/exploration-survey-operations` | INTEGRATED | 0 / 818 | `7b5c2a21f97f` | Tip is an ancestor of integration; preserve branch history |
| `work/galaxy-star-system-visuals` | ACTIVE | 1 / 0 | `f546d0f77d84` | Established continuing workstream; unique history requires review |
| `work/habitat-support-capability-foundation` | INTEGRATED | 0 / 3 | `f94bfaaab66b` | Tip is an ancestor of integration; preserve branch history |
| `work/screenshot-capture` | INTEGRATED | 0 / 274 | `90f73f27c6cd` | Tip is an ancestor of integration; preserve branch history |
| `work/solar-economy-logistics` | ACTIVE | 0 / 522 | `fa4022a35db9` | Established continuing workstream; prior milestone integrated |
| `work/species-compatible-homeworlds` | INTEGRATED | 0 / 327 | `c3b657ce87ec` | Tip is an ancestor of integration; preserve branch history |
| `work/species-demographic-pressure` | INTEGRATED | 0 / 430 | `fc5b0b21fe35` | Tip is an ancestor of integration; preserve branch history |
| `work/species-environmental-demographics` | INTEGRATED | 0 / 291 | `a93fd36e85e4` | Tip is an ancestor of integration; preserve branch history |
| `work/species-fleet-biological-load` | INTEGRATED | 0 / 391 | `99e3bacf3b91` | Tip is an ancestor of integration; preserve branch history |
| `work/species-fleet-crews` | INTEGRATED | 0 / 404 | `3db72e31f6f9` | Tip is an ancestor of integration; preserve branch history |
| `work/species-habitat-burden-aggregation` | INTEGRATED | 0 / 11 | `62ef3f4a9781` | Tip is an ancestor of integration; preserve branch history |
| `work/species-habitat-support-burden` | INTEGRATED | 0 / 38 | `c2472ef833a8` | Tip is an ancestor of integration; preserve branch history |
| `work/species-race-mechanics` | ACTIVE | 9 / 606 | `0bfeb63d123a` | Established continuing workstream; unique history requires review |
| `work/species-race-mechanics-followup` | INTEGRATED | 0 / 594 | `08049228b849` | Tip is an ancestor of integration; preserve branch history |
| `work/species-race-persistence-hardening` | INTEGRATED | 0 / 515 | `b10b5fbd1d2e` | Tip is an ancestor of integration; preserve branch history |
| `work/species-v8-latest-sync` | INTEGRATED | 0 / 596 | `3a77c040fc92` | Tip is an ancestor of integration; preserve branch history |
| `work/testing-release` | ACTIVE | 0 / 306 | `3c3ef8863392` | Established continuing workstream; prior milestone integrated |
| `work/ui-player-experience` | ACTIVE | 0 / 535 | `06a74879b3cd` | Established continuing workstream; prior milestone integrated |
| `work/visual-style-assets` | ACTIVE | 30 / 0 | `2a9124d666fc` | Established continuing workstream; unique history requires review |

## Recovered exceptions

- `research/adaptive-research` is canonical, confirmed by #179 and accepted post-M19 history. The mixed `dev/` lineage is superseded; archives remain untouched. Active research has later unique work and must be reviewed separately from M19 already in integration.
- `work/core-preserve-recovered-backup` contains a real unaccepted fix: first repair must not rotate a corrupt primary over the known-good recovered backup. Core is validating the existing child.
- `work/core-adaptive-research-outcomes-m19` includes mostly accepted outcome implementation plus a potentially useful stranded policy-check suite. Research must review the narrow unique validation slice.
- `work/colonization-shared-body-resolver` contains unique resolver work; Exploration must validate exact-body/species continuity before acceptance.
- `work/civilization-ai` has unique history but no merge-base tree delta. Its lead must distinguish retired overlays from current canonical interfaces.
- Species persistence-hardening is fully ancestral to integration despite open synchronization PR #128. Species will confirm its stale status before closure.
- PR #218 Visual Assets and PR #220 Galaxy/Maps remain unaccepted at recovery; they require current-head review and semantic Godot validation.
- Accepted build workflow at c529a1a is a false-positive runtime gate: exact-head logs contain script-instantiation errors. Testing/Release owns #61 repair; green historical exit-code checks alone do not prove playable startup.

## Shared-memory sources

Core #18; coordination #15; Shipbuilding #14; Species #16; Economy #26; UI #27; AI #28; Testing #29; Exploration #32; Diplomacy #37; Combat #40; Research #179; Visual Assets #217; Galaxy #219; astronomy data child #221. Read recent comments as well as issue descriptions, which often retain their initial milestone text.
