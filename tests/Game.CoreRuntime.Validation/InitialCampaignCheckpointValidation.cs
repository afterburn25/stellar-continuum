using System;
using System.IO;
using System.Runtime.CompilerServices;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class InitialCampaignCheckpointValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateImmediateCheckpointAndPreviousCampaignBackup();
        Console.WriteLine("PASS: initial/new-game campaign checkpoint semantics");
    }

    private static void ValidateImmediateCheckpointAndPreviousCampaignBackup()
    {
        var root = Path.Combine(Path.GetTempPath(), $"stellar-continuum-initial-checkpoint-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);

        try
        {
            var savePath = Path.Combine(root, "autosave.json");
            var backupPath = savePath + ".bak";
            var sessions = new CampaignSessionService();
            var persistence = new CampaignStatePersistenceService();
            var settings = new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            };

            const long firstRunSeed = 0x4649_5253_5452_554EL;
            var firstRun = sessions.CreateNew(firstRunSeed, settings);
            sessions.Save(savePath, firstRun.Galaxy, firstRun.Diplomacy, simulationDays: 0.0);

            Require(File.Exists(savePath),
                "day-zero first-run checkpoint did not create the primary autosave");
            var loadedFirstRun = sessions.LoadOrCreate(savePath, fallbackSeed: 1, settings);
            Require(loadedFirstRun.Source == CampaignBootstrapSource.LoadedSave &&
                    loadedFirstRun.Seed == firstRunSeed &&
                    Math.Abs(loadedFirstRun.SimulationDays) < 0.000001,
                "day-zero first-run checkpoint did not reload the new campaign exactly");

            // Give the original campaign meaningful elapsed time before replacing it, then prove
            // an explicit New Game checkpoint makes the new campaign primary while atomic replace
            // preserves the previous campaign as the ordinary backup.
            sessions.Save(savePath, firstRun.Galaxy, firstRun.Diplomacy, simulationDays: 42.25);

            const long replacementSeed = 0x4E45_5747_414D_4501L;
            var replacement = sessions.CreateNew(replacementSeed, settings);
            sessions.Save(savePath, replacement.Galaxy, replacement.Diplomacy, simulationDays: 0.0);

            var loadedReplacement = sessions.LoadOrCreate(savePath, fallbackSeed: 2, settings);
            Require(loadedReplacement.Source == CampaignBootstrapSource.LoadedSave &&
                    loadedReplacement.Seed == replacementSeed &&
                    Math.Abs(loadedReplacement.SimulationDays) < 0.000001,
                "explicit New Game checkpoint did not become the primary day-zero campaign");

            Require(File.Exists(backupPath),
                "explicit New Game checkpoint did not preserve the replaced primary as backup");
            var previous = persistence.Load(backupPath);
            Require(previous.Galaxy.Seed == firstRunSeed &&
                    Math.Abs(previous.SimulationDays - 42.25) < 0.000001,
                "New Game checkpoint backup did not preserve the immediately previous campaign state");

            // Marking the checkpoint successful uses the same scheduler contract as any manual or
            // automatic save: the next automatic save should be one full interval later, not on
            // the next frame.
            var scheduler = new CampaignAutosaveScheduler();
            scheduler.Reset(0.0);
            scheduler.MarkSuccess(0.0);
            Require(!scheduler.IsDue(29.999) && scheduler.IsDue(30.0),
                "successful day-zero checkpoint did not establish the normal autosave cadence");
        }
        finally
        {
            if (Directory.Exists(root))
                Directory.Delete(root, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
