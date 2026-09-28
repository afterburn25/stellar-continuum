using System.Text.Json;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Combat;

namespace Game.CoreRuntime.Validation;

internal static class PreparedCampaignSaveValidation
{
    public static void Run()
    {
        ValidateDetachedPlayerSnapshotAndOrderedDrain();
        ValidateDetachedActiveCombatSnapshot();
        ValidateAtomicFailurePreservesExistingPair();
        ValidateDetachedDeveloperProvenance();
    }

    private static void ValidateDetachedActiveCombatSnapshot()
    {
        WithDirectory(directory =>
        {
            var blockingWriter = new BlockingWriter();
            var persistence = new CampaignStatePersistenceService(saveWriter: blockingWriter);
            var campaign = new CampaignSessionService().CreateNew(20260909);
            var galaxy = campaign.Galaxy;
            galaxy.Fleets.Clear();
            var system = galaxy.Systems[0];
            var civilizations = galaxy.Civilizations.Take(2).ToArray();
            var profile = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1);
            for (var index = 0; index < 2; index++)
            {
                var id = index + 1;
                var loadout = MassiveCombatLoadouts.FromLegacy(profile);
                loadout.Weapons[0].Kind = MassiveWeaponKind.Missile;
                loadout.Weapons[0].Range = 1_200;
                loadout.Weapons[0].ShotsPerSecond = 10;
                galaxy.Fleets.Add(new FleetState
                {
                    Id = id, CivilizationId = civilizations[index].Id, Name = $"Snapshot vessel {id}",
                    Role = FleetRole.Military, Position = system.Position, CurrentSystemId = system.Id,
                    Combat = CombatProfileRegistry.CreateInitialState(profile.Id, FleetRole.Military),
                    TacticalLoadout = loadout,
                    TacticalVessel = new MassiveVesselState
                    { Id = id, Name = $"Snapshot vessel {id}", DesignId = profile.Id, IsFlagship = true },
                });
            }

            var bridge = new CampaignMassiveCombat(new DelegateCombatHostilityView((first, second) => first != second));
            Require(bridge.Begin(galaxy, civilizations[0].Id, 1, 17).Accepted,
                "real combat bridge did not create the prepared-save encounter");
            bridge.Advance(galaxy, .1);
            var encounter = galaxy.ActiveCombatEncounter ??
                throw new InvalidOperationException("real combat bridge omitted its active encounter");
            Require(encounter.Battle.Events.Count > 0 && encounter.Battle.ActiveSalvos.Count > 0,
                "prepared-save encounter lacked events or an in-flight salvo");
            var expected = JsonSerializer.Serialize(encounter);
            var capturedTick = encounter.Battle.Tick;
            var capturedDurability = encounter.Battle.Formations.Sum(formation =>
                formation.ShieldPool + formation.ArmorPool + formation.HullPool);
            var prepared = persistence.PrepareSave(galaxy, 17, campaign.Diplomacy, campaign.AdaptiveResearch);
            var path = Path.Combine(directory, "active-combat.json");
            // These mutations happen before JSON serialization begins. The regression must fail
            // if the prepared graph retained any live encounter, battle, formation, or salvo reference.
            bridge.Advance(galaxy, .2);
            encounter.Battle.Formations[0].Order = MassiveCombatOrderType.Surrender;
            encounter.Battle.Formations[0].HullPool = 1;
            encounter.Battle.ActiveSalvos.Clear();
            var write = Task.Run(() => persistence.WritePrepared(path, prepared));
            Require(blockingWriter.Entered.Wait(TimeSpan.FromSeconds(10)),
                "active-combat prepared write never reached the blocked writer");

            encounter.EngagedFormationPairs.Clear();
            blockingWriter.Release.Set();
            write.GetAwaiter().GetResult();

            var loaded = persistence.Load(path).Galaxy.ActiveCombatEncounter ??
                throw new InvalidOperationException("prepared active encounter was not restored");
            var restoredDurability = loaded.Battle.Formations.Sum(formation =>
                formation.ShieldPool + formation.ArmorPool + formation.HullPool);
            Require(loaded.Battle.Tick == capturedTick && loaded.Battle.ActiveSalvos.Count > 0 &&
                    Math.Abs(restoredDurability - capturedDurability) < .001f &&
                    JsonSerializer.Serialize(loaded) == expected,
                "worker write followed mutable battle tick, durability, orders, salvos, events, or encounter arrays");
        });
    }

    private static void ValidateDetachedPlayerSnapshotAndOrderedDrain()
    {
        WithDirectory(directory =>
        {
            var blockingWriter = new BlockingWriter();
            var persistence = new CampaignStatePersistenceService(saveWriter: blockingWriter);
            var session = new CampaignSessionService(saveService: persistence);
            var campaign = session.CreateNew(20260908);
            var playerId = campaign.Galaxy.PlayerCivilizationId;
            var economy = campaign.Galaxy.Economies.Single(value => value.CivilizationId == playerId);
            var homeSystem = campaign.Galaxy.Systems.Single(value =>
                value.Id == campaign.Galaxy.Civilizations.Single(item => item.Id == playerId).HomeSystemId);
            var fleet = new FleetState
            {
                Id = 900001,
                CivilizationId = playerId,
                Name = "Prepared Save Scout",
                Role = FleetRole.Scout,
                Position = homeSystem.Position,
                CurrentSystemId = homeSystem.Id,
                TacticalLoadout = new MassiveCombatLoadout
                {
                    Weapons = new() { new MassiveWeaponGroup { Id = "prepared-beam", DamagePerShot = 11 } },
                },
                TacticalVessel = new MassiveVesselState
                {
                    Id = 900001, Name = "Prepared Save Scout", DesignId = "prepared-scout",
                },
            };
            campaign.Galaxy.Fleets.Add(fleet);
            var research = AdaptiveResearchCampaignCommands.StartDirectedResearch(
                campaign.Galaxy, campaign.AdaptiveResearch, playerId, "fusion_power", 4);
            Require(research.Accepted, "could not establish active research for prepared-save validation");

            var expectedCredits = economy.Credits;
            var expectedHold = fleet.HoldRequested;
            var expectedDiplomacy = JsonSerializer.Serialize(campaign.Diplomacy.Snapshot());
            var captureStarted = System.Diagnostics.Stopwatch.GetTimestamp();
            var prepared = persistence.PrepareSave(
                campaign.Galaxy, 41.25, campaign.Diplomacy, campaign.AdaptiveResearch);
            var captureMilliseconds = System.Diagnostics.Stopwatch.GetElapsedTime(captureStarted).TotalMilliseconds;
            Console.WriteLine($"PREPARED_SAVE_CAPTURE_MS={captureMilliseconds:0.00}");
            Console.WriteLine($"PREPARED_SAVE_PHASE_MS diplomacy={prepared.CaptureMetrics.DiplomacyMilliseconds:0.00} " +
                $"galaxyValidation={prepared.CaptureMetrics.GalaxyValidationMilliseconds:0.00} " +
                $"galaxyDto={prepared.CaptureMetrics.GalaxyDtoMilliseconds:0.00} " +
                $"adaptive={prepared.CaptureMetrics.AdaptiveResearchMilliseconds:0.00}");

            economy.Credits += 500;
            fleet.HoldRequested = !expectedHold;
            fleet.TacticalLoadout.Weapons[0].DamagePerShot = 99;
            fleet.TacticalVessel.Name = "Mutated after capture";
            Require(AdaptiveResearchCampaignCommands.PauseDirectedResearch(
                campaign.AdaptiveResearch, playerId, "fusion_power").Accepted,
                "could not mutate research after capture");
            AddContact(campaign, playerId);

            var path = Path.Combine(directory, "campaign.json");
            var write = Task.Run(() => persistence.WritePrepared(path, prepared));
            Require(blockingWriter.Entered.Wait(TimeSpan.FromSeconds(10)), "prepared write never reached the blocked writer");
            economy.Credits += 500;
            fleet.TransitProgress = 0.75;
            fleet.TacticalLoadout.Modules.Add(new MassiveModuleState { Id = "late-module" });
            blockingWriter.Release.Set();
            write.GetAwaiter().GetResult();

            var loaded = persistence.Load(path);
            var loadedEconomy = loaded.Galaxy.Economies.Single(value => value.CivilizationId == playerId);
            var loadedFleet = loaded.Galaxy.Fleets.Single(value => value.Id == fleet.Id);
            var loadedResearch = loaded.AdaptiveResearch.GetCivilization(playerId).ActiveProjects["fusion_power"];
            Require(loaded.SimulationDays == 41.25 && loadedEconomy.Credits == expectedCredits &&
                    loadedFleet.HoldRequested == expectedHold && loadedFleet.TransitProgress == 0 &&
                    loadedFleet.TacticalLoadout?.Weapons[0].DamagePerShot == 11 &&
                    loadedFleet.TacticalLoadout.Modules.Count == 0 &&
                    loadedFleet.TacticalVessel?.Name == "Prepared Save Scout" &&
                    !loadedResearch.Paused && JsonSerializer.Serialize(loaded.Diplomacy.Snapshot()) == expectedDiplomacy,
                "worker write traversed live economy, fleet, research, or diplomacy state after capture");

            // A transition drains the older write before committing its newer checkpoint.
            var newer = persistence.PrepareSave(
                campaign.Galaxy, 52.5, campaign.Diplomacy, campaign.AdaptiveResearch);
            Console.WriteLine($"PREPARED_SAVE_WARM_PHASE_MS total={newer.CaptureMetrics.TotalMilliseconds:0.00} " +
                $"diplomacy={newer.CaptureMetrics.DiplomacyMilliseconds:0.00} " +
                $"galaxyValidation={newer.CaptureMetrics.GalaxyValidationMilliseconds:0.00} " +
                $"galaxyDto={newer.CaptureMetrics.GalaxyDtoMilliseconds:0.00} " +
                $"adaptive={newer.CaptureMetrics.AdaptiveResearchMilliseconds:0.00}");
            persistence.WritePrepared(path, newer);
            Require(persistence.Load(path).SimulationDays == 52.5,
                "a drained older prepared save overwrote the newer checkpoint");
        });
    }

    private static void ValidateAtomicFailurePreservesExistingPair()
    {
        WithDirectory(directory =>
        {
            var path = Path.Combine(directory, "campaign.json");
            var backupPath = path + ".bak";
            File.WriteAllText(path, "known-primary");
            File.WriteAllText(backupPath, "known-backup");
            var primary = File.ReadAllBytes(path);
            var backup = File.ReadAllBytes(backupPath);

            var campaign = new CampaignSessionService().CreateNew(77);
            var persistence = new CampaignStatePersistenceService(saveWriter: new ThrowingWriter());
            var prepared = persistence.PrepareSave(
                campaign.Galaxy, 10, campaign.Diplomacy, campaign.AdaptiveResearch);
            Reject(() => persistence.WritePrepared(path, prepared), "injected atomic write failure was hidden");
            Require(primary.SequenceEqual(File.ReadAllBytes(path)) && backup.SequenceEqual(File.ReadAllBytes(backupPath)),
                "failed prepared write modified the known primary or backup");
        });
    }

    private static void ValidateDetachedDeveloperProvenance()
    {
        WithDirectory(directory =>
        {
            var campaign = new DeveloperCampaignSessionService().CreateNew(99);
            var path = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
            var persistence = new DeveloperCampaignPersistenceService();
            var playerId = campaign.Galaxy.PlayerCivilizationId;
            var economy = campaign.Galaxy.Economies.Single(value => value.CivilizationId == playerId);
            var expectedCredits = economy.Credits;
            var prepared = persistence.PrepareSave(
                path, campaign.Galaxy, 12, campaign.Diplomacy, campaign.AdaptiveResearch);

            RejectInvalidOperation(() => new CampaignStatePersistenceService().WritePrepared(path, prepared),
                "Player writer accepted a Developer prepared envelope");
            RejectInvalidOperation(() => persistence.WritePrepared(" ", prepared),
                "Developer prepared writer bypassed its path contract");

            campaign.Galaxy.DeveloperSession = new DeveloperSessionState(ToolsUsed: true);
            economy.Credits += 900;
            persistence.WritePrepared(path, prepared);
            var loaded = persistence.Load(path);
            Require(loaded.Galaxy.DeveloperSession is { ToolsUsed: false } &&
                    loaded.Galaxy.Economies.Single(value => value.CivilizationId == playerId).Credits == expectedCredits,
                "Developer prepared save followed mutable provenance or campaign state");

            var player = new CampaignSessionService().CreateNew(100);
            var playerPrepared = new CampaignStatePersistenceService().PrepareSave(
                player.Galaxy, 1, player.Diplomacy, player.AdaptiveResearch);
            RejectInvalidOperation(() => persistence.WritePrepared(path, playerPrepared),
                "Developer writer accepted a Player prepared envelope");
        });
    }

    private static void AddContact(CampaignBootstrapResult campaign, int observer)
    {
        var target = campaign.Galaxy.Civilizations.First(value => value.Id != observer);
        new DiplomacySimulation(campaign.Diplomacy).ProcessContactOpportunity(new FirstContactOpportunity(
            observer, "prepared-save-contact", target.Id, 1, target.HomeSystemId,
            ContactAwareness.CommunicationAvailable, ContactCondition.Active,
            CommunicationAvailable: true, Confidence: 1));
    }

    private static void WithDirectory(Action<string> action)
    {
        var directory = Path.Combine(Path.GetTempPath(), $"stellar-prepared-save-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try { action(directory); }
        finally { Directory.Delete(directory, recursive: true); }
    }

    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (IOException) { return; }
        throw new InvalidOperationException(message);
    }

    private static void RejectInvalidOperation(Action action, string message)
    {
        try { action(); }
        catch (InvalidOperationException) { return; }
        catch (ArgumentException) { return; }
        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private sealed class BlockingWriter : ICampaignSaveWriter
    {
        public ManualResetEventSlim Entered { get; } = new(false);
        public ManualResetEventSlim Release { get; } = new(false);

        public void WriteAtomically(string path, string json, bool preserveExistingBackup)
        {
            Entered.Set();
            if (!Release.Wait(TimeSpan.FromSeconds(10)))
                throw new IOException("Timed out waiting to release the blocked save writer.");
            File.WriteAllText(path, json);
        }
    }

    private sealed class ThrowingWriter : ICampaignSaveWriter
    {
        public void WriteAtomically(string path, string json, bool preserveExistingBackup) =>
            throw new IOException("Injected atomic write failure.");
    }
}
