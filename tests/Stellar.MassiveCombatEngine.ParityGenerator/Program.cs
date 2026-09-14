using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Combat.Massive;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        PropertyNamingPolicy = null,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };

    private sealed record Row(
        string Name,
        string Operation,
        JsonNode Input,
        JsonNode? Before,
        JsonNode? After,
        JsonNode? Result,
        string? ErrorType,
        string? ErrorMessage,
        string? ErrorParameter);

    private sealed class Hostility(params (int First, int Second)[] pairs)
        : IMassiveCombatHostilityView
    {
        private readonly HashSet<(int, int)> _pairs = pairs.ToHashSet();
        public bool AreHostile(int firstCivilizationId, int secondCivilizationId) =>
            _pairs.Contains((firstCivilizationId, secondCivilizationId));
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine("Usage: MassiveCombatEngineOracle <fixture-path> <source-root>");
            return 1;
        }

        var output = Path.GetFullPath(args[0]);
        var sourceRoot = Path.GetFullPath(args[1]);
        try
        {
            var rows = BuildRows();
            var sourceFiles = new[]
            {
                "Simulation/Combat/Massive/MassiveCombatEngine.cs",
                "Simulation/Combat/Massive/MassiveCombatClock.cs",
                "Simulation/Combat/Massive/MassiveCombatContracts.cs",
                "Simulation/Combat/Massive/MassiveCombatState.cs",
                "Simulation/Combat/Massive/MassiveCombatEquipment.cs",
            };
            var fingerprints = sourceFiles.Select(path => new
            {
                Path = path.Replace('\\', '/'),
                Sha256 = Hash(File.ReadAllBytes(Path.Combine(sourceRoot, path))),
            }).ToArray();
            var document = new
            {
                SchemaVersion = 1,
                Authority = "actual C# source",
                SourceFiles = fingerprints,
                RowCount = rows.Count,
                SourceOnlyRows = 0,
                Rows = rows,
            };
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            File.WriteAllText(output, JsonSerializer.Serialize(document,
                new JsonSerializerOptions(Json) { WriteIndented = true }) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"Massive combat engine source oracle: {rows.Count}/{rows.Count} rows written to {output}");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"Source root: {sourceRoot}");
            Console.Error.WriteLine($"Fixture path: {output}");
            return 1;
        }
    }

    private static List<Row> BuildRows()
    {
        var rows = new List<Row>();
        AddClockRows(rows);
        AddHostilityRows(rows);
        AddOrderRows(rows);
        AddAdvanceRows(rows);
        AddContinuationRow(rows);
        AddMetricsRetentionRow(rows);
        return rows;
    }

    private static void AddClockRows(List<Row> rows)
    {
        foreach (var speed in MassiveCombatClock.AllowedSpeeds)
        {
            var clock = new MassiveCombatClock();
            AddCall(rows, $"clock-speed-{speed:R}", "Clock", new { Speed = speed, Real = .4, Maximum = .25 },
                before: new { clock.SpeedMultiplier },
                call: () =>
                {
                    clock.SetSpeed(speed);
                    return new { Accepted = clock.AcceptFrame(.4, .25), clock.SpeedMultiplier };
                },
                after: () => new { clock.SpeedMultiplier });
        }
        AddClockFailure(rows, "clock-invalid-speed", 3, .1, .25);
        AddClockFailure(rows, "clock-nan-speed", double.NaN, .1, .25);
        AddClockFailure(rows, "clock-negative-frame", 1, -.1, .25);
        AddClockFailure(rows, "clock-zero-maximum", 1, .1, 0);
        AddClockFailure(rows, "clock-nan-maximum", 1, .1, double.NaN);
    }

    private static void AddClockFailure(List<Row> rows, string name, double speed,
        double real, double maximum)
    {
        var clock = new MassiveCombatClock();
        AddCall(rows, name, "Clock", new { Speed = speed, Real = real, Maximum = maximum },
            before: new { clock.SpeedMultiplier },
            call: () =>
            {
                clock.SetSpeed(speed);
                return new { Accepted = clock.AcceptFrame(real, maximum), clock.SpeedMultiplier };
            },
            after: () => new { clock.SpeedMultiplier });
    }

    private static void AddHostilityRows(List<Row> rows)
    {
        var directional = new Hostility((1, 2));
        AddBattleCall(rows, "hostility-forward-only", "HasActiveHostilities", Battle(),
            new { Pairs = new[] { new[] { 1, 2 } } },
            battle => new MassiveCombatEngine(directional).HasActiveHostilities(battle));
        var reverse = new Hostility((2, 1));
        AddBattleCall(rows, "hostility-reverse-only", "HasActiveHostilities", Battle(),
            new { Pairs = new[] { new[] { 2, 1 } } },
            battle => new MassiveCombatEngine(reverse).HasActiveHostilities(battle));
        var none = new Hostility();
        AddBattleCall(rows, "hostility-none", "HasActiveHostilities", Battle(),
            new { Pairs = Array.Empty<int[]>() },
            battle => new MassiveCombatEngine(none).HasActiveHostilities(battle));
        var oneCivilization = Battle();
        foreach (var formation in oneCivilization.Formations)
            formation.CivilizationId = 1;
        AddBattleCall(rows, "hostility-one-distinct-civilization", "HasActiveHostilities",
            oneCivilization, new { Default = true },
            battle => new MassiveCombatEngine().HasActiveHostilities(battle));
    }

    private static void AddOrderRows(List<Row> rows)
    {
        AddOrder(rows, "order-invalid-type", 1,
            new(1, (MassiveCombatOrderType)99));
        AddOrder(rows, "order-invalid-shape", 1,
            new(1, MassiveCombatOrderType.Hold, Shape: (MassiveFormationShape)99));
        AddOrder(rows, "order-nonfinite-objective", 1,
            new(1, MassiveCombatOrderType.Advance, Objective: new(float.NaN, 0)));
        AddOrder(rows, "order-missing-owned-formation", 1,
            new(999, MassiveCombatOrderType.Hold));
        AddOrder(rows, "order-wrong-owner", 1,
            new(2, MassiveCombatOrderType.Hold));
        AddOrder(rows, "order-target-missing", 1,
            new(1, MassiveCombatOrderType.Engage, 999));
        AddOrder(rows, "order-nonhostile-target", 1,
            new(1, MassiveCombatOrderType.Engage, 3), new Hostility((1, 2), (2, 1)));
        AddOrder(rows, "order-protect-hostile", 1,
            new(1, MassiveCombatOrderType.ProtectCriticalAsset, 2));
        AddOrder(rows, "order-requires-target", 1,
            new(1, MassiveCombatOrderType.FocusFire));
        AddOrder(rows, "order-protect-friendly", 1,
            new(1, MassiveCombatOrderType.ProtectCriticalAsset, 3));
        AddOrder(rows, "order-withdraw-starts-warp", 1,
            new(1, MassiveCombatOrderType.Retreat, Objective: new(-1000, 25),
                Shape: MassiveFormationShape.Dispersed));
        AddOrder(rows, "order-surrender", 1,
            new(1, MassiveCombatOrderType.Surrender));
    }

    private static void AddOrder(List<Row> rows, string name, int civilization,
        MassiveCombatOrder order, IMassiveCombatHostilityView? hostility = null)
    {
        var battle = Battle();
        if (name == "order-withdraw-starts-warp")
            battle.Formations[0].WarpSpoolProgress = 0;
        var input = new
        {
            CivilizationId = civilization,
            Order = new
            {
                order.FormationId,
                order.Type,
                order.TargetFormationId,
                Objective = order.Objective is { } objective
                    ? new MassivePoint(objective.X, objective.Y)
                    : (MassivePoint?)null,
                order.Shape,
            },
        };
        AddBattleCall(rows, name, "IssueOrder", battle,
            input,
            value => new MassiveCombatEngine(hostility).IssueOrder(value, civilization, order));
    }

    private static void AddAdvanceRows(List<Row> rows)
    {
        AddAdvance(rows, "advance-zero", Battle(), 0);
        AddAdvance(rows, "advance-less-than-tick", Battle(), .049);
        AddAdvance(rows, "advance-pending-crosses-tick", Battle(.075), .025);
        AddAdvance(rows, "advance-exact-multitick", Battle(), .5);
        AddAdvance(rows, "advance-negative", Battle(), -.1);
        AddAdvance(rows, "advance-nan", Battle(), double.NaN);

        var invalid = Battle();
        invalid.Formations[0].Cohesion = float.NaN;
        AddAdvance(rows, "advance-validates-before-pending-mutation", invalid, .2);

        var catchUp = Battle();
        AddAdvance(rows, "advance-catchup-cap-retains-backlog", catchUp, 100);
        AddAdvance(rows, "advance-overflowed-pending-is-retained", Battle(), double.MaxValue);

        var arrival = Battle();
        arrival.ActiveSalvos[0].RemainingSeconds = .1f;
        AddAdvance(rows, "advance-inflight-salvo-arrives", arrival, .1);

        var warp = Battle();
        var first = warp.Formations[0];
        first.Order = MassiveCombatOrderType.EmergencyRetreat;
        first.WarpSpoolProgress = .99f;
        first.TargetFormationId = 2;
        AddAdvance(rows, "advance-warp-escape", warp, .1);

        var blocked = Battle();
        var firstBlocked = blocked.Formations[0];
        firstBlocked.Order = MassiveCombatOrderType.Retreat;
        firstBlocked.WarpSpoolProgress = .95f;
        blocked.Formations[1].Position = firstBlocked.Position;
        AddAdvance(rows, "advance-warp-interdiction", blocked, .1);

        var damaged = Battle();
        damaged.Formations[1].ShieldPool = 0;
        damaged.Formations[1].ArmorPool = 0;
        damaged.Formations[1].HullPool = 30;
        damaged.Formations[1].HullDamageRemainder = 90;
        AddAdvance(rows, "advance-damaged-cohort-and-vessel", damaged, .1);

        var hugeCoordinates = Battle();
        foreach (var formation in hugeCoordinates.Formations)
            formation.Position = new(float.MaxValue, -float.MaxValue);
        AddAdvance(rows, "advance-finite-huge-coordinate-wrap", hugeCoordinates, .1);

        var protectedAssetWithoutNearbyEnemy = Battle();
        var escort = protectedAssetWithoutNearbyEnemy.Formations.Single(x => x.Id == 1);
        var enemy = protectedAssetWithoutNearbyEnemy.Formations.Single(x => x.Id == 2);
        var asset = protectedAssetWithoutNearbyEnemy.Formations.Single(x => x.Id == 3);
        escort.Order = MassiveCombatOrderType.ProtectCriticalAsset;
        escort.ProtectedFormationId = asset.Id;
        escort.TargetFormationId = enemy.Id;
        escort.Position = new(0, 0);
        enemy.Position = new(100, 0);
        asset.Position = new(100_000, 0);
        protectedAssetWithoutNearbyEnemy.ActiveSalvos.Clear();
        AddAdvance(rows, "advance-protected-asset-empty-neighborhood-does-not-fallback",
            protectedAssetWithoutNearbyEnemy, .1);

        var fractionalProtection = Battle();
        var fractionalEscort = fractionalProtection.Formations.Single(x => x.Id == 1);
        var fractionalEnemy = fractionalProtection.Formations.Single(x => x.Id == 2);
        var fractionalAsset = fractionalProtection.Formations.Single(x => x.Id == 3);
        fractionalEscort.Order = MassiveCombatOrderType.ProtectCriticalAsset;
        fractionalEscort.ProtectedFormationId = fractionalAsset.Id;
        fractionalEscort.TargetFormationId = fractionalEnemy.Id;
        fractionalEscort.Position = new(10.25f, -3.75f);
        fractionalAsset.Position = new(420.5f, 88.125f);
        fractionalEnemy.Position = new(450.75f, 113.625f);
        fractionalProtection.ActiveSalvos.Clear();
        AddAdvance(rows, "advance-protected-fractional-non-axis-movement",
            fractionalProtection, .1);
    }

    private static void AddAdvance(List<Row> rows, string name,
        MassiveCombatBattleState battle, double elapsed)
    {
        AddBattleCall(rows, name, "Advance", battle, new { ElapsedSeconds = elapsed },
            value => new MassiveCombatEngine(new Hostility((1, 2), (2, 1)))
                .Advance(value, elapsed));
    }

    private static void AddContinuationRow(List<Row> rows)
    {
        var battle = Battle(.06);
        battle.Formations[0].Heat = 16;
        battle.Formations[0].ShieldPool = 5;
        battle.Formations[0].HullDamageRemainder = 70;
        battle.Formations[0].ImportantVessels[0].HullFraction = .63f;
        battle.Formations[0].Order = MassiveCombatOrderType.Retreat;
        battle.Formations[0].WarpSpoolProgress = .35f;
        battle.ActiveSalvos[0].RemainingSeconds = .24f;
        var serialized = JsonSerializer.Serialize(battle, Json);
        var restored = JsonSerializer.Deserialize<MassiveCombatBattleState>(serialized, Json)!;
        var engine = new MassiveCombatEngine(new Hostility((1, 2), (2, 1)));
        var before = Node(restored);
        var input = Node(new { Serialized = serialized, Steps = new[] { .04, .2, .35, .6 } });
        JsonNode? result = null;
        string? errorType = null;
        string? errorMessage = null;
        string? errorParameter = null;
        try
        {
            var results = new List<MassiveCombatMetrics>();
            foreach (var step in new[] { .04, .2, .35, .6 })
                results.Add(engine.Advance(restored, step));
            result = Node(results);
        }
        catch (Exception exception)
        {
            CaptureError(exception, out errorType, out errorMessage, out errorParameter);
        }
        rows.Add(new("saved-battle-multitick-continuation", "Continuation", input,
            before, Node(restored), result, errorType, errorMessage, errorParameter));
    }

    private static void AddMetricsRetentionRow(List<Row> rows)
    {
        var battle = Battle();
        var engine = new MassiveCombatEngine(new Hostility((1, 2), (2, 1)));
        var before = Node(battle);
        var first = engine.Advance(battle, 0);
        battle.Formations[0].InitialShipCount = 0;
        battle.Formations[1].Cohesion = float.NaN;
        string? errorType = null;
        string? errorMessage = null;
        string? errorParameter = null;
        try
        {
            engine.Advance(battle, .1);
        }
        catch (Exception exception)
        {
            CaptureError(exception, out errorType, out errorMessage, out errorParameter);
        }
        rows.Add(new("advance-failure-retains-previous-metrics", "MetricsRetention",
            Node(new
            {
                ElapsedSeconds = .1,
                InitialShipCountFormationIndex = 0,
                InvalidCohesionFormationIndex = 1,
            }), before, Node(battle),
            Node(new
            {
                Previous = first,
                Current = battle.LastMetrics,
                Stable = first == battle.LastMetrics,
            }), errorType, errorMessage, errorParameter));
    }

    private static void AddBattleCall<T>(List<Row> rows, string name, string operation,
        MassiveCombatBattleState battle, object input, Func<MassiveCombatBattleState, T> call)
    {
        var before = Node(battle);
        JsonNode? result = null;
        string? errorType = null;
        string? errorMessage = null;
        string? errorParameter = null;
        try
        {
            result = Node(call(battle));
        }
        catch (Exception exception)
        {
            CaptureError(exception, out errorType, out errorMessage, out errorParameter);
        }
        rows.Add(new(name, operation, Node(input), before, Node(battle), result,
            errorType, errorMessage, errorParameter));
    }

    private static void AddCall<TBefore, TResult, TAfter>(List<Row> rows,
        string name, string operation, object input, TBefore before,
        Func<TResult> call, Func<TAfter> after)
    {
        JsonNode? result = null;
        string? errorType = null;
        string? errorMessage = null;
        string? errorParameter = null;
        try
        {
            result = Node(call());
        }
        catch (Exception exception)
        {
            CaptureError(exception, out errorType, out errorMessage, out errorParameter);
        }
        rows.Add(new(name, operation, Node(input), Node(before), Node(after()), result,
            errorType, errorMessage, errorParameter));
    }

    private static void CaptureError(Exception exception, out string type,
        out string message, out string? parameter)
    {
        type = exception.GetType().Name;
        message = exception.Message;
        parameter = (exception as ArgumentException)?.ParamName;
    }

    private static MassiveCombatBattleState Battle(double pending = 0)
    {
        var alpha = Formation(1, 1, "Alpha", 0, BeamMissileLoadout());
        alpha.Cohorts.Add(new MassiveCohortState
        {
            Id = 11,
            DesignId = "alpha-line",
            InitialCount = 3,
            ActiveCount = 3,
            Experience = .8f,
        });
        alpha.ImportantVessels.Add(Vessel(101, "Alpha Prime", "alpha-command", true));
        alpha.InitialShipCount = 4;

        var beta = Formation(2, 2, "Beta", 450, PointDefenseLoadout());
        beta.Cohorts.Add(new MassiveCohortState
        {
            Id = 21,
            DesignId = "beta-line",
            InitialCount = 4,
            ActiveCount = 3,
            Experience = .55f,
        });
        beta.ImportantVessels.Add(Vessel(201, "Beta Spear", "beta-command", false));
        beta.InitialShipCount = 5;
        beta.DestroyedShips = 1;
        beta.Cohesion = .72f;
        beta.Morale = .61f;
        beta.HullDamageRemainder = 12.5f;

        var gamma = Formation(3, 1, "Gamma", -250, BeamMissileLoadout());
        gamma.Cohorts.Add(new MassiveCohortState
        {
            Id = 31,
            DesignId = "gamma-screen",
            InitialCount = 2,
            ActiveCount = 2,
            Experience = .4f,
        });
        gamma.InitialShipCount = 2;
        gamma.Order = MassiveCombatOrderType.Screen;
        gamma.TargetFormationId = null;

        return new MassiveCombatBattleState
        {
            BattleId = new Guid("00112233-4455-6677-8899-aabbccddeeff"),
            Seed = 0x1234_5678_9ABC_DEF0,
            Tick = 9,
            SimulatedSeconds = .9,
            PendingSeconds = pending,
            NextEventSequence = 3,
            NextSalvoId = 8,
            Formations = new() { beta, gamma, alpha },
            Events = new()
            {
                new(1, 8, MassiveCombatEventType.Engagement, 1, 1, 2, 2, 0,
                    new(0, 0), "engaged"),
                new(2, 9, MassiveCombatEventType.Damage, 1, 1, 2, 2, 1,
                    new(450, 0), "opening damage"),
            },
            ActiveSalvos = new()
            {
                new MassiveMissileSalvoState
                {
                    Id = 7,
                    SourceFormationId = 1,
                    TargetFormationId = 2,
                    MissileCount = 3,
                    Damage = 28,
                    RemainingSeconds = .35f,
                    LaunchPosition = new(0, 0),
                    InitialFlightSeconds = .6f,
                },
            },
        };
    }

    private static MassiveFormationState Formation(long id, int civilizationId,
        string name, float x, MassiveCombatLoadout loadout) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        FleetId = checked((int)(100 + id)),
        TaskForceId = checked((int)(200 + id)),
        Name = name,
        Position = new(x, id * 15),
        Velocity = new(id == 2 ? -2 : 3, 0),
        Heading = new(1, 0),
        Objective = new(500, 0),
        Shape = MassiveFormationShape.Line,
        Order = MassiveCombatOrderType.Engage,
        TargetFormationId = id == 2 ? 1 : 2,
        Cohesion = .9f,
        Morale = .85f,
        ShieldPool = loadout.ShieldPerShip * 4,
        ArmorPool = loadout.ArmorPerShip * 4,
        HullPool = loadout.HullPerShip * 4,
        HullLossThresholdPerShip = loadout.HullPerShip,
        Heat = 3,
        PowerReserve = .8f,
        Loadout = loadout,
    };

    private static MassiveVesselState Vessel(long id, string name, string design,
        bool flagship) => new()
    {
        Id = id,
        Name = name,
        DesignId = design,
        IsFlagship = flagship,
        IsCarrier = !flagship,
        HullFraction = .73f,
        EngineFraction = .82f,
        SensorFraction = .91f,
        WarpDriveFraction = .76f,
        ReactorFraction = .88f,
        InterdictorFraction = .69f,
        BattlesFought = 2,
        ConfirmedKills = 1,
    };

    private static MassiveCombatLoadout BeamMissileLoadout() => new()
    {
        MassPerShip = 120,
        Acceleration = 18,
        MaximumSpeed = 110,
        ShieldPerShip = 30,
        ArmorPerShip = 42,
        HullPerShip = 90,
        ReactorOutputPerShip = 115,
        CoolingPerShip = 25,
        WarpStabilization = 45,
        WarpSpoolSeconds = .2f,
        ModuleSlotCapacity = 8,
        MaximumModuleMass = 300,
        Weapons = new()
        {
            new MassiveWeaponGroup
            {
                Id = "beam", Kind = MassiveWeaponKind.Beam, MountsPerShip = 2,
                DamagePerShot = 15, ShotsPerSecond = 2, Range = 800,
                Accuracy = .75f, PowerPerSecond = 8, HeatPerSecond = 3,
            },
            new MassiveWeaponGroup
            {
                Id = "missile", Kind = MassiveWeaponKind.Missile, MountsPerShip = 1,
                DamagePerShot = 24, ShotsPerSecond = 1, Range = 1200,
                Accuracy = .68f, PowerPerSecond = 5, HeatPerSecond = 2,
            },
        },
        Modules = new()
        {
            Module("reactor", MassiveModuleKind.Reactor, 1),
            Module("engine", MassiveModuleKind.Engine, 1),
            Module("control", MassiveModuleKind.WeaponControl, 1),
            new MassiveModuleState
            {
                Id = "interdictor", Kind = MassiveModuleKind.WarpInterdictor,
                InstalledCount = 1, MassEach = 20, PowerPerSecondEach = 4,
                HeatPerSecondEach = 1, Condition = .85f, Enabled = true,
                EffectiveRange = 1000, FieldStrength = 100,
                DetectionSignature = 5, Slots = 1,
            },
        },
    };

    private static MassiveCombatLoadout PointDefenseLoadout() => new()
    {
        MassPerShip = 105,
        Acceleration = 16,
        MaximumSpeed = 100,
        ShieldPerShip = 28,
        ArmorPerShip = 45,
        HullPerShip = 100,
        ReactorOutputPerShip = 108,
        CoolingPerShip = 24,
        WarpStabilization = 90,
        WarpSpoolSeconds = .3f,
        ModuleSlotCapacity = 8,
        MaximumModuleMass = 300,
        Weapons = new()
        {
            new MassiveWeaponGroup
            {
                Id = "kinetic", Kind = MassiveWeaponKind.Kinetic, MountsPerShip = 2,
                DamagePerShot = 13, ShotsPerSecond = 2, Range = 750,
                Accuracy = .7f, PowerPerSecond = 7, HeatPerSecond = 3,
            },
            new MassiveWeaponGroup
            {
                Id = "pd", Kind = MassiveWeaponKind.PointDefense, MountsPerShip = 2,
                DamagePerShot = 1, ShotsPerSecond = 12, Range = 500,
                Accuracy = .8f, PowerPerSecond = 4, HeatPerSecond = 1,
            },
        },
        Modules = new()
        {
            Module("reactor", MassiveModuleKind.Reactor, 1),
            Module("engine", MassiveModuleKind.Engine, 1),
            Module("warp", MassiveModuleKind.WarpDrive, 1),
        },
    };

    private static MassiveModuleState Module(string id, MassiveModuleKind kind,
        int count) => new()
    {
        Id = id,
        Kind = kind,
        InstalledCount = count,
        MassEach = 10,
        PowerPerSecondEach = 2,
        HeatPerSecondEach = 1,
        Condition = .9f,
        Enabled = true,
        Slots = 1,
    };

    private static JsonNode Node<T>(T value) =>
        JsonSerializer.SerializeToNode(value, Json)!;

    private static string Hash(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes));
}
