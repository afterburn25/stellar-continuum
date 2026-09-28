using Game.Simulation.Industry;
using Game.Simulation.Models;

namespace Game.Presentation;

public sealed record UiIndustryPrioritySnapshot(IndustryPriority Priority, string DisplayName, double ConstructionWeight,
    double ShipbuildingWeight, double LastConstructionAllocated, double LastShipbuildingAllocated, bool HasLastAllocation);

public partial class Main
{
    private CivilizationIndustryAllocation? _lastPlayerIndustryAllocation;
    public UiIndustryPrioritySnapshot UiIndustryPriority => _galaxy is null
        ? Weights(IndustryPriority.Balanced)
        : _lastPlayerIndustryAllocation is { } last
        ? new(PlayerEconomy.IndustryPriority ?? IndustryPriority.Balanced, Display(PlayerEconomy.IndustryPriority ?? IndustryPriority.Balanced), last.ConstructionWeight, last.ShipbuildingWeight,
            last.ConstructionAllocated, last.ShipbuildingAllocated, true)
        : Weights(PlayerEconomy.IndustryPriority ?? IndustryPriority.Balanced);

    public void UiSetIndustryPriority(IndustryPriority priority)
    {
        if (_galaxy is null)
        {
            SetStatus("Industry priority is unavailable while the campaign initializes.", 5);
            return;
        }
        var result = IndustryPriorityCommands.Set(_galaxy, _galaxy.PlayerCivilizationId, _galaxy.PlayerCivilizationId, priority);
        if (result.Accepted) _lastPlayerIndustryAllocation = null;
        SetStatus(result.Message, 5);
        if (result.Accepted) PublishPlayerNotification("Economy", result.Message);
        QueueRedraw();
    }

    private static UiIndustryPrioritySnapshot Weights(IndustryPriority priority) => priority switch
    {
        IndustryPriority.InfrastructureFirst => new(priority, Display(priority), 3, 1, 0, 0, false),
        IndustryPriority.ShipbuildingFirst => new(priority, Display(priority), 1, 3, 0, 0, false),
        _ => new(IndustryPriority.Balanced, Display(IndustryPriority.Balanced), 1, 1, 0, 0, false),
    };
    private static string Display(IndustryPriority priority) => priority switch
    {
        IndustryPriority.InfrastructureFirst => "Infrastructure first",
        IndustryPriority.ShipbuildingFirst => "Shipbuilding first",
        _ => "Balanced",
    };
}
