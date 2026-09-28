using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Economy;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Generation;

public sealed class ColonySeeder
{
    /// <summary>
    /// Legacy-compatible scalar seeding path. Older migrations that do not provide a body
    /// catalog retain a null PlanetaryBodyId and can be resolved separately; new campaigns use
    /// the body-aware overload below.
    /// </summary>
    public IList<ColonyState> Seed(IList<CivilizationState> civilizations) =>
        SeedInternal(civilizations, planetaryBodies: null);

    public IList<ColonyState> Seed(
        IList<CivilizationState> civilizations,
        IReadOnlyList<PlanetaryBodyState> planetaryBodies)
    {
        ArgumentNullException.ThrowIfNull(planetaryBodies);
        return SeedInternal(civilizations, planetaryBodies);
    }

    private static IList<ColonyState> SeedInternal(
        IList<CivilizationState> civilizations,
        IReadOnlyList<PlanetaryBodyState>? planetaryBodies)
    {
        ArgumentNullException.ThrowIfNull(civilizations);

        var colonies = new List<ColonyState>(civilizations.Count + 2);
        var homeworldPlanner = planetaryBodies is null ? null : new SpeciesHomeworldPlanner();
        var id = 0;
        foreach (var civilization in civilizations)
        {
            int? planetaryBodyId = null;
            if (homeworldPlanner is not null)
            {
                var home = homeworldPlanner.ResolveWithinSystem(
                    civilization.Id,
                    civilization.SpeciesId,
                    civilization.HomeSystemId,
                    planetaryBodies!);
                planetaryBodyId = home.PlanetaryBodyId;
            }

            colonies.Add(new ColonyState
            {
                Id = id++,
                CivilizationId = civilization.Id,
                SystemId = civilization.HomeSystemId,
                PlanetaryBodyId = planetaryBodyId,
                Name = civilization.SpeciesId == SpeciesCatalog.TerranBaselineId &&
                    civilization.HomeSystemId == SolCatalogPreset.SystemId && planetaryBodyId == SolCatalogPreset.EarthBodyId
                        ? "Earth" : $"{civilization.Name} Prime",
                PopulationSpeciesId = civilization.SpeciesId,
                PopulationMillions = civilization.IsSeededAncient ? 12000.0 : 9500.0,
                StoredFoodPopulationDaysMillions = (civilization.IsSeededAncient ? 12000.0 : 9500.0) * ColonySustenanceReserves.MaximumFoodReserveDays,
                StoredWaterPopulationDaysMillions = (civilization.IsSeededAncient ? 12000.0 : 9500.0) * ColonySustenanceReserves.MaximumWaterReserveDays,
                Infrastructure = civilization.IsSeededAncient ? 3.0 : 1.0,
                Stability = 1.0,
                SurfaceHubLevel = civilization.IsSeededAncient ? 3 : 2,
            });

            // The human 2050 opening is already an early multi-world civilization. These are
            // small dependent settlements, not extra homeworlds or free productive centers.
            // They use the same colony, environment, logistics, surface and persistence rules
            // as every later settlement.
            if (planetaryBodies is not null && civilization.SpeciesId == SpeciesCatalog.TerranBaselineId &&
                civilization.HomeSystemId == SolCatalogPreset.SystemId && planetaryBodyId == SolCatalogPreset.EarthBodyId)
            {
                AddHumanSolSettlement(SolCatalogPreset.MoonBodyId, "Luna", 0.10, 0.28);
                AddHumanSolSettlement(4, "Mars", 0.25, 0.24);
            }

            void AddHumanSolSettlement(int bodyId, string name, double populationMillions, double infrastructure)
            {
                if (!planetaryBodies.Any(body => body.Id == bodyId && body.SystemId == SolCatalogPreset.SystemId &&
                        body.Environment.HasSolidSurface))
                    throw new InvalidOperationException($"Canonical Sol settlement body {bodyId} is unavailable.");
                colonies.Add(new ColonyState
                {
                    Id = id++, CivilizationId = civilization.Id, SystemId = SolCatalogPreset.SystemId,
                    PlanetaryBodyId = bodyId, Name = name, PopulationSpeciesId = civilization.SpeciesId,
                    PopulationMillions = populationMillions, Infrastructure = infrastructure, Stability = 0.92,
                    SurfaceHubLevel = 1,
                    StoredFoodPopulationDaysMillions = populationMillions * ColonySustenanceReserves.MaximumFoodReserveDays,
                    StoredWaterPopulationDaysMillions = populationMillions * ColonySustenanceReserves.MaximumWaterReserveDays,
                });
            }
        }
        return colonies;
    }

    public IReadOnlyList<CivilizationEconomyState> SeedEconomies(IList<CivilizationState> civilizations)
    {
        var economies = new List<CivilizationEconomyState>(civilizations.Count);
        foreach (var civilization in civilizations)
        {
            economies.Add(new CivilizationEconomyState
            {
                CivilizationId = civilization.Id,
                Credits = civilization.IsSeededAncient ? 50000.0 : 500.0,
                Industry = civilization.IsSeededAncient ? 25000.0 : 200.0,
                Science = civilization.IsSeededAncient ? 10000.0 : 0.0,
            });
        }
        return economies;
    }
}
