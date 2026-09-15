using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions
{
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
var cases = new List<object>();

CivilizationState Civ(int id = 1, bool player = true,
    CivilizationDevelopmentStage stage = CivilizationDevelopmentStage.PreWarp,
    CivilizationTraits? traits = null, string? name = null) =>
    new(id, name ?? $"Civilization {id}", 0, CivilizationArchetype.Adaptive,
        traits ?? CivilizationTraits.Balanced, player, stage);

GalaxyState World(params CivilizationState[] civilizations)
{
    var values = civilizations.Length == 0 ? new[] { Civ() } : civilizations;
    return new GalaxyState
    {
        Seed = 27,
        Systems = [],
        PlanetaryBodies = [],
        Civilizations = values.ToList(),
        Fleets = [],
        Colonies = [],
        Economies = values.Select(c => new CivilizationEconomyState
            { CivilizationId = c.Id }).ToList(),
        Technologies = values.Select(c => new TechnologyState
            { CivilizationId = c.Id }).ToList(),
        ConstructionStates = values.Select(c => new ConstructionState
            { CivilizationId = c.Id }).ToList(),
        ShipyardStates = [],
        PlayerCivilizationId = values[0].Id,
        Knowledge = new CivilizationKnowledgeState(),
    };
}

object Observe(GalaxyState galaxy) => new
{
    Civilizations = galaxy.Civilizations.Select(c => new
    {
        c.Id,
        c.Name,
        c.HomeSystemId,
        c.Archetype,
        Traits = c.Traits,
        c.IsPlayer,
        c.DevelopmentStage,
        c.IsSeededAncient,
        c.ExpansionAllowed,
        c.NeutralUnlessProvoked,
        c.SpeciesId,
        Leadership = c.Leadership.Offices.Select(entry => new
        {
            Office = entry.Key,
            Character = new
            {
                entry.Value.Id,
                entry.Value.DisplayName,
                entry.Value.VoiceProfileId,
                entry.Value.Portrait,
            },
        }).ToArray(),
    }).ToArray(),
    Technologies = galaxy.Technologies.Select(t => new
    {
        t.CivilizationId,
        CompletedTechnologyIds = t.CompletedTechnologyIds.ToArray(),
        t.ActiveResearchId,
        t.ActiveResearchProgress,
    }).ToArray(),
    Construction = galaxy.ConstructionStates.Select(c => new
    {
        c.CivilizationId,
        CompletedProjectIds = c.CompletedProjectIds.ToArray(),
        c.ActiveProjectId,
        c.ActiveProjectProgress,
        c.ActiveProjectAuthorizationCredits,
        QueuedProjects = c.QueuedProjects.ToArray(),
    }).ToArray(),
    Economies = galaxy.Economies.Select(e => new
    {
        e.CivilizationId,
        e.Credits,
        e.Industry,
        e.Science,
        e.LastCreditsPerSecond,
        e.LastIndustryPerSecond,
        e.LastSciencePerSecond,
        e.LastResearchSpendingPerDay,
        e.LastResearchFundingFraction,
        e.OperatingArrears,
        e.LastBaseOperationsFundingFraction,
        e.IndustryPriority,
    }).ToArray(),
};

void AddStart(string name, int civilizationId, string? technologyId,
    Action<GalaxyState> arrange)
{
    var galaxy = World();
    arrange(galaxy);
    var before = Freeze(Observe(galaxy));
    var arguments = Freeze(new { CivilizationId = civilizationId,
        TechnologyId = technologyId, World = before });
    ResearchOrderResult? outcome = null;
    object? error = null;
    try
    {
        outcome = new ResearchSimulation().StartResearch(
            galaxy, civilizationId, technologyId!);
    }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
    }
    var result = outcome is null ? (JsonElement?)null : Freeze(outcome);
    var after = Freeze(Observe(galaxy));
    cases.Add(new { Name = name, Kind = "StartResearch", Arguments = arguments,
        Result = result, Error = error, Before = before, After = after });
}

void AddAdvance(string name, string kind, int? civilizationId,
    Action<GalaxyState> arrange, Func<GalaxyState>? create = null)
{
    var galaxy = create?.Invoke() ?? World();
    arrange(galaxy);
    var before = Freeze(Observe(galaxy));
    var arguments = Freeze(new { CivilizationId = civilizationId, World = before });
    IReadOnlyList<ResearchEvent>? outcome = null;
    object? error = null;
    try
    {
        var simulation = new ResearchSimulation();
        outcome = kind == "Advance" ? simulation.Advance(galaxy) :
            simulation.AdvanceForCivilization(galaxy, civilizationId!.Value);
    }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
    }
    var result = outcome is null ? (JsonElement?)null : Freeze(outcome);
    var after = Freeze(Observe(galaxy));
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments,
        Result = result, Error = error, Before = before, After = after });
}

void Complete(TechnologyState state, params string[] ids)
{
    foreach (var id in ids) state.CompletedTechnologyIds.Add(id);
}

