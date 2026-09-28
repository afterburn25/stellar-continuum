using System;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

public sealed record ColonyLaborSnapshot(
    double PopulationMillions,
    double WorkingAgePopulationMillions,
    double EmployedPopulationMillions,
    double UnemployedPopulationMillions,
    double EmploymentRate);

/// <summary>Early labor-backed tax base shared by Player and AI economies.</summary>
public static class ColonyLaborEconomy
{
    public const double WorkingAgePopulationFraction = 0.45;
    public const double BaselineEmploymentRate = 0.775;

    public static ColonyLaborSnapshot GetSnapshot(
        ColonyState colony,
        bool industrialAutomation = false,
        double additionalRepresentedJobsMillions = 0.0)
    {
        ArgumentNullException.ThrowIfNull(colony);
        var population = Math.Max(0.0, colony.PopulationMillions);
        var workingAge = population * WorkingAgePopulationFraction;
        var infrastructure = Math.Clamp(colony.Infrastructure, 0.1, 5.0);
        var jobCapacityRate = Math.Clamp(0.70 + 0.075 * infrastructure +
            (industrialAutomation ? 0.05 : 0.0), 0.0, 0.98);
        var baselineJobs = workingAge * jobCapacityRate;
        var employed = Math.Min(workingAge,
            baselineJobs + Math.Max(0.0, additionalRepresentedJobsMillions));
        return new(population, workingAge, employed, Math.Max(0.0, workingAge - employed),
            workingAge <= 0.0 ? 0.0 : employed / workingAge);
    }
}
