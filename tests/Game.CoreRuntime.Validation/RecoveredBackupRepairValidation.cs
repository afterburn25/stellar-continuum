using System;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class RecoveredBackupRepairValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateRecoveredBackupSurvivesPrimaryRepair();
        Console.WriteLine("PASS: recovered backup survives first primary repair save");
    }

    private static void ValidateRecoveredBackupSurvivesPrimaryRepair()
    {
        var root = Path.Combine(Path.GetTempPath(), $"stellar-continuum-backup-repair-{Guid.NewGuid():N}");
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

            const long campaignSeed = 0x5245_434F_5645_5259L;
            var campaign = sessions.CreateNew(campaignSeed, settings);

            // Create a known-good previous backup at day 12.5 and a newer primary at day 18.75.
            sessions.Save(savePath, campaign.Galaxy, campaign.Diplomacy, simulationDays: 12.5);
            sessions.Save(savePath, campaign.Galaxy, campaign.Diplomacy, simulationDays: 18.75);
            Require(File.Exists(backupPath), "test setup did not create the ordinary atomic backup");

            var knownGoodBackupBytes = File.ReadAllBytes(backupPath);
            var knownGoodBackup = persistence.Load(backupPath);
            Require(knownGoodBackup.Galaxy.Seed == campaignSeed &&
                    Math.Abs(knownGoodBackup.SimulationDays - 12.5) < 0.000001,
                "test setup backup did not contain the expected prior campaign state");

            // Damage the newest primary and prove startup correctly recovers the prior backup.
            File.WriteAllText(savePath, "{\"FormatVersion\":9,\"damaged\":");
            var recovered = sessions.LoadOrCreate(savePath, fallbackSeed: 1, settings);
            Require(recovered.Source == CampaignBootstrapSource.RecoveredFromBackup,
                "damaged primary did not recover from the known-good backup");
            Require(recovered.Seed == campaignSeed && Math.Abs(recovered.SimulationDays - 12.5) < 0.000001,
                "recovered campaign did not match the known-good backup state");

            // The first repair save must atomically replace only primary. It must not rotate the
            // known-bad destination over the backup we just recovered from.
            sessions.SavePreservingBackup(
                savePath,
                recovered.Galaxy,
                recovered.Diplomacy,
                recovered.SimulationDays);

            Require(File.Exists(savePath), "backup-preserving repair did not recreate a valid primary");
            Require(File.Exists(backupPath), "backup-preserving repair removed the known-good backup");
            Require(File.ReadAllBytes(backupPath).SequenceEqual(knownGoodBackupBytes),
                "first repair save changed the recovered backup bytes");

            var repairedPrimary = persistence.Load(savePath);
            var preservedBackup = persistence.Load(backupPath);
            Require(repairedPrimary.Galaxy.Seed == campaignSeed &&
                    Math.Abs(repairedPrimary.SimulationDays - 12.5) < 0.000001,
                "repaired primary did not persist the recovered campaign state");
            Require(preservedBackup.Galaxy.Seed == campaignSeed &&
                    Math.Abs(preservedBackup.SimulationDays - 12.5) < 0.000001,
                "known-good backup became unreadable after primary repair");

            // Once primary is healthy, ordinary saves must resume normal rotation. Advancing only
            // the persisted campaign clock is sufficient to distinguish the new primary from the backup.
            sessions.Save(savePath, recovered.Galaxy, recovered.Diplomacy, simulationDays: 13.5);
            var newestPrimary = persistence.Load(savePath);
            var rotatedBackup = persistence.Load(backupPath);
            Require(Math.Abs(newestPrimary.SimulationDays - 13.5) < 0.000001,
                "ordinary save after repair did not advance the primary campaign clock");
            Require(Math.Abs(rotatedBackup.SimulationDays - 12.5) < 0.000001,
                "ordinary save after repair did not resume normal backup rotation");
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
