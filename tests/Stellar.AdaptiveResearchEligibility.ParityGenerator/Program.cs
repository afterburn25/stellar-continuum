using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchEligibilityOracle <research-data> <fixture>");
    return 1;
}

var options = new JsonSerializerOptions
{
    WriteIndented = false,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);

var rootPath = Path.GetFullPath(args[0]);
var catalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(rootPath);
var applicability = AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(rootPath, catalog);
var facilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(rootPath, catalog);
var evaluator = new AdaptiveResearchEligibilityEvaluator(catalog, applicability, facilities);
var methodCache = typeof(AdaptiveResearchCivilizationState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .GroupBy(method => (method.Name, method.GetParameters().Length))
    .ToDictionary(group => group.Key, group => group.Single());

object Snapshot(AdaptiveResearchCivilizationState state) => new
{
    state.CivilizationId,
    state.Revision,
    state.MaterializedViewRevision,
    state.DirectedProgramStageId,
    state.TotalEffectiveResearchLabs,
    state.AssignedEffectiveLabs,
    state.FreeEffectiveLabs,
    NodeStates = state.NodeStates.Values.ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    EvidenceInstances = state.EvidenceInstances.Values.ToArray(),
    CivilizationTraits = state.CivilizationTraits.ToArray(),
    Capabilities = state.Capabilities.ToArray(),
    FacilityCapabilities = state.FacilityCapabilities.ToArray(),
    EnabledDeploymentEventIds = state.EnabledDeploymentEventIds.ToArray(),
    ApplicabilityContexts = state.ApplicabilityContexts
        .Select(pair => new { ContextId = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    ActiveProjects = state.ActiveProjects.Values.ToArray(),
};


StateBuilder Satisfied(AdaptiveResearchNodeDefinition node, string? context = "context:target", bool includeNode = true, ResearchMaturity maturity = ResearchMaturity.Investigable,
                       AdaptiveResearchCatalog? selectedCatalog = null, AdaptiveResearchApplicabilityCatalog? selectedApplicability = null,
                       AdaptiveResearchFacilityCatalog? selectedFacilities = null)
{
    selectedCatalog ??= catalog;
    selectedApplicability ??= applicability;
    selectedFacilities ??= facilities;
    var builder = new StateBuilder(selectedCatalog.Metadata.StartingDirectedProgramStageId, methodCache);
    builder.Labs(1_000_000.0);
    foreach (var id in node.Prerequisites.AllOf)
        builder.Node(id);
    if (node.Prerequisites.AnyOf.Count > 0)
        builder.Node(node.Prerequisites.AnyOf[0], ResearchMaturity.Archived, "Mature_History");
    foreach (var id in node.Applicability.Traits)
    {
        var trait = selectedApplicability.GetTrait(id);
        if (trait.Scope == ResearchApplicabilityTraitScope.Civilization)
            builder.Trait(id);
        else if (context is not null)
            builder.ContextTrait(context, id);
    }
    var evidenceIndex = 0;
    foreach (var id in node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence).Distinct(StringComparer.Ordinal))
        builder.Evidence(new ResearchEvidenceInstance($"evidence:{evidenceIndex++}", id, "fixture", 1.0, 1.0, context, 0));
    foreach (var pair in node.ProjectRequirements.RequiredPressure)
        builder.Pressure(pair.Key, pair.Value);
    if (node.ProjectRequirements.RequiredPressureAny.Count > 0)
    {
        var pair = node.ProjectRequirements.RequiredPressureAny.First();
        builder.Pressure(pair.Key, pair.Value);
    }
    foreach (var id in node.CapabilityRequirements.AllOf)
    {
        var definition = selectedCatalog.Capabilities[id];
        builder.Capability(id, definition.Scope == ResearchCapabilityScope.Civilization ? null : context);
    }
    if (node.CapabilityRequirements.AnyOf.Count > 0)
    {
        var id = node.CapabilityRequirements.AnyOf[0];
        var definition = selectedCatalog.Capabilities[id];
        builder.Capability(id, definition.Scope == ResearchCapabilityScope.Civilization ? null : context);
    }
    var facility = selectedFacilities.GetStageRequirement(node.Id, ResearchMaturity.Experimental);
    if (facility is not null)
    {
        foreach (var id in facility.AllOf)
            builder.Facility(id);
        if (facility.AnyOf.Count > 0)
            builder.Facility(facility.AnyOf[0]);
    }
    if (includeNode)
        builder.Node(node.Id, maturity);
    return builder;
}

var cases = new List<object>();
void Case(string name, string method, StateBuilder builder, string nodeId, double? labs = null,
          string? context = null, bool contextPresent = false, ResearchMaturity? stage = null,
          string catalogVariant = "canonical", AdaptiveResearchEligibilityEvaluator? selectedEvaluator = null)
{
    selectedEvaluator ??= evaluator;
    var input = Freeze(new
    {
        Method = method,
        NodeId = nodeId,
        RequestedAssignedLabs = labs,
        ContextPresent = contextPresent,
        Context = context,
        Stage = stage,
        CatalogVariant = catalogVariant,
        Setup = builder.Setup,
        State = Snapshot(builder.State),
    });
    var before = Freeze(Snapshot(builder.State));
    ResearchEligibilityResult? result = null;
    Exception? error = null;
    Func<ResearchEligibilityResult> invoke = method switch
    {
        "Scientific" => () => selectedEvaluator.EvaluateScientificEligibility(builder.State, nodeId, contextPresent ? context : null),
        "ProjectStart" when labs.HasValue => () => selectedEvaluator.EvaluateProjectStart(builder.State, nodeId, labs.Value, contextPresent ? context : null),
        "StageFacility" when stage.HasValue => () => selectedEvaluator.EvaluateStageFacilityEligibility(builder.State, nodeId, stage.Value),
        _ => throw new InvalidOperationException("Invalid fixture method or arguments."),
    };
    try
    {
        result = invoke();
    }
    catch (Exception caught)
    {
        error = caught;
    }
    var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    cases.Add(new { Name = name, Input = input, Result = frozenResult, Error = frozenError, Before = before, After = Freeze(Snapshot(builder.State)) });
}

ScratchLease CreateSyntheticRoot(string nonPublicNodeId, string anyCapabilityNodeId,
                           IReadOnlyList<string> anyCapabilityIds,
                           out string anyFacilityNodeId,
                           out ResearchMaturity anyFacilityStage,
                           out string[] anyFacilityIds)
{
    var lease = new ScratchLease(rootPath);
    var scratch = lease.Path;

    var foundNonPublic = false;
    var foundAnyCapability = false;
    foreach (var path in Directory.EnumerateFiles(scratch, "*.json"))
    {
        var document = JsonNode.Parse(File.ReadAllText(path))?.AsObject();
        if (document?["nodes"] is not JsonArray nodes)
            continue;
        var changed = false;
        foreach (var value in nodes)
        {
            var node = value!.AsObject();
            var id = node["id"]!.GetValue<string>();
            if (id == nonPublicNodeId)
            {
                node["public_normal_research"] = false;
                foundNonPublic = true;
                changed = true;
            }
            if (id == anyCapabilityNodeId)
            {
                node["capability_requirements"] = new JsonObject
                {
                    ["all_of"] = new JsonArray(),
                    ["any_of"] = new JsonArray(anyCapabilityIds.Select(id => JsonValue.Create(id)).ToArray()),
                    ["context"] = "civilization",
                };
                foundAnyCapability = true;
                changed = true;
            }
        }
        if (changed)
            File.WriteAllText(path, document.ToJsonString());
    }
    if (!foundNonPublic || !foundAnyCapability)
        throw new InvalidOperationException("Synthetic node mutations were not applied.");

    var facilityPath = Path.Combine(scratch, "biochemical_research_facilities.json");
    var facilityDocument = JsonNode.Parse(File.ReadAllText(facilityPath))!.AsObject();
    var nodeRequirement = facilityDocument["stage_requirements"]!.AsObject().First();
    anyFacilityNodeId = nodeRequirement.Key;
    var stageRequirement = nodeRequirement.Value!.AsObject().First();
    anyFacilityStage = Enum.Parse<ResearchMaturity>(stageRequirement.Key, ignoreCase: true);
    var existingAll = stageRequirement.Value!["all_of"]!.AsArray()
        .Select(value => value!.GetValue<string>()).ToHashSet(StringComparer.Ordinal);
    anyFacilityIds = facilityDocument["facility_capabilities"]!.AsArray()
        .Select(value => value!["id"]!.GetValue<string>())
        .Where(id => !existingAll.Contains(id)).Take(2).ToArray();
    stageRequirement.Value!.AsObject()["any_of"] =
        new JsonArray(anyFacilityIds.Select(id => JsonValue.Create(id)).ToArray());
    File.WriteAllText(facilityPath, facilityDocument.ToJsonString());
    return lease;
}

foreach (var node in catalog.Nodes.Values)
{
    var context = node.Applicability.Traits.Any(id => applicability.GetTrait(id).Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies)
        || node.CapabilityRequirements.AllOf.Concat(node.CapabilityRequirements.AnyOf).Any(id => catalog.Capabilities[id].Scope != ResearchCapabilityScope.Civilization)
        ? "context:canonical"
        : null;
    var builder = Satisfied(node, context);
    Case($"canonical:{node.Id}", "Scientific", builder, node.Id, context: context, contextPresent: context is not null);
}

var publicNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch);
Case("unknown-node", "Scientific", Satisfied(publicNode), "missing:node");
var syntheticAnyNode = catalog.Nodes.Values.First(node => node.Id != publicNode.Id && node.Applicability.Traits.Count == 0);
var syntheticAnyCapabilityIds = catalog.Capabilities.Values
    .Where(capability => capability.Scope != ResearchCapabilityScope.Civilization)
    .Take(2).Select(capability => capability.Id).ToArray();
var syntheticLease = CreateSyntheticRoot(publicNode.Id, syntheticAnyNode.Id,
    syntheticAnyCapabilityIds, out var syntheticFacilityNodeId,
    out var syntheticFacilityStage, out var syntheticFacilityIds);
var syntheticRoot = syntheticLease.Path;
var syntheticCatalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(syntheticRoot);
var syntheticApplicability = AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(syntheticRoot, syntheticCatalog);
var syntheticFacilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(syntheticRoot, syntheticCatalog);
var syntheticEvaluator = new AdaptiveResearchEligibilityEvaluator(syntheticCatalog, syntheticApplicability, syntheticFacilities);
var syntheticNonPublic = syntheticCatalog.GetNode(publicNode.Id);
Case("non-public", "Scientific",
    Satisfied(syntheticNonPublic, selectedCatalog: syntheticCatalog,
        selectedApplicability: syntheticApplicability, selectedFacilities: syntheticFacilities),
    syntheticNonPublic.Id, catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);

var allPrerequisite = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.Prerequisites.AllOf.Count > 0);
var missingPrerequisite = Satisfied(allPrerequisite);
missingPrerequisite.RemoveNode(allPrerequisite.Prerequisites.AllOf[0]);
Case("missing-prerequisite", "Scientific", missingPrerequisite, allPrerequisite.Id, context: "context:target", contextPresent: true);

