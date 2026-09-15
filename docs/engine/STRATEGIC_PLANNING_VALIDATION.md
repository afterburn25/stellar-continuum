# Strategic planning validation

The retained actual C# oracle contains 75 cases: 12 knowledge freshness cases, 29 war assessments, and 34 stateful planner sequences with 54 commands. It records five source-only null-reference observations separately from the typed native boundary.

The matrix covers wrapping freshness age at extreme ticks; missing, stale, uncertain, NaN and infinite military estimates; honor and survival gates; every planner priority predicate; insertion-ordered threat and relationship ties; dictionary key versus record-ID mismatches; and observer-local unknown-strength wars. Stateful sequences cover changed inputs before review, the exact review boundary, forced review, interval clamping, invalidation, removal, clearing, checked tick overflow, retry, and survival of the prior cache after overflow.

Every returned assessment, priority, reason, generated/review tick, error, command, and read-only source input is compared. Integer fixture values are compared exactly before floating-point tolerance. A dedicated `long.MaxValue - 31` plan probes exact generated and review ticks. Source reference identity is recorded separately: a forced review at the same tick with identical inputs produces a new C# plan reference with the same value, while native plans remain owned values.

Source comparison required one production repair: invariant `P0` formatting includes a space before the percent sign. The native implementation also preserves source NaN ordering, dictionary insertion order, defined wrapping tick subtraction, and checked review addition without replacing an existing cache entry on failure.

Director composition exposed a native-only type-name collision between the existing survey `KnowledgeSnapshot` and the observer-local strategic snapshot. The strategic type is named `StrategicKnowledgeSnapshot`; the C# source type and fixture schema remain unchanged. The strategic planning test includes both public headers and instantiates both snapshot types as a compile-time composition regression.

Standalone Release and Debug consumers compile as C++23 with MSVC `/W4 /WX`; their object and PDB outputs remain under the ignored gate workspace. Both pass all 75 actual C# cases and validate the five separately observed null references.

The promoted fixture SHA-256 is `14D7FBF2571FB7A0BDB64644EB6C92E389DC0CD6EE1C72DB14C38079087F0E10`. The maintained `windows-testing` build passed 41 of 41 CTest targets, including `strategic_planning_parity`, and 19 of 19 Python recovery/package-integrity tests. Its complete log is `work/native-033-strategic-planning-testing.log` with SHA-256 `FACB499DE74EC92CB0B7B7821654CCB6E87DEC0C34991932747BC436F1EF44C0`.
