# Gate 067 validation

The retained fixture contains 33 actual-source rows: twenty-one construction
queries from `ConstructionCapabilities.cs` and twelve shipbuilding queries from
`ShipbuildingCapabilities.cs`. Its SHA-256 is
`A2E6168D6D940D172891FE9840984854DB0F761630C55CA6BA9018D54315C082`.
The retained generator SHA-256 is
`0F0E55772E01FE071C8F8E196F9D859D10FCAA0F150698CA9997D8CAFC07A9AD`.
The canonical research fingerprint before and after every source and native
query is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

Both strict configurations passed all 33 rows with MSVC
`/W4 /WX /permissive- /fp:precise`:

```powershell
python work/067-research-capability-adapters/build_strict.py debug
python work/067-research-capability-adapters/build_strict.py release
```

Object and program-database outputs are contained below `build-debug` and
`build-release` through explicit `/Fo` and `/Fd` arguments. The native replay
has no Windows cryptography dependency; it uses the maintained portable
Adaptive Research SHA-256 helper for input fingerprints.

The rows cover every special construction and shipbuilding mapping, exact
ordinal case behavior, empty and custom IDs, civilization-scoped versus
context-scoped capability flags, Mature and both qualifying Archived
resolutions, and exact missing-civilization type/message behavior. Each of the
five construction IDs that requires established knowledge has a paired row
showing that a same-named capability flag alone does not satisfy it. The
spacecraft query separately proves its three accepted routes, and the source
queries accept a null galaxy because these authoritative adapters never inspect
legacy technology state. Source and native state projections are unchanged by
every query.

The retained managed generator exits 1 with a full exception and stack,
current directory, research root, and fixture path for missing arguments and a
missing research root. The `dotnet run --no-build` outputs are retained in
`managed-missing-args.log` and `managed-wrong-root.log`.


Maintained integration is engine0.1.30. The exported host remains the legacy
campaign; these focused gates do not establish full Adaptive Research campaign
advance, diplomacy composition, player save compatibility or graphical parity.

Maintained engine0.1.30 validation passed75/75 CTest and20/20 Python checks (`work/native-030-testing-embedded.log`). Exact committed package verification follows.
