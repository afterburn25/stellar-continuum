using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        PropertyNamingPolicy = null,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };

    private sealed record Row(string Name, string Operation, JsonNode Input,
        JsonNode Before, JsonNode After, JsonNode? Result, string? ErrorType,
        string? ErrorMessage, bool SourceOnly = false, string? SourceOnlyReason = null);

    private static int Main(string[] args)
    {
        var output = args.Length > 0 ? Path.GetFullPath(args[0]) :
            Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "fixture.json"));
        var sourceRoot = args.Length > 1 ? Path.GetFullPath(args[1]) :
            Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../src/Game"));
        try
        {
            var rows = BuildRows();
            var sourceFiles = new[]
            {
                "Simulation/Combat/Massive/MassiveCombatState.cs",
                "Simulation/Combat/Massive/MassiveCombatContracts.cs",
                "Simulation/Combat/Massive/MassiveCombatEquipment.cs",
                "Simulation/Combat/CampaignMassiveCombat.cs",
                "Persistence/CampaignSaveService.cs",
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
                GuidByteConvention = "System.Guid.ToByteArray order",
                SourceFiles = fingerprints,
                RowCount = rows.Count,
                SourceOnlyCount = rows.Count(row => row.SourceOnly),
                Rows = rows,
            };
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            File.WriteAllText(output, JsonSerializer.Serialize(document, new JsonSerializerOptions(Json)
            {
                WriteIndented = true,
            }) + Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"Massive encounter source oracle: {rows.Count}/{rows.Count} rows written to {output}");
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
        AddValidation(rows, "valid-materializes-legacy-counts", _ => { });
        AddValidation(rows, "empty-battle-id", value => value.encounter.Battle.BattleId = Guid.Empty);
        AddValidation(rows, "negative-pending", value => value.encounter.Battle.PendingSeconds = -1);
        AddValidation(rows, "nan-simulated", value => value.encounter.Battle.SimulatedSeconds = double.NaN);
        AddValidation(rows, "duplicate-formation", value => value.encounter.Battle.Formations[1].Id = 1);
        AddValidation(rows, "unknown-formation-shape", value => value.encounter.Battle.Formations[0].Shape = (MassiveFormationShape)99);
        AddValidation(rows, "unicode-blank-formation-name", value => value.encounter.Battle.Formations[0].Name = "\u2003\u00a0");
        AddValidation(rows, "duplicate-cohort-id", value => value.encounter.Battle.Formations[1].Cohorts.Add(
            new MassiveCohortState { Id = 20, DesignId = "design-b", InitialCount = 0, ActiveCount = 0 }));
        AddValidation(rows, "invalid-cohort-before-loadout", value =>
        {
            value.encounter.Battle.Formations[1].Cohorts[0].ActiveCount = 3;
            value.encounter.Battle.Formations[1].Loadout.HullPerShip = 0;
        });
        AddValidation(rows, "invalid-important-vessel", value => value.encounter.Battle.Formations[0].ImportantVessels[0].Name = "");
        AddValidation(rows, "invalid-loadout", value => value.encounter.Battle.Formations[0].Loadout.HullPerShip = 0);
        AddValidation(rows, "partial-materialization-before-later-failure", value => value.encounter.Battle.Formations[1].Cohesion = float.NaN);
        AddValidation(rows, "negative-destroyed", value => value.encounter.Battle.Formations[0].DestroyedShips = -1);
        AddValidation(rows, "durability-negative", value => value.encounter.Battle.Formations[0].ShieldPool = -1);
        AddValidation(rows, "normalized-over-one", value => value.encounter.Battle.Formations[0].Morale = 1.01f);
        AddValidation(rows, "duplicate-important-global", value =>
        {
            value.encounter.Battle.Formations[1].ImportantVessels.Add(new MassiveVesselState
                { Id = 101, Name = "Duplicate", DesignId = "design-b" });
            value.encounter.Battle.Formations[1].InitialShipCount = 3;
            value.encounter.Vessels.Add(new CampaignCombatBinding(104, 2));
            value.galaxy.Fleets.Add(Fleet(104, 2, "design-b"));
        });
        AddValidation(rows, "invalid-event-enum", value => value.encounter.Battle.Events[0] =
            value.encounter.Battle.Events[0] with { Type = (MassiveCombatEventType)99 });
        AddValidation(rows, "invalid-event-position", value => value.encounter.Battle.Events[0] =
            value.encounter.Battle.Events[0] with { Position = new(float.PositiveInfinity, 0) });
        AddValidation(rows, "salvo-unknown-target", value => value.encounter.Battle.ActiveSalvos[0].TargetFormationId = 999);
        AddValidation(rows, "duplicate-salvo", value => value.encounter.Battle.ActiveSalvos.Add(Clone(value.encounter.Battle.ActiveSalvos[0])));
        AddValidation(rows, "event-counter-not-after", value => value.encounter.Battle.NextEventSequence = 2);
        AddValidation(rows, "salvo-counter-not-after", value => value.encounter.Battle.NextSalvoId = 8);
        AddValidation(rows, "missing-system", value => value.encounter.SystemId = 999);
        AddValidation(rows, "negative-start-day", value => value.encounter.StartedDay = -1);
        AddValidation(rows, "empty-bindings", value => value.encounter.Vessels.Clear());
        AddValidation(rows, "duplicate-binding-fleet", value => value.encounter.Vessels[1] = new(101, 2));
        AddValidation(rows, "negative-observed-sequence", value => value.encounter.LastObservedEventSequence = -1);
        AddValidation(rows, "duplicate-engagement", value => value.encounter.EngagedFormationPairs.Add(new(1, 2)));
        AddValidation(rows, "duplicate-world-fleet", value => value.galaxy.Fleets.Add(Fleet(101, 1, "design-a")));
        AddValidation(rows, "binding-missing-fleet", value => value.encounter.Vessels[0] = new(999, 1));
        AddValidation(rows, "binding-missing-formation", value => value.encounter.Vessels[0] = new(101, 999));
        AddValidation(rows, "binding-civilization-mismatch", value => value.galaxy.Fleets[0] = Fleet(101, 2, "design-a"));
        AddValidation(rows, "binding-design-mismatch", value => value.galaxy.Fleets[1] = Fleet(102, 2, "other"));
        AddValidation(rows, "binding-combat-profile-fallback", value => value.galaxy.Fleets[1] = Fleet(102, 2, null, "design-b"));
        AddValidation(rows, "formation-binding-count", value => value.encounter.Vessels.RemoveAt(2));
        AddValidation(rows, "engagement-noncanonical", value => value.encounter.EngagedFormationPairs[0] = new(2, 1));
        AddValidation(rows, "engagement-unknown-formation", value => value.encounter.EngagedFormationPairs[0] = new(1, 99));
        AddClone(rows);
        AddPublicSaveCapture(rows);
        rows.Add(new Row("null-enumerable-element", "Validate", JsonValue.Create("not representable by native typed spans")!,
            JsonValue.Create("not invoked")!, JsonValue.Create("not invoked")!, null, "NullReferenceException",
            "Object reference not set to an instance of an object.", true,
            "Source permits null reference elements; native public spans contain valid typed values only."));
        return rows;
    }

    private static void AddValidation(List<Row> rows, string name,
        Action<(GalaxyState galaxy, CampaignMassiveEncounter encounter)> mutate)
    {
        var value = Baseline();
        mutate(value);
        var input = InputJson(value.galaxy, value.encounter);
        var before = JsonSerializer.SerializeToNode(value.encounter, Json)!;
        JsonNode? result = null;
        string? errorType = null;
        string? errorMessage = null;
        try
        {
            value.encounter.Validate(value.galaxy);
            result = JsonSerializer.SerializeToNode(new
            {
                value.encounter.Battle.IsComplete,
                ActiveShipCounts = value.encounter.Battle.Formations.Select(x => x.ActiveShipCount).ToArray(),
                SurvivingShipCounts = value.encounter.Battle.Formations.Select(x => x.SurvivingShipCount).ToArray(),
            }, Json);
        }
        catch (Exception exception)
        {
            errorType = exception.GetType().Name;
            errorMessage = exception.Message;
        }
        var after = JsonSerializer.SerializeToNode(value.encounter, Json)!;
        rows.Add(new(name, "Validate", input, before, after, result, errorType, errorMessage));
    }

    private static void AddClone(List<Row> rows)
    {
        var value = Baseline();
        value.encounter.Validate(value.galaxy);
        var input = InputJson(value.galaxy, value.encounter);
        var before = JsonSerializer.SerializeToNode(value.encounter, Json)!;
        var method = typeof(CampaignSaveService).GetMethod("CloneEncounter",
            BindingFlags.NonPublic | BindingFlags.Static) ??
            throw new MissingMethodException("CampaignSaveService.CloneEncounter");
        var clone = (CampaignMassiveEncounter?)method.Invoke(null, new object?[] { value.encounter }) ??
            throw new InvalidOperationException("CloneEncounter returned null.");
        var clonedBeforeMutation = JsonSerializer.SerializeToNode(clone, Json)!;
        value.encounter.Battle.Formations[0].Name = "mutated-live";
        value.encounter.Battle.Formations[0].Loadout.Weapons[0].DamagePerShot = 999;
        value.encounter.Battle.Formations[0].ImportantVessels[0].Name = "mutated-vessel";
        value.encounter.Battle.Events.Clear();
        value.encounter.Battle.ActiveSalvos.Clear();
        value.encounter.Vessels.Clear();
        value.encounter.EngagedFormationPairs.Clear();
        var clonedAfterLiveMutation = JsonSerializer.SerializeToNode(clone, Json)!;
        clone.Battle.Formations[1].Name = "mutated-clone";
        var liveAfterCloneMutation = JsonSerializer.SerializeToNode(value.encounter, Json)!;
        rows.Add(new("campaign-save-private-clone-deep-independence", "Clone", input,
            before, liveAfterCloneMutation, JsonSerializer.SerializeToNode(new
            {
                CloneBeforeMutation = clonedBeforeMutation,
                CloneAfterLiveMutation = clonedAfterLiveMutation,
                CloneStable = JsonNode.DeepEquals(clonedBeforeMutation, clonedAfterLiveMutation),
                LiveUnaffectedByClone = liveAfterCloneMutation["Battle"]!["Formations"]![1]!["Name"]!.GetValue<string>() != "mutated-clone",
                Provenance = "actual private CampaignSaveService.CloneEncounter used by CaptureDetachedEnvelope",
            }, Json), null, null));
    }

    private static void AddPublicSaveCapture(List<Row> rows)
    {
        var scratch = Path.Combine(Path.GetTempPath(),
            "stellar-massive-encounter-oracle-" + Guid.NewGuid().ToString("N"));
        var ownership = scratch + ".claim";
        using (new FileStream(ownership, FileMode.CreateNew, FileAccess.Write,
                   FileShare.None)) { }
        var ownsDirectory = false;
        try
        {
            if (Directory.Exists(scratch))
                throw new IOException("Exclusive scratch directory already exists.");
            Directory.CreateDirectory(scratch);
            ownsDirectory = true;
            var campaign = new CampaignSessionService().CreateNew(790079);
            var galaxy = campaign.Galaxy;
            galaxy.Fleets.Clear();
            var system = galaxy.Systems[0];
            var civilizations = galaxy.Civilizations.Take(2).ToArray();
            var profile = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1);
            for (var index = 0; index < 2; ++index)
            {
                var id = 700 + index;
                galaxy.Fleets.Add(new FleetState
                {
                    Id = id,
                    CivilizationId = civilizations[index].Id,
                    Name = $"Captured vessel {id}",
                    Role = FleetRole.Military,
                    Position = system.Position,
                    CurrentSystemId = system.Id,
                    Combat = CombatProfileRegistry.CreateInitialState(profile.Id, FleetRole.Military),
                    TacticalLoadout = MassiveCombatLoadouts.FromLegacy(profile),
                    TacticalVessel = new MassiveVesselState
                    {
                        Id = id, Name = $"Captured vessel {id}",
                        DesignId = profile.Id, IsFlagship = true,
                    },
                });
            }
            var bridge = new CampaignMassiveCombat(
                new DelegateCombatHostilityView((first, second) => first != second));
            var opened = bridge.Begin(galaxy, civilizations[0].Id,
                galaxy.Fleets[0].Id, 18.25);
            if (!opened.Accepted)
                throw new InvalidOperationException(opened.Message);
            var encounter = galaxy.ActiveCombatEncounter ??
                throw new InvalidOperationException("Public campaign bridge omitted encounter.");
            var input = InputJson(galaxy, encounter);
            var before = JsonSerializer.SerializeToNode(encounter, Json)!;
            var path = Path.Combine(scratch, "capture.json");
            new CampaignSaveService().Save(path, galaxy, 18.25);
            var document = JsonNode.Parse(File.ReadAllText(path))!;
            var captured = document["Galaxy"]!["ActiveCombatEncounter"]!.DeepClone();
            encounter.Battle.Formations[0].Name = "mutated-after-public-save";
            encounter.Battle.Formations[0].Loadout.Weapons.Clear();
            encounter.Vessels.Clear();
            var capturedAfterMutation = document["Galaxy"]!["ActiveCombatEncounter"]!.DeepClone();
            rows.Add(new("public-campaign-save-deep-capture", "Capture", input,
                before, JsonSerializer.SerializeToNode(encounter, Json)!,
                JsonSerializer.SerializeToNode(new
                {
                    Captured = captured,
                    CapturedAfterLiveMutation = capturedAfterMutation,
                    Stable = JsonNode.DeepEquals(captured, capturedAfterMutation),
                    Provenance = "actual public CampaignSaveService.Save detached galaxy capture",
                }, Json), null, null));
        }
        finally
        {
            if (File.Exists(ownership))
            {
                if (ownsDirectory && Directory.Exists(scratch))
                    Directory.Delete(scratch, recursive: true);
                File.Delete(ownership);
            }
        }
    }

    private static (GalaxyState galaxy, CampaignMassiveEncounter encounter) Baseline()
    {
        var system = new StarSystemState(7, "Anchor", new(1, 2), StarArchetype.Standard,
            true, false, false, false);
        var fleets = new List<FleetState>
        {
            Fleet(101, 1, "design-a"), Fleet(102, 2, "design-b"), Fleet(103, 2, "design-b"),
        };
        var galaxy = new GalaxyState
        {
            Seed = 79,
            Systems = new[] { system },
            PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
            Civilizations = new List<CivilizationState>(),
            Fleets = fleets,
            Colonies = new List<ColonyState>(),
            Economies = Array.Empty<CivilizationEconomyState>(),
            Technologies = new List<TechnologyState>(),
            ConstructionStates = new List<ConstructionState>(),
            ShipyardStates = new List<ShipyardState>(),
            PlayerCivilizationId = 1,
            Knowledge = new CivilizationKnowledgeState(),
        };
        var first = Formation(1, 1, 101, "Alpha");
        first.ImportantVessels.Add(new MassiveVesselState
        {
            Id = 101, Name = "Named Alpha", DesignId = "design-a", IsFlagship = true,
            HullFraction = .8f, EngineFraction = .9f, SensorFraction = .7f,
            WarpDriveFraction = .6f, ReactorFraction = .5f, InterdictorFraction = .4f,
            BattlesFought = 3, ConfirmedKills = 2,
        });
        var second = Formation(2, 2, 102, "Beta");
        second.Cohorts.Add(new MassiveCohortState
        {
            Id = 20, DesignId = "design-b", InitialCount = 2, ActiveCount = 2,
            Experience = .75f,
        });
        var encounter = new CampaignMassiveEncounter
        {
            SystemId = 7,
            StartedDay = 12.5,
            Battle = new MassiveCombatBattleState
            {
                BattleId = new Guid("00112233-4455-6677-8899-aabbccddeeff"),
                Seed = 1234,
                Tick = 4,
                SimulatedSeconds = 1.25,
                PendingSeconds = .05,
                NextEventSequence = 3,
                NextSalvoId = 9,
                Formations = new() { first, second },
                Events = new()
                {
                    new MassiveCombatEvent(1, 3, MassiveCombatEventType.Engagement,
                        1, 1, 2, 2, 0, new(-4, 5), "contact"),
                    new MassiveCombatEvent(2, 4, MassiveCombatEventType.Damage,
                        1, 1, 2, 2, 3, new(6, -7), "impact"),
                },
                ActiveSalvos = new()
                {
                    new MassiveMissileSalvoState
                    {
                        Id = 8, SourceFormationId = 1, TargetFormationId = 2,
                        MissileCount = 3, Damage = 9.5f, RemainingSeconds = .5f,
                        LaunchPosition = new MassivePoint(-2, 3), InitialFlightSeconds = 1,
                    },
                },
            },
            Vessels = new() { new(101, 1), new(102, 2), new(103, 2) },
            EngagedFormationPairs = new() { new(1, 2) },
            LastObservedEventSequence = 2,
            Reconciled = false,
        };
        return (galaxy, encounter);
    }

    private static MassiveFormationState Formation(long id, int civilizationId,
        int fleetId, string name)
    {
        var loadout = new MassiveCombatLoadout
        {
            MassPerShip = 110, Acceleration = 12, MaximumSpeed = 90,
            ShieldPerShip = 30, ArmorPerShip = 40, HullPerShip = 95,
            ReactorOutputPerShip = 120, CoolingPerShip = 22,
            WarpStabilization = 45, WarpSpoolSeconds = 14,
            ModuleSlotCapacity = 4, MaximumModuleMass = 100,
            Weapons = new()
            {
                new MassiveWeaponGroup
                {
                    Id = "beam", Kind = MassiveWeaponKind.Beam, MountsPerShip = 2,
                    DamagePerShot = 7, ShotsPerSecond = .5f, Range = 600,
                    Accuracy = .8f, PowerPerSecond = 4, HeatPerSecond = 2,
                },
            },
            Modules = new()
            {
                new MassiveModuleState
                {
                    Id = "reactor", Kind = MassiveModuleKind.Reactor,
                    InstalledCount = 1, MassEach = 10, PowerPerSecondEach = 0,
                    HeatPerSecondEach = 0, Condition = .9f, Enabled = true,
                    EffectiveRange = 0, FieldStrength = 40,
                    DetectionSignature = 5, Slots = 1,
                },
            },
        };
        return new MassiveFormationState
        {
            Id = id, CivilizationId = civilizationId, FleetId = fleetId,
            TaskForceId = fleetId + 1000, Name = name,
            Position = new(id * 10, id * -2), Velocity = new(1, 0),
            Heading = new(1, 0), Objective = new(50, 5),
            Shape = MassiveFormationShape.Line, Order = MassiveCombatOrderType.Engage,
            TargetFormationId = id == 1 ? 2 : 1,
            ProtectedFormationId = null,
            InterdictorProtection = InterdictorProtectionPolicy.High,
            Cohesion = .9f, Morale = .8f, ShieldPool = 30,
            ArmorPool = 40, HullPool = 95, HullLossThresholdPerShip = 95,
            Heat = 2, PowerReserve = .7f, WarpSpoolProgress = .2f,
            WarpBlocked = false, Escaped = false, Surrendered = false,
            InitialShipCount = 0, DestroyedShips = 0, HullDamageRemainder = .25f,
            Loadout = loadout,
        };
    }

    private static FleetState Fleet(int id, int civilizationId, string? designId,
        string? profileId = null) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = $"Fleet {id}",
        Role = FleetRole.Military,
        DesignId = designId,
        Position = new(1, 2),
        CurrentSystemId = 7,
        Combat = profileId is null ? null : new FleetCombatState { ProfileId = profileId },
    };

    private static JsonNode InputJson(GalaxyState galaxy,
        CampaignMassiveEncounter encounter) => JsonSerializer.SerializeToNode(new
    {
        Systems = galaxy.Systems.Select(system => new { system.Id }).ToArray(),
        Fleets = galaxy.Fleets.Select(fleet => new
        {
            fleet.Id,
            fleet.CivilizationId,
            fleet.DesignId,
            CombatProfileId = fleet.Combat?.ProfileId,
        }).ToArray(),
        Encounter = encounter,
    }, Json)!;

    private static T Clone<T>(T value) => JsonSerializer.Deserialize<T>(
        JsonSerializer.Serialize(value, Json), Json)!;

    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));
}