AddStart("start-unknown-civilization", 99, "fusion_propulsion", _ => { });
AddStart("start-ancient-without-state", 1, "fusion_propulsion", g =>
{
    g.Civilizations[0] = Civ(stage: CivilizationDevelopmentStage.AncientSpacefaring);
    g.Technologies.Clear(); g.ConstructionStates.Clear();
});
AddStart("start-missing-technology-state", 1, "fusion_propulsion", g => g.Technologies.Clear());
AddStart("start-active-skips-construction", 1, "fusion_propulsion", g =>
{
    g.Technologies[0].ActiveResearchId = "deep_space_sensors";
    g.ConstructionStates.Clear();
});
AddStart("start-missing-construction", 1, "fusion_propulsion", g => g.ConstructionStates.Clear());
AddStart("start-fusion", 1, "fusion_propulsion", g => g.Technologies[0].ActiveResearchProgress = 55);
AddStart("start-sensors", 1, "deep_space_sensors", _ => { });
AddStart("start-orbital-without-project", 1, "orbital_industry", _ => { });
AddStart("start-orbital-with-project", 1, "orbital_industry", g => g.ConstructionStates[0].CompletedProjectIds.Add("orbital_launch_complex"));
AddStart("start-exotic-missing-prerequisite", 1, "exotic_field_theory", g => Complete(g.Technologies[0], "fusion_propulsion"));
AddStart("start-exotic-available", 1, "exotic_field_theory", g => Complete(g.Technologies[0], "fusion_propulsion", "deep_space_sensors"));
AddStart("start-warp-control-available", 1, "warp_field_control", g => Complete(g.Technologies[0], "orbital_industry", "fusion_propulsion", "deep_space_sensors", "exotic_field_theory"));
AddStart("start-prototype-missing-project", 1, "prototype_warp_drive", g => Complete(g.Technologies[0], "warp_field_control"));
AddStart("start-prototype-available", 1, "prototype_warp_drive", g => { Complete(g.Technologies[0], "warp_field_control"); g.ConstructionStates[0].CompletedProjectIds.Add("warp_test_facility"); });
AddStart("start-completed", 1, "fusion_propulsion", g => Complete(g.Technologies[0], "fusion_propulsion"));
AddStart("start-case-sensitive", 1, "Fusion_Propulsion", _ => { });
AddStart("start-empty", 1, "", _ => { });
AddStart("start-null", 1, null, _ => { });
AddStart("start-no-economy-needed", 1, "deep_space_sensors", g => ((List<CivilizationEconomyState>)g.Economies).Clear());
AddStart("start-duplicate-civilization-first-ancient", 1, "fusion_propulsion", g => g.Civilizations.Insert(0, Civ(stage: CivilizationDevelopmentStage.AncientSpacefaring, name: "First")));
AddStart("start-duplicate-technology-first-active", 1, "fusion_propulsion", g => { g.Technologies.Insert(0, new TechnologyState { CivilizationId=1, ActiveResearchId="deep_space_sensors" }); });
AddStart("start-duplicate-construction-first", 1, "orbital_industry", g =>
{
    g.ConstructionStates.Add(new ConstructionState { CivilizationId=1 });
    g.ConstructionStates[1].CompletedProjectIds.Add("orbital_launch_complex");
});

AddAdvance("advance-empty-civilizations", "Advance", null, g => g.Civilizations.Clear());
AddAdvance("advance-player-idle-lookups", "Advance", null, _ => { });
AddAdvance("advance-player-idle-missing-construction", "Advance", null, g => g.ConstructionStates.Clear());
AddAdvance("advance-player-idle-missing-economy", "Advance", null, g => ((List<CivilizationEconomyState>)g.Economies).Clear());
AddAdvance("advance-missing-technology", "Advance", null, g => g.Technologies.Clear());
AddAdvance("advance-ancient-skips-all", "Advance", null, g => { g.Civilizations[0]=Civ(stage:CivilizationDevelopmentStage.AncientSpacefaring); g.Technologies.Clear(); g.ConstructionStates.Clear(); ((List<CivilizationEconomyState>)g.Economies).Clear(); });
AddAdvance("selected-absent", "AdvanceForCivilization", 99, g => { g.Technologies.Clear(); g.ConstructionStates.Clear(); ((List<CivilizationEconomyState>)g.Economies).Clear(); });
AddAdvance("selected-other-skips-broken", "AdvanceForCivilization", 2, _ => { }, () => World(Civ(1), Civ(2)));

foreach (var science in new[] { -5.0, 0.0, 50.0, 1100.0, 2000.0,
                                double.NaN, double.PositiveInfinity,
                                double.NegativeInfinity })
    AddAdvance($"science-{Freeze(science)}", "Advance", null, g =>
    {
        g.Technologies[0].ActiveResearchId = "deep_space_sensors";
        g.Economies[0].Science = science;
    });
foreach (var progress in new[] { 0.0, 1099.0, 1099.9998, 1099.9999,
                                 1100.0, 1200.0, double.NaN,
                                 double.PositiveInfinity, double.NegativeInfinity })
    AddAdvance($"progress-{Freeze(progress)}", "Advance", null, g =>
    {
        g.Technologies[0].ActiveResearchId = "deep_space_sensors";
        g.Technologies[0].ActiveResearchProgress = progress;
        g.Economies[0].Science = 0;
    });

