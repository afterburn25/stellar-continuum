using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Territory;

/// <summary>Versioned, embedded balance data: independent of working directory and packaged with the game.</summary>
public sealed class TerritorialTuning
{
    public Dictionary<string, double> Values { get; init; } = new();
    public TerritorialInstallationDefinition[] Installations { get; init; } = Array.Empty<TerritorialInstallationDefinition>();
    private static readonly string[] Required = { "ReviewDays", "ColonyRange", "OutpostRange", "RelayConnectionRange", "RelayAttenuation", "IndependentWeight", "FrontierMinimumPolitical", "FrontierMinimumAdministration", "FrontierMinimumSupply", "MinimumTaxCollection", "ColonyStrength", "PopulationStrength", "InfrastructureStrength", "CapitalTierStrength", "OutpostStrength", "PoliticalFalloff", "AdministrationFalloff", "EstablishedPolitical", "EstablishedAdministration", "EstablishedSupply", "ContestedMinimumShare", "ContestedMaximumGap", "ControlMinimum", "DominanceMinimumShare", "FrontierSetupBase", "FrontierSetupIsolation", "FrontierTimeBase", "FrontierTimeIsolation", "AdministrationPenalty", "SupplyPenalty", "ResearchNetworkInfluence", "OrbitalShipyardInfluence", "MiningNetworkInfluence", "RecognizedClaimInfluence" };
    public static TerritorialTuning Load()
    {
        using var stream = typeof(TerritorialTuning).Assembly.GetManifestResourceStream("Game.Territory.Balance.json")
            ?? throw new InvalidDataException("Missing embedded territorial balance data: data/territory/balance-v1.json.");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    }
    public static TerritorialTuning Parse(string json)
    {
        var options = new JsonSerializerOptions { UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow };
        options.Converters.Add(new JsonStringEnumConverter());
        var data = JsonSerializer.Deserialize<TerritorialTuning>(json, options)
            ?? throw new InvalidDataException("Territorial balance data is empty.");
        if (data.Values is null || data.Values.Count != Required.Length ||
            Required.Any(key => !data.Values.TryGetValue(key, out var value) || !double.IsFinite(value) || value <= 0))
            throw new InvalidDataException("Territorial balance requires every named finite positive tuning value.");
        foreach (var key in new[] { "RelayAttenuation", "FrontierMinimumAdministration", "FrontierMinimumSupply", "MinimumTaxCollection", "EstablishedAdministration", "EstablishedSupply", "ContestedMinimumShare", "ContestedMaximumGap", "ControlMinimum", "DominanceMinimumShare" })
            if (data.Values[key] > 1) throw new InvalidDataException($"Territorial balance {key} must be between zero and one.");
        if (data.Installations is null || data.Installations.Length != Enum.GetValues<TerritorialInstallationKind>().Length ||
            data.Installations.Any(i => i is null) || data.Installations.Select(i => i.Kind).Distinct().Count() != data.Installations.Length)
            throw new InvalidDataException("Territorial balance must define each installation exactly once.");
        foreach (var site in data.Installations)
            if (!Enum.IsDefined(site.Kind) || string.IsNullOrWhiteSpace(site.Name) ||
                new[] { site.Credits, site.Industry, site.Days, site.Upkeep, site.Political, site.Range }.Any(v => !double.IsFinite(v) || v <= 0) ||
                new[] { site.Administration, site.Trade, site.Military }.Any(v => !double.IsFinite(v) || v < 0 || v > 1))
                throw new InvalidDataException($"Invalid territorial balance for {site.Kind}.");
        return data;
    }
}
