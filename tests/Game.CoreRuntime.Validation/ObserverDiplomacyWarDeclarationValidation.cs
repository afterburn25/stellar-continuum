using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class ObserverDiplomacyWarDeclarationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunObserverDiplomacyWarDeclarationChecks()
    {
        ValidateObserverSafeFormalWarDeclaration();
        Console.WriteLine("PASS: observer-safe formal Diplomacy war declaration");
    }

    private static void ValidateObserverSafeFormalWarDeclaration()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        runtime.Reset(simulationDays: 0.0, reviewImmediately: false);

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 11,
            ContactId: "civilization:22",
            TargetCivilizationId: 22,
            ObservedAtTick: 5,
            ObservedSystemId: 7,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.95));

        var hiddenKnownTarget = runtime.Commands.DeclareWar(22, 11, tick: 6);
        var nonexistentTarget = runtime.Commands.DeclareWar(22, 999, tick: 6);
        Require(!hiddenKnownTarget.Accepted && !nonexistentTarget.Accepted,
            "observer without visible target identity unexpectedly declared war");
        Require(hiddenKnownTarget.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistentTarget.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                hiddenKnownTarget.Message == nonexistentTarget.Message,
            "hidden and nonexistent war targets produced distinguishable observer results");

        var declaration = runtime.Commands.DeclareWar(11, 22, tick: 7);
        Require(declaration.Accepted && declaration.Status == ObserverDiplomacyCommandStatus.Accepted,
            "identified observer could not formally declare war through the safe gateway");
        Require(state.GetRelationship(11, 22)?.PoliticalState == DiplomaticPoliticalState.AtWar,
            "formal war command did not set authoritative AtWar state");
        Require(state.GetAccessPermission(11, 22) == AccessPermission.Denied &&
                state.GetAccessPermission(22, 11) == AccessPermission.Denied,
            "formal war declaration did not close both political access directions");
        Require(runtime.HostilityView.AreHostile(11, 22),
            "formal war declaration did not immediately authorize Combat hostility");

        var declarerView = runtime.BuildView(11);
        Require(declarerView.Relationships.Single(r => r.OtherCivilizationId == 22).PoliticalState == DiplomaticPoliticalState.AtWar,
            "declarer view did not expose its own formal war state");
        Require(declarerView.RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.WarDeclared &&
                evt.PrimaryCivilizationId == 11 &&
                evt.SecondaryCivilizationId == 22),
            "declarer lost its own WarDeclared history event");

        var unawareTargetView = runtime.BuildView(22);
        Require(unawareTargetView.Contacts.Count == 0 &&
                unawareTargetView.Relationships.Count == 0 &&
                unawareTargetView.RecentEvents.All(evt => evt.Kind != DiplomaticEventKind.WarDeclared),
            "one-sided war declaration leaked declarer identity to an unaware target");

        var retry = runtime.Commands.DeclareWar(11, 22, tick: 8);
        Require(retry.Accepted,
            "formal war declaration was not idempotent for an already-AtWar pair");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 11,
            ContactId: "civilization:33",
            TargetCivilizationId: 33,
            ObservedAtTick: 9,
            ObservedSystemId: 8,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.StaleOrLost,
            CommunicationAvailable: false,
            Confidence: 0.60));

        var staleDeclaration = runtime.Commands.DeclareWar(11, 33, tick: 10);
        Require(staleDeclaration.Accepted &&
                state.GetRelationship(11, 33)?.PoliticalState == DiplomaticPoliticalState.AtWar,
            "stale but legitimately identified counterpart could not be formally declared on");

        var invalidSelf = runtime.Commands.DeclareWar(11, 11, tick: 11);
        var invalidTick = runtime.Commands.DeclareWar(11, 22, tick: -1);
        Require(invalidSelf.Status == ObserverDiplomacyCommandStatus.InvalidRequest &&
                invalidTick.Status == ObserverDiplomacyCommandStatus.InvalidRequest,
            "invalid formal war requests did not fail through InvalidRequest");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
