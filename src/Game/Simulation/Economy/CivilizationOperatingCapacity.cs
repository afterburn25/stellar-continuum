using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

/// <summary>Shared effective capacity for services paid from the base operating budget.</summary>
public static class CivilizationOperatingCapacity
{
    public static double GetFundingFraction(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var value = galaxy.Economies
            .FirstOrDefault(economy => economy.CivilizationId == civilizationId)
            ?.LastBaseOperationsFundingFraction ?? 1.0;
        return double.IsFinite(value) ? Math.Clamp(value, 0.0, 1.0) : 0.0;
    }
}
