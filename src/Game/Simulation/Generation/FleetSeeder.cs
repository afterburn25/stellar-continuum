using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.Simulation.Generation;

public sealed class FleetSeeder
{
    public IList<FleetState> Seed(
        IReadOnlyList<StarSystemState> systems,
        IList<CivilizationState> civilizations,
        IList<ColonyState>? populationSources = null)
    {
        var fleets = new List<FleetState>();
        foreach (var civilization in civilizations)
        {
            if (civilization.DevelopmentStage == CivilizationDevelopmentStage.WarpCapable)
                AddStarterFleets(fleets, systems, civilization, populationSources);
        }

        return fleets;
    }

    public void EnsureStarterFleets(GalaxyState galaxy, int civilizationId)
    {
        var civilization = galaxy.Civilizations.First(c => c.Id == civilizationId);
        if (galaxy.Fleets.Any(f => f.IsActive && f.CivilizationId == civilizationId && f.Role == FleetRole.Scout))
            return;

        AddStarterFleets(galaxy.Fleets, galaxy.Systems, civilization, galaxy.Colonies);
    }

    private static void AddStarterFleets(
        IList<FleetState> fleets,
        IReadOnlyList<StarSystemState> systems,
        CivilizationState civilization,
        IList<ColonyState>? populationSources)
    {
        var home = systems.First(system => system.Id == civilization.HomeSystemId);
        var nextId = fleets.Count == 0 ? 0 : fleets.Max(f => f.Id) + 1;
        var scoutDesign = ShipDesignRegistry.GetCurrentDesignForRole(FleetRole.Scout);
        fleets.Add(new FleetState
        {
            Id = nextId++,
            CivilizationId = civilization.Id,
            Name = civilization.IsPlayer ? "Pathfinder One" : $"{civilization.Name} Scout",
            Role = FleetRole.Scout,
            DesignId = scoutDesign.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = scoutDesign.StrategicSpeed,
            MaximumLegRangeLightYears = scoutDesign.MaximumLegRangeLightYears,
            FuelCapacityLightYears = scoutDesign.FuelEnduranceLightYears,
            FuelRemainingLightYears = scoutDesign.FuelEnduranceLightYears,
            SensorRange = scoutDesign.SensorRange,
            IsActive = true,
        });

        if (!civilization.ExpansionAllowed || populationSources is null)
            return;

        var colonyDesign = ShipDesignRegistry.All.FirstOrDefault(design => design.Role == FleetRole.Colony);
        if (colonyDesign is null || colonyDesign.PopulationCostMillions <= 0.0)
            return;

        var source = populationSources
            .Where(colony => colony.CivilizationId == civilization.Id)
            .OrderByDescending(colony => colony.PopulationMillions)
            .FirstOrDefault();
        if (source is null || source.PopulationMillions < colonyDesign.PopulationCostMillions + 500.0)
            return;

        if (!SpeciesCatalog.TryGet(source.PopulationSpeciesId, out _))
        {
            throw new InvalidOperationException(
                $"Source colony {source.Id} references unknown population species '{source.PopulationSpeciesId}'.");
        }

        source.PopulationMillions -= colonyDesign.PopulationCostMillions;
        fleets.Add(new FleetState
        {
            Id = nextId,
            CivilizationId = civilization.Id,
            Name = civilization.IsPlayer ? "Pioneer One" : $"{civilization.Name} Pioneer",
            Role = FleetRole.Colony,
            DesignId = colonyDesign.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = colonyDesign.StrategicSpeed,
            MaximumLegRangeLightYears = colonyDesign.MaximumLegRangeLightYears,
            FuelCapacityLightYears = colonyDesign.FuelEnduranceLightYears,
            FuelRemainingLightYears = colonyDesign.FuelEnduranceLightYears,
            SensorRange = colonyDesign.SensorRange,
            IsActive = true,
            EmbarkedPopulationMillions = colonyDesign.PopulationCostMillions,
            EmbarkedPopulationSpeciesId = source.PopulationSpeciesId,
        });
    }
}
