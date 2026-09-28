using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignV9DiplomacyReferenceValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCampaignV9DiplomacyReferenceChecks()
    {
        ValidateDanglingCampaignReferencesAreRejected();
        Console.WriteLine("PASS: save v9 rejects dangling Diplomacy campaign references");
    }

    private static void ValidateDanglingCampaignReferencesAreRejected()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-v9-refs",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var galaxy = new GalaxyGenerator().Generate(
                0x5639_5245_4653_4C4FL,
                new GalaxyGenerationSettings
                {
                    SystemCount = 20,
                    PreWarpCivilizationCount = 2,
                    AncientCivilizationCount = 0,
                    Radius = 280.0f,
                });

            var danglingSnapshot = new DiplomacyStateSnapshot(
                Contacts: new[]
                {
                    new DiplomaticContactSnapshot(
                        ObserverCivilizationId: 999_999,
                        ContactId: "dangling-observer",
                        TargetCivilizationId: null,
                        FirstObservedTick: 1,
                        LastObservedTick: 1,
                        LastObservedSystemId: null,
                        Awareness: ContactAwareness.DetectedUnidentified,
                        Condition: ContactCondition.Active,
                        CommunicationAvailable: false,
                        Confidence: 0.5),
                },
                Relationships: Array.Empty<DiplomaticRelationshipSnapshot>(),
                AccessPermissions: Array.Empty<DiplomaticAccessSnapshot>(),
                Claims: Array.Empty<TerritorialClaimSnapshot>(),
                ClaimResponses: Array.Empty<TerritorialClaimResponseSnapshot>(),
                Agreements: Array.Empty<DiplomaticAgreementSnapshot>(),
                Proposals: Array.Empty<DiplomaticProposalSnapshot>(),
                RecentHistory: Array.Empty<DiplomaticHistoryEventSnapshot>(),
                NextClaimId: 1,
                NextAgreementId: 1,
                NextProposalId: 1,
                NextEventId: 1);

            // The snapshot is intentionally internally valid. The failure must come from the
            // campaign cross-reference boundary, not Diplomacy's own structural validator.
            DiplomacySnapshotInvariantValidator.Validate(danglingSnapshot);
            var danglingState = DiplomacyState.Restore(danglingSnapshot);
            var service = new CampaignStatePersistenceService();
            var path = Path.Combine(directory, "dangling-save.json");

            ExpectInvalidData(
                () => service.Save(path, galaxy, simulationDays: 10.0, danglingState),
                "v9 save accepted a Diplomacy contact owned by a nonexistent civilization");

            var validPath = Path.Combine(directory, "valid-v9.json");
            service.Save(validPath, galaxy, simulationDays: 10.0, new DiplomacyState());
            var root = JsonNode.Parse(File.ReadAllText(validPath))?.AsObject()
                ?? throw new InvalidOperationException("could not parse generated v9 save");
            root["Diplomacy"] = JsonSerializer.SerializeToNode(danglingSnapshot)
                ?? throw new InvalidOperationException("could not serialize dangling snapshot");
            var tamperedPath = Path.Combine(directory, "tampered-v9.json");
            File.WriteAllText(tamperedPath, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));

            ExpectInvalidData(
                () => service.Load(tamperedPath),
                "v9 load accepted a Diplomacy contact owned by a nonexistent civilization");
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void ExpectInvalidData(Action action, string message)
    {
        try
        {
            action();
        }
        catch (InvalidDataException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }
}
