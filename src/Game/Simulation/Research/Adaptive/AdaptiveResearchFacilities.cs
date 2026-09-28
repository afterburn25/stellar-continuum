using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchStageFacilityRequirement(
    IReadOnlyList<string> AllOf,
    IReadOnlyList<string> AnyOf);

public sealed record ResearchFacilityInstitutionDefinition(
    string Id,
    double EffectiveLabUnits,
    IReadOnlyList<string> FacilityCapabilities);

/// <summary>
/// Immutable research-facing facility capability catalog. Construction/economy owns actual
/// facility instances, costs, location and access; Adaptive Research only consumes capability IDs.
/// </summary>
public sealed class AdaptiveResearchFacilityCatalog
{
    private readonly IReadOnlyDictionary<string, ResearchStageFacilityRequirement> _stageRequirements;

    private AdaptiveResearchFacilityCatalog(
        IReadOnlySet<string> facilityCapabilityIds,
        IReadOnlyDictionary<string, ResearchFacilityInstitutionDefinition> institutions,
        IReadOnlyDictionary<string, ResearchStageFacilityRequirement> stageRequirements)
    {
        FacilityCapabilityIds = facilityCapabilityIds;
        Institutions = institutions;
        _stageRequirements = stageRequirements;
    }

    public IReadOnlySet<string> FacilityCapabilityIds { get; }
    public IReadOnlyDictionary<string, ResearchFacilityInstitutionDefinition> Institutions { get; }

    public ResearchStageFacilityRequirement? GetStageRequirement(string nodeId, ResearchMaturity stage) =>
        _stageRequirements.TryGetValue(StageKey(nodeId, stage), out var requirement) ? requirement : null;

    public static AdaptiveResearchFacilityCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        var root = Path.GetFullPath(rootPath);
        using var indexDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_facility_index.json")));
        var indexRoot = indexDoc.RootElement;
        ValidateCatalogId(indexRoot, catalog.Metadata.CatalogId, "research_facility_index.json");

        var files = indexRoot.GetProperty("facility_catalog_files")
            .EnumerateArray()
            .Select(value => value.GetString() ?? throw new InvalidDataException("Facility catalog filename cannot be null."))
            .ToArray();
        if (files.Length == 0)
            throw new InvalidDataException("research_facility_index.json contains no facility catalogs.");

        var capabilities = new HashSet<string>(StringComparer.Ordinal);
        var institutions = new Dictionary<string, ResearchFacilityInstitutionDefinition>(StringComparer.Ordinal);
        var stagedElements = new List<(string FileName, JsonElement Root)>();
        var documents = new List<JsonDocument>();

        try
        {
            foreach (var fileName in files)
            {
                var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, fileName)));
                documents.Add(document);
                var fileRoot = document.RootElement;
                ValidateCatalogId(fileRoot, catalog.Metadata.CatalogId, fileName);

                foreach (var capability in fileRoot.GetProperty("facility_capabilities").EnumerateArray())
                {
                    var id = RequiredString(capability, "id", fileName);
                    if (!capabilities.Add(id))
                        throw new InvalidDataException($"Duplicate declared facility capability '{id}' across facility catalogs.");
                }
                stagedElements.Add((fileName, fileRoot));
            }

            foreach (var (fileName, fileRoot) in stagedElements)
            {
                foreach (var institution in fileRoot.GetProperty("institution_archetypes").EnumerateArray())
                {
                    var id = RequiredString(institution, "id", fileName);
                    var institutionCapabilities = StringArray(institution, "facility_capabilities");
                    foreach (var capability in institutionCapabilities)
                        if (!capabilities.Contains(capability))
                            throw new InvalidDataException($"Institution '{id}' references undeclared facility capability '{capability}'.");

                    var labUnits = institution.GetProperty("effective_lab_units").GetDouble();
                    if (labUnits <= 0.0 || double.IsNaN(labUnits) || double.IsInfinity(labUnits))
                        throw new InvalidDataException($"Institution '{id}' has invalid effective_lab_units {labUnits}.");
                    if (!institutions.TryAdd(id, new ResearchFacilityInstitutionDefinition(id, labUnits, institutionCapabilities)))
                        throw new InvalidDataException($"Duplicate research institution '{id}'.");
                }
            }

            var providedCapabilities = institutions.Values
                .SelectMany(institution => institution.FacilityCapabilities)
                .ToHashSet(StringComparer.Ordinal);
            var stageRequirements = new Dictionary<string, ResearchStageFacilityRequirement>(StringComparer.Ordinal);

            foreach (var (fileName, fileRoot) in stagedElements)
            {
                if (!fileRoot.TryGetProperty("stage_requirements", out var stageRoot))
                    continue;

                foreach (var nodeProperty in stageRoot.EnumerateObject())
                {
                    if (!catalog.Nodes.ContainsKey(nodeProperty.Name))
                        throw new InvalidDataException($"{fileName} stage requirement references unknown node '{nodeProperty.Name}'.");

                    foreach (var stageProperty in nodeProperty.Value.EnumerateObject())
                    {
                        var stage = ParseStage(stageProperty.Name, fileName, nodeProperty.Name);
                        var requirement = new ResearchStageFacilityRequirement(
                            StringArray(stageProperty.Value, "all_of"),
                            StringArray(stageProperty.Value, "any_of"));

                        foreach (var capability in requirement.AllOf.Concat(requirement.AnyOf))
                        {
                            if (!capabilities.Contains(capability))
                                throw new InvalidDataException($"{fileName}:{nodeProperty.Name} references unknown facility capability '{capability}'.");
                            if (!providedCapabilities.Contains(capability))
                                throw new InvalidDataException($"No research institution provides required facility capability '{capability}'.");
                        }

                        var key = StageKey(nodeProperty.Name, stage);
                        if (!stageRequirements.TryAdd(key, requirement))
                            throw new InvalidDataException($"Duplicate facility stage requirement for '{nodeProperty.Name}' at {stage}.");
                    }
                }
            }

            return new AdaptiveResearchFacilityCatalog(
                capabilities,
                new ReadOnlyDictionary<string, ResearchFacilityInstitutionDefinition>(institutions),
                new ReadOnlyDictionary<string, ResearchStageFacilityRequirement>(stageRequirements));
        }
        finally
        {
            foreach (var document in documents)
                document.Dispose();
        }
    }

    private static string StageKey(string nodeId, ResearchMaturity stage) => $"{nodeId}\u001f{stage}";

    private static ResearchMaturity ParseStage(string value, string fileName, string nodeId) => value switch
    {
        "experimental" => ResearchMaturity.Experimental,
        "demonstrated" => ResearchMaturity.Demonstrated,
        "engineering" => ResearchMaturity.Engineering,
        _ => throw new InvalidDataException($"{fileName}:{nodeId} uses unsupported facility stage '{value}'."),
    };

    private static void ValidateCatalogId(JsonElement root, string expected, string fileName)
    {
        var actual = RequiredString(root, "catalog_id", fileName);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{fileName} catalog_id '{actual}' does not match '{expected}'.");
    }

    private static string RequiredString(JsonElement element, string name, string context) =>
        element.TryGetProperty(name, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? throw new InvalidDataException($"{context}.{name} cannot be null.")
            : throw new InvalidDataException($"{context} is missing string '{name}'.");

    private static IReadOnlyList<string> StringArray(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var property))
            return Array.Empty<string>();
        return property.EnumerateArray()
            .Select(value => value.GetString() ?? throw new InvalidDataException($"{name} contains null."))
            .ToArray();
    }
}