var anyPrerequisite = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.Prerequisites.AnyOf.Count > 0);
var missingAnyPrerequisite = Satisfied(anyPrerequisite);
foreach (var id in anyPrerequisite.Prerequisites.AnyOf)
    missingAnyPrerequisite.RemoveNode(id);
Case("missing-alternative-prerequisite", "Scientific", missingAnyPrerequisite, anyPrerequisite.Id, context: "context:target", contextPresent: true);

var populationTraitNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.Applicability.Traits.Any(id => applicability.GetTrait(id).Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies));
Case("missing-applicability-context", "Scientific", Satisfied(populationTraitNode), populationTraitNode.Id);
var missingPopulationTrait = Satisfied(populationTraitNode);
var populationTrait = populationTraitNode.Applicability.Traits.First(id => applicability.GetTrait(id).Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies);
missingPopulationTrait.RemoveContextTrait("context:target", populationTrait);
Case("missing-applicability-trait", "Scientific", missingPopulationTrait, populationTraitNode.Id, context: "context:target", contextPresent: true);

var evidenceNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence).Any());
var missingEvidence = Satisfied(evidenceNode);
missingEvidence.RemoveEvidence("evidence:0");
Case("missing-evidence", "Scientific", missingEvidence, evidenceNode.Id, context: "context:target", contextPresent: true);
var firstEvidenceId = evidenceNode.Applicability.EvidenceTypes.Concat(evidenceNode.ProjectRequirements.RequiredEvidence).Distinct(StringComparer.Ordinal).First();
var globalEvidence = Satisfied(evidenceNode);
globalEvidence.RemoveEvidence("evidence:0");
globalEvidence.Evidence(new ResearchEvidenceInstance("evidence:global", firstEvidenceId, "fixture", 1.0, 1.0, null, 0));
Case("global-evidence-matches-target-context", "Scientific", globalEvidence, evidenceNode.Id, context: "context:target", contextPresent: true);
var mismatchedEvidence = Satisfied(evidenceNode);
mismatchedEvidence.RemoveEvidence("evidence:0");
mismatchedEvidence.Evidence(new ResearchEvidenceInstance("evidence:other", firstEvidenceId, "fixture", 1.0, 1.0, "context:other", 0));
Case("mismatched-context-evidence", "Scientific", mismatchedEvidence, evidenceNode.Id, context: "context:target", contextPresent: true);

var pressureNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.ProjectRequirements.RequiredPressure.Count > 0);
var missingPressure = Satisfied(pressureNode);
var pressure = pressureNode.ProjectRequirements.RequiredPressure.First();
missingPressure.Pressure(pressure.Key, pressure.Value - 0.000002);
Case("missing-pressure", "Scientific", missingPressure, pressureNode.Id, context: "context:target", contextPresent: true);
var pressureThreshold = Satisfied(pressureNode);
pressureThreshold.Pressure(pressure.Key, pressure.Value - 0.000001);
Case("pressure-threshold", "Scientific", pressureThreshold, pressureNode.Id, context: "context:target", contextPresent: true);
var nanPressure = Satisfied(pressureNode);
nanPressure.Pressure(pressure.Key, double.NaN);
Case("pressure-nan-source-comparison", "Scientific", nanPressure, pressureNode.Id, context: "context:target", contextPresent: true);

var anyPressureNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.ProjectRequirements.RequiredPressureAny.Count > 0);
var missingAnyPressure = Satisfied(anyPressureNode);
foreach (var pair in anyPressureNode.ProjectRequirements.RequiredPressureAny)
    missingAnyPressure.Pressure(pair.Key, 0.0);
Case("missing-alternative-pressure", "Scientific", missingAnyPressure, anyPressureNode.Id, context: "context:target", contextPresent: true);
var anyPressureFallback = Satisfied(anyPressureNode);
foreach (var pair in anyPressureNode.ProjectRequirements.RequiredPressureAny)
    anyPressureFallback.Pressure(pair.Key, 0.0);
