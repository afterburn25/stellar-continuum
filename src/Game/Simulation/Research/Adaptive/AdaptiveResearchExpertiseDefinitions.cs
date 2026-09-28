using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public enum ResearchCompetenceComponent
{
    Theoretical,
    Experimental,
    Engineering,
}

public enum ResearchTacitAssimilationStage
{
    Access,
    Interpreted,
    Codified,
    Trained,
    NativePractice,
}

public enum ResearchTacitScopeKind
{
    KnowledgeField,
    TechnologyNode,
    SolutionFamily,
    ForeignLineage,
    FacilityOrProcess,
}

public sealed record ResearchCompetenceVector(double Theoretical, double Experimental, double Engineering)
{
    public double Get(ResearchCompetenceComponent component) => component switch
    {
        ResearchCompetenceComponent.Theoretical => Theoretical,
        ResearchCompetenceComponent.Experimental => Experimental,
        ResearchCompetenceComponent.Engineering => Engineering,
        _ => throw new ArgumentOutOfRangeException(nameof(component)),
    };
}

public sealed record ResearchKnowledgeFieldDefinition(
    string Id,
    string Name,
    string Family,
    IReadOnlyList<string> RelatedFieldIds);

public sealed record ResearchTacitAssetTypeDefinition(
    string Id,
    IReadOnlyList<ResearchCompetenceComponent> SupportedComponents);

public sealed record ResearchInstitutionSpecializationDefinition(
    string InstitutionArchetypeId,
    double EffectiveLabUnits,
    IReadOnlyList<string> SpecializedFieldIds,
    IReadOnlyList<string> FacilityCapabilityIds);

public sealed record ResearchReadinessComponentWeights(
    double FieldCompetence,
    double FacilityReadiness,
    double EvidenceReadiness,
    double TacitExpertise);

public sealed record ResearchStageCompetenceWeights(
    double Theoretical,
    double Experimental,
    double Engineering);

public sealed record ResearchExpertiseRuntimePolicy(
    IReadOnlyDictionary<ResearchMaturity, ResearchCompetenceVector> StagePracticeGain,
    double RelatedFieldTransferFraction,
    double MinimumGainFactor,
    ResearchCompetenceVector AnnualAtrophyRates,
    double AtrophyGraceYears,
    double GeneralLabMatchingFactor,
    double SpecializedMatchingFactor,
    double NonmatchingSpecialistFactor,
    IReadOnlyDictionary<ResearchTacitAssimilationStage, double> TacitAssimilationFactors,
    double TacitBestWeight,
    double TacitMeanWeight,
    double PreservationFloorFraction,
    double EstablishedKnowledgeTheoreticalFloorFraction);

/// <summary>
/// Immutable competence, institution specialization, tacit-asset and readiness policy catalog.
/// </summary>
public sealed class AdaptiveResearchExpertiseCatalog
{
    private AdaptiveResearchExpertiseCatalog(
        IReadOnlyDictionary<string, ResearchKnowledgeFieldDefinition> fields,
        IReadOnlyDictionary<ResearchMaturity, ResearchStageCompetenceWeights> stageWeights,
        ResearchReadinessComponentWeights readinessWeights,
        IReadOnlyDictionary<string, ResearchTacitAssetTypeDefinition> tacitAssetTypes,
        IReadOnlyDictionary<string, ResearchInstitutionSpecializationDefinition> institutions,
        ResearchExpertiseRuntimePolicy runtimePolicy)
    {
        Fields = fields;
        StageWeights = stageWeights;
        ReadinessWeights = readinessWeights;
        TacitAssetTypes = tacitAssetTypes;
        Institutions = institutions;
        RuntimePolicy = runtimePolicy;
    }

