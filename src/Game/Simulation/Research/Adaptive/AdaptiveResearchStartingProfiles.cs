using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public sealed record StartingFieldCompetenceSeed(
    double Theoretical,
    double Experimental,
    double Engineering);

public sealed record StartingResearchInstitutionSeed(
    string InstitutionArchetypeId,
    int Count);

public sealed record StartingTacitAssetSeed(
    string AssetTypeId,
    string Provenance,
    string? ScopeRef);

public sealed record AdaptiveResearchDeferredStartingState(
    IReadOnlyDictionary<string, StartingFieldCompetenceSeed> FieldCompetence,
    IReadOnlyList<StartingResearchInstitutionSeed> ResearchInstitutions,
    IReadOnlyList<StartingTacitAssetSeed> TacitAssets,
    IReadOnlyList<string> SelectedFragmentIds,
    string ReferenceProfileId,
    string? HistoricalNotes);

public sealed record AdaptiveResearchStartingCompositionResult(
    AdaptiveResearchCivilizationState State,
    AdaptiveResearchDeferredStartingState Deferred,
    IReadOnlyList<AdaptiveResearchRuntimeEvent> InitialHorizonEvents);

/// <summary>
/// Initialization-only composition of historical research fragments. It seeds past/current state,
/// never a future tree. A one-time public-catalog review is allowed here because this runs only at
/// new-game/migration boundaries; normal campaign emergence remains indexed/event-driven.
/// </summary>
public sealed class AdaptiveResearchStartingProfileComposer
{
    private readonly AdaptiveResearchRuntime _runtime;
    private readonly string _rootPath;
    private readonly IReadOnlyDictionary<string, StartingFragmentDefinition> _fragments;
    private readonly IReadOnlyDictionary<string, StartingReferenceProfileDefinition> _profiles;
    private readonly int _competenceCompositionCap;