var lastPressure = anyPressureNode.ProjectRequirements.RequiredPressureAny.Last();
anyPressureFallback.Pressure(lastPressure.Key, lastPressure.Value);
Case("alternative-pressure-fallback", "Scientific", anyPressureFallback, anyPressureNode.Id, context: "context:target", contextPresent: true);

var capabilityNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch && node.CapabilityRequirements.AllOf.Count > 0);
var missingCapability = Satisfied(capabilityNode);
var requiredCapability = capabilityNode.CapabilityRequirements.AllOf[0];
var requiredCapabilityContext = catalog.Capabilities[requiredCapability].Scope == ResearchCapabilityScope.Civilization ? null : "context:target";
missingCapability.RemoveCapability(requiredCapability, requiredCapabilityContext);
Case("missing-capability", "Scientific", missingCapability, capabilityNode.Id, context: "context:target", contextPresent: true);

var anyCapabilityNode = catalog.Nodes.Values.FirstOrDefault(node => node.PublicNormalResearch && node.CapabilityRequirements.AnyOf.Count > 0);
if (anyCapabilityNode is not null)
{
    var missingAnyCapability = Satisfied(anyCapabilityNode);
    foreach (var id in anyCapabilityNode.CapabilityRequirements.AnyOf)
        missingAnyCapability.RemoveCapability(id, catalog.Capabilities[id].Scope == ResearchCapabilityScope.Civilization ? null : "context:target");
    Case("missing-alternative-capability", "Scientific", missingAnyCapability, anyCapabilityNode.Id, context: "context:target", contextPresent: true);
    var anyCapabilityFallback = Satisfied(anyCapabilityNode);
    foreach (var id in anyCapabilityNode.CapabilityRequirements.AnyOf)
        anyCapabilityFallback.RemoveCapability(id, catalog.Capabilities[id].Scope == ResearchCapabilityScope.Civilization ? null : "context:target");
    var lastCapability = anyCapabilityNode.CapabilityRequirements.AnyOf.Last();
    anyCapabilityFallback.Capability(lastCapability, catalog.Capabilities[lastCapability].Scope == ResearchCapabilityScope.Civilization ? null : "context:target");
    Case("alternative-capability-fallback", "Scientific", anyCapabilityFallback, anyCapabilityNode.Id, context: "context:target", contextPresent: true);
}
else
{
    var node = syntheticCatalog.GetNode(syntheticAnyNode.Id);
    var missing = Satisfied(node, selectedCatalog: syntheticCatalog,
        selectedApplicability: syntheticApplicability, selectedFacilities: syntheticFacilities);
    foreach (var id in syntheticAnyCapabilityIds)
        missing.RemoveCapability(id, "context:target");
    Case("missing-alternative-capability", "Scientific", missing, node.Id,
        context: "context:target", contextPresent: true,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
    var fallback = Satisfied(node, selectedCatalog: syntheticCatalog,
        selectedApplicability: syntheticApplicability, selectedFacilities: syntheticFacilities);
    foreach (var id in syntheticAnyCapabilityIds)
        fallback.RemoveCapability(id, "context:target");
    fallback.Capability(syntheticAnyCapabilityIds.Last(), "context:target");
    Case("alternative-capability-fallback", "Scientific", fallback, node.Id,
        context: "context:target", contextPresent: true,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
    var nullScoped = Satisfied(node, selectedCatalog: syntheticCatalog,
        selectedApplicability: syntheticApplicability, selectedFacilities: syntheticFacilities);
    foreach (var id in syntheticAnyCapabilityIds)
        nullScoped.RemoveCapability(id, "context:target");
    nullScoped.Capability(syntheticAnyCapabilityIds[0], null);
    Case("scoped-capability-null-does-not-match", "Scientific", nullScoped, node.Id,
        context: "context:target", contextPresent: true,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
    var emptyScoped = Satisfied(node, selectedCatalog: syntheticCatalog,
        selectedApplicability: syntheticApplicability, selectedFacilities: syntheticFacilities);
    foreach (var id in syntheticAnyCapabilityIds)
        emptyScoped.RemoveCapability(id, "context:target");
    emptyScoped.Capability(syntheticAnyCapabilityIds.Last(), "");
    Case("scoped-capability-empty-context", "Scientific", emptyScoped, node.Id,
        context: "", contextPresent: true,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
}

var startNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch);
var allowedStart = Satisfied(startNode);
Case("project-start-allowed", "ProjectStart", allowedStart, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var absentNode = Satisfied(startNode, includeNode: false);
Case("node-not-visible", "ProjectStart", absentNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var rumoredNode = Satisfied(startNode, maturity: ResearchMaturity.Rumored);
Case("node-not-investigable", "ProjectStart", rumoredNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var matureNode = Satisfied(startNode, maturity: ResearchMaturity.Mature);
Case("already-mature", "ProjectStart", matureNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var archivedNode = Satisfied(startNode, maturity: ResearchMaturity.Archived);
archivedNode.Node(startNode.Id, ResearchMaturity.Archived, "mature_history");
Case("archived-established", "ProjectStart", archivedNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var activeNode = Satisfied(startNode);
activeNode.Project(startNode.Id, true);
Case("already-active-paused", "ProjectStart", activeNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);

var limitedStage = catalog.DirectedProgramStages.Values.First(stage => stage.DirectedProgramLimit.HasValue);
var capacityNode = Satisfied(startNode);
capacityNode.Stage(limitedStage.Id);
foreach (var id in catalog.Nodes.Keys.Where(id => id != startNode.Id).Take(limitedStage.DirectedProgramLimit!.Value))
    capacityNode.Project(id, false);
Case("directed-capacity", "ProjectStart", capacityNode, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var pausedCapacity = Satisfied(startNode);
pausedCapacity.Stage(limitedStage.Id);
foreach (var id in catalog.Nodes.Keys.Where(id => id != startNode.Id).Take(limitedStage.DirectedProgramLimit!.Value))
    pausedCapacity.Project(id, true);
Case("paused-projects-do-not-use-capacity", "ProjectStart", pausedCapacity, startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
var unknownProgramStage = Satisfied(startNode);
unknownProgramStage.Stage("missing:directed-stage");
Case("unknown-directed-program-stage", "ProjectStart", unknownProgramStage,
    startNode.Id, startNode.ProjectRequirements.MinimumLabs, "context:target", true);
Case("scientific-ignores-unknown-program-stage", "Scientific", unknownProgramStage,
    startNode.Id, context: "context:target", contextPresent: true);
Case("unknown-node-project-start-bypasses-program-stage", "ProjectStart", unknownProgramStage,
    "missing:node", double.NegativeInfinity, "context:target", true);

var belowLabs = Satisfied(startNode);
Case("below-minimum-labs", "ProjectStart", belowLabs, startNode.Id, startNode.ProjectRequirements.MinimumLabs - 0.000002, "context:target", true);
var minimumThreshold = Satisfied(startNode);
Case("minimum-labs-threshold", "ProjectStart", minimumThreshold, startNode.Id, startNode.ProjectRequirements.MinimumLabs - 0.000001, "context:target", true);
var insufficientLabs = Satisfied(startNode);
insufficientLabs.Labs(startNode.ProjectRequirements.MinimumLabs);
Case("insufficient-free-labs", "ProjectStart", insufficientLabs, startNode.Id, startNode.ProjectRequirements.MinimumLabs + 0.000002, "context:target", true);
Case("requested-labs-nan", "ProjectStart", Satisfied(startNode), startNode.Id, double.NaN, "context:target", true);
Case("requested-labs-positive-infinity", "ProjectStart", Satisfied(startNode), startNode.Id, double.PositiveInfinity, "context:target", true);
Case("requested-labs-negative-infinity", "ProjectStart", Satisfied(startNode), startNode.Id, double.NegativeInfinity, "context:target", true);

var allFacility = (from node in catalog.Nodes.Values
                   from stage in Enum.GetValues<ResearchMaturity>()
                   let requirement = facilities.GetStageRequirement(node.Id, stage)
                   where requirement is not null && requirement.AllOf.Count > 0
                   select (node, stage, requirement)).First();
Case("missing-facility", "StageFacility", new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache), allFacility.node.Id, stage: allFacility.stage);
var allFacilityAllowed = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache);
foreach (var id in allFacility.requirement!.AllOf) allFacilityAllowed.Facility(id);
if (allFacility.requirement.AnyOf.Count > 0) allFacilityAllowed.Facility(allFacility.requirement.AnyOf[0]);
Case("facility-all-allowed", "StageFacility", allFacilityAllowed, allFacility.node.Id, stage: allFacility.stage);
var scientificIgnoresOperational = Satisfied(allFacility.node);
foreach (var id in allFacility.requirement.AllOf) scientificIgnoresOperational.RemoveFacility(id);
foreach (var id in allFacility.requirement.AnyOf) scientificIgnoresOperational.RemoveFacility(id);
scientificIgnoresOperational.Labs(0.0);
scientificIgnoresOperational.Stage(limitedStage.Id);
foreach (var id in catalog.Nodes.Keys.Where(id => id != allFacility.node.Id).Take(limitedStage.DirectedProgramLimit!.Value))
    scientificIgnoresOperational.Project(id, false);
Case("scientific-excludes-labs-capacity-facilities", "Scientific", scientificIgnoresOperational,
    allFacility.node.Id, context: "context:target", contextPresent: true);
var projectNeedsFacilities = Satisfied(allFacility.node);
foreach (var id in allFacility.requirement.AllOf) projectNeedsFacilities.RemoveFacility(id);
foreach (var id in allFacility.requirement.AnyOf) projectNeedsFacilities.RemoveFacility(id);
Case("project-start-includes-experimental-facilities", "ProjectStart", projectNeedsFacilities,
    allFacility.node.Id, allFacility.node.ProjectRequirements.MinimumLabs,
    "context:target", true);

var anyFacility = (from node in catalog.Nodes.Values
                   from stage in Enum.GetValues<ResearchMaturity>()
                   let requirement = facilities.GetStageRequirement(node.Id, stage)
                   where requirement is not null && requirement.AnyOf.Count > 0
                   select (node, stage, requirement)).FirstOrDefault();
if (anyFacility.requirement is not null)
{
    var anyFacilityMissing = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache);
    foreach (var id in anyFacility.requirement.AllOf) anyFacilityMissing.Facility(id);
    Case("missing-alternative-facility", "StageFacility", anyFacilityMissing, anyFacility.node.Id, stage: anyFacility.stage);
    var anyFacilityLast = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache);
    foreach (var id in anyFacility.requirement.AllOf) anyFacilityLast.Facility(id);
    anyFacilityLast.Facility(anyFacility.requirement.AnyOf.Last());
    Case("alternative-facility-fallback", "StageFacility", anyFacilityLast, anyFacility.node.Id, stage: anyFacility.stage);
}
else
{
    var requirement = syntheticFacilities.GetStageRequirement(syntheticFacilityNodeId, syntheticFacilityStage)!;
    var missing = new StateBuilder(syntheticCatalog.Metadata.StartingDirectedProgramStageId, methodCache);
    foreach (var id in requirement.AllOf) missing.Facility(id);
    Case("missing-alternative-facility", "StageFacility", missing,
        syntheticFacilityNodeId, stage: syntheticFacilityStage,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
    var fallback = new StateBuilder(syntheticCatalog.Metadata.StartingDirectedProgramStageId, methodCache);
    foreach (var id in requirement.AllOf) fallback.Facility(id);
    fallback.Facility(syntheticFacilityIds.Last());
    Case("alternative-facility-fallback", "StageFacility", fallback,
        syntheticFacilityNodeId, stage: syntheticFacilityStage,
        catalogVariant: "synthetic", selectedEvaluator: syntheticEvaluator);
}
Case("stage-facility-unknown-node", "StageFacility", new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache), "missing:node", stage: ResearchMaturity.Experimental);
Case("stage-facility-unknown-maturity", "StageFacility", new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache), publicNode.Id, stage: (ResearchMaturity)999);

var emptyContextNode = catalog.Nodes.Values.First(node => node.PublicNormalResearch &&
    (node.Applicability.Traits.Any(id => applicability.GetTrait(id).Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies)
     || node.CapabilityRequirements.AllOf.Concat(node.CapabilityRequirements.AnyOf).Any(id => catalog.Capabilities[id].Scope != ResearchCapabilityScope.Civilization)));
Case("empty-context-is-present", "Scientific", Satisfied(emptyContextNode, "context:target"), emptyContextNode.Id, context: "", contextPresent: true);

var sourceOnly = new List<object>();
foreach (var method in new[] { "Scientific", "ProjectStart", "StageFacility" })
{
    Exception? error = null;
    Func<ResearchEligibilityResult> invoke = method switch
    {
        "Scientific" => () => evaluator.EvaluateScientificEligibility(null!, publicNode.Id),
        "ProjectStart" => () => evaluator.EvaluateProjectStart(null!, publicNode.Id, 1.0),
        "StageFacility" => () => evaluator.EvaluateStageFacilityEligibility(null!, publicNode.Id, ResearchMaturity.Experimental),
        _ => throw new InvalidOperationException("Invalid source-only method."),
    };
    try
    {
        _ = invoke();
    }
    catch (Exception caught)
    {
        error = caught;
    }
    sourceOnly.Add(new { Kind = "null-state", Method = method, Error = error is null ? null : new { Type = error.GetType().Name, error.Message } });
}
foreach (var method in new[] { "Scientific", "ProjectStart", "StageFacility" })
{
    Exception? error = null;
    var sourceOnlyState = new AdaptiveResearchCivilizationState("source:null-node", catalog.Metadata.StartingDirectedProgramStageId);
    Func<ResearchEligibilityResult> invoke = method switch
    {
        "Scientific" => () => evaluator.EvaluateScientificEligibility(sourceOnlyState, null!),
        "ProjectStart" => () => evaluator.EvaluateProjectStart(sourceOnlyState, null!, 1.0),
        "StageFacility" => () => evaluator.EvaluateStageFacilityEligibility(sourceOnlyState, null!, ResearchMaturity.Experimental),
        _ => throw new InvalidOperationException("Invalid source-only method."),
    };
    try
    {
        _ = invoke();
    }
    catch (Exception caught)
    {
        error = caught;
    }
    sourceOnly.Add(new { Kind = "null-node-id", Method = method, Error = error is null ? null : new { Type = error.GetType().Name, error.Message } });
}

var output = new
{
    Schema = "stellar-adaptive-research-eligibility-v1",
    Culture = "InvariantCulture",
    CatalogId = catalog.Metadata.CatalogId,
    CanonicalNodeCount = catalog.Nodes.Count,
    Synthetic = new
    {
        NonPublicNodeId = publicNode.Id,
        AnyCapabilityNodeId = syntheticAnyNode.Id,
        AnyCapabilityIds = syntheticAnyCapabilityIds,
        AnyFacilityNodeId = syntheticFacilityNodeId,
        AnyFacilityStage = syntheticFacilityStage,
        AnyFacilityIds = syntheticFacilityIds,
    },
    Cases = cases,
    SourceOnly = sourceOnly,
};
File.WriteAllText(args[1], JsonSerializer.Serialize(output, options) + Environment.NewLine);
if (!syntheticLease.TryCleanup(out var cleanupError))
{
    Console.Error.WriteLine($"research eligibility oracle cleanup failure: {cleanupError}");
    return 1;
}
Console.WriteLine($"research eligibility oracle: {cases.Count} native cases over {catalog.Nodes.Count} canonical nodes; {sourceOnly.Count} source-only null cases");
return 0;

sealed class StateBuilder
{
    private readonly Dictionary<(string, int), MethodInfo> methods;
    public AdaptiveResearchCivilizationState State { get; }
    public List<object> Setup { get; } = new();

    public StateBuilder(string stageId, Dictionary<(string, int), MethodInfo> methods)
    {
        this.methods = methods;
        State = new AdaptiveResearchCivilizationState("civ:eligibility", stageId);
    }

    private void Invoke(string op, object input, string method, params object?[] values)
    {
        Setup.Add(new { Op = op, Input = input });
        try
        {
            methods[(method, values.Length)].Invoke(State, values);
        }
        catch (TargetInvocationException error)
        {
            throw error.InnerException ?? error;
        }
    }

    public void Labs(double value) => Invoke("SetLabs", new { Value = value }, "SetTotalEffectiveResearchLabs", value);
    public void Pressure(string id, double value) => Invoke("SetPressure", new { Id = id, Value = value }, "SetPressure", id, value);
    public void Evidence(ResearchEvidenceInstance value) => Invoke("AddEvidence", value, "AddEvidence", value);
    public void RemoveEvidence(string id) => Invoke("RemoveEvidence", new { Id = id }, "RemoveEvidence", id);
    public void Trait(string id) => Invoke("AddTrait", new { Id = id }, "AddCivilizationTrait", id);
    public void RemoveTrait(string id) => Invoke("RemoveTrait", new { Id = id }, "RemoveCivilizationTrait", id);
    public void ContextTrait(string context, string trait) => Invoke("AddContextTrait", new { Context = context, Trait = trait }, "AddApplicabilityTrait", context, trait);
    public void RemoveContextTrait(string context, string trait) => Invoke("RemoveContextTrait", new { Context = context, Trait = trait }, "RemoveApplicabilityTrait", context, trait);
    public void Capability(string id, string? context) => Invoke("AddCapability", new { Id = id, Context = context }, "AddCapability", id, context);
    public void RemoveCapability(string id, string? context) => Invoke("RemoveCapability", new { Id = id, Context = context }, "RemoveCapability", id, context);
    public void Facility(string id) => Invoke("AddFacility", new { Id = id }, "AddFacilityCapability", id);
    public void RemoveFacility(string id) => Invoke("RemoveFacility", new { Id = id }, "RemoveFacilityCapability", id);
    public void Node(string id, ResearchMaturity maturity = ResearchMaturity.Mature, string? resolution = null) =>
        Invoke("SetNode", new ResearchNodeRuntimeState(id, maturity, resolution, 0.0, 0.0, 0), "SetNodeState", new ResearchNodeRuntimeState(id, maturity, resolution, 0.0, 0.0, 0));
    public void RemoveNode(string id) => Invoke("RemoveNode", new { Id = id }, "RemoveNodeState", id);
    public void Stage(string id) => Invoke("SetStage", new { Id = id }, "SetDirectedProgramStage", id);
    public void Project(string id, bool paused, double labs = 0.0) =>
        Invoke("SetProject", new ResearchProjectRuntimeState(id, ResearchMaturity.Experimental, null, labs, 1.0, paused, paused ? "paused" : null, 0.0, 0.0, 0), "SetProject", new ResearchProjectRuntimeState(id, ResearchMaturity.Experimental, null, labs, 1.0, paused, paused ? "paused" : null, 0.0, 0.0, 0));
}

sealed class ScratchLease : IDisposable
{
    private readonly string parent;
    private readonly string claimPath;
    private bool cleaned;
    public string Path { get; }

    public ScratchLease(string source)
    {
        var normalizedSource = System.IO.Path.GetFullPath(source);
        if (!Directory.Exists(normalizedSource))
            throw new DirectoryNotFoundException(normalizedSource);
        parent = System.IO.Path.GetFullPath(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "stellar-research-eligibility-scratch"));
        Directory.CreateDirectory(parent);
        claimPath = System.IO.Path.Combine(parent, $"{Guid.NewGuid():N}.claim");
        using (new FileStream(claimPath, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { }
        Path = claimPath + ".directory";
        try
        {
            if (Directory.Exists(Path))
                throw new IOException($"Scratch directory already exists: {Path}");
            Directory.CreateDirectory(Path);
            if (!string.Equals(System.IO.Path.GetDirectoryName(Path), parent, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Scratch directory escaped its owned parent.");
            foreach (var file in Directory.EnumerateFiles(normalizedSource))
                File.Copy(file, System.IO.Path.Combine(Path, System.IO.Path.GetFileName(file)));
        }
        catch
        {
            TryCleanup(out _);
            throw;
        }
    }

    public bool TryCleanup(out string? error)
    {
        error = null;
        if (cleaned)
            return true;
        try
        {
            if (!string.Equals(System.IO.Path.GetDirectoryName(Path), parent, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Refusing to clean a scratch directory outside its owned parent.");
            if (Directory.Exists(Path))
                Directory.Delete(Path, recursive: true);
            if (File.Exists(claimPath))
                File.Delete(claimPath);
            cleaned = true;
            return true;
        }
        catch (Exception caught)
        {
            error = caught.Message;
            return false;
        }
    }

    public void Dispose() => TryCleanup(out _);
}