AddAdvance("unknown-active-id", "Advance", null, g => g.Technologies[0].ActiveResearchId = "missing");
AddAdvance("completion-existing-id", "Advance", null, g => { Complete(g.Technologies[0], "deep_space_sensors"); g.Technologies[0].ActiveResearchId="deep_space_sensors"; g.Economies[0].Science=1100; });
AddAdvance("duplicate-economy-first", "Advance", null, g =>
{
    g.Technologies[0].ActiveResearchId="deep_space_sensors";
    g.Economies[0].Science=50;
    ((List<CivilizationEconomyState>)g.Economies).Add(
        new CivilizationEconomyState { CivilizationId=1, Science=1100 });
});
AddAdvance("prototype-prewarp", "Advance", null, g =>
{
    Complete(g.Technologies[0], "warp_field_control");
    g.Technologies[0].ActiveResearchId="prototype_warp_drive";
    g.Economies[0].Science=4800;
    g.Civilizations[0].Leadership.Assign("ChiefScientist",
        new CivilizationCharacter("scientist-1", "Ada Researcher", "voice-a", "portrait-a"));
    g.Civilizations[0].Leadership.Assign("Governor",
        new CivilizationCharacter("governor-1", "Grace Governor"));
});
AddAdvance("prototype-already-warp", "Advance", null, g => { g.Civilizations[0]=Civ(stage:CivilizationDevelopmentStage.WarpCapable); g.Technologies[0].ActiveResearchId="prototype_warp_drive"; g.Economies[0].Science=4800; });

foreach (var traits in new[]
{
    CivilizationTraits.Balanced,
    new CivilizationTraits(1, 0, 0, 0, 1, 1),
    new CivilizationTraits(0, 1, 1, 0, 0, 1),
    new CivilizationTraits(0, 0, 0, 1, 0, 1),
    new CivilizationTraits(double.NaN, 0, 0, 0, 0, 1),
    new CivilizationTraits(double.NaN, double.NaN, double.NaN,
        double.NaN, double.NaN, 1),
    new CivilizationTraits(1, 0, 0, 4.0 / 11.0, 0, 1),
})
    AddAdvance($"ai-choice-{cases.Count}", "Advance", null, g =>
    {
        g.Civilizations[0] = Civ(player:false, traits:traits);
        g.ConstructionStates[0].CompletedProjectIds.Add("orbital_launch_complex");
        g.Economies[0].Science = 1;
    });
AddAdvance("ai-no-available", "Advance", null, g => { g.Civilizations[0]=Civ(player:false); foreach(var d in TechnologyRegistry.All) Complete(g.Technologies[0], d.Id); g.Economies[0].Science=100; });
AddAdvance("ordered-two-completions", "Advance", null, _ => { }, () =>
{
    var g=World(Civ(2,name:"Two"),Civ(1,name:"One"));
    foreach(var i in new[]{0,1}) { g.Technologies[i].ActiveResearchId="deep_space_sensors"; g.Economies[i].Science=1100; }
    return g;
});
AddAdvance("selected-second-only", "AdvanceForCivilization", 2, _ => { }, () =>
{
    var g=World(Civ(1),Civ(2));
    foreach(var i in new[]{0,1}) { g.Technologies[i].ActiveResearchId="deep_space_sensors"; g.Economies[i].Science=1100; }
    return g;
});
AddAdvance("duplicate-snapshot-visits", "Advance", null, _ => { }, () =>
{
    var g=World(Civ(1,name:"First"),Civ(1,name:"Second"));
    g.Technologies[0].ActiveResearchId="deep_space_sensors";
    g.Economies[0].Science=1200;
    return g;
});
AddAdvance("duplicate-prewarp-replacement-first", "Advance", null, _ => { }, () =>
{
    var g=World(Civ(1,name:"First"),Civ(1,name:"Second"));
    g.Technologies[0].ActiveResearchId="prototype_warp_drive";
    g.Economies[0].Science=4800;
    return g;
});
AddAdvance("prior-completion-before-error", "Advance", null, _ => { }, () =>
{
    var g=World(Civ(1),Civ(2));
    g.Technologies[0].ActiveResearchId="deep_space_sensors";
    g.Economies[0].Science=1100;
    g.Technologies.RemoveAt(1);
    return g;
});

var sourceNull = new List<object>();
foreach (var command in new[] { "Advance", "AdvanceForCivilization", "StartResearch" })
{
    object? error = null;
    try
    {
        var simulation = new ResearchSimulation();
        if (command == "Advance") _ = simulation.Advance(null!);
        else if (command == "AdvanceForCivilization") _ = simulation.AdvanceForCivilization(null!, 1);
        else _ = simulation.StartResearch(null!, 1, "fusion_propulsion");
    }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
    }
    sourceNull.Add(new { Command = command, Error = error });
}

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-legacy-research-oracle-v1",
    Cases = cases,
    SourceOnlyNullGalaxy = sourceNull,
    NativeBoundary = "Typed native spans cannot represent a null galaxy.",
}, options) + Environment.NewLine);
