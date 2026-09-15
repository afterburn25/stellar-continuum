using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 2)
        throw new ArgumentException("Expected source root and fixture path.");
    var sourceRoot = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var sourcePath = Path.Combine(sourceRoot, "src/Game/Persistence/CampaignSaveService.cs");
    string SourceHash() => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(sourcePath)));
    var sourceBefore = SourceHash();
    var options = new JsonSerializerOptions
    {
        IncludeFields = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze<T>(T value) => JsonSerializer.SerializeToElement(value, options);
    var service = typeof(CampaignSaveService);
    MethodInfo Method(string name) => service.GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)
        ?? throw new MissingMethodException(service.FullName, name);
    var restoreColonies = Method("ToColonies");
    var captureColonies = Method("ToColonyDtos");
    var restoreEconomies = Method("ToEconomies");
    var captureEconomies = Method("ToEconomyDtos");
    var restoreTechnologies = Method("ToTechnologies");
    var captureTechnologies = Method("ToTechnologyDtos");
    var restoreConstruction = Method("ToConstructionStates");
    var captureConstruction = Method("ToConstructionDtos");
    object? Invoke(MethodInfo method, params object?[] values)
    {
        try { return method.Invoke(null, values); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { throw error.InnerException; }
    }
    object? Failure(Action action)
    {
        try { action(); return null; }
        catch (Exception error) { return new { Type = error.GetType().Name, error.Message }; }
    }
    CivilizationState Civ(int id, string species = SpeciesCatalog.TerranBaselineId) => new(
        id, $"C{id}", id, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced,
        true, CivilizationDevelopmentStage.WarpCapable, false, SpeciesId: species);
    var civilizations = new List<CivilizationState>
    {
        Civ(1), Civ(2, SpeciesCatalog.PelagicHighPressureId),
    };
    SurfaceBuildingState Building() => new()
    {
        Id = 9, TypeId = "science_lab", X = 1.25f, Z = -2.5f,
        RotationDegrees = 33.5f, IndustryProgress = 123.25, IsComplete = false,
        IsEnabled = false, PendingUpgradeTypeId = "advanced_science_lab",
        UpgradeDaysRemaining = 4.5, OperatingPriority = 7, Condition = .75,
        StoredPowerDays = 2.25,
    };
    ColonySaveDto ColonyDto(int id = 10) => new()
    {
        Id = id, CivilizationId = 1, SystemId = 3, PlanetaryBodyId = 31,
        Name = $"Colony {id}", Kind = SettlementKind.ResourceOutpost,
        PopulationSpeciesId = SpeciesCatalog.TerranBaselineId,
        PopulationMillions = 12.5, Infrastructure = .8, Stability = .7,
        StoredFoodPopulationDaysMillions = 44, StoredWaterPopulationDaysMillions = 22,
        StoredExtractedMaterials = 333, RemainingExtractableMaterials = 444,
        SurfaceHubLevel = 2, SurfaceHubUpgradeDaysRemaining = 5.5,
        SurfaceBuildings = new() { Building() },
    };
    ColonyState ColonyState(int id = 20) => new()
    {
        Id = id, CivilizationId = 1, SystemId = 4, PlanetaryBodyId = 41,
        Name = $"State {id}", Kind = SettlementKind.Colony,
        PopulationSpeciesId = SpeciesCatalog.TerranBaselineId,
        PopulationMillions = 22, Infrastructure = .9, Stability = .6,
        StoredFoodPopulationDaysMillions = 12, StoredWaterPopulationDaysMillions = 5,
        StoredExtractedMaterials = 50, RemainingExtractableMaterials = 60,
        SurfaceHubLevel = 3, SurfaceHubUpgradeDaysRemaining = 6,
        SurfaceBuildings = { Building() },
    };
    EconomySaveDto EconomyDto() => new()
    {
        CivilizationId = 1, Credits = 10, Industry = 20, Science = 30,
        LastCreditsPerSecond = 1.25, LastIndustryPerSecond = 2.5,
        LastSciencePerSecond = 3.75, LastResearchSpendingPerDay = 4.5,
        LastResearchFundingFraction = .6, OperatingArrears = 7.5,
        LastBaseOperationsFundingFraction = .8,
        IndustryPriority = IndustryPriority.InfrastructureFirst,
    };
    CivilizationEconomyState EconomyState() => new()
    {
        CivilizationId = 2, Credits = 11, Industry = 21, Science = 31,
        LastCreditsPerSecond = 1.5, LastIndustryPerSecond = 2.75,
        LastSciencePerSecond = 4, LastResearchSpendingPerDay = 5,
        LastResearchFundingFraction = .65, OperatingArrears = 8,
        LastBaseOperationsFundingFraction = .85,
        IndustryPriority = IndustryPriority.ShipbuildingFirst,
    };
    TechnologySaveDto TechnologyDto() => new()
    {
        CivilizationId = 1,
        CompletedTechnologyIds = new() { "beta", "alpha", "beta" },
        ActiveResearchId = "fusion_power", ActiveResearchProgress = 12.25,
    };
    TechnologyState TechnologyState()
    {
        var state = new TechnologyState
        {
            CivilizationId = 2, ActiveResearchId = "warp_field_control",
            ActiveResearchProgress = 13.5,
        };
        state.CompletedTechnologyIds.Add("\uE000");
        state.CompletedTechnologyIds.Add("\U00010000");
        state.CompletedTechnologyIds.Add("alpha");
        return state;
    }
    ConstructionSaveDto ConstructionDto() => new()
    {
        CivilizationId = 1,
        CompletedProjectIds = new() { "research_network" },
        ActiveProjectId = "industrial_automation", ActiveProjectProgress = 100,
        ActiveProjectAuthorizationCredits = 20,
        QueuedProjects = new()
        {
            new() { ProjectId = "orbital_launch_complex", AuthorizationCredits = 30 },
            new() { ProjectId = "warp_test_facility", AuthorizationCredits = 40 },
        },
    };
    ConstructionState ConstructionState()
    {
        var state = new ConstructionState
        {
            CivilizationId = 2, ActiveProjectId = "industrial_automation",
            ActiveProjectProgress = 101, ActiveProjectAuthorizationCredits = 22,
        };
        state.CompletedProjectIds.Add("\uE000");
        state.CompletedProjectIds.Add("\U00010000");
        state.CompletedProjectIds.Add("research_network");
        state.QueuedProjects.Add(new("orbital_launch_complex", 31));
        state.QueuedProjects.Add(new("warp_test_facility", 41));
        return state;
    }

    var rows = new List<object>();
    void ColonyRestore(string name, Action<ColonySaveDto> edit, int version = 16,
        List<CivilizationState>? civs = null, int count = 1, int editIndex = 0)
    {
        var input = Enumerable.Range(0, count).Select(index => ColonyDto(10 + index)).ToList();
        edit(input[editIndex]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<ColonyState>)
            Invoke(restoreColonies, input, civs ?? civilizations, version)!));
        rows.Add(new { Name = name, Operation = "RestoreColonies", Version = version,
            Civilizations = Freeze(civs ?? civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void ColonyCapture(string name, Action<ColonyState> edit)
    {
        var input = new List<ColonyState> { ColonyState() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<ColonySaveDto>)
            Invoke(captureColonies, input)!));
        rows.Add(new { Name = name, Operation = "CaptureColonies", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void EconomyRestore(string name, Action<EconomySaveDto> edit)
    {
        var input = new List<EconomySaveDto> { EconomyDto() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<CivilizationEconomyState>)
            Invoke(restoreEconomies, input)!));
        rows.Add(new { Name = name, Operation = "RestoreEconomies", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void EconomyCapture(string name, Action<CivilizationEconomyState> edit)
    {
        var input = new List<CivilizationEconomyState> { EconomyState() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<EconomySaveDto>)
            Invoke(captureEconomies, input)!));
        rows.Add(new { Name = name, Operation = "CaptureEconomies", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void TechnologyRestore(string name, Action<TechnologySaveDto> edit)
    {
        var input = new List<TechnologySaveDto> { TechnologyDto() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IList<TechnologyState>)
            Invoke(restoreTechnologies, input)!));
        rows.Add(new { Name = name, Operation = "RestoreTechnologies", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void TechnologyCapture(string name, Action<TechnologyState> edit)
    {
        var input = new List<TechnologyState> { TechnologyState() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<TechnologySaveDto>)
            Invoke(captureTechnologies, input)!));
        rows.Add(new { Name = name, Operation = "CaptureTechnologies", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void ConstructionRestore(string name, Action<ConstructionSaveDto> edit,
        int count = 1, int editIndex = 0)
    {
        var input = Enumerable.Range(0, count).Select(index =>
        {
            var dto = ConstructionDto();
            dto.CivilizationId += index;
            return dto;
        }).ToList();
        edit(input[editIndex]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IList<ConstructionState>)
            Invoke(restoreConstruction, input)!));
        rows.Add(new { Name = name, Operation = "RestoreConstruction", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }
    void ConstructionCapture(string name, Action<ConstructionState> edit)
    {
        var input = new List<ConstructionState> { ConstructionState() }; edit(input[0]);
        var before = Freeze(input); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<ConstructionSaveDto>)
            Invoke(captureConstruction, input)!));
        rows.Add(new { Name = name, Operation = "CaptureConstruction", Version = 0,
            Civilizations = Freeze(civilizations), BeforeInput = before,
            AfterInput = Freeze(input), Result = result, Error = error });
    }

    ColonyRestore("colony-current-complete", _ => { });
    ColonyRestore("colony-current-nullable-fields", d =>
    { d.PlanetaryBodyId = null; d.RemainingExtractableMaterials = null; d.SurfaceHubLevel = null; });
    ColonyRestore("colony-format7-derives-species-ignores-body", d =>
    { d.CivilizationId = 2; d.PopulationSpeciesId = "unknown"; }, 7);
    ColonyRestore("colony-format7-unknown-civilization", d => d.CivilizationId = 99, 7);
    ColonyRestore("colony-format8-missing-species", d => d.PopulationSpeciesId = null, 8);
    ColonyRestore("colony-format8-unicode-blank-species", d => d.PopulationSpeciesId = "\u3000", 8);
    ColonyRestore("colony-format11-nonempty-surface", _ => { }, 11);
    ColonyRestore("colony-format11-null-surface-default-hub", d =>
    { d.SurfaceBuildings = null; d.SurfaceHubLevel = null; }, 11);
    ColonyRestore("colony-format11-empty-surface", d => d.SurfaceBuildings = new(), 11);
    ColonyRestore("colony-format12-null-surface", d => d.SurfaceBuildings = null, 12);
    ColonyRestore("colony-species-error-before-surface", d =>
    { d.PopulationSpeciesId = "bad"; d.SurfaceBuildings = null; }, 12);
    ColonyRestore("colony-second-row-failure-discards-output", d =>
        d.SurfaceBuildings = null, version: 12, count: 2, editIndex: 1);
    ColonyCapture("colony-capture-complete", _ => { });
    ColonyCapture("colony-capture-unknown-species", c => c.PopulationSpeciesId = "bad");
    ColonyCapture("colony-capture-unicode-blank-species", c => c.PopulationSpeciesId = "\u00A0");

    EconomyRestore("economy-restore-complete", _ => { });
    EconomyRestore("economy-null-industry-priority", d => d.IndustryPriority = null);
    EconomyRestore("economy-unvalidated-credit-nan", d => d.Credits = double.NaN);
    EconomyRestore("economy-invalid-spending", d => d.LastResearchSpendingPerDay = double.NaN);
    EconomyRestore("economy-invalid-funding", d => d.LastResearchFundingFraction = 1.01);
    EconomyRestore("economy-invalid-arrears", d => d.OperatingArrears = -1);
    EconomyRestore("economy-invalid-operations-funding", d => d.LastBaseOperationsFundingFraction = double.PositiveInfinity);
    EconomyRestore("economy-invalid-enum", d => d.IndustryPriority = (IndustryPriority)99);
    EconomyRestore("economy-funding-error-before-enum", d =>
    { d.LastResearchSpendingPerDay = -1; d.IndustryPriority = (IndustryPriority)99; });
    EconomyCapture("economy-capture-complete", _ => { });
    EconomyCapture("economy-capture-does-not-revalidate-funding", e => e.LastResearchSpendingPerDay = double.NaN);
    EconomyCapture("economy-capture-invalid-enum", e => e.IndustryPriority = (IndustryPriority)99);

    TechnologyRestore("technology-restore-insertion-dedup", _ => { });
    TechnologyRestore("technology-null-active-research", d =>
    { d.ActiveResearchId = null; d.ActiveResearchProgress = -0.0; });
    TechnologyRestore("technology-restore-null-completed", d => d.CompletedTechnologyIds = null!);
    TechnologyRestore("technology-restore-allows-arbitrary-values", d =>
    { d.CompletedTechnologyIds = new() { "", "\u3000", "unknown" }; d.ActiveResearchProgress = double.NaN; });
    TechnologyCapture("technology-capture-utf16-ordinal-sort", _ => { });
    TechnologyCapture("technology-capture-null-active-research", t =>
    { t.ActiveResearchId = null; t.ActiveResearchProgress = -0.0; });

    ConstructionRestore("construction-restore-complete", _ => { });
    ConstructionRestore("construction-valid-inactive", d =>
    { d.ActiveProjectId = null; d.ActiveProjectProgress = 0; d.ActiveProjectAuthorizationCredits = 0; });
    ConstructionRestore("construction-missing-completed", d => d.CompletedProjectIds = null!);
    ConstructionRestore("construction-missing-queue", d => d.QueuedProjects = null!);
    ConstructionRestore("construction-invalid-active-number", d => d.ActiveProjectProgress = double.NaN);
    ConstructionRestore("construction-unknown-active", d => d.ActiveProjectId = "unknown");
    ConstructionRestore("construction-orphan-active-values", d =>
    { d.ActiveProjectId = null; d.ActiveProjectProgress = 1; });
    ConstructionRestore("construction-cost-tolerance-accepted", d => d.ActiveProjectProgress = 900.0001);
    ConstructionRestore("construction-cost-exceeded", d => d.ActiveProjectProgress = 900.0002);
    ConstructionRestore("construction-completed-unknown", d => d.CompletedProjectIds.Add("unknown"));
    ConstructionRestore("construction-completed-duplicate", d => d.CompletedProjectIds.Add("research_network"));
    ConstructionRestore("construction-active-completed-overlap", d => d.CompletedProjectIds.Add("industrial_automation"));
    ConstructionRestore("construction-queue-overflow", d =>
    { while (d.QueuedProjects.Count <= Game.Simulation.Construction.ConstructionState.MaxQueuedProjects) d.QueuedProjects.Add(new() { ProjectId = $"x{d.QueuedProjects.Count}", AuthorizationCredits = 0 }); });
    ConstructionRestore("construction-null-queue-item", d => d.QueuedProjects.Add(null!));
    ConstructionRestore("construction-unicode-blank-queue-id", d => d.QueuedProjects.Add(new() { ProjectId = "\u202F", AuthorizationCredits = 0 }));
    ConstructionRestore("construction-unknown-queue-id", d => d.QueuedProjects.Add(new() { ProjectId = "unknown", AuthorizationCredits = 0 }));
    ConstructionRestore("construction-invalid-queue-authorization", d => d.QueuedProjects[0] = new() { ProjectId = "orbital_launch_complex", AuthorizationCredits = double.NaN });
    ConstructionRestore("construction-duplicate-queue", d => d.QueuedProjects.Add(new() { ProjectId = "orbital_launch_complex", AuthorizationCredits = 0 }));
    ConstructionRestore("construction-queue-completed-overlap", d => d.QueuedProjects.Add(new() { ProjectId = "research_network", AuthorizationCredits = 0 }));
    ConstructionRestore("construction-queue-active-overlap", d => d.QueuedProjects.Add(new() { ProjectId = "industrial_automation", AuthorizationCredits = 0 }));
    ConstructionRestore("construction-first-error-missing-before-number", d =>
    { d.CompletedProjectIds = null!; d.ActiveProjectProgress = double.NaN; });
    ConstructionRestore("construction-second-row-failure-discards-output",
        d => d.ActiveProjectId = "unknown", count: 2, editIndex: 1);
    ConstructionCapture("construction-capture-utf16-sort-and-queue", _ => { });
    ConstructionCapture("construction-capture-does-not-validate", c =>
    { c.ActiveProjectId = "unknown"; c.ActiveProjectProgress = double.NaN; });

    var aliasSource = ColonyState(88);
    var aliasDtos = (IReadOnlyList<ColonySaveDto>)Invoke(captureColonies,
        new List<ColonyState> { aliasSource })!;
    var aliasBefore = Freeze(aliasDtos);
    aliasSource.SurfaceBuildings[0].TypeId = "mutated";
    aliasSource.SurfaceBuildings.Add(Building());
    var aliasAfter = Freeze(aliasDtos);
    var document = new
    {
        Format = "stellar-galaxy-economy-persistence-actual-source-v1",
        SourceSha256Before = sourceBefore,
        SourceSha256After = SourceHash(),
        RowCount = rows.Count,
        SourceCaptureAliasProbe = new { Before = aliasBefore, After = aliasAfter },
        Rows = rows,
    };
    File.WriteAllText(output, JsonSerializer.Serialize(document, new JsonSerializerOptions(options)
    { WriteIndented = true }));
    Console.WriteLine($"rows={rows.Count} source={sourceBefore} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
}
catch (Exception error)
{
    Console.Error.WriteLine($"{error.GetType().FullName}: {error.Message}");
    Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"Source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"Fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
return 0;
