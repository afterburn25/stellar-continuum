using System.Collections.Generic;
using System.Linq;
using System.Text;
using Game.Simulation.Economy;

namespace Game.Presentation;

public sealed record UiLogisticsNodeSnapshot(int NodeId, string Name, string Kind,
    double SupplyPerDay, double DemandPerDay, double DeliveredPerDay, string Status);
public sealed record UiHomeSystemLogisticsSnapshot(string SystemName, int CorridorCount,
    double SupplyPerDay, double DemandPerDay, double DeliveredPerDay, double ShortfallPerDay,
    UiLogisticsNodeSnapshot[] Nodes);

/// <summary>
/// Read-only player-facing logistics presentation adapter. Authoritative logistics
/// calculations stay in the economy/logistics subsystem.
/// </summary>
public partial class Main
{
    private readonly IEconomyLogisticsView _economyLogisticsView = new PrototypeEconomyLogisticsView();
    private readonly IHomeSystemLogisticsNetworkView _homeSystemLogisticsView = new PrototypeHomeSystemLogisticsNetworkView();

    public string UiLogisticsSummary
    {
        get
        {
            if (_galaxy is null)
                return "Supply initializing…";

            var logistics = _economyLogisticsView.GetSnapshot(_galaxy, _galaxy.PlayerCivilizationId);
            var localCoverage = logistics.TotalSupportDemandPerDay <= 0.0
                ? 1.0
                : logistics.TotalLocalSupportCapacityPerDay / logistics.TotalSupportDemandPerDay;

            return $"Supply {logistics.Condition} · effective {logistics.EffectiveCoverageRatio:P0} · "
                 + $"local {localCoverage:P0} · imports {logistics.ImportRequirementPerDay:0.00}/day · "
                 + $"cargo {logistics.CargoHandlingCapacityPerDay:0.00}/day · "
                 + $"strained {logistics.StrainedColonyCount} · critical {logistics.CriticalColonyCount}";
        }
    }

    public string UiHomeSystemLogisticsDetails
    {
        get
        {
            if (_galaxy is null)
                return "Campaign logistics are initializing…";

            var network = _homeSystemLogisticsView.Build(_galaxy, _galaxy.PlayerCivilizationId);
            var systemName = _galaxy.Systems.FirstOrDefault(system => system.Id == network.HomeSystemId)?.Name
                ?? $"System {network.HomeSystemId}";

            var builder = new StringBuilder();
            builder.AppendLine(systemName);
            builder.Append("Nodes: ").Append(network.Nodes.Count)
                .Append(" · corridors: ").AppendLine(network.Links.Count.ToString());
            builder.Append("Supply offered: ").Append(network.TotalSupplyOfferedPerDay.ToString("0.00"))
                .Append("/day · demand: ").Append(network.TotalDemandPerDay.ToString("0.00")).AppendLine("/day");
            builder.Append("Allocated: ").Append(network.TotalAllocatedPerDay.ToString("0.00"))
                .Append("/day · unmet: ").Append(network.TotalUnmetDemandPerDay.ToString("0.00")).AppendLine("/day");

            if (network.Nodes.Count == 0)
            {
                builder.Append("No represented logistics nodes.");
                return builder.ToString();
            }

            builder.AppendLine("Network:");
            foreach (var node in network.Nodes.Take(6))
                builder.Append("• ").Append(node.Kind).Append(" — ").AppendLine(node.Name);

            if (network.Nodes.Count > 6)
                builder.Append("• +").Append(network.Nodes.Count - 6).Append(" more nodes");

            return builder.ToString().TrimEnd();
        }
    }

    public UiHomeSystemLogisticsSnapshot UiHomeSystemLogistics
    {
        get
        {
            if (_galaxy is null)
                return new("Initializing", 0, 0, 0, 0, 0, System.Array.Empty<UiLogisticsNodeSnapshot>());

            var network = _homeSystemLogisticsView.Build(_galaxy, _galaxy.PlayerCivilizationId);
            var systemName = _galaxy.Systems.FirstOrDefault(system => system.Id == network.HomeSystemId)?.Name
                ?? $"System {network.HomeSystemId}";
            var supplies = network.SupplyOffers.GroupBy(offer => offer.NodeId)
                .ToDictionary(group => group.Key, group => group.Sum(offer => offer.AvailablePerDay));
            var demands = network.Demands.GroupBy(demand => demand.NodeId)
                .ToDictionary(group => group.Key, group => group.Sum(demand => demand.RequiredPerDay));
            var delivered = network.DailyFlow.Allocations.GroupBy(allocation => allocation.DestinationNodeId)
                .ToDictionary(group => group.Key, group => group.Sum(allocation => allocation.AllocatedPerDay));
            var nodes = network.Nodes.Take(8).Select(node =>
            {
                var supply = supplies.GetValueOrDefault(node.Id);
                var demand = demands.GetValueOrDefault(node.Id);
                var received = delivered.GetValueOrDefault(node.Id);
                var status = demand <= 0 ? "Supply node" : received + 0.0001 >= demand ? "Fully supplied" : "Shortfall";
                return new UiLogisticsNodeSnapshot(node.Id, node.Name, FormatNodeKind(node.Kind),
                    supply, demand, received, status);
            }).ToArray();
            return new UiHomeSystemLogisticsSnapshot(systemName, network.Links.Count,
                network.TotalSupplyOfferedPerDay, network.TotalDemandPerDay, network.TotalAllocatedPerDay,
                network.TotalUnmetDemandPerDay, nodes);
        }
    }

    private static string FormatNodeKind(LogisticsNodeKind kind) => kind switch
    {
        LogisticsNodeKind.Homeworld => "Homeworld",
        LogisticsNodeKind.OrbitalHub => "Orbital hub",
        LogisticsNodeKind.LunarSettlement => "Lunar settlement",
        LogisticsNodeKind.PlanetarySettlement => "Planetary settlement",
        LogisticsNodeKind.ResourceSite => "Resource site",
        LogisticsNodeKind.Depot => "Depot",
        LogisticsNodeKind.Shipyard => "Shipyard",
        _ => kind.ToString(),
    };
}
