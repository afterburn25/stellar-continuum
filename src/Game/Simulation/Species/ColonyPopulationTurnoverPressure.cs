using System;
using Game.Simulation.Models;

namespace Game.Simulation.Species;

/// <summary>
/// Species-owned population-turnover inputs for one scalar colony population.
/// Life history supplies intrinsic pace; an exact naturally viable occupied world may add
/// biological environmental pressure. This record still does not own fertility policy,
/// healthcare, mortality, habitat-support consumption, or the final population mutation.
/// </summary>
public sealed record ColonyPopulationTurnoverPressure(
    int ColonyId,
    string SpeciesId,
    double IntrinsicGrowthPaceFactor,
    bool UsesExactOccupiedBody,
    int? PlanetaryBodyId,
    SpeciesColonizationViability? ColonizationViability,
    double NaturalHabitability,
    double NaturalEnvironmentTurnoverFactor,
    double EffectiveGrowthPaceFactor,
    EnvironmentalLimitingFactor LimitingFactor,
    bool RequiresEnvironmentalSupport)
{
    public bool EnvironmentalPressureApplied =>
        UsesExactOccupiedBody &&
        ColonizationViability == SpeciesColonizationViability.NaturallyViable &&
        NaturalEnvironmentTurnoverFactor < 0.999999999;

    public ColonyPopulationTurnoverPressure Validated()
    {
        if (ColonyId < 0)
            throw new InvalidOperationException("Colony population-turnover pressure requires a non-negative colony ID.");
        if (string.IsNullOrWhiteSpace(SpeciesId))
            throw new InvalidOperationException("Colony population-turnover pressure requires a Species ID.");
        ValidatePositive(IntrinsicGrowthPaceFactor, nameof(IntrinsicGrowthPaceFactor));
        ValidateUnitInterval(NaturalHabitability, nameof(NaturalHabitability));
        ValidateUnitInterval(NaturalEnvironmentTurnoverFactor, nameof(NaturalEnvironmentTurnoverFactor));
        ValidatePositive(EffectiveGrowthPaceFactor, nameof(EffectiveGrowthPaceFactor));

        var expected = IntrinsicGrowthPaceFactor * NaturalEnvironmentTurnoverFactor;
        if (Math.Abs(EffectiveGrowthPaceFactor - expected) > 0.000000001)
            throw new InvalidOperationException("Effective colony growth pace must equal intrinsic pace multiplied by environmental turnover factor.");

        if (!UsesExactOccupiedBody)
        {
            if (PlanetaryBodyId is not null || ColonizationViability is not null)
                throw new InvalidOperationException("A non-exact environmental pressure profile cannot claim a body or evaluated colonization viability.");
        }
        else if (PlanetaryBodyId is null || ColonizationViability is null)
        {
            throw new InvalidOperationException("An exact environmental pressure profile requires both body identity and evaluated colonization viability.");
        }

        return this;
    }

    private static void ValidatePositive(double value, string name)
    {
        if (!double.IsFinite(value) || value <= 0.0)
            throw new InvalidOperationException($"{name} must be finite and positive.");
    }

    private static void ValidateUnitInterval(double value, string name)
    {
        if (!double.IsFinite(value) || value < 0.0 || value > 1.0)
            throw new InvalidOperationException($"{name} must be between 0 and 1.");
    }
}

public interface IColonyPopulationTurnoverPressureView
{
    ColonyPopulationTurnoverPressure Build(GalaxyState galaxy, ColonyState colony);
}

/// <summary>
/// Current scalar-colony bridge. Environmental demographic pressure is intentionally applied
/// only when the colony stores an exact occupied body AND that world is naturally viable.
///
/// Legacy null-body colonies remain neutral because their compatibility world is a migration
/// convenience rather than authoritative historical occupancy. Habitat-supported fallback
/// colonies also remain neutral until habitat-support capacity/reliability/costs are modeled;
/// applying raw natural stress there would double-count an unspecified support system.
/// </summary>
public sealed class CurrentColonyPopulationTurnoverPressureView : IColonyPopulationTurnoverPressureView
{
    private readonly CurrentColonyDemographicPressureView _intrinsic = new();
    private readonly ColonySpeciesEnvironmentView _environment = new();

    public ColonyPopulationTurnoverPressure Build(GalaxyState galaxy, ColonyState colony)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(colony);

        var intrinsic = _intrinsic.Build(colony);
        if (colony.PlanetaryBodyId is not int exactBodyId)
        {
            return new ColonyPopulationTurnoverPressure(
                colony.Id,
                intrinsic.SpeciesId,
                intrinsic.IntrinsicGrowthPaceFactor,
                UsesExactOccupiedBody: false,
                PlanetaryBodyId: null,
                ColonizationViability: null,
                NaturalHabitability: 1.0,
                NaturalEnvironmentTurnoverFactor: 1.0,
                EffectiveGrowthPaceFactor: intrinsic.IntrinsicGrowthPaceFactor,
                EnvironmentalLimitingFactor.None,
                RequiresEnvironmentalSupport: false).Validated();
        }

        var environment = _environment.Build(galaxy, colony.Id);
        if (environment.PlanetaryBodyId != exactBodyId)
        {
            throw new InvalidOperationException(
                $"Colony {colony.Id} environmental view resolved body {environment.PlanetaryBodyId} instead of exact body {exactBodyId}.");
        }

        // NaturalHabitability is a worst-axis physical suitability in [0,1]. Square root keeps
        // comfortable worlds at 1 while compressing the penalty on merely viable worlds; at the
        // natural-colonization threshold (0.20) this yields ~0.447 rather than an extreme 0.20.
        // No named racial bonus/penalty or arbitrary per-Species percentage table is introduced.
        var environmentalFactor = environment.ColonizationViability == SpeciesColonizationViability.NaturallyViable
            ? Math.Sqrt(Math.Clamp(environment.NaturalHabitability, 0.0, 1.0))
            : 1.0;

        if (environment.ColonizationViability == SpeciesColonizationViability.NaturallyViable &&
            environmentalFactor <= 0.0)
        {
            throw new InvalidOperationException(
                $"Naturally viable colony {colony.Id} produced a non-positive environmental demographic factor.");
        }

        return new ColonyPopulationTurnoverPressure(
            colony.Id,
            intrinsic.SpeciesId,
            intrinsic.IntrinsicGrowthPaceFactor,
            UsesExactOccupiedBody: true,
            PlanetaryBodyId: exactBodyId,
            environment.ColonizationViability,
            environment.NaturalHabitability,
            environmentalFactor,
            intrinsic.IntrinsicGrowthPaceFactor * environmentalFactor,
            environment.LimitingFactor,
            environment.RequiresEnvironmentalSupport).Validated();
    }
}
