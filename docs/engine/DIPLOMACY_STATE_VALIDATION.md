# Gate 068 validation

The retained fixture contains 35 actual-source rows from
`DiplomacySystem.cs` lines 1-386: thirteen contact-opportunity validations,
six relationship-impact validations, five low-level restores, two queries, one
complete controlled-storage sequence, seven focused contact/proposal primitive
capacity and failure rows, and one separately classified checked counter
boundary. The fixture SHA-256 is
`C798DE00C2B7953DFF82106671E0982F96AA2025D1C80EA397B62D564F65FFE5`;
the generator SHA-256 is
`E423BCFB64870C7CF34CF90A40522ED9BD201CF81F31A4759D11275481BAA9C0`.
The source-file fingerprint before and after every call is
`CF0145DE915178662CDECDA37C99627AE0BA8C4C37A6B7343BE198E09DF0DF19`.

Both strict configurations passed with MSVC
`/W4 /WX /permissive- /fp:precise`:

```powershell
python work/068-diplomacy-state/build_strict.py debug
python work/068-diplomacy-state/build_strict.py release
```

Each replay executed all 35 rows. Explicit `/Fo` and `/Fd` arguments contain
all compiler output below the draft's `build-debug` and `build-release`
directories. Native missing-argument and missing-fixture probes, plus managed
missing-argument and missing-root probes, exit 1 through their top-level
diagnostic boundaries.

The restore cases compare the complete captured state, three observer views,
directional contact queries, canonical relationship lookup, access and transit
results. They establish null-to-empty repair, skip/clamp/deduplicate behavior,
4096/128/256 collection caps, counter repair, directional visibility, explicit
claim and history audiences, and stable UTF-16 contact sorting. A forty-contact
case retains duplicate observer/contact keys with distinct values and proves
stable snapshot/view order and first-entry behavior for equal lookup ticks.
The controlled sequence covers contact insertion/update, relationship creation,
directional access, claim/response, proposal, agreement, event history and ID
counters. Focused primitive rows cover full-contact stale eviction, full-contact
failure with no eligible stale entry, backward observation, stable contact-ID
reassignment, the per-pair proposal cap, global resolved-proposal eviction and
the all-pending global failure. Each failing primitive compares the retained
post-call state with its pre-call state. State moves preserve populated contact,
relationship, claim, proposal, and history storage; a held detail relationship
reference remains stable through growth, move construction, and move assignment.
Detached snapshots and observer views remain unchanged after later live-state
mutation. The documented own-entry removal/replacement invalidation still
applies.

One row is deliberately excluded from semantic parity: source unchecked
arithmetic accepts a restored claim ID of `Int64.MaxValue` and repairs its next
counter to 1, while native rejects before returning state with
`Diplomacy claim counter overflow.` This is an explicit defined native safety
boundary, not a strict player-save validation claim.

Promoted to the maintained engine0.1.31 build. Combined maintained validation passed77/77 CTest and20/20 Python (`work/native-031-testing.log`); exact committed export follows.
