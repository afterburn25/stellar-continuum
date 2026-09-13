using System.Globalization;
using System.Security.Cryptography;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";
try
{
    if (args.Length != 2)
        throw new ArgumentException("Expected canonical research directory and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);

    string Fingerprint()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(root, "*.json")
                     .OrderBy(path => Path.GetFileName(path), StringComparer.Ordinal))
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    object Error(Exception? error) => error is null ? null! :
        new { Type = error.GetType().Name, error.Message };
    object Number(double value) => double.IsNaN(value) ? "NaN" :
        value == double.PositiveInfinity ? "Infinity" :
        value == double.NegativeInfinity ? "-Infinity" : value;
    object QuoteView(AdaptiveResearchFundingQuote quote) => new
    {
        AssignedEffectiveLabs = Number(quote.AssignedEffectiveLabs),
        AuthorizationCredits = Number(quote.AuthorizationCredits),
        MilestoneCommitmentCredits = Number(quote.MilestoneCommitmentCredits),
        OperatingCreditsPerDay = Number(quote.OperatingCreditsPerDay),
        EstimatedTotalOperatingCredits = Number(quote.EstimatedTotalOperatingCredits),
        EstimatedTotalCredits = Number(quote.EstimatedTotalCredits),
        EstimatedYearsAtFullFunding = Number(quote.EstimatedYearsAtFullFunding),
        quote.Complexity,
    };

    var beforeLoad = Fingerprint();
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var afterLoad = Fingerprint();
    if (beforeLoad != afterLoad)
        throw new InvalidOperationException("Runtime loading changed canonical inputs.");
    var catalog = runtime.Authority.Catalog;
    var rows = new List<object>();

    void AddQuote(string name, AdaptiveResearchNodeDefinition node, double labs)
    {
        var before = Fingerprint();
        Func<AdaptiveResearchFundingQuote> call = () =>
            AdaptiveResearchFundingPolicy.Quote(node, labs, catalog);
        AdaptiveResearchFundingQuote? quote = null;
        Exception? error = null;
        try { quote = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        rows.Add(new
        {
            Name = name, Kind = "quote", NodeId = node.Id,
            Node = new
            {
                node.Id,
                node.Complexity,
                node.ProjectRequirements.BaseResearchPoints,
                node.ProjectRequirements.RecommendedLabs,
            },
            Labs = Number(labs), BeforeFingerprint = before,
            AfterFingerprint = after, Error = Error(error),
            Quote = quote is null ? null : QuoteView(quote),
        });
    }

    foreach (var complexity in new[] { "foundation", "developing", "advanced", "frontier" })
    {
        var node = catalog.Nodes.Values.First(value =>
            string.Equals(value.Complexity, complexity, StringComparison.Ordinal));
        AddQuote("quote-" + complexity, node, node.ProjectRequirements.RecommendedLabs);
    }
    var quoteNode = catalog.Nodes.Values.First();
    AddQuote("quote-zero-labs", quoteNode, 0);
    AddQuote("quote-fractional-labs", quoteNode, 2.675);
    AddQuote("quote-mutated-passed-definition", quoteNode with
    {
        Complexity = "frontier",
        ProjectRequirements = quoteNode.ProjectRequirements with
        {
            BaseResearchPoints = 123,
            RecommendedLabs = 7,
        },
    }, 2.675);
    AddQuote("quote-max-finite-labs", quoteNode, double.MaxValue);
    AddQuote("quote-negative-labs", quoteNode, -1);
    AddQuote("quote-nan-labs", quoteNode, double.NaN);
    AddQuote("quote-positive-infinity-labs", quoteNode, double.PositiveInfinity);

    void AddComplexity(string name, string complexity)
    {
        var before = Fingerprint();
        Func<(double Authorization, double Milestone, double Multiplier)> call = () =>
            (AdaptiveResearchFundingPolicy.AuthorizationCredits(complexity),
             AdaptiveResearchFundingPolicy.MilestoneCommitmentCredits(complexity),
             AdaptiveResearchFundingPolicy.ComplexityMultiplier(complexity));
        (double Authorization, double Milestone, double Multiplier)? value = null;
        Exception? error = null;
        try { value = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        rows.Add(new
        {
            Name = name, Kind = "complexity", Input = complexity,
            BeforeFingerprint = before, AfterFingerprint = after,
            Error = Error(error),
            Value = value is null ? null : new
            {
                value.Value.Authorization,
                value.Value.Milestone,
                value.Value.Multiplier,
            },
        });
    }
    foreach (var complexity in new[]
             { "foundation", " DEVELOPING ", "\u2003Advanced\u3000", "frontier", "", "unknown" })
        AddComplexity("complexity-" + (complexity.Length == 0 ? "empty" :
            complexity.Trim().ToLowerInvariant()), complexity);

    void AddRunway(string name, double available, double beforeResearch,
                   double research)
    {
        var before = Fingerprint();
        Func<double> call = () => AdaptiveResearchFundingPolicy
            .EstimateTreasuryRunwayDays(available, beforeResearch, research);
        double? value = null;
        Exception? error = null;
        try { value = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        rows.Add(new
        {
            Name = name, Kind = "runway",
            Available = Number(available), BeforeResearch = Number(beforeResearch),
            Research = Number(research), BeforeFingerprint = before,
            AfterFingerprint = after, Error = Error(error),
            Value = value is null ? null : Number(value.Value),
        });
    }
    AddRunway("runway-surplus", 100, 2, 1);
    AddRunway("runway-zero-burn", 100, 1, 1);
    AddRunway("runway-threshold", 100, 0, 0.0000001);
    AddRunway("runway-over-threshold", 100, 0, 0.0000001000001);
    AddRunway("runway-zero-credits", 0, 0, 2);
    AddRunway("runway-negative-available", -1, 0, 1);
    AddRunway("runway-nan-before", 1, double.NaN, 1);
    AddRunway("runway-infinite-research", 1, 0, double.PositiveInfinity);

    foreach (var (name, quote) in new[]
    {
        ("credits-needed-normal", new AdaptiveResearchFundingQuote(
            1, 2, 3, 4, 0, 0, 0, "fixture")),
        ("credits-needed-nan", new AdaptiveResearchFundingQuote(
            1, double.NaN, 3, 4, 0, 0, 0, "fixture")),
        ("credits-needed-infinity", new AdaptiveResearchFundingQuote(
            1, 2, 3, double.PositiveInfinity, 0, 0, 0, "fixture")),
    })
    {
        var before = Fingerprint();
        Func<double> call = () =>
            AdaptiveResearchCampaignCommands.CreditsNeededToStart(quote);
        double value = 0;
        Exception? error = null;
        try { value = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        rows.Add(new
        {
            Name = name, Kind = "credits-needed", Input = QuoteView(quote),
            BeforeFingerprint = before, AfterFingerprint = after,
            Error = Error(error), Value = Number(value),
        });
    }

    CivilizationState Civilization(int id = 1,
        string species = SpeciesCatalog.TerranBaselineId) =>
        new(id, "Civilization " + id, 0, CivilizationArchetype.Scientific,
            new(0, 0, 0, 0, 0, 0, false), true,
            CivilizationDevelopmentStage.WarpCapable, false,
            SpeciesId: species);
    GalaxyState Galaxy(IList<CivilizationState> civilizations,
                       IReadOnlyList<CivilizationEconomyState> economies) => new()
    {
        Seed = 1,
        Systems = Array.Empty<StarSystemState>(),
        PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
        Civilizations = civilizations,
        Fleets = new List<FleetState>(),
        Colonies = new List<ColonyState>(),
        Economies = economies,
        Technologies = new List<TechnologyState>(),
        ConstructionStates = new List<ConstructionState>(),
        ShipyardStates = new List<ShipyardState>(),
        PlayerCivilizationId = civilizations.FirstOrDefault()?.Id ?? 0,
        Knowledge = new CivilizationKnowledgeState(),
    };
    (GalaxyState Galaxy, AdaptiveResearchCampaignState Campaign,
        string NodeId, double Labs) Setup(double credits = 500)
    {
        var civilization = Civilization();
        var galaxy = Galaxy(new List<CivilizationState> { civilization },
            new[] { new CivilizationEconomyState
                { CivilizationId = civilization.Id, Credits = credits } });
        var campaign = new AdaptiveResearchCampaignFactory(runtime).Create(galaxy);
        var state = campaign.GetCivilization(civilization.Id);
        var candidate = runtime.Authority.BuildView(state).VisibleNodes.First(value =>
            value.State == ResearchMaturity.Investigable &&
            value.Blockers.Count == 0 && value.MinimumLabs is not null);
        return (galaxy, campaign, candidate.NodeId, candidate.MinimumLabs!.Value);
    }

    object CampaignView(GalaxyState galaxy, AdaptiveResearchCampaignState campaign,
                        int civilizationId, string nodeId)
    {
        var economy = galaxy.Economies.FirstOrDefault(value =>
            value.CivilizationId == civilizationId);
        var state = campaign.Civilizations.TryGetValue(civilizationId, out var found)
            ? found : null;
        var project = state is not null && state.ActiveProjects.TryGetValue(nodeId, out var active)
            ? active : null;
        var funding = campaign.Civilizations.ContainsKey(civilizationId)
            ? campaign.GetProjectFunding(civilizationId).Values.ToArray()
            : Array.Empty<AdaptiveResearchProjectFundingState>();
        return new
        {
            Credits = economy is null ? null : Number(economy.Credits),
            StateRevision = state?.Revision,
            Project = project,
            Funding = funding,
        };
    }

    void AddCommand(string name, string operation, GalaxyState galaxy,
                    AdaptiveResearchCampaignState campaign, int civilizationId,
                    string nodeId, double labs)
    {
        var before = Fingerprint();
        var beforeState = CampaignView(galaxy, campaign, civilizationId, nodeId);
        Func<AdaptiveResearchCommandResult> call = operation switch
        {
            "start" => () => AdaptiveResearchCampaignCommands.StartDirectedResearch(
                galaxy, campaign, civilizationId, nodeId, labs,
                "species:terran_baseline"),
            "pause" => () => AdaptiveResearchCampaignCommands.PauseDirectedResearch(
                campaign, civilizationId, nodeId),
            "resume" => () => AdaptiveResearchCampaignCommands.ResumeDirectedResearch(
                galaxy, campaign, civilizationId, nodeId, labs),
            _ => throw new InvalidOperationException("Unknown fixture operation."),
        };
        AdaptiveResearchCommandResult? result = null;
        Exception? error = null;
        try { result = call(); } catch (Exception caught) { error = caught; }
        var afterState = CampaignView(galaxy, campaign, civilizationId, nodeId);
        var after = Fingerprint();
        rows.Add(new
        {
            Name = name, Kind = "command", Operation = operation,
            CivilizationId = civilizationId, NodeId = nodeId,
            Labs = Number(labs), BeforeFingerprint = before,
            AfterFingerprint = after, Error = Error(error), Result = result,
            Before = beforeState, After = afterState,
        });
    }

    {
        var setup = Setup();
        AddCommand("start-success", "start", setup.Galaxy, setup.Campaign,
            1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations, Array.Empty<CivilizationEconomyState>());
        AddCommand("start-missing-economy", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations, new[]
        {
            new CivilizationEconomyState { CivilizationId = 1, Credits = 500 },
            new CivilizationEconomyState { CivilizationId = 1, Credits = 600 },
        });
        AddCommand("start-duplicate-economy", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations,
            new[] { new CivilizationEconomyState { CivilizationId = 2, Credits = 500 } });
        AddCommand("start-unknown-campaign-civilization", "start", setup.Galaxy,
            setup.Campaign, 2, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        AddCommand("start-unknown-node", "start", setup.Galaxy, setup.Campaign,
            1, "unknown", setup.Labs);
    }
    {
        var setup = Setup();
        AddCommand("start-invalid-labs", "start", setup.Galaxy, setup.Campaign,
            1, setup.NodeId, double.NaN);
    }
    {
        var setup = Setup(0);
        AddCommand("start-insufficient-credits", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        AddCommand("start-authority-rejects-zero-labs", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, 0);
    }
    {
        var setup = Setup();
        var node = catalog.GetNode(setup.NodeId);
        var quote = AdaptiveResearchFundingPolicy.Quote(node, setup.Labs, catalog);
        setup.Galaxy.Economies[0].Credits =
            AdaptiveResearchCampaignCommands.CreditsNeededToStart(quote) - 0.0000005;
        AddCommand("start-affordability-epsilon", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        AddCommand("start-duplicate-funding", "start", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        AddCommand("pause-unknown-civilization", "pause", setup.Galaxy,
            setup.Campaign, 2, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        AddCommand("pause-success", "pause", setup.Galaxy, setup.Campaign,
            1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        AddCommand("resume-not-paused", "resume", setup.Galaxy, setup.Campaign,
            1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        _ = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            setup.Campaign, 1, setup.NodeId);
        AddCommand("resume-success", "resume", setup.Galaxy, setup.Campaign,
            1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        _ = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            setup.Campaign, 1, setup.NodeId);
        setup.Galaxy.Economies[0].Credits = 0;
        AddCommand("resume-insufficient-credits", "resume", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        _ = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            setup.Campaign, 1, setup.NodeId);
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations,
            Array.Empty<CivilizationEconomyState>());
        AddCommand("resume-missing-economy", "resume", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        _ = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            setup.Campaign, 1, setup.NodeId);
        AddCommand("resume-invalid-labs-throws", "resume", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, double.NaN);
    }
    {
        var setup = Setup();
        _ = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId, setup.Labs,
            "species:terran_baseline");
        _ = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            setup.Campaign, 1, setup.NodeId);
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations, new[]
        {
            new CivilizationEconomyState { CivilizationId = 1, Credits = 500 },
            new CivilizationEconomyState { CivilizationId = 1, Credits = 600 },
        });
        AddCommand("resume-duplicate-economy", "resume", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }
    {
        var setup = Setup();
        var state = setup.Campaign.GetCivilization(1);
        var started = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            setup.Galaxy, setup.Campaign, 1, setup.NodeId,
            setup.Labs, "species:terran_baseline");
        if (!started.Accepted) throw new InvalidOperationException(started.Message);
        var setProject = typeof(AdaptiveResearchCivilizationState).GetMethod(
            "SetProject", BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new MissingMethodException("SetProject");
        var pausedForResolution = state.ActiveProjects[setup.NodeId] with
        {
            Paused = true,
            PauseReason = "hypothesis_resolution_required",
        };
        _ = setProject.Invoke(state, new object[] { pausedForResolution });
        setup.Galaxy = Galaxy(setup.Galaxy.Civilizations,
            Array.Empty<CivilizationEconomyState>());
        AddCommand("resume-hypothesis-before-economy", "resume", setup.Galaxy,
            setup.Campaign, 1, setup.NodeId, setup.Labs);
    }

    var fixture = new
    {
        Source = "src/Game/Simulation/Research/Adaptive/AdaptiveResearchFunding.cs",
        CanonicalFingerprint = beforeLoad,
        FinalFingerprint = Fingerprint(),
        RowCount = rows.Count,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)
        ?? throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output, JsonSerializer.Serialize(fixture));
}
catch (Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"researchRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode = 1;
}
