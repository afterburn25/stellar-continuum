using Game.Simulation.Models;
using Game.Simulation.Territory;
using Game.Validation;

namespace Game.Territory.Validation;

internal static class TerritorialStrategicPlannerValidation
{
    [RegressionCheck]
    private static void PlannerRefusesUnaffordableOrUnavailableExpansion()
    {
        foreach (var mutate in new Action<GalaxyState, int>[]
        {
            (galaxy, owner) => galaxy.Economies.Single(e => e.CivilizationId == owner).Credits = 1,
            (galaxy, owner) => galaxy.Technologies.Single(t => t.CivilizationId == owner).CompletedTechnologyIds.Clear(),
            (galaxy, owner) => ReplaceCivilization(galaxy, owner, c => c with { IsSeededAncient = true }),
            (galaxy, owner) => ReplaceCivilization(galaxy, owner, c => c with { ExpansionAllowed = false }),
        })
        {
            var galaxy = Prepared(); var owner = TerritoryScenario.Owner(galaxy); mutate(galaxy, owner);
            var decision = TerritorialStrategicPlanner.Assess(galaxy, owner);
            Require(decision.SystemId is null && decision.Kind is null && !decision.NeedsLogisticsShip,
                "planner proposed territorial spending despite an unavailable capability, policy, or treasury");
        }
    }

    [RegressionCheck]
    private static void PlannerRequestsBuilderThenRoutesAndPaysForARealProject()
    {
        var needsBuilder = Prepared(); var owner = TerritoryScenario.Owner(needsBuilder);
        var request = TerritorialStrategicPlanner.Assess(needsBuilder, owner);
        Require(request.SystemId is not null && request.Kind is not null && request.NeedsLogisticsShip,
            "a worthwhile supported frontier did not ask the strategic director for a logistics vessel: " + request.Reason);
        Require(!TerritorialStrategicPlanner.Execute(needsBuilder, request).Accepted,
            "planner spent capital when it only requested a missing logistics ship");

        var galaxy = Prepared(); owner = TerritoryScenario.Owner(galaxy); var home = TerritoryScenario.System(galaxy, 0);
        var builder = TerritoryScenario.Fleet(galaxy, owner, home, FleetRole.Logistics); galaxy.Fleets.Add(builder);
        var decision = TerritorialStrategicPlanner.Assess(galaxy, owner);
        Require(decision.SystemId is not null && decision.Kind is not null && !decision.NeedsLogisticsShip,
            "an available logistics ship did not make a viable frontier project executable");
        var economy = galaxy.Economies.Single(e => e.CivilizationId == owner); var credits = economy.Credits; var industry = economy.Industry;
        var first = TerritorialStrategicPlanner.Execute(galaxy, decision);
        Require(first.Accepted, "strategic planner could not issue a valid logistics route or build order");
        if (builder.CurrentSystemId != decision.SystemId)
        {
            // Arrival is normally performed by the travel simulation; this keeps the command
            // validation focused on the planner's second, paid build decision.
            builder.CurrentSystemId = decision.SystemId; builder.DestinationSystemId = null; builder.TransitPhase = FleetTransitPhase.None;
            Require(TerritorialStrategicPlanner.Execute(galaxy, decision).Accepted, "arrived logistics ship could not start the selected regional project");
        }
        Require(galaxy.Territory!.Installations.Count == 1 && economy.Credits < credits && economy.Industry < industry,
            "planner did not create exactly one paid territorial construction site");
        var repeat = TerritorialStrategicPlanner.Assess(galaxy, owner);
        Require(repeat.SystemId is null && repeat.Reason.Contains("existing", StringComparison.OrdinalIgnoreCase),
            "planner proposed another project while an owned regional project was already active");
    }

    private static GalaxyState Prepared()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy);
        ReplaceCivilization(galaxy, owner, c => c with { DevelopmentStage = CivilizationDevelopmentStage.WarpCapable, IsSeededAncient = false, ExpansionAllowed = true });
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, TerritoryScenario.System(galaxy, 0)));
        TerritorialRuntime.Initialize(galaxy);
        return galaxy;
    }
    private static void ReplaceCivilization(GalaxyState galaxy, int owner, Func<CivilizationState, CivilizationState> transform)
    {
        var index = galaxy.Civilizations.ToList().FindIndex(c => c.Id == owner); galaxy.Civilizations[index] = transform(galaxy.Civilizations[index]);
    }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
