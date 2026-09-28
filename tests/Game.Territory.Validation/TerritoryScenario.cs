using System.Numerics;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;

namespace Game.Territory.Validation;

internal static class TerritoryScenario
{
    internal static GalaxyState AnchoredLine(int systems = 7, double spacing = 10)
    {
        var generated = new GalaxyGenerator().Generate(0x5445_5252_4954_4F52L,
            new GalaxyGenerationSettings { SystemCount = Math.Max(24, systems), PreWarpCivilizationCount = 3, AncientCivilizationCount = 0, Radius = 320 });
        var stars = generated.Systems.Take(systems).Select((star, index) => star with
        {
            Name = $"Anchor {index}", Position = new Vector2((float)(index * spacing), 0), HasHabitableWorld = true,
        }).ToArray();
        var sourceCivilizations = generated.Civilizations.Where(c => !c.IsSeededAncient).Take(2).ToArray();
        var civilizations = new List<CivilizationState>
        {
            sourceCivilizations[0] with { HomeSystemId = stars[0].Id, IsPlayer = true },
            sourceCivilizations[1] with { HomeSystemId = stars[^1].Id, IsPlayer = false },
        };
        var knowledge = new CivilizationKnowledgeState();
        foreach (var civilization in civilizations)
            foreach (var system in stars) knowledge.MarkSystemFullySurveyed(civilization.Id, system.Id);
        var technologies = civilizations.Select(c => new TechnologyState { CivilizationId = c.Id }).ToList();
        foreach (var technology in technologies) technology.CompletedTechnologyIds.Add("orbital_industry");
        return new GalaxyState
        {
            Seed = generated.Seed,
            Systems = stars,
            Civilizations = civilizations,
            Fleets = new List<FleetState>(),
            Colonies = new List<ColonyState>(),
            Economies = civilizations.Select(c => new CivilizationEconomyState { CivilizationId = c.Id, Credits = 10_000, Industry = 10_000 }).ToArray(),
            Technologies = technologies,
            ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(),
            ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(),
            PlayerCivilizationId = civilizations[0].Id,
            Knowledge = knowledge,
        };
    }

    internal static ColonyState Colony(GalaxyState galaxy, int civilization, int system, double population = 600) => new()
    {
        Id = galaxy.Colonies.Select(c => c.Id).DefaultIfEmpty(0).Max() + 1,
        CivilizationId = civilization, SystemId = system, Name = $"Colony {civilization}-{system}",
        PopulationMillions = population, Infrastructure = 4, Stability = 1, SurfaceHubLevel = 3,
    };

    internal static ColonyState Outpost(GalaxyState galaxy, int civilization, int system) => new()
    {
        Id = galaxy.Colonies.Select(c => c.Id).DefaultIfEmpty(0).Max() + 1,
        CivilizationId = civilization, SystemId = system, Name = $"Outpost {civilization}-{system}",
        Kind = SettlementKind.ResourceOutpost, PopulationMillions = 20, Infrastructure = 1, Stability = 1,
    };

    internal static FleetState Fleet(GalaxyState galaxy, int civilization, int system, FleetRole role, int? id = null) => new()
    {
        Id = id ?? galaxy.Fleets.Select(f => f.Id).DefaultIfEmpty(0).Max() + 1,
        CivilizationId = civilization, Name = $"{role} {civilization}", Role = role,
        Position = galaxy.Systems.Single(s => s.Id == system).Position, CurrentSystemId = system,
        TransitPhase = FleetTransitPhase.None, MaximumLegRangeLightYears = 22, FuelCapacityLightYears = 22, FuelRemainingLightYears = 22,
    };

    internal static int System(GalaxyState galaxy, int index) => galaxy.Systems[index].Id;
    internal static int Owner(GalaxyState galaxy, int index = 0) => galaxy.Civilizations[index].Id;
}
