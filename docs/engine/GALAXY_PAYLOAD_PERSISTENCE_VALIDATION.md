# Gate 087 validation

This draft composes the maintained current-format galaxy adapters into one owned
Galaxy16 capture/restore boundary. It supports format 16 only. Historical
formats, Player17/Developer envelopes, JSON parsing, DateTimeOffset parsing,
research/diplomacy snapshots and tactical stepping remain separate work.

The retained actual-source fixture has 14 rows. It covers a field-complete
current load and real source capture, reverse-authored collection order, null
and empty surface precedence, null collection exception categories,
generation-metadata core fallback, unknown player home fallback, and the
no-civilization compatibility overload that regenerates a canonical body
catalog for homeworld planning. One failed capture proves that fleet combat is
normalized before a later invalid economy priority throws. Every row retains
its complete serialized input, input before/after, complete output or exact
source error, and an ordered six-file source inventory. The fixture is
byte-repeatable under invariant culture. Its SHA-256 is
`8217317A7AA1FE8873FAF76B4FAF2FD04E1A6BB61251D647A168E69EE33C056B`.

The native replay invokes all 14 source rows. It decodes the actual retained
payload into owned DTOs and compares every nested field before and after the
operation, including metadata, leaders, transit and cargo, paid queues, surface
state, surveys, intelligence, and the active encounter. Projection and
assertions remain outside the production catch. It also proves payload and
restored-world copy isolation, exact failed-capture partial mutation, and a
successful native nonbattle integration smoke through the campaign coordinator with
all restored civilizations initialized in Adaptive Research. The encounter is
removed before that continuation: routing a live encounter before strategic
advancement remains required lifecycle work.

Twelve supplemental native branch rows retain the independently generated
250-system baseline for branch diagnostics, plus the provenance and
unrepresented geometric-core boundaries. They do not substitute for the exact
20-system source replay. Nullable record elements that established typed DTOs
cannot represent remain parser boundaries and are not counted as native source
rows.

Run the source oracle from the repository root:

```
dotnet run --project ../campaign-galaxy-payload-oracle-087/CampaignGalaxyPayloadOracle.csproj -- ../campaign-galaxy-payload-oracle-087/fixture.json src/Game
```

Run strict native replay in both configurations:

```
python work/087-galaxy-payload-persistence/build_strict.py Debug
python work/087-galaxy-payload-persistence/build_strict.py Release
```

Both `/W4 /WX` configurations pass 14/14 exact rows and 12 supplemental rows.
Logs are retained under `work/087-galaxy-payload-persistence/build/debug` and
`build/release`. Missing arguments, fixture, source root, and astronomy catalog
all exit 1 with type, message, working directory, and resolved input paths. The
managed generator also exits 1 cleanly for missing arguments and source root.

## Engine 0.1.39 maintained validation

`galaxy_payload_persistence_parity` passed in the combined 97/97 native suite; 29/29 packaging checks passed (`work/native-039-testing.log`). The maintained generator is `tests/Stellar.GalaxyPayloadPersistence.ParityGenerator`. Its Program.cs is byte-identical to the reviewed oracle; only the project reference and native helper includes were relocated.

- Retained fixture SHA-256: `8217317A7AA1FE8873FAF76B4FAF2FD04E1A6BB61251D647A168E69EE33C056B`
- Maintained Program.cs SHA-256: `D6CC68AA588483D5A9C9E543E71D8C5FB7D6AB23EF75CD8EEAEE8EED29FC1674`
