using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacyValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyChecks()
    {
        var tests = new (string Name, Action Run)[]
        {
            ("diplomacy directional contact / fair information", ValidateDirectionalContactAndFairInformation),
            ("diplomacy agreements / access / war authority", ValidateAgreementsAccessAndWarAuthority),
            ("diplomacy claims / warnings / trade hooks", ValidateClaimsWarningsAndTradeHooks),
            ("diplomacy third-party information isolation", ValidateThirdPartyInformationIsolation),
            ("diplomacy snapshot / bounded history", ValidateSnapshotRoundTripAndBoundedHistory),
        };

        foreach (var test in tests)
        {
            test.Run();
            Console.WriteLine($"PASS: {test.Name}");
        }
    }

    public static void ValidateDirectionalContactAndFairInformation()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            1, "signal-alpha", null, 10, 42,
            ContactAwareness.DetectedUnidentified, ContactCondition.Active, false, 0.35));

        var observer = state.BuildViewFor(1);
        var other = state.BuildViewFor(2);
        Require(observer.Contacts.Count == 1, "observer did not retain detected contact");
        Require(observer.Contacts[0].TargetCivilizationId is null, "unidentified contact leaked authoritative identity");
        Require(other.Contacts.Count == 0, "contact observation was incorrectly made reciprocal");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            1, "signal-alpha", 2, 20, 42,
            ContactAwareness.Identified, ContactCondition.Active, false, 0.72));
        Require(state.BuildViewFor(1).Relationships.Count == 0, "identification alone created a political relationship too early");
        Require(state.BuildViewFor(2).Contacts.Count == 0, "identification leaked reciprocal contact knowledge");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            1, "signal-alpha", 2, 30, 42,
            ContactAwareness.ContactEstablished, ContactCondition.Active, false, 0.86));
        Require(state.GetRelationship(1, 2)?.PoliticalState == DiplomaticPoliticalState.Peace,
            "established contact did not create neutral peace-state relationship");
        Require(state.BuildViewFor(1).Relationships.Count == 1, "observer could not view its established relationship");
        Require(state.BuildViewFor(2).Relationships.Count == 0, "bilateral authoritative relationship leaked to an unaware civilization view");
    }

    public static void ValidateAgreementsAccessAndWarAuthority()
    {
        var state = CreateMutualCommunications(1, 2);
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.SetAccessPermission(1, 2, AccessPermission.Granted, 50);
        Require(state.GetAccessPermission(1, 2) == AccessPermission.Granted, "granted access was not stored directionally");
        Require(state.GetAccessPermission(2, 1) == AccessPermission.Unspecified, "unilateral access incorrectly became reciprocal");
        Require(state.IsTransitAuthorized(1, 2), "granted access was not reported as politically authorized");

        var proposalId = diplomacy.SendProposal(
            1, 2, DiplomaticProposalKind.Agreement, 60,
            "Mutual non-aggression pact", DiplomaticAgreementType.NonAggression);
        diplomacy.RespondToProposal(proposalId, 2, accept: true, 65);

        var preWar = state.BuildViewFor(1);
        Require(preWar.Agreements.Any(agreement => agreement.Type == DiplomaticAgreementType.NonAggression && agreement.Status == DiplomaticAgreementStatus.Active),
            "accepted non-aggression proposal did not create an active agreement");

        diplomacy.DeclareWar(1, 2, 80);
        var relationship = state.GetRelationship(1, 2);
        Require(relationship?.PoliticalState == DiplomaticPoliticalState.AtWar, "diplomacy did not become authoritative for war state");
        Require(!state.IsTransitAuthorized(1, 2), "wartime transit was incorrectly reported as politically authorized");
        Require(state.GetAccessPermission(1, 2) == AccessPermission.Denied, "war did not revoke legal access from declarer to target");
        Require(state.GetAccessPermission(2, 1) == AccessPermission.Denied, "war did not revoke legal access from target to declarer");
        Require(state.BuildViewFor(1).Agreements.All(agreement => agreement.Status == DiplomaticAgreementStatus.Terminated),
            "war did not terminate incompatible active agreements");
    }

    public static void ValidateClaimsWarningsAndTradeHooks()
    {
        var state = CreateMutualCommunications(1, 2);
        var diplomacy = new DiplomacySimulation(state);

        var claimId = diplomacy.AssertTerritorialClaim(1, 77, 100);
        Require(state.BuildViewFor(2).Claims.Count == 0, "uncommunicated claim leaked to another civilization");

        diplomacy.CommunicateTerritorialClaim(claimId, 2, 110);
        Require(state.BuildViewFor(2).Claims.Any(claim => claim.ClaimId == claimId), "communicated claim was not visible to recipient");
        diplomacy.RespondToTerritorialClaim(claimId, 2, TerritorialClaimResponse.Disputed, 115);
        Require(state.BuildViewFor(1).ClaimResponses.Any(response => response.ClaimId == claimId && response.Response == TerritorialClaimResponse.Disputed),
            "claim dispute did not reach claimant");

        diplomacy.SetAccessPermission(1, 2, AccessPermission.Denied, 120);
        diplomacy.RecordTrespass(1, 2, 77, 121);
        diplomacy.IssueBorderWarning(1, 2, 77, 122);
        var recipientEvents = state.BuildViewFor(2).RecentEvents;
        Require(recipientEvents.Any(item => item.Kind == DiplomaticEventKind.TrespassRecorded && item.SystemId == 77),
            "trespass incident was not preserved as a meaningful diplomatic event");
        Require(recipientEvents.Any(item => item.Kind == DiplomaticEventKind.BorderWarningIssued && item.SystemId == 77),
            "border warning was not preserved as a meaningful diplomatic event");

        var tradeProposal = diplomacy.SendProposal(
            1, 2, DiplomaticProposalKind.TradeOffer, 130,
            "Exchange industrial feedstock for scientific instrumentation",
            externalTermsReference: "economy-offer:test-001");
        diplomacy.RespondToProposal(tradeProposal, 2, accept: true, 135);
        var tradeAgreement = state.BuildViewFor(1).Agreements.Single(agreement => agreement.Type == DiplomaticAgreementType.Trade);
        Require(tradeAgreement.ExternalTermsReference == "economy-offer:test-001",
            "trade agreement lost the authoritative economy/logistics terms reference");
    }

    public static void ValidateThirdPartyInformationIsolation()
    {
        var state = new DiplomacyState();
        EstablishMutualCommunication(state, 1, 2, 10);
        EstablishMutualCommunication(state, 2, 3, 20);
        var diplomacy = new DiplomacySimulation(state);

        var proposal = diplomacy.SendProposal(
            2, 3, DiplomaticProposalKind.Agreement, 30,
            "Cooperation framework", DiplomaticAgreementType.Cooperation);
        diplomacy.RespondToProposal(proposal, 3, accept: true, 35);

        var observerOne = state.BuildViewFor(1);
        Require(observerOne.Contacts.All(contact => contact.TargetCivilizationId != 3), "observer learned an unknown third civilization through diplomacy state");
        Require(observerOne.Relationships.All(relationship => relationship.OtherCivilizationId != 3), "observer learned a hidden third-party relationship");
        Require(observerOne.Agreements.All(agreement => agreement.CivilizationAId != 3 && agreement.CivilizationBId != 3),
            "observer learned a secret third-party agreement");
        Require(observerOne.RecentEvents.All(item => item.PrimaryCivilizationId != 3 && item.SecondaryCivilizationId != 3),
            "observer received a third-party diplomatic event outside its legitimate audience");
    }

    public static void ValidateSnapshotRoundTripAndBoundedHistory()
    {
        var state = CreateMutualCommunications(4, 5);
        var diplomacy = new DiplomacySimulation(state);

        for (var i = 0; i < DiplomacyState.MaxRecentHistoryEvents + 40; i++)
        {
            diplomacy.ApplyRelationshipImpact(
                4,
                5,
                new RelationshipImpact(0.001, 0.0, 0.0, 0.0, 0.0, 0.0, $"Cooperative exchange {i}"),
                1000 + i);
        }

        Require(state.Snapshot().RecentHistory.Length == DiplomacyState.MaxRecentHistoryEvents,
            "diplomatic history grew beyond its bounded recent-event limit");

        var snapshot = state.Snapshot();
        var json = JsonSerializer.Serialize(snapshot);
        var deserialized = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(json)
            ?? throw new InvalidOperationException("diplomacy snapshot JSON round trip returned null");
        var restored = DiplomacyState.Restore(deserialized);

        var originalView = state.BuildViewFor(4);
        var restoredView = restored.BuildViewFor(4);
        Require(restoredView.Contacts.Count == originalView.Contacts.Count, "snapshot restore changed contact count");
        Require(restoredView.Relationships.Count == originalView.Relationships.Count, "snapshot restore changed relationship count");
        Require(restoredView.RecentEvents.Count == DiplomacyState.MaxRecentHistoryEvents, "snapshot restore lost bounded recent history");
        Require(Math.Abs(restoredView.Relationships[0].Trust - originalView.Relationships[0].Trust) < 0.000001,
            "snapshot restore changed relationship state");
    }

    private static DiplomacyState CreateMutualCommunications(int a, int b)
    {
        var state = new DiplomacyState();
        EstablishMutualCommunication(state, a, b, 10);
        return state;
    }

    private static void EstablishMutualCommunication(DiplomacyState state, int a, int b, long tick)
    {
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            a, $"civilization:{b}", b, tick, null,
            ContactAwareness.CommunicationAvailable, ContactCondition.Active, true, 1.0));
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            b, $"civilization:{a}", a, tick, null,
            ContactAwareness.CommunicationAvailable, ContactCondition.Active, true, 1.0));
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
