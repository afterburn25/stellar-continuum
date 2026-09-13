using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Presentation;

static class Program
{
    static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: dotnet run --project <project> -- <fixture-path>");
            return 1;
        }

        try
        {
            var rows = BuildRows();
            var root = new JsonObject
            {
                ["Schema"] = 1,
                ["SourceFiles"] = new JsonArray(
                    "src/Game/Simulation/Combat/FleetCombatPower.cs",
                    "src/Game/Simulation/Combat/Massive/MassiveCombatOutcome.cs:MassiveCombatPowerCalculator.PerShipPower",
                    "src/Game/Presentation/Main.MassiveCombat.cs:HasCombatScanner"),
                ["SourceFingerprint"] = SourceFingerprint(),
                ["Rows"] = rows,
            };
            var path = Path.GetFullPath(args[0]);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, root.ToJsonString(JsonOptions) + Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"wrote {rows.Count} rows to {path}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
            Console.Error.WriteLine($"fixture={Path.GetFullPath(args[0])}");
            return 1;
        }
    }

    static JsonArray BuildRows()
    {
        var rows = new JsonArray();
        AddPowerRows(rows);
        AddObservedRows(rows);
        AddObserveRows(rows);
        AddScannerRows(rows);
        AddCapabilityRows(rows);
        return rows;
    }

    static void AddPowerRows(JsonArray rows)
    {
        var defaultLoadout = new MassiveCombatLoadout();
        AddReadOnly(rows, "per_ship_default", "PerShip", LoadoutJson(defaultLoadout), () => LoadoutJson(defaultLoadout), () => MassiveCombatPowerCalculator.PerShipPower(defaultLoadout));

        var custom = new MassiveCombatLoadout { ShieldPerShip = 1, ArmorPerShip = 2, HullPerShip = 3 };
        custom.Weapons.Add(new() { Id = "beam", Kind = MassiveWeaponKind.Beam, DamagePerShot = 10, ShotsPerSecond = 2, MountsPerShip = 3, Accuracy = 1.5f });
        custom.Weapons.Add(new() { Id = "pd", Kind = MassiveWeaponKind.PointDefense, ShotsPerSecond = 2, MountsPerShip = 2 });
        custom.Weapons.Add(new() { Id = "ew", Kind = MassiveWeaponKind.ElectronicWarfare, DamagePerShot = 999, ShotsPerSecond = 999, Accuracy = 1 });
        custom.Modules.Add(new() { Id = "on", Kind = MassiveModuleKind.Sensor, FieldStrength = 50, Condition = .5f, Enabled = true });
        custom.Modules.Add(new() { Id = "off", Kind = MassiveModuleKind.Sensor, FieldStrength = 999, Enabled = false });
        AddReadOnly(rows, "per_ship_custom", "PerShip", LoadoutJson(custom), () => LoadoutJson(custom), () => MassiveCombatPowerCalculator.PerShipPower(custom));

        var accumulation = new MassiveCombatLoadout();
        accumulation.Weapons.Add(new() { Id = "large", Kind = MassiveWeaponKind.Beam, DamagePerShot = 100_000_000, ShotsPerSecond = 1, Accuracy = 1 });
        for (var i = 0; i < 31; ++i)
            accumulation.Weapons.Add(new() { Id = $"small-{i}", Kind = MassiveWeaponKind.Beam, DamagePerShot = 1, ShotsPerSecond = 1, Accuracy = 1 });
        AddReadOnly(rows, "per_ship_double_sum_accumulator", "PerShip", LoadoutJson(accumulation), () => LoadoutJson(accumulation), () => MassiveCombatPowerCalculator.PerShipPower(accumulation));

        var invalidScalar = new MassiveCombatLoadout { MassPerShip = float.NaN };
        AddReadOnly(rows, "per_ship_nan_scalar", "PerShip", LoadoutJson(invalidScalar), () => LoadoutJson(invalidScalar), () => MassiveCombatPowerCalculator.PerShipPower(invalidScalar));
        var invalidBounds = new MassiveCombatLoadout { ModuleSlotCapacity = -1 };
        AddReadOnly(rows, "per_ship_invalid_bounds", "PerShip", LoadoutJson(invalidBounds), () => LoadoutJson(invalidBounds), () => MassiveCombatPowerCalculator.PerShipPower(invalidBounds));
        var invalidModule = new MassiveCombatLoadout();
        invalidModule.Modules.Add(new() { Id = "m", Kind = MassiveModuleKind.Sensor, Condition = 2 });
        AddReadOnly(rows, "per_ship_invalid_module", "PerShip", LoadoutJson(invalidModule), () => LoadoutJson(invalidModule), () => MassiveCombatPowerCalculator.PerShipPower(invalidModule));

        foreach (var (name, fleet) in new[]
        {
            ("own_inactive", Fleet(1, 1, FleetRole.Military, active: false)),
            ("own_military_default", Fleet(2, 1, FleetRole.Military)),
            ("own_science_default", Fleet(3, 1, FleetRole.Science)),
            ("own_unknown_profile", Fleet(4, 1, FleetRole.Colony, combat: new FleetCombatState { ProfileId = "missing", Armor = 12, Hull = 20 })),
            ("own_partial_damage", Fleet(5, 1, FleetRole.Military, combat: new FleetCombatState { ProfileId = CombatProfileIds.PatrolCorvetteMk1, Shields = 0, Armor = 0, Hull = 87.5 })),
            ("own_nan_damage", Fleet(6, 1, FleetRole.Military, combat: new FleetCombatState { ProfileId = CombatProfileIds.PatrolCorvetteMk1, Shields = double.NaN, Armor = 0, Hull = 0 })),
        }) AddReadOnly(rows, name, "OwnPower", FleetJson(fleet), () => FleetJson(fleet), () => FleetCombatPower.OwnPower(fleet));
    }

    static void AddObservedRows(JsonArray rows)
    {
        var own = Fleet(1, 1, FleetRole.Military);
        var ownGalaxy = Galaxy([own], [Civilization(1)]);
        AddWorld(rows, "observed_own_live", "Observed", ownGalaxy, new JsonObject { ["ObserverId"] = 1, ["TargetIndex"] = 0 }, () => FleetCombatPower.ObservedPower(ownGalaxy, 1, own));

        var foreign = Fleet(2, 2, FleetRole.Military);
        var none = Galaxy([foreign], [Civilization(1), Civilization(2)]);
        AddWorld(rows, "observed_foreign_none", "Observed", none, new JsonObject { ["ObserverId"] = 1, ["TargetIndex"] = 0 }, () => FleetCombatPower.ObservedPower(none, 1, foreign));

        var last = Galaxy([foreign], [Civilization(1), Civilization(2)]);
        last.CombatIntelligence.AddRange([new(1, 2, 10, 9, "old"), new(1, 2, 20, 1, "last")]);
        AddWorld(rows, "observed_foreign_last_insertion", "Observed", last, new JsonObject { ["ObserverId"] = 1, ["TargetIndex"] = 0 }, () => FleetCombatPower.ObservedPower(last, 1, foreign));
    }

    static void AddObserveRows(JsonArray rows)
    {
        foreach (var (name, observer, day, engaged, scan) in new[]
        {
            ("observe_negative_observer", -1, 1.0, true, false),
            ("observe_negative_day", 1, -1.0, true, false),
            ("observe_nan_day", 1, double.NaN, true, false),
            ("observe_infinity_day", 1, double.PositiveInfinity, true, false),
        })
        {
            var fleet = Fleet(2, 2, FleetRole.Military);
            var galaxy = Galaxy([fleet], [Civilization(1), Civilization(2)]);
            AddWorld(rows, name, "ObserveMany", galaxy, new JsonObject { ["ObserverId"] = observer, ["Day"] = D(day), ["Engaged"] = engaged, ["Scanning"] = scan, ["TargetIndices"] = new JsonArray(0) }, () => { FleetCombatPower.ObserveMany(galaxy, observer, [fleet], day, engaged, scan); return null; });
        }

        var gateFleetA = Fleet(7, 2, FleetRole.Military);
        var gateFleetB = Fleet(7, 3, FleetRole.Military);
        var gated = Galaxy([gateFleetA, gateFleetB], [Civilization(1), Civilization(2), Civilization(3)]);
        AddWorld(rows, "observe_gate_before_duplicate_map", "ObserveMany", gated, new JsonObject { ["ObserverId"] = 1, ["Day"] = 4, ["Engaged"] = false, ["Scanning"] = false, ["TargetIndices"] = new JsonArray(0) }, () => { FleetCombatPower.ObserveMany(gated, 1, [gateFleetA], 4, false, false); return null; });

        var duplicate = Galaxy([gateFleetA, gateFleetB], [Civilization(1), Civilization(2), Civilization(3)]);
        AddWorld(rows, "observe_duplicate_fleet_map", "ObserveMany", duplicate, new JsonObject { ["ObserverId"] = 1, ["Day"] = 4, ["Engaged"] = true, ["Scanning"] = false, ["TargetIndices"] = new JsonArray(0) }, () => { FleetCombatPower.ObserveMany(duplicate, 1, [gateFleetA], 4, true, false); return null; });

        var member = Fleet(8, 2, FleetRole.Military);
        var detached = Fleet(8, 2, FleetRole.Military);
        var detachedGalaxy = Galaxy([member], [Civilization(1), Civilization(2)]);
        detachedGalaxy.CombatIntelligence.Add(new(1, 8, 44, 2, "must survive rejected membership"));
        AddWorld(rows, "observe_detached_same_id", "ObserveDetached", detachedGalaxy, new JsonObject { ["ObserverId"] = 1, ["Day"] = 4, ["Engaged"] = true, ["Scanning"] = false, ["Detached"] = FleetJson(detached) }, () => { FleetCombatPower.ObserveMany(detachedGalaxy, 1, [detached], 4, true, false); return null; });

        var a = Fleet(30, 2, FleetRole.Military);
        var b = Fleet(10, 2, FleetRole.Science);
        var reverse = Galaxy([a, b], [Civilization(1), Civilization(2)]);
        reverse.CombatIntelligence.AddRange([new(1, 10, 1, 0, "old"), new(1, 99, 2, 0, "keep")]);
        AddWorld(rows, "observe_reverse_duplicate_targets", "ObserveMany", reverse, new JsonObject { ["ObserverId"] = 1, ["Day"] = 5.5, ["Engaged"] = false, ["Scanning"] = true, ["TargetIndices"] = new JsonArray(0, 1, 0) }, () => { FleetCombatPower.ObserveMany(reverse, 1, [a, b, a], 5.5, false, true); return null; });

        var repeatedTactical = Fleet(31, 2, FleetRole.Military);
        repeatedTactical.TacticalLoadout = customTacticalLoadout();
        var tacticalGalaxy = Galaxy([repeatedTactical], [Civilization(1), Civilization(2)]);
        AddWorld(rows, "observe_repeated_tactical_target_cache", "ObserveMany", tacticalGalaxy, new JsonObject { ["ObserverId"] = 1, ["Day"] = 6, ["Engaged"] = true, ["Scanning"] = false, ["TargetIndices"] = new JsonArray(0, 0) }, () => { FleetCombatPower.ObserveMany(tacticalGalaxy, 1, [repeatedTactical, repeatedTactical], 6, true, false); return null; });

        var evictFleet = Fleet(1, 2, FleetRole.Military);
        var evict = Galaxy([evictFleet], [Civilization(1), Civilization(2)]);
        for (var i = 0; i < 2048; ++i) evict.CombatIntelligence.Add(new(1, 10000 + i, i == 0 ? double.NaN : i % 3, i == 1 ? -0.0 : i % 4, "old"));
        AddWorld(rows, "observe_per_observer_eviction_nan_zero", "ObserveMany", evict, new JsonObject { ["ObserverId"] = 1, ["Day"] = 9, ["Engaged"] = true, ["Scanning"] = false, ["TargetIndices"] = new JsonArray(0) }, () => { FleetCombatPower.ObserveMany(evict, 1, [evictFleet], 9, true, false); return null; });

        var globalFleet = Fleet(2, 2, FleetRole.Military);
        var global = Galaxy([globalFleet], [Civilization(1), Civilization(2), Civilization(3)]);
        for (var i = 0; i < 4096; ++i) global.CombatIntelligence.Add(new(i % 2 == 0 ? 3 : 4, 20000 + i, i, i, "historic"));
        AddWorld(rows, "observe_global_prefix_eviction", "ObserveMany", global, new JsonObject { ["ObserverId"] = 1, ["Day"] = 10, ["Engaged"] = true, ["Scanning"] = false, ["TargetIndices"] = new JsonArray(0) }, () => { FleetCombatPower.ObserveMany(global, 1, [globalFleet], 10, true, false); return null; });

        var nullTarget = Galaxy([member], [Civilization(1), Civilization(2)]);
        AddSourceOnly(rows, "observe_null_target_source_boundary", "ObserveManyNull", WorldJson(nullTarget), new JsonObject { ["ObserverId"] = 1, ["Day"] = 1, ["Engaged"] = true, ["Scanning"] = false }, () => { FleetCombatPower.ObserveMany(nullTarget, 1, new FleetState[] { null! }, 1, true, false); return null; }, "Native public spans contain valid typed records; null enumerable elements are outside that type boundary.");
    }

    static void AddScannerRows(JsonArray rows)
    {
        var baseFleet = Fleet(1, 1, FleetRole.Military, system: 4);
        foreach (var (name, observer, day, capability) in new[]
        {
            ("scanner_negative_observer", -1, 1.0, true),
            ("scanner_nan_day", 1, double.NaN, true),
            ("scanner_missing_civilization", 9, 1.0, false),
        })
        {
            var galaxy = Galaxy([baseFleet], [Civilization(1)]);
            AddWorld(rows, name, "RecordSensorContacts", galaxy, new JsonObject { ["ObserverId"] = observer, ["Day"] = D(day), ["Scanning"] = capability }, () => FleetCombatPower.RecordSensorContacts(galaxy, observer, day, capability));
        }

        var off = Galaxy([baseFleet, Fleet(2, 2, FleetRole.Military, system: 4)], [Civilization(1), Civilization(2)]);
        AddWorld(rows, "scanner_capability_false", "RecordSensorContacts", off, new JsonObject { ["ObserverId"] = 1, ["Day"] = 2, ["Scanning"] = false }, () => FleetCombatPower.RecordSensorContacts(off, 1, 2, false));
        var noOccupied = Galaxy([Fleet(1, 1, FleetRole.Military, active: false, system: 4), Fleet(2, 2, FleetRole.Military, system: 4)], [Civilization(1), Civilization(2)]);
        AddWorld(rows, "scanner_no_active_observer_presence", "RecordSensorContacts", noOccupied, new JsonObject { ["ObserverId"] = 1, ["Day"] = 2, ["Scanning"] = true }, () => FleetCombatPower.RecordSensorContacts(noOccupied, 1, 2, true));

        var visible = Galaxy([
            Fleet(1, 1, FleetRole.Military, system: 4), Fleet(2, 1, FleetRole.Science, system: 5),
            Fleet(30, 2, FleetRole.Military, system: 4), Fleet(10, 2, FleetRole.Military, system: 5),
            Fleet(20, 2, FleetRole.Military, active: false, system: 4), Fleet(40, 2, FleetRole.Military, system: null)],
            [Civilization(1), Civilization(2)]);
        AddWorld(rows, "scanner_same_system_filter_and_sort", "RecordSensorContacts", visible, new JsonObject { ["ObserverId"] = 1, ["Day"] = 3, ["Scanning"] = true }, () => FleetCombatPower.RecordSensorContacts(visible, 1, 3, true));

        var boundedFleets = new List<FleetState> { Fleet(1, 1, FleetRole.Military, system: 7) };
        for (var id = 5000; id >= 2951; --id)
            boundedFleets.Add(Fleet(id, 2, FleetRole.Military, system: 7));
        var bounded = Galaxy(boundedFleets, [Civilization(1), Civilization(2)]);
        AddWorld(rows, "scanner_stable_2048_bound", "RecordSensorContacts", bounded, new JsonObject { ["ObserverId"] = 1, ["Day"] = 4, ["Scanning"] = true }, () => FleetCombatPower.RecordSensorContacts(bounded, 1, 4, true));
    }

    static void AddCapabilityRows(JsonArray rows)
    {
        foreach (var (name, capabilities) in new[]
        {
            ("capability_none", Array.Empty<string>()),
            ("capability_quantum", new[] { "tech:quantum_sensors" }),
            ("capability_distributed", new[] { "tech:distributed_sensor_network" }),
            ("capability_wrong_case", new[] { "TECH:QUANTUM_SENSORS" }),
        })
        {
            var state = new AdaptiveResearchCivilizationState("1", "stage");
            foreach (var capability in capabilities) AddCapability(state, capability);
            JsonObject StateJson() => new() { ["Capabilities"] = new JsonArray(state.Capabilities.Select(value => JsonValue.Create(value.CapabilityId)).ToArray()) };
            AddReadOnly(rows, name, "HasCombatScanner", StateJson(), StateJson, () => InvokeHasCombatScanner(state, 1));
        }
        Add(rows, "capability_null_campaign", "HasCombatScannerNull", new JsonObject(), () => InvokeHasCombatScanner(null, 1));
        var missing = new AdaptiveResearchCivilizationState("1", "stage");
        Add(rows, "capability_missing_civilization", "HasCombatScannerMissing", new JsonObject(), () => InvokeHasCombatScanner(missing, 2));
    }

    static void AddCapability(AdaptiveResearchCivilizationState state, string capability)
    {
        var method = typeof(AdaptiveResearchCivilizationState).GetMethod("AddCapability", BindingFlags.Instance | BindingFlags.NonPublic)!;
        try { method.Invoke(state, [capability, null]); }
        catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
    }

    static bool InvokeHasCombatScanner(AdaptiveResearchCivilizationState? state, int civilizationId)
    {
        var main = (Main)RuntimeHelpers.GetUninitializedObject(typeof(Main));
        if (state is not null)
        {
            var campaign = (AdaptiveResearchCampaignState)RuntimeHelpers.GetUninitializedObject(typeof(AdaptiveResearchCampaignState));
            var civilizations = new Dictionary<int, AdaptiveResearchCivilizationState> { [1] = state };
            typeof(AdaptiveResearchCampaignState).GetField("_civilizations", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(campaign, civilizations);
            typeof(Main).GetField("_adaptiveResearch", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(main, campaign);
        }
        var method = typeof(Main).GetMethod("HasCombatScanner", BindingFlags.Instance | BindingFlags.NonPublic)!;
        try { return (bool)method.Invoke(main, [civilizationId])!; }
        catch (TargetInvocationException ex) when (ex.InnerException is not null) { throw ex.InnerException; }
    }

    static void Add(JsonArray rows, string name, string op, JsonNode input, Func<object?> invoke)
    {
        object? result = null; Exception? error = null;
        try { result = invoke(); } catch (Exception ex) { error = ex; }
        rows.Add(new JsonObject { ["Name"] = name, ["Operation"] = op, ["Native"] = true, ["Input"] = input.DeepClone(), ["InputAfter"] = input.DeepClone(), ["Result"] = Value(result), ["Error"] = Error(error) });
    }

    static void AddReadOnly(JsonArray rows, string name, string op, JsonNode input, Func<JsonNode> projectAfter, Func<object?> invoke)
    {
        object? result = null; Exception? error = null;
        try { result = invoke(); } catch (Exception ex) { error = ex; }
        rows.Add(new JsonObject { ["Name"] = name, ["Operation"] = op, ["Native"] = true, ["Input"] = input.DeepClone(), ["InputAfter"] = projectAfter(), ["Result"] = Value(result), ["Error"] = Error(error) });
    }

    static void AddWorld(JsonArray rows, string name, string op, GalaxyState galaxy, JsonObject args, Func<object?> invoke)
    {
        var before = WorldJson(galaxy);
        object? result = null; Exception? error = null;
        try { result = invoke(); } catch (Exception ex) { error = ex; }
        rows.Add(new JsonObject { ["Name"] = name, ["Operation"] = op, ["Native"] = true, ["Args"] = args.DeepClone(), ["ArgsAfter"] = args.DeepClone(), ["Before"] = before, ["After"] = WorldJson(galaxy), ["Result"] = Value(result), ["Error"] = Error(error) });
    }

    static void AddSourceOnly(JsonArray rows, string name, string op, JsonNode input, JsonObject args, Func<object?> invoke, string boundary)
    {
        object? result = null; Exception? error = null;
        try { result = invoke(); } catch (Exception ex) { error = ex; }
        rows.Add(new JsonObject { ["Name"] = name, ["Operation"] = op, ["Native"] = false, ["Boundary"] = boundary, ["Input"] = input.DeepClone(), ["InputAfter"] = input.DeepClone(), ["Args"] = args.DeepClone(), ["Result"] = Value(result), ["Error"] = Error(error) });
    }

    static JsonNode? Value(object? value) => value switch
    {
        null => null, double d => D(d), float f => D(f), int i => i, bool b => b, _ => JsonValue.Create(value.ToString())
    };
    static JsonNode? Error(Exception? ex) => ex is null ? null : new JsonObject { ["Type"] = ex.GetType().Name, ["Message"] = ex.Message };
    static JsonNode D(double value) => double.IsNaN(value) ? "NaN" : double.IsPositiveInfinity(value) ? "Infinity" : double.IsNegativeInfinity(value) ? "-Infinity" : JsonValue.Create(value)!;

    static CivilizationState Civilization(int id) => new(id, $"C{id}", id, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, id == 1, CivilizationDevelopmentStage.WarpCapable);
    static FleetState Fleet(int id, int civilizationId, FleetRole role, bool active = true, int? system = 1, FleetCombatState? combat = null) => new()
    {
        Id = id, CivilizationId = civilizationId, Name = $"F{id}", Role = role, Position = Vector2.Zero,
        CurrentSystemId = system, IsActive = active, Combat = combat,
    };
    static MassiveCombatLoadout customTacticalLoadout()
    {
        var result = new MassiveCombatLoadout { ShieldPerShip = 7, ArmorPerShip = 11, HullPerShip = 13 };
        result.Weapons.Add(new() { Id = "cache-beam", Kind = MassiveWeaponKind.Beam, DamagePerShot = 3, ShotsPerSecond = 2, Accuracy = .75f });
        return result;
    }
    static GalaxyState Galaxy(IList<FleetState> fleets, IList<CivilizationState> civilizations) => new()
    {
        Seed = 1, Systems = Array.Empty<StarSystemState>(), Civilizations = civilizations, Fleets = fleets,
        Colonies = Array.Empty<ColonyState>(), Economies = Array.Empty<CivilizationEconomyState>(),
        Technologies = Array.Empty<TechnologyState>(), ConstructionStates = Array.Empty<ConstructionState>(),
        ShipyardStates = Array.Empty<ShipyardState>(), PlayerCivilizationId = 1, Knowledge = new CivilizationKnowledgeState(),
    };

    static JsonObject WorldJson(GalaxyState galaxy) => new()
    {
        ["CivilizationIds"] = new JsonArray(galaxy.Civilizations.Select(x => JsonValue.Create(x.Id)).ToArray()),
        ["Fleets"] = new JsonArray(galaxy.Fleets.Select(FleetJson).ToArray()),
        ["CombatIntelligence"] = new JsonArray(galaxy.CombatIntelligence.Select(ObservationJson).ToArray()),
    };
    static JsonObject FleetJson(FleetState fleet) => new()
    {
        ["Id"] = fleet.Id, ["CivilizationId"] = fleet.CivilizationId, ["Role"] = (int)fleet.Role,
        ["CurrentSystemId"] = fleet.CurrentSystemId, ["IsActive"] = fleet.IsActive,
        ["Combat"] = fleet.Combat is null ? null : new JsonObject { ["ProfileId"] = fleet.Combat.ProfileId, ["Shields"] = D(fleet.Combat.Shields), ["Armor"] = D(fleet.Combat.Armor), ["Hull"] = D(fleet.Combat.Hull) },
        ["TacticalLoadout"] = fleet.TacticalLoadout is null ? null : LoadoutJson(fleet.TacticalLoadout),
    };
    static JsonObject ObservationJson(FleetPowerObservation value) => new() { ["ObserverId"] = value.ObserverId, ["FleetId"] = value.FleetId, ["Power"] = D(value.Power), ["ObservedDay"] = D(value.ObservedDay), ["Evidence"] = value.Evidence };
    static JsonObject LoadoutJson(MassiveCombatLoadout loadout) => new()
    {
        ["MassPerShip"] = D(loadout.MassPerShip), ["Acceleration"] = D(loadout.Acceleration), ["MaximumSpeed"] = D(loadout.MaximumSpeed),
        ["ShieldPerShip"] = D(loadout.ShieldPerShip), ["ArmorPerShip"] = D(loadout.ArmorPerShip), ["HullPerShip"] = D(loadout.HullPerShip),
        ["ReactorOutputPerShip"] = D(loadout.ReactorOutputPerShip), ["CoolingPerShip"] = D(loadout.CoolingPerShip), ["WarpStabilization"] = D(loadout.WarpStabilization), ["WarpSpoolSeconds"] = D(loadout.WarpSpoolSeconds),
        ["ModuleSlotCapacity"] = loadout.ModuleSlotCapacity, ["MaximumModuleMass"] = D(loadout.MaximumModuleMass),
        ["Weapons"] = new JsonArray(loadout.Weapons.Select(x => new JsonObject { ["Id"] = x.Id, ["Kind"] = (int)x.Kind, ["MountsPerShip"] = x.MountsPerShip, ["DamagePerShot"] = D(x.DamagePerShot), ["ShotsPerSecond"] = D(x.ShotsPerSecond), ["Range"] = D(x.Range), ["Accuracy"] = D(x.Accuracy), ["PowerPerSecond"] = D(x.PowerPerSecond), ["HeatPerSecond"] = D(x.HeatPerSecond) }).ToArray()),
        ["Modules"] = new JsonArray(loadout.Modules.Select(x => new JsonObject { ["Id"] = x.Id, ["Kind"] = (int)x.Kind, ["InstalledCount"] = x.InstalledCount, ["MassEach"] = D(x.MassEach), ["PowerPerSecondEach"] = D(x.PowerPerSecondEach), ["HeatPerSecondEach"] = D(x.HeatPerSecondEach), ["Condition"] = D(x.Condition), ["Enabled"] = x.Enabled, ["EffectiveRange"] = D(x.EffectiveRange), ["FieldStrength"] = D(x.FieldStrength), ["DetectionSignature"] = D(x.DetectionSignature), ["Slots"] = x.Slots }).ToArray()),
    };

    static string SourceFingerprint()
    {
        var root = Path.GetFullPath(Environment.CurrentDirectory);
        var files = new[] { "src/Game/Simulation/Combat/FleetCombatPower.cs", "src/Game/Simulation/Combat/Massive/MassiveCombatOutcome.cs", "src/Game/Presentation/Main.MassiveCombat.cs" };
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var file in files) { var bytes = File.ReadAllBytes(Path.Combine(root, file)); hash.AppendData(bytes); }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
}
