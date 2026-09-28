using System;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Species;

public sealed record FleetCrewSpeciesSnapshot(
    int FleetId,
    int CivilizationId,
    FleetRole Role,
    string DesignId,
    string CrewSpeciesId,
    int CrewComplementIndividuals,
    string? EmbarkedPopulationSpeciesId,
    double CrewAdultBiomassKg,
    double BaselineMetabolicDemandUnits,
    double TypicalDayMetabolicDemandUnits,
    double PeakActivityMetabolicDemandUnits,
    DormancyMode NaturalDormancyMode,
    double DormantMetabolicDemandUnits,
    double MaximumNaturalDormancyDays,
    double TypicalDormancyRecoveryDays,
    BodyPlan BodyPlan,
    WorkOrientation PreferredWorkOrientation,
    double TypicalBodyLengthMeters,
    double TypicalBodyWidthMeters,
    bool RequiresBuoyantWorkspace)
{
    public bool HasNaturalDormancy => NaturalDormancyMode != DormancyMode.None;
    public bool CarriesPopulation => !string.IsNullOrWhiteSpace(EmbarkedPopulationSpeciesId);
    public bool CrewMatchesEmbarkedPopulationSpecies =>
        !CarriesPopulation || string.Equals(CrewSpeciesId, EmbarkedPopulationSpeciesId, StringComparison.Ordinal);
}

/// <summary>
/// Current fleet-crew bridge. FleetState persists its exact design ID, while legacy fleets
/// without one resolve to the baseline design for their role. The owning civilization's
/// founding species remains the temporary operating-crew identity.
///
/// Captured vessels, assigned foreign crews, or mixed-species crews will require a persisted
/// crew manifest rather than the current owner-species bridge.
/// </summary>
public sealed class CurrentFleetCrewSpeciesView
{
    public FleetCrewSpeciesSnapshot Build(GalaxyState galaxy, int fleetId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId)
            ?? throw new InvalidOperationException($"Unknown fleet {fleetId}.");
        var civilization = galaxy.Civilizations.FirstOrDefault(candidate => candidate.Id == fleet.CivilizationId)
            ?? throw new InvalidOperationException($"Fleet {fleet.Id} references unknown civilization {fleet.CivilizationId}.");
        var design = ShipDesignRegistry.GetForFleet(fleet);
        var species = SpeciesCatalog.Get(civilization.SpeciesId).Validated();

        if (design.CrewComplementIndividuals <= 0)
            throw new InvalidOperationException($"Fleet design {design.Id} does not define a positive crew complement.");

        var crewPopulationMillions = design.CrewComplementIndividuals / 1_000_000.0;
        var cohort = SpeciesPopulationCohort.Founding(species.Id, crewPopulationMillions);
        var metabolic = SpeciesMetabolicEnvelopeEvaluator.Evaluate(cohort);
        var scale = 1_000_000.0;

        var biomassKg = design.CrewComplementIndividuals * species.Physiology.TypicalAdultMassKg;
        if (!double.IsFinite(biomassKg) || biomassKg <= 0.0)
            throw new InvalidOperationException($"Fleet {fleet.Id} produced invalid crew biomass.");

        string? embarkedSpeciesId = null;
        if (fleet.EmbarkedPopulationMillions > 0.0)
        {
            if (string.IsNullOrWhiteSpace(fleet.EmbarkedPopulationSpeciesId) ||
                !SpeciesCatalog.TryGet(fleet.EmbarkedPopulationSpeciesId, out _))
            {
                throw new InvalidOperationException(
                    $"Fleet {fleet.Id} carries population without a known passenger species identity.");
            }

            embarkedSpeciesId = fleet.EmbarkedPopulationSpeciesId;
        }
        else if (!string.IsNullOrWhiteSpace(fleet.EmbarkedPopulationSpeciesId))
        {
            throw new InvalidOperationException(
                $"Fleet {fleet.Id} carries no population but retains passenger species '{fleet.EmbarkedPopulationSpeciesId}'.");
        }

        return new FleetCrewSpeciesSnapshot(
            fleet.Id,
            fleet.CivilizationId,
            fleet.Role,
            design.Id,
            species.Id,
            design.CrewComplementIndividuals,
            embarkedSpeciesId,
            biomassKg,
            metabolic.BaselineDemandMillions * scale,
            metabolic.TypicalDayAverageDemandMillions * scale,
            metabolic.PeakActivityDemandMillions * scale,
            metabolic.NaturalDormancyMode,
            metabolic.DormantDemandMillions * scale,
            metabolic.MaximumNaturalDormancyDays,
            metabolic.TypicalDormancyRecoveryDays,
            species.Morphology.BodyPlan,
            species.Morphology.PreferredWorkOrientation,
            species.Morphology.TypicalBodyLengthMeters,
            species.Morphology.TypicalBodyWidthMeters,
            species.Morphology.RequiresBuoyantWorkspace);
    }
}
