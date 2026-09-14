# Gate 091 Player17 typed campaign persistence validation

Combined maintained release validation passed 100/100 CTest and 29/29 Python
(`work/native-040-combined-testing.log`); the initial harness failure is
retained in `work/native-040-testing.log`. The retained fixture SHA-256 is
`5471AD98B38E5801F598B32BE5EFE7CCCD509B8E0BA55100C33D7291CB45ACF1`, and the
maintained generator `Program.cs` SHA-256 is
`8B5F468E7A41100059E157CDFEABD06F71AEC80042B499CF7C1DDF08B8869AF1`.

This gate composes the maintained Galaxy16, Diplomacy, and Adaptive Research
typed persistence boundaries. It does not parse JSON or expose file I/O. The
17 retained actual-source rows cover successful capture/restore, a complete
nonbattle restore/activation/0.25-day integrated step, Player provenance and
time validation, direct Diplomacy structural errors and their inner diagnostic,
Diplomacy-before-Galaxy and fleet-normalization-before-economy failure order,
world-reference-before-research order, missing wrapper members, research
catalog failure, and persistence-versus-host activation behavior.

The negative persisted-day row establishes the boundary precisely: actual C#
persistence returns the typed loaded state, then host Diplomacy activation
rejects the clock with the retained `ArgumentOutOfRangeException`. The native
owner matches that error and remains valid only for destruction or move
assignment after the consuming activation call. A large finite persisted day
restores and activates successfully; Diplomacy clock conversion saturates it to
the maximum tick as the source does.

The replay compares complete Galaxy, Diplomacy, and research state. Research
enum presentation is normalized by decoding both source numeric enums and
native symbolic enums into the same owned V5 DTO before field-complete
comparison. It checks detached input ownership before/after every operation,
live capture partial mutation, metadata, wrapper presence/version fields,
stable Pimpl move and move-assignment addresses, runtime/research association,
fixture immutability, and the ordered 11-file source inventory.

Successful capture, restore, and recapture include two identified communicating
contacts, a relationship with a retained grievance, an accepted non-aggression
proposal and active agreement, and six retained Diplomacy history events. The
research campaign contains authorized active projects with nonzero funding and
stage progress. The subsequent integrated step increases that progress while
preserving every authorization/reserved/consumed funding record, proving that
activation does not replay the paid start authorization.

Regeneration from the actual Game assembly:

```powershell
dotnet run --project tests\Stellar.PlayerCampaignPersistence.ParityGenerator\Stellar.PlayerCampaignPersistence.ParityGenerator.csproj -- native-tests\fixtures\player-campaign-persistence.json src\Game data\research\v1
```

The generator was run twice with byte-identical output. The fixture contains
17 native rows, zero source-only rows, and has SHA-256
`5471AD98B38E5801F598B32BE5EFE7CCCD509B8E0BA55100C33D7291CB45ACF1`.

Strict native replay:

```powershell
cmake --build build-native\testing --config Debug --target stellar_player_campaign_persistence_tests
ctest --test-dir build-native\testing -C Debug -R player_campaign_persistence_parity --output-on-failure
```

Both `/W4 /WX` configurations pass 17/17 rows. The Release executable also
returns exit code 1 with useful exception type/message, current directory,
fixture path, source root, and research root diagnostics for missing arguments,
fixture, source root, and research root. Outputs and PDBs stay under the ignored
draft directory.

The integrated continuation uses the same plain C# composition as
`Main.CoreIntegration`: capability-aware construction and shipbuilding,
strategic AI with Diplomacy knowledge, combat runtime, sensor recording,
Adaptive Research, then Diplomacy. It is a nonbattle state/event composition
path with complete resulting persisted state comparison; phase event payloads
are not separately asserted by this gate. Live tactical encounters remain
routed to the later tactical lifecycle
and are never advanced through this strategic path.
