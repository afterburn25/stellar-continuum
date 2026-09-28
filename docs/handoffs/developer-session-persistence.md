# Developer session persistence

`GalaxyState.DeveloperSession` is a nullable `Game.Campaign.DeveloperSessionState(bool ToolsUsed)` record. Null denotes Player mode. A fresh Developer campaign uses ordinary `CampaignSessionService.CreateNew` generation and adds `ToolsUsed=false`; no resources, research, fleets or physical worlds are changed by session creation.

## Public contracts

`Game.Persistence.DeveloperCampaignPersistenceService` has a default constructor and an optional `CampaignStatePersistenceService` dependency.

```csharp
void Save(string path, GalaxyState galaxy, double simulationDays,
    DiplomacyState diplomacy, bool preserveExistingBackup = false);
LoadedCampaignState Load(string path);
```

`Game.Campaign.DeveloperCampaignSessionService` has a default constructor and optional `CampaignSessionService`, `DeveloperCampaignPersistenceService`, and legacy `CampaignStatePersistenceService` dependencies.

```csharp
const string SaveFileName = "developer-autosave.json";
CampaignBootstrapResult CreateNew(long seed);
CampaignBootstrapResult LoadOrCreate(string path, long fallbackSeed);
```

The loaded marker is `LoadedCampaignState.Galaxy.DeveloperSession`. The services provide no command that resets `ToolsUsed`; the Developer command owner sets it when an authorized tool is used.

## Format and isolation

Developer files have exactly four outer fields: `DeveloperFormatVersion: 1`, `Mode: "Developer"`, required boolean `ToolsUsed`, and object `Campaign`. `Campaign` is the canonical v9, v11, v13 or v15 payload, including Diplomacy, Adaptive Research in v15, and the existing validated galaxy/surface data. Nested session metadata is rejected. Missing, duplicate or unexpected outer fields, wrong field types, unsupported versions and contradictory modes are rejected before loading the canonical payload.

The public Player `CampaignSaveService.Save`, `CampaignStatePersistenceService.Save` and `SavePreservingBackup` reject any non-null Developer marker before file creation/replacement. Player readers explicitly reject Developer envelopes. Existing Player format versions and migrations are unchanged.

Internal `SaveDeveloperPayload` paths in the existing serializers reuse canonical validation while keeping the live Developer marker attached. No temporary metadata clearing occurs. The outer Developer serializer stages only uniquely named files that it owns, flushes the completed envelope, then atomically replaces the primary and rotates its backup. `preserveExistingBackup=true` repairs the primary without replacing the known-good backup. Cleanup is limited to owned intermediate files.

## Recovery and legacy demo import

Developer startup first loads its primary, then its `.bak` on missing/corrupt primary. If both fail, it creates the requested fallback seed and reports `RecoveredFromInvalidSave` with full failure details, matching ordinary campaign bootstrap semantics.

Only when neither Developer primary nor backup exists does startup consider the sibling `demo-autosave.json`, including its `.bak` fallback. Successful legacy import retains all campaign contents, attaches `ToolsUsed=false`, and leaves both original files untouched. It is an in-memory import: a subsequent explicit Developer save/checkpoint creates `developer-autosave.json`. A corrupt Developer file never silently falls back to an older legacy demo.

## Validation handoff

Full production source compiled against .NET 8/GodotSharp with only the four existing nullable warnings. The caught managed DLL runner passed the existing Core 18/18 suite (including Player recovery, surface persistence, canonical Sol continuity and real settlement progression), and Quality 8/8 including 20 spatial contracts. No apphost executable was launched.

The independent testing owner is adding maintained Developer envelope, mode-isolation, continuity and import/recovery tests against these public APIs. UI/session lifecycle integration remains the root owner's scope; this change does not edit Main, screenshot drivers or tests.
