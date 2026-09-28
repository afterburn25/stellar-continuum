using System.Diagnostics;
using System.Text.Json;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;

namespace Game.MassiveCombat.Validation;

internal static class MassiveCombatBenchmark
{
    private static readonly int[] ShipsPerSide = [10, 100, 1_000, 10_000, 50_000];
    private static readonly string[] Scenarios = ["beam", "missile", "mixed", "interdictor", "breakout"];

    public static string Run(string outputPath)
    {
        _ = Measure(100, "mixed", 4); // JIT warm-up is deliberately outside measurements.
        var results = new List<BenchmarkCase>();
        foreach (var ships in ShipsPerSide)
            foreach (var scenario in Scenarios)
                results.Add(Measure(ships, scenario, 40));

        var artifact = new BenchmarkArtifact(
            "massive-combat-benchmark-v1", DateTimeOffset.UtcNow, Environment.MachineName,
            Environment.OSVersion.ToString(), Environment.Version.ToString(), Environment.ProcessorCount,
            MassiveCombatEngine.TickSeconds, results);
        var directory = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        File.WriteAllText(outputPath, JsonSerializer.Serialize(artifact, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        }));
        return outputPath;
    }

    private static BenchmarkCase Measure(int shipsPerSide, string scenario, int ticks)
    {
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        var gen0 = GC.CollectionCount(0); var gen1 = GC.CollectionCount(1); var gen2 = GC.CollectionCount(2);
        var allocatedStart = GC.GetTotalAllocatedBytes(true);
        var initialization = Stopwatch.StartNew();
        var battle = BuildBattle(shipsPerSide, scenario);
        initialization.Stop();
        var initializationAllocated = GC.GetTotalAllocatedBytes(false) - allocatedStart;

        var engine = new MassiveCombatEngine();
        var tickTimes = new double[ticks];
        long candidates = 0, weaponGroups = 0;
        var maxCells = 0;
        var simulationAllocatedStart = GC.GetTotalAllocatedBytes(false);
        for (var tick = 0; tick < ticks; tick++)
        {
            var watch = Stopwatch.StartNew();
            var metrics = engine.Advance(battle, MassiveCombatEngine.TickSeconds);
            watch.Stop();
            tickTimes[tick] = watch.Elapsed.TotalMilliseconds;
            candidates += metrics.TargetCandidatesExamined;
            weaponGroups += metrics.WeaponGroupsResolved;
            maxCells = Math.Max(maxCells, metrics.SpatialCells);
        }
        var sorted = tickTimes.Order().ToArray();
        var simulationAllocated = GC.GetTotalAllocatedBytes(false) - simulationAllocatedStart;
        return new(shipsPerSide, checked(shipsPerSide * 2), scenario, battle.Formations.Count, ticks,
            initialization.Elapsed.TotalMilliseconds, initializationAllocated, simulationAllocated,
            tickTimes.Average(), Percentile(sorted, .95), sorted[^1],
            candidates / (double)ticks, weaponGroups / (double)ticks, maxCells,
            GC.CollectionCount(0) - gen0, GC.CollectionCount(1) - gen1, GC.CollectionCount(2) - gen2,
            battle.Formations.Sum(x => x.InitialShipCount), battle.Events.Count, battle.ActiveSalvos.Count);
    }

    private static MassiveCombatBattleState BuildBattle(int shipsPerSide, string scenario)
    {
        var formations = new List<MassiveFormationState>();
        var formationsPerSide = Math.Clamp((int)Math.Ceiling(shipsPerSide / 1_000d), 1, 50);
        for (var side = 0; side < 2; side++)
        {
            var remaining = shipsPerSide;
            for (var index = 0; index < formationsPerSide; index++)
            {
                var count = remaining / (formationsPerSide - index); remaining -= count;
                var id = side * 1_000L + index + 1;
                var loadout = Loadout(scenario);
                if (side == 1 && scenario is "interdictor" or "breakout")
                    loadout.Modules.Add(MassiveCombatLoadouts.WarpInterdictor(900, 72));
                formations.Add(new()
                {
                    Id = id, CivilizationId = side, FleetId = side * 10_000 + index + 1,
                    TaskForceId = side * 100 + index / 10, Name = $"{scenario} side {side} formation {index}",
                    Position = new(side == 0 ? -260 + index * 3 : 260 - index * 3, (index % 10 - 5) * 42),
                    Heading = new(side == 0 ? 1 : -1, 0), Objective = new(0, 0),
                    Shape = scenario == "breakout" && side == 0 ? MassiveFormationShape.Breakout : MassiveFormationShape.Line,
                    Order = scenario switch
                    {
                        "interdictor" when side == 0 => MassiveCombatOrderType.Retreat,
                        "breakout" when side == 0 => MassiveCombatOrderType.Breakout,
                        _ => MassiveCombatOrderType.Engage,
                    },
                    Loadout = loadout,
                    Cohorts = [new() { Id = id * 10, DesignId = scenario + "_line", InitialCount = count, ActiveCount = count }],
                });
            }
        }
        ulong scenarioSeed = 1469598103934665603UL;
        foreach (var character in scenario) scenarioSeed = (scenarioSeed ^ character) * 1099511628211UL;
        return MassiveCombatBattleState.Create(unchecked((ulong)shipsPerSide * 101UL + scenarioSeed), formations);
    }

    private static MassiveCombatLoadout Loadout(string scenario)
    {
        var profile = new CombatProfileDefinition("benchmark_" + scenario, 35, 45, 95, 1.2, .2, 1);
        var value = MassiveCombatLoadouts.FromLegacy(profile);
        value.Acceleration = 20; value.MaximumSpeed = 100; value.ReactorOutputPerShip = 120; value.CoolingPerShip = 24;
        value.Weapons[0].Kind = scenario == "missile" ? MassiveWeaponKind.Missile : MassiveWeaponKind.Beam;
        if (scenario == "mixed" || scenario == "breakout")
        {
            value.Weapons.Add(new() { Id = MassiveEquipmentIds.KineticBattery, Kind = MassiveWeaponKind.Kinetic, DamagePerShot = 1, Range = 650, Accuracy = .62f });
            value.Weapons.Add(new() { Id = MassiveEquipmentIds.MissileBattery, Kind = MassiveWeaponKind.Missile, DamagePerShot = 1.5f, ShotsPerSecond = .5f, Range = 900, Accuracy = .7f });
            value.Weapons.Add(new() { Id = MassiveEquipmentIds.PointDefense, Kind = MassiveWeaponKind.PointDefense, DamagePerShot = 0, ShotsPerSecond = 1.5f, Range = 450, Accuracy = .75f });
            value.Weapons.Add(new() { Id = MassiveEquipmentIds.ElectronicWarfare, Kind = MassiveWeaponKind.ElectronicWarfare, DamagePerShot = 8, ShotsPerSecond = 1, Range = 800, Accuracy = 1 });
        }
        return value;
    }

    private static double Percentile(double[] sorted, double percentile) => sorted[Math.Clamp((int)Math.Ceiling(sorted.Length * percentile) - 1, 0, sorted.Length - 1)];

    private sealed record BenchmarkArtifact(string Schema, DateTimeOffset RecordedUtc, string Machine, string OperatingSystem,
        string Runtime, int LogicalProcessors, double FixedTickSeconds, IReadOnlyList<BenchmarkCase> Cases);
    private sealed record BenchmarkCase(int ShipsPerSide, int TotalShips, string Scenario, int FormationCount, int MeasuredTicks,
        double InitializationMilliseconds, long InitializationAllocatedBytes, long SimulationAllocatedBytes,
        double AverageTickMilliseconds, double P95TickMilliseconds, double WorstTickMilliseconds,
        double AverageTargetCandidates, double AverageWeaponGroups, int MaximumActiveCells,
        int Gen0Collections, int Gen1Collections, int Gen2Collections,
        int ExactInitialShips, int RetainedEvents, int ActiveSalvos);
}
