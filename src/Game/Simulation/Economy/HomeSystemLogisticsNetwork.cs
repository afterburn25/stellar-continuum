using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

/// <summary>
/// Reconstructible strategic logistics network for the civilization's represented home-system
/// settlements and orbital infrastructure. It does not invent settlements that are not present
/// in authoritative state and is intentionally not persisted.
/// </summary>
public sealed record HomeSystemLogisticsNetwork(
    int CivilizationId,
    int HomeSystemId,
    IReadOnlyList<LogisticsNode> Nodes,
    IReadOnlyList<LogisticsLink> Links,
    IReadOnlyList<LogisticsSupplyOffer> SupplyOffers,
    IReadOnlyList<LogisticsDemand> Demands,
    LogisticsFlowPlan DailyFlow
)
{
    public double TotalDemandPerDay => Demands.Sum(demand => demand.RequiredPerDay);
    public double TotalSupplyOfferedPerDay => SupplyOffers.Sum(offer => offer.AvailablePerDay);
    public double TotalAllocatedPerDay => DailyFlow.TotalAllocatedPerDay;
    public double TotalUnmetDemandPerDay => DailyFlow.TotalUnmetDemandPerDay;
}

public interface IHomeSystemLogisticsNetworkView
{
    HomeSystemLogisticsNetwork Build(GalaxyState galaxy, int civilizationId);
}

/// <summary>
/// Early-release adapter that translates the existing colony/construction/economy prototype
/// into the generic logistics node/link/flow contracts. As richer 2050 Moon/Mars/orbital
/// settlement state is added by its owning systems, those real settlements can appear here
/// without changing route/flow architecture.
/// </summary>
public sealed class PrototypeHomeSystemLogisticsNetworkView : IHomeSystemLogisticsNetworkView
{
    private readonly IEconomyLogisticsView _logisticsView;

    public PrototypeHomeSystemLogisticsNetworkView(IEconomyLogisticsView? logisticsView = null)
    {
        _logisticsView = logisticsView ?? new PrototypeEconomyLogisticsView();
    }

    public HomeSystemLogisticsNetwork Build(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        var civilization = galaxy.Civilizations.FirstOrDefault(candidate => candidate.Id == civilizationId)
            ?? throw new InvalidOperationException($"Unknown civilization {civilizationId}.");
        var construction = galaxy.ConstructionStates.FirstOrDefault(candidate => candidate.CivilizationId == civilizationId)
            ?? throw new InvalidOperationException($"Civilization {civilizationId} has no construction state.");
        var logistics = _logisticsView.GetSnapshot(galaxy, civilizationId);

        var homeColonies = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId && colony.SystemId == civilization.HomeSystemId)
            .OrderBy(colony => colony.Id)
            .ToArray();

        var nodes = new List<LogisticsNode>();
        var links = new List<LogisticsLink>();
        var offers = new List<LogisticsSupplyOffer>();
        var demands = new List<LogisticsDemand>();
        var colonyNodeIds = new Dictionary<int, int>();
        var nextNodeId = 1;
        var nextLinkId = 1;

        for (var index = 0; index < homeColonies.Length; index++)
        {
            var colony = homeColonies[index];
            var nodeId = nextNodeId++;
            colonyNodeIds[colony.Id] = nodeId;
            nodes.Add(new LogisticsNode(
                nodeId,
                civilizationId,
                civilization.HomeSystemId,
                colony.Name,
                index == 0 ? LogisticsNodeKind.Homeworld : LogisticsNodeKind.PlanetarySettlement));

            var colonyLogistics = logistics.Colonies.First(snapshot => snapshot.ColonyId == colony.Id);
            var localSurplus = Math.Max(0.0, colonyLogistics.LocalSupportCapacityPerDay - colonyLogistics.SupportDemandPerDay);
            if (localSurplus > 0.0)
                offers.Add(new LogisticsSupplyOffer(nodeId, localSurplus));

            if (colonyLogistics.ImportedSupportRequiredPerDay > 0.0)
            {
                demands.Add(new LogisticsDemand(
                    nodeId,
                    colonyLogistics.ImportedSupportRequiredPerDay,
                    PriorityFor(colonyLogistics.Condition)));
            }
        }

