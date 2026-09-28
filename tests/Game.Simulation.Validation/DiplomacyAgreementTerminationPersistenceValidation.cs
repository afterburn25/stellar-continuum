using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Persistence;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;

namespace Game.Simulation.Validation;

internal static class DiplomacyAgreementTerminationPersistenceValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyAgreementTerminationPersistenceChecks()
    {
        ValidateTerminatedAgreementsSurviveCampaignPersistence();
        Console.WriteLine("PASS: save-v9 Diplomacy agreement termination persistence");
    }

    private static void ValidateTerminatedAgreementsSurviveCampaignPersistence()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            $"steller-diplomacy-agreement-termination-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);

        var firstPath = Path.Combine(directory, "agreement-termination-v9.json");
        var secondPath = Path.Combine(directory, "agreement-termination-v9-resaved.json");

        try
        {
            Require(CampaignStatePersistenceService.CurrentFormatVersion == 17 && CampaignStatePersistenceService.AdaptiveFormatVersion == 15 && CampaignStatePersistenceService.SurfaceFormatVersion == 13 && CampaignStatePersistenceService.PresetFormatVersion == 11 && CampaignStatePersistenceService.LegacyFormatVersion == 9,
                "agreement termination persistence unexpectedly changed the campaign save version");

            var galaxy = new GalaxyGenerator().Generate(
                0x4147_5245_454D_454EL,
                new GalaxyGenerationSettings
                {
                    SystemCount = 40,
                    PreWarpCivilizationCount = 5,
                    AncientCivilizationCount = 1,
                    Radius = 480.0f,
                });

            var civilizationIds = galaxy.Civilizations
                .OrderBy(civilization => civilization.Id)
                .Take(3)
                .Select(civilization => civilization.Id)
                .ToArray();
            Require(civilizationIds.Length == 3,
                "agreement termination persistence validation requires three civilizations");

            var first = civilizationIds[0];
            var second = civilizationIds[1];
            var third = civilizationIds[2];

            var diplomacyState = new DiplomacyState();
            EstablishMutualCommunication(diplomacyState, first, second, tick: 1);
            EstablishMutualCommunication(diplomacyState, second, third, tick: 10);
            var diplomacy = new DiplomacySimulation(diplomacyState);
            var termination = new DiplomaticAgreementTerminationService(diplomacyState);

            var accessProposal = diplomacy.SendProposal(
                first,
                second,
                DiplomaticProposalKind.Agreement,
                tick: 2,
                summary: "Mutual access for persistence validation.",
                agreementType: DiplomaticAgreementType.Access);
            diplomacy.RespondToProposal(accessProposal, responder: second, accept: true, tick: 3);
            var accessAgreement = diplomacyState.Snapshot().Agreements.Single(agreement =>
                agreement.Type == DiplomaticAgreementType.Access &&
                agreement.CivilizationAId == Math.Min(first, second) &&
                agreement.CivilizationBId == Math.Max(first, second) &&
                agreement.Status == DiplomaticAgreementStatus.Active);
            termination.Terminate(
                accessAgreement.AgreementId,
                requesterCivilizationId: first,
                tick: 4,
                reason: "Access compact formally concluded.");

            Require(diplomacyState.GetAccessPermission(first, second) == AccessPermission.Unspecified &&
                    diplomacyState.GetAccessPermission(second, first) == AccessPermission.Unspecified,
                "Access agreement did not clear mutual access before persistence");

            diplomacy.DeclareWar(second, third, tick: 11);
            var ceasefireProposal = diplomacy.SendProposal(
                second,
                third,
                DiplomaticProposalKind.CeasefireOffer,
                tick: 12,
                summary: "Temporary ceasefire for persistence validation.");
            diplomacy.RespondToProposal(ceasefireProposal, responder: third, accept: true, tick: 13);
            var ceasefireAgreement = diplomacyState.Snapshot().Agreements.Single(agreement =>
                agreement.Type == DiplomaticAgreementType.Ceasefire &&
                agreement.CivilizationAId == Math.Min(second, third) &&
                agreement.CivilizationBId == Math.Max(second, third) &&
                agreement.Status == DiplomaticAgreementStatus.Active);
            termination.Terminate(
                ceasefireAgreement.AgreementId,
                requesterCivilizationId: third,
                tick: 14,
                reason: "Ceasefire negotiations collapsed.");

            Require(diplomacyState.GetRelationship(second, third)?.PoliticalState == DiplomaticPoliticalState.Hostile,
                "ceasefire termination did not resume Hostile state before persistence");
            Require(new DiplomacyCombatHostilityView(diplomacyState).AreHostile(second, third),
                "Combat hostility did not resume before persistence");
            Require(diplomacyState.GetRelationship(second, third)?.PoliticalState != DiplomaticPoliticalState.AtWar,
                "ceasefire termination silently declared war before persistence");

            var beforeSnapshot = diplomacyState.Snapshot();
            DiplomacySnapshotInvariantValidator.Validate(beforeSnapshot);

            var persistence = new CampaignStatePersistenceService();
            persistence.Save(firstPath, galaxy, simulationDays: 88.5, diplomacyState);

            using (var document = JsonDocument.Parse(File.ReadAllText(firstPath)))
            {
                Require(document.RootElement.GetProperty("FormatVersion").GetInt32() == 17,
                    "agreement termination save was not written as campaign format v9");
            }

            var loaded = persistence.Load(firstPath);
            Require(Math.Abs(loaded.SimulationDays - 88.5) < 0.000001,
                "agreement termination persistence changed simulation time");
            AssertRestoredAgreementState(
                loaded.Diplomacy,
                first,
                second,
                third,
                accessAgreement.AgreementId,
                ceasefireAgreement.AgreementId);

            var firstLoadedSnapshot = loaded.Diplomacy.Snapshot();
            Require(firstLoadedSnapshot.NextAgreementId == beforeSnapshot.NextAgreementId &&
                    firstLoadedSnapshot.NextEventId == beforeSnapshot.NextEventId,
                "agreement termination persistence changed next identity allocators");

            persistence.Save(
                secondPath,
                loaded.Galaxy,
                loaded.SimulationDays,
                loaded.Diplomacy);
            var reloaded = persistence.Load(secondPath);
            AssertRestoredAgreementState(
                reloaded.Diplomacy,
                first,
                second,
                third,
                accessAgreement.AgreementId,
                ceasefireAgreement.AgreementId);

            var secondLoadedSnapshot = reloaded.Diplomacy.Snapshot();
            Require(secondLoadedSnapshot.NextAgreementId == firstLoadedSnapshot.NextAgreementId &&
                    secondLoadedSnapshot.NextEventId == firstLoadedSnapshot.NextEventId,
                "second save-v9 cycle changed agreement/event identity allocators");
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void AssertRestoredAgreementState(
        DiplomacyState state,
        int first,
        int second,
        int third,
        long accessAgreementId,
        long ceasefireAgreementId)
    {
        var snapshot = state.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(snapshot);

        var access = snapshot.Agreements.Single(agreement => agreement.AgreementId == accessAgreementId);
        Require(access.Type == DiplomaticAgreementType.Access &&
                access.Status == DiplomaticAgreementStatus.Terminated &&
                access.EndedAtTick == 4,
            "save-v9 load did not preserve terminated Access agreement chronology");
        Require(state.GetAccessPermission(first, second) == AccessPermission.Unspecified &&
                state.GetAccessPermission(second, first) == AccessPermission.Unspecified,
            "save-v9 load did not preserve cleared mutual Access permissions");
        Require(state.GetRelationship(first, second)?.PoliticalState == DiplomaticPoliticalState.Peace,
            "save-v9 load invented hostility after Access termination");

        var ceasefire = snapshot.Agreements.Single(agreement => agreement.AgreementId == ceasefireAgreementId);
        Require(ceasefire.Type == DiplomaticAgreementType.Ceasefire &&
                ceasefire.Status == DiplomaticAgreementStatus.Terminated &&
                ceasefire.EndedAtTick == 14,
            "save-v9 load did not preserve terminated Ceasefire agreement chronology");
        Require(state.GetRelationship(second, third)?.PoliticalState == DiplomaticPoliticalState.Hostile,
            "save-v9 load did not preserve Hostile state after ceasefire termination");
        Require(state.GetRelationship(second, third)?.PoliticalState != DiplomaticPoliticalState.AtWar,
            "save-v9 load converted ended ceasefire hostility into war");
        Require(new DiplomacyCombatHostilityView(state).AreHostile(second, third),
            "restored Combat hostility view did not reflect ended ceasefire Hostile state");

        var firstView = state.BuildViewFor(first);
        var secondView = state.BuildViewFor(second);
        var thirdView = state.BuildViewFor(third);
        Require(firstView.RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.AgreementTerminated && evt.Tick == 4),
            "first Access participant lost termination history after save-v9 load");
        Require(secondView.RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.AgreementTerminated &&
                (evt.Tick == 4 || evt.Tick == 14)),
            "shared agreement participant lost termination history after save-v9 load");
        Require(thirdView.RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.AgreementTerminated && evt.Tick == 14),
            "ceasefire participant lost termination history after save-v9 load");
    }

    private static void EstablishMutualCommunication(
        DiplomacyState state,
        int first,
        int second,
        long tick)
    {
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(CommunicatingContact(first, second, tick));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(second, first, tick));
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"agreement-save-contact-{observer}-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: null,
        Awareness: ContactAwareness.CommunicationAvailable,
        Condition: ContactCondition.Active,
        CommunicationAvailable: true,
        Confidence: 1.0);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
