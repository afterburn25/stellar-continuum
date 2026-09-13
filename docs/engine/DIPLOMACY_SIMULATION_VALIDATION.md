# Gate 069 validation

The retained actual-source fixture is generated from `DiplomacySimulation` in
`src/Game/Simulation/Diplomacy/DiplomacySystem.cs` lines 387-553. Every row
retains the complete input snapshot and command arguments, input serialization
before and after the source call, source-file fingerprints before and after,
the typed return or exact authored exception, the complete resulting snapshot,
and observer views for civilizations 1, 2, and 3.

The rows cover every public command, contact event selection and confidence
formatting, validation order, directional and mutual knowledge, claim and
proposal lifecycles, every accepted proposal-effect branch, proposal partial
mutation order, grievance bounds, access effects, agreement activation and war
termination order, one-way and reciprocal event visibility, known numeric enum
behavior, and no-op branches. A permissively restored Agreement proposal with
no agreement type proves the source-authored nullable failure before proposal
mutation.

The fixture contains 57 rows. Its SHA-256 is
`542012CD5A0BDF4156FA298CEC07C9CA00C31D642305CED79AADD4E440FFAF17`;
the generator SHA-256 is
`B8372FFEC542BEA465D368A4CD110E059BC9662D79256C2D389E63E98F783864`.
The source fingerprint before and after every call is
`CF0145DE915178662CDECDA37C99627AE0BA8C4C37A6B7343BE198E09DF0DF19`.

Strict validation uses MSVC `/W4 /WX /permissive- /fp:precise` in both
configurations, with explicit `/Fo` and `/Fd` paths contained below the draft:

```powershell
python work/069-diplomacy-simulation/build_strict.py debug
python work/069-diplomacy-simulation/build_strict.py release
```

Both Debug and Release replays passed all 57 rows. The native missing-argument
and missing-fixture probes and the managed missing-argument and missing-root
probes exit 1 through top-level diagnostics that include exception details,
working directory, repository root, and fixture path.

Maintained invocation: CTest `diplomacy_simulation_parity` from the configured native build. The retained generator is `tests/Stellar.DiplomacySimulation.ParityGenerator`; regenerate with `dotnet run --project` and explicit repository root/fixture arguments. Draft compile commands above describe isolated evidence; the maintained build covers the final promoted files.
