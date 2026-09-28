using System;
using System.Linq;
using Game.Campaign;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class OrbitalEconomyValidation
{
    public static void Run()
    {
        const long seed = 20260908;
        var generator = new GalaxyGenerator();
        var baseline = generator.Generate(seed);
        var galaxy = generator.Generate(seed);
        var player = galaxy.PlayerCivilizationId;
        var construction = galaxy.ConstructionStates.Single(item => item.CivilizationId == player);
        var technology = galaxy.Technologies.Single(item => item.CivilizationId == player);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        var project = ConstructionRegistry.Get("asteroid_resource_network");

        Require(project.CreditCost == 320 && project.IndustryCost == 1800 &&
            project.IndustryPerDay == 1.50 && project.UpkeepCreditsPerDay == .18,
            "asteroid network lost its explicit capital, output, or operating-cost contract");
        Require(ConstructionRegistry.Get("orbital_launch_complex").UpkeepCreditsPerDay == .08 &&
            ConstructionRegistry.Get("orbital_shipyard").UpkeepCreditsPerDay == .12 &&
            ConstructionRegistry.Get("warp_test_facility").UpkeepCreditsPerDay == .15,
            "strategic orbital facility upkeep changed outside the economic contract");
        Require(!ConstructionRegistry.GetAvailable(construction, technology).Any(item => item.Id == project.Id),
            "asteroid extraction was available before orbital-industry knowledge and launch infrastructure");
        var lockedOrder = new ConstructionSimulation().StartProject(galaxy, player, project.Id);
        Require(!lockedOrder.Accepted && lockedOrder.Message.Contains("Orbital Industry", StringComparison.Ordinal) &&
            lockedOrder.Message.Contains("Orbital Launch Complex", StringComparison.Ordinal),
            "locked asteroid order did not explain both missing prerequisites");
        technology.CompletedTechnologyIds.Add("orbital_industry");
        var beforeLaunch = ConstructionRegistry.GetAvailable(construction, technology);
        Require(!beforeLaunch.Any(item => item.Id is "asteroid_resource_network" or "orbital_shipyard"),
            "shipyard or asteroid extraction ignored its launch-complex prerequisite");
        lockedOrder = new ConstructionSimulation().StartProject(galaxy, player, project.Id);
        Require(!lockedOrder.Accepted && !lockedOrder.Message.Contains("Orbital Industry", StringComparison.Ordinal) &&
            lockedOrder.Message.Contains("Orbital Launch Complex", StringComparison.Ordinal),
            "partially unlocked asteroid order did not report only the remaining prerequisite");
        construction.CompletedProjectIds.Add("orbital_launch_complex");
        var afterLaunch = ConstructionRegistry.GetAvailable(construction, technology);
        Require(afterLaunch.Any(item => item.Id == project.Id) &&
            afterLaunch.Any(item => item.Id == "orbital_shipyard"),
            "shipyard or asteroid extraction did not unlock after represented prerequisites");

        economy.Credits = 1000;
        var order = new ConstructionSimulation().StartProject(galaxy, player, project.Id);
        Require(order.Accepted && economy.Credits == 680 && construction.ActiveProjectId == project.Id,
            "asteroid extraction authorization did not charge or start exactly once");
        economy.Industry = project.IndustryCost;
        new ConstructionSimulation().AdvanceForCivilization(galaxy, player, project.IndustryCost, 0.25);
        Require(construction.ActiveProjectId == project.Id && construction.ActiveProjectProgress < project.IndustryCost,
            "stockpiled materials bypassed orbital construction time");
        new ConstructionSimulation().AdvanceForCivilization(galaxy, player, project.IndustryCost,
            project.IndustryCost / ConstructionSimulation.IndustryPerDay);
        Require(construction.CompletedProjectIds.Contains(project.Id) && construction.ActiveProjectId is null,
            "funded asteroid extraction did not complete through ordinary construction");

        var baselineFlow = EconomySimulation.GetCreditFlow(baseline, baseline.PlayerCivilizationId);
        var developedFlow = EconomySimulation.GetCreditFlow(galaxy, player);
        Near(developedFlow.OrbitalMaintenancePerDay, .26,
            "launch and extraction maintenance were not represented in daily cash flow");
        Near(developedFlow.NetCreditsPerDay, baselineFlow.NetCreditsPerDay - .26,
            "orbital maintenance did not reduce the authoritative net credit rate");

        new EconomySimulation().Advance(baseline, 1);
        new EconomySimulation().Advance(galaxy, 1);
        var baselineEconomy = baseline.Economies.Single(item => item.CivilizationId == baseline.PlayerCivilizationId);
        Near(economy.LastIndustryPerSecond, baselineEconomy.LastIndustryPerSecond + 1.50,
            "completed asteroid extraction did not add its bounded industrial throughput");

        var network = new PrototypeHomeSystemLogisticsNetworkView().Build(galaxy, player);
        Require(network.Nodes.Count(item => item.Kind == LogisticsNodeKind.ResourceSite) == 1 &&
            network.Links.Any(link => network.Nodes.Single(node => node.Id == link.FromNodeId).Kind == LogisticsNodeKind.ResourceSite ||
                network.Nodes.Single(node => node.Id == link.ToNodeId).Kind == LogisticsNodeKind.ResourceSite),
            "completed asteroid extraction was absent from the reconstructible logistics network");

        var guideGalaxy = generator.Generate(seed);
        var guidePlayer = guideGalaxy.PlayerCivilizationId;
        var guideTechnology = guideGalaxy.Technologies.Single(item => item.CivilizationId == guidePlayer);
        var guideConstruction = guideGalaxy.ConstructionStates.Single(item => item.CivilizationId == guidePlayer);
        guideTechnology.CompletedTechnologyIds.Add("orbital_industry");
        guideConstruction.CompletedProjectIds.Add("research_network");
        guideConstruction.CompletedProjectIds.Add("industrial_automation");
        guideConstruction.CompletedProjectIds.Add("orbital_launch_complex");
        guideConstruction.CompletedProjectIds.Add("orbital_shipyard");
        var guide = DemoObjectiveView.Build(guideGalaxy, 1);
        Require(guide.Construction.Contains("Optional build: Asteroid Resource Network", StringComparison.Ordinal) &&
            guide.Construction.Contains("1.50 Industry/day", StringComparison.Ordinal) &&
            guide.Construction.Contains("−$1.8M UED/day", StringComparison.Ordinal),
            "graphical guide did not expose the optional extraction tradeoff between core projects");
    }

    private static void Near(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > 0.000001)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
