using System.Collections.Generic;
using Game.Campaign;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Models;

public sealed class GalaxyState
{
    private IReadOnlyList<PlanetaryBodyState>? _planetaryBodies;

    /// <summary>Developer campaigns use their separate persistence envelope, never Player saves.</summary>
    public DeveloperSessionState? DeveloperSession { get; set; }
    public GalaxyGenerationMetadata? GenerationMetadata { get; set; }
    /// <summary>Optional so pre-core saves retain their original catalogue and presentation.</summary>
    public GalacticCoreMetadata? GalacticCore { get; set; }
    public Game.Simulation.Combat.CampaignMassiveEncounter? ActiveCombatEncounter { get; set; }
    public List<Game.Simulation.Combat.FleetPowerObservation> CombatIntelligence { get; set; } = new();

    public required long Seed { get; init; }
    public required IReadOnlyList<StarSystemState> Systems { get; init; }

    /// <summary>
    /// Authoritative deterministic world catalog. Version 16+ campaign saves persist this
    /// catalog, while legacy saves and lazily created states reconstruct it from the saved
    /// Seed and Systems. Supplying an explicit catalog during generation avoids recomputing it
    /// during the active campaign.
    /// </summary>
    public IReadOnlyList<PlanetaryBodyState> PlanetaryBodies
    {
        get => _planetaryBodies ??= new PlanetaryBodyGenerator().Generate(Seed, Systems);
        init => _planetaryBodies = value;
    }

    public required IList<CivilizationState> Civilizations { get; init; }
    public required IList<FleetState> Fleets { get; init; }
    public required IList<ColonyState> Colonies { get; init; }
    public required IReadOnlyList<CivilizationEconomyState> Economies { get; init; }
    public required IList<TechnologyState> Technologies { get; init; }
    public required IList<ConstructionState> ConstructionStates { get; init; }
    public required IList<ShipyardState> ShipyardStates { get; init; }
    public required int PlayerCivilizationId { get; init; }
    public required CivilizationKnowledgeState Knowledge { get; init; }
}
