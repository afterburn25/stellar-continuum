using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Species;

/// <summary>
/// Environmental-control requirements for an exact occupied planetary body. These are
/// physical/biological requirements only; they are not supply units, credits, construction
/// capacity, route demand, or a claim that the requirement is currently satisfied.
/// </summary>
public sealed record ColonyEnvironmentalSupportRequirements(
    int PlanetaryBodyId,
    SpeciesColonizationViability ColonizationViability,
    double NaturalHabitability,
    double UnprotectedOperationalCapacity,
    EnvironmentalLimitingFactor LimitingFactor,
    int RequiredMitigationCategories,
    bool RequiresGravityMitigation,
    bool RequiresThermalControl,
    bool RequiresPressureControl,
    bool RequiresSealedHabitat,
    bool RequiresArtificialBiosphere,
    bool RequiresRadiationShielding)
{
    public bool RequiresAnyEnvironmentalMitigation => RequiredMitigationCategories > 0;
    public bool UsesPrototypeHabitatSupportedFallback =>
        ColonizationViability == SpeciesColonizationViability.HabitatSupportedFallback;

    public ColonyEnvironmentalSupportRequirements Validated()
    {
        if (PlanetaryBodyId < 0)
            throw new InvalidOperationException("Habitat-support requirements require a non-negative planetary body ID.");
        ValidateUnitInterval(NaturalHabitability, nameof(NaturalHabitability));
        ValidateUnitInterval(UnprotectedOperationalCapacity, nameof(UnprotectedOperationalCapacity));
        if (RequiredMitigationCategories < 0 || RequiredMitigationCategories > 6)
            throw new InvalidOperationException("Environmental mitigation category count must remain between 0 and 6.");

        var counted = 0;
        if (RequiresGravityMitigation) counted++;
        if (RequiresThermalControl) counted++;
        if (RequiresPressureControl) counted++;
        if (RequiresSealedHabitat) counted++;
        if (RequiresArtificialBiosphere) counted++;
        if (RequiresRadiationShielding) counted++;
        if (counted != RequiredMitigationCategories)
            throw new InvalidOperationException("Environmental mitigation category count does not match its physical requirement flags.");

        return this;
    }

    private static void ValidateUnitInterval(double value, string name)
    {
        if (!double.IsFinite(value) || value < 0.0 || value > 1.0)
            throw new InvalidOperationException($"{name} must be finite and between 0 and 1.");
    }
}

/// <summary>
/// Raw Species-side physical burden for one scalar colony population. Logistics/Construction
/// may consume these facts later, but Species does not convert them into cargo, upkeep,
/// infrastructure capacity, money, route throughput, or reliability.
/// </summary>
public sealed record ColonyHabitatSupportBurden(
    int ColonyId,
    int CivilizationId,
    int SystemId,
    string SpeciesId,
    double PopulationMillions,
    double TypicalDayMetabolicDemandMillions,
    double AdultBiomassMillionKg,
    ColonyEnvironmentalSupportRequirements? Environment)
{
    public bool UsesExactOccupiedBody => Environment is not null;
    public bool RequiresEnvironmentalSupport => Environment?.RequiresAnyEnvironmentalMitigation ?? false;

    public double GravityMitigationPopulationMillions =>
        Environment?.RequiresGravityMitigation == true ? PopulationMillions : 0.0;
    public double ThermalControlPopulationMillions =>
        Environment?.RequiresThermalControl == true ? PopulationMillions : 0.0;
    public double PressureControlPopulationMillions =>
        Environment?.RequiresPressureControl == true ? PopulationMillions : 0.0;
    public double SealedHabitatPopulationMillions =>
        Environment?.RequiresSealedHabitat == true ? PopulationMillions : 0.0;
    public double ArtificialBiospherePopulationMillions =>
        Environment?.RequiresArtificialBiosphere == true ? PopulationMillions : 0.0;
    public double RadiationShieldingPopulationMillions =>
        Environment?.RequiresRadiationShielding == true ? PopulationMillions : 0.0;

    public ColonyHabitatSupportBurden Validated()
    {
        if (ColonyId < 0 || CivilizationId < 0 || SystemId < 0)
            throw new InvalidOperationException("Colony habitat-support burden IDs must be non-negative.");
        if (string.IsNullOrWhiteSpace(SpeciesId))
            throw new InvalidOperationException("Colony habitat-support burden requires a Species ID.");
        ValidatePositive(PopulationMillions, nameof(PopulationMillions));
        ValidatePositive(TypicalDayMetabolicDemandMillions, nameof(TypicalDayMetabolicDemandMillions));
        ValidatePositive(AdultBiomassMillionKg, nameof(AdultBiomassMillionKg));
        Environment?.Validated();
        return this;
    }

    private static void ValidatePositive(double value, string name)
    {
        if (!double.IsFinite(value) || value <= 0.0)
            throw new InvalidOperationException($"{name} must be finite and positive.");
    }
}

public interface IColonyHabitatSupportBurdenView
{
    ColonyHabitatSupportBurden Build(GalaxyState galaxy, int colonyId);
}

public sealed class CurrentColonyHabitatSupportBurdenView : IColonyHabitatSupportBurdenView
{
    private readonly ColonySpeciesEnvironmentView _environment = new();

    public ColonyHabitatSupportBurden Build(GalaxyState galaxy, int colonyId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var colony = galaxy.Colonies.FirstOrDefault(candidate => candidate.Id == colonyId)
            ?? throw new InvalidOperationException($"Unknown colony {colonyId}.");
        if (colony.PopulationMillions <= 0.0 || !double.IsFinite(colony.PopulationMillions))
            throw new InvalidOperationException($"Colony {colony.Id} has no valid positive population to evaluate.");

        var species = SpeciesCatalog.Get(colony.PopulationSpeciesId);
        var cohort = SpeciesPopulationCohort.Founding(species.Id, colony.PopulationMillions);
        var metabolism = SpeciesMetabolicEnvelopeEvaluator.Evaluate(cohort);
        var biomass = colony.PopulationMillions * species.Physiology.TypicalAdultMassKg;

        ColonyEnvironmentalSupportRequirements? environmentalRequirements = null;
        if (colony.PlanetaryBodyId is int exactBodyId)
        {
            var environment = _environment.Build(galaxy, colony.Id);
            if (environment.PlanetaryBodyId != exactBodyId)
            {
                throw new InvalidOperationException(
                    $"Colony {colony.Id} support burden resolved body {environment.PlanetaryBodyId} instead of exact body {exactBodyId}.");
            }

            environmentalRequirements = new ColonyEnvironmentalSupportRequirements(
                exactBodyId,
                environment.ColonizationViability,
                environment.NaturalHabitability,
                environment.UnprotectedOperationalCapacity,
                environment.LimitingFactor,
                environment.RequiredEnvironmentalMitigationCategories,
                environment.RequiresGravityMitigation,
                environment.RequiresThermalControl,
                environment.RequiresPressureControl,
                environment.RequiresSealedHabitat,
                environment.RequiresArtificialBiosphere,
                environment.RequiresRadiationShielding).Validated();
        }

        return new ColonyHabitatSupportBurden(
            colony.Id,
            colony.CivilizationId,
            colony.SystemId,
            species.Id,
            colony.PopulationMillions,
            metabolism.TypicalDayAverageDemandMillions,
            biomass,
            environmentalRequirements).Validated();
    }
}
