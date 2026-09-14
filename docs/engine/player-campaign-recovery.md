# Player campaign recovery

The native recovery boundary loads the current Player format 17 only. It tries
the requested primary file once and then the same native filesystem path with
`.bak` appended once. It never creates a campaign, writes a replacement, or
activates the returned owner. The session host must build and validate its
candidate clock and frame before replacing the live campaign.

Each attempt creates a fresh authored research runtime. A successful result owns
the restored Player17 state, source origin, requested and loaded paths, and any
prior attempt. Aggregate failure is `PlayerCampaignLoadError`; it owns the two
ordered Primary/Backup attempts with Missing/Failed classification, native path,
diagnostic text, and the original `exception_ptr` for failed work. Nonthrowing
regular-file probes give directories the same missing-file behavior as source
`File.Exists`. Backup paths stay in `std::filesystem::path`, including Unicode
Windows paths.

Restore progress is emitted where each operation starts: `.04` read, `.16`
parse, `.25` Diplomacy decode, `.42` galaxy decode, `.72` reference validation,
`.82` research restore, and `.97` final Diplomacy restoration. Fractions remain
monotonic across fallback, with a `.12` backup transition floor. The JSON codec
preserves a callback exception's original identity across decoder translation.
The enclosing recovery service separately catches that exception as the current
attempt's failure, so a primary callback failure can recover through backup just
as `CampaignSessionService.LoadExisting` does.

## Source parity evidence

The maintained generator at
`tests/Stellar.PlayerCampaignRecovery.ParityGenerator` references the real
`Game.csproj` and calls the source `CampaignSessionService.LoadExisting`. Its 17
rows retain actual Player17 primary and backup files and cover primary success,
corrupt/missing/blank primary fallback, both-file failures, Developer-envelope
rejection, primary preference, ordered progress, callback fallback, UTF-8 BOM,
malformed input, directory and Unicode paths, and whitespace rejection.

The native replay at `native-tests/player_campaign_recovery_tests.cpp` provides
the accepted `14 + 1 + 1 + 1` evidence:

- 14 rows compare source progress and recovery outcomes exactly, recapture the
  complete owned Player17 state after activation, and prove both input files
  remain byte-identical.
- One malformed-encoding row compares the same backup outcome and complete
  restored state while recording the earlier native read-stage rejection.
- One native contract case proves direct JSON progress callbacks retain their
  exception type and message.
- One source-success case records the deliberate native UTF-16 rejection.

Whitespace-path rejection is also replayed against the source row. Debug and
Release validation compiled the staged codec and every Core translation unit
with `/W4 /WX /permissive-`; changing the first expected progress fraction from
`.04` to `.05` made replay fail. The generator was run twice and its fixture and
real save evidence matched byte-for-byte.

Maintained-generator verification exposed checkout-dependent stack frames in a
callback diagnostic. The fixture now retains the actual exception type/message
chain and excludes stack-frame locations from diagnostic strings. Source
outcomes, progress and full primary/backup bytes are unchanged. Regeneration
from the maintained project is byte-identical. The generator refuses to delete
an existing evidence directory and reports top-level failures in the terminal
with a nonzero exit code.

Native input encoding is strict UTF-8 with an optional UTF-8 BOM. UTF-16LE,
UTF-16BE, legacy code pages, and malformed UTF-8 byte streams are excluded.
The source may decode those inputs before JSON parsing, so their progress prefix
is not claimed equivalent.

## Maintained paths and hashes

- `core/include/stellar/core/player_campaign_recovery.hpp` —
  `77CC0F50E3B6C91B4BC44B6FC19B2E9C3893F9E5C805AE117A6001FA5CA65BE1`
- `core/src/player_campaign_recovery.cpp` —
  `8D2F9011CED6A1B6F2B2B4D55543D66532D669088C20B79DC0E369A0C751A3EC`
- `native-tests/player_campaign_recovery_tests.cpp` —
  `93CABD1499AD0EA28499225A8A8669F41B8E09AEF7122E03B6E082436896CAB5`
- `tests/Stellar.PlayerCampaignRecovery.ParityGenerator/Program.cs` —
  `7DD40D72FA5FFF3A3A9DD301E7F6FC2DEB2F05D55B6A8944899974C6392D7FBC`
- `tests/Stellar.PlayerCampaignRecovery.ParityGenerator/Stellar.PlayerCampaignRecovery.ParityGenerator.csproj` —
  `6412753FF9489684ABE4C7E50CEA187948E3AAEC73F564C6A7E31C7BB09261EE`
- `native-tests/fixtures/player-campaign-recovery.json` —
  `0F890EC2D450D4F277A8B7E37D896807D15BC9CAE94BE18D3E2A5E7BBEB35DCF`
- `native-tests/fixtures/player-campaign-recovery/primary.json` —
  `4663F6230AFB2D4A4D802DB8BE3A3D260F5FF3D85A01AA511502F49D70B22A68`
- `native-tests/fixtures/player-campaign-recovery/backup.json` —
  `BEFAD7634474C4C30287AC688D3B3CD7180701C21EE36BDA9149F2FFCE4E4034`

The staged codec implementation promoted into
`player_campaign_json.hpp/.cpp`, `player_campaign_persistence.hpp/.cpp`, and
`detail/player_campaign_persistence_restore.hpp` was validated before promotion
as patch SHA-256
`8FB791AF55724F23C22C1EC3CE1BDBCE2FBC60CE098E123D04F3341A47006D77`.
Their promoted file hashes are, respectively,
`E55659B9651036D2E3293A38DE600C5EA2C9BD3EDDC63A5DC746D91A06A2B6E5`,
`BDEDC0CA723FAA103C5B3D87D5C98468B3BA7D9BC9C116040382D644F8C1372E`,
`33C935A0D03E5A17383EB67A7251339F1F25E81265445165F0640FBDE80780`,
`378ECECBEE7CA5D98FD994E004C57D7058426EDAB7CB422242E03E1BF959C19B`,
and `23B2935E4DCAA7D523C986E2D1D44308DDACA2DB1FA5A7E2A8E352D0389003A4`.
