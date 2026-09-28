using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Research-side deployment permission query. Research only says an event is scientifically enabled;
/// the owning gameplay subsystem must still create/pay for/perform the physical deployment.
/// </summary>
public static class AdaptiveResearchDeploymentQueries
{
    public static bool IsDeploymentEventEnabled(
        AdaptiveResearchRuntime runtime,
        AdaptiveResearchCivilizationState state,
        string deploymentEventId)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        ArgumentNullException.ThrowIfNull(state);
        if (!runtime.Catalog.DeploymentEvents.TryGetValue(deploymentEventId, out var definition))
            throw new KeyNotFoundException($"Unknown research-enabled deployment event '{deploymentEventId}'.");
        if (state.IsDeploymentEventEnabled(deploymentEventId))
            return true;
        return definition.RequiresAnyMatureTechnologyIds.Any(state.HasEstablishedKnowledge);
    }

    public static IReadOnlyList<string> GetEnabledDeploymentEventIds(
        AdaptiveResearchRuntime runtime,
        AdaptiveResearchCivilizationState state) =>
        runtime.Catalog.DeploymentEvents.Keys
            .Where(id => IsDeploymentEventEnabled(runtime, state, id))
            .OrderBy(id => id, StringComparer.Ordinal)
            .ToArray();
}
