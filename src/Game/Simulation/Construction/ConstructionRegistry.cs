using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Research;

namespace Game.Simulation.Construction;

public static class ConstructionRegistry
{
    public static readonly IReadOnlyList<ConstructionProjectDefinition> All = new[]
    {
        new ConstructionProjectDefinition(
            "research_network",
            "Planetary Research Network",
            "Expand universities, laboratories, compute infrastructure, and scientific coordination. Adds 4 Effective Research Labs.",
            700.0,
            Array.Empty<string>(),
            ConstructionCategory.Science, 150.0),
        new ConstructionProjectDefinition(
            "industrial_automation",
            "Industrial Automation Program",
            "Modernize planetary production with autonomous fabrication and logistics.",
            900.0,
            Array.Empty<string>(),
            ConstructionCategory.Industry, 200.0),
        new ConstructionProjectDefinition(
            "orbital_launch_complex",
            "Orbital Launch Complex",
            "Build permanent heavy-lift infrastructure needed for sustained orbital construction.",
            1100.0,
            Array.Empty<string>(),
            ConstructionCategory.Orbital, 250.0, UpkeepCreditsPerDay: 0.08),
        new ConstructionProjectDefinition(
            "orbital_shipyard",
            "Orbital Shipyard",
            "Construct a permanent orbital yard capable of assembling large interplanetary and future interstellar vessels.",
            1600.0,
            new[] { "orbital_industry" },
            ConstructionCategory.Orbital, 350.0, UpkeepCreditsPerDay: 0.12,
            RequiredProjects: new[] { "orbital_launch_complex" }),
        new ConstructionProjectDefinition(
            "asteroid_resource_network",
            "Asteroid Resource Network",
            "Deploy prospectors, autonomous extraction platforms, and cargo tugs across the home system's resource belt.",
            1800.0,
            new[] { "orbital_industry" },
            ConstructionCategory.Orbital, 320.0, IndustryPerDay: 1.50, UpkeepCreditsPerDay: 0.18,
            RequiredProjects: new[] { "orbital_launch_complex" }),
        new ConstructionProjectDefinition(
            "warp_test_facility",
            "Warp Test Facility",
            "A remote hardened research and engineering complex for full-scale spacetime-field experiments.",
            2200.0,
            new[] { "warp_field_control" },
            ConstructionCategory.Ftl, 450.0, UpkeepCreditsPerDay: 0.15),
    };

    public static ConstructionProjectDefinition Get(string id) =>
        All.First(project => string.Equals(project.Id, id, StringComparison.Ordinal));

    public static ConstructionProjectDefinition? Find(string id) =>
        All.FirstOrDefault(project => string.Equals(project.Id, id, StringComparison.Ordinal));

    public static string? GetLockReason(ConstructionProjectDefinition project,
        ConstructionState construction, TechnologyState technology)
    {
        var missingTechnologies = project.RequiredTechnologies
            .Where(id => !technology.CompletedTechnologyIds.Contains(id))
            .Select(TechnologyRegistry.Get)
            .Select(item => item.Name)
            .ToArray();
        var missingProjects = (project.RequiredProjects ?? Array.Empty<string>())
            .Where(id => !construction.CompletedProjectIds.Contains(id))
            .Select(Get)
            .Select(item => item.Name)
            .ToArray();
        if (missingTechnologies.Length == 0 && missingProjects.Length == 0) return null;
        var requirements = missingTechnologies.Concat(missingProjects);
        return "requires " + string.Join(" and ", requirements);
    }

    public static IReadOnlyList<ConstructionProjectDefinition> GetAvailable(ConstructionState construction, TechnologyState technology) =>
        All.Where(project =>
                !construction.CompletedProjectIds.Contains(project.Id) &&
                GetLockReason(project, construction, technology) is null)
            .ToArray();
}
