using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Game.Simulation.Construction;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";

try
{
    if (args.Length != 2)
        throw new ArgumentException(
            "Expected canonical research directory and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);

    string Fingerprint()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(root, "*.json")
                     .OrderBy(Path.GetFileName, StringComparer.Ordinal))
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    object Error(Exception? error) => error is null ? null! :
        new { Type = error.GetType().Name, error.Message };

    var beforeLoad = Fingerprint();
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var afterLoad = Fingerprint();
    if (beforeLoad != afterLoad)
        throw new InvalidOperationException("Runtime loading changed canonical inputs.");

    var campaignConstructor = typeof(AdaptiveResearchCampaignState)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
        .Single();
    var addCapability = typeof(AdaptiveResearchCivilizationState).GetMethod(
        "AddCapability", BindingFlags.Instance | BindingFlags.NonPublic)
        ?? throw new MissingMethodException("AddCapability");
    var setNodeState = typeof(AdaptiveResearchCivilizationState).GetMethod(
        "SetNodeState", BindingFlags.Instance | BindingFlags.NonPublic)
        ?? throw new MissingMethodException("SetNodeState");

    AdaptiveResearchCampaignState Campaign(
        string? grantedCapability = null,
        string? capabilityContext = null,
        string? establishedNode = null,
        ResearchMaturity maturity = ResearchMaturity.Rumored,
        string? resolution = null)
    {
        var state = new AdaptiveResearchCivilizationState(
            "civilization:1", "institutional_science");
        if (grantedCapability is not null)
            _ = addCapability.Invoke(state,
                new object?[] { grantedCapability, capabilityContext });
        if (establishedNode is not null)
            _ = setNodeState.Invoke(state, new object[]
            {
                new ResearchNodeRuntimeState(
                    establishedNode, maturity, resolution, 0, 0, 1),
            });
        var states = new Dictionary<int, AdaptiveResearchCivilizationState>
        {
            [1] = state,
        };
        var starts = new Dictionary<int, AdaptiveResearchCivilizationStart>
        {
            [1] = new(1, "terran_baseline", "fixture", "species:terran_baseline"),
        };
        return (AdaptiveResearchCampaignState)campaignConstructor.Invoke(
            new object[] { runtime, states, starts });
    }

    object StateView(AdaptiveResearchCampaignState campaign)
    {
        var state = campaign.GetCivilization(1);
        return new
        {
            Revision = state.Revision,
            Capabilities = state.Capabilities.Select(value => new
            {
                value.CapabilityId,
                value.ContextId,
            }).ToArray(),
            Nodes = state.NodeStates.Values.Select(value => new
            {
                value.NodeId,
                value.Maturity,
                value.Resolution,
                value.Revision,
            }).ToArray(),
        };
    }

    var rows = new List<object>();
    void AddConstruction(
        string name, string capabilityId,
        string? grantedCapability = null,
        string? capabilityContext = null,
        string? establishedNode = null,
        ResearchMaturity maturity = ResearchMaturity.Rumored,
        string? resolution = null,
        int civilizationId = 1)
    {
        var campaign = Campaign(grantedCapability, capabilityContext,
            establishedNode, maturity, resolution);
        var view = new AdaptiveResearchConstructionCapabilityView(campaign);
        var ownedCapabilityId = capabilityId;
        var beforeState = StateView(campaign);
        var before = Fingerprint();
        bool? result = null;
        Exception? error = null;
        try
        {
            result = view.HasCivilizationCapability(
                null!, civilizationId, ownedCapabilityId);
        }
        catch (Exception caught)
        {
            error = caught;
        }
        var after = Fingerprint();
        var afterState = StateView(campaign);
        rows.Add(new
        {
            Name = name,
            Kind = "construction",
            CivilizationId = civilizationId,
            CapabilityId = ownedCapabilityId,
            GrantedCapability = grantedCapability,
            CapabilityContext = capabilityContext,
            EstablishedNode = establishedNode,
            Maturity = maturity,
            Resolution = resolution,
            GalaxyWasNull = true,
            BeforeFingerprint = before,
            AfterFingerprint = after,
            BeforeState = beforeState,
            AfterState = afterState,
            Error = Error(error),
            Result = result,
        });
    }

    void AddShipbuilding(
        string name, string capabilityId,
        string? grantedCapability = null,
        string? capabilityContext = null,
        string? establishedNode = null,
        ResearchMaturity maturity = ResearchMaturity.Rumored,
        string? resolution = null,
        int civilizationId = 1)
    {
        var campaign = Campaign(grantedCapability, capabilityContext,
            establishedNode, maturity, resolution);
        var view = new AdaptiveResearchShipbuildingCapabilityView(campaign);
        var ownedCapabilityId = capabilityId;
        var beforeState = StateView(campaign);
        var before = Fingerprint();
        bool? result = null;
        Exception? error = null;
        try
        {
            result = view.HasCivilizationCapability(
                null!, civilizationId, ownedCapabilityId);
        }
        catch (Exception caught)
        {
            error = caught;
        }
        var after = Fingerprint();
        var afterState = StateView(campaign);
        rows.Add(new
        {
            Name = name,
            Kind = "shipbuilding",
            CivilizationId = civilizationId,
            CapabilityId = ownedCapabilityId,
            GrantedCapability = grantedCapability,
            CapabilityContext = capabilityContext,
            EstablishedNode = establishedNode,
            Maturity = maturity,
            Resolution = resolution,
            GalaxyWasNull = true,
            BeforeFingerprint = before,
            AfterFingerprint = after,
            BeforeState = beforeState,
            AfterState = afterState,
            Error = Error(error),
            Result = result,
        });
    }

    AddConstruction("construction-orbital-direct", "orbital_industry",
        grantedCapability: "orbital_industry");
    AddConstruction("construction-orbital-context-does-not-match",
        "orbital_industry", "orbital_industry", "species:test");
    AddConstruction("construction-orbital-mature-knowledge", "orbital_industry",
        establishedNode: "orbital_manufacturing", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-warp-knowledge", "warp_field_control",
        establishedNode: "warp_field_control", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-warp-flag-is-not-knowledge",
        "warp_field_control", grantedCapability: "warp_field_control");
    AddConstruction("construction-fusion-knowledge", "fusion_power",
        establishedNode: "fusion_power", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-fusion-flag-is-not-knowledge", "fusion_power",
        grantedCapability: "fusion_power");
    AddConstruction("construction-additive-knowledge", "additive_manufacturing",
        establishedNode: "additive_manufacturing", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-additive-flag-is-not-knowledge",
        "additive_manufacturing", grantedCapability: "additive_manufacturing");
    AddConstruction("construction-trade-knowledge",
        "interplanetary_trade_standards",
        establishedNode: "interplanetary_trade_standards",
        maturity: ResearchMaturity.Mature);
    AddConstruction("construction-trade-flag-is-not-knowledge",
        "interplanetary_trade_standards",
        grantedCapability: "interplanetary_trade_standards");
    AddConstruction("construction-recycling-knowledge", "closed_loop_recycling",
        establishedNode: "closed_loop_recycling", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-recycling-flag-is-not-knowledge",
        "closed_loop_recycling", grantedCapability: "closed_loop_recycling");
    AddConstruction("construction-archived-mature-history", "warp_field_control",
        establishedNode: "warp_field_control", maturity: ResearchMaturity.Archived,
        resolution: "MATURE_HISTORY");
    AddConstruction("construction-archived-superseded", "fusion_power",
        establishedNode: "fusion_power", maturity: ResearchMaturity.Archived,
        resolution: "Superseded");
    AddConstruction("construction-archived-other", "additive_manufacturing",
        establishedNode: "additive_manufacturing", maturity: ResearchMaturity.Archived,
        resolution: "abandoned");
    AddConstruction("construction-fallback-direct", "fixture_capability",
        grantedCapability: "fixture_capability");
    AddConstruction("construction-fallback-absent", "fixture_capability");
    AddConstruction("construction-case-sensitive", "FUSION_POWER",
        establishedNode: "fusion_power", maturity: ResearchMaturity.Mature);
    AddConstruction("construction-empty", "");
    AddConstruction("construction-missing-civilization", "fusion_power",
        civilizationId: 9);

    AddShipbuilding("ship-spacecraft-direct",
        ShipbuildingCapabilityIds.SpacecraftConstruction,
        grantedCapability: ShipbuildingCapabilityIds.SpacecraftConstruction);
    AddShipbuilding("ship-spacecraft-orbital-capability",
        ShipbuildingCapabilityIds.SpacecraftConstruction,
        grantedCapability: "orbital_industry");
    AddShipbuilding("ship-spacecraft-orbital-knowledge",
        ShipbuildingCapabilityIds.SpacecraftConstruction,
        establishedNode: "orbital_manufacturing", maturity: ResearchMaturity.Mature);
    AddShipbuilding("ship-experimental-direct",
        ShipbuildingCapabilityIds.ExperimentalInterstellarTransit,
        grantedCapability: ShipbuildingCapabilityIds.ExperimentalInterstellarTransit);
    AddShipbuilding("ship-reliable-fallback",
        ShipbuildingCapabilityIds.ReliableInterstellarTransit,
        grantedCapability: ShipbuildingCapabilityIds.ReliableInterstellarTransit);
    AddShipbuilding("ship-extended-fallback",
        ShipbuildingCapabilityIds.ExtendedInterstellarTransit,
        grantedCapability: ShipbuildingCapabilityIds.ExtendedInterstellarTransit);
    AddShipbuilding("ship-custom-fallback", "fixture_ship_capability",
        grantedCapability: "fixture_ship_capability");
    AddShipbuilding("ship-context-does-not-match", "fixture_ship_capability",
        grantedCapability: "fixture_ship_capability",
        capabilityContext: "species:test");
    AddShipbuilding("ship-absent", "fixture_ship_capability");
    AddShipbuilding("ship-case-sensitive", "SPACECRAFT_CONSTRUCTION",
        grantedCapability: ShipbuildingCapabilityIds.SpacecraftConstruction);
    AddShipbuilding("ship-empty", "");
    AddShipbuilding("ship-missing-civilization",
        ShipbuildingCapabilityIds.SpacecraftConstruction, civilizationId: 9);

    var fixture = new
    {
        Source = new[]
        {
            "src/Game/Simulation/Construction/ConstructionCapabilities.cs",
            "src/Game/Simulation/Shipbuilding/ShipbuildingCapabilities.cs",
        },
        CanonicalFingerprint = beforeLoad,
        FinalFingerprint = Fingerprint(),
        RowCount = rows.Count,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)
        ?? throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output, JsonSerializer.Serialize(fixture,
        new JsonSerializerOptions { WriteIndented = true }));
    Console.WriteLine($"adaptive research capability adapter oracle: " +
        $"{rows.Count} rows -> {output}");
}
catch (Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"researchRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode = 1;
}
