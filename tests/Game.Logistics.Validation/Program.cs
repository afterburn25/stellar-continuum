using Game.Simulation.Economy;
using Game.Simulation.Generation;

namespace Game.Logistics.Validation;

internal static class Program
{
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            ("priority wins shared corridor", ValidatePriorityAndSharedCapacity),
            ("allocation never exceeds supply", ValidateSupplyBound),
            ("disabled corridor leaves demand unmet", ValidateDisabledCorridor),
            ("home-system network is represented and reconstructible", ValidateHomeSystemNetwork),
        };

        var failures = 0;
        foreach (var test in tests)
        {
            try
            {
                test.Run();
                Console.WriteLine($"PASS: {test.Name}");
            }
            catch (Exception ex)
            {
                failures++;
                Console.Error.WriteLine($"FAIL: {test.Name}: {ex.Message}");
            }
        }

        Console.WriteLine($"Logistics flow validation: {tests.Length - failures}/{tests.Length} passed.");
        return failures == 0 ? 0 : 1;
    }

    private static void ValidatePriorityAndSharedCapacity()
    {
        var nodes = new[]
        {
            new LogisticsNode(1, 1, 10, "Source", LogisticsNodeKind.Homeworld),
            new LogisticsNode(2, 1, 10, "Hub", LogisticsNodeKind.OrbitalHub),
            new LogisticsNode(3, 1, 10, "Critical", LogisticsNodeKind.PlanetarySettlement),
            new LogisticsNode(4, 1, 10, "Routine", LogisticsNodeKind.PlanetarySettlement),
        };
        var links = new[]
        {
            new LogisticsLink(1, 1, 1, 2, CapacityPerDay: 10.0, TransitDays: 1.0),
            new LogisticsLink(2, 1, 2, 3, CapacityPerDay: 10.0, TransitDays: 1.0),
            new LogisticsLink(3, 1, 2, 4, CapacityPerDay: 10.0, TransitDays: 1.0),
        };

        var planner = new LogisticsRoutePlanner(nodes, links);
        var allocator = new LogisticsFlowAllocator(planner, links);
        var plan = allocator.AllocateDaily(
            new[] { new LogisticsSupplyOffer(1, 20.0) },
            new[]
            {
                new LogisticsDemand(4, RequiredPerDay: 8.0, Priority: 10),
                new LogisticsDemand(3, RequiredPerDay: 8.0, Priority: 100),
            });

        var critical = plan.Allocations.Where(allocation => allocation.DestinationNodeId == 3).Sum(allocation => allocation.AllocatedPerDay);
        var routine = plan.Allocations.Where(allocation => allocation.DestinationNodeId == 4).Sum(allocation => allocation.AllocatedPerDay);

        Require(Math.Abs(critical - 8.0) < 0.000001, $"critical demand received {critical} instead of 8");
        Require(Math.Abs(routine - 2.0) < 0.000001, $"routine demand received {routine} instead of remaining shared capacity 2");
        Require(Math.Abs(plan.TotalAllocatedPerDay - 10.0) < 0.000001, "shared 10/day corridor was oversubscribed");
        Require(Math.Abs(plan.UnmetDemandPerDay[4] - 6.0) < 0.000001, "routine unmet demand was not reported correctly");
        Require(Math.Abs(plan.UnmetDemandPerDay[3]) < 0.000001, "critical demand should have been fully served first");
    }

    private static void ValidateSupplyBound()
    {
        var nodes = new[]
        {
            new LogisticsNode(1, 1, 10, "Source", LogisticsNodeKind.Homeworld),
            new LogisticsNode(2, 1, 10, "Demand", LogisticsNodeKind.PlanetarySettlement),
        };
        var links = new[]
        {
            new LogisticsLink(1, 1, 1, 2, CapacityPerDay: 100.0, TransitDays: 1.0),
        };

        var allocator = new LogisticsFlowAllocator(new LogisticsRoutePlanner(nodes, links), links);
        var plan = allocator.AllocateDaily(
            new[] { new LogisticsSupplyOffer(1, 3.5) },
            new[] { new LogisticsDemand(2, RequiredPerDay: 12.0, Priority: 100) });

        Require(Math.Abs(plan.TotalAllocatedPerDay - 3.5) < 0.000001, "allocator moved more than available source supply");
        Require(Math.Abs(plan.UnmetDemandPerDay[2] - 8.5) < 0.000001, "supply-limited unmet demand was incorrect");
        Require(Math.Abs(plan.UnusedSupplyPerDay[1]) < 0.000001, "fully consumed source still reported unused supply");
    }

    private static void ValidateDisabledCorridor()
    {
        var nodes = new[]
        {
            new LogisticsNode(1, 1, 10, "Source", LogisticsNodeKind.Homeworld),
            new LogisticsNode(2, 1, 10, "Demand", LogisticsNodeKind.PlanetarySettlement),
        };
        var links = new[]
        {
            new LogisticsLink(1, 1, 1, 2, CapacityPerDay: 50.0, TransitDays: 1.0, Enabled: false),
        };

        var allocator = new LogisticsFlowAllocator(new LogisticsRoutePlanner(nodes, links), links);
        var plan = allocator.AllocateDaily(
            new[] { new LogisticsSupplyOffer(1, 10.0) },
            new[] { new LogisticsDemand(2, RequiredPerDay: 7.0, Priority: 100) });

        Require(plan.Allocations.Count == 0, "allocator moved cargo across a disabled corridor");
        Require(Math.Abs(plan.UnmetDemandPerDay[2] - 7.0) < 0.000001, "disabled corridor did not leave demand unmet");
        Require(Math.Abs(plan.UnusedSupplyPerDay[1] - 10.0) < 0.000001, "disabled corridor consumed source supply");
    }

    private static void ValidateHomeSystemNetwork()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4C4F_4749_5354_4943L,
            new GalaxyGenerationSettings
            {
                SystemCount = 40,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 460.0f,
            });

        var civilization = galaxy.Civilizations
            .Where(candidate => !candidate.IsSeededAncient)
            .OrderBy(candidate => candidate.Id)
            .First();
        var construction = galaxy.ConstructionStates.Single(state => state.CivilizationId == civilization.Id);
        construction.CompletedProjectIds.Clear();

        var homeColonies = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id && colony.SystemId == civilization.HomeSystemId)
            .OrderBy(colony => colony.Id)
            .ToArray();
        Require(homeColonies.Length > 0, "validation civilization did not have a represented home-system colony");

        var populationsBefore = homeColonies.Select(colony => colony.PopulationMillions).ToArray();
        var economy = galaxy.Economies.Single(state => state.CivilizationId == civilization.Id);
        var creditsBefore = economy.Credits;
        var industryBefore = economy.Industry;
        var scienceBefore = economy.Science;

        IHomeSystemLogisticsNetworkView view = new PrototypeHomeSystemLogisticsNetworkView();
        var first = view.Build(galaxy, civilization.Id);
        var second = view.Build(galaxy, civilization.Id);

        Require(first.Nodes.SequenceEqual(second.Nodes), "identical represented state produced different logistics nodes");
        Require(first.Links.SequenceEqual(second.Links), "identical represented state produced different logistics links");
        Require(first.SupplyOffers.SequenceEqual(second.SupplyOffers), "identical represented state produced different logistics supply offers");
        Require(first.Demands.SequenceEqual(second.Demands), "identical represented state produced different logistics demands");
        Require(first.DailyFlow.Allocations.SequenceEqual(second.DailyFlow.Allocations), "identical represented state produced different daily logistics allocations");
        Require(Math.Abs(first.TotalAllocatedPerDay - second.TotalAllocatedPerDay) < 0.000001,
            "identical represented state produced different aggregate logistics flow");

        Require(first.Nodes.Count == homeColonies.Length,
            "network invented infrastructure/settlements when no orbital projects were completed");
        Require(first.Nodes.All(node => node.Kind is LogisticsNodeKind.Homeworld or LogisticsNodeKind.PlanetarySettlement),
            "network created a non-settlement node without represented orbital infrastructure");
        var representedColonyNames = homeColonies.Select(colony => colony.Name).ToHashSet(StringComparer.Ordinal);
        Require(first.Nodes.All(node => representedColonyNames.Contains(node.Name)),
            "network invented a settlement that is absent from authoritative colony state");

        Require(economy.Credits == creditsBefore && economy.Industry == industryBefore && economy.Science == scienceBefore,
            "reconstructible logistics network mutated economy resources");
        for (var i = 0; i < homeColonies.Length; i++)
            Require(homeColonies[i].PopulationMillions == populationsBefore[i], "reconstructible logistics network mutated colony population");

        construction.CompletedProjectIds.Add("orbital_launch_complex");
        var launchNetwork = view.Build(galaxy, civilization.Id);
        Require(launchNetwork.Nodes.Count(node => node.Kind == LogisticsNodeKind.OrbitalHub) == 1,
            "completed orbital launch complex did not create exactly one orbital logistics hub");
        Require(launchNetwork.Nodes.All(node => node.Kind != LogisticsNodeKind.Shipyard),
            "orbital launch complex fabricated a shipyard node");

        construction.CompletedProjectIds.Add("orbital_shipyard");
        var shipyardNetwork = view.Build(galaxy, civilization.Id);
        Require(shipyardNetwork.Nodes.Count(node => node.Kind == LogisticsNodeKind.OrbitalHub) == 1,
            "orbital shipyard state changed the single-hub invariant");
        Require(shipyardNetwork.Nodes.Count(node => node.Kind == LogisticsNodeKind.Shipyard) == 1,
            "completed orbital shipyard did not create exactly one shipyard node");
        Require(shipyardNetwork.Links.Count > launchNetwork.Links.Count,
            "represented shipyard did not add a logistics corridor to the orbital network");

        construction.CompletedProjectIds.Add("asteroid_resource_network");
        var resourceNetwork = view.Build(galaxy, civilization.Id);
        Require(resourceNetwork.Nodes.Count(node => node.Kind == LogisticsNodeKind.ResourceSite) == 1,
            "completed asteroid network did not create exactly one resource-site node");
        Require(resourceNetwork.Links.Count == shipyardNetwork.Links.Count + 1,
            "represented asteroid network did not connect to the orbital logistics hub");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
