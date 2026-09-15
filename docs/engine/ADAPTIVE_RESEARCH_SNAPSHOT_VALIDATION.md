# Gate 046 Adaptive Research snapshot validation

## Scope

The codec borrows the immutable catalog, applicability
catalog and facility catalog; all three owners must outlive the codec and must not
be moved while it exists. Snapshot DTOs, JSON strings and restored states own their
content. This is schema-1 Adaptive Research payload coverage only, not campaign
save-v16, funding, expertise or sidecar compatibility.

Capture uses ordinal UTF-16 ordering to match `StringComparer.Ordinal`. Runtime
state collections reject duplicate keys before capture, so the source's stable
`OrderBy` has no observable equal-key ordering case. JSON object decoding keeps
the last duplicate value while retaining the first key position, matching the
source dictionary decoder. The canonical ASCII fixture has byte-identical JSON;
non-integral exponent spelling and invalid UTF-8 diagnostics remain serializer
platform boundaries and are compared semantically.

Malformed JSON that assigns `null` to a non-nullable source record string can
still produce a C# object; the fixture records a real null evidence provenance as
a source-only case. The native owning-string DTO rejects that input explicitly.
Null collections are reported as a distinct dereference boundary, and schema and
catalog validation retain source precedence over those later collection failures.
For mixed malformed payloads beyond the two recorded precedence cases, the native
decoder does not promise identical failure ordering. System.Text.Json materializes
the complete DTO before Restore, while the native decoder checks some member shapes
as it parses them; a wrong-typed later member or a null collection in a different
position can therefore surface in a different order.

## Validation

The retained source oracle loads the actual `AdaptiveResearchRuntime` and invokes
the actual `AdaptiveResearchSnapshotCodec`. Writer-equivalent reflection creates
Capture and Serialize inputs outside the measured calls. Restore DTOs and state
inputs are fingerprinted before and after every call; failed Restore operations
leave their input unchanged and a later retry succeeds.

The 73 replayed records cover 19 successful operations and 54 failures across Capture,
Serialize, typed Restore, and Deserialize. They include sorted capture, complete
restored state and revisions, paused lab accounting, null and empty optional
strings, duplicate list and JSON dictionary keys, string/numeric/case-insensitive
enums (including numeric and whitespace strings), checked Int32 overflow,
unknown and missing JSON members, malformed/type/null-collection JSON boundaries,
schema/catalog failure precedence over null collections, and each
ordered semantic validation branch. Typed NaN behavior is split explicitly:
pressures, assigned labs, and progress values accepted by Restore remain accepted;
total labs, evidence quality, project readiness, and JSON serialization reject it.
One additional actual source-only case records `null` in a non-nullable evidence
provenance string; the native owning-string DTO explicitly rejects this malformed
shape instead of collapsing null to empty, and the fixture consumer separately
catches and verifies that rejection. Mixed malformed payloads do not carry a
universal error-order parity claim: System.Text.Json parses the complete DTO before
Restore, while native member-shape checks can fail during decoding. Expertise is
marked as pending a separate gate.

The fixture SHA-256 is
`A7AE67FBD647E38C3E13A225F240C766309437A40F1F1A963EB4644605FBCEDD`.
Both strict MSVC configurations use `/W4 /WX /permissive- /fp:precise /utf-8`
and place `/Fo` and `/Fd` outputs under the ignored Gate 046 directory. Debug and
Release each replayed all 73 rows successfully. A reproducible invocation first
creates `work/046-research-snapshot/build-debug`, initializes the Visual Studio
x64 environment, compiles the four public dependency sources plus
`adaptive_research_snapshot.cpp` and `snapshot_tests.cpp`, and sets:

```text
/Fowork/046-research-snapshot/build-debug/
/Fdwork/046-research-snapshot/build-debug/vc140.pdb
/Fework/046-research-snapshot/build-debug/snapshot_tests.exe
```

Run the result with:

```text
work/046-research-snapshot/build-debug/snapshot_tests.exe data/research/v1 work/046-research-snapshot/research-snapshot-oracle.json
```