    public IReadOnlyDictionary<string, ResearchKnowledgeFieldDefinition> Fields { get; }
    public IReadOnlyDictionary<ResearchMaturity, ResearchStageCompetenceWeights> StageWeights { get; }
    public ResearchReadinessComponentWeights ReadinessWeights { get; }
    public IReadOnlyDictionary<string, ResearchTacitAssetTypeDefinition> TacitAssetTypes { get; }
    public IReadOnlyDictionary<string, ResearchInstitutionSpecializationDefinition> Institutions { get; }
    public ResearchExpertiseRuntimePolicy RuntimePolicy { get; }

    public static AdaptiveResearchExpertiseCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchFacilityCatalog facilities)
    {
        var root = Path.GetFullPath(rootPath);
        var fields = LoadFields(root, catalog);
        var (stageWeights, readinessWeights) = LoadCompetencePolicy(root, catalog);
        var tacitTypes = LoadTacitTypes(root, catalog);
        var institutions = LoadInstitutionSpecializations(root, catalog, facilities, fields);
        var runtimePolicy = LoadRuntimePolicy(root, catalog);
        return new AdaptiveResearchExpertiseCatalog(
            fields,
            stageWeights,
            readinessWeights,
            tacitTypes,
            institutions,
            runtimePolicy);
    }

    private static IReadOnlyDictionary<string, ResearchKnowledgeFieldDefinition> LoadFields(
        string root,
        AdaptiveResearchCatalog catalog)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "knowledge_fields.json")));
        ValidateCatalogId(document.RootElement, catalog.Metadata.CatalogId, "knowledge_fields.json");
        var result = new Dictionary<string, ResearchKnowledgeFieldDefinition>(StringComparer.Ordinal);
        foreach (var element in document.RootElement.GetProperty("fields").EnumerateArray())
        {
            var id = RequiredString(element, "id", "knowledge_fields.json");
            result.Add(id, new ResearchKnowledgeFieldDefinition(
                id,
                RequiredString(element, "name", id),
                RequiredString(element, "family", id),
                StringArray(element, "related_fields")));
        }
        if (result.Count != catalog.Metadata.KnowledgeFieldCount || result.Keys.Any(id => !catalog.KnowledgeFieldIds.Contains(id)))
            throw new InvalidDataException("Runtime expertise field catalog does not match the Adaptive Research knowledge-field catalog.");
        foreach (var field in result.Values)
            foreach (var related in field.RelatedFieldIds)
                if (!result.ContainsKey(related))
                    throw new InvalidDataException($"Knowledge field '{field.Id}' references unknown related field '{related}'.");
        return new ReadOnlyDictionary<string, ResearchKnowledgeFieldDefinition>(result);
    }

    private static (IReadOnlyDictionary<ResearchMaturity, ResearchStageCompetenceWeights>, ResearchReadinessComponentWeights) LoadCompetencePolicy(
        string root,
        AdaptiveResearchCatalog catalog)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_competence_model.json")));
        ValidateCatalogId(document.RootElement, catalog.Metadata.CatalogId, "research_competence_model.json");
        var stageRoot = document.RootElement.GetProperty("project_field_readiness").GetProperty("stage_component_weights");
        var stages = new Dictionary<ResearchMaturity, ResearchStageCompetenceWeights>
        {
            [ResearchMaturity.Experimental] = ParseStageWeights(stageRoot.GetProperty("experimental")),
            [ResearchMaturity.Demonstrated] = ParseStageWeights(stageRoot.GetProperty("demonstrated")),
            [ResearchMaturity.Engineering] = ParseStageWeights(stageRoot.GetProperty("engineering")),
        };
        foreach (var pair in stages)
        {
            var sum = pair.Value.Theoretical + pair.Value.Experimental + pair.Value.Engineering;
            if (Math.Abs(sum - 1.0) > 0.000001)
                throw new InvalidDataException($"Competence stage weights for {pair.Key} sum to {sum}, not 1.0.");
        }

        var inputs = document.RootElement.GetProperty("project_readiness").GetProperty("inputs");
        var weights = new ResearchReadinessComponentWeights(
            inputs.GetProperty("field_competence").GetProperty("weight").GetDouble(),
            inputs.GetProperty("facility_readiness").GetProperty("weight").GetDouble(),
            inputs.GetProperty("evidence_readiness").GetProperty("weight").GetDouble(),
            inputs.GetProperty("tacit_expertise").GetProperty("weight").GetDouble());
        var weightSum = weights.FieldCompetence + weights.FacilityReadiness + weights.EvidenceReadiness + weights.TacitExpertise;
        if (Math.Abs(weightSum - 1.0) > 0.000001)
            throw new InvalidDataException($"Project readiness component weights sum to {weightSum}, not 1.0.");
        return (new ReadOnlyDictionary<ResearchMaturity, ResearchStageCompetenceWeights>(stages), weights);
    }

    private static IReadOnlyDictionary<string, ResearchTacitAssetTypeDefinition> LoadTacitTypes(
        string root,
        AdaptiveResearchCatalog catalog)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "tacit_knowledge_model.json")));
        ValidateCatalogId(document.RootElement, catalog.Metadata.CatalogId, "tacit_knowledge_model.json");
        var result = new Dictionary<string, ResearchTacitAssetTypeDefinition>(StringComparer.Ordinal);
        foreach (var element in document.RootElement.GetProperty("knowledge_asset_types").EnumerateArray())
        {
            var id = RequiredString(element, "id", "tacit_knowledge_model.json");
            var supports = StringArray(element, "primary_support").Select(ParseComponent).Distinct().ToArray();
            result.Add(id, new ResearchTacitAssetTypeDefinition(id, supports));
        }
        return new ReadOnlyDictionary<string, ResearchTacitAssetTypeDefinition>(result);
    }

    private static IReadOnlyDictionary<string, ResearchInstitutionSpecializationDefinition> LoadInstitutionSpecializations(
        string root,
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchFacilityCatalog facilities,
        IReadOnlyDictionary<string, ResearchKnowledgeFieldDefinition> fields)
    {
        using var indexDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_facility_index.json")));
        ValidateCatalogId(indexDoc.RootElement, catalog.Metadata.CatalogId, "research_facility_index.json");
        var result = new Dictionary<string, ResearchInstitutionSpecializationDefinition>(StringComparer.Ordinal);
        foreach (var fileElement in indexDoc.RootElement.GetProperty("facility_catalog_files").EnumerateArray())
        {
            var fileName = fileElement.GetString() ?? throw new InvalidDataException("Facility catalog filename cannot be null.");
            using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, fileName)));
            ValidateCatalogId(document.RootElement, catalog.Metadata.CatalogId, fileName);
            foreach (var institution in document.RootElement.GetProperty("institution_archetypes").EnumerateArray())
            {
                var id = RequiredString(institution, "id", fileName);
                var specialized = StringArray(institution, "specialized_fields");
                foreach (var fieldId in specialized)
                    if (!fields.ContainsKey(fieldId))
                        throw new InvalidDataException($"Institution '{id}' references unknown specialized field '{fieldId}'.");
                var capabilities = StringArray(institution, "facility_capabilities");
                if (!facilities.Institutions.TryGetValue(id, out var baseInstitution))
                    throw new InvalidDataException($"Expertise institution '{id}' is missing from the research facility catalog.");
                result.Add(id, new ResearchInstitutionSpecializationDefinition(
                    id,
                    baseInstitution.EffectiveLabUnits,
                    specialized,
                    capabilities));
            }
        }
        if (result.Count != facilities.Institutions.Count)
            throw new InvalidDataException("Expertise institution count does not match research facility institution count.");
        return new ReadOnlyDictionary<string, ResearchInstitutionSpecializationDefinition>(result);
    }

    private static ResearchExpertiseRuntimePolicy LoadRuntimePolicy(string root, AdaptiveResearchCatalog catalog)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_runtime_expertise_policy.json")));
        var json = document.RootElement;
        ValidateCatalogId(json, catalog.Metadata.CatalogId, "research_runtime_expertise_policy.json");

        var gainsRoot = json.GetProperty("competence_practice_gain_per_completed_stage");
        var gains = new Dictionary<ResearchMaturity, ResearchCompetenceVector>
        {
            [ResearchMaturity.Experimental] = ParseVector(gainsRoot.GetProperty("experimental")),
            [ResearchMaturity.Demonstrated] = ParseVector(gainsRoot.GetProperty("demonstrated")),
            [ResearchMaturity.Engineering] = ParseVector(gainsRoot.GetProperty("engineering")),
        };
        var gainRules = json.GetProperty("competence_gain_rules");
        var atrophy = json.GetProperty("annual_active_competence_atrophy_toward_preservation_floor");
        var facility = json.GetProperty("facility_readiness");
        var tacit = json.GetProperty("tacit_assimilation_factors");
        var tacitFactors = new Dictionary<ResearchTacitAssimilationStage, double>
        {
            [ResearchTacitAssimilationStage.Access] = tacit.GetProperty("access").GetDouble(),
            [ResearchTacitAssimilationStage.Interpreted] = tacit.GetProperty("interpreted").GetDouble(),
            [ResearchTacitAssimilationStage.Codified] = tacit.GetProperty("codified").GetDouble(),
            [ResearchTacitAssimilationStage.Trained] = tacit.GetProperty("trained").GetDouble(),
            [ResearchTacitAssimilationStage.NativePractice] = tacit.GetProperty("native_practice").GetDouble(),
        };
        var preservation = json.GetProperty("preservation_floor");
        return new ResearchExpertiseRuntimePolicy(
            new ReadOnlyDictionary<ResearchMaturity, ResearchCompetenceVector>(gains),
            gainRules.GetProperty("related_field_transfer_fraction").GetDouble(),
            gainRules.GetProperty("minimum_gain_factor").GetDouble(),
            ParseVector(atrophy),
            json.GetProperty("atrophy_rules").GetProperty("only_after_years_without_meaningful_component_activity").GetDouble(),
            facility.GetProperty("general_lab_matching_factor").GetDouble(),
            facility.GetProperty("specialized_matching_factor").GetDouble(),
            facility.GetProperty("nonmatching_specialist_factor").GetDouble(),
            new ReadOnlyDictionary<ResearchTacitAssimilationStage, double>(tacitFactors),
            0.70,
            0.30,
            preservation.GetProperty("floor_fraction").GetDouble(),
            preservation.GetProperty("established_knowledge_theoretical_floor_fraction_of_historical_peak").GetDouble());
    }

    private static ResearchStageCompetenceWeights ParseStageWeights(JsonElement element) => new(
        element.GetProperty("theoretical").GetDouble(),
        element.GetProperty("experimental").GetDouble(),
        element.GetProperty("engineering").GetDouble());

    private static ResearchCompetenceVector ParseVector(JsonElement element) => new(
        element.GetProperty("theoretical").GetDouble(),
        element.GetProperty("experimental").GetDouble(),
        element.GetProperty("engineering").GetDouble());

    private static ResearchCompetenceComponent ParseComponent(string value) => value switch
    {
        "theoretical" => ResearchCompetenceComponent.Theoretical,
        "experimental" => ResearchCompetenceComponent.Experimental,
        "engineering" => ResearchCompetenceComponent.Engineering,
        _ => throw new InvalidDataException($"Unknown competence component '{value}'."),
    };

    private static void ValidateCatalogId(JsonElement root, string expected, string source)
    {
        var actual = RequiredString(root, "catalog_id", source);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{source} catalog_id '{actual}' does not match '{expected}'.");
    }

    private static string RequiredString(JsonElement element, string name, string source) =>
        element.TryGetProperty(name, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? throw new InvalidDataException($"{source}.{name} cannot be null.")
            : throw new InvalidDataException($"{source} is missing string '{name}'.");

    private static IReadOnlyList<string> StringArray(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var property))
            return Array.Empty<string>();
        return property.EnumerateArray().Select(value => value.GetString() ?? throw new InvalidDataException($"{name} contains null.")).ToArray();
    }
}
