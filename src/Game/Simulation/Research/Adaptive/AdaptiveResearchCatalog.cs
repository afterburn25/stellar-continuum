using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Immutable shared Adaptive Research definitions and wake-up indexes.
/// Civilization state stores stable IDs only and never copies this catalog.
/// </summary>
public sealed class AdaptiveResearchCatalog
{
    internal AdaptiveResearchCatalog(
        AdaptiveResearchCatalogMetadata metadata,
        IReadOnlyDictionary<string, AdaptiveResearchNodeDefinition> nodes,
        IReadOnlyDictionary<string, ResearchCapabilityDefinition> capabilities,
        IReadOnlyList<ResearchCapabilityImplication> capabilityImplications,
        IReadOnlySet<string> pressureIds,
        IReadOnlySet<string> traitIds,
        IReadOnlySet<string> evidenceTypeIds,
        IReadOnlySet<string> knowledgeFieldIds,
        ResearchLabScaling labScaling,
        IReadOnlyDictionary<string, DirectedResearchProgramStage> directedProgramStages,
        IReadOnlyDictionary<string, ResearchMaturityGrant> demonstratedGrants,
        IReadOnlyDictionary<string, ResearchMaturityGrant> matureGrants,
        IReadOnlyDictionary<string, ResearchDeploymentEventDefinition> deploymentEvents,
        IReadOnlyDictionary<string, IReadOnlyList<string>> childrenByPrerequisite,
        IReadOnlyDictionary<string, IReadOnlyList<string>> nodesByPressure,
        IReadOnlyDictionary<string, IReadOnlyList<string>> nodesByEvidence,
        IReadOnlyDictionary<string, IReadOnlyList<string>> nodesByTrait,
        IReadOnlyDictionary<string, IReadOnlyList<string>> nodesByCapabilityRequirement)
    {
        Metadata = metadata;
        Nodes = nodes;
        Capabilities = capabilities;
        CapabilityImplications = capabilityImplications;
        PressureIds = pressureIds;
        TraitIds = traitIds;
        EvidenceTypeIds = evidenceTypeIds;
        KnowledgeFieldIds = knowledgeFieldIds;
        LabScaling = labScaling;
        DirectedProgramStages = directedProgramStages;
        DemonstratedGrants = demonstratedGrants;
        MatureGrants = matureGrants;
        DeploymentEvents = deploymentEvents;
        ChildrenByPrerequisite = childrenByPrerequisite;
        NodesByPressure = nodesByPressure;
        NodesByEvidence = nodesByEvidence;
        NodesByTrait = nodesByTrait;
        NodesByCapabilityRequirement = nodesByCapabilityRequirement;
    }

    public AdaptiveResearchCatalogMetadata Metadata { get; }
    public IReadOnlyDictionary<string, AdaptiveResearchNodeDefinition> Nodes { get; }
    public IReadOnlyDictionary<string, ResearchCapabilityDefinition> Capabilities { get; }
    public IReadOnlyList<ResearchCapabilityImplication> CapabilityImplications { get; }
    public IReadOnlySet<string> PressureIds { get; }
    public IReadOnlySet<string> TraitIds { get; }
    public IReadOnlySet<string> EvidenceTypeIds { get; }
    public IReadOnlySet<string> KnowledgeFieldIds { get; }
    public ResearchLabScaling LabScaling { get; }
    public IReadOnlyDictionary<string, DirectedResearchProgramStage> DirectedProgramStages { get; }
    public IReadOnlyDictionary<string, ResearchMaturityGrant> DemonstratedGrants { get; }
    public IReadOnlyDictionary<string, ResearchMaturityGrant> MatureGrants { get; }
    public IReadOnlyDictionary<string, ResearchDeploymentEventDefinition> DeploymentEvents { get; }

