using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Construction;
using Game.Simulation.Research.Adaptive;

namespace Game.CoreRuntime.Validation;

internal static class AdaptiveResearchCampaignPersistenceValidation
{
    public static void Run()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"stellar-adaptive-campaign-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var persistence = new CampaignStatePersistenceService();
            var sessions = new CampaignSessionService(saveService: persistence);
            var campaign = sessions.CreateNew(20260908);
            var playerId = campaign.Galaxy.PlayerCivilizationId;
            var playerResearch = campaign.AdaptiveResearch.GetCivilization(playerId);
            var started = AdaptiveResearchCampaignCommands.StartDirectedResearch(
                campaign.Galaxy, campaign.AdaptiveResearch, playerId, "fusion_power", 4);
            Require(started.Accepted, $"could not establish persistence fixture: {started.Message}");
            var playerEconomy = campaign.Galaxy.Economies.Single(value => value.CivilizationId == playerId);
            playerEconomy.LastResearchSpendingPerDay = 0.75;
            playerEconomy.LastResearchFundingFraction = 0.625;
            playerEconomy.OperatingArrears = 2.5;
            playerEconomy.LastBaseOperationsFundingFraction = 0.4;
            var home = campaign.Galaxy.Colonies.First(value => value.CivilizationId == playerId);
            Require(SurfaceConstruction.Place(campaign.Galaxy, playerId, home.Id,
                    "science_lab", 120, 80, 0).Accepted,
                "could not establish a v12 surface payload for v13 migration coverage");

            var path = Path.Combine(directory, "campaign.json");
            sessions.Save(path, campaign.Galaxy, campaign.Diplomacy, campaign.AdaptiveResearch, 91.25);
            var root = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
            Require(root["FormatVersion"]!.GetValue<int>() == CampaignStatePersistenceService.CurrentFormatVersion &&
                    root["AdaptiveResearch"] is JsonObject,
                "current campaign did not write the v15 Adaptive Research payload");

            var loaded = persistence.Load(path);
            var restored = loaded.AdaptiveResearch.GetCivilization(playerId);
            Require(loaded.SimulationDays == 91.25 && restored.ActiveProjects.ContainsKey("fusion_power") &&
                    Math.Abs(restored.ActiveProjects["fusion_power"].AssignedEffectiveLabs - 4) < 0.000001,
                "Adaptive Research project ownership or lab assignment did not survive save/load");
            Require(loaded.AdaptiveResearch.GetProjectFunding(playerId).TryGetValue(
                        "fusion_power", out var restoredFunding) &&
                    restoredFunding.ReservedMilestoneCredits > 0.0 &&
                    restoredFunding.ConsumedMilestoneCredits == 0.0 &&
                    restoredFunding.AuthorizationCredits > 0.0,
                "research milestone reserve did not survive save/load");
            var restoredEconomy = loaded.Galaxy.Economies.Single(value => value.CivilizationId == playerId);
            Require(Math.Abs(restoredEconomy.LastResearchSpendingPerDay - 0.75) < 0.000001 &&
                    Math.Abs(restoredEconomy.LastResearchFundingFraction - 0.625) < 0.000001 &&
                    Math.Abs(restoredEconomy.OperatingArrears - 2.5) < 0.000001 &&
                    Math.Abs(restoredEconomy.LastBaseOperationsFundingFraction - 0.4) < 0.000001,
                "research funding or base operating arrears did not survive save/load");

            var invalidFundingPath = Path.Combine(directory, "invalid-funding.json");
            var invalidFundingRoot = (JsonObject)root.DeepClone();
            invalidFundingRoot["Galaxy"]!["Economies"]![0]!["LastResearchFundingFraction"] = 1.25;
            File.WriteAllText(invalidFundingPath, invalidFundingRoot.ToJsonString());
            Reject(() => persistence.Load(invalidFundingPath),
                "campaign load accepted an impossible research funding fraction");

            var invalidArrearsPath = Path.Combine(directory, "invalid-arrears.json");
            var invalidArrearsRoot = (JsonObject)root.DeepClone();
            invalidArrearsRoot["Galaxy"]!["Economies"]![0]!["OperatingArrears"] = -1.0;
            File.WriteAllText(invalidArrearsPath, invalidArrearsRoot.ToJsonString());
            Reject(() => persistence.Load(invalidArrearsPath),
                "campaign load accepted negative operating arrears");

