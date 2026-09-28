using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public enum ResearchApplicabilityTraitScope
{
    Civilization,
    PopulationOrSpecies,
}

public sealed record ResearchApplicabilityTraitDefinition(
    string Id,
    ResearchApplicabilityTraitScope Scope,
    bool Mutable);

/// <summary>
/// Immutable trait semantics. Trait IDs remain in the shared catalog; this companion catalog
/// tells runtime code whether a requirement is civilization-wide or target-context scoped.
/// </summary>
public sealed class AdaptiveResearchApplicabilityCatalog
{
    private AdaptiveResearchApplicabilityCatalog(
        IReadOnlyDictionary<string, ResearchApplicabilityTraitDefinition> traits)
    {
        Traits = traits;
    }

    public IReadOnlyDictionary<string, ResearchApplicabilityTraitDefinition> Traits { get; }

    public ResearchApplicabilityTraitDefinition GetTrait(string traitId) =>
        Traits.TryGetValue(traitId, out var definition)
            ? definition
            : throw new KeyNotFoundException($"Unknown Adaptive Research applicability trait '{traitId}'.");

    public static AdaptiveResearchApplicabilityCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        var path = Path.Combine(Path.GetFullPath(rootPath), "applicability_traits.json");
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        var catalogId = root.GetProperty("catalog_id").GetString();
        if (!string.Equals(catalog.Metadata.CatalogId, catalogId, StringComparison.Ordinal))
            throw new InvalidDataException("applicability_traits.json catalog_id does not match the loaded research catalog.");

        var result = new Dictionary<string, ResearchApplicabilityTraitDefinition>(StringComparer.Ordinal);
        foreach (var element in root.GetProperty("traits").EnumerateArray())
        {
            var id = RequiredString(element, "id");
            var scope = RequiredString(element, "scope") switch
            {
                "civilization" => ResearchApplicabilityTraitScope.Civilization,
                "population_or_species" => ResearchApplicabilityTraitScope.PopulationOrSpecies,
                var unknown => throw new InvalidDataException($"Unknown applicability trait scope '{unknown}' on '{id}'."),
            };
            var mutable = element.GetProperty("mutable").GetBoolean();
            if (!catalog.TraitIds.Contains(id))
                throw new InvalidDataException($"Applicability trait '{id}' is not registered by the base research catalog.");
            if (!result.TryAdd(id, new ResearchApplicabilityTraitDefinition(id, scope, mutable)))
                throw new InvalidDataException($"Duplicate applicability trait '{id}'.");
        }

        if (result.Count != catalog.Metadata.TraitCount)
            throw new InvalidDataException($"Applicability trait count {result.Count} does not match base catalog count {catalog.Metadata.TraitCount}.");

        return new AdaptiveResearchApplicabilityCatalog(
            new ReadOnlyDictionary<string, ResearchApplicabilityTraitDefinition>(result));
    }

    private static string RequiredString(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? throw new InvalidDataException($"{propertyName} cannot be null.")
            : throw new InvalidDataException($"Missing required string '{propertyName}'.");
}