    public AdaptiveResearchStartingProfileComposer(AdaptiveResearchRuntime runtime, string rootPath)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _rootPath = Path.GetFullPath(rootPath);
        (_fragments, _profiles, _competenceCompositionCap) = LoadDefinitions();
    }

    public IReadOnlyCollection<string> ReferenceProfileIds => _profiles.Keys.ToArray();
    public IReadOnlyCollection<string> FragmentIds => _fragments.Keys.ToArray();

    public AdaptiveResearchStartingCompositionResult ComposeReferenceProfile(
        string civilizationId,
        string referenceProfileId,
        string primaryApplicabilityContextId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(civilizationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(referenceProfileId);
        ArgumentException.ThrowIfNullOrWhiteSpace(primaryApplicabilityContextId);
        if (!_profiles.TryGetValue(referenceProfileId, out var profile))
            throw new KeyNotFoundException($"Unknown starting research reference profile '{referenceProfileId}'.");

        var selected = profile.FragmentIds.Select(id =>
            _fragments.TryGetValue(id, out var fragment)
                ? fragment
                : throw new InvalidDataException($"Starting profile '{profile.Id}' references unknown fragment '{id}'."))
            .ToArray();
        if (selected.Count(fragment => string.Equals(fragment.Kind, "base_era", StringComparison.Ordinal)) != 1)
            throw new InvalidDataException($"Starting profile '{profile.Id}' must compose exactly one base-era fragment.");

        var state = _runtime.CreateCivilizationState(civilizationId);
        var nodeSeeds = ComposeNodeStates(selected);
        var pressureSeeds = ComposePressures(selected, profile.AdditionalStartingPressureState);
        var traitSeeds = selected.SelectMany(fragment => fragment.StartingApplicabilityTraits)
            .Concat(profile.AdditionalStartingApplicabilityTraits)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        var capabilitySeeds = selected.SelectMany(fragment => fragment.StartingCapabilities)
            .Concat(profile.AdditionalStartingCapabilities)
            .GroupBy(seed => new ResearchCapabilityKey(seed.CapabilityId, seed.ContextId), seed => seed)
            .Select(group => group.First())
            .ToArray();
        var institutions = ComposeInstitutions(selected);
        var competence = ComposeCompetence(selected);
        var tacitAssets = selected.SelectMany(fragment => fragment.StartingTacitAssets).ToArray();
        var evidenceSeeds = selected.SelectMany(fragment => fragment.StartingEvidence)
            .Concat(profile.AdditionalStartingEvidence)
            .GroupBy(seed => seed.EvidenceInstanceId, StringComparer.Ordinal)
            .Select(group => group.First())
            .ToArray();

        state.SetDirectedProgramStage(ChooseDirectedProgramStage(selected));

        var populationTraits = new List<string>();
        foreach (var traitId in traitSeeds)
        {
            var trait = _runtime.Applicability.GetTrait(traitId);
            if (trait.Scope == ResearchApplicabilityTraitScope.Civilization)
                state.AddCivilizationTrait(traitId);
            else
                populationTraits.Add(traitId);
        }
        state.SetApplicabilityContextTraits(primaryApplicabilityContextId, populationTraits);

        foreach (var pair in pressureSeeds)
        {
            if (!_runtime.Catalog.PressureIds.Contains(pair.Key))
                throw new InvalidDataException($"Starting profile '{profile.Id}' references unknown Research Pressure '{pair.Key}'.");
            state.SetPressure(pair.Key, pair.Value);
        }

        foreach (var evidence in evidenceSeeds)
        {
            if (!_runtime.Catalog.EvidenceTypeIds.Contains(evidence.EvidenceTypeId))
                throw new InvalidDataException($"Starting profile '{profile.Id}' references unknown evidence type '{evidence.EvidenceTypeId}'.");
            state.AddEvidence(new ResearchEvidenceInstance(
                evidence.EvidenceInstanceId,
                evidence.EvidenceTypeId,
                evidence.Provenance,
                evidence.Quality,
                evidence.Confidence,
                evidence.ContextId ?? primaryApplicabilityContextId,
                state.Revision + 1));
        }

        foreach (var nodeSeed in nodeSeeds.Values.OrderBy(seed => _runtime.Catalog.GetNode(seed.NodeId).GraphDepth))
        {
            var node = _runtime.Catalog.GetNode(nodeSeed.NodeId);
            var totalRp = InitialTotalResearchPoints(node, nodeSeed.Maturity, nodeSeed.Resolution);
            state.SetNodeState(new ResearchNodeRuntimeState(
                nodeSeed.NodeId,
                nodeSeed.Maturity,
                nodeSeed.Resolution,
                0.0,
                totalRp,
                state.Revision + 1));
        }

        ValidateStartingPrerequisiteClosure(state, profile.Id);
        ValidateStartingApplicability(state, nodeSeeds.Values, primaryApplicabilityContextId, profile.Id);

        foreach (var capability in capabilitySeeds)
        {
            var definition = _runtime.Catalog.Capabilities.TryGetValue(capability.CapabilityId, out var loaded)
                ? loaded
                : throw new InvalidDataException($"Starting profile '{profile.Id}' references unknown capability '{capability.CapabilityId}'.");
            var context = definition.Scope == ResearchCapabilityScope.Civilization
                ? null
                : capability.ContextId ?? primaryApplicabilityContextId;
            _runtime.AddCapability(state, capability.CapabilityId, context);
        }

        foreach (var nodeSeed in nodeSeeds.Values.Where(seed => seed.Maturity == ResearchMaturity.Mature))
            ApplyHistoricalMatureEffects(state, nodeSeed.NodeId, primaryApplicabilityContextId);

        double totalLabs = 0.0;
        foreach (var institutionSeed in institutions)
        {
            if (!_runtime.Facilities.Institutions.TryGetValue(institutionSeed.InstitutionArchetypeId, out var institution))
                throw new InvalidDataException($"Starting profile '{profile.Id}' references unknown research institution '{institutionSeed.InstitutionArchetypeId}'.");
            totalLabs += institution.EffectiveLabUnits * institutionSeed.Count;
            foreach (var facilityCapability in institution.FacilityCapabilities)
                state.AddFacilityCapability(facilityCapability);
        }
        state.SetTotalEffectiveResearchLabs(totalLabs);

        // Initialization-only one-time review. Only basic-science-aware possibilities participate;
        // contact/problem/anomaly-driven branches remain hidden until their real wake-up source occurs.
        var basicScienceCandidates = _runtime.Catalog.Nodes.Values
            .Where(node => node.PublicNormalResearch && node.AwarenessSources.Contains("basic_science", StringComparer.Ordinal))
            .Select(node => node.Id)
            .ToArray();
        var initialEvents = _runtime.ReviewBasicScienceCandidates(
            state,
            basicScienceCandidates,
            primaryApplicabilityContextId);

        var deferred = new AdaptiveResearchDeferredStartingState(
            new ReadOnlyDictionary<string, StartingFieldCompetenceSeed>(competence),
            institutions,
            tacitAssets,
            selected.Select(fragment => fragment.Id).ToArray(),
            profile.Id,
            profile.HistoricalNotes);
        return new AdaptiveResearchStartingCompositionResult(state, deferred, initialEvents);
    }

    private void ApplyHistoricalMatureEffects(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string primaryApplicabilityContextId)
    {
        var node = _runtime.Catalog.GetNode(nodeId);
        foreach (var capabilityId in node.DeclaredCapabilities.Where(_runtime.Catalog.Capabilities.ContainsKey))
        {
            var definition = _runtime.Catalog.Capabilities[capabilityId];
            _runtime.AddCapability(
                state,
                capabilityId,
                definition.Scope == ResearchCapabilityScope.Civilization ? null : primaryApplicabilityContextId);
        }

        if (!_runtime.Catalog.MatureGrants.TryGetValue(nodeId, out var grant))
            return;
        foreach (var capabilityId in grant.CapabilityIds)
        {
            var definition = _runtime.Catalog.Capabilities[capabilityId];
            _runtime.AddCapability(
                state,
                capabilityId,
                definition.Scope == ResearchCapabilityScope.Civilization ? null : primaryApplicabilityContextId);
        }
        foreach (var traitId in grant.CivilizationTraitIds)
        {
            var trait = _runtime.Applicability.GetTrait(traitId);
            if (trait.Scope == ResearchApplicabilityTraitScope.Civilization)
                state.AddCivilizationTrait(traitId);
            else
                state.AddApplicabilityTrait(primaryApplicabilityContextId, traitId);
        }
        if (grant.ResearchCapacityStageId is not null)
            state.SetDirectedProgramStage(grant.ResearchCapacityStageId);
        foreach (var deploymentEventId in grant.EnabledDeploymentEventIds)
            state.AddEnabledDeploymentEvent(deploymentEventId);
    }

    private void ValidateStartingPrerequisiteClosure(AdaptiveResearchCivilizationState state, string profileId)
    {
        foreach (var nodeState in state.NodeStates.Values)
        {
            if (nodeState.Maturity is ResearchMaturity.Rumored or ResearchMaturity.Hypothesized or ResearchMaturity.Archived)
                continue;
            var node = _runtime.Catalog.GetNode(nodeState.NodeId);
            foreach (var prerequisiteId in node.Prerequisites.AllOf)
            {
                if (!state.HasEstablishedKnowledge(prerequisiteId))
                    throw new InvalidDataException($"Starting profile '{profileId}' seeds '{node.Id}' at {nodeState.Maturity} without mature prerequisite '{prerequisiteId}'.");
            }
            if (node.Prerequisites.AnyOf.Count > 0 && !node.Prerequisites.AnyOf.Any(state.HasEstablishedKnowledge))
                throw new InvalidDataException($"Starting profile '{profileId}' seeds '{node.Id}' without any mature alternative prerequisite.");
        }
    }

    private void ValidateStartingApplicability(
        AdaptiveResearchCivilizationState state,
        IEnumerable<StartingNodeStateSeed> seeds,
        string primaryContextId,
        string profileId)
    {
        foreach (var seed in seeds.Where(seed => seed.Maturity >= ResearchMaturity.Investigable && seed.Maturity != ResearchMaturity.Archived))
        {
            var node = _runtime.Catalog.GetNode(seed.NodeId);
            foreach (var traitId in node.Applicability.Traits)
            {
                var trait = _runtime.Applicability.GetTrait(traitId);
                var present = trait.Scope == ResearchApplicabilityTraitScope.Civilization
                    ? state.HasCivilizationTrait(traitId)
                    : state.HasApplicabilityTrait(primaryContextId, traitId);
                if (!present)
                    throw new InvalidDataException($"Starting profile '{profileId}' seeds '{node.Id}' without required applicability trait '{traitId}'.");
            }
        }
    }

    private string ChooseDirectedProgramStage(IEnumerable<StartingFragmentDefinition> fragments)
    {
        var candidates = fragments.Select(fragment => fragment.StartingDirectedProgramStageId)
            .Where(value => value is not null)
            .Cast<string>()
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        if (candidates.Length == 0)
            return _runtime.Catalog.Metadata.StartingDirectedProgramStageId;
        return candidates
            .Select(id => _runtime.Catalog.GetDirectedProgramStage(id))
            .OrderBy(stage => stage.LabCapacityOnly ? int.MaxValue : stage.DirectedProgramLimit ?? 0)
            .Last().Id;
    }

    private Dictionary<string, StartingNodeStateSeed> ComposeNodeStates(IEnumerable<StartingFragmentDefinition> fragments)
    {
        var result = new Dictionary<string, StartingNodeStateSeed>(StringComparer.Ordinal);
        foreach (var fragment in fragments)
        {
            foreach (var pair in fragment.StartingNodeStates)
            {
                _runtime.Catalog.GetNode(pair.Key);
                if (!result.TryGetValue(pair.Key, out var existing))
                {
                    result.Add(pair.Key, pair.Value);
                    continue;
                }
                if (existing.Maturity == ResearchMaturity.Archived && pair.Value.Maturity != ResearchMaturity.Archived ||
                    pair.Value.Maturity == ResearchMaturity.Archived && existing.Maturity != ResearchMaturity.Archived)
                    throw new InvalidDataException($"Starting fragments contain conflicting archived/non-archived history for '{pair.Key}'.");
                if (MaturityRank(pair.Value.Maturity) > MaturityRank(existing.Maturity))
                    result[pair.Key] = pair.Value;
            }
        }
        return result;
    }

    private static Dictionary<string, double> ComposePressures(
        IEnumerable<StartingFragmentDefinition> fragments,
        IReadOnlyDictionary<string, double> profilePressures)
    {
        var result = new Dictionary<string, double>(StringComparer.Ordinal);
        foreach (var pair in fragments.SelectMany(fragment => fragment.StartingPressureState))
            result[pair.Key] = result.TryGetValue(pair.Key, out var existing) ? Math.Max(existing, pair.Value) : pair.Value;
        foreach (var pair in profilePressures)
            result[pair.Key] = result.TryGetValue(pair.Key, out var existing) ? Math.Max(existing, pair.Value) : pair.Value;
        return result;
    }

    private IReadOnlyList<StartingResearchInstitutionSeed> ComposeInstitutions(IEnumerable<StartingFragmentDefinition> fragments) =>
        fragments.SelectMany(fragment => fragment.StartingResearchInstitutions)
            .GroupBy(seed => seed.InstitutionArchetypeId, StringComparer.Ordinal)
            .Select(group => new StartingResearchInstitutionSeed(group.Key, group.Sum(seed => seed.Count)))
            .OrderBy(seed => seed.InstitutionArchetypeId, StringComparer.Ordinal)
            .ToArray();

    private Dictionary<string, StartingFieldCompetenceSeed> ComposeCompetence(IEnumerable<StartingFragmentDefinition> fragments)
    {
        var result = new Dictionary<string, StartingFieldCompetenceSeed>(StringComparer.Ordinal);
        foreach (var pair in fragments.SelectMany(fragment => fragment.StartingFieldCompetence))
        {
            if (!_runtime.Catalog.KnowledgeFieldIds.Contains(pair.Key))
                throw new InvalidDataException($"Starting fragment references unknown knowledge field '{pair.Key}'.");
            var value = new StartingFieldCompetenceSeed(
                Math.Min(_competenceCompositionCap, pair.Value.Theoretical),
                Math.Min(_competenceCompositionCap, pair.Value.Experimental),
                Math.Min(_competenceCompositionCap, pair.Value.Engineering));
            if (result.TryGetValue(pair.Key, out var existing))
            {
                value = new StartingFieldCompetenceSeed(
                    Math.Max(existing.Theoretical, value.Theoretical),
                    Math.Max(existing.Experimental, value.Experimental),
                    Math.Max(existing.Engineering, value.Engineering));
            }
            result[pair.Key] = value;
        }
        return result;
    }

    private double InitialTotalResearchPoints(
        AdaptiveResearchNodeDefinition node,
        ResearchMaturity maturity,
        string? resolution) => maturity switch
    {
        ResearchMaturity.Experimental => 0.0,
        ResearchMaturity.Demonstrated => _runtime.ProgressPolicy.GetStageBand(ResearchMaturity.Experimental).EndFraction * node.ProjectRequirements.BaseResearchPoints,
        ResearchMaturity.Engineering => _runtime.ProgressPolicy.GetStageBand(ResearchMaturity.Demonstrated).EndFraction * node.ProjectRequirements.BaseResearchPoints,
        ResearchMaturity.Mature => node.ProjectRequirements.BaseResearchPoints,
        ResearchMaturity.Archived when string.Equals(resolution, "mature_history", StringComparison.OrdinalIgnoreCase) || string.Equals(resolution, "superseded", StringComparison.OrdinalIgnoreCase) => node.ProjectRequirements.BaseResearchPoints,
        _ => 0.0,
    };

    private (IReadOnlyDictionary<string, StartingFragmentDefinition>, IReadOnlyDictionary<string, StartingReferenceProfileDefinition>, int) LoadDefinitions()
    {
        using var indexDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(_rootPath, "starting_profile_index.json")));
        var index = indexDoc.RootElement;
        ValidateCatalogId(index, "starting_profile_index.json");
        var files = index.GetProperty("files");
        var fragmentFiles = files.GetProperty("history_fragment_files").EnumerateArray().Select(value => value.GetString()!).ToArray();
        var profileFiles = files.GetProperty("reference_profile_files").EnumerateArray().Select(value => value.GetString()!).ToArray();

        var fragments = new Dictionary<string, StartingFragmentDefinition>(StringComparer.Ordinal);
        var competenceCap = 100;
        foreach (var fileName in fragmentFiles)
        {
            using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(_rootPath, fileName)));
            ValidateCatalogId(document.RootElement, fileName);
            if (document.RootElement.TryGetProperty("composition_cap_per_competence_component", out var capElement))
                competenceCap = Math.Min(competenceCap, capElement.GetInt32());
            foreach (var element in document.RootElement.GetProperty("fragments").EnumerateArray())
            {
                var fragment = ParseFragment(element, fileName);
                if (!fragments.TryAdd(fragment.Id, fragment))
                    throw new InvalidDataException($"Duplicate starting research fragment '{fragment.Id}'.");
            }
        }

        var profiles = new Dictionary<string, StartingReferenceProfileDefinition>(StringComparer.Ordinal);
        foreach (var fileName in profileFiles)
        {
            using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(_rootPath, fileName)));
            ValidateCatalogId(document.RootElement, fileName);
            foreach (var element in document.RootElement.GetProperty("profiles").EnumerateArray())
            {
                var profile = ParseProfile(element, fileName);
                if (!profiles.TryAdd(profile.Id, profile))
                    throw new InvalidDataException($"Duplicate starting research reference profile '{profile.Id}'.");
            }
        }

        var counts = index.GetProperty("counts");
        if (fragments.Count != counts.GetProperty("history_fragments").GetInt32())
            throw new InvalidDataException("Starting research fragment count does not match starting_profile_index.json.");
        if (profiles.Count != counts.GetProperty("reference_profiles").GetInt32())
            throw new InvalidDataException("Starting research reference-profile count does not match starting_profile_index.json.");

        return (
            new ReadOnlyDictionary<string, StartingFragmentDefinition>(fragments),
            new ReadOnlyDictionary<string, StartingReferenceProfileDefinition>(profiles),
            competenceCap);
    }

    private StartingFragmentDefinition ParseFragment(JsonElement element, string fileName)
    {
        var id = RequiredString(element, "id", fileName);
        return new StartingFragmentDefinition(
            id,
            RequiredString(element, "kind", fileName),
            ParseNodeStateMap(element, "starting_node_states", fileName, id),
            ParseCompetenceMap(element, fileName, id),
            StringArray(element, "starting_applicability_traits"),
            ParseCapabilitySeeds(element, "starting_capabilities"),
            ParseInstitutions(element, fileName, id),
            NumberDictionary(element, "starting_pressure_state"),
            ParseEvidence(element, "starting_evidence", id),
            ParseTacitAssets(element, "starting_tacit_assets"),
            OptionalString(element, "starting_directed_program_stage"),
            OptionalString(element, "historical_notes"));
    }

    private StartingReferenceProfileDefinition ParseProfile(JsonElement element, string fileName)
    {
        var id = RequiredString(element, "id", fileName);
        return new StartingReferenceProfileDefinition(
            id,
            StringArray(element, "fragment_ids"),
            StringArray(element, "additional_starting_applicability_traits"),
            NumberDictionary(element, "additional_starting_pressure_state"),
            ParseCapabilitySeeds(element, "additional_starting_capabilities"),
            ParseEvidence(element, "additional_starting_evidence", id),
            OptionalString(element, "historical_notes"));
    }

    private static IReadOnlyDictionary<string, StartingNodeStateSeed> ParseNodeStateMap(JsonElement element, string propertyName, string fileName, string fragmentId)
    {
        var result = new Dictionary<string, StartingNodeStateSeed>(StringComparer.Ordinal);
        if (!element.TryGetProperty(propertyName, out var property))
            return result;
        foreach (var nodeProperty in property.EnumerateObject())
        {
            if (nodeProperty.Value.ValueKind == JsonValueKind.String)
            {
                result.Add(nodeProperty.Name, new StartingNodeStateSeed(nodeProperty.Name, ParseMaturity(nodeProperty.Value.GetString()!, fileName, fragmentId), null));
            }
            else if (nodeProperty.Value.ValueKind == JsonValueKind.Object)
            {
                result.Add(nodeProperty.Name, new StartingNodeStateSeed(
                    nodeProperty.Name,
                    ParseMaturity(RequiredString(nodeProperty.Value, "state", fileName), fileName, fragmentId),
                    OptionalString(nodeProperty.Value, "archive_resolution")));
            }
            else
            {
                throw new InvalidDataException($"{fileName}:{fragmentId} has invalid starting state for '{nodeProperty.Name}'.");
            }
        }
        return result;
    }

    private static IReadOnlyDictionary<string, StartingFieldCompetenceSeed> ParseCompetenceMap(JsonElement element, string fileName, string fragmentId)
    {
        var result = new Dictionary<string, StartingFieldCompetenceSeed>(StringComparer.Ordinal);
        if (!element.TryGetProperty("starting_field_competence", out var property))
            return result;
        foreach (var field in property.EnumerateObject())
        {
            var value = field.Value;
            result.Add(field.Name, new StartingFieldCompetenceSeed(
                value.GetProperty("theoretical").GetDouble(),
                value.GetProperty("experimental").GetDouble(),
                value.GetProperty("engineering").GetDouble()));
            var seed = result[field.Name];
            if (seed.Theoretical is < 0 or > 100 || seed.Experimental is < 0 or > 100 || seed.Engineering is < 0 or > 100)
                throw new InvalidDataException($"{fileName}:{fragmentId} competence '{field.Name}' is outside 0..100.");
        }
        return result;
    }

    private static IReadOnlyList<StartingResearchInstitutionSeed> ParseInstitutions(JsonElement element, string fileName, string fragmentId)
    {
        if (!element.TryGetProperty("starting_research_institutions", out var property))
            return Array.Empty<StartingResearchInstitutionSeed>();
        return property.EnumerateArray().Select(value =>
        {
            var id = RequiredString(value, "institution_archetype_id", fileName);
            var count = value.GetProperty("count").GetInt32();
            if (count <= 0)
                throw new InvalidDataException($"{fileName}:{fragmentId} institution '{id}' has non-positive count.");
            return new StartingResearchInstitutionSeed(id, count);
        }).ToArray();
    }

    private static IReadOnlyList<StartingCapabilitySeed> ParseCapabilitySeeds(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property))
            return Array.Empty<StartingCapabilitySeed>();
        return property.EnumerateArray().Select(value => value.ValueKind switch
        {
            JsonValueKind.String => new StartingCapabilitySeed(value.GetString()!, null),
            JsonValueKind.Object => new StartingCapabilitySeed(
                RequiredString(value, "capability_id", propertyName),
                OptionalString(value, "context_id")),
            _ => throw new InvalidDataException($"{propertyName} contains an invalid capability seed."),
        }).ToArray();
    }

    private static IReadOnlyList<StartingEvidenceSeed> ParseEvidence(JsonElement element, string propertyName, string sourceId)
    {
        if (!element.TryGetProperty(propertyName, out var property))
            return Array.Empty<StartingEvidenceSeed>();
        var index = 0;
        return property.EnumerateArray().Select(value =>
        {
            var evidenceType = RequiredString(value, "evidence_type_id", propertyName);
            var instanceId = OptionalString(value, "evidence_instance_id") ?? $"start:{sourceId}:{evidenceType}:{index++}";
            return new StartingEvidenceSeed(
                instanceId,
                evidenceType,
                OptionalString(value, "provenance") ?? $"starting_history:{sourceId}",
                OptionalDouble(value, "quality") ?? 1.0,
                OptionalDouble(value, "confidence") ?? 1.0,
                OptionalString(value, "context_id"));
        }).ToArray();
    }

    private static IReadOnlyList<StartingTacitAssetSeed> ParseTacitAssets(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property))
            return Array.Empty<StartingTacitAssetSeed>();
        return property.EnumerateArray().Select(value => value.ValueKind switch
        {
            JsonValueKind.String => new StartingTacitAssetSeed(value.GetString()!, "starting_history", null),
            JsonValueKind.Object => new StartingTacitAssetSeed(
                RequiredString(value, "asset_type_id", propertyName),
                OptionalString(value, "provenance") ?? "starting_history",
                OptionalString(value, "scope_ref")),
            _ => throw new InvalidDataException($"{propertyName} contains an invalid tacit-asset seed."),
        }).ToArray();
    }

    private static ResearchMaturity ParseMaturity(string value, string fileName, string fragmentId) => value switch
    {
        "rumored" => ResearchMaturity.Rumored,
        "hypothesized" => ResearchMaturity.Hypothesized,
        "investigable" => ResearchMaturity.Investigable,
        "experimental" => ResearchMaturity.Experimental,
        "demonstrated" => ResearchMaturity.Demonstrated,
        "engineering" => ResearchMaturity.Engineering,
        "mature" => ResearchMaturity.Mature,
        "archived" => ResearchMaturity.Archived,
        _ => throw new InvalidDataException($"{fileName}:{fragmentId} uses unknown research maturity '{value}'."),
    };

    private static int MaturityRank(ResearchMaturity maturity) => maturity switch
    {
        ResearchMaturity.Rumored => 1,
        ResearchMaturity.Hypothesized => 2,
        ResearchMaturity.Investigable => 3,
        ResearchMaturity.Experimental => 4,
        ResearchMaturity.Demonstrated => 5,
        ResearchMaturity.Engineering => 6,
        ResearchMaturity.Mature => 7,
        ResearchMaturity.Archived => 8,
        _ => 0,
    };

    private void ValidateCatalogId(JsonElement root, string fileName)
    {
        var actual = RequiredString(root, "catalog_id", fileName);
        if (!string.Equals(actual, _runtime.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"{fileName} catalog_id '{actual}' does not match '{_runtime.Catalog.Metadata.CatalogId}'.");
    }

    private static string RequiredString(JsonElement element, string propertyName, string context) =>
        element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? throw new InvalidDataException($"{context}.{propertyName} cannot be null.")
            : throw new InvalidDataException($"{context} is missing string '{propertyName}'.");

    private static string? OptionalString(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString()
            : null;

    private static double? OptionalDouble(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.Number
            ? property.GetDouble()
            : null;

    private static IReadOnlyList<string> StringArray(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var property)
            ? property.EnumerateArray().Select(value => value.GetString() ?? throw new InvalidDataException($"{propertyName} contains null.")).ToArray()
            : Array.Empty<string>();

    private static IReadOnlyDictionary<string, double> NumberDictionary(JsonElement element, string propertyName)
    {
        var result = new Dictionary<string, double>(StringComparer.Ordinal);
        if (!element.TryGetProperty(propertyName, out var property))
            return result;
        foreach (var pair in property.EnumerateObject())
            result.Add(pair.Name, pair.Value.GetDouble());
        return result;
    }

    private sealed record StartingNodeStateSeed(string NodeId, ResearchMaturity Maturity, string? Resolution);
    private sealed record StartingCapabilitySeed(string CapabilityId, string? ContextId);
    private sealed record StartingEvidenceSeed(string EvidenceInstanceId, string EvidenceTypeId, string Provenance, double Quality, double Confidence, string? ContextId);

    private sealed record StartingFragmentDefinition(
        string Id,
        string Kind,
        IReadOnlyDictionary<string, StartingNodeStateSeed> StartingNodeStates,
        IReadOnlyDictionary<string, StartingFieldCompetenceSeed> StartingFieldCompetence,
        IReadOnlyList<string> StartingApplicabilityTraits,
        IReadOnlyList<StartingCapabilitySeed> StartingCapabilities,
        IReadOnlyList<StartingResearchInstitutionSeed> StartingResearchInstitutions,
        IReadOnlyDictionary<string, double> StartingPressureState,
        IReadOnlyList<StartingEvidenceSeed> StartingEvidence,
        IReadOnlyList<StartingTacitAssetSeed> StartingTacitAssets,
        string? StartingDirectedProgramStageId,
        string? HistoricalNotes);

    private sealed record StartingReferenceProfileDefinition(
        string Id,
        IReadOnlyList<string> FragmentIds,
        IReadOnlyList<string> AdditionalStartingApplicabilityTraits,
        IReadOnlyDictionary<string, double> AdditionalStartingPressureState,
        IReadOnlyList<StartingCapabilitySeed> AdditionalStartingCapabilities,
        IReadOnlyList<StartingEvidenceSeed> AdditionalStartingEvidence,
        string? HistoricalNotes);
}
