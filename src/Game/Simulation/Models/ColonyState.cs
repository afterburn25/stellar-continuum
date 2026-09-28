using Game.Simulation.Species;
using System.Collections.Generic;
using Game.Simulation.Construction;

namespace Game.Simulation.Models;

public enum SettlementKind
{
    Colony,
    ResourceOutpost,
}

public sealed class ColonyState
{
    public required int Id { get; init; }
    public required int CivilizationId { get; init; }
    public required int SystemId { get; init; }

    /// <summary>
    /// Exact physical body occupied by the colony when body-level state is available.
    /// Legacy saves may reconstruct this from the deterministic compatibility candidate.
    /// </summary>
    public int? PlanetaryBodyId { get; set; }

    public required string Name { get; init; }
    public SettlementKind Kind { get; set; } = SettlementKind.Colony;

    /// <summary>
    /// Current early-release population owner. Until colonies gain bounded multi-species
    /// cohorts, the scalar population represents one species and this stable ID identifies it.
    /// </summary>
    public string PopulationSpeciesId { get; set; } = SpeciesCatalog.TerranBaselineId;

    public double PopulationMillions { get; set; }
    public double Infrastructure { get; set; } = 1.0;
    public double Stability { get; set; } = 1.0;
    /// <summary>Food held locally, measured in millions of population-days.</summary>
    public double StoredFoodPopulationDaysMillions { get; set; }
    /// <summary>Potable water held locally, measured in millions of population-days.</summary>
    public double StoredWaterPopulationDaysMillions { get; set; }
    public double StoredExtractedMaterials { get; set; }
    /// <summary>
    /// Unprocessed material remaining in a resource deposit. Null preserves older campaigns
    /// and resolves to the deterministic reserve for the occupied body.
    /// </summary>
    public double? RemainingExtractableMaterials { get; set; }
    /// <summary>Persistent surface administration tier. Levels 1/2/3 support 16/32/64 modules.</summary>
    public int SurfaceHubLevel { get; set; } = 1;
    public double SurfaceHubUpgradeDaysRemaining { get; set; }
    public List<SurfaceBuildingState> SurfaceBuildings { get; init; } = new();
}
