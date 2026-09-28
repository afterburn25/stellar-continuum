using Game.Simulation.Diplomacy;
using Game.Simulation.Territory;
using Game.Validation;

namespace Game.Territory.Validation;

internal static class TerritorialDiplomacyBridgeValidation
{
    [RegressionCheck]
    private static void ClaimsNeedAnchorsAndRecognitionIsLocal()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var a = TerritoryScenario.Owner(galaxy); var b = TerritoryScenario.Owner(galaxy, 1);
        var diplomacy = Mutual(a, b); var simulation = new DiplomacySimulation(diplomacy);
        var near = TerritoryScenario.System(galaxy, 1); var remote = TerritoryScenario.System(galaxy, 6);
        TerritorialDiplomacyBridge.Bind(galaxy, diplomacy);
        var unsupported = simulation.AssertTerritorialClaim(a, remote, 1); simulation.CommunicateTerritorialClaim(unsupported, b, 2); simulation.RespondToTerritorialClaim(unsupported, b, TerritorialClaimResponse.Recognized, 3);
        TerritorialDiplomacyBridge.Bind(galaxy, diplomacy); TerritorialRuntime.Peek(galaxy)!.Recompute(galaxy);
        Require(TerritorialRuntime.Peek(galaxy)!.Read(a, remote)!.Political == 0 && TerritorialRuntime.Peek(galaxy)!.Systems[remote].ControllerId is null,
            "a paper claim without a represented anchor created territorial influence or sovereignty");

        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, a, TerritoryScenario.System(galaxy, 0)));
        var runtime = TerritorialRuntime.Initialize(galaxy); runtime.Recompute(galaxy);
        var nearBefore = runtime.Read(a, near)!.Political; var remoteBefore = runtime.Read(a, remote)!.Political;
        var nearClaim = simulation.AssertTerritorialClaim(a, near, 4); simulation.CommunicateTerritorialClaim(nearClaim, b, 5); simulation.RespondToTerritorialClaim(nearClaim, b, TerritorialClaimResponse.Recognized, 6);
        TerritorialDiplomacyBridge.Bind(galaxy, diplomacy); runtime.Recompute(galaxy);
        Require(runtime.Read(a, near)!.Political > nearBefore + 2.9, "recognized claim near a real anchor did not add bounded political reinforcement");
        Require(Math.Abs(runtime.Read(a, remote)!.Political - remoteBefore) < .000001, "recognition of a remote claim projected influence beyond its nearby anchor");
    }

    [RegressionCheck]
    private static void AccessChangesSupplyButDoesNotCreatePoliticalControl()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var a = TerritoryScenario.Owner(galaxy); var b = TerritoryScenario.Owner(galaxy, 1);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, a, TerritoryScenario.System(galaxy, 0)));
        // The sparse lane graph retains local alternate links. Occupying this narrow corridor
        // makes the access effect observable without treating permission as a political source.
        foreach (var index in new[] { 1, 2, 3 }) galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, b, TerritoryScenario.System(galaxy, index)));
        galaxy.Fleets.Add(TerritoryScenario.Fleet(galaxy, a, TerritoryScenario.System(galaxy, 0), Game.Simulation.Models.FleetRole.Logistics));
        var diplomacy = Mutual(a, b); TerritorialDiplomacyBridge.Bind(galaxy, diplomacy); var runtime = TerritorialRuntime.Peek(galaxy)!;
        var target = TerritoryScenario.System(galaxy, 4); var blocked = runtime.Read(a, target)!;
        new DiplomacySimulation(diplomacy).SetAccessPermission(b, a, AccessPermission.Granted, 7); runtime.Recompute(galaxy);
        var opened = runtime.Read(a, target)!;
        Require(opened.Supply > blocked.Supply, $"granted foreign access did not unblock the operational supply lane ({blocked.Supply:0.000} -> {opened.Supply:0.000})");
        Require(Math.Abs(opened.Political - blocked.Political) < .000001, "access permission fabricated political influence instead of changing only operational supply");
    }

    [RegressionCheck]
    private static void ClaimTensionNeedsMutualKnowledgeAndIsBoundedAndPersistent()
    {
        var hidden = Competitive(out var hiddenA, out var hiddenB, out var hiddenSystem);
        var hiddenDiplomacy = new DiplomacyState(); var hiddenSimulation = new DiplomacySimulation(hiddenDiplomacy);
        hiddenSimulation.AssertTerritorialClaim(hiddenA, hiddenSystem, 1); hiddenSimulation.AssertTerritorialClaim(hiddenB, hiddenSystem, 2);
        Require(TerritorialDiplomacyBridge.Review(hidden, hiddenDiplomacy, 3) == 0 && hiddenDiplomacy.GetRelationship(hiddenA, hiddenB) is null,
            "unknown competing claims created relationship tension through fog of war");

        var galaxy = Competitive(out var a, out var b, out var contested); var diplomacy = Mutual(a, b); var simulation = new DiplomacySimulation(diplomacy);
        var first = simulation.AssertTerritorialClaim(a, contested, 10); simulation.CommunicateTerritorialClaim(first, b, 11);
        var second = simulation.AssertTerritorialClaim(b, contested, 12); simulation.CommunicateTerritorialClaim(second, a, 13);
        Require(TerritorialDiplomacyBridge.Review(galaxy, diplomacy, 14) == 1, "known mutual claims in a surveyed contested system did not create bounded border tension");
        var relationship = diplomacy.GetRelationship(a, b)!; var hostility = relationship.Hostility;
        Require(TerritorialDiplomacyBridge.Review(galaxy, diplomacy, 15) == 0 && Math.Abs(diplomacy.GetRelationship(a, b)!.Hostility - hostility) < .000001,
            "border tension repeated before the per-pair 180-day bound");
        var captured = TerritorialPersistence.Capture(galaxy)!;
        Require(captured.BorderIncidents.Count == 1 && captured.NextDiplomaticReviewDay > 0, "border incident memory or diplomatic review clock was not persisted");
    }

    [RegressionCheck]
    private static void ObserverMemoryPersistsOnlyLegitimateControlReports()
    {
        var galaxy = Competitive(out var observer, out var foreign, out var system);
        galaxy.Knowledge.RevealCivilization(observer, foreign); galaxy.Knowledge.MarkSystemFullySurveyed(observer, system);
        TerritorialObservationMemory.Remember(galaxy, observer, system, foreign, TerritorialControlStatus.Dominant);
        var captured = TerritorialPersistence.Capture(galaxy)!;
        Require(captured.Observations.Single() == new TerritorialObservation(observer, system, foreign, TerritorialControlStatus.Dominant),
            "captured territory omitted a legitimate observer control report");
        galaxy.Territory = captured; TerritorialPersistence.Validate(galaxy);
        Require(TerritorialObservationMemory.Recall(galaxy, observer, system)?.CivilizationId == foreign,
            "restored territorial state lost last-known observer control");
        const int unknown = 999;
        galaxy.Knowledge.MarkSystemFullySurveyed(observer, system);
        TerritorialObservationMemory.Remember(galaxy, observer, system, unknown, TerritorialControlStatus.Dominant);
        Require(galaxy.Territory.Observations.Count == 1 && galaxy.Territory.Observations[0].CivilizationId == foreign,
            "unidentified foreign control was written into observer memory");
    }

    private static Game.Simulation.Models.GalaxyState Competitive(out int a, out int b, out int contested)
    {
        var galaxy = TerritoryScenario.AnchoredLine(); a = TerritoryScenario.Owner(galaxy); b = TerritoryScenario.Owner(galaxy, 1); contested = TerritoryScenario.System(galaxy, 3);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, a, TerritoryScenario.System(galaxy, 0)));
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, b, TerritoryScenario.System(galaxy, 6)));
        TerritorialRuntime.Initialize(galaxy); return galaxy;
    }
    private static DiplomacyState Mutual(int a, int b)
    {
        var state = new DiplomacyState(); var simulation = new DiplomacySimulation(state);
        simulation.ProcessContactOpportunity(new(a, "contact-" + b, b, 1, null, ContactAwareness.CommunicationAvailable, ContactCondition.Active, true, 1));
        simulation.ProcessContactOpportunity(new(b, "contact-" + a, a, 1, null, ContactAwareness.CommunicationAvailable, ContactCondition.Active, true, 1));
        return state;
    }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
