using System.Collections.Generic;

namespace Game.Simulation.Construction;

public sealed record ConstructionProjectDefinition(
    string Id,
    string Name,
    string Description,
    double IndustryCost,
    IReadOnlyList<string> RequiredTechnologies,
    ConstructionCategory Category,
    double CreditCost = 0.0,
    double IndustryPerDay = 0.0,
    double UpkeepCreditsPerDay = 0.0,
    IReadOnlyList<string>? RequiredProjects = null
);

public enum ConstructionCategory
{
    Science,
    Industry,
    Orbital,
    Ftl,
}
