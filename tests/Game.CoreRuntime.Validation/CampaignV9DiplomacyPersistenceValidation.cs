using System;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignV9DiplomacyPersistenceValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCampaignV9DiplomacyPersistenceChecks()
    {
        ValidateSessionRoundTripAndLegacyMigration();
        Console.WriteLine("PASS: campaign save v9 Diplomacy persistence and legacy migration");
    }

    private static void ValidateSessionRoundTripAndLegacyMigration()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-v9",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var settings = new GalaxyGenerationSettings
            {
                SystemCount = 28,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 340.0f,
            };

            var session = new CampaignSessionService();
            var fresh = session.CreateNew(0x5639_4449_504C_4F4DL, settings);
            var civilizations = fresh.Galaxy.Civilizations
                .Where(civilization => !civilization.IsSeededAncient)
                .Take(2)
                .ToArray();
            Require(civilizations.Length == 2, "validation campaign did not contain two ordinary civilizations");

            var first = civilizations[0];
            var second = civilizations[1];
            var diplomacy = new DiplomacySimulation(fresh.Diplomacy);
            EstablishCommunication(diplomacy, first.Id, second.Id, "v9:first-second", tick: 10, first.HomeSystemId);
            EstablishCommunication(diplomacy, second.Id, first.Id, "v9:second-first", tick: 11, second.HomeSystemId);
            diplomacy.SetAccessPermission(first.Id, second.Id, AccessPermission.Granted, tick: 12);
            var claimId = diplomacy.AssertTerritorialClaim(first.Id, first.HomeSystemId, tick: 13);
            diplomacy.CommunicateTerritorialClaim(claimId, second.Id, tick: 14);
            var proposalId = diplomacy.SendProposal(
                first.Id,
                second.Id,
                DiplomaticProposalKind.AccessRequest,
                tick: 15,
                summary: "Request reciprocal transit access.");

            var expected = fresh.Diplomacy.Snapshot();
            Require(expected.Contacts.Length == 2, "validation setup did not create two directional contacts");
            Require(expected.AccessPermissions.Length == 1, "validation setup did not create access state");
            Require(expected.Claims.Length == 1 && expected.Claims[0].ClaimId == claimId, "validation setup did not create the territorial claim");
            Require(expected.Proposals.Length == 1 && expected.Proposals[0].ProposalId == proposalId, "validation setup did not create the proposal");

            var v9Path = Path.Combine(directory, "campaign-v9.json");
            const double savedDays = 812.75;
            session.Save(v9Path, fresh.Galaxy, fresh.Diplomacy, savedDays);

            var json = File.ReadAllText(v9Path);
            Require(json.Contains("\"FormatVersion\": 17", StringComparison.Ordinal), "campaign session did not write authoritative-catalog format v17");
            Require(json.Contains("\"Diplomacy\"", StringComparison.Ordinal), "format v9 save omitted Diplomacy");

            var loaded = session.LoadOrCreate(v9Path, fallbackSeed: 1L, fallbackSettings: settings);
            Require(loaded.Source == CampaignBootstrapSource.LoadedSave, "v9 save did not reload through campaign session");
            Require(Math.Abs(loaded.SimulationDays - savedDays) < 0.000001, "v9 load changed simulation time");

            var actual = loaded.Diplomacy.Snapshot();
            var expectedJson = JsonSerializer.Serialize(expected);
            var actualJson = JsonSerializer.Serialize(actual);
            Require(actualJson == expectedJson, "v9 load changed the canonical Diplomacy snapshot");

            var legacyPath = Path.Combine(directory, "legacy-v8.json");
            new CampaignSaveService().Save(legacyPath, fresh.Galaxy, savedDays);
            var legacyRoot = JsonNode.Parse(File.ReadAllText(legacyPath))!.AsObject();
            legacyRoot["FormatVersion"] = CampaignSaveService.LegacyFormatVersion;
            legacyRoot["Galaxy"]!.AsObject().Remove("PlanetaryBodies");
            File.WriteAllText(legacyPath, legacyRoot.ToJsonString());
            var legacyLoaded = new CampaignStatePersistenceService().Load(legacyPath);
            var migratedDiplomacy = legacyLoaded.Diplomacy.Snapshot();
            Require(migratedDiplomacy.Contacts.Length == 0, "v8 migration invented diplomatic contacts");
            Require(migratedDiplomacy.Relationships.Length == 0, "v8 migration invented relationships");
            Require(migratedDiplomacy.Claims.Length == 0, "v8 migration invented territorial claims");
            Require(migratedDiplomacy.Agreements.Length == 0, "v8 migration invented agreements");
            Require(migratedDiplomacy.Proposals.Length == 0, "v8 migration invented proposals");
            Require(migratedDiplomacy.RecentHistory.Length == 0, "v8 migration invented diplomatic history");
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void EstablishCommunication(
        DiplomacySimulation diplomacy,
        int observer,
        int target,
        string contactId,
        long tick,
        int systemId)
    {
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            observer,
            contactId,
            target,
            tick,
            systemId,
            ContactAwareness.CommunicationAvailable,
            ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
