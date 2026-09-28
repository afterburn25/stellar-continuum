using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Generation;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace Game.CoreRuntime.Validation;

internal static class CampaignLoadingProgressValidation
{
    [Game.Validation.RegressionCheck]
    public static void GenerationProgressDoesNotChangeDeterministicState()
    {
        var settings = new GalaxyGenerationSettings
        {
            SystemCount = 40,
            PreWarpCivilizationCount = 4,
            AncientCivilizationCount = 1,
            Radius = 460,
        };
        var updates = new List<GalaxyGenerationProgress>();
        var observed = new CampaignSessionService().CreateNew(712_041, settings, updates.Add);
        var baseline = new CampaignSessionService().CreateNew(712_041, settings);

        Require(updates.Count >= 12, "generation did not expose its completed stages");
        Require(updates.Zip(updates.Skip(1)).All(pair => pair.First.Fraction <= pair.Second.Fraction),
            "generation progress moved backward");
        Require(updates[^1].Fraction == .98, "generation reported completion before main-thread commit");
        Require(GenerationHash(observed) == GenerationHash(baseline),
            "observing generation progress changed deterministic campaign state");
    }

    [Game.Validation.RegressionCheck]
    public static void BackupRestorationProgressIsMonotonicAndReadOnly()
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-load-progress-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var sessions = new CampaignSessionService();
            var campaign = sessions.CreateNew(98_221, new GalaxyGenerationSettings
            {
                SystemCount = 40,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 460,
            });
            sessions.Save(path, campaign.Galaxy, campaign.Diplomacy, campaign.AdaptiveResearch, 10);
            sessions.Save(path, campaign.Galaxy, campaign.Diplomacy, campaign.AdaptiveResearch, 20);
            File.WriteAllText(path, "{ corrupt primary");
            var identityBefore = (campaign.Galaxy.Seed, campaign.Galaxy.Systems.Count, campaign.Galaxy.Fleets.Count);
            var updates = new List<CampaignRestorationProgress>();

            var restored = sessions.LoadExisting(path, updates.Add);

            Require(restored.RecoveredFromBackup && restored.SimulationDays == 10,
                "manual restoration did not recover the valid backup");
            Require(updates.Count >= 4 && updates.Zip(updates.Skip(1)).All(pair => pair.First.Fraction <= pair.Second.Fraction),
                "primary-to-backup restoration progress moved backward");
            Require(updates[^1].Fraction < 1, "background restoration claimed UI completion before apply");
            Require(identityBefore == (campaign.Galaxy.Seed, campaign.Galaxy.Systems.Count, campaign.Galaxy.Fleets.Count),
                "preparing a restore mutated the active campaign candidate");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static string GenerationHash(CampaignBootstrapResult campaign)
    {
        var text = new StringBuilder().Append(campaign.Seed).Append('|');
        foreach (var system in campaign.Galaxy.Systems)
            text.Append(system.Id).Append(':').Append(system.Name).Append(':')
                .Append(system.Position.X.ToString("R", CultureInfo.InvariantCulture)).Append(',')
                .Append(system.Position.Y.ToString("R", CultureInfo.InvariantCulture)).Append(';');
        foreach (var civilization in campaign.Galaxy.Civilizations)
            text.Append(civilization.Id).Append(':').Append(civilization.Name).Append(':').Append(civilization.HomeSystemId).Append(';');
        foreach (var fleet in campaign.Galaxy.Fleets)
            text.Append(fleet.Id).Append(':').Append(fleet.CivilizationId).Append(':').Append(fleet.CurrentSystemId).Append(';');
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text.ToString())));
    }
}
