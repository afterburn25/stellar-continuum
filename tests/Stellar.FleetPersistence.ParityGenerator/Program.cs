using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 2) throw new ArgumentException("Expected source root and fixture path.");
    var sourceRoot = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var options = new JsonSerializerOptions
    {
        IncludeFields = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze<T>(T value) => JsonSerializer.SerializeToElement(value, options);
    T Clone<T>(T value) => JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(value, options), options)!;
    var type = typeof(CampaignSaveService);
    var restore = type.GetMethod("ToFleets", BindingFlags.NonPublic | BindingFlags.Static)!;
    var capture = type.GetMethod("ToFleetDtos", BindingFlags.NonPublic | BindingFlags.Static)!;
    object? Invoke(MethodInfo method, params object?[] values)
    {
        try { return method.Invoke(null, values); }
        catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
    }
    object? Failure(Action action)
    {
        try { action(); return null; }
        catch (Exception e) { return new { Type = e.GetType().Name, e.Message }; }
    }
    CivilizationState Civ(int id, string species = SpeciesCatalog.TerranBaselineId) => new(
        id, $"C{id}", 1, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced,
        true, CivilizationDevelopmentStage.WarpCapable, false, SpeciesId: species);
    var civilizations = new List<CivilizationState> { Civ(1), Civ(2, SpeciesCatalog.PelagicHighPressureId) };
    FleetSaveDto Dto(int id = 10) => new()
    {
        Id = id, CivilizationId = 1, Name = $"F{id}", Role = FleetRole.Military,
        DesignId = "patrol_corvette", X = 1.25f, Y = -2.5f, CurrentSystemId = 1,
        DestinationSystemId = 9, TransitPhase = FleetTransitPhase.InterstellarWarp,
        TransitOriginSystemId = 1, TransitTargetSystemId = 4, TransitProgress = .25,
        LocalTransitStartX = 1, LocalTransitStartY = 2, LocalTransitPositionX = 3,
        LocalTransitPositionY = 4, LocalTransitTargetX = 5, LocalTransitTargetY = 6,
        PlannedRouteSystemIds = new() { 4, 9 }, HoldRequested = true,
        ReturnToBaseRequested = true, ReturnToBaseFailureReason = "blocked", MissionOrderRevision = 7,
        DestinationPlanetaryBodyId = 90, PreventAutomaticSettlement = false, SettlementBodyId = 91,
        SettlementDaysCompleted = 2.25, ReconnaissanceSystemId = 8, ReconnaissanceDaysCompleted = 3.5,
        FreightTargetOutpostId = 31, FreightHomeColonyId = 32, CargoMaterialCapacity = 44,
        CargoMaterials = 11, StrategicSpeed = 23, MaximumLegRangeLightYears = 345,
        FuelCapacityLightYears = 678, FuelRemainingLightYears = 456, SensorRange = 87,
        IsActive = true, EmbarkedPopulationMillions = 12, EmbarkedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId,
        Combat = new() { ProfileId = "patrol_corvette_mk1", Shields = 12, Armor = 23, Hull = 34,
            WeaponCooldownRemainingDays = .5, Order = MilitaryOrderType.Attack, TargetFleetId = 99,
            DefendSystemId = 8, RetreatProgressDays = .2, RetreatStarted = true,
            IsDisengaged = false, DisengagedSystemId = null },
        TacticalLoadout = new() { MassPerShip = 101, Acceleration = 19, MaximumSpeed = 121,
            ShieldPerShip = 36, ArmorPerShip = 46, HullPerShip = 96, ReactorOutputPerShip = 102,
            CoolingPerShip = 29, WarpStabilization = 51, WarpSpoolSeconds = 13,
            ModuleSlotCapacity = 14, MaximumModuleMass = 430,
            Weapons = { new() { Id = "w", Kind = MassiveWeaponKind.Missile, MountsPerShip = 2,
                DamagePerShot = 9, ShotsPerSecond = 2, Range = 600, Accuracy = .7f, PowerPerSecond = 4, HeatPerSecond = 3 } },
            Modules = { new() { Id = "m", Kind = MassiveModuleKind.Sensor, InstalledCount = 2,
                MassEach = 2, PowerPerSecondEach = 3, HeatPerSecondEach = 4, Condition = .8f,
                Enabled = false, EffectiveRange = 500, FieldStrength = 6, DetectionSignature = 7, Slots = 2 } } },
        TacticalVessel = new() { Id = 123, Name = "Named", DesignId = "patrol_corvette",
            IsFlagship = true, IsCarrier = true, IsInterdictor = true, IsStoryShip = true,
            HullFraction = .9f, EngineFraction = .8f, SensorFraction = .7f, WarpDriveFraction = .6f,
            ReactorFraction = .5f, InterdictorFraction = .4f, BattlesFought = 3, ConfirmedKills = 2,
            Destroyed = false, Escaped = true },
    };
    FleetState State(int id = 20) => new()
    {
        Id = id, CivilizationId = 1, Name = $"S{id}", Role = FleetRole.Military,
        DesignId = "patrol_corvette", Position = new(1, 2), CurrentSystemId = 1,
        PlannedRouteSystemIds = new() { 3 }, StrategicSpeed = 22, MaximumLegRangeLightYears = 360,
        FuelCapacityLightYears = 1000, FuelRemainingLightYears = 900, SensorRange = 100,
        IsActive = true,
    };
    var rows = new List<object>();
    void RestoreCase(string name, Action<FleetSaveDto> edit, int version = 16, bool legacyPopulation = false,
        List<CivilizationState>? civs = null, params FleetSaveDto[]? supplied)
    {
        var hasSupplied = supplied is { Length: > 0 };
        var dtos = hasSupplied ? supplied!.Select(Clone).ToList() : new List<FleetSaveDto> { Dto() };
        if (!hasSupplied) edit(dtos[0]);
        var before = Freeze(dtos); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<FleetState>)Invoke(restore, dtos, civs ?? civilizations, version, legacyPopulation)!));
        rows.Add(new { Name = name, Operation = "Restore", Version = version, RestoreLegacyPopulation = legacyPopulation,
            Civilizations = Freeze(civs ?? civilizations), BeforeInput = before, AfterInput = Freeze(dtos), Result = result, Error = error });
    }
    void CaptureCase(string name, params FleetState[] values)
    {
        var fleets = values.Select(Clone).ToList(); var before = Freeze(fleets); object? result = null;
        var error = Failure(() => result = Freeze((IReadOnlyList<FleetSaveDto>)Invoke(capture, fleets)!));
        rows.Add(new { Name = name, Operation = "Capture", Version = 0, RestoreLegacyPopulation = false,
            Civilizations = Freeze(civilizations), BeforeInput = before, AfterInput = Freeze(fleets), Result = result, Error = error });
    }

    RestoreCase("current-complete-nested", _ => { });
    RestoreCase("null-route-becomes-empty", d => d.PlannedRouteSystemIds = null);
    RestoreCase("empty-route-stays-empty", d => d.PlannedRouteSystemIds = new());
    RestoreCase("old-inflight-route-first", d => { d.CurrentSystemId = null; d.TransitPhase = 0; });
    RestoreCase("old-inflight-destination-fallback", d => { d.CurrentSystemId = null; d.TransitPhase = 0; d.PlannedRouteSystemIds = new(); });
    RestoreCase("warp-without-target", d => { d.CurrentSystemId = 1; d.DestinationSystemId = null; d.TransitPhase = FleetTransitPhase.InterstellarWarp; d.TransitTargetSystemId = null; });
    RestoreCase("local-without-current", d => { d.CurrentSystemId = null; d.DestinationSystemId = null; d.TransitPhase = FleetTransitPhase.LocalArrival; });
    RestoreCase("invalid-transit-enum", d => d.TransitPhase = (FleetTransitPhase)99);
    RestoreCase("invalid-transit-progress", d => d.TransitProgress = double.NaN);
    RestoreCase("invalid-local-coordinate", d => d.LocalTransitPositionX = float.PositiveInfinity);
    RestoreCase("format7-species-and-body", d => { d.CivilizationId = 2; d.EmbarkedPopulationSpeciesId = "bad"; }, 7);
    RestoreCase("format8-saved-species-and-body", d => { d.CivilizationId = 2; d.EmbarkedPopulationSpeciesId = SpeciesCatalog.CompactHighGravityId; }, 8);
    RestoreCase("format7-unknown-civilization", d => { d.CivilizationId = 77; }, 7);
    RestoreCase("format8-unknown-species", d => d.EmbarkedPopulationSpeciesId = "bad", 8);
    RestoreCase("legacy-colony-population-restored", d => { d.Role = FleetRole.Colony; d.Combat = null; d.EmbarkedPopulationMillions = null; d.EmbarkedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId; }, 7, true);
    RestoreCase("legacy-colony-population-not-restored", d => { d.Role = FleetRole.Colony; d.Combat = null; d.EmbarkedPopulationMillions = null; d.EmbarkedPopulationSpeciesId = null; }, 7, false);
    RestoreCase("range-and-fuel-fallbacks", d => { d.MaximumLegRangeLightYears = double.NaN; d.FuelCapacityLightYears = -1; d.FuelRemainingLightYears = null; d.EmbarkedPopulationMillions = -5; d.EmbarkedPopulationSpeciesId = "bad"; });
    RestoreCase("nan-population-preserved-without-species", d => { d.EmbarkedPopulationMillions = double.NaN; d.EmbarkedPopulationSpeciesId = "bad"; });
    var restoredFull = (IReadOnlyList<FleetState>)Invoke(restore, new List<FleetSaveDto>{Dto()}, civilizations, 16, false)!;
    CaptureCase("capture-full-nested", restoredFull[0]);
    var ensure = State(); CaptureCase("capture-ensures-combat", ensure);
    var zero = State(); zero.EmbarkedPopulationMillions = -5; zero.EmbarkedPopulationSpeciesId = "bad"; CaptureCase("capture-negative-population-ignores-species", zero);
    var nan = State(); nan.EmbarkedPopulationMillions = double.NaN; nan.EmbarkedPopulationSpeciesId = "bad"; CaptureCase("capture-nan-population-preserved", nan);
    var bad = State(); bad.EmbarkedPopulationMillions = 5; bad.EmbarkedPopulationSpeciesId = "bad"; CaptureCase("capture-species-failure-after-combat-mutation", bad);
    var first = State(30); var second = State(31); second.EmbarkedPopulationMillions = 5; second.EmbarkedPopulationSpeciesId = "bad"; CaptureCase("capture-second-failure-mutates-both", first, second);

    var source = "src/Game/Persistence/CampaignSaveService.cs";
    var fixture = new { Schema = "stellar-fleet-persistence-v1", SourceHashes = new Dictionary<string,string>{{source, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot, source))))}}, RowCount = rows.Count, Rows = rows };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals }));
    Console.WriteLine($"wrote {rows.Count} fleet persistence rows");
}
catch (Exception e) { Console.Error.WriteLine($"fleet persistence fixture failure: {e.GetType().Name}: {e.Message}"); return 1; }
return 0;
