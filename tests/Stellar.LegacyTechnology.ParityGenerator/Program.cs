using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");

var options = new JsonSerializerOptions
{
    WriteIndented = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();
JsonElement Clone<T>(T value) => JsonSerializer.SerializeToElement(value, options);

void Add(string name, string kind, object arguments, Func<object?> operation)
{
    object? result = null;
    object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Result = result, Error = error });
}

foreach (var definition in TechnologyRegistry.All)
    Add("get-" + definition.Id, "Get", new { Id = definition.Id }, () => TechnologyRegistry.Get(definition.Id));
foreach (var id in new[] { "missing", "Orbital_Industry", "", " orbital_industry", "orbital_industry " })
    Add("get-invalid-" + cases.Count, "Get", new { Id = id }, () => TechnologyRegistry.Get(id));

TechnologyState State(IEnumerable<string>? completed = null, string? active = null, double progress = 0)
{
    var state = new TechnologyState { CivilizationId = 7, ActiveResearchId = active, ActiveResearchProgress = progress };
    if (completed is not null)
        foreach (var id in completed) state.CompletedTechnologyIds.Add(id);
    return state;
}
ConstructionState Construction(IEnumerable<string>? completed = null)
{
    var state = new ConstructionState
    {
        CivilizationId = 91,
        ActiveProjectId = "unrelated_active",
        ActiveProjectProgress = 12.5,
        ActiveProjectAuthorizationCredits = 33
    };
    if (completed is not null)
        foreach (var id in completed) state.CompletedProjectIds.Add(id);
    state.QueuedProjects.Add(new("unrelated_queue", 4));
    return state;
}
void Available(string name, TechnologyState state, ConstructionState construction)
{
    var beforeState = Clone(state);
    var beforeConstruction = Clone(construction);
    object? result = null;
    object? error = null;
    try { result = TechnologyRegistry.GetAvailable(state, construction); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new
    {
        Name = name,
        Kind = "Available",
        Arguments = new { State = beforeState, Construction = beforeConstruction },
        BeforeState = beforeState,
        BeforeConstruction = beforeConstruction,
        Result = result,
        Error = error,
        AfterState = Clone(state),
        AfterConstruction = Clone(construction)
    });
}

Available("available-empty", State(), Construction());
Available("available-orbital-project", State(), Construction(new[] { "orbital_launch_complex" }));
Available("available-fusion-complete", State(new[] { "fusion_propulsion" }), Construction());
Available("available-exotic-prerequisites", State(new[] { "fusion_propulsion", "deep_space_sensors" }), Construction());
Available("available-warp-prerequisites-direct", State(new[] { "orbital_industry", "exotic_field_theory" }), Construction());
Available("available-prototype-tech-only", State(new[] { "warp_field_control" }), Construction());
Available("available-prototype-project-only", State(), Construction(new[] { "warp_test_facility" }));
Available("available-prototype-unlocked", State(new[] { "warp_field_control" }), Construction(new[] { "warp_test_facility" }));
Available("available-all-complete", State(TechnologyRegistry.All.Select(item => item.Id)), Construction(new[] { "orbital_launch_complex", "warp_test_facility" }));
Available("available-unrelated-ids", State(new[] { "future_tech", "FUSION_PROPULSION" }), Construction(new[] { "future_project", "ORBITAL_LAUNCH_COMPLEX" }));
Available("available-duplicate-insertion", State(new[] { "fusion_propulsion", "future", "fusion_propulsion", "deep_space_sensors", "future" }), Construction());
Available("available-active-progress-ignored", State(new[] { "fusion_propulsion" }, "deep_space_sensors", 789.25), Construction(new[] { "orbital_launch_complex" }));
Available("available-nonfinite-progress-ignored", State(active: "missing_active", progress: double.NaN), Construction());

CivilizationState Civilization(int id, CivilizationDevelopmentStage stage, string name)
{
    var leadership = new CivilizationLeadershipState();
    leadership.Assign("Science", new CivilizationCharacter(
        "leader-" + id, "Leader " + id, "voice-" + id, "portrait-" + id));
    return new CivilizationState(
        id, name, id + 100, CivilizationArchetype.Adaptive,
        new Game.Simulation.AI.CivilizationTraits(.1, .2, .3, .4, .5, .6, false),
        false, stage, IsSeededAncient: stage == CivilizationDevelopmentStage.AncientSpacefaring,
        ExpansionAllowed: id % 2 == 0, NeutralUnlessProvoked: id % 3 == 0, SpeciesId: "species_" + id)
    { Leadership = leadership };
}
void Seed(string name, IList<CivilizationState> civilizations)
{
    var before = Clone(civilizations);
    object? result = null;
    object? error = null;
    try { result = new TechnologySeeder().Seed(civilizations); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new
    {
        Name = name,
        Kind = "Seed",
        Arguments = new { Civilizations = before },
        BeforeCivilizations = before,
        Result = result,
        Error = error,
        AfterCivilizations = Clone(civilizations)
    });
}

Seed("seed-empty", new List<CivilizationState>());
Seed("seed-prewarp", new List<CivilizationState> { Civilization(1, CivilizationDevelopmentStage.PreWarp, "Pre") });
Seed("seed-warp-capable", new List<CivilizationState> { Civilization(2, CivilizationDevelopmentStage.WarpCapable, "Warp") });
Seed("seed-ancient", new List<CivilizationState> { Civilization(3, CivilizationDevelopmentStage.AncientSpacefaring, "Ancient") });
Seed("seed-stage-order-and-duplicate-ids", new List<CivilizationState> {
    Civilization(8, CivilizationDevelopmentStage.AncientSpacefaring, "Ancient first"),
    Civilization(4, CivilizationDevelopmentStage.PreWarp, "Pre second"),
    Civilization(8, CivilizationDevelopmentStage.WarpCapable, "Duplicate ID third"),
    Civilization(-2, CivilizationDevelopmentStage.AncientSpacefaring, "Ancient fourth"),
});

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-legacy-technology-oracle-v1",
    Catalog = TechnologyRegistry.All,
    Cases = cases,
}, options) + Environment.NewLine);
