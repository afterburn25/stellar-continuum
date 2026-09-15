# Gate 072 validation

Managed generation passes 46 source rows (32 observer commands, 2 action
availability builds, and 12 campaign commands). Two consecutive generations
produced byte-identical fixture SHA-256
`5DB84CE03B3765399ABD062E206F6F2D23C72984CF76FBB29D1C1601F451AA94`.
The retained generator lives outside the repository at
`../diplomacy-observer-commands-oracle-072` while this gate is under review.
Native C++ latest Debug (`/Od /RTC1`) and Release (`/O2 /DNDEBUG`) builds both
pass all 46 rows under `/W4 /WX /permissive- /fp:precise /utf-8 /Z7`, with
explicit object and PDB outputs confined to the ignored draft directories.
Both run transcripts have SHA-256
`FFC4B0D5B9C3832EC49A779142D8C701A5D3E687FE7324B5E91E1648EB34F3C5`.

The planned replay invokes only the typed source/native operation inside each
catch boundary. JSON decoding, input ownership, projection, result encoding,
and complete state comparison remain outside. Source authority is the exact
combined byte fingerprint of the five bounded command/availability files.
The current combined source fingerprint is
`42906D150482C19971CA6F67A51549A761127AB2E688F94583F68BE3B377274C`.
Missing-argument and missing-source managed launches both exit 1 through the
top-level diagnostic boundary. Text matching is scoped to valid UTF-8, which is
the repository JSON/input contract.

The native header/source/test SHA-256 values are respectively
`12FB0EE9BA7FD6A7ADF88C80E0C41509E199B539AC0A4D70F71187BD3B99F801`,
`88E2B11BA92116349403F8A450BC80F8480825D7BAEAD51A342252A2724C9F7E`,
and `31966B03909B63BB1E59711F0187AD624433E3BD9C3DC081B27A7E6BA720EF01`.
The generator SHA-256 is
`0BDB0B6CC36456B59EFE546E003258251E8E0460EAFD1E0C96267332B1B35516`.
Native missing-argument, missing-fixture, and missing-source-root launches each
exit 1 through the top-level diagnostic boundary.

Maintained CTest: `diplomacy_observer_commands_parity`. Retained actual-source generator: `tests/Stellar.DiplomacyObserverCommands.ParityGenerator`; use `dotnet run --project` with explicit root/fixture paths. Isolated draft command paths above are evidence, not shipped executables.