    public IReadOnlyDictionary<string, IReadOnlyList<string>> ChildrenByPrerequisite { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> NodesByPressure { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> NodesByEvidence { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> NodesByTrait { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> NodesByCapabilityRequirement { get; }

    public AdaptiveResearchNodeDefinition GetNode(string nodeId) =>
        Nodes.TryGetValue(nodeId, out var node)
            ? node
            : throw new KeyNotFoundException($"Unknown Adaptive Research node '{nodeId}'.");

    public DirectedResearchProgramStage GetDirectedProgramStage(string stageId) =>
        DirectedProgramStages.TryGetValue(stageId, out var stage)
            ? stage
            : throw new KeyNotFoundException($"Unknown directed research stage '{stageId}'.");
}

public static class AdaptiveResearchCatalogLoader
{
    public static AdaptiveResearchCatalog LoadFromDirectory(string rootPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rootPath);
        var root = Path.GetFullPath(rootPath);
        if (!Directory.Exists(root))
            throw new DirectoryNotFoundException($"Adaptive Research data directory not found: {root}");

        using var indexDoc = LoadJson(root, "index.json");
        var indexRoot = indexDoc.RootElement;
        var catalogId = RequiredString(indexRoot, "catalog_id", "index.json");
        var schemaVersion = RequiredInt(indexRoot, "schema_version", "index.json");
        var declaredNodeCount = RequiredInt(indexRoot, "node_count", "index.json");

        var domainFiles = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var property in RequiredProperty(indexRoot, "domain_files", "index.json").EnumerateObject())
            domainFiles.Add(property.Name, property.Value.GetString() ?? throw Invalid("index.json", $"domain_files.{property.Name} must be a string"));

        var domainRows = RequiredProperty(indexRoot, "domains", "index.json").EnumerateArray().ToArray();
        if (domainRows.Length != domainFiles.Count)
            throw Invalid("index.json", $"domains count {domainRows.Length} != domain_files count {domainFiles.Count}");

        using var pressureDoc = LoadJson(root, "pressure_dynamics.json");
        ValidateCatalogId(pressureDoc.RootElement, catalogId, "pressure_dynamics.json");
        var pressureIds = RequiredProperty(pressureDoc.RootElement, "rules", "pressure_dynamics.json")
            .EnumerateObject().Select(p => p.Name).ToHashSet(StringComparer.Ordinal);

        using var traitDoc = LoadJson(root, "applicability_traits.json");
        ValidateCatalogId(traitDoc.RootElement, catalogId, "applicability_traits.json");
        var traitIds = IdSet(RequiredProperty(traitDoc.RootElement, "traits", "applicability_traits.json"), "applicability_traits.json");

        using var evidenceDoc = LoadJson(root, "evidence_types.json");
        ValidateCatalogId(evidenceDoc.RootElement, catalogId, "evidence_types.json");
        var evidenceIds = IdSet(RequiredProperty(evidenceDoc.RootElement, "evidence_types", "evidence_types.json"), "evidence_types.json");

        using var fieldDoc = LoadJson(root, "knowledge_fields.json");
        ValidateCatalogId(fieldDoc.RootElement, catalogId, "knowledge_fields.json");
        var fieldIds = IdSet(RequiredProperty(fieldDoc.RootElement, "fields", "knowledge_fields.json"), "knowledge_fields.json");

        using var capabilityDoc = LoadJson(root, "capability_model.json");
        ValidateCatalogId(capabilityDoc.RootElement, catalogId, "capability_model.json");
        var capabilities = ParseCapabilities(capabilityDoc.RootElement);
        var implications = ParseCapabilityImplications(capabilityDoc.RootElement, capabilities);

        using var economyDoc = LoadJson(root, "research_economy.json");
        ValidateCatalogId(economyDoc.RootElement, catalogId, "research_economy.json");
        var economy = ParseEconomy(economyDoc.RootElement, pressureIds, evidenceIds);

        var nodes = new Dictionary<string, AdaptiveResearchNodeDefinition>(StringComparer.Ordinal);
        foreach (var domain in domainRows)
        {
            var domainId = RequiredString(domain, "id", "index.json domain");
            if (!domainFiles.TryGetValue(domainId, out var fileName))
                throw Invalid("index.json", $"No domain file mapping for '{domainId}'.");

            using var domainDoc = LoadJson(root, fileName);
            ValidateCatalogId(domainDoc.RootElement, catalogId, fileName);
            var fileDomain = RequiredString(domainDoc.RootElement, "domain", fileName);
            if (!string.Equals(domainId, fileDomain, StringComparison.Ordinal))
                throw Invalid(fileName, $"domain '{fileDomain}' does not match index id '{domainId}'.");

            foreach (var nodeElement in RequiredProperty(domainDoc.RootElement, "nodes", fileName).EnumerateArray())
            {
                var node = ParseNode(nodeElement, fileName, economy.ComplexityDefaults, economy.NodeOverrides);
                if (!string.Equals(node.DomainId, domainId, StringComparison.Ordinal))
                    throw Invalid(fileName, $"node '{node.Id}' domain '{node.DomainId}' does not match file domain '{domainId}'.");
                if (!nodes.TryAdd(node.Id, node))
                    throw Invalid(fileName, $"Duplicate research node id '{node.Id}'.");
            }
        }

        if (nodes.Count != declaredNodeCount)
            throw Invalid("index.json", $"Declared node_count {declaredNodeCount} != loaded node count {nodes.Count}.");

        ValidateNodeReferences(nodes, pressureIds, traitIds, evidenceIds, fieldIds, capabilities);

        using var grantDoc = LoadJson(root, "technology_grants.json");
        ValidateCatalogId(grantDoc.RootElement, catalogId, "technology_grants.json");
        var grants = ParseGrants(grantDoc.RootElement, nodes, capabilities, traitIds, economy.DirectedProgramStages);

        var childrenByPrerequisite = NewIndex();
        var nodesByPressure = NewIndex();
        var nodesByEvidence = NewIndex();
        var nodesByTrait = NewIndex();
        var nodesByCapabilityRequirement = NewIndex();

        foreach (var node in nodes.Values)
        {
            foreach (var prerequisite in node.Prerequisites.AllOf.Concat(node.Prerequisites.AnyOf))
                AddIndex(childrenByPrerequisite, prerequisite, node.Id);

            foreach (var pressure in node.PressureAffinities
                         .Concat(node.ProjectRequirements.RequiredPressure.Keys)
                         .Concat(node.ProjectRequirements.RequiredPressureAny.Keys)
                         .Distinct(StringComparer.Ordinal))
                AddIndex(nodesByPressure, pressure, node.Id);

            foreach (var evidence in node.Applicability.EvidenceTypes
                         .Concat(node.ProjectRequirements.RequiredEvidence)
                         .Distinct(StringComparer.Ordinal))
                AddIndex(nodesByEvidence, evidence, node.Id);

            foreach (var trait in node.Applicability.Traits)
                AddIndex(nodesByTrait, trait, node.Id);

            foreach (var capability in node.CapabilityRequirements.AllOf.Concat(node.CapabilityRequirements.AnyOf))
                AddIndex(nodesByCapabilityRequirement, capability, node.Id);
        }

        var metadata = new AdaptiveResearchCatalogMetadata(
            schemaVersion,
            catalogId,
            declaredNodeCount,
            domainRows.Length,
            pressureIds.Count,
            traitIds.Count,
            evidenceIds.Count,
            fieldIds.Count,
            capabilities.Count,
            economy.BaseRpPerEffectiveLabPerYear,
            economy.StartingDirectedProgramStageId);

        return new AdaptiveResearchCatalog(
            metadata,
            ReadOnly(nodes),
            ReadOnly(capabilities),
            implications.AsReadOnly(),
            pressureIds,
            traitIds,
            evidenceIds,
            fieldIds,
            economy.LabScaling,
            ReadOnly(economy.DirectedProgramStages),
            ReadOnly(grants.Demonstrated),
            ReadOnly(grants.Mature),
            ReadOnly(grants.DeploymentEvents),
            FreezeIndex(childrenByPrerequisite),
            FreezeIndex(nodesByPressure),
            FreezeIndex(nodesByEvidence),
            FreezeIndex(nodesByTrait),
            FreezeIndex(nodesByCapabilityRequirement));
    }

    private static AdaptiveResearchNodeDefinition ParseNode(
        JsonElement element,
        string fileName,
        IReadOnlyDictionary<string, ComplexityDefault> complexityDefaults,
        IReadOnlyDictionary<string, NodeOverride> nodeOverrides)
    {
        var id = RequiredString(element, "id", fileName);
        var complexity = RequiredString(element, "complexity", $"{fileName}:{id}");
        if (!complexityDefaults.TryGetValue(complexity, out var defaults))
            throw Invalid(fileName, $"node '{id}' references unknown complexity '{complexity}'.");

        var prerequisitesElement = RequiredProperty(element, "prerequisites", $"{fileName}:{id}");
        var applicabilityElement = RequiredProperty(element, "applicability", $"{fileName}:{id}");

        var capabilityRequirements = new ResearchCapabilityRequirements(Array.Empty<string>(), Array.Empty<string>(), "civilization");
        if (element.TryGetProperty("capability_requirements", out var capabilityElement))
        {
            capabilityRequirements = new ResearchCapabilityRequirements(
                StringArray(capabilityElement, "all_of"),
                StringArray(capabilityElement, "any_of"),
                capabilityElement.TryGetProperty("context", out var contextElement)
                    ? contextElement.GetString() ?? "civilization"
                    : "civilization");
        }

        nodeOverrides.TryGetValue(id, out var nodeOverride);
        var minimumLabs = nodeOverride?.MinimumLabs ?? defaults.MinimumLabs;
        var recommendedLabs = nodeOverride?.RecommendedLabs ?? defaults.RecommendedLabs;
        var baseResearchPoints = Math.Round(defaults.BaseResearchPoints * (1.0 + (0.08 * RequiredInt(element, "graph_depth", $"{fileName}:{id}"))));

        return new AdaptiveResearchNodeDefinition(
            id,
            RequiredString(element, "name", $"{fileName}:{id}"),
            RequiredString(element, "domain", $"{fileName}:{id}"),
            complexity,
            RequiredInt(element, "graph_depth", $"{fileName}:{id}"),
            RequiredString(element, "solution_family", $"{fileName}:{id}"),
            StringArray(element, "knowledge_fields"),
            StringArray(element, "awareness_sources"),
            StringArray(element, "pressure_affinities"),
            new ResearchNodePrerequisites(
                StringArray(prerequisitesElement, "all_of"),
                StringArray(prerequisitesElement, "any_of")),
            new ResearchApplicabilityRequirements(
                StringArray(applicabilityElement, "requires_traits"),
                StringArray(applicabilityElement, "requires_evidence")),
            capabilityRequirements,
            StringArray(element, "capabilities"),
            RequiredBool(element, "is_hypothesis", $"{fileName}:{id}"),
            RequiredBool(element, "public_normal_research", $"{fileName}:{id}"),
            new ResearchProjectRequirements(
                baseResearchPoints,
                minimumLabs,
                recommendedLabs,
                nodeOverride?.RequiredPressure ?? EmptyNumberDictionary(),
                nodeOverride?.RequiredPressureAny ?? EmptyNumberDictionary(),
                nodeOverride?.RequiredEvidence ?? Array.Empty<string>()));
    }

    private static void ValidateNodeReferences(
        IReadOnlyDictionary<string, AdaptiveResearchNodeDefinition> nodes,
        IReadOnlySet<string> pressureIds,
        IReadOnlySet<string> traitIds,
        IReadOnlySet<string> evidenceIds,
        IReadOnlySet<string> fieldIds,
        IReadOnlyDictionary<string, ResearchCapabilityDefinition> capabilities)
    {
        foreach (var node in nodes.Values)
        {
            foreach (var prerequisite in node.Prerequisites.AllOf.Concat(node.Prerequisites.AnyOf))
                if (!nodes.ContainsKey(prerequisite))
                    throw Invalid(node.Id, $"Unknown prerequisite '{prerequisite}'.");

            foreach (var pressure in node.PressureAffinities
                         .Concat(node.ProjectRequirements.RequiredPressure.Keys)
                         .Concat(node.ProjectRequirements.RequiredPressureAny.Keys))
                if (!pressureIds.Contains(pressure))
                    throw Invalid(node.Id, $"Unknown pressure '{pressure}'.");

            foreach (var trait in node.Applicability.Traits)
                if (!traitIds.Contains(trait))
                    throw Invalid(node.Id, $"Unknown applicability trait '{trait}'.");

            foreach (var evidence in node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence))
                if (!evidenceIds.Contains(evidence))
                    throw Invalid(node.Id, $"Unknown evidence type '{evidence}'.");

            foreach (var field in node.KnowledgeFields)
                if (!fieldIds.Contains(field))
                    throw Invalid(node.Id, $"Unknown knowledge field '{field}'.");

            foreach (var capability in node.CapabilityRequirements.AllOf.Concat(node.CapabilityRequirements.AnyOf))
                if (!capabilities.ContainsKey(capability))
                    throw Invalid(node.Id, $"Unknown cross-lineage capability requirement '{capability}'.");

            // Node output markers are not required to be cross-lineage capabilities. The public
            // catalog also contains implementation-specific outputs (for example ftl_prototype).
            // Runtime grant logic promotes only outputs that resolve to this registry.
        }
    }

    private static Dictionary<string, ResearchCapabilityDefinition> ParseCapabilities(JsonElement root)
    {
        var result = new Dictionary<string, ResearchCapabilityDefinition>(StringComparer.Ordinal);
        foreach (var element in RequiredProperty(root, "cross_lineage_capabilities", "capability_model.json").EnumerateArray())
        {
            var id = RequiredString(element, "id", "capability_model.json");
            var scope = RequiredString(element, "scope", $"capability_model.json:{id}") switch
            {
                "civilization" => ResearchCapabilityScope.Civilization,
                "population_or_species" => ResearchCapabilityScope.PopulationOrSpecies,
                "colony_or_installation" => ResearchCapabilityScope.ColonyOrInstallation,
                var unknown => throw Invalid("capability_model.json", $"Unknown scope '{unknown}' for capability '{id}'."),
            };
            if (!result.TryAdd(id, new ResearchCapabilityDefinition(id, RequiredString(element, "name", $"capability_model.json:{id}"), scope)))
                throw Invalid("capability_model.json", $"Duplicate capability '{id}'.");
        }
        return result;
    }

    private static List<ResearchCapabilityImplication> ParseCapabilityImplications(
        JsonElement root,
        IReadOnlyDictionary<string, ResearchCapabilityDefinition> capabilities)
    {
        var result = new List<ResearchCapabilityImplication>();
        if (!root.TryGetProperty("implications", out var implications))
            return result;

        foreach (var element in implications.EnumerateArray())
        {
            var from = RequiredString(element, "from", "capability_model.json implication");
            var to = RequiredString(element, "to", "capability_model.json implication");
            if (!capabilities.ContainsKey(from) || !capabilities.ContainsKey(to))
                throw Invalid("capability_model.json", $"Implication '{from}' -> '{to}' references unknown capability.");
            result.Add(new ResearchCapabilityImplication(
                from,
                to,
                element.TryGetProperty("preserve_target_context", out var preserve) && preserve.GetBoolean()));
        }
        return result;
    }

    private static EconomyParseResult ParseEconomy(
        JsonElement root,
        IReadOnlySet<string> pressureIds,
        IReadOnlySet<string> evidenceIds)
    {
        var defaults = new Dictionary<string, ComplexityDefault>(StringComparer.Ordinal);
        foreach (var property in RequiredProperty(root, "complexity_defaults", "research_economy.json").EnumerateObject())
        {
            var element = property.Value;
            defaults.Add(property.Name, new ComplexityDefault(
                RequiredInt(element, "minimum_labs", $"research_economy.json:{property.Name}"),
                RequiredInt(element, "recommended_labs", $"research_economy.json:{property.Name}"),
                RequiredDouble(element, "base_research_points", $"research_economy.json:{property.Name}")));
        }

        var overrides = new Dictionary<string, NodeOverride>(StringComparer.Ordinal);
        if (root.TryGetProperty("node_requirement_overrides", out var overrideElement))
        {
            foreach (var property in overrideElement.EnumerateObject())
            {
                var element = property.Value;
                var requiredPressure = NumberDictionary(element, "required_pressure");
                var requiredPressureAny = NumberDictionary(element, "required_pressure_any");
                foreach (var pressure in requiredPressure.Keys.Concat(requiredPressureAny.Keys))
                    if (!pressureIds.Contains(pressure))
                        throw Invalid("research_economy.json", $"Override '{property.Name}' references unknown pressure '{pressure}'.");

                var requiredEvidence = StringArray(element, "required_evidence");
                foreach (var evidence in requiredEvidence)
                    if (!evidenceIds.Contains(evidence))
                        throw Invalid("research_economy.json", $"Override '{property.Name}' references unknown evidence '{evidence}'.");

                overrides.Add(property.Name, new NodeOverride(
                    OptionalInt(element, "minimum_labs"),
                    OptionalInt(element, "recommended_labs"),
                    requiredPressure,
                    requiredPressureAny,
                    requiredEvidence));
            }
        }

        var scalingElement = RequiredProperty(root, "lab_scaling", "research_economy.json");
        var labScaling = new ResearchLabScaling(
            RequiredDouble(scalingElement, "at_or_below_recommended_labs_efficiency_per_lab", "research_economy.json:lab_scaling"),
            RequiredDouble(scalingElement, "above_recommended_to_2x_recommended_efficiency_per_extra_lab", "research_economy.json:lab_scaling"),
            RequiredDouble(scalingElement, "above_2x_recommended_efficiency_per_extra_lab", "research_economy.json:lab_scaling"));

        var stages = new Dictionary<string, DirectedResearchProgramStage>(StringComparer.Ordinal);
        var concurrency = RequiredProperty(root, "directed_program_concurrency", "research_economy.json");
        foreach (var element in RequiredProperty(concurrency, "progression", "research_economy.json:directed_program_concurrency").EnumerateArray())
        {
            var id = RequiredString(element, "id", "research_economy.json directed stage");
            int? limit = null;
            if (element.TryGetProperty("directed_program_limit", out var limitElement) && limitElement.ValueKind != JsonValueKind.Null)
                limit = limitElement.GetInt32();
            var policy = element.TryGetProperty("limit_policy", out var policyElement) ? policyElement.GetString() : null;
            var requiredTechnology = element.TryGetProperty("required_technology", out var techElement) && techElement.ValueKind != JsonValueKind.Null
                ? techElement.GetString()
                : null;
            stages.Add(id, new DirectedResearchProgramStage(id, limit, string.Equals(policy, "lab_capacity_only", StringComparison.Ordinal), requiredTechnology));
        }

        var start = RequiredProperty(concurrency, "starting_stage", "research_economy.json:directed_program_concurrency");
        var startingStageId = RequiredString(start, "id", "research_economy.json starting_stage");
        if (!stages.ContainsKey(startingStageId))
            throw Invalid("research_economy.json", $"Starting directed-program stage '{startingStageId}' is not in progression.");

        var points = RequiredProperty(RequiredProperty(root, "research_model", "research_economy.json"), "research_points", "research_economy.json:research_model");
        var baseRp = RequiredDouble(points, "base_rp_per_effective_lab_per_year", "research_economy.json:research_points");

        return new EconomyParseResult(defaults, overrides, labScaling, stages, baseRp, startingStageId);
    }

    private static GrantParseResult ParseGrants(
        JsonElement root,
        IReadOnlyDictionary<string, AdaptiveResearchNodeDefinition> nodes,
        IReadOnlyDictionary<string, ResearchCapabilityDefinition> capabilities,
        IReadOnlySet<string> traitIds,
        IReadOnlyDictionary<string, DirectedResearchProgramStage> stages)
    {
        var demonstrated = ParseGrantSection(root, "on_demonstrated", nodes, capabilities, traitIds, stages);
        var mature = ParseGrantSection(root, "on_mature", nodes, capabilities, traitIds, stages);
        var deploymentEvents = new Dictionary<string, ResearchDeploymentEventDefinition>(StringComparer.Ordinal);

        if (root.TryGetProperty("deployment_events", out var eventElement))
        {
            foreach (var property in eventElement.EnumerateObject())
            {
                var requires = StringArray(property.Value, "requires_any_mature_technology");
                foreach (var nodeId in requires)
                    if (!nodes.ContainsKey(nodeId))
                        throw Invalid("technology_grants.json", $"Deployment event '{property.Name}' references unknown node '{nodeId}'.");
                var traits = StringArray(property.Value, "add_civilization_traits");
                foreach (var trait in traits)
                    if (!traitIds.Contains(trait))
                        throw Invalid("technology_grants.json", $"Deployment event '{property.Name}' references unknown trait '{trait}'.");
                deploymentEvents.Add(property.Name, new ResearchDeploymentEventDefinition(property.Name, requires, traits));
            }
        }

        return new GrantParseResult(demonstrated, mature, deploymentEvents);
    }

    private static Dictionary<string, ResearchMaturityGrant> ParseGrantSection(
        JsonElement root,
        string sectionName,
        IReadOnlyDictionary<string, AdaptiveResearchNodeDefinition> nodes,
        IReadOnlyDictionary<string, ResearchCapabilityDefinition> capabilities,
        IReadOnlySet<string> traitIds,
        IReadOnlyDictionary<string, DirectedResearchProgramStage> stages)
    {
        var result = new Dictionary<string, ResearchMaturityGrant>(StringComparer.Ordinal);
        if (!root.TryGetProperty(sectionName, out var section))
            return result;

        foreach (var property in section.EnumerateObject())
        {
            if (!nodes.ContainsKey(property.Name))
                throw Invalid("technology_grants.json", $"{sectionName} references unknown node '{property.Name}'.");
            var capabilitiesToGrant = StringArray(property.Value, "grant_capabilities");
            foreach (var capability in capabilitiesToGrant)
                if (!capabilities.ContainsKey(capability))
                    throw Invalid("technology_grants.json", $"Node '{property.Name}' grants unknown capability '{capability}'.");
            var traits = StringArray(property.Value, "add_civilization_traits");
            foreach (var trait in traits)
                if (!traitIds.Contains(trait))
                    throw Invalid("technology_grants.json", $"Node '{property.Name}' grants unknown trait '{trait}'.");
            var stage = property.Value.TryGetProperty("set_research_capacity_stage", out var stageElement)
                ? stageElement.GetString()
                : null;
            if (stage is not null && !stages.ContainsKey(stage))
                throw Invalid("technology_grants.json", $"Node '{property.Name}' grants unknown directed-program stage '{stage}'.");
            var deploymentEvents = StringArray(property.Value, "unlock_deployment_events");
            result.Add(property.Name, new ResearchMaturityGrant(capabilitiesToGrant, traits, stage, deploymentEvents));
        }
        return result;
    }

    private static JsonDocument LoadJson(string root, string relativePath)
    {
        var path = Path.Combine(root, relativePath);
        if (!File.Exists(path))
            throw new FileNotFoundException($"Required Adaptive Research file not found: {path}", path);
        return JsonDocument.Parse(File.ReadAllText(path));
    }

    private static void ValidateCatalogId(JsonElement root, string expected, string source)
    {
        var actual = RequiredString(root, "catalog_id", source);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw Invalid(source, $"catalog_id '{actual}' != '{expected}'.");
    }

    private static HashSet<string> IdSet(JsonElement array, string source)
    {
        var result = new HashSet<string>(StringComparer.Ordinal);
        foreach (var element in array.EnumerateArray())
        {
            var id = RequiredString(element, "id", source);
            if (!result.Add(id))
                throw Invalid(source, $"Duplicate id '{id}'.");
        }
        return result;
    }

    private static JsonElement RequiredProperty(JsonElement element, string name, string source) =>
        element.TryGetProperty(name, out var value)
            ? value
            : throw Invalid(source, $"Missing required property '{name}'.");

    private static string RequiredString(JsonElement element, string name, string source)
    {
        var value = RequiredProperty(element, name, source);
        if (value.ValueKind != JsonValueKind.String || string.IsNullOrWhiteSpace(value.GetString()))
            throw Invalid(source, $"Property '{name}' must be a non-empty string.");
        return value.GetString()!;
    }

    private static int RequiredInt(JsonElement element, string name, string source)
    {
        var value = RequiredProperty(element, name, source);
        if (!value.TryGetInt32(out var result))
            throw Invalid(source, $"Property '{name}' must be an integer.");
        return result;
    }

    private static double RequiredDouble(JsonElement element, string name, string source)
    {
        var value = RequiredProperty(element, name, source);
        if (!value.TryGetDouble(out var result))
            throw Invalid(source, $"Property '{name}' must be numeric.");
        return result;
    }

    private static bool RequiredBool(JsonElement element, string name, string source)
    {
        var value = RequiredProperty(element, name, source);
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw Invalid(source, $"Property '{name}' must be boolean.");
        return value.GetBoolean();
    }

    private static int? OptionalInt(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value) || value.ValueKind == JsonValueKind.Null)
            return null;
        return value.GetInt32();
    }

    private static IReadOnlyList<string> StringArray(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value) || value.ValueKind == JsonValueKind.Null)
            return Array.Empty<string>();
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException($"Property '{name}' must be an array.");
        return value.EnumerateArray().Select(item => item.GetString() ?? throw new InvalidDataException($"Property '{name}' contains non-string value.")).ToArray();
    }

    private static IReadOnlyDictionary<string, double> NumberDictionary(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value) || value.ValueKind == JsonValueKind.Null)
            return EmptyNumberDictionary();
        if (value.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException($"Property '{name}' must be an object.");
        var result = new Dictionary<string, double>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
            result.Add(property.Name, property.Value.GetDouble());
        return new ReadOnlyDictionary<string, double>(result);
    }

    private static IReadOnlyDictionary<string, double> EmptyNumberDictionary() =>
        new ReadOnlyDictionary<string, double>(new Dictionary<string, double>(StringComparer.Ordinal));

    private static Dictionary<string, HashSet<string>> NewIndex() => new(StringComparer.Ordinal);

    private static void AddIndex(Dictionary<string, HashSet<string>> index, string key, string nodeId)
    {
        if (!index.TryGetValue(key, out var set))
        {
            set = new HashSet<string>(StringComparer.Ordinal);
            index.Add(key, set);
        }
        set.Add(nodeId);
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<string>> FreezeIndex(Dictionary<string, HashSet<string>> index)
    {
        var result = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal);
        foreach (var pair in index)
            result.Add(pair.Key, pair.Value.OrderBy(id => id, StringComparer.Ordinal).ToArray());
        return new ReadOnlyDictionary<string, IReadOnlyList<string>>(result);
    }

    private static IReadOnlyDictionary<string, T> ReadOnly<T>(Dictionary<string, T> source) =>
        new ReadOnlyDictionary<string, T>(source);

    private static InvalidDataException Invalid(string source, string message) =>
        new($"Adaptive Research catalog error in {source}: {message}");

    private sealed record ComplexityDefault(int MinimumLabs, int RecommendedLabs, double BaseResearchPoints);
    private sealed record NodeOverride(
        int? MinimumLabs,
        int? RecommendedLabs,
        IReadOnlyDictionary<string, double> RequiredPressure,
        IReadOnlyDictionary<string, double> RequiredPressureAny,
        IReadOnlyList<string> RequiredEvidence);
    private sealed record EconomyParseResult(
        Dictionary<string, ComplexityDefault> ComplexityDefaults,
        Dictionary<string, NodeOverride> NodeOverrides,
        ResearchLabScaling LabScaling,
        Dictionary<string, DirectedResearchProgramStage> DirectedProgramStages,
        double BaseRpPerEffectiveLabPerYear,
        string StartingDirectedProgramStageId);
    private sealed record GrantParseResult(
        Dictionary<string, ResearchMaturityGrant> Demonstrated,
        Dictionary<string, ResearchMaturityGrant> Mature,
        Dictionary<string, ResearchDeploymentEventDefinition> DeploymentEvents);
}
