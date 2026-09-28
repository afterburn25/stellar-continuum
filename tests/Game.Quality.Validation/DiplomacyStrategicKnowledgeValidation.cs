using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Diplomacy;

namespace Game.Quality.Validation;

internal static class DiplomacyStrategicKnowledgeValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateObserverFilteredDiplomacyKnowledge();
        Console.WriteLine("PASS: Civilization AI consumes observer-filtered persisted Diplomacy knowledge");
    }

    private static void ValidateObserverFilteredDiplomacyKnowledge()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        EstablishMutualContact(diplomacy, 1, 2, tick: 10);
        EstablishMutualContact(diplomacy, 2, 3, tick: 20);

        diplomacy.ApplyRelationshipImpact(
            1,
            2,
            new RelationshipImpact(
                TrustDelta: 0.60,
                HostilityDelta: 0.20,
                FearDelta: 0.0,
                RespectDelta: 0.10,
                CooperationDelta: 0.20,
                GrievanceSeverity: 0.0,
                Reason: "Observer-visible bilateral history."),
            tick: 25);

        diplomacy.ApplyRelationshipImpact(
            2,
            3,
            new RelationshipImpact(
                TrustDelta: 0.75,
                HostilityDelta: 0.0,
                FearDelta: 0.0,
                RespectDelta: 0.20,
                CooperationDelta: 0.50,
                GrievanceSeverity: 0.0,
                Reason: "Private third-party relationship."),
            tick: 26);

        diplomacy.DeclareWar(declarer: 2, target: 1, tick: 30);

        IStrategicKnowledgeProvider provider = new DiplomacyStrategicKnowledgeProvider(state);
        var observerOne = provider.Build(observerCivilizationId: 1, nowTick: 40);
        var observerTwo = provider.Build(observerCivilizationId: 2, nowTick: 40);

        Require(observerOne.ObservedAtTick == 40, "AI knowledge snapshot changed the strategic observation tick");
        Require(observerOne.Civilizations.Count == 1 && observerOne.Civilizations.ContainsKey(2),
            "observer 1 did not receive exactly its legitimately identified civilization");
        Require(!observerOne.Civilizations.ContainsKey(3),
            "AI knowledge adapter leaked civilization 3 through civilization 2's private third-party contact");
        Require(observerTwo.Civilizations.ContainsKey(1) && observerTwo.Civilizations.ContainsKey(3),
            "observer 2 did not retain its two legitimately identified contacts");

        var knownTwo = observerOne.Civilizations[2];
        RequireNear(knownTwo.Trust, 0.40,
            "AI diplomatic sentiment did not reflect observer-visible trust minus hostility");
        Require(knownTwo.KnownToBeAtWar,
            "persisted observer-visible war state was not mapped into AI knowledge");
        Require(!knownTwo.HasMilitaryEstimate,
            "Diplomacy adapter fabricated a military-strength estimate");
        RequireNear(knownTwo.EstimatedMilitaryLow, 0.0,
            "missing military intelligence was represented as a nonzero lower estimate");
        RequireNear(knownTwo.EstimatedMilitaryHigh, 0.0,
            "missing military intelligence was represented as a nonzero upper estimate");
        RequireNear(knownTwo.EstimateConfidence, 0.0,
            "Diplomacy contact confidence was incorrectly reused as military-estimate confidence");

        var warAssessment = new StrategicDecisionEvaluator().EvaluateWar(
            CivilizationTraits.Balanced,
            ownKnownMilitaryStrength: 500.0,
            knownTwo,
            nowTick: 40);
        Require(!warAssessment.RecommendWar && warAssessment.IntelligenceConfidence == 0.0,
            "offensive evaluator produced a war recommendation without lawful military intelligence");

        var ownState = new CivilizationOwnState(
            MilitaryStrength: 500.0,
            SupplyCoverageRatio: 1.10,
            IndustryReserve: 800.0,
            ResearchCapacity: 20.0,
            HasAvailableResearch: false,
            HasUnexploredReachableSystems: false,
            HasKnownColonizationOpportunity: false,
            CanBuildInterstellarShips: false,
            HasFleetCapacityShortfall: false);
        var plan = new CivilizationStrategicPlanner(reviewIntervalTicks: 30).GetPlan(
            civilizationId: 1,
            traits: CivilizationTraits.Balanced,
            ownState,
            observerOne,
            nowTick: 40);

        var defense = plan.Priorities.SingleOrDefault(priority => priority.Type == StrategicPriorityType.Defend);
        Require(defense is not null,
            "known active war did not produce defensive strategic pressure when enemy strength was unknown");
        Require(defense!.Reason.Contains("strength remains legitimately unknown", StringComparison.Ordinal),
            "unknown-strength war defense reason did not preserve the intelligence boundary");
        Require(plan.Priorities.Count(priority => priority.Type == StrategicPriorityType.Defend) == 1,
            "known war produced duplicate defensive priorities");
    }

    private static void EstablishMutualContact(DiplomacySimulation diplomacy, int first, int second, long tick)
    {
        diplomacy.ProcessContactOpportunity(CommunicatingContact(first, second, tick));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(second, first, tick));
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"ai-knowledge-{observer}-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: 12,
        Awareness: ContactAwareness.CommunicationAvailable,
        Condition: ContactCondition.Active,
        CommunicationAvailable: true,
        Confidence: 1.0);

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
