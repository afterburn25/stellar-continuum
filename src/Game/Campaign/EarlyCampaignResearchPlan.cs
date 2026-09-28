using System;
using System.Collections.Generic;

namespace Game.Campaign;

/// <summary>
/// Player-facing priorities for the first interstellar capability. This does not unlock,
/// start or complete research; normal Adaptive Research eligibility remains authoritative.
/// </summary>
public static class EarlyCampaignResearchPlan
{
    public static IReadOnlyList<string> WarpCapabilityPath { get; } = new[]
    {
        "in_space_assembly",
        "asteroid_prospecting",
        "asteroid_mining",
        "vacuum_refining",
        "orbital_manufacturing",
        "orbital_shipyard",
        "gravitational_physics",
        "field_theory",
        "warp_metric_theory",
        "exotic_energy_coupling",
        "micro_field_distortion",
        "warp_field_control",
        "prototype_warp_drive",
    };

    public static int Rank(string nodeId)
    {
        for (var index = 0; index < WarpCapabilityPath.Count; index++)
            if (string.Equals(WarpCapabilityPath[index], nodeId, StringComparison.Ordinal))
                return index;
        return int.MaxValue;
    }
}