        int? orbitalHubNodeId = null;
        if (construction.CompletedProjectIds.Contains("orbital_launch_complex")
            || construction.CompletedProjectIds.Contains("orbital_shipyard"))
        {
            orbitalHubNodeId = nextNodeId++;
            nodes.Add(new LogisticsNode(
                orbitalHubNodeId.Value,
                civilizationId,
                civilization.HomeSystemId,
                "Home-System Orbital Logistics Hub",
                LogisticsNodeKind.OrbitalHub));
        }

        int? shipyardNodeId = null;
        if (construction.CompletedProjectIds.Contains("orbital_shipyard"))
        {
            shipyardNodeId = nextNodeId++;
            nodes.Add(new LogisticsNode(
                shipyardNodeId.Value,
                civilizationId,
                civilization.HomeSystemId,
                "Orbital Shipyard",
                LogisticsNodeKind.Shipyard));
        }

        int? resourceNodeId = null;
        if (construction.CompletedProjectIds.Contains("asteroid_resource_network"))
        {
            resourceNodeId = nextNodeId++;
            nodes.Add(new LogisticsNode(
                resourceNodeId.Value,
                civilizationId,
                civilization.HomeSystemId,
                "Asteroid Resource Network",
                LogisticsNodeKind.ResourceSite));
        }

        if (homeColonies.Length > 0 && orbitalHubNodeId is { } hubId)
        {
            var handlingCapacity = Math.Max(0.10, logistics.CargoHandlingCapacityPerDay);
            foreach (var colony in homeColonies)
            {
                links.Add(new LogisticsLink(
                    nextLinkId++,
                    civilizationId,
                    colonyNodeIds[colony.Id],
                    hubId,
                    handlingCapacity,
                    TransitDays: 0.10,
                    Bidirectional: true));
            }

            if (shipyardNodeId is { } yardId)
            {
                links.Add(new LogisticsLink(
                    nextLinkId++,
                    civilizationId,
                    hubId,
                    yardId,
                    handlingCapacity,
                    TransitDays: 0.03,
                    Bidirectional: true));
            }
            if (resourceNodeId is { } resourceId)
            {
                links.Add(new LogisticsLink(
                    nextLinkId++,
                    civilizationId,
                    resourceId,
                    hubId,
                    handlingCapacity,
                    TransitDays: 0.35,
                    Bidirectional: true));
            }
        }

        // Without a represented orbital hub, multiple settlements still need a coarse local
        // surface/interplanetary connection in the prototype. This fallback disappears naturally
        // once physical transport infrastructure is represented explicitly.
        if (orbitalHubNodeId is null && homeColonies.Length > 1)
        {
            var fallbackCapacity = Math.Max(0.05, logistics.CargoHandlingCapacityPerDay * 0.35);
            var anchorNodeId = colonyNodeIds[homeColonies[0].Id];
            foreach (var colony in homeColonies.Skip(1))
            {
                links.Add(new LogisticsLink(
                    nextLinkId++,
                    civilizationId,
                    anchorNodeId,
                    colonyNodeIds[colony.Id],
                    fallbackCapacity,
                    TransitDays: 0.75,
                    Bidirectional: true));
            }
        }

        var routePlanner = new LogisticsRoutePlanner(nodes, links, cacheCapacity: Math.Max(8, nodes.Count * 2));
        var flowAllocator = new LogisticsFlowAllocator(routePlanner, links);
        var dailyFlow = flowAllocator.AllocateDaily(offers, demands);

        return new HomeSystemLogisticsNetwork(
            civilizationId,
            civilization.HomeSystemId,
            nodes,
            links,
            offers,
            demands,
            dailyFlow);
    }

    private static int PriorityFor(SupplyCondition condition) => condition switch
    {
        SupplyCondition.Critical => 100,
        SupplyCondition.Strained => 50,
        _ => 10,
    };
}
