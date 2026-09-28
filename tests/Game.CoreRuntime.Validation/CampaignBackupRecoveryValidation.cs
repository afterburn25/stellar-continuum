using System;
using System.IO;
using System.Runtime.CompilerServices;
using Game.Campaign;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignBackupRecoveryValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidatePrimaryPreferenceBackupRecoveryAndFinalFallback();
        Console.WriteLine("PASS: campaign primary/backup autosave recovery");
    }

    private static void ValidatePrimaryPreferenceBackupRecoveryAndFinalFallback()
    {
        var root = Path.Combine(Path.GetTempPath(), $"stellar-continuum-backup-recovery-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);

        try
        {
            var savePath = Path.Combine(root, "autosave.json");
            var backupPath = savePath + ".bak";
            var service = new CampaignSessionService();
            var settings = new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            };

            const long campaignSeed = 0x4241_434B_5550_5245L;
            const long fallbackSeed = 0x4641_4C4C_4241_434BL;
            var campaign = service.CreateNew(campaignSeed, settings);

            service.Save(savePath, campaign.Galaxy, campaign.Diplomacy, simulationDays: 12.5);
            service.Save(savePath, campaign.Galaxy, campaign.Diplomacy, simulationDays: 18.75);

            Require(File.Exists(savePath), "second campaign save did not leave a primary autosave");
            Require(File.Exists(backupPath), "atomic campaign replacement did not retain the previous autosave backup");

            var primary = service.LoadOrCreate(savePath, fallbackSeed, settings);
            Require(primary.Source == CampaignBootstrapSource.LoadedSave && primary.WasLoaded,
                "valid primary autosave was not preferred over the backup");
            Require(primary.Seed == campaignSeed && Math.Abs(primary.SimulationDays - 18.75) < 0.000001,
                "valid primary autosave did not restore the newest campaign state");
            var primaryBytes = File.ReadAllBytes(savePath);
            var backupBytes = File.ReadAllBytes(backupPath);
            var explicitlyLoaded = service.LoadExisting(savePath);
            Require(explicitlyLoaded.Source == CampaignBootstrapSource.LoadedSave &&
                    explicitlyLoaded.Seed == campaignSeed && explicitlyLoaded.SimulationDays == 18.75 &&
                    primaryBytes.SequenceEqual(File.ReadAllBytes(savePath)) &&
                    backupBytes.SequenceEqual(File.ReadAllBytes(backupPath)),
                "explicit load did not restore the primary exactly or modified the save pair");

            File.WriteAllText(savePath, "{\"FormatVersion\":9,\"broken\":");
            var corruptPrimary = File.ReadAllBytes(savePath);
            var explicitBackup = service.LoadExisting(savePath);
            Require(explicitBackup.Source == CampaignBootstrapSource.RecoveredFromBackup &&
                    explicitBackup.Seed == campaignSeed && explicitBackup.SimulationDays == 12.5 &&
                    corruptPrimary.SequenceEqual(File.ReadAllBytes(savePath)) &&
                    backupBytes.SequenceEqual(File.ReadAllBytes(backupPath)),
                "explicit load did not recover the backup without repairing or rotating either save");
            var recovered = service.LoadOrCreate(savePath, fallbackSeed, settings);
            Require(recovered.Source == CampaignBootstrapSource.RecoveredFromBackup,
                "corrupt primary autosave did not fall back to the previous valid backup");
            Require(recovered.WasLoaded && recovered.RecoveredFromBackup && !recovered.RecoveredFromInvalidSave,
                "backup recovery bootstrap flags were inconsistent");
            Require(recovered.Seed == campaignSeed && Math.Abs(recovered.SimulationDays - 12.5) < 0.000001,
                "backup recovery did not restore the prior campaign state");
            Require(recovered.LoadFailure?.Contains("Primary autosave failed", StringComparison.Ordinal) == true,
                "backup recovery did not preserve the primary-load diagnostic");

            File.Delete(savePath);
            var missingPrimary = service.LoadOrCreate(savePath, fallbackSeed, settings);
            Require(missingPrimary.Source == CampaignBootstrapSource.RecoveredFromBackup,
                "missing primary autosave did not recover the existing backup");
            Require(missingPrimary.Seed == campaignSeed && Math.Abs(missingPrimary.SimulationDays - 12.5) < 0.000001,
                "missing-primary recovery did not restore backup campaign state");
            Require(missingPrimary.LoadFailure?.Contains("Primary autosave was missing", StringComparison.Ordinal) == true,
                "missing-primary recovery did not retain a bounded diagnostic");

            File.WriteAllText(savePath, "not-json-primary");
            File.WriteAllText(backupPath, "not-json-backup");
            var brokenPrimary = File.ReadAllBytes(savePath);
            var brokenBackup = File.ReadAllBytes(backupPath);
            RejectExistingLoad(service, savePath, "invalid primary and backup were accepted by explicit load");
            Require(brokenPrimary.SequenceEqual(File.ReadAllBytes(savePath)) && brokenBackup.SequenceEqual(File.ReadAllBytes(backupPath)),
                "failed explicit load modified the invalid primary or backup");
            var fallback = service.LoadOrCreate(savePath, fallbackSeed, settings);
            Require(fallback.Source == CampaignBootstrapSource.RecoveredFromInvalidSave,
                "invalid primary and backup did not fall back to a new campaign");
            Require(!fallback.WasLoaded && !fallback.RecoveredFromBackup && fallback.RecoveredFromInvalidSave,
                "new-campaign fallback bootstrap flags were inconsistent");
            Require(fallback.Seed == fallbackSeed && Math.Abs(fallback.SimulationDays) < 0.000001,
                "double-corrupt autosave recovery did not use the requested fallback campaign seed");
            Require(fallback.LoadFailure?.Contains("Backup autosave also failed", StringComparison.Ordinal) == true,
                "double-corrupt autosave recovery omitted the backup failure diagnostic");

            File.Delete(savePath);
            File.Delete(backupPath);
            RejectExistingLoad(service, savePath, "missing primary and backup were accepted by explicit load");
        }
        finally
        {
            if (Directory.Exists(root))
                Directory.Delete(root, recursive: true);
        }
    }

    private static void RejectExistingLoad(CampaignSessionService service, string path, string message)
    {
        try { _ = service.LoadExisting(path); }
        catch (Exception error) when (error is InvalidDataException or FileNotFoundException) { return; }
        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
