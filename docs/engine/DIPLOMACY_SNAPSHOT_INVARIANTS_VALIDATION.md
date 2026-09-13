# Gate 070 validation

The managed fixture captures typed validation result/error and complete input
before and after every source call under invariant culture. The fixture also
pins the exact `DiplomacySnapshotInvariantValidator.cs` source bytes. The native
harness decodes all DTOs and all top-level/nested presence metadata before the
operation catch, verifies its typed projection, invokes only `validate` inside
the catch, and compares exact result/error and unchanged post-call input.

Strict C++ latest Debug (`/Od /RTC1`) and Release (`/O2 /DNDEBUG`) builds both
pass under `/W4 /WX /permissive- /fp:precise /utf-8 /Z7`. Each replay accounts
for all 128 retained rows: 127 native invocations pass and the one null source
argument remains explicitly metadata-only because the native value/reference
API cannot represent a null snapshot. Compile and run transcripts are retained
under `build-debug` and `build-release`.

Managed generation currently retains 128 rows (127 native-replayable rows plus one explicitly metadata-only null-snapshot boundary). Two consecutive runs produced
identical fixture SHA-256
`E48E5645F039A0498A4BE9A58547ECCBA3CCEC1E044378122BB6E60E914D2EF6`.
The generator SHA-256 is
`3D5003CD0BCA5D42709D3788A3755A0C412AF8E6DC27F30818D9344C0B3C93F3`,
and the pinned source authority SHA-256 is
`6702DE597A6E13A01DD9C92C00911DBD094CDD49BCC15BA0C302B8743490E7F8`.
Managed missing-argument and missing-source runs both exit 1 through the outer
diagnostic boundary with the full exception, current directory, source root,
and fixture path.

Maintained invocation: CTest `diplomacy_snapshot_invariants_parity` from the configured native build. The retained generator is `tests/Stellar.DiplomacySnapshotInvariants.ParityGenerator`; regenerate with `dotnet run --project` and explicit repository root/fixture arguments. Draft compile commands above describe isolated evidence; the maintained build covers the final promoted files.
