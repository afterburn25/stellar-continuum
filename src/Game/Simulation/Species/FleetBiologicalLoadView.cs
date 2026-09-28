using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Species;

/// <summary>
/// Read-only biological load aboard one fleet. Metabolic values use reference-individual demand
/// units so small operating crews and large embarked populations can be summed without inventing
/// cargo, credit, fuel, food, oxygen, or routing units. Logistics remains the owner of translating
/// these physical causes into actual supply flows and infrastructure.
/// </summary>
public sealed record FleetBiologicalLoadSnapshot(
    int FleetId,
    string CrewSpeciesId,
    int CrewComplementIndividuals,
    string? PassengerSpeciesId,
    double PassengerPopulationMillions,
    PopulationMetabolicOperatingState CrewOperatingState,
    PopulationMetabolicOperatingState? PassengerOperatingState,
    double CrewAdultBiomassKg,
    double PassengerAdultBiomassKg,
    double TotalAdultBiomassKg,
    double CrewMetabolicDemandUnits,
    double PassengerMetabolicDemandUnits,
    double TotalMetabolicDemandUnits,
    DormancyMode CrewNaturalDormancyMode,
    DormancyMode? PassengerNaturalDormancyMode,
    double CrewMaximumNaturalDormancyDays,
    double PassengerMaximumNaturalDormancyDays,
    bool RequiresSeparateEnvironmentalAccommodation,
    bool RequiresDedicatedPassengerNutrition,
    bool RequiresCrossSpeciesQuarantineAssessment,
    bool RequiresXenomedicalInterfaceAdaptation)
{
    public bool CarriesPassengers => PassengerPopulationMillions > 0.0;
    public bool MultipleSpeciesAboard =>
        CarriesPassengers &&
        !string.Equals(CrewSpeciesId, PassengerSpeciesId, StringComparison.Ordinal);
    public bool HasAnyNaturalDormancy =>
        CrewNaturalDormancyMode != DormancyMode.None ||
        (PassengerNaturalDormancyMode is DormancyMode mode && mode != DormancyMode.None);
}

public sealed class FleetBiologicalLoadView
{
    private const double IndividualsPerMillion = 1_000_000.0;
    private readonly CurrentFleetCrewSpeciesView _crewView = new();

