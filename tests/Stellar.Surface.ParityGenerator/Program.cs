using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    var options = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };

    ColonyState Colony(params SurfaceBuildingState[] buildings) => new()
    {
        Id = 1, CivilizationId = 1, SystemId = 1, Name = "Surface",
        PopulationSpeciesId = "terran_baseline", PopulationMillions = 100,
        Infrastructure = 1, SurfaceHubLevel = 2, SurfaceBuildings = buildings.ToList(),
    };
    SurfaceBuildingState Building(int id, string type, bool complete = true, bool enabled = true,
        double condition = 1, int priority = 0, double stored = 0, double progress = 0) => new()
    {
        Id = id, TypeId = type, IsComplete = complete, IsEnabled = enabled,
        Condition = condition, OperatingPriority = priority, StoredPowerDays = stored,
        IndustryProgress = complete ? SurfaceBuildingCatalog.Find(type)!.IndustryCost : progress,
    };
    ColonyState Clone(ColonyState value) =>
        JsonSerializer.Deserialize<ColonyState>(JsonSerializer.Serialize(value, options), options)!;

    var cases = new List<object>
    {
        new { Name = "catalog", Kind = "Catalog", Expected = SurfaceBuildingCatalog.All },
    };

    foreach (var hub in new[] { 0, 1, 2, 3, 4 })
    {
        var colony = Colony(); colony.SurfaceHubLevel = hub;
        cases.Add(new { Name = $"capacity-hub-{hub}", Kind = "Capacity", Colony = colony,
            Expected = SurfaceConstruction.GetBuildingCapacity(colony) });
    }
    var outpost = Colony(); outpost.Kind = SettlementKind.ResourceOutpost; outpost.SurfaceHubLevel = 3;
    cases.Add(new { Name = "capacity-resource-outpost", Kind = "Capacity", Colony = outpost,
        Expected = SurfaceConstruction.GetBuildingCapacity(outpost) });

    foreach (var progress in new[] { 0d, .15, .40, .75, .95, 1d, 500d / 450d })
    {
        var building = Building(1, "fabricator", complete: false, progress: 450 * progress);
        cases.Add(new { Name = $"stage-{progress}", Kind = "Stage", Building = building,
            Expected = SurfaceConstruction.GetConstructionStage(building) });
    }
    var completedStage = Building(1, "fabricator");
    cases.Add(new { Name = "stage-completed", Kind = "Stage", Building = completedStage,
        Expected = SurfaceConstruction.GetConstructionStage(completedStage) });

    void AddOutput(string name, ColonyState colony, double interval = 1) => cases.Add(new
    {
        Name = name, Kind = "Output", Colony = colony, PowerIntervalDays = interval,
        Expected = SurfaceConstruction.GetOutput(colony, interval),
    });

    AddOutput("output-all-basic", Colony(
        Building(1,"power_generator"), Building(2,"science_lab"), Building(3,"fabricator"),
        Building(4,"trade_hub"), Building(5,"habitat_complex"), Building(6,"controlled_agriculture"),
        Building(7,"water_reclamation"), Building(8,"cargo_terminal")));
    AddOutput("output-priority", Colony(Building(10,"science_lab",priority:1),
        Building(11,"fabricator"), Building(12,"power_generator")));
    AddOutput("output-battery-discharge", Colony(Building(20,"power_generator"),
        Building(21,"grid_battery",stored:5), Building(22,"advanced_science_lab")));
    AddOutput("output-ineligible", Colony(Building(30,"science_lab",condition:.15),
        Building(31,"fabricator",enabled:false), Building(32,"trade_hub",complete:false)));

    var scarce = Colony(Building(1,"science_lab"), Building(2,"water_reclamation"),
        Building(3,"controlled_agriculture"), Building(4,"habitat_complex"), Building(5,"trade_hub",priority:1));
    scarce.PopulationMillions = .12;
    AddOutput("output-workforce-scarcity-priority-services", scarce);

    AddOutput("output-inactive-storage-totals", Colony(
        Building(1,"grid_battery",enabled:false,stored:7), Building(2,"grid_battery",condition:.15,stored:3),
        Building(3,"grid_battery",complete:false,stored:2), Building(4,"power_generator")));
    AddOutput("output-damage-capacities-cargo", Colony(
        Building(1,"advanced_power_generator",condition:.50), Building(2,"controlled_agriculture",condition:.50),
        Building(3,"water_reclamation",condition:.50), Building(4,"habitat_complex",condition:.50),
        Building(5,"cargo_terminal",condition:.50)));
    AddOutput("output-habitat-cap", Colony(
        Building(1,"advanced_power_generator"), Building(2,"advanced_power_generator"),
        Building(3,"advanced_habitat_complex"), Building(4,"advanced_habitat_complex")));

    var specialtyColonies = new[]
    {
        ("science", Colony(Building(1,"advanced_power_generator"), Building(2,"science_lab"), Building(3,"science_lab"), Building(4,"advanced_science_lab"))),
        ("industry", Colony(Building(1,"advanced_power_generator"), Building(2,"fabricator"), Building(3,"fabricator"), Building(4,"advanced_fabricator"))),
        ("trade", Colony(Building(1,"advanced_power_generator"), Building(2,"trade_hub"), Building(3,"trade_hub"), Building(4,"advanced_trade_hub"))),
        ("power", Colony(Building(1,"power_generator"), Building(2,"power_generator"), Building(3,"advanced_power_generator"), Building(4,"advanced_science_lab"))),
    };
    foreach (var (name, colony) in specialtyColonies)
    {
        AddOutput($"output-specialty-{name}", colony);
        cases.Add(new { Name = $"specialization-active-{name}", Kind = "Specialization", Colony = colony,
            Expected = SurfaceConstruction.GetSpecialization(colony) });
    }
    var mixedTie = Colony(Building(1,"science_lab"), Building(2,"advanced_science_lab"),
        Building(3,"fabricator"), Building(4,"advanced_fabricator"), Building(5,"trade_hub"), Building(6,"advanced_trade_hub"));
    cases.Add(new { Name = "specialization-mixed-tie", Kind = "Specialization", Colony = mixedTie,
        Expected = SurfaceConstruction.GetSpecialization(mixedTie) });
    var developing = Colony(Building(1,"science_lab"));
    cases.Add(new { Name = "specialization-developing", Kind = "Specialization", Colony = developing,
        Expected = SurfaceConstruction.GetSpecialization(developing) });
    var general = Colony(Building(1,"science_lab",enabled:false), Building(2,"fabricator",condition:.15));
    cases.Add(new { Name = "specialization-general", Kind = "Specialization", Colony = general,
        Expected = SurfaceConstruction.GetSpecialization(general) });

    var powerCases = new[]
    {
        Colony(Building(1,"power_generator"),Building(2,"grid_battery",stored:0)),
        Colony(Building(1,"science_lab"),Building(2,"grid_battery",stored:12)),
        Colony(Building(1,"science_lab"),Building(2,"grid_battery",stored:3),Building(3,"grid_battery",stored:8)),
        Colony(Building(1,"power_generator"),Building(2,"grid_battery",stored:4),Building(3,"grid_battery",stored:8)),
    };
    foreach (var template in powerCases)
    foreach (var days in new[] { 0d, .5d, 1d, 3d })
    {
        var colony = Clone(template); var before = Clone(colony);
        var output = SurfaceConstruction.GetOutput(colony, days == 0 ? 1 : days);
        SurfaceConstruction.AdvancePowerStorage(colony, output, days);
        cases.Add(new { Name = $"power-{cases.Count}", Kind = "AdvancePower", Colony = before,
            Interval = days == 0 ? 1 : days, ExpectedOutput = output, Days = days,
            ExpectedColonyBefore = before, ExpectedColonyAfter = Clone(colony) });
    }

    var order = Colony(Building(30,"grid_battery",stored:10), Building(10,"grid_battery",stored:2),
        Building(20,"grid_battery",stored:5), Building(40,"advanced_science_lab"));
    var orderBefore = Clone(order); var orderOutput = SurfaceConstruction.GetOutput(order);
    SurfaceConstruction.AdvancePowerStorage(order,orderOutput,1);
    cases.Add(new { Name = "power-stable-id-order", Kind = "AdvancePower", Colony = orderBefore,
        Interval = 1d, ExpectedOutput = orderOutput, Days = 1d,
        ExpectedColonyBefore = orderBefore, ExpectedColonyAfter = Clone(order) });

    var depleted = Colony(Building(1,"advanced_science_lab"),Building(2,"grid_battery",stored:2));
    var depletedBefore = Clone(depleted); var depletionOutput = SurfaceConstruction.GetOutput(depleted);
    SurfaceConstruction.AdvancePowerStorage(depleted,depletionOutput,20);
    cases.Add(new { Name = "power-exhaustion", Kind = "AdvancePower", Colony = depletedBefore,
        Interval = 1d, ExpectedOutput = depletionOutput, Days = 20d,
        ExpectedColonyBefore = depletedBefore, ExpectedColonyAfter = Clone(depleted) });

    foreach (var invalid in new[] { double.NaN, double.PositiveInfinity, 0d, -1d })
    {
        try { SurfaceConstruction.GetOutput(Colony(), invalid); }
        catch (Exception error) { cases.Add(new { Name = $"invalid-output-{invalid}", Kind = "InvalidOutput",
            Colony = Colony(), PowerIntervalDays = invalid, ExpectedError = error.Message }); }
    }
    foreach (var invalid in new[] { double.NaN, double.PositiveInfinity, -1d })
    {
        try { SurfaceConstruction.AdvancePowerStorage(Colony(), SurfaceConstruction.GetOutput(Colony()), invalid); }
        catch (Exception error) { cases.Add(new { Name = $"invalid-advance-{invalid}", Kind = "InvalidAdvance",
            Colony = Colony(), Days = invalid, ExpectedError = error.Message }); }
    }

    var unknown = Colony();
    unknown.SurfaceBuildings.Add(new SurfaceBuildingState { Id=1,TypeId="unknown_complex",IsComplete=true,IsEnabled=true,Condition=1 });
    try { SurfaceConstruction.GetOutput(unknown); }
    catch (Exception error) { cases.Add(new { Name="unknown-building-type",Kind="UnknownType",Colony=unknown,ExpectedError=error.Message }); }

    var integrationGalaxy = new GalaxyGenerator().Generate(8374837,
        GalaxyGenerationMetadata.FullGalaxy500("surface",8374837,systemCount:250).ToSettings());
    var integration = Colony(Building(1,"power_generator"),Building(2,"controlled_agriculture"),
        Building(3,"water_reclamation"),Building(4,"habitat_complex"));
    integration.PlanetaryBodyId = null;
    var integrationOutput = SurfaceConstruction.GetOutput(integration);
    var sustenance = ColonySustenanceCapacity.GetSnapshot(integrationGalaxy,integration,
        new SurfaceColonyOutput(integrationOutput.Supply,integrationOutput.Demand,integrationOutput.SciencePerDay,
            integrationOutput.IndustryPerDay,integrationOutput.CreditsPerDay,integrationOutput.UpkeepCreditsPerDay,
            new HashSet<int>(integrationOutput.PoweredBuildingIds),integrationOutput.HabitatSupportReduction,
            integrationOutput.FoodCapacityMillions,integrationOutput.WaterCapacityMillions,integrationOutput.HousingCapacityMillions,
            integrationOutput.WorkforceAvailableMillions,integrationOutput.WorkforceDemandMillions,
            new HashSet<int>(integrationOutput.StaffedBuildingIds),integrationOutput.StoredPowerDays,
            integrationOutput.PowerStorageCapacityDays,integrationOutput.StorageChargePerDay,
            integrationOutput.StorageDischargePerDay,integrationOutput.CargoTransferCapacityPerDay));
    var reserve = ColonySustenanceReserves.Preview(integration,sustenance,1);
    cases.Add(new { Name="integration-sustenance",Kind="Integration",Colony=integration,ExpectedOutput=integrationOutput,
        ExpectedSustenance=sustenance,Days=1d,ExpectedReservePreview=reserve });

    File.WriteAllText(args[0],JsonSerializer.Serialize(new
        { Format="stellar-surface-economy-parity-v1",Catalog=SurfaceBuildingCatalog.All,Cases=cases },options)+Environment.NewLine);
    Console.WriteLine($"Exported {cases.Count} surface cases.");
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine(error);
    return 1;
}
