using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Species;

/// <summary>
/// Additive raw physical burden totals. These fields deliberately retain the same Species-side
/// units as the colony burden contract and therefore do not represent support capacity, cargo,
/// credits, routes, reliability, or whether any requirement is currently satisfied.
/// </summary>
public sealed record HabitatSupportBurdenTotals(
    double PopulationMillions,
    double TypicalDayMetabolicDemandMillions,
    double AdultBiomassMillionKg,
    double GravityMitigationPopulationMillions,
    double ThermalControlPopulationMillions,
    double PressureControlPopulationMillions,
    double SealedHabitatPopulationMillions,
    double ArtificialBiospherePopulationMillions,
    double RadiationShieldingPopulationMillions)
{
    public static HabitatSupportBurdenTotals Zero { get; } = new(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    public HabitatSupportBurdenTotals Plus(ColonyHabitatSupportBurden burden)
    {
        ArgumentNullException.ThrowIfNull(burden);
        burden.Validated();
        return new HabitatSupportBurdenTotals(
            PopulationMillions + burden.PopulationMillions,
            TypicalDayMetabolicDemandMillions + burden.TypicalDayMetabolicDemandMillions,
            AdultBiomassMillionKg + burden.AdultBiomassMillionKg,
            GravityMitigationPopulationMillions + burden.GravityMitigationPopulationMillions,
            ThermalControlPopulationMillions + burden.ThermalControlPopulationMillions,
            PressureControlPopulationMillions + burden.PressureControlPopulationMillions,
            SealedHabitatPopulationMillions + burden.SealedHabitatPopulationMillions,
            ArtificialBiospherePopulationMillions + burden.ArtificialBiospherePopulationMillions,
            RadiationShieldingPopulationMillions + burden.RadiationShieldingPopulationMillions).Validated();
    }

    public HabitatSupportBurdenTotals Plus(HabitatSupportBurdenTotals other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return new HabitatSupportBurdenTotals(
            PopulationMillions + other.PopulationMillions,
            TypicalDayMetabolicDemandMillions + other.TypicalDayMetabolicDemandMillions,
            AdultBiomassMillionKg + other.AdultBiomassMillionKg,
            GravityMitigationPopulationMillions + other.GravityMitigationPopulationMillions,
            ThermalControlPopulationMillions + other.ThermalControlPopulationMillions,
            PressureControlPopulationMillions + other.PressureControlPopulationMillions,
            SealedHabitatPopulationMillions + other.SealedHabitatPopulationMillions,
            ArtificialBiospherePopulationMillions + other.ArtificialBiospherePopulationMillions,
            RadiationShieldingPopulationMillions + other.RadiationShieldingPopulationMillions).Validated();
    }

    public HabitatSupportBurdenTotals Validated()
    {
        ValidateNonNegative(PopulationMillions, nameof(PopulationMillions));
        ValidateNonNegative(TypicalDayMetabolicDemandMillions, nameof(TypicalDayMetabolicDemandMillions));
        ValidateNonNegative(AdultBiomassMillionKg, nameof(AdultBiomassMillionKg));
        ValidatePopulationSubset(GravityMitigationPopulationMillions, nameof(GravityMitigationPopulationMillions));
        ValidatePopulationSubset(ThermalControlPopulationMillions, nameof(ThermalControlPopulationMillions));
        ValidatePopulationSubset(PressureControlPopulationMillions, nameof(PressureControlPopulationMillions));
        ValidatePopulationSubset(SealedHabitatPopulationMillions, nameof(SealedHabitatPopulationMillions));
        ValidatePopulationSubset(ArtificialBiospherePopulationMillions, nameof(ArtificialBiospherePopulationMillions));
        ValidatePopulationSubset(RadiationShieldingPopulationMillions, nameof(RadiationShieldingPopulationMillions));
        return this;
    }

    private void ValidatePopulationSubset(double value, string name)
    {
        ValidateNonNegative(value, name);
        if (value > PopulationMillions + 0.000000001)
            throw new InvalidOperationException($"{name} cannot exceed total population burden.");
    }

    private static void ValidateNonNegative(double value, string name)
    {
        if (!double.IsFinite(value) || value < 0.0)
            throw new InvalidOperationException($"{name} must be finite and non-negative.");
    }
}

public sealed record SystemHabitatSupportBurden(
    int CivilizationId,
    int SystemId,
    int PopulatedColonyCount,
    int ExactBodyColonyCount,
    int LegacyUnknownEnvironmentColonyCount,
    int HabitatSupportedFallbackColonyCount,
    int DistinctSpeciesCount,
    HabitatSupportBurdenTotals Totals)
{
    public SystemHabitatSupportBurden Validated()
    {
        if (CivilizationId < 0 || SystemId < 0)
            throw new InvalidOperationException("System habitat-support burden IDs must be non-negative.");
        ValidateCounts(
            PopulatedColonyCount,
            ExactBodyColonyCount,
            LegacyUnknownEnvironmentColonyCount,
            HabitatSupportedFallbackColonyCount,
            DistinctSpeciesCount);
        Totals.Validated();
        return this;
    }

    private static void ValidateCounts(
        int populated,
        int exact,
        int unknown,
        int fallback,
        int species)
    {
        if (populated <= 0)
            throw new InvalidOperationException("System habitat-support burden requires at least one populated colony.");
        if (exact < 0 || unknown < 0 || fallback < 0 || species <= 0)
            throw new InvalidOperationException("System habitat-support burden counts must remain valid and non-negative.");
        if (exact + unknown != populated)
            throw new InvalidOperationException("Exact-body and legacy-unknown colony counts must partition populated colonies.");
        if (fallback > exact)
            throw new InvalidOperationException("Habitat-supported fallback colonies must be a subset of exact-body colonies.");
        if (species > populated)
            throw new InvalidOperationException("Distinct Species count cannot exceed populated colony count in the current scalar-colony model.");
    }
}

public sealed record CivilizationHabitatSupportBurden(
    int CivilizationId,
    int PopulatedColonyCount,
    int ExactBodyColonyCount,
    int LegacyUnknownEnvironmentColonyCount,
    int HabitatSupportedFallbackColonyCount,
    int DistinctSpeciesCount,
    HabitatSupportBurdenTotals Totals,
    IReadOnlyList<SystemHabitatSupportBurden> Systems)
{
    public CivilizationHabitatSupportBurden Validated()
    {
        if (CivilizationId < 0)
            throw new InvalidOperationException("Civilization habitat-support burden requires a non-negative civilization ID.");
        if (PopulatedColonyCount < 0 || ExactBodyColonyCount < 0 ||
            LegacyUnknownEnvironmentColonyCount < 0 || HabitatSupportedFallbackColonyCount < 0 ||
            DistinctSpeciesCount < 0)
        {
            throw new InvalidOperationException("Civilization habitat-support burden counts must remain non-negative.");
        }
        if (ExactBodyColonyCount + LegacyUnknownEnvironmentColonyCount != PopulatedColonyCount)
            throw new InvalidOperationException("Exact-body and legacy-unknown colony counts must partition populated colonies.");
        if (HabitatSupportedFallbackColonyCount > ExactBodyColonyCount)
            throw new InvalidOperationException("Fallback colonies must be a subset of exact-body colonies.");
        if (DistinctSpeciesCount > PopulatedColonyCount)
            throw new InvalidOperationException("Distinct Species count cannot exceed populated colony count in the current scalar-colony model.");
        Totals.Validated();

        if (Systems is null)
            throw new InvalidOperationException("Civilization habitat-support burden requires a system collection.");
        if (Systems.Any(system => system.CivilizationId != CivilizationId))
            throw new InvalidOperationException("System habitat-support burden references a different civilization.");
        if (!Systems.Select(system => system.SystemId).SequenceEqual(Systems.Select(system => system.SystemId).OrderBy(id => id)))
            throw new InvalidOperationException("System habitat-support burden list must remain deterministically ordered by system ID.");
        if (Systems.Select(system => system.SystemId).Distinct().Count() != Systems.Count)
            throw new InvalidOperationException("Civilization habitat-support burden cannot contain duplicate system entries.");

        var rebuiltCounts = new
        {
            Populated = Systems.Sum(system => system.PopulatedColonyCount),
            Exact = Systems.Sum(system => system.ExactBodyColonyCount),
            Unknown = Systems.Sum(system => system.LegacyUnknownEnvironmentColonyCount),
            Fallback = Systems.Sum(system => system.HabitatSupportedFallbackColonyCount),
        };
        if (rebuiltCounts.Populated != PopulatedColonyCount ||
            rebuiltCounts.Exact != ExactBodyColonyCount ||
            rebuiltCounts.Unknown != LegacyUnknownEnvironmentColonyCount ||
            rebuiltCounts.Fallback != HabitatSupportedFallbackColonyCount)
        {
            throw new InvalidOperationException("Civilization habitat-support colony counts do not conserve their per-system totals.");
        }

        var rebuiltTotals = Systems.Aggregate(
            HabitatSupportBurdenTotals.Zero,
            (current, system) => current.Plus(system.Totals));
        ValidateTotalsEqual(Totals, rebuiltTotals);
        return this;
    }

    private static void ValidateTotalsEqual(
        HabitatSupportBurdenTotals expected,
        HabitatSupportBurdenTotals actual)
    {
        RequireClose(expected.PopulationMillions, actual.PopulationMillions, "population");
        RequireClose(expected.TypicalDayMetabolicDemandMillions, actual.TypicalDayMetabolicDemandMillions, "metabolic demand");
        RequireClose(expected.AdultBiomassMillionKg, actual.AdultBiomassMillionKg, "adult biomass");
        RequireClose(expected.GravityMitigationPopulationMillions, actual.GravityMitigationPopulationMillions, "gravity mitigation population");
        RequireClose(expected.ThermalControlPopulationMillions, actual.ThermalControlPopulationMillions, "thermal-control population");
        RequireClose(expected.PressureControlPopulationMillions, actual.PressureControlPopulationMillions, "pressure-control population");
        RequireClose(expected.SealedHabitatPopulationMillions, actual.SealedHabitatPopulationMillions, "sealed-habitat population");
        RequireClose(expected.ArtificialBiospherePopulationMillions, actual.ArtificialBiospherePopulationMillions, "artificial-biosphere population");
        RequireClose(expected.RadiationShieldingPopulationMillions, actual.RadiationShieldingPopulationMillions, "radiation-shielding population");
    }

    private static void RequireClose(double expected, double actual, string name)
    {
        if (Math.Abs(expected - actual) > 0.000000001)
            throw new InvalidOperationException($"Civilization habitat-support {name} does not conserve per-system totals.");
    }
}

public interface ICivilizationHabitatSupportBurdenView
{
    CivilizationHabitatSupportBurden Build(GalaxyState galaxy, int civilizationId);
}

/// <summary>
/// Owner-scoped raw aggregation over populated colonies. It exposes no hidden foreign observer
/// view and creates no new persistent state. Unknown legacy environment remains a distinct
/// classification instead of being collapsed into zero support need.
/// </summary>
public sealed class CurrentCivilizationHabitatSupportBurdenView : ICivilizationHabitatSupportBurdenView
{
    private readonly IColonyHabitatSupportBurdenView _colonyView;

    public CurrentCivilizationHabitatSupportBurdenView(IColonyHabitatSupportBurdenView? colonyView = null)
    {
        _colonyView = colonyView ?? new CurrentColonyHabitatSupportBurdenView();
    }

    public CivilizationHabitatSupportBurden Build(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!galaxy.Civilizations.Any(civilization => civilization.Id == civilizationId))
            throw new InvalidOperationException($"Unknown civilization {civilizationId}.");

        var populatedColonies = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId)
            .OrderBy(colony => colony.Id)
            .Where(colony =>
            {
                if (!double.IsFinite(colony.PopulationMillions) || colony.PopulationMillions < 0.0)
                    throw new InvalidOperationException($"Colony {colony.Id} has invalid population {colony.PopulationMillions}.");
                return colony.PopulationMillions > 0.0;
            })
            .ToArray();

        var burdens = populatedColonies
            .Select(colony => _colonyView.Build(galaxy, colony.Id))
            .ToArray();

        var systems = burdens
            .GroupBy(burden => burden.SystemId)
            .OrderBy(group => group.Key)
            .Select(group => BuildSystem(civilizationId, group.Key, group.ToArray()))
            .ToArray();

        var totals = burdens.Aggregate(
            HabitatSupportBurdenTotals.Zero,
            (current, burden) => current.Plus(burden));

        return new CivilizationHabitatSupportBurden(
            civilizationId,
            burdens.Length,
            burdens.Count(burden => burden.UsesExactOccupiedBody),
            burdens.Count(burden => !burden.UsesExactOccupiedBody),
            burdens.Count(burden => burden.Environment?.UsesPrototypeHabitatSupportedFallback == true),
            burdens.Select(burden => burden.SpeciesId).Distinct(StringComparer.Ordinal).Count(),
            totals,
            systems).Validated();
    }

    private static SystemHabitatSupportBurden BuildSystem(
        int civilizationId,
        int systemId,
        IReadOnlyList<ColonyHabitatSupportBurden> burdens)
    {
        var totals = burdens.Aggregate(
            HabitatSupportBurdenTotals.Zero,
            (current, burden) => current.Plus(burden));

        return new SystemHabitatSupportBurden(
            civilizationId,
            systemId,
            burdens.Count,
            burdens.Count(burden => burden.UsesExactOccupiedBody),
            burdens.Count(burden => !burden.UsesExactOccupiedBody),
            burdens.Count(burden => burden.Environment?.UsesPrototypeHabitatSupportedFallback == true),
            burdens.Select(burden => burden.SpeciesId).Distinct(StringComparer.Ordinal).Count(),
            totals).Validated();
    }
}