    public FleetBiologicalLoadSnapshot Build(
        GalaxyState galaxy,
        int fleetId,
        PopulationMetabolicOperatingState crewOperatingState = PopulationMetabolicOperatingState.TypicalDay,
        PopulationMetabolicOperatingState passengerOperatingState = PopulationMetabolicOperatingState.TypicalDay)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId)
            ?? throw new InvalidOperationException($"Unknown fleet {fleetId}.");
        var crew = _crewView.Build(galaxy, fleetId);
        var crewSpecies = SpeciesCatalog.Get(crew.CrewSpeciesId);
        var crewCohort = SpeciesPopulationCohort.Founding(
            crew.CrewSpeciesId,
            crew.CrewComplementIndividuals / IndividualsPerMillion);
        var crewMetabolism = SpeciesMetabolicEnvelopeEvaluator.Evaluate(crewCohort);
        var crewDemand = SelectDemand(crewMetabolism, crewOperatingState) * IndividualsPerMillion;

        if (!double.IsFinite(crewDemand) || crewDemand <= 0.0)
            throw new InvalidOperationException($"Fleet {fleetId} produced invalid crew metabolic demand.");

        if (fleet.EmbarkedPopulationMillions <= 0.0)
        {
            return new FleetBiologicalLoadSnapshot(
                fleet.Id,
                crew.CrewSpeciesId,
                crew.CrewComplementIndividuals,
                PassengerSpeciesId: null,
                PassengerPopulationMillions: 0.0,
                crewOperatingState,
                PassengerOperatingState: null,
                crew.CrewAdultBiomassKg,
                PassengerAdultBiomassKg: 0.0,
                TotalAdultBiomassKg: crew.CrewAdultBiomassKg,
                CrewMetabolicDemandUnits: crewDemand,
                PassengerMetabolicDemandUnits: 0.0,
                TotalMetabolicDemandUnits: crewDemand,
                crewMetabolism.NaturalDormancyMode,
                PassengerNaturalDormancyMode: null,
                crewMetabolism.MaximumNaturalDormancyDays,
                PassengerMaximumNaturalDormancyDays: 0.0,
                RequiresSeparateEnvironmentalAccommodation: false,
                RequiresDedicatedPassengerNutrition: false,
                RequiresCrossSpeciesQuarantineAssessment: false,
                RequiresXenomedicalInterfaceAdaptation: false);
        }

        if (string.IsNullOrWhiteSpace(fleet.EmbarkedPopulationSpeciesId) ||
            !SpeciesCatalog.TryGet(fleet.EmbarkedPopulationSpeciesId, out var passengerSpecies) ||
            passengerSpecies is null)
        {
            throw new InvalidOperationException(
                $"Fleet {fleet.Id} carries population without a known passenger species identity.");
        }

        var passengerCohort = SpeciesPopulationCohort.Founding(
            passengerSpecies.Id,
            fleet.EmbarkedPopulationMillions);
        var passengerMetabolism = SpeciesMetabolicEnvelopeEvaluator.Evaluate(passengerCohort);
        var passengerDemand = SelectDemand(passengerMetabolism, passengerOperatingState) * IndividualsPerMillion;
        var passengerBiomassKg =
            fleet.EmbarkedPopulationMillions *
            passengerSpecies.Physiology.TypicalAdultMassKg *
            IndividualsPerMillion;
        var totalBiomassKg = crew.CrewAdultBiomassKg + passengerBiomassKg;
        var totalDemand = crewDemand + passengerDemand;

        if (!double.IsFinite(passengerBiomassKg) || passengerBiomassKg <= 0.0 ||
            !double.IsFinite(totalBiomassKg) || totalBiomassKg <= crew.CrewAdultBiomassKg ||
            !double.IsFinite(passengerDemand) || passengerDemand <= 0.0 ||
            !double.IsFinite(totalDemand) || totalDemand <= crewDemand)
        {
            throw new InvalidOperationException($"Fleet {fleet.Id} produced invalid combined biological load.");
        }

        var crossSpecies = !string.Equals(crewSpecies.Id, passengerSpecies.Id, StringComparison.Ordinal)
            ? SpeciesFirstContactInterfaceEvaluator.Evaluate(crewSpecies, passengerSpecies)
            : null;

        return new FleetBiologicalLoadSnapshot(
            fleet.Id,
            crew.CrewSpeciesId,
            crew.CrewComplementIndividuals,
            passengerSpecies.Id,
            fleet.EmbarkedPopulationMillions,
            crewOperatingState,
            passengerOperatingState,
            crew.CrewAdultBiomassKg,
            passengerBiomassKg,
            totalBiomassKg,
            crewDemand,
            passengerDemand,
            totalDemand,
            crewMetabolism.NaturalDormancyMode,
            passengerMetabolism.NaturalDormancyMode,
            crewMetabolism.MaximumNaturalDormancyDays,
            passengerMetabolism.MaximumNaturalDormancyDays,
            crossSpecies?.RequiresMutualEnvironmentalAccommodation ?? false,
            crossSpecies?.RequiresDedicatedNutrition ?? false,
            crossSpecies?.RequiresCrossSpeciesQuarantineAssessment ?? false,
            crossSpecies?.RequiresXenomedicalInterfaceAdaptation ?? false);
    }

    private static double SelectDemand(
        SpeciesMetabolicEnvelope metabolism,
        PopulationMetabolicOperatingState operatingState) => operatingState switch
    {
        PopulationMetabolicOperatingState.TypicalDay => metabolism.TypicalDayAverageDemandMillions,
        PopulationMetabolicOperatingState.Resting => metabolism.RestingDemandMillions,
        PopulationMetabolicOperatingState.PeakActivity => metabolism.PeakActivityDemandMillions,
        PopulationMetabolicOperatingState.NaturalDormancy when metabolism.HasNaturalDormancy => metabolism.DormantDemandMillions,
        PopulationMetabolicOperatingState.NaturalDormancy => throw new InvalidOperationException(
            $"Species '{metabolism.SpeciesId}' has no natural dormancy mode; technological or medical dormancy must be modeled separately."),
        _ => throw new ArgumentOutOfRangeException(nameof(operatingState)),
    };
}
