using System.Globalization;
using System.Text.Json;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    double Value(string value) => value switch { "NaN" => double.NaN, "+Infinity" => double.PositiveInfinity,
        "-Infinity" => double.NegativeInfinity, _ => double.Parse(value, CultureInfo.InvariantCulture) };
    var values = new[] { "-Infinity", "-100", "-0", "0", "0.000000049", "0.00000005", "0.000000099",
        "0.000099999", "0.0001", "0.000100001", "0.099999999", "0.1", "99.9999999", "100",
        "99999.9999999", "100000", "99999999999.9999", "100000", "100000000", "100000000000", "1E+300",
        "NaN", "+Infinity" };
    var species = new[] { SpeciesCatalog.TerranBaselineId, SpeciesCatalog.PelagicHighPressureId,
        SpeciesCatalog.CompactHighGravityId, SpeciesCatalog.CryogenicHydrocarbonId, "unknown_civilization" };
    var currencyCases = new List<object>();
    foreach (var speciesId in species)
    foreach (var value in values)
    {
        var currency = SovereignCurrencyCatalog.ForSpecies(speciesId);
        currencyCases.Add(new { SpeciesId = speciesId, BudgetUnits = value, Currency = new { currency.Name, currency.Code, currency.Symbol, currency.LocalUnitsPerBudgetUnit },
            Format = currency.Format(Value(value)), FormatWithoutCode = currency.Format(Value(value), false), Rate = currency.FormatRate(Value(value)) });
    }
    var fixedValues = new[] { "-Infinity", "-7.25", "-0", "0", "0.05", "7.15", "7.25", "1E+300", "NaN", "+Infinity" };
    var fixedCases = fixedValues.Select(value => new { Value = value, Format = Value(value).ToString("0.0", CultureInfo.InvariantCulture) }).ToArray();
    var fixedPaddingCases = new[] { "123456789012345.6", "1E+300" }.Select(value => new { Value = value,
        FixedTwo = Value(value).ToString("0.00", CultureInfo.InvariantCulture), FixedThree = Value(value).ToString("0.000", CultureInfo.InvariantCulture) }).ToArray();
    CivilizationState Civ(int id, bool ancient = false, string species = SpeciesCatalog.TerranBaselineId) => new(id, "Civ " + id, 0, CivilizationArchetype.Scientific,
        new(0,0,0,0,0,0,false), true, ancient ? CivilizationDevelopmentStage.AncientSpacefaring : CivilizationDevelopmentStage.WarpCapable,
        ancient, SpeciesId: species);
    var civilizations = new List<CivilizationState> { Civ(9), Civ(4, true) };
    var seeded = new ConstructionSeeder().Seed(civilizations);
    var lookupGalaxy = new GalaxyState { Seed = 1, Systems = Array.Empty<StarSystemState>(), Civilizations = new List<CivilizationState> { Civ(4), Civ(4, species: SpeciesCatalog.CompactHighGravityId) },
        Fleets = new List<FleetState>(), Colonies = new List<ColonyState>(), Economies = Array.Empty<CivilizationEconomyState>(), Technologies = new List<Game.Simulation.Research.TechnologyState>(),
        ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(), ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(), PlayerCivilizationId = 4,
        Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState() };
    var lookupCurrency = SovereignCurrencyCatalog.ForCivilization(lookupGalaxy, 4);
    var result = new { Format = "stellar-construction-currency-oracle-v1", CurrencyCases = currencyCases, FixedOneDecimalCases = fixedCases, FixedPaddingCases = fixedPaddingCases,
        CivilizationLookup = new { Name = lookupCurrency.Name, Code = lookupCurrency.Code, Symbol = lookupCurrency.Symbol, LocalUnitsPerBudgetUnit = lookupCurrency.LocalUnitsPerBudgetUnit },
        Seed = new { Before = civilizations, After = seeded } };
    File.WriteAllText(args[0], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }) + Environment.NewLine);
    Console.WriteLine($"Exported {currencyCases.Count} construction/currency oracle cases.");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    return 1;
}
