using System.Globalization;
using System.Text.Json;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

static object Core(AdaptiveResearchCivilizationState state) => new
{
    state.CivilizationId,
    state.Revision,
    state.MaterializedViewRevision,
    Nodes = state.NodeStates.Values.Select(value => new
    {
        value.NodeId,
        Maturity = (int)value.Maturity,
        value.Resolution,
        value.StageResearchPoints,
        value.TotalResearchPoints,
        value.Revision,
    }).ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
};

static object Pressure(AdaptiveResearchPressureState state) => new
{
    state.Revision,
    MetricSignals = state.MetricSignals.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    ActivePressureIds = state.ActivePressureIds.ToArray(),
};

static object Agenda(AdaptiveResearchAgendaState state) => new
{
    state.Revision,
    Problems = state.ProblemPriorities.Select(pair => new { Id = pair.Key, Priority = pair.Value }).ToArray(),
};

static object Foreign(AdaptiveResearchForeignTechnologyState state) => new
{
    state.Revision,
    Assessments = state.Assessments.Values.Select(value => new
    {
        value.ForeignTechnologyReference,
        value.SourceLineageReference,
        Understanding = (int)value.Understanding,
        Adaptation = (int)value.Adaptation,
        value.KnownConstraintIds,
        value.Confidence,
        value.Revision,
    }).ToArray(),
};

static object Discovery((ForeignTechnologyAssessmentRuntimeState Assessment,
                         IReadOnlyList<ForeignResearchAwarenessEvent> AwarenessEvents) value) => new
{
    Assessment = new
    {
        value.Assessment.ForeignTechnologyReference,
        value.Assessment.SourceLineageReference,
        Understanding = (int)value.Assessment.Understanding,
        Adaptation = (int)value.Assessment.Adaptation,
        value.Assessment.KnownConstraintIds,
        value.Assessment.Confidence,
        value.Assessment.Revision,
    },
    Events = value.AwarenessEvents.Select(item => new
    {
        item.ForeignTechnologyReference,
        item.NodeId,
        PreviousState = item.PreviousState is null ? (int?)null : (int)item.PreviousState,
        NewState = (int)item.NewState,
        item.Reason,
    }).ToArray(),
};

static object Plan(PlannedResearchOutcome value) => new
{
    value.NodeId,
    value.CheckpointId,
    value.AttemptIndex,
    Profile = (int)value.Profile,
    Outcome = (int)value.Outcome,
    value.DeterministicRoll,
    value.PlannedSideDiscoveryNodeId,
    value.Explanation,
};

try
{
    if (args.Length != 2)
    {
        Console.Error.WriteLine("usage: ResearchStrategicOwnerOracle <research-data> <fixture>");
        return 1;
    }
    var root = Path.GetFullPath(args[0]);
    var fixture = Path.GetFullPath(args[1]);
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var state = runtime.Authority.CreateCivilizationState("fixture:strategic-owner");
    var initial = new
    {
        Core = Core(state),
        Pressure = Pressure(runtime.Pressure.GetSupportState(state)),
        Agenda = Agenda(runtime.Agenda.GetState(state)),
        Foreign = Foreign(runtime.ForeignTechnology.GetState(state)),
        OutcomeRevision = runtime.Outcomes.GetState(state).Revision,
    };
    runtime.Pressure.ReportMetricSignal(state, "unresolved_alien_signal_relevance", 0.6);
    runtime.Agenda.SetProblemPriority(state, "alien_signal", "critical");
    var discovery = runtime.ForeignDiscovery.Observe(
        state, "foreign:facade", "lineage:facade", 0.75, 2120,
        new[] { "scientific_gap" });
    var plan = runtime.Outcomes.PlanOutcome(
        state, "xenolinguistics", "seed:facade", "checkpoint:facade");
    var output = new
    {
        Generator = "actual C# AdaptiveResearchStrategicRuntime",
        Catalogs = new
        {
            runtime.Authority.Catalog.Metadata.CatalogId,
            PressureRules = runtime.PressureCatalog.Rules.Count,
            AgendaPriorities = runtime.AgendaCatalog.Priorities.Count,
            ForeignComponents = runtime.ForeignTechnologyCatalog.Components.Count,
            DiscoveryRules = runtime.ForeignDiscoveryCatalog.MethodRules.Count,
            OutcomeSideIndexes = runtime.OutcomeCatalog.SideDiscoveryCandidatesByNode.Count,
        },
        StableRepeatedGetters =
            ReferenceEquals(runtime.Authority, runtime.Authority) &&
            ReferenceEquals(runtime.Pressure, runtime.Pressure) &&
            ReferenceEquals(runtime.Agenda, runtime.Agenda) &&
            ReferenceEquals(runtime.ForeignTechnology, runtime.ForeignTechnology) &&
            ReferenceEquals(runtime.ForeignDiscovery, runtime.ForeignDiscovery) &&
            ReferenceEquals(runtime.Outcomes, runtime.Outcomes),
        Initial = initial,
        Discovery = Discovery(discovery),
        Plan = Plan(plan),
        Final = new
        {
            Core = Core(state),
            Pressure = Pressure(runtime.Pressure.GetSupportState(state)),
            Agenda = Agenda(runtime.Agenda.GetState(state)),
            Foreign = Foreign(runtime.ForeignTechnology.GetState(state)),
            OutcomeRevision = runtime.Outcomes.GetState(state).Revision,
        },
    };
    File.WriteAllText(fixture, JsonSerializer.Serialize(output));
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine($"ResearchStrategicOwnerOracle failed: {error}\nCWD: {Environment.CurrentDirectory}\nResearch root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}\nFixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
