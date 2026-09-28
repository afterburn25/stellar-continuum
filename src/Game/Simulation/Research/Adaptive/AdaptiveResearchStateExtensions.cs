using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

internal static class AdaptiveResearchStateExtensions
{
    internal static void SetFacilityCapabilities(
        this AdaptiveResearchCivilizationState state,
        IEnumerable<string> desiredCapabilities)
    {
        var desired = desiredCapabilities.ToHashSet(StringComparer.Ordinal);
        foreach (var existing in state.FacilityCapabilities.ToArray())
            if (!desired.Contains(existing))
                state.RemoveFacilityCapability(existing);
        foreach (var capability in desired)
            state.AddFacilityCapability(capability);
    }
}
