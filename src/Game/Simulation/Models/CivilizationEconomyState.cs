namespace Game.Simulation.Models;

public enum IndustryPriority
{
    Balanced,
    InfrastructureFirst,
    ShipbuildingFirst,
}

public sealed class CivilizationEconomyState
{
    public required int CivilizationId { get; init; }
    public double Credits { get; set; } = 500.0;
    public double Industry { get; set; } = 200.0;
    public double Science { get; set; }
    public double LastCreditsPerSecond { get; set; }
    public double LastIndustryPerSecond { get; set; }
    public double LastSciencePerSecond { get; set; }
    public double LastResearchSpendingPerDay { get; set; }
    public double LastResearchFundingFraction { get; set; } = 1.0;
    public double OperatingArrears { get; set; }
    public double LastBaseOperationsFundingFraction { get; set; } = 1.0;
    /// <summary>Null preserves the civilization's existing strategic allocation policy.</summary>
    public IndustryPriority? IndustryPriority { get; set; }
}