            var invalidMilestonePath = Path.Combine(directory, "invalid-milestone.json");
            var invalidMilestoneRoot = (JsonObject)root.DeepClone();
            var adaptiveCivilizations = invalidMilestoneRoot["AdaptiveResearch"]!["Civilizations"]!.AsArray();
            var playerAdaptive = adaptiveCivilizations
                .Select(value => value!.AsObject())
                .Single(value => value["CivilizationId"]!.GetValue<int>() == playerId);
            var milestoneFunding = playerAdaptive["ProjectFunding"]!.AsArray()[0]!.AsObject();
            milestoneFunding["ConsumedMilestoneCredits"] =
                milestoneFunding["ReservedMilestoneCredits"]!.GetValue<double>() + 1.0;
            File.WriteAllText(invalidMilestonePath, invalidMilestoneRoot.ToJsonString());
            Reject(() => persistence.Load(invalidMilestonePath),
                "campaign load accepted milestone consumption beyond its reserve");

            var invalidAuthorizationPath = Path.Combine(directory, "invalid-research-authorization.json");
            var invalidAuthorizationRoot = (JsonObject)root.DeepClone();
            var invalidAuthorizationCivilizations =
                invalidAuthorizationRoot["AdaptiveResearch"]!["Civilizations"]!.AsArray();
            var invalidAuthorizationPlayer = invalidAuthorizationCivilizations
                .Select(value => value!.AsObject())
                .Single(value => value["CivilizationId"]!.GetValue<int>() == playerId);
            invalidAuthorizationPlayer["ProjectFunding"]!.AsArray()[0]!["AuthorizationCredits"] = -1.0;
            File.WriteAllText(invalidAuthorizationPath, invalidAuthorizationRoot.ToJsonString());
            Reject(() => persistence.Load(invalidAuthorizationPath),
                "campaign load accepted negative research authorization spending");

            var legacyResearchPath = Path.Combine(directory, "adaptive-schema-1.json");
            var legacyResearchRoot = (JsonObject)root.DeepClone();
            legacyResearchRoot["AdaptiveResearch"]!["SchemaVersion"] = 1;
            foreach (var entry in legacyResearchRoot["AdaptiveResearch"]!["Civilizations"]!.AsArray())
                entry!.AsObject().Remove("ProjectFunding");
            File.WriteAllText(legacyResearchPath, legacyResearchRoot.ToJsonString());
            var legacyResearch = persistence.Load(legacyResearchPath);
            Require(legacyResearch.AdaptiveResearch.GetCivilization(playerId).ActiveProjects.ContainsKey("fusion_power") &&
                    legacyResearch.AdaptiveResearch.GetProjectFunding(playerId).Count == 0,
                "Adaptive Research campaign schema 1 did not migrate without inventing retroactive milestone charges");

            var migratedPath = Path.Combine(directory, "v13.json");
            var migratedRoot = (JsonObject)root.DeepClone();
            migratedRoot["FormatVersion"] = CampaignStatePersistenceService.SurfaceFormatVersion;
            migratedRoot.Remove("AdaptiveResearch");
            File.WriteAllText(migratedPath, migratedRoot.ToJsonString());
            var migrated = persistence.Load(migratedPath);
            Require(migrated.AdaptiveResearch.Civilizations.Count == migrated.Galaxy.Civilizations.Count &&
                    !migrated.AdaptiveResearch.GetCivilization(playerId).ActiveProjects.ContainsKey("fusion_power"),
                "v13 migration did not compose bounded starting research without inventing active work");

            var invalidPath = Path.Combine(directory, "invalid.json");
            var invalidRoot = (JsonObject)root.DeepClone();
            var entries = invalidRoot["AdaptiveResearch"]!["Civilizations"]!.AsArray();
            entries[0]!["SpeciesId"] = "wrong_species";
            File.WriteAllText(invalidPath, invalidRoot.ToJsonString());
            Reject(() => persistence.Load(invalidPath),
                "v15 load accepted Adaptive Research state attached to the wrong species");
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (Exception) { return; }
        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
