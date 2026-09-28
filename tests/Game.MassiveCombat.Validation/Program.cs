using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;

namespace Game.MassiveCombat.Validation;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = false };

    private static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--benchmark")
        {
            var output = args.Length > 1 ? args[1] : Path.Combine("tests", "Game.MassiveCombat.Validation", "Artifacts", "massive-combat-benchmark.json");
            Console.WriteLine("Benchmark artifact: " + MassiveCombatBenchmark.Run(output));
            return 0;
        }
        var tests = new (string Name, Action Run)[]
        {
            ("fixed tick determinism and save roundtrip", DeterminismAndSave),
            ("presentation budgets cannot alter authoritative state", PresentationIndependence),
            ("100,000 ships use bounded formation work", MassiveBenchmark),
            ("interdiction blocks spool until a real module drops", InterdictionAndEscape),
            ("persistent damage lowers power and preserves important identity", DamagePowerAndIdentity),
            ("point defense and persisted salvos resolve authoritative damage", PointDefenseAndSalvos),
            ("destroyed escaped and surrendered outcomes conserve ships", OutcomeConservation),
            ("observer snapshot hides unauthorized enemy power", ObserverSafety),
            ("observer events sanitize unidentified attackers", ObserverEventSafety),
            ("observer headings and missile tracks disclose only established contacts", ObserverManeuverAndMissileSafety),
            ("observer impact positions and snapshots remain safe after state changes", ObserverImpactAndSnapshotImmutability),
            ("equipment enforces slot and mass budgets", EquipmentBudgets),
            ("orders validate ownership and protection targets", OrderAuthority),
            ("doctrine uses observer-safe interdiction evidence", DoctrineUsesSnapshot),
            ("live ceasefire stops targeting and exposes encounter completion", CeasefireStopsCombat),
            ("events and catch-up remain bounded", BoundedRuntime),
            ("tactical clock exposes bounded real-time speeds", TacticalClock),
        };
        var failures = 0;
        foreach (var test in tests)
        {
            try { test.Run(); Console.WriteLine($"PASS: {test.Name}"); }
            catch (Exception exception) { failures++; Console.Error.WriteLine($"FAIL: {test.Name}\n{exception}"); }
        }
        Console.WriteLine($"Massive combat validation: {tests.Length - failures}/{tests.Length} passed.");
        return failures == 0 ? 0 : 1;
    }

    private static void DeterminismAndSave()
    {
        var original = Duel(1_000, 0xD371UL);
        var engine = new MassiveCombatEngine();
        Require(engine.IssueOrder(original, 0, new(1, MassiveCombatOrderType.Engage, 2)).Accepted, "engage rejected");
        Require(engine.IssueOrder(original, 1, new(2, MassiveCombatOrderType.Engage, 1)).Accepted, "return engage rejected");
        var serialized = JsonSerializer.Serialize(original, Json);
        var whole = JsonSerializer.Deserialize<MassiveCombatBattleState>(serialized, Json)!;
        var sliced = JsonSerializer.Deserialize<MassiveCombatBattleState>(serialized, Json)!;
        engine.Advance(whole, 10);
        for (var i = 0; i < 100; i++) engine.Advance(sliced, .1);
        Require(JsonSerializer.Serialize(whole, Json) == JsonSerializer.Serialize(sliced, Json), "render-frame slicing changed authoritative state");

        var jittered = JsonSerializer.Deserialize<MassiveCombatBattleState>(serialized, Json)!;
        for (var cycle = 0; cycle < 100; cycle++)
            foreach (var slice in new[] { .016, .027, .004, .053 }) engine.Advance(jittered, slice);
        Require(StateHash(whole) == StateHash(jittered), "uneven render timing changed the fixed-tick result");

        var pending = Duel(5, 77); engine.Advance(pending, .07);
        var loaded = JsonSerializer.Deserialize<MassiveCombatBattleState>(JsonSerializer.Serialize(pending, Json), Json)!;
        engine.Advance(loaded, .03);
        Require(loaded.Tick == 1 && loaded.PendingSeconds == 0, "save lost the fixed-tick accumulator");
        Require(loaded.Formations[0].Position.X != 0 || loaded.Formations[0].Position.Y == 0, "serialized coordinates were not restored");

        var missileBattle = Duel(20, 78);
        missileBattle.Formations[0].Loadout.Weapons[0].Kind = MassiveWeaponKind.Missile;
        Require(engine.IssueOrder(missileBattle, 0, new(1, MassiveCombatOrderType.Engage, 2)).Accepted, "missile engage rejected");
        engine.Advance(missileBattle, .1);
        Require(missileBattle.ActiveSalvos.Count > 0, "missiles did not create persistent in-flight salvos");
        var missileLoaded = JsonSerializer.Deserialize<MassiveCombatBattleState>(JsonSerializer.Serialize(missileBattle, Json), Json)!;
        Require(missileLoaded.ActiveSalvos.Count == missileBattle.ActiveSalvos.Count && missileLoaded.NextSalvoId == missileBattle.NextSalvoId &&
                missileLoaded.ActiveSalvos[0].LaunchPosition == missileBattle.ActiveSalvos[0].LaunchPosition &&
                missileLoaded.ActiveSalvos[0].InitialFlightSeconds == missileBattle.ActiveSalvos[0].InitialFlightSeconds,
            "save lost bounded active missile salvos or their launch timing");
    }

    private static void PresentationIndependence()
    {
        var source = Duel(2_000, 0x5151); var engine = new MassiveCombatEngine();
        engine.IssueOrder(source, 0, new(1, MassiveCombatOrderType.Engage, 2));
        engine.IssueOrder(source, 1, new(2, MassiveCombatOrderType.Engage, 1));
        var headless = Clone(source);
        var lowPresentation = Clone(source);
        var ultraPresentation = Clone(source);
        for (var tick = 0; tick < 50; tick++)
        {
            engine.Advance(headless, .1);
            engine.Advance(lowPresentation, .1);
            engine.Advance(ultraPresentation, .1);
            if (tick % 10 == 0) _ = MassiveCombatObserver.BuildSnapshot(lowPresentation, 0, new ZeroSensors());
            for (var frame = 0; frame < 4; frame++) _ = MassiveCombatObserver.BuildSnapshot(ultraPresentation, 0, new FullSensors());
        }
        Require(StateHash(headless) == StateHash(lowPresentation) && StateHash(headless) == StateHash(ultraPresentation),
            "headless, low, and ultra presentation budgets produced different authoritative hashes");
    }

    private static void MassiveBenchmark()
    {
        var formations = new List<MassiveFormationState>();
        for (var side = 0; side < 2; side++)
            for (var i = 0; i < 50; i++)
                formations.Add(Formation(side * 100 + i + 1, side, side * 10 + i / 10, side * 1000 + i / 5,
                    $"Side {side} Formation {i}", 1_000, new(side == 0 ? -300 + i * 4 : 300 - i * 4, (i % 10) * 35), BasicLoadout(.12f)));
        var battle = MassiveCombatBattleState.Create(0x100000UL, formations);
        foreach (var formation in battle.Formations) formation.Order = MassiveCombatOrderType.Advance;
        var stopwatch = Stopwatch.StartNew();
        var metrics = new MassiveCombatEngine().Advance(battle, 10); stopwatch.Stop();
        Require(battle.Formations.Sum(x => x.InitialShipCount) == 100_000, "stress battle did not authoritatively contain 100,000 ships");
        Require(metrics.ActiveFormations <= 100 && metrics.TargetCandidatesExamined < 4_000_000, "targeting work was not bounded by the spatial formation index");
        Require(battle.Events.Count <= MassiveCombatLimits.MaxRetainedEvents, "stress events exceeded their cap");
        Console.WriteLine($"BENCHMARK massive: {stopwatch.Elapsed.TotalMilliseconds:0.0} ms, candidates={metrics.TargetCandidatesExamined:N0}, weaponGroups={metrics.WeaponGroupsResolved:N0}, machine={Environment.MachineName}/{Environment.ProcessorCount} logical CPUs");
    }

    private static void InterdictionAndEscape()
    {
        var hunter = Formation(1, 0, 1, 1, "Interdictor Guard", 20, new(0, 0), BasicLoadout(.1f));
        hunter.Loadout.Modules.Add(MassiveCombatLoadouts.WarpInterdictor(900, 72));
        hunter.ImportantVessels.Add(new() { Id = 5001, Name = "TCS Ravager", DesignId = "interdictor", IsInterdictor = true });
        hunter.Cohorts[0].InitialCount--; hunter.Cohorts[0].ActiveCount--; hunter.InitialShipCount = hunter.ActiveShipCount;
        hunter.ShieldPool = hunter.Loadout.ShieldPerShip * hunter.ActiveShipCount; hunter.ArmorPool = hunter.Loadout.ArmorPerShip * hunter.ActiveShipCount; hunter.HullPool = hunter.Loadout.HullPerShip * hunter.ActiveShipCount;
        var runnerLoadout = BasicLoadout(0); runnerLoadout.WarpSpoolSeconds = 2; runnerLoadout.WarpStabilization = 48;
        var runner = Formation(2, 1, 2, 2, "Runner", 10, new(150, 0), runnerLoadout);
        var battle = MassiveCombatBattleState.Create(5, new[] { hunter, runner });
        var engine = new MassiveCombatEngine(); engine.IssueOrder(battle, 1, new(2, MassiveCombatOrderType.Retreat));
        engine.Advance(battle, 3);
        Require(runner.WarpBlocked && !runner.Escaped && battle.Events.Any(x => x.Type == MassiveCombatEventType.WarpBlocked), "active powered interdictor did not block warp");
        hunter.Loadout.Modules.Single(x => x.Kind == MassiveModuleKind.WarpInterdictor).Condition = 0;
        engine.Advance(battle, 3);
        Require(runner.Escaped, "runner did not complete warp after the real interdiction module failed");
        Require(battle.Events.Last(x => x.Type == MassiveCombatEventType.Escaped).Magnitude == runner.SurvivingShipCount,
            "escape event lost its authoritative surviving ship count");
    }

    private static void DamagePowerAndIdentity()
    {
        var battle = Duel(200, 91);
        var named = new MassiveVesselState { Id = 9001, Name = "CSV Endurance", DesignId = "flagship", IsFlagship = true, BattlesFought = 4 };
        var own = battle.Formations[0]; own.ImportantVessels.Add(named); own.Cohorts[0].InitialCount--; own.Cohorts[0].ActiveCount--; own.InitialShipCount = own.ActiveShipCount;
        battle.Formations[1].Loadout.Weapons[0].DamagePerShot = 20;
        var before = MassiveCombatPowerCalculator.FormationPower(own);
        var engine = new MassiveCombatEngine(); engine.IssueOrder(battle, 0, new(1, MassiveCombatOrderType.Engage, 2)); engine.IssueOrder(battle, 1, new(2, MassiveCombatOrderType.Engage, 1));
        engine.Advance(battle, 8);
        var after = MassiveCombatPowerCalculator.FormationPower(own);
        Require(after < before && (own.DestroyedShips > 0 || own.HullPool < own.Loadout.HullPerShip * own.InitialShipCount), "damage did not reduce authoritative combat power");
        var copy = JsonSerializer.Deserialize<MassiveCombatBattleState>(JsonSerializer.Serialize(battle, Json), Json)!;
        Require(copy.Formations.SelectMany(x => x.ImportantVessels).Single(x => x.Id == 9001).Name == "CSV Endurance", "important vessel identity/history did not persist");
        var outcome = MassiveCombatOutcomeBuilder.Build(copy).Fleets.Single(x => x.FleetId == own.FleetId);
        Require(outcome.SurvivingShips + outcome.DestroyedShips == own.InitialShipCount, "FleetId outcome did not conserve exact ships");
    }

    private static void ObserverSafety()
    {
        var battle = Duel(100, 42); var sensors = new TestSensors(power: false);
        var first = MassiveCombatObserver.BuildSnapshot(battle, 0, sensors);
        Require(first.ExactOwnShips == 100, "snapshot exact total included hidden enemy ships");
        var enemy = first.Formations.Single(x => x.CivilizationId == 1);
        Require(enemy.StrengthLow is null && enemy.StrengthHigh is null && enemy.PerShipCombatPower is null && !enemy.IsExact, "unauthorized enemy power leaked");
        Require(enemy.Cohorts.Count == 1 && !enemy.Cohorts[0].Identified && enemy.Cohorts[0].DisplayClass == "Unidentified ships",
            "unidentified enemy composition was not collapsed into one opaque group");
        Require(first.Formations.Single(x => x.CivilizationId == 0).Cohorts is [{ Identified: true, CountLow: 100, CountHigh: 100 }],
            "own cohort composition was not exact");
        battle.Formations[1].Loadout.Weapons[0].DamagePerShot *= 1000;
        battle.Formations[1].Cohorts =
        [
            new() { Id = 421, DesignId = "hidden_scout", InitialCount = 40, ActiveCount = 40 },
            new() { Id = 422, DesignId = "hidden_carrier", InitialCount = 60, ActiveCount = 60 },
        ];
        var second = MassiveCombatObserver.BuildSnapshot(battle, 0, sensors).Formations.Single(x => x.CivilizationId == 1);
        Require(enemy.StrengthLow == second.StrengthLow && enemy.StrengthHigh == second.StrengthHigh &&
            enemy.PerShipCombatPower == second.PerShipCombatPower && enemy.ShipCountLow == second.ShipCountLow && enemy.ShipCountHigh == second.ShipCountHigh &&
            enemy.Cohorts.SequenceEqual(second.Cohorts),
            "hidden enemy power changed an unauthorized observer snapshot");
        var scanned = MassiveCombatObserver.BuildSnapshot(battle, 0, new TestSensors(power: true)).Formations.Single(x => x.CivilizationId == 1);
        Require(scanned.StrengthLow > 0 && scanned.StrengthHigh >= scanned.StrengthLow && scanned.PerShipCombatPower > 0, "authorized scan did not expose a bounded power estimate");
        var identified = MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()).Formations.Single(x => x.CivilizationId == 1);
        Require(identified.Cohorts.Count == 2 && identified.Cohorts.All(x => x.Identified) && identified.Cohorts.Sum(x => x.CountLow) == 100,
            "authorized composition scan did not expose real bounded cohort groups");
        Require(!MassiveCombatObserver.BuildSnapshot(battle, 0, new ZeroSensors()).Formations.Any(x => x.CivilizationId == 1), "zero-confidence enemy appeared in snapshot");
    }

    private static void PointDefenseAndSalvos()
    {
        MassiveCombatBattleState Battle(bool pointDefense)
        {
            var value = Duel(100, pointDefense ? 111UL : 112UL);
            value.Formations[0].Loadout.Weapons[0].Kind = MassiveWeaponKind.Missile;
            value.Formations[0].Loadout.Weapons[0].DamagePerShot = 40;
            if (pointDefense) value.Formations[1].Loadout.Weapons.Add(new()
            {
                Id = MassiveEquipmentIds.PointDefense, Kind = MassiveWeaponKind.PointDefense,
                DamagePerShot = 0, ShotsPerSecond = 2, Accuracy = .9f, Range = 500,
            });
            return value;
        }
        var engine = new MassiveCombatEngine();
        var defended = Battle(true); var exposed = Battle(false);
        foreach (var battle in new[] { defended, exposed })
        {
            engine.IssueOrder(battle, 0, new(1, MassiveCombatOrderType.Engage, 2));
            engine.Advance(battle, .1);
            Require(battle.ActiveSalvos.Count > 0, "missile attack did not persist in flight before impact");
        }
        var resumed = Clone(defended);
        engine.Advance(defended, .5); engine.Advance(resumed, .5); engine.Advance(exposed, .5);
        Require(StateHash(defended) == StateHash(resumed), "save/resume changed in-flight missile resolution");
        float Durability(MassiveCombatBattleState battle) => battle.Formations[1].ShieldPool + battle.Formations[1].ArmorPool + battle.Formations[1].HullPool;
        Require(Durability(defended) > Durability(exposed), "real point-defense equipment did not reduce missile damage");
    }

    private static void OutcomeConservation()
    {
        var escape = Duel(7, 201); escape.Formations[0].Loadout.WarpSpoolSeconds = .1f;
        var surrender = Duel(9, 202);
        var engine = new MassiveCombatEngine();
        engine.IssueOrder(escape, 0, new(1, MassiveCombatOrderType.Retreat)); engine.Advance(escape, .2);
        engine.IssueOrder(surrender, 0, new(1, MassiveCombatOrderType.Surrender));
        Require(escape.Formations[0].Escaped && surrender.Formations[0].Surrendered, "outcome setup failed");
        foreach (var formation in escape.Formations.Concat(surrender.Formations))
            Require(formation.InitialShipCount == formation.SurvivingShipCount + formation.DestroyedShips,
                "escaped or surrendered formation violated exact ship conservation");

        var destroyed = Duel(3, 203); destroyed.Formations[0].Loadout.Weapons[0].DamagePerShot = 50_000;
        engine.IssueOrder(destroyed, 0, new(1, MassiveCombatOrderType.Engage, 2)); engine.Advance(destroyed, .2);
        var victim = destroyed.Formations[1];
        Require(victim.DestroyedShips > 0 && victim.InitialShipCount == victim.SurvivingShipCount + victim.DestroyedShips,
            "combat destruction violated exact ship conservation");
    }

    private static void ObserverEventSafety()
    {
        var battle = Duel(10, 99);
        battle.Events.Add(new(1, 1, MassiveCombatEventType.MissileSalvo, 1, 2, 0, 1, 9876, new(999, 888), "Secret Raiders fired 9,876 missiles."));
        var observed = MassiveCombatObserver.BuildSnapshot(battle, 0, new TestSensors(power: false)).Events.Single();
        Require(!observed.DetailsKnown && observed.ActorFormationId is null && observed.ActorCivilizationId is null &&
            observed.Magnitude is null && observed.Position is null && !observed.Message.Contains("Secret", StringComparison.Ordinal),
            "unknown attacker details leaked through the event stream");
    }

    private static void ObserverManeuverAndMissileSafety()
    {
        var battle = Duel(10, 771);
        battle.Formations[0].Heading = new(0, 1);
        battle.ActiveSalvos.Add(new()
        {
            Id = 41, SourceFormationId = 2, TargetFormationId = 1,
            MissileCount = 120, Damage = 90, RemainingSeconds = .15f,
            LaunchPosition = battle.Formations[1].Position, InitialFlightSeconds = .3f,
        });
        battle.Formations.Add(Formation(3, 2, 30, 300, "Remote Third Party", 5, new(4000, 0), BasicLoadout(.5f)));
        battle.Formations.Add(Formation(4, 3, 40, 400, "Remote Fourth Party", 5, new(4200, 0), BasicLoadout(.5f)));
        battle.ActiveSalvos.Add(new()
        {
            Id = 42, SourceFormationId = 3, TargetFormationId = 4,
            MissileCount = 40, Damage = 30, RemainingSeconds = .2f,
            LaunchPosition = battle.Formations[2].Position, InitialFlightSeconds = .4f,
        });
        battle.ActiveSalvos.Add(new()
        {
            Id = 43, SourceFormationId = 1, TargetFormationId = 2,
            MissileCount = 20, Damage = 15, RemainingSeconds = .2f,
            LaunchPosition = battle.Formations[0].Position, InitialFlightSeconds = .3f,
        });
        battle.ActiveSalvos.Add(new()
        {
            Id = 44, SourceFormationId = 1, TargetFormationId = 2,
            MissileCount = 5, Damage = 4, RemainingSeconds = .1f,
        });
        battle.NextSalvoId = 45;
        battle.Validate();

        var hidden = MassiveCombatObserver.BuildSnapshot(battle, 0, new TestSensors(power: false));
        Require(Math.Abs(hidden.Formations.Single(x => x.FormationId == 1).HeadingRadians - MathF.PI / 2) < .0001f,
            "own formation heading was not projected in radians");
        var incoming = hidden.ActiveMissileSalvos.Single(x => x.SalvoId == 41);
        Require(incoming.IncomingToOwn && incoming.SourceFormationId is null && incoming.SourcePosition is null &&
                incoming.CurrentPosition is null && incoming.CountLow is null && incoming.CountHigh is null &&
                incoming.TargetFormationId == 1 && incoming.TargetPosition == battle.Formations[0].Position &&
                incoming.Progress01 is null && incoming.RemainingSeconds == .15f,
            "incoming fire from an unidentified source leaked its origin, path, or magnitude");
        var ownFireAtHiddenTarget = hidden.ActiveMissileSalvos.Single(x => x.SalvoId == 43);
        Require(ownFireAtHiddenTarget.SourceFormationId == 1 && ownFireAtHiddenTarget.SourcePosition is not null &&
                ownFireAtHiddenTarget.TargetFormationId is null && ownFireAtHiddenTarget.TargetPosition is null &&
                ownFireAtHiddenTarget.CurrentPosition is null && ownFireAtHiddenTarget.Progress01 is null,
            "an owned shooter disclosed the identity, position, or trajectory of its hidden target");
        Require(hidden.ActiveMissileSalvos.All(x => x.SalvoId != 42),
            "a missile exchange between two unrelated hidden contacts was disclosed");

        var identified = MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()).ActiveMissileSalvos.Single(x => x.SalvoId == 41);
        Require(identified.SourceFormationId == 2 && identified.SourcePosition == battle.Formations[1].Position &&
                identified.TargetFormationId == 1 && identified.CurrentPosition is not null &&
                identified.CountLow == 120 && identified.CountHigh == 120,
            "identified in-flight salvo omitted its real bounded trajectory");
        var initialProgress = identified.Progress01;
        battle.Formations[0].Position = new(-6000, 3000);
        battle.ActiveSalvos.Single(x => x.Id == 41).RemainingSeconds = .1f;
        var movedTarget = MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()).ActiveMissileSalvos.Single(x => x.SalvoId == 41);
        Require(initialProgress is not null && movedTarget.Progress01 is not null && movedTarget.Progress01.Value > initialProgress.Value,
            "moving a missile target made persisted flight progress stall or run backwards");
        var legacy = MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()).ActiveMissileSalvos.Single(x => x.SalvoId == 44);
        Require(legacy.Progress01 is null && legacy.CurrentPosition is null && legacy.RemainingSeconds == .1f,
            "legacy salvo state fabricated launch progress without persisted timing metadata");
    }

    private static void ObserverImpactAndSnapshotImmutability()
    {
        var battle = Duel(10, 772);
        battle.ActiveSalvos.Add(new()
        {
            Id = 51, SourceFormationId = 2, TargetFormationId = 1,
            MissileCount = 12, Damage = 9, RemainingSeconds = .2f,
            LaunchPosition = battle.Formations[1].Position, InitialFlightSeconds = .4f,
        });
        battle.NextSalvoId = 52;
        battle.Events.Add(new(1, 1, MassiveCombatEventType.Damage, 1, 2, 0, 1, 7,
            battle.Formations[1].Position, "Hidden attacker damaged the friendly line."));
        battle.NextEventSequence = 2;
        battle.Validate();

        var snapshot = MassiveCombatObserver.BuildSnapshot(battle, 0, new TestSensors(power: false));
        var safeEvent = snapshot.Events.Single();
        var safeSalvo = snapshot.ActiveMissileSalvos.Single();
        Require(!safeEvent.DetailsKnown && safeEvent.Position is null &&
                safeEvent.ImpactPosition == battle.Formations[0].Position,
            "owned impact location was missing or disclosed the hidden attacker's position");

        battle.Formations[0].Position = new(9000, 9000);
        battle.Formations[0].Heading = new(-1, 0);
        battle.Formations[1].DestroyedShips = battle.Formations[1].InitialShipCount;
        battle.ActiveSalvos[0].RemainingSeconds = 0;
        Require(snapshot.Formations.Single(x => x.FormationId == 1).Position != battle.Formations[0].Position.Vector &&
                safeEvent.ImpactPosition != battle.Formations[0].Position && safeSalvo.RemainingSeconds == .2f,
            "observer snapshot changed after authoritative formations, destruction, or salvo timers changed");

        battle.Formations[0].HullPool = 0;
        Require(MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()).ActiveMissileSalvos.All(x => x.TargetFormationId != 1),
            "observer retained an in-flight marker after its target formation was destroyed");
    }

    private static void EquipmentBudgets()
    {
        var loadout = BasicLoadout(0); loadout.ModuleSlotCapacity = 1;
        loadout.Modules.Add(MassiveCombatLoadouts.WarpInterdictor());
        RequireThrows(loadout.Validate, "slot/mass overfit loadout was accepted");
    }

    private static void OrderAuthority()
    {
        var battle = Duel(10, 3); var engine = new MassiveCombatEngine();
        Require(!engine.IssueOrder(battle, 1, new(1, MassiveCombatOrderType.Hold)).Accepted, "foreign formation accepted player order");
        Require(!engine.IssueOrder(battle, 0, new(1, MassiveCombatOrderType.ProtectCriticalAsset, 2)).Accepted, "hostile formation accepted as protected asset");
        var beforeOrder = battle.Formations[0].Order; var beforeTarget = battle.Formations[0].TargetFormationId;
        Require(!engine.IssueOrder(battle, 0, new(1, MassiveCombatOrderType.Engage, 2, new(float.NaN, 0))).Accepted,
            "nonfinite objective was accepted");
        Require(battle.Formations[0].Order == beforeOrder && battle.Formations[0].TargetFormationId == beforeTarget,
            "rejected order partially mutated authoritative state");
        var escort = Formation(3, 0, 3, 3, "Escort", 5, new(-50, 0), BasicLoadout(.1f));
        battle.Formations.Add(escort); battle.Validate();
        Require(engine.IssueOrder(battle, 0, new(3, MassiveCombatOrderType.ProtectCriticalAsset, 1, Shape: MassiveFormationShape.Escort)).Accepted, "friendly protection order rejected");
        Require(engine.IssueOrder(battle, 0, new(3, MassiveCombatOrderType.Surrender)).Accepted && escort.Surrendered,
            "explicit surrender did not preserve a reconciliable formation outcome");
    }

    private static void BoundedRuntime()
    {
        var battle = Duel(500, 8); var engine = new MassiveCombatEngine();
        battle.Formations[0].Order = battle.Formations[1].Order = MassiveCombatOrderType.Advance;
        engine.Advance(battle, 1_000);
        Require(battle.Tick == MassiveCombatLimits.MaxCatchUpTicks, "catch-up work exceeded fixed cap");
        Require(battle.PendingSeconds > 0 && battle.Events.Count <= MassiveCombatLimits.MaxRetainedEvents, "catch-up remainder or event bound was lost");
        Require(battle.ActiveSalvos.Count <= MassiveCombatLimits.MaxActiveSalvos, "active salvo state exceeded its bound");
        RequireThrows(() => engine.Advance(battle, double.NaN), "invalid time was accepted");
    }

    private static void TacticalClock()
    {
        var clock = new MassiveCombatClock();
        foreach (var speed in MassiveCombatClock.AllowedSpeeds)
        {
            clock.SetSpeed(speed);
            Require(Math.Abs(clock.AcceptFrame(1) - .25 * speed) < .000001, "tactical frame budget or speed mapping changed");
        }
        RequireThrows(() => clock.SetSpeed(3), "unsupported tactical speed was accepted");
        RequireThrows(() => clock.AcceptFrame(double.NaN), "invalid tactical frame time was accepted");
    }

    private static void DoctrineUsesSnapshot()
    {
        var battle = Duel(10, 31);
        battle.Formations[0].WarpBlocked = true;
        battle.Formations[1].Loadout.Modules.Add(MassiveCombatLoadouts.WarpInterdictor());
        var hiddenOrders = MassiveCombatDoctrine.Decide(MassiveCombatObserver.BuildSnapshot(battle, 0, new ZeroSensors()), 0);
        Require(hiddenOrders.Single().Type == MassiveCombatOrderType.EmergencyRetreat,
            "doctrine inferred a hidden enemy interdictor outside its snapshot");
        var identifiedOrders = MassiveCombatDoctrine.Decide(MassiveCombatObserver.BuildSnapshot(battle, 0, new FullSensors()), 0);
        Require(identifiedOrders.Single().Type == MassiveCombatOrderType.Breakout && identifiedOrders.Single().TargetFormationId == 2,
            "doctrine did not break out against an identified interdictor");
    }

    private static void CeasefireStopsCombat()
    {
        var hostility = new MutableHostility();
        var battle = Duel(40, 58); var engine = new MassiveCombatEngine(hostility);
        engine.IssueOrder(battle, 0, new(1, MassiveCombatOrderType.Engage, 2));
        engine.IssueOrder(battle, 1, new(2, MassiveCombatOrderType.Engage, 1));
        engine.Advance(battle, .5);
        var durability = battle.Formations.Sum(x => x.ShieldPool + x.ArmorPool + x.HullPool);
        hostility.Hostile = false;
        engine.Advance(battle, 2);
        Require(Math.Abs(durability - battle.Formations.Sum(x => x.ShieldPool + x.ArmorPool + x.HullPool)) < .001f,
            "formations continued attacking after live diplomacy ended hostilities");
        Require(!engine.HasActiveHostilities(battle), "ceasefire did not expose encounter completion to the campaign adapter");
    }

    private static MassiveCombatBattleState Duel(int shipsPerSide, ulong seed) => MassiveCombatBattleState.Create(seed, new[]
    {
        Formation(1, 0, 10, 100, "Human Line", shipsPerSide, new(-120, 0), BasicLoadout(.5f)),
        Formation(2, 1, 20, 200, "Enemy Line", shipsPerSide, new(120, 0), BasicLoadout(.5f)),
    });

    private static MassiveFormationState Formation(long id, int civilization, int fleet, int taskForce, string name, int ships, MassivePoint position, MassiveCombatLoadout loadout) => new()
    {
        Id = id, CivilizationId = civilization, FleetId = fleet, TaskForceId = taskForce, Name = name,
        Position = position, Objective = position, Loadout = loadout,
        Cohorts = new() { new() { Id = id * 10, DesignId = "line_ship", InitialCount = ships, ActiveCount = ships } },
        InitialShipCount = ships, ShieldPool = loadout.ShieldPerShip * ships, ArmorPool = loadout.ArmorPerShip * ships, HullPool = loadout.HullPerShip * ships,
    };

    private static MassiveCombatLoadout BasicLoadout(float damage)
    {
        var legacy = new CombatProfileDefinition("massive_test", 35, 45, 95, damage, .2, 1);
        var result = MassiveCombatLoadouts.FromLegacy(legacy);
        result.Acceleration = 20; result.MaximumSpeed = 100; result.ReactorOutputPerShip = 100; result.CoolingPerShip = 20;
        if (result.Weapons.Count > 0) { result.Weapons[0].ShotsPerSecond = 1; result.Weapons[0].Accuracy = .7f; }
        return result;
    }

    private sealed class TestSensors(bool power) : IMassiveCombatSensorView
    {
        public float Confidence(int observerCivilizationId, long formationId) => .35f;
        public bool IdentifiesCohorts(int observerCivilizationId, long formationId) => false;
        public bool IdentifiesImportantVessels(int observerCivilizationId, long formationId) => false;
        public bool CanEstimateCombatPower(int observerCivilizationId, long formationId) => power;
    }
    private sealed class ZeroSensors : IMassiveCombatSensorView
    {
        public float Confidence(int observerCivilizationId, long formationId) => 0;
        public bool IdentifiesCohorts(int observerCivilizationId, long formationId) => false;
        public bool IdentifiesImportantVessels(int observerCivilizationId, long formationId) => false;
        public bool CanEstimateCombatPower(int observerCivilizationId, long formationId) => false;
    }
    private sealed class FullSensors : IMassiveCombatSensorView
    {
        public float Confidence(int observerCivilizationId, long formationId) => 1;
        public bool IdentifiesCohorts(int observerCivilizationId, long formationId) => true;
        public bool IdentifiesImportantVessels(int observerCivilizationId, long formationId) => true;
        public bool CanEstimateCombatPower(int observerCivilizationId, long formationId) => true;
    }
    private sealed class MutableHostility : IMassiveCombatHostilityView
    {
        public bool Hostile { get; set; } = true;
        public bool AreHostile(int firstCivilizationId, int secondCivilizationId) => Hostile && firstCivilizationId != secondCivilizationId;
    }
    private static void Require(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    private static void RequireThrows(Action action, string message) { try { action(); } catch (Exception) { return; } throw new InvalidOperationException(message); }
    private static string StateHash(MassiveCombatBattleState battle) => Convert.ToHexString(
        SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(battle, Json))));
    private static MassiveCombatBattleState Clone(MassiveCombatBattleState battle) =>
        JsonSerializer.Deserialize<MassiveCombatBattleState>(JsonSerializer.Serialize(battle, Json), Json)!;
}
