using System;
using System.Collections.Generic;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Economy;

/// <summary>
/// Player-facing sovereign money. The simulation keeps one normalized budget unit so every
/// civilization uses the same economic rules; this catalog gives that unit a local denomination.
/// Interstellar Credits are deliberately absent until a later clearing-currency system exists.
/// </summary>
public sealed record SovereignCurrencyDefinition(
    string Name,
    string Code,
    string Symbol,
    double LocalUnitsPerBudgetUnit)
{
    public string Format(double budgetUnits, bool includeCode = true)
    {
        var local = Math.Max(0.0, budgetUnits) * LocalUnitsPerBudgetUnit;
        var amount = local >= 1_000_000_000_000.0 ? $"{local / 1_000_000_000_000.0:0.##}T" :
            local >= 1_000_000_000.0 ? $"{local / 1_000_000_000.0:0.##}B" :
            local >= 1_000_000.0 ? $"{local / 1_000_000.0:0.##}M" :
            local >= 1_000.0 ? $"{local / 1_000.0:0.##}K" : $"{local:0.##}";
        return $"{Symbol}{amount}{(includeCode ? $" {Code}" : string.Empty)}";
    }

    public string FormatRate(double budgetUnitsPerDay) =>
        $"{(budgetUnitsPerDay < 0 ? "−" : budgetUnitsPerDay > 0 ? "+" : string.Empty)}{Format(Math.Abs(budgetUnitsPerDay))}/day";
}

public static class SovereignCurrencyCatalog
{
    private static readonly IReadOnlyDictionary<string, SovereignCurrencyDefinition> BySpecies =
        new Dictionary<string, SovereignCurrencyDefinition>(StringComparer.Ordinal)
        {
            [SpeciesCatalog.TerranBaselineId] = new("United Earth Dollar", "UED", "$", 10_000_000.0),
            [SpeciesCatalog.PelagicHighPressureId] = new("Tide Mark", "TM", "◈", 12_000_000.0),
            [SpeciesCatalog.CompactHighGravityId] = new("Forge Crown", "FC", "◆", 8_000_000.0),
            [SpeciesCatalog.CryogenicHydrocarbonId] = new("Thermal Ledger", "TL", "◇", 15_000_000.0),
        };

    public static SovereignCurrencyDefinition ForSpecies(string speciesId) =>
        BySpecies.TryGetValue(speciesId, out var currency)
            ? currency
            : new SovereignCurrencyDefinition("Sovereign Currency", "SC", "¤", 10_000_000.0);

    public static SovereignCurrencyDefinition ForCivilization(GalaxyState galaxy, int civilizationId) =>
        ForSpecies(galaxy.Civilizations.FindById(civilizationId).SpeciesId);

    private static CivilizationState FindById(this IList<CivilizationState> civilizations, int civilizationId)
    {
        foreach (var civilization in civilizations)
            if (civilization.Id == civilizationId)
                return civilization;
        throw new InvalidOperationException($"Civilization {civilizationId} does not exist.");
    }
}
