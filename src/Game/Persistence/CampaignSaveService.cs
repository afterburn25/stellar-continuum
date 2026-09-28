using System;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Diagnostics;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;
using Game.Simulation.Diplomacy;
using Game.Simulation.Research.Adaptive;

namespace Game.Persistence;

internal sealed record DetachedGalaxySave(
    CampaignSaveEnvelope Envelope,
    double ValidationMilliseconds,
    double DtoCaptureMilliseconds);

public sealed class CampaignSaveService
{
    public const int LegacyFormatVersion = 8;
    public const int PresetFormatVersion = 10;
    public const int SurfaceFormatVersion = 12;
    public const int CurrentFormatVersion = 16; // Odd versions belong to the campaign Diplomacy wrapper.

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
    };

    public void Save(string path, GalaxyState galaxy, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (galaxy.DeveloperSession is not null)
            throw new InvalidOperationException("A Developer campaign cannot be written as a Player save. Use Developer campaign persistence.");
        SaveCore(path, galaxy, simulationDays);
    }

    // Only the Developer envelope serializer may use this canonical payload path. The live
    // provenance marker remains attached throughout validation and serialization.
    internal void SaveDeveloperPayload(string path, GalaxyState galaxy, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (galaxy.DeveloperSession is null)
            throw new InvalidOperationException("Developer payload serialization requires explicit Developer session provenance.");
        SaveCore(path, galaxy, simulationDays);
    }

    private void SaveCore(string path, GalaxyState galaxy, double simulationDays)
    {
        var envelope = CaptureDetachedEnvelope(galaxy, simulationDays, galaxy.DeveloperSession is not null).Envelope;
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
            Directory.CreateDirectory(directory);
        var json = JsonSerializer.Serialize(envelope, JsonOptions);
        var tempPath = path + ".tmp";
        File.WriteAllText(tempPath, json);
        if (File.Exists(path))
            File.Replace(tempPath, path, path + ".bak", ignoreMetadataErrors: true);
        else
            File.Move(tempPath, path);
    }

    // The campaign wrapper uses a detached in-memory payload instead of writing and
    // immediately reading a second multi-megabyte save on the rendering thread.
    internal JsonObject CapturePayload(GalaxyState galaxy, double simulationDays, bool developerPayload)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if ((galaxy.DeveloperSession is not null) != developerPayload)
            throw new InvalidOperationException("Campaign payload provenance does not match the requested save mode.");
        return JsonSerializer.SerializeToNode(CaptureDetachedEnvelope(galaxy, simulationDays, developerPayload).Envelope, JsonOptions)!.AsObject();
    }

    internal DetachedGalaxySave CaptureDetachedEnvelope(GalaxyState galaxy, double simulationDays, bool developerPayload)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if ((galaxy.DeveloperSession is not null) != developerPayload)
            throw new InvalidOperationException("Campaign payload provenance does not match the requested save mode.");
        var validationStarted = Stopwatch.GetTimestamp();
        ValidateStellarCatalog(galaxy.Systems);
        ValidatePlanetaryCatalog(galaxy.PlanetaryBodies, galaxy.Systems);
        ValidatePlanetaryReferences(galaxy);
        var metadata = ValidateGenerationMetadata(galaxy.GenerationMetadata, galaxy.Seed, galaxy.Systems);
        var galacticCore = ValidateGalacticCore(galaxy.GalacticCore, galaxy.Systems);
        ValidateGalacticCoreAgreement(metadata?.GalacticCore, galacticCore);
        var validationMilliseconds = Stopwatch.GetElapsedTime(validationStarted).TotalMilliseconds;
        var captureStarted = Stopwatch.GetTimestamp();

        var envelope = new CampaignSaveEnvelope
        {
            FormatVersion = CurrentFormatVersion,
            GameVersion = GameVersion.Current,
            SavedAtUtc = DateTimeOffset.UtcNow,
            SimulationDays = simulationDays,
            Galaxy = new GalaxySaveDto
            {
                Seed = galaxy.Seed,
                GenerationMetadata = metadata,
                GalacticCore = galacticCore,
                Systems = ToSystemDtos(galaxy.Systems),
                PlanetaryBodies = ToPlanetaryBodyDtos(galaxy.PlanetaryBodies),
                Civilizations = ToCivilizationDtos(galaxy.Civilizations),
                Fleets = ToFleetDtos(galaxy.Fleets),
                Colonies = ToColonyDtos(galaxy.Colonies),
                Economies = ToEconomyDtos(galaxy.Economies),
                Technologies = ToTechnologyDtos(galaxy.Technologies),
                ConstructionStates = ToConstructionDtos(galaxy.ConstructionStates),
                ShipyardStates = ToShipyardDtos(galaxy.ShipyardStates),
                PlayerCivilizationId = galaxy.PlayerCivilizationId,
                Knowledge = ToKnowledgeDtos(galaxy.Knowledge),
                ActiveCombatEncounter = CloneEncounter(galaxy.ActiveCombatEncounter),
                CombatIntelligence = galaxy.CombatIntelligence.Count == 0 ? null : galaxy.CombatIntelligence.ToList(),
            },
        };
        return new DetachedGalaxySave(envelope, validationMilliseconds,
            Stopwatch.GetElapsedTime(captureStarted).TotalMilliseconds);
    }

    private static CampaignMassiveEncounter? CloneEncounter(CampaignMassiveEncounter? source) => source is null ? null : new()
    {
        SystemId = source.SystemId,
        StartedDay = source.StartedDay,
        Battle = CloneBattle(source.Battle),
        Vessels = source.Vessels.ToList(),
        EngagedFormationPairs = source.EngagedFormationPairs.ToList(),
        LastObservedEventSequence = source.LastObservedEventSequence,
        Reconciled = source.Reconciled,
    };

    private static MassiveCombatBattleState CloneBattle(MassiveCombatBattleState source) => new()
    {
        BattleId = source.BattleId, Seed = source.Seed, Tick = source.Tick,
        SimulatedSeconds = source.SimulatedSeconds, PendingSeconds = source.PendingSeconds,
        NextEventSequence = source.NextEventSequence, NextSalvoId = source.NextSalvoId,
        Formations = source.Formations.Select(CloneFormation).ToList(), Events = source.Events.ToList(),
        ActiveSalvos = source.ActiveSalvos.Select(s => new MassiveMissileSalvoState
        {
            Id = s.Id, SourceFormationId = s.SourceFormationId, TargetFormationId = s.TargetFormationId,
            MissileCount = s.MissileCount, Damage = s.Damage, RemainingSeconds = s.RemainingSeconds,
            LaunchPosition = s.LaunchPosition, InitialFlightSeconds = s.InitialFlightSeconds,
        }).ToList(),
    };

    private static MassiveFormationState CloneFormation(MassiveFormationState source) => new()
    {
        Id = source.Id, CivilizationId = source.CivilizationId, FleetId = source.FleetId, TaskForceId = source.TaskForceId,
        Name = source.Name, Position = source.Position, Velocity = source.Velocity, Heading = source.Heading,
        Objective = source.Objective, Shape = source.Shape, Order = source.Order,
        TargetFormationId = source.TargetFormationId, ProtectedFormationId = source.ProtectedFormationId,
        InterdictorProtection = source.InterdictorProtection, Cohesion = source.Cohesion, Morale = source.Morale,
        ShieldPool = source.ShieldPool, ArmorPool = source.ArmorPool, HullPool = source.HullPool,
        HullLossThresholdPerShip = source.HullLossThresholdPerShip, Heat = source.Heat,
        PowerReserve = source.PowerReserve, WarpSpoolProgress = source.WarpSpoolProgress,
        WarpBlocked = source.WarpBlocked, Escaped = source.Escaped, Surrendered = source.Surrendered,
        InitialShipCount = source.InitialShipCount, DestroyedShips = source.DestroyedShips,
        HullDamageRemainder = source.HullDamageRemainder, Loadout = CloneLoadout(source.Loadout),
        Cohorts = source.Cohorts.Select(c => new MassiveCohortState
        { Id = c.Id, DesignId = c.DesignId, InitialCount = c.InitialCount, ActiveCount = c.ActiveCount, Experience = c.Experience }).ToList(),
        ImportantVessels = source.ImportantVessels.Select(CloneVessel).ToList(),
    };

    private static MassiveCombatLoadout CloneLoadout(MassiveCombatLoadout source) => new()
    {
        MassPerShip = source.MassPerShip, Acceleration = source.Acceleration, MaximumSpeed = source.MaximumSpeed,
        ShieldPerShip = source.ShieldPerShip, ArmorPerShip = source.ArmorPerShip, HullPerShip = source.HullPerShip,
        ReactorOutputPerShip = source.ReactorOutputPerShip, CoolingPerShip = source.CoolingPerShip,
        WarpStabilization = source.WarpStabilization, WarpSpoolSeconds = source.WarpSpoolSeconds,
        ModuleSlotCapacity = source.ModuleSlotCapacity, MaximumModuleMass = source.MaximumModuleMass,
        Weapons = source.Weapons.Select(w => new MassiveWeaponGroup
        { Id = w.Id, Kind = w.Kind, MountsPerShip = w.MountsPerShip, DamagePerShot = w.DamagePerShot,
          ShotsPerSecond = w.ShotsPerSecond, Range = w.Range, Accuracy = w.Accuracy,
          PowerPerSecond = w.PowerPerSecond, HeatPerSecond = w.HeatPerSecond }).ToList(),
        Modules = source.Modules.Select(m => new MassiveModuleState
        { Id = m.Id, Kind = m.Kind, InstalledCount = m.InstalledCount, MassEach = m.MassEach,
          PowerPerSecondEach = m.PowerPerSecondEach, HeatPerSecondEach = m.HeatPerSecondEach,
          Condition = m.Condition, Enabled = m.Enabled, EffectiveRange = m.EffectiveRange,
          FieldStrength = m.FieldStrength, DetectionSignature = m.DetectionSignature, Slots = m.Slots }).ToList(),
    };

    private static MassiveVesselState CloneVessel(MassiveVesselState source) => new()
    {
        Id = source.Id, Name = source.Name, DesignId = source.DesignId, IsFlagship = source.IsFlagship,
        IsCarrier = source.IsCarrier, IsInterdictor = source.IsInterdictor, IsStoryShip = source.IsStoryShip,
        HullFraction = source.HullFraction, EngineFraction = source.EngineFraction, SensorFraction = source.SensorFraction,
        WarpDriveFraction = source.WarpDriveFraction, ReactorFraction = source.ReactorFraction,
        InterdictorFraction = source.InterdictorFraction, BattlesFought = source.BattlesFought,
        ConfirmedKills = source.ConfirmedKills, Destroyed = source.Destroyed, Escaped = source.Escaped,
    };

    public LoadedCampaign Load(string path)
    {
        var json = File.ReadAllText(path);
        using (var document = JsonDocument.Parse(json))
        {
            if (document.RootElement.ValueKind == JsonValueKind.Object &&
                document.RootElement.TryGetProperty("DeveloperFormatVersion", out _))
                throw new InvalidDataException("Developer campaign envelopes cannot be opened as Player saves.");
        }
        var envelope = JsonSerializer.Deserialize<CampaignSaveEnvelope>(json, JsonOptions)
            ?? throw new InvalidDataException("Save file did not contain a campaign envelope.");

        if (envelope.FormatVersion < 1 ||
            envelope.FormatVersion > LegacyFormatVersion && envelope.FormatVersion is not
                (PresetFormatVersion or SurfaceFormatVersion or CurrentFormatVersion))
        {
            throw new InvalidDataException(
                $"Unsupported save format {envelope.FormatVersion}; maximum supported is {CurrentFormatVersion}.");
        }

        var simulationDays = envelope.FormatVersion >= 5
            ? envelope.SimulationDays
            : envelope.SimulationSeconds;
        var systems = ToSystems(envelope.Galaxy.Systems);
        ValidateStellarCatalog(systems);
        IReadOnlyList<PlanetaryBodyState> planetaryBodies = envelope.FormatVersion == CurrentFormatVersion
            ? ToPlanetaryBodies(envelope.Galaxy.PlanetaryBodies, systems)
            : new PlanetaryBodyGenerator().Generate(envelope.Galaxy.Seed, systems);
        try { planetaryBodies = SolCatalogPreset.UpgradeSavedCatalog(planetaryBodies, systems); }
        catch (InvalidOperationException exception)
        {
            throw new InvalidDataException("The saved canonical Sol catalog cannot be upgraded safely.", exception);
        }

        IList<CivilizationState> civilizations;
        CivilizationKnowledgeState knowledge;
        int playerCivilizationId;

        if (envelope.FormatVersion == 1 || envelope.Galaxy.Civilizations.Count == 0)
        {
            civilizations = new CivilizationSeeder().Seed(
                systems,
                Math.Min(8, systems.Count),
                Math.Min(2, Math.Max(0, systems.Count - 8)),
                envelope.Galaxy.Seed);
            playerCivilizationId = civilizations.First(c => c.IsPlayer).Id;
            knowledge = CreateInitialKnowledge(systems, civilizations);
        }
        else
        {
            civilizations = ToCivilizations(
                envelope.Galaxy.Civilizations,
                legacyAlreadyWarpCapable: envelope.FormatVersion < 5,
                saveFormatVersion: envelope.FormatVersion,
                campaignSeed: envelope.Galaxy.Seed);
            playerCivilizationId = envelope.Galaxy.PlayerCivilizationId;
            knowledge = ToKnowledge(envelope.Galaxy.Knowledge);

            if (knowledge.GetKnownSystems(playerCivilizationId).Count == 0)
            {
                var player = civilizations.First(c => c.Id == playerCivilizationId);
                knowledge.MarkSystemFullySurveyed(player.Id, player.HomeSystemId);
                knowledge.RevealWithinSensorRange(
                    player.Id,
                    player.HomeSystemId,
                    systems,
                    player.IsSeededAncient ? 420.0f : 95.0f);
            }
        }

        // An empty fleet list is valid in current campaigns: ships begin as paid shipyard
        // orders and only become fleets after construction completes. Seed prototype fleets
        // solely for formats that predate fleet persistence; otherwise loading would create a
        // free vessel and change the campaign merely because no ship has finished yet.
        IList<FleetState> fleets = envelope.FormatVersion < 3
            ? new FleetSeeder().Seed(systems, civilizations)
            : ToFleets(
                    envelope.Galaxy.Fleets,
                    civilizations,
                    envelope.FormatVersion,
                    restoreUnserializedShipbuildingPopulation: envelope.FormatVersion >= 7)
                .ToList();

        IList<ColonyState> colonies;
        IReadOnlyList<CivilizationEconomyState> economies;
        if (envelope.FormatVersion >= SurfaceFormatVersion)
        {
            if (envelope.Galaxy.Colonies is null || envelope.Galaxy.Colonies.Count == 0 ||
                envelope.Galaxy.Economies is null || envelope.Galaxy.Economies.Count == 0)
                throw new InvalidDataException("Surface-aware saves require their authoritative colonies and economies; they cannot be reseeded.");
            foreach (var economy in envelope.Galaxy.Economies)
                ValidateEconomyStock(economy.CivilizationId, economy.Credits, economy.Industry, economy.Science);
            if (envelope.Galaxy.Economies.GroupBy(item => item.CivilizationId).Any(group => group.Count() != 1) ||
                civilizations.Any(civilization => !envelope.Galaxy.Economies.Any(item => item.CivilizationId == civilization.Id)))
                throw new InvalidDataException("Surface-aware saves require one economy for each civilization.");
        }

        if (envelope.FormatVersion < 4 ||
            envelope.Galaxy.Colonies.Count == 0 ||
            envelope.Galaxy.Economies.Count == 0)
        {
            var colonySeeder = new ColonySeeder();
            colonies = colonySeeder.Seed(civilizations);
            economies = colonySeeder.SeedEconomies(civilizations);
        }
        else
        {
            colonies = ToColonies(
                    envelope.Galaxy.Colonies,
                    civilizations,
                    envelope.FormatVersion)
                .ToList();
            economies = ToEconomies(envelope.Galaxy.Economies);
        }

        // Pre-shipbuilding saves had free prototype colony fleets. When migrating those
        // campaigns, reserve real population now so later colony founding cannot create it.
        if (envelope.FormatVersion < 7)
            EnsureLegacyExpansionFleets(fleets, systems, civilizations, colonies);

        IList<TechnologyState> technologies = envelope.FormatVersion < 5 ||
                                              envelope.Galaxy.Technologies.Count == 0
            ? CreateMigratedTechnologyStates(civilizations)
            : ToTechnologies(envelope.Galaxy.Technologies);

        IList<ConstructionState> construction = envelope.FormatVersion < 6 ||
                                                envelope.Galaxy.ConstructionStates.Count == 0
            ? CreateMigratedConstructionStates(civilizations)
            : ToConstructionStates(envelope.Galaxy.ConstructionStates);

        IList<ShipyardState> shipyards = envelope.FormatVersion < 7 ||
                                         envelope.Galaxy.ShipyardStates.Count == 0
            ? new ShipyardSeeder().Seed(civilizations)
            : ToShipyardStates(
                envelope.Galaxy.ShipyardStates,
                civilizations,
                envelope.FormatVersion);

        // Saves that predate explicit fleet-combat persistence are upgraded in memory to the
        // registry baseline. Explicit Combat DTO state is restored above when present.
        foreach (var fleet in fleets)
            CombatProfileRegistry.EnsureState(fleet);

        var metadata = ValidateGenerationMetadata(envelope.Galaxy.GenerationMetadata, envelope.Galaxy.Seed, systems);
        var persistedCore = ValidateGalacticCore(envelope.Galaxy.GalacticCore, systems);
        ValidateGalacticCoreAgreement(metadata?.GalacticCore, persistedCore);
        var galacticCore = persistedCore ?? metadata?.GalacticCore;
        var galaxy = new GalaxyState
        {
            Seed = envelope.Galaxy.Seed,
            GenerationMetadata = metadata,
            GalacticCore = galacticCore,
            Systems = systems,
            PlanetaryBodies = planetaryBodies,
            Civilizations = civilizations,
            Fleets = fleets,
            Colonies = colonies,
            Economies = economies,
            Technologies = technologies,
            ConstructionStates = construction,
            ShipyardStates = shipyards,
            PlayerCivilizationId = playerCivilizationId,
            Knowledge = knowledge,
            ActiveCombatEncounter = envelope.Galaxy.ActiveCombatEncounter,
            CombatIntelligence = envelope.Galaxy.CombatIntelligence ?? new(),
        };

        ValidatePlanetaryCatalog(galaxy.PlanetaryBodies, galaxy.Systems);
        ValidatePlanetaryReferences(galaxy);

        return new LoadedCampaign(
            galaxy,
            simulationDays,
            envelope.GameVersion,
            envelope.SavedAtUtc);
    }

    private static GalaxyGenerationMetadata? ValidateGenerationMetadata(
        GalaxyGenerationMetadata? metadata,
        long seed,
        IReadOnlyList<StarSystemState> systems)
    {
        // Metadata was introduced after the existing save formats and is intentionally
        // optional so older campaigns continue to load unchanged.
        if (metadata is null)
            return null;
        if (string.IsNullOrWhiteSpace(metadata.EnteredSeed) ||
            string.IsNullOrWhiteSpace(metadata.GeneratorVersion) ||
            metadata.InternalSeed != seed ||
            metadata.SystemCount != systems.Count ||
            metadata.SystemCount <= 0 ||
            metadata.OtherCivilizations < 0 ||
            metadata.GuaranteedNearbyHabitableWorlds < 0)
            throw new InvalidDataException("Campaign generation metadata is invalid or does not match the saved galaxy.");
        ValidateGalacticCore(metadata.GalacticCore, systems);
        return metadata;
    }

    private static GalacticCoreMetadata? ValidateGalacticCore(GalacticCoreMetadata? core, IReadOnlyList<StarSystemState> systems)
    {
        if (core is null) return null;
        if (core.LandmarkKey != GalacticCoreMetadata.StableLandmarkKey ||
            !float.IsFinite(core.X) || !float.IsFinite(core.Y) ||
            !float.IsFinite(core.ExclusionRadius) || core.ExclusionRadius <= 0)
            throw new InvalidDataException("Campaign galactic-core metadata is invalid.");
        var position = new System.Numerics.Vector2(core.X, core.Y);
        var radiusSquared = core.ExclusionRadius * core.ExclusionRadius;
        if (systems.Any(system => System.Numerics.Vector2.DistanceSquared(system.Position, position) < radiusSquared))
            throw new InvalidDataException("Campaign galactic-core metadata overlaps a saved system.");
        return core;
    }

    private static void ValidateGalacticCoreAgreement(GalacticCoreMetadata? metadataCore, GalacticCoreMetadata? stateCore)
    {
        if (metadataCore is not null && stateCore is not null && metadataCore != stateCore)
            throw new InvalidDataException("Campaign galactic-core metadata disagrees with the saved galaxy landmark.");
    }

    private static CivilizationKnowledgeState CreateInitialKnowledge(
        IReadOnlyList<StarSystemState> systems,
        IList<CivilizationState> civilizations)
    {
        var knowledge = new CivilizationKnowledgeState();
        foreach (var civilization in civilizations)
        {
            knowledge.MarkSystemFullySurveyed(civilization.Id, civilization.HomeSystemId);
            knowledge.RevealWithinSensorRange(
                civilization.Id,
                civilization.HomeSystemId,
                systems,
                civilization.IsSeededAncient ? 420.0f : 95.0f);
        }

        return knowledge;
    }

    private static IList<TechnologyState> CreateMigratedTechnologyStates(
        IList<CivilizationState> civilizations)
    {
        var states = new TechnologySeeder().Seed(civilizations);
        foreach (var civilization in civilizations)
        {
            if (civilization.DevelopmentStage != CivilizationDevelopmentStage.WarpCapable)
                continue;

            var state = states.First(t => t.CivilizationId == civilization.Id);
            foreach (var technology in TechnologyRegistry.All)
                state.CompletedTechnologyIds.Add(technology.Id);
        }

        return states;
    }

    private static IList<ConstructionState> CreateMigratedConstructionStates(
        IList<CivilizationState> civilizations)
    {
        var states = new ConstructionSeeder().Seed(civilizations);
        foreach (var civilization in civilizations)
        {
            if (civilization.DevelopmentStage != CivilizationDevelopmentStage.WarpCapable)
                continue;

            var state = states.First(c => c.CivilizationId == civilization.Id);
            foreach (var project in ConstructionRegistry.All)
                state.CompletedProjectIds.Add(project.Id);
        }

        return states;
    }

    private static void EnsureLegacyExpansionFleets(
        IList<FleetState> fleets,
        IReadOnlyList<StarSystemState> systems,
        IList<CivilizationState> civilizations,
        IList<ColonyState> colonies)
    {
        var colonyDesign = ShipDesignRegistry.All.FirstOrDefault(
            design => design.Role == FleetRole.Colony);
        if (colonyDesign is null || colonyDesign.PopulationCostMillions <= 0.0)
            return;

        var nextId = fleets.Count == 0 ? 0 : fleets.Max(f => f.Id) + 1;
        foreach (var civilization in civilizations)
        {
            if (civilization.DevelopmentStage != CivilizationDevelopmentStage.WarpCapable ||
                !civilization.ExpansionAllowed)
            {
                continue;
            }

            var existing = fleets.FirstOrDefault(f =>
                f.IsActive &&
                f.CivilizationId == civilization.Id &&
                f.Role == FleetRole.Colony);
            if (existing is not null && existing.EmbarkedPopulationMillions > 0.0)
                continue;

            var source = colonies
                .Where(colony => colony.CivilizationId == civilization.Id)
                .OrderByDescending(colony => colony.PopulationMillions)
                .FirstOrDefault();
            if (source is null ||
                source.PopulationMillions < colonyDesign.PopulationCostMillions + 500.0)
            {
                continue;
            }

            var sourceSpeciesId = RequireKnownPopulationSpeciesId(
                source.PopulationSpeciesId,
                $"source colony {source.Id}");
            source.PopulationMillions -= colonyDesign.PopulationCostMillions;

            if (existing is not null)
            {
                existing.EmbarkedPopulationMillions = colonyDesign.PopulationCostMillions;
                existing.EmbarkedPopulationSpeciesId = sourceSpeciesId;
                CombatProfileRegistry.EnsureState(existing);
                continue;
            }

            var home = systems.First(s => s.Id == civilization.HomeSystemId);
            var fleet = new FleetState
            {
                Id = nextId++,
                CivilizationId = civilization.Id,
                Name = civilization.IsPlayer
                    ? "Pioneer One"
                    : $"{civilization.Name} Pioneer",
                Role = FleetRole.Colony,
                Position = home.Position,
                CurrentSystemId = home.Id,
                StrategicSpeed = colonyDesign.StrategicSpeed,
                SensorRange = colonyDesign.SensorRange,
                IsActive = true,
                EmbarkedPopulationMillions = colonyDesign.PopulationCostMillions,
                EmbarkedPopulationSpeciesId = sourceSpeciesId,
                Combat = CombatProfileRegistry.CreateInitialState(
                    colonyDesign.CombatProfileId,
                    FleetRole.Colony),
            };
            fleets.Add(fleet);
        }
    }

    private static List<StarSystemState> ToSystems(
        IReadOnlyList<StarSystemSaveDto> dtos) =>
        dtos.Select(d => new StarSystemState(
                d.Id,
                d.Name,
                new Vector2(d.X, d.Y),
                d.Archetype,
                d.HasHabitableWorld,
                d.HasAnomaly,
                d.HasRareResource,
                d.HasPreWarpCivilization,
                d.CatalogPresetId,
                d.StellarClass,
                d.SecondaryStellarClass,
                d.TertiaryStellarClass,
                d.GalacticDepthLightYears,
                d.StellarCatalogId))
            .ToList();

    private static void ValidateStellarCatalog(IReadOnlyList<StarSystemState> systems)
    {
        foreach (var system in systems)
        {
            if (system.GalacticDepthLightYears is double depth && !double.IsFinite(depth))
                throw new InvalidDataException($"System {system.Id} ({system.Name}) has an invalid galactic depth.");
            if (system.StellarClass is { } primary && !Enum.IsDefined(primary) ||
                system.SecondaryStellarClass is { } secondary && !Enum.IsDefined(secondary) ||
                system.TertiaryStellarClass is { } tertiary && !Enum.IsDefined(tertiary))
                throw new InvalidDataException($"System {system.Id} ({system.Name}) has an invalid stellar class.");
            if (system.SecondaryStellarClass.HasValue && !system.StellarClass.HasValue ||
                system.TertiaryStellarClass.HasValue && !system.SecondaryStellarClass.HasValue)
                throw new InvalidDataException($"System {system.Id} ({system.Name}) has an incomplete stellar companion configuration; B requires A and C requires B.");
            if (system.CatalogPresetId == SolCatalogPreset.PresetId &&
                (system.SecondaryStellarClass.HasValue || system.TertiaryStellarClass.HasValue))
                throw new InvalidDataException($"System {system.Id} (Sol) must retain its single canonical star.");
        }
    }

    private static IList<CivilizationState> ToCivilizations(
        IReadOnlyList<CivilizationSaveDto> dtos,
        bool legacyAlreadyWarpCapable,
        int saveFormatVersion,
        long campaignSeed)
    {
        return dtos.Select(d => new CivilizationState(
                d.Id,
                d.Name,
                d.HomeSystemId,
                d.Archetype,
                new CivilizationTraits(
                    d.Aggression,
                    d.Territoriality,
                    d.Greed,
                    d.ScientificCuriosity,
                    d.RiskTolerance,
                    d.SurvivalPriority,
                    d.HonorBound),
                d.IsPlayer,
                legacyAlreadyWarpCapable
                    ? CivilizationDevelopmentStage.WarpCapable
                    : d.DevelopmentStage,
                legacyAlreadyWarpCapable ? false : d.IsSeededAncient,
                legacyAlreadyWarpCapable ? true : d.ExpansionAllowed,
                legacyAlreadyWarpCapable ? false : d.NeutralUnlessProvoked,
                saveFormatVersion < 8
                    ? SpeciesAssignmentPolicy.Assign(campaignSeed, d.Id)
                    : RequireKnownSpeciesId(d.SpeciesId, d.Id))
            {
                Leadership = d.Leadership is null
                    ? CivilizationLeadershipState.CreateFoundingRoster(d.Id,
                        (saveFormatVersion < 8 ? SpeciesAssignmentPolicy.Assign(campaignSeed, d.Id) : d.SpeciesId) == SpeciesCatalog.TerranBaselineId)
                    : CivilizationLeadershipState.Restore(d.Leadership),
            })
            .ToList();
    }

    private static string RequireKnownSpeciesId(string speciesId, int civilizationId)
    {
        if (string.IsNullOrWhiteSpace(speciesId) ||
            !SpeciesCatalog.TryGet(speciesId, out _))
        {
            throw new InvalidDataException(
                $"Civilization {civilizationId} references unknown species ID '{speciesId}'.");
        }

        return speciesId;
    }

    private static string RequireKnownPopulationSpeciesId(
        string? speciesId,
        string owner)
    {
        if (string.IsNullOrWhiteSpace(speciesId) ||
            !SpeciesCatalog.TryGet(speciesId, out _))
        {
            throw new InvalidDataException(
                $"{owner} references unknown population species ID '{speciesId}'.");
        }

        return speciesId;
    }

    private static string GetCivilizationSpeciesId(
        IList<CivilizationState> civilizations,
        int civilizationId)
    {
        var civilization = civilizations.FirstOrDefault(c => c.Id == civilizationId)
            ?? throw new InvalidDataException(
                $"Population state references unknown civilization {civilizationId}.");

        return RequireKnownPopulationSpeciesId(
            civilization.SpeciesId,
            $"civilization {civilizationId}");
    }

    private static string ResolvePopulationSpeciesId(
        string? savedSpeciesId,
        int civilizationId,
        IList<CivilizationState> civilizations,
        int saveFormatVersion,
        string owner)
    {
        return saveFormatVersion < 8
            ? GetCivilizationSpeciesId(civilizations, civilizationId)
            : RequireKnownPopulationSpeciesId(savedSpeciesId, owner);
    }

    private static IReadOnlyList<FleetState> ToFleets(
        IReadOnlyList<FleetSaveDto> dtos,
        IList<CivilizationState> civilizations,
        int saveFormatVersion,
        bool restoreUnserializedShipbuildingPopulation)
    {
        var colonyPopulation = ShipDesignRegistry.All.FirstOrDefault(
                design => design.Role == FleetRole.Colony)
            ?.PopulationCostMillions ?? 0.0;

        var fleets = new List<FleetState>(dtos.Count);
        foreach (var dto in dtos)
        {
            if (!Enum.IsDefined(dto.TransitPhase) || !double.IsFinite(dto.TransitProgress) || dto.TransitProgress < 0 || dto.TransitProgress > 1 ||
                !float.IsFinite(dto.LocalTransitStartX) || !float.IsFinite(dto.LocalTransitStartY) ||
                !float.IsFinite(dto.LocalTransitPositionX) || !float.IsFinite(dto.LocalTransitPositionY) ||
                !float.IsFinite(dto.LocalTransitTargetX) || !float.IsFinite(dto.LocalTransitTargetY))
                throw new InvalidDataException($"Fleet {dto.Id} contains invalid persisted transit state.");
            var embarkedPopulation = Math.Max(
                0.0,
                dto.EmbarkedPopulationMillions ??
                (restoreUnserializedShipbuildingPopulation && dto.Role == FleetRole.Colony
                    ? colonyPopulation
                    : 0.0));

            var embarkedSpeciesId = embarkedPopulation > 0.0
                ? ResolvePopulationSpeciesId(
                    dto.EmbarkedPopulationSpeciesId,
                    dto.CivilizationId,
                    civilizations,
                    saveFormatVersion,
                    $"fleet {dto.Id}")
                : null;

            var fleet = new FleetState
            {
                Id = dto.Id,
                CivilizationId = dto.CivilizationId,
                Name = dto.Name,
                Role = dto.Role,
                DesignId = dto.DesignId,
                Position = new Vector2(dto.X, dto.Y),
                CurrentSystemId = dto.CurrentSystemId,
                DestinationSystemId = dto.DestinationSystemId,
                TransitPhase = dto.TransitPhase,
                TransitOriginSystemId = dto.TransitOriginSystemId,
                TransitTargetSystemId = dto.TransitTargetSystemId,
                TransitProgress = dto.TransitProgress,
                LocalTransitStart = new Vector2(dto.LocalTransitStartX, dto.LocalTransitStartY),
                LocalTransitPosition = new Vector2(dto.LocalTransitPositionX, dto.LocalTransitPositionY),
                LocalTransitTarget = new Vector2(dto.LocalTransitTargetX, dto.LocalTransitTargetY),
                PlannedRouteSystemIds = dto.PlannedRouteSystemIds ?? new List<int>(),
                HoldRequested = dto.HoldRequested,
                ReturnToBaseRequested = dto.ReturnToBaseRequested,
                ReturnToBaseFailureReason = dto.ReturnToBaseFailureReason,
                MissionOrderRevision = dto.MissionOrderRevision,
                DestinationPlanetaryBodyId = saveFormatVersion >= 8
                    ? dto.DestinationPlanetaryBodyId
                    : null,
                SettlementBodyId = dto.SettlementBodyId,
                PreventAutomaticSettlement = dto.PreventAutomaticSettlement,
                SettlementDaysCompleted = dto.SettlementDaysCompleted,
                ReconnaissanceSystemId = dto.ReconnaissanceSystemId,
                ReconnaissanceDaysCompleted = dto.ReconnaissanceDaysCompleted,
                FreightTargetOutpostId = dto.FreightTargetOutpostId,
                FreightHomeColonyId = dto.FreightHomeColonyId,
                CargoMaterialCapacity = dto.CargoMaterialCapacity,
                CargoMaterials = dto.CargoMaterials,
                StrategicSpeed = dto.StrategicSpeed,
                MaximumLegRangeLightYears = dto.MaximumLegRangeLightYears > 0.0
                    ? dto.MaximumLegRangeLightYears
                    : 360.0,
                FuelCapacityLightYears = dto.FuelCapacityLightYears > 0.0
                    ? dto.FuelCapacityLightYears
                    : 1000.0,
                FuelRemainingLightYears = dto.FuelRemainingLightYears ??
                    (dto.FuelCapacityLightYears > 0.0 ? dto.FuelCapacityLightYears : 1000.0),
                SensorRange = dto.SensorRange,
                IsActive = dto.IsActive,
                EmbarkedPopulationMillions = embarkedPopulation,
                EmbarkedPopulationSpeciesId = embarkedSpeciesId,
                Combat = dto.Combat is null
                    ? null
                    : new FleetCombatState
                    {
                        ProfileId = dto.Combat.ProfileId,
                        Shields = dto.Combat.Shields,
                        Armor = dto.Combat.Armor,
                        Hull = dto.Combat.Hull,
                        WeaponCooldownRemainingDays = dto.Combat.WeaponCooldownRemainingDays,
                        Order = dto.Combat.Order,
                        TargetFleetId = dto.Combat.TargetFleetId,
                        DefendSystemId = dto.Combat.DefendSystemId,
                        RetreatProgressDays = dto.Combat.RetreatProgressDays,
                        RetreatStarted = dto.Combat.RetreatStarted,
                        IsDisengaged = dto.Combat.IsDisengaged,
                        DisengagedSystemId = dto.Combat.DisengagedSystemId,
                    },
                TacticalLoadout = dto.TacticalLoadout,
                TacticalVessel = dto.TacticalVessel,
            };

            // Earlier saves represented an in-flight fleet only through its strategic position.
            // Preserve that coordinate and finish its existing lane; it receives a normalized
            // inbound chart gate only when it reaches the next system.
            if (fleet.CurrentSystemId is null && fleet.DestinationSystemId is not null &&
                fleet.TransitPhase == FleetTransitPhase.None)
            {
                fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
                fleet.TransitTargetSystemId = fleet.PlannedRouteSystemIds.FirstOrDefault(fleet.DestinationSystemId.Value);
            }
            if (fleet.TransitPhase == FleetTransitPhase.InterstellarWarp && fleet.TransitTargetSystemId is null)
                throw new InvalidDataException($"Fleet {dto.Id} contains a warp phase without a target.");
            if (fleet.TransitPhase is FleetTransitPhase.LocalDeparture or FleetTransitPhase.LocalArrival && fleet.CurrentSystemId is null)
                throw new InvalidDataException($"Fleet {dto.Id} contains local transit without a current system.");

            CombatProfileRegistry.EnsureState(fleet);
            fleets.Add(fleet);
        }

        return fleets;
    }

    private static IReadOnlyList<ColonyState> ToColonies(
        IReadOnlyList<ColonySaveDto> dtos,
        IList<CivilizationState> civilizations,
        int saveFormatVersion)
    {
        return dtos.Select(d => new ColonyState
            {
                Id = d.Id,
                CivilizationId = d.CivilizationId,
                SystemId = d.SystemId,
                PlanetaryBodyId = saveFormatVersion >= 8 ? d.PlanetaryBodyId : null,
                Name = d.Name,
                Kind = d.Kind,
                PopulationSpeciesId = ResolvePopulationSpeciesId(
                    d.PopulationSpeciesId,
                    d.CivilizationId,
                    civilizations,
                    saveFormatVersion,
                    $"colony {d.Id}"),
                PopulationMillions = d.PopulationMillions,
                Infrastructure = d.Infrastructure,
                Stability = d.Stability,
                StoredFoodPopulationDaysMillions = d.StoredFoodPopulationDaysMillions,
                StoredWaterPopulationDaysMillions = d.StoredWaterPopulationDaysMillions,
                StoredExtractedMaterials = d.StoredExtractedMaterials,
                RemainingExtractableMaterials = d.RemainingExtractableMaterials,
                SurfaceHubLevel = d.SurfaceHubLevel ?? 3,
                SurfaceHubUpgradeDaysRemaining = d.SurfaceHubUpgradeDaysRemaining,
                SurfaceBuildings = RestoreSurfaceBuildings(d, saveFormatVersion),
            })
            .ToArray();
    }

    private static List<SurfaceBuildingState> RestoreSurfaceBuildings(ColonySaveDto dto, int version)
    {
        if (version < SurfaceFormatVersion && dto.SurfaceBuildings is { Count: > 0 })
            throw new InvalidDataException($"Colony {dto.Id} surface construction requires save format {SurfaceFormatVersion}.");
        if (version >= SurfaceFormatVersion && dto.SurfaceBuildings is null)
            throw new InvalidDataException($"Colony {dto.Id} is missing its surface construction collection.");
        return dto.SurfaceBuildings ?? new List<SurfaceBuildingState>();
    }

    private static IReadOnlyList<CivilizationEconomyState> ToEconomies(
        IReadOnlyList<EconomySaveDto> dtos) =>
        dtos.Select(d =>
        {
            if (!double.IsFinite(d.LastResearchSpendingPerDay) || d.LastResearchSpendingPerDay < 0.0 ||
                !double.IsFinite(d.LastResearchFundingFraction) ||
                d.LastResearchFundingFraction is < 0.0 or > 1.0 ||
                !double.IsFinite(d.OperatingArrears) || d.OperatingArrears < 0.0 ||
                !double.IsFinite(d.LastBaseOperationsFundingFraction) ||
                d.LastBaseOperationsFundingFraction is < 0.0 or > 1.0)
            {
                throw new InvalidDataException(
                    $"Civilization {d.CivilizationId} has invalid research funding state.");
            }
            if (d.IndustryPriority is not null && !Enum.IsDefined(d.IndustryPriority.Value))
                throw new InvalidDataException($"Civilization {d.CivilizationId} has an unknown industry priority.");
            return new CivilizationEconomyState
            {
                CivilizationId = d.CivilizationId,
                Credits = d.Credits,
                Industry = d.Industry,
                Science = d.Science,
                LastCreditsPerSecond = d.LastCreditsPerSecond,
                LastIndustryPerSecond = d.LastIndustryPerSecond,
                LastSciencePerSecond = d.LastSciencePerSecond,
                LastResearchSpendingPerDay = d.LastResearchSpendingPerDay,
                LastResearchFundingFraction = d.LastResearchFundingFraction,
                OperatingArrears = d.OperatingArrears,
                LastBaseOperationsFundingFraction = d.LastBaseOperationsFundingFraction,
                IndustryPriority = d.IndustryPriority,
            };
        })
            .ToArray();

    private static IList<TechnologyState> ToTechnologies(
        IReadOnlyList<TechnologySaveDto> dtos)
    {
        var result = new List<TechnologyState>(dtos.Count);
        foreach (var dto in dtos)
        {
            var state = new TechnologyState
            {
                CivilizationId = dto.CivilizationId,
                ActiveResearchId = dto.ActiveResearchId,
                ActiveResearchProgress = dto.ActiveResearchProgress,
            };
            foreach (var id in dto.CompletedTechnologyIds)
                state.CompletedTechnologyIds.Add(id);
            result.Add(state);
        }

        return result;
    }

    private static IList<ConstructionState> ToConstructionStates(
        IReadOnlyList<ConstructionSaveDto> dtos)
    {
        var result = new List<ConstructionState>(dtos.Count);
        foreach (var dto in dtos)
        {
            var state = new ConstructionState
            {
                CivilizationId = dto.CivilizationId,
                ActiveProjectId = dto.ActiveProjectId,
                ActiveProjectProgress = dto.ActiveProjectProgress,
                ActiveProjectAuthorizationCredits = dto.ActiveProjectAuthorizationCredits,
            };
            ValidateConstructionStateDto(dto);
            foreach (var id in dto.CompletedProjectIds)
                state.CompletedProjectIds.Add(id);
            foreach (var order in dto.QueuedProjects)
                state.QueuedProjects.Add(new QueuedConstructionProject(order.ProjectId, order.AuthorizationCredits));
            result.Add(state);
        }

        return result;
    }

    private static void ValidateConstructionStateDto(ConstructionSaveDto dto)
    {
        var known = ConstructionRegistry.All.Select(project => project.Id).ToHashSet(StringComparer.Ordinal);
        if (dto.CompletedProjectIds is null || dto.QueuedProjects is null)
            throw new InvalidDataException($"Construction state {dto.CivilizationId} is missing required collections.");
        if (!double.IsFinite(dto.ActiveProjectProgress) || dto.ActiveProjectProgress < 0 ||
            !double.IsFinite(dto.ActiveProjectAuthorizationCredits) || dto.ActiveProjectAuthorizationCredits < 0)
            throw new InvalidDataException($"Construction state {dto.CivilizationId} has invalid active progress or authorization.");
        if (dto.ActiveProjectId is not null && !known.Contains(dto.ActiveProjectId))
            throw new InvalidDataException($"Construction state {dto.CivilizationId} references an unknown active project.");
        if (dto.ActiveProjectId is null && (dto.ActiveProjectProgress != 0 || dto.ActiveProjectAuthorizationCredits != 0))
            throw new InvalidDataException($"Construction state {dto.CivilizationId} has active state without a project.");
        if (dto.ActiveProjectId is { } activeId && dto.ActiveProjectProgress > ConstructionRegistry.Get(activeId).IndustryCost + 0.0001)
            throw new InvalidDataException($"Construction state {dto.CivilizationId} exceeds active project materials.");
        if (dto.CompletedProjectIds.Any(id => !known.Contains(id)) || dto.CompletedProjectIds.Distinct(StringComparer.Ordinal).Count() != dto.CompletedProjectIds.Count)
            throw new InvalidDataException($"Construction state {dto.CivilizationId} has invalid completed projects.");
        if (dto.ActiveProjectId is { } active && dto.CompletedProjectIds.Contains(active, StringComparer.Ordinal))
            throw new InvalidDataException($"Construction state {dto.CivilizationId} overlaps active and completed projects.");
        if (dto.QueuedProjects.Count > ConstructionState.MaxQueuedProjects ||
            dto.QueuedProjects.Any(order => order is null || string.IsNullOrWhiteSpace(order.ProjectId) || !known.Contains(order.ProjectId) || !double.IsFinite(order.AuthorizationCredits) || order.AuthorizationCredits < 0) ||
            dto.QueuedProjects.Select(order => order.ProjectId).Distinct(StringComparer.Ordinal).Count() != dto.QueuedProjects.Count ||
            dto.QueuedProjects.Any(order => dto.CompletedProjectIds.Contains(order.ProjectId, StringComparer.Ordinal) || order.ProjectId == dto.ActiveProjectId))
            throw new InvalidDataException($"Construction state {dto.CivilizationId} has an invalid queued project.");
    }

    private static IList<ShipyardState> ToShipyardStates(
        IReadOnlyList<ShipyardSaveDto> dtos,
        IList<CivilizationState> civilizations,
        int saveFormatVersion)
    {
        var knownDesignIds = ShipDesignRegistry.All
            .Select(design => design.Id)
            .ToHashSet(StringComparer.Ordinal);
        var result = new List<ShipyardState>(dtos.Count);

        foreach (var dto in dtos)
        {
            if (!double.IsFinite(dto.ActiveBuildProgress) || dto.ActiveBuildProgress < 0 || !double.IsFinite(dto.ActiveAuthorizationCredits) || dto.ActiveAuthorizationCredits < 0 ||
                !double.IsFinite(dto.ReservedPopulationMillions) ||
                (dto.ReservedPopulationMillions < 0 && (dto.ActiveAuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(dto.ActiveOrderId) ||
                    dto.ReservedPopulationSourceColonyId is not null || !string.IsNullOrWhiteSpace(dto.ReservedPopulationSpeciesId))) ||
                dto.NextOrderSequence <= 0 ||
                dto.ReservedPopulationSourceColonyId is < 0 || dto.QueuedBuilds is null ||
                dto.QueuedBuilds.Any(build => build is null || !double.IsFinite(build.AuthorizationCredits) || build.AuthorizationCredits < 0 || !double.IsFinite(build.ReservedPopulationMillions) ||
                    (build.ReservedPopulationMillions < 0 && (build.AuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(build.OrderId) ||
                        build.ReservedPopulationSourceColonyId is not null || !string.IsNullOrWhiteSpace(build.ReservedPopulationSpeciesId))) || build.ReservedPopulationSourceColonyId is < 0 ||
                    (!string.IsNullOrWhiteSpace(build.OrderId) && !ShipyardState.IsValidPersistedOrderId(build.OrderId))))
                throw new InvalidDataException($"Shipyard {dto.CivilizationId} has invalid order accounting.");
            if (!string.IsNullOrWhiteSpace(dto.ActiveOrderId) && !ShipyardState.IsValidPersistedOrderId(dto.ActiveOrderId))
                throw new InvalidDataException($"Shipyard {dto.CivilizationId} has an invalid active order identity.");
            var orderIds = dto.QueuedBuilds.Where(build => !string.IsNullOrWhiteSpace(build.OrderId)).Select(build => build.OrderId!).ToList();
            if (!string.IsNullOrWhiteSpace(dto.ActiveOrderId)) orderIds.Add(dto.ActiveOrderId);
            if (orderIds.Distinct(StringComparer.Ordinal).Count() != orderIds.Count)
                throw new InvalidDataException($"Shipyard {dto.CivilizationId} has duplicate order identities.");
            var reservedPopulation = Math.Max(0.0, dto.ReservedPopulationMillions);
            var activeDesignId = string.IsNullOrWhiteSpace(dto.ActiveDesignId)
                ? null
                : dto.ActiveDesignId;

            if (activeDesignId is not null && !knownDesignIds.Contains(activeDesignId))
            {
                if (reservedPopulation > 0.0)
                {
                    throw new InvalidDataException(
                        $"Shipyard {dto.CivilizationId} active build '{activeDesignId}' is unknown but retains {reservedPopulation:0.###} million reserved population; refusing to discard reserved colonists.");
                }
                if (dto.ActiveBuildProgress != 0 || dto.ActiveAuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(dto.ActiveOrderId))
                    throw new InvalidDataException(
                        $"Shipyard {dto.CivilizationId} active build '{activeDesignId}' is unknown but retains refund metadata.");

                // An unknown zero-population build is safe to discard as corrupt queue metadata.
                activeDesignId = null;
            }

            if (activeDesignId is null && reservedPopulation > 0.0)
            {
                throw new InvalidDataException(
                    $"Shipyard {dto.CivilizationId} retains {reservedPopulation:0.###} million reserved population without a valid active design.");
            }
            if (activeDesignId is null &&
                (dto.ActiveBuildProgress != 0 || dto.ActiveAuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(dto.ActiveOrderId)))
                throw new InvalidDataException($"Shipyard {dto.CivilizationId} has active accounting without a valid active design.");
            if (activeDesignId is { } validatedActiveDesign &&
                dto.ActiveBuildProgress > ShipDesignRegistry.Get(validatedActiveDesign).IndustryCost + 0.0001)
                throw new InvalidDataException($"Shipyard {dto.CivilizationId} exceeds its active vessel material requirement.");

            var state = new ShipyardState
            {
                CivilizationId = dto.CivilizationId,
                NextOrderSequence = dto.NextOrderSequence,
                ActiveDesignId = activeDesignId,
                ActiveOrderId = dto.ActiveOrderId,
                ActiveBuildProgress = activeDesignId is null ? 0.0 : dto.ActiveBuildProgress,
                ActiveAuthorizationCredits = Math.Max(0.0, dto.ActiveAuthorizationCredits),
                ReservedPopulationMillions = reservedPopulation,
                ReservedPopulationSpeciesId = reservedPopulation > 0.0
                    ? ResolvePopulationSpeciesId(
                        dto.ReservedPopulationSpeciesId,
                        dto.CivilizationId,
                        civilizations,
                        saveFormatVersion,
                        $"shipyard {dto.CivilizationId} active reservation")
                    : null,
                ReservedPopulationSourceColonyId = reservedPopulation > 0.0 ? dto.ReservedPopulationSourceColonyId : null,
            };
            if (state.ActiveDesignId is not null && string.IsNullOrWhiteSpace(state.ActiveOrderId))
                state.ActiveOrderId = $"legacy-{dto.CivilizationId}-active";

            var availableQueueSlots = ShipyardState.MaxPendingBuilds -
                                      (state.ActiveDesignId is null ? 0 : 1);
            var acceptedQueueEntries = 0;

            foreach (var queued in dto.QueuedBuilds)
            {
                var queuedPopulation = Math.Max(0.0, queued.ReservedPopulationMillions);
                var hasKnownDesign = !string.IsNullOrWhiteSpace(queued.DesignId) &&
                                     knownDesignIds.Contains(queued.DesignId);

                if (!hasKnownDesign)
                {
                    if (queuedPopulation > 0.0)
                    {
                        throw new InvalidDataException(
                            $"Shipyard {dto.CivilizationId} queued build '{queued.DesignId}' is invalid but retains {queuedPopulation:0.###} million reserved population; refusing to discard reserved colonists.");
                    }
                    if (queued.AuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(queued.OrderId))
                        throw new InvalidDataException(
                            $"Shipyard {dto.CivilizationId} queued build '{queued.DesignId}' is invalid but retains refund metadata.");

                    // Invalid zero-population metadata can be dropped without changing people.
                    continue;
                }

                if (acceptedQueueEntries >= Math.Max(0, availableQueueSlots))
                {
                    if (queuedPopulation > 0.0)
                    {
                        throw new InvalidDataException(
                            $"Shipyard {dto.CivilizationId} queue exceeds the bounded maximum while overflow build '{queued.DesignId}' retains {queuedPopulation:0.###} million reserved population; refusing to truncate reserved colonists.");
                    }
                    if (queued.AuthorizationCredits != 0 || !string.IsNullOrWhiteSpace(queued.OrderId))
                        throw new InvalidDataException(
                            $"Shipyard {dto.CivilizationId} queue overflow retains paid authorization or order identity metadata.");

                    // Overflow with no population payload is safe to clamp away.
                    continue;
                }

                state.QueuedBuilds.Add(new ShipBuildOrderState
                {
                    OrderId = string.IsNullOrWhiteSpace(queued.OrderId) ? $"legacy-{dto.CivilizationId}-queued-{acceptedQueueEntries + 1}" : queued.OrderId,
                    DesignId = queued.DesignId,
                    AuthorizationCredits = Math.Max(0.0, queued.AuthorizationCredits),
                    ReservedPopulationMillions = queuedPopulation,
                    ReservedPopulationSpeciesId = queuedPopulation > 0.0
                        ? ResolvePopulationSpeciesId(
                            queued.ReservedPopulationSpeciesId,
                            dto.CivilizationId,
                            civilizations,
                            saveFormatVersion,
                            $"shipyard {dto.CivilizationId} queued reservation")
                        : null,
                    ReservedPopulationSourceColonyId = queuedPopulation > 0.0 ? queued.ReservedPopulationSourceColonyId : null,
                });
                acceptedQueueEntries++;
            }

            ValidateLoadedShipyardIdentity(state);

            result.Add(state);
        }

        return result;
    }

    private static CivilizationKnowledgeState ToKnowledge(
        IReadOnlyList<CivilizationKnowledgeSaveDto> dtos)
    {
        var knowledge = new CivilizationKnowledgeState();
        foreach (var dto in dtos)
        {
            if (dto.GalacticCoreExplored && !dto.GalacticCoreAccessUnlocked)
                throw new InvalidDataException("Landmark exploration requires its access unlock.");
            if (dto.GalacticCoreAccessUnlocked) knowledge.UnlockGalacticCoreAccess(dto.CivilizationId);
            if (dto.GalacticCoreExplored) knowledge.RecordGalacticCoreExploration(dto.CivilizationId);
            if (dto.SystemSurveys.Count == 0)
            {
                // Legacy saves used "known" to mean all system facts were available.
                foreach (var systemId in dto.KnownSystemIds)
                    knowledge.MarkSystemFullySurveyed(dto.CivilizationId, systemId);
            }
            else
            {
                foreach (var systemId in dto.KnownSystemIds)
                    knowledge.RevealSystem(dto.CivilizationId, systemId);

                foreach (var survey in dto.SystemSurveys)
                {
                    switch (survey.Level)
                    {
                        case SystemSurveyLevel.Unknown:
                            break;
                        case SystemSurveyLevel.Detected:
                            knowledge.RevealSystem(dto.CivilizationId, survey.SystemId);
                            break;
                        case SystemSurveyLevel.PartiallySurveyed:
                            knowledge.AdvanceSystemSurvey(
                                dto.CivilizationId,
                                survey.SystemId,
                                Math.Clamp(survey.Progress, 0.000001, 0.999999));
                            break;
                        case SystemSurveyLevel.FullySurveyed:
                            knowledge.MarkSystemFullySurveyed(
                                dto.CivilizationId,
                                survey.SystemId);
                            break;
                    }
                }
            }

            foreach (var civilizationId in dto.KnownCivilizationIds)
                knowledge.RevealCivilization(dto.CivilizationId, civilizationId);
        }

        return knowledge;
    }

    private static void ValidatePlanetaryReferences(GalaxyState galaxy)
    {
        galaxy.ActiveCombatEncounter?.Validate(galaxy);
        if (galaxy.CombatIntelligence.Count > 4096 ||
            galaxy.CombatIntelligence.GroupBy(x => (x.ObserverId, x.FleetId)).Any(x => x.Count() != 1) ||
            galaxy.CombatIntelligence.Any(x => x.ObserverId < 0 || !galaxy.Fleets.Any(f => f.Id == x.FleetId) ||
                !double.IsFinite(x.Power) || x.Power < 0 || !double.IsFinite(x.ObservedDay) || x.ObservedDay < 0 ||
                string.IsNullOrWhiteSpace(x.Evidence)))
            throw new InvalidDataException("Campaign combat intelligence is invalid, duplicated, or unbounded.");
        if (galaxy.Colonies.Any(colony => colony.SurfaceBuildings is { Count: > 0 }))
        {
            foreach (var economy in galaxy.Economies)
                ValidateEconomyStock(economy.CivilizationId, economy.Credits, economy.Industry, economy.Science);
            if (galaxy.Civilizations.Any(civilization => galaxy.Economies.Count(item => item.CivilizationId == civilization.Id) != 1))
                throw new InvalidDataException("Surface construction requires one authoritative economy for each civilization.");
        }
        var bodies = galaxy.PlanetaryBodies.ToDictionary(body => body.Id);
        var systemIds = galaxy.Systems.Select(system => system.Id).ToHashSet();

        foreach (var colony in galaxy.Colonies)
        {
            if (!Enum.IsDefined(colony.Kind))
                throw new InvalidDataException($"Settlement {colony.Id} has an unknown settlement kind.");
            if (!double.IsFinite(colony.StoredExtractedMaterials) || colony.StoredExtractedMaterials < 0.0)
                throw new InvalidDataException($"Settlement {colony.Id} has invalid extracted-material storage.");
            if (colony.RemainingExtractableMaterials is double remainingDeposit &&
                (!double.IsFinite(remainingDeposit) || remainingDeposit < 0.0))
                throw new InvalidDataException($"Settlement {colony.Id} has an invalid remaining resource deposit.");
            if (colony.SurfaceHubLevel is < 1 or > 3)
                throw new InvalidDataException($"Settlement {colony.Id} has an invalid surface hub level.");
            if (!double.IsFinite(colony.StoredFoodPopulationDaysMillions) || colony.StoredFoodPopulationDaysMillions < 0.0 ||
                !double.IsFinite(colony.StoredWaterPopulationDaysMillions) || colony.StoredWaterPopulationDaysMillions < 0.0)
                throw new InvalidDataException($"Settlement {colony.Id} has invalid food or potable-water reserves.");
            SurfaceConstruction.Validate(colony);
            if (colony.SurfaceBuildings.Count > SurfaceConstruction.GetBuildingCapacity(colony))
                throw new InvalidDataException($"Settlement {colony.Id} exceeds its represented hub module capacity.");
            if (colony.PlanetaryBodyId is not int bodyId)
            {
                if (colony.SurfaceBuildings.Count > 0)
                    throw new InvalidDataException($"Colony {colony.Id} has surface buildings without an exact planetary body.");
                continue;
            }

            if (!bodies.TryGetValue(bodyId, out var body) || body.SystemId != colony.SystemId)
            {
                throw new InvalidDataException(
                    $"Colony {colony.Id} references planetary body {bodyId} outside system {colony.SystemId}.");
            }
            if (colony.SurfaceBuildings.Count > 0 && !body.Environment.HasSolidSurface)
                throw new InvalidDataException($"Colony {colony.Id} has buildings on a body without solid ground.");
            var outpostOperations = ResourceOutpostOperations.GetSnapshot(galaxy, colony);
            if (outpostOperations.IsResourceOutpost && colony.StoredExtractedMaterials > outpostOperations.StorageCapacity + 0.000001)
                throw new InvalidDataException($"Settlement {colony.Id} stores more extracted material than its represented capacity.");
            if (outpostOperations.IsResourceOutpost && colony.RemainingExtractableMaterials is double remaining &&
                remaining + colony.StoredExtractedMaterials > outpostOperations.InitialDepositMaterials + 0.000001)
                throw new InvalidDataException($"Settlement {colony.Id} has more remaining and stored material than its represented deposit.");
        }

        foreach (var fleet in galaxy.Fleets)
        {
            fleet.TacticalLoadout?.Validate();
            fleet.TacticalVessel?.Validate();
            if (fleet.TacticalVessel is not null && fleet.TacticalVessel.Id != fleet.Id)
                throw new InvalidDataException($"Fleet {fleet.Id} has tactical state for a different vessel identity.");
            if (fleet.DesignId is not null &&
                (!ShipDesignRegistry.TryGet(fleet.DesignId, out var design) || design!.Role != fleet.Role))
                throw new InvalidDataException($"Fleet {fleet.Id} references an unknown or role-incompatible ship design.");
            if (!double.IsFinite(fleet.MaximumLegRangeLightYears) || fleet.MaximumLegRangeLightYears <= 0.0)
                throw new InvalidDataException($"Fleet {fleet.Id} has an invalid maximum interstellar leg range.");
            if (!double.IsFinite(fleet.FuelCapacityLightYears) || fleet.FuelCapacityLightYears <= 0.0 ||
                !double.IsFinite(fleet.FuelRemainingLightYears) || fleet.FuelRemainingLightYears < 0.0 ||
                fleet.FuelRemainingLightYears > fleet.FuelCapacityLightYears + 0.000001)
                throw new InvalidDataException($"Fleet {fleet.Id} has invalid interstellar fuel endurance.");
            if (!double.IsFinite(fleet.CargoMaterialCapacity) || fleet.CargoMaterialCapacity < 0.0 ||
                !double.IsFinite(fleet.CargoMaterials) || fleet.CargoMaterials < 0.0 ||
                fleet.CargoMaterials > fleet.CargoMaterialCapacity + 0.000001)
                throw new InvalidDataException($"Fleet {fleet.Id} has invalid freight cargo state.");
            if ((fleet.FreightTargetOutpostId is not null || fleet.FreightHomeColonyId is not null || fleet.CargoMaterials > 0.0) &&
                fleet.Role != FleetRole.Logistics)
                throw new InvalidDataException($"Fleet {fleet.Id} carries freight mission state without a logistics role.");
            if (fleet.FreightTargetOutpostId is int outpostId && !galaxy.Colonies.Any(colony =>
                    colony.Id == outpostId && colony.CivilizationId == fleet.CivilizationId && colony.Kind == SettlementKind.ResourceOutpost))
                throw new InvalidDataException($"Fleet {fleet.Id} references an invalid freight outpost.");
            if (fleet.FreightHomeColonyId is int freightHomeId && !galaxy.Colonies.Any(colony =>
                    colony.Id == freightHomeId && colony.CivilizationId == fleet.CivilizationId && colony.Kind == SettlementKind.Colony))
                throw new InvalidDataException($"Fleet {fleet.Id} references an invalid freight home colony.");
            if (fleet.PlannedRouteSystemIds.Any(systemId => !systemIds.Contains(systemId)))
                throw new InvalidDataException($"Fleet {fleet.Id} has a route waypoint outside the generated galaxy.");
            if (fleet.DestinationSystemId is null && fleet.PlannedRouteSystemIds.Count > 0)
                throw new InvalidDataException($"Fleet {fleet.Id} has route waypoints without an active destination.");
            if (fleet.PlannedRouteSystemIds.Count > 0 &&
                fleet.PlannedRouteSystemIds[^1] != fleet.DestinationSystemId)
                throw new InvalidDataException($"Fleet {fleet.Id} route does not end at its mission destination.");
            // Destroyed ships retain inert historical metadata until a future cleanup/migration.
            // Active unsupported roles are rejected because the civilian command boundary cannot
            // create those holds.
            if (fleet.HoldRequested && fleet.IsActive && fleet.Role is not (FleetRole.Scout or FleetRole.Science or FleetRole.Colony))
                throw new InvalidDataException($"Fleet {fleet.Id} has an unsupported civilian hold order.");
            if ((fleet.ReturnToBaseRequested || fleet.ReturnToBaseFailureReason is not null) && fleet.IsActive &&
                fleet.Role is not (FleetRole.Scout or FleetRole.Science or FleetRole.Colony))
                throw new InvalidDataException($"Fleet {fleet.Id} has an unsupported civilian return order.");

            if (!double.IsFinite(fleet.SettlementDaysCompleted) || fleet.SettlementDaysCompleted < 0 ||
                fleet.SettlementDaysCompleted > ColonizationSimulation.EstablishmentDays(fleet) ||
                !double.IsFinite(fleet.ReconnaissanceDaysCompleted) || fleet.ReconnaissanceDaysCompleted < 0 ||
                fleet.ReconnaissanceDaysCompleted > ExplorationSimulation.ScoutReconnaissanceDays)
                throw new InvalidDataException($"Fleet {fleet.Id} has invalid local-work progress.");
            if ((fleet.SettlementBodyId is null && fleet.SettlementDaysCompleted > 0) ||
                (fleet.ReconnaissanceSystemId is null && fleet.ReconnaissanceDaysCompleted > 0) ||
                (fleet.PreventAutomaticSettlement && (fleet.Role != FleetRole.Colony || fleet.SettlementBodyId is not null)))
                throw new InvalidDataException($"Fleet {fleet.Id} has local work without a valid order.");
            if (fleet.SettlementBodyId is int site &&
                (fleet.Role != FleetRole.Colony || !bodies.TryGetValue(site, out var siteBody) ||
                 fleet.CurrentSystemId != siteBody.SystemId || fleet.DestinationSystemId is not null))
                throw new InvalidDataException($"Fleet {fleet.Id} has an invalid settlement work site.");
            if (fleet.ReconnaissanceSystemId is int recon && (fleet.Role != FleetRole.Scout || !systemIds.Contains(recon)))
                throw new InvalidDataException($"Fleet {fleet.Id} has an invalid reconnaissance work site.");
            if (fleet.DestinationPlanetaryBodyId is not int bodyId)
                continue;

            var targetSystemId = fleet.DestinationSystemId ?? (fleet.SettlementBodyId == bodyId ? fleet.CurrentSystemId : null);
            if (fleet.Role != FleetRole.Colony || targetSystemId is not int systemId)
            {
                throw new InvalidDataException(
                    $"Fleet {fleet.Id} has a planetary-body target without an active colony-system destination.");
            }

            if (!bodies.TryGetValue(bodyId, out var body) || body.SystemId != systemId)
            {
                throw new InvalidDataException(
                    $"Fleet {fleet.Id} targets planetary body {bodyId} outside destination system {systemId}.");
            }
        }
    }

    private static void ValidatePlanetaryCatalog(
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<StarSystemState> systems)
    {
        if (bodies is null || bodies.Count == 0)
            throw new InvalidDataException("The authoritative planetary catalog is missing or empty.");
        if (bodies.Select(body => body.Id).Distinct().Count() != bodies.Count)
            throw new InvalidDataException("The authoritative planetary catalog contains duplicate body IDs.");

        var systemIds = systems.Select(system => system.Id).ToHashSet();
        var byId = bodies.ToDictionary(body => body.Id);
        foreach (var body in bodies)
        {
            if (!Enum.IsDefined(body.Kind))
                throw new InvalidDataException($"Planetary body {body.Id} has an unknown body kind.");
            if (body.Environment is null || !Enum.IsDefined(body.Environment.Atmosphere) ||
                !Enum.IsDefined(body.Environment.AvailableSolvent))
                throw new InvalidDataException($"Planetary body {body.Id} has an invalid environment.");
            try { body.Validated(); }
            catch (Exception exception) when (exception is InvalidOperationException or NullReferenceException)
            {
                throw new InvalidDataException($"Planetary body {body.Id} has invalid physical values.", exception);
            }
            if (!systemIds.Contains(body.SystemId))
                throw new InvalidDataException($"Planetary body {body.Id} references unknown system {body.SystemId}.");

            var visited = new HashSet<int> { body.Id };
            var ancestor = body;
            while (ancestor.ParentBodyId is int ancestorId)
            {
                if (!visited.Add(ancestorId))
                    throw new InvalidDataException($"Planetary body {body.Id} belongs to a cyclic parent chain.");
                if (!byId.TryGetValue(ancestorId, out var nextAncestor)) break;
                ancestor = nextAncestor;
            }
            if ((body.Kind is PlanetaryBodyKind.Planet or PlanetaryBodyKind.DwarfPlanet) && body.ParentBodyId is not null)
                throw new InvalidDataException($"Planetary body {body.Id} is a primary body with a parent body.");
            if (body.Kind == PlanetaryBodyKind.Moon && body.ParentBodyId is not int)
                throw new InvalidDataException($"Planetary body {body.Id} is a moon without a parent planet.");
            if (body.ParentBodyId is int referencedParent &&
                (!byId.TryGetValue(referencedParent, out var parent) || parent.Kind != PlanetaryBodyKind.Planet ||
                 parent.SystemId != body.SystemId))
                throw new InvalidDataException($"Planetary body {body.Id} has an invalid or cross-system parent.");

        }
    }

    private static void ValidateEconomyStock(int civilizationId, double credits, double industry, double science)
    {
        if (!double.IsFinite(credits) || credits < 0 || !double.IsFinite(industry) || industry < 0 || !double.IsFinite(science) || science < 0)
            throw new InvalidDataException($"Civilization {civilizationId} has invalid economy stock; resources must be finite and nonnegative.");
    }

    private static List<StarSystemSaveDto> ToSystemDtos(
        IReadOnlyList<StarSystemState> systems) =>
        systems.Select(s => new StarSystemSaveDto
            {
                Id = s.Id,
                Name = s.Name,
                X = s.Position.X,
                Y = s.Position.Y,
                Archetype = s.Archetype,
                HasHabitableWorld = s.HasHabitableWorld,
                HasAnomaly = s.HasAnomaly,
                HasRareResource = s.HasRareResource,
                HasPreWarpCivilization = s.HasPreWarpCivilization,
                CatalogPresetId = s.CatalogPresetId,
                StellarClass = s.StellarClass,
                SecondaryStellarClass = s.SecondaryStellarClass,
                TertiaryStellarClass = s.TertiaryStellarClass,
                GalacticDepthLightYears = s.GalacticDepthLightYears,
                StellarCatalogId = s.StellarCatalogId,
            })
            .ToList();

    private static List<PlanetaryBodySaveDto?> ToPlanetaryBodyDtos(
        IReadOnlyList<PlanetaryBodyState> bodies) => bodies.Select(body => (PlanetaryBodySaveDto?)new PlanetaryBodySaveDto
        {
            Id = body.Id,
            SystemId = body.SystemId,
            ParentBodyId = body.ParentBodyId,
            OrbitIndex = body.OrbitIndex,
            Name = body.Name,
            Kind = body.Kind,
            RadiusEarth = body.RadiusEarth,
            MassEarth = body.MassEarth,
            Environment = new PlanetaryEnvironmentSaveDto
            {
                GravityG = body.Environment.GravityG,
                TemperatureKelvin = body.Environment.TemperatureKelvin,
                PressureKPa = body.Environment.PressureKPa,
                Atmosphere = body.Environment.Atmosphere,
                AvailableSolvent = body.Environment.AvailableSolvent,
                RadiationHazard = body.Environment.RadiationHazard,
                IsImmersedEnvironment = body.Environment.IsImmersedEnvironment,
                HasSolidSurface = body.Environment.HasSolidSurface,
            },
            LegacyColonizationCandidate = body.LegacyColonizationCandidate,
            HasRareResource = body.HasRareResource,
            HasAnomaly = body.HasAnomaly,
            HasPreWarpCivilization = body.HasPreWarpCivilization,
            OrbitalEccentricity = body.OrbitalEccentricity,
            OrbitalInclinationDegrees = body.OrbitalInclinationDegrees,
        }).ToList();

    private static IReadOnlyList<PlanetaryBodyState> ToPlanetaryBodies(
        List<PlanetaryBodySaveDto?>? dtos,
        IReadOnlyList<StarSystemState> systems)
    {
        if (dtos is null || dtos.Count == 0)
            throw new InvalidDataException($"Format v{CurrentFormatVersion} save is missing its authoritative planetary catalog.");
        if (dtos.Any(dto => dto is null))
            throw new InvalidDataException($"Format v{CurrentFormatVersion} planetary catalog contains a null body entry.");
        var bodies = dtos.Select(item =>
        {
            var dto = item!;
            return new PlanetaryBodyState(
            dto.Id, dto.SystemId, dto.ParentBodyId, dto.OrbitIndex, dto.Name, dto.Kind,
            dto.RadiusEarth, dto.MassEarth,
            dto.Environment is null ? null! : new PlanetaryEnvironmentState(
                dto.Environment.GravityG, dto.Environment.TemperatureKelvin, dto.Environment.PressureKPa,
                dto.Environment.Atmosphere, dto.Environment.AvailableSolvent, dto.Environment.RadiationHazard,
                dto.Environment.IsImmersedEnvironment, dto.Environment.HasSolidSurface),
            dto.LegacyColonizationCandidate, dto.HasRareResource, dto.HasAnomaly, dto.HasPreWarpCivilization,
            dto.OrbitalEccentricity, dto.OrbitalInclinationDegrees);
        }).ToArray();
        ValidatePlanetaryCatalog(bodies, systems);
        return Array.AsReadOnly(bodies);
    }

    private static List<CivilizationSaveDto> ToCivilizationDtos(
        IEnumerable<CivilizationState> civilizations) =>
        civilizations.Select(c => new CivilizationSaveDto
            {
                Id = c.Id,
                Name = c.Name,
                HomeSystemId = c.HomeSystemId,
                Archetype = c.Archetype,
                Aggression = c.Traits.Aggression,
                Territoriality = c.Traits.Territoriality,
                Greed = c.Traits.Greed,
                ScientificCuriosity = c.Traits.ScientificCuriosity,
                RiskTolerance = c.Traits.RiskTolerance,
                SurvivalPriority = c.Traits.SurvivalPriority,
                HonorBound = c.Traits.HonorBound,
                IsPlayer = c.IsPlayer,
                DevelopmentStage = c.DevelopmentStage,
                IsSeededAncient = c.IsSeededAncient,
                ExpansionAllowed = c.ExpansionAllowed,
                NeutralUnlessProvoked = c.NeutralUnlessProvoked,
                SpeciesId = RequireKnownSpeciesId(c.SpeciesId, c.Id),
                Leadership = new Dictionary<string, CivilizationCharacter>(c.Leadership.Offices),
            })
            .ToList();

    private static List<FleetSaveDto> ToFleetDtos(
        IEnumerable<FleetState> fleets)
    {
        return fleets.Select(fleet =>
        {
            var population = Math.Max(0.0, fleet.EmbarkedPopulationMillions);
            var combat = CombatProfileRegistry.EnsureState(fleet);

            return new FleetSaveDto
            {
                Id = fleet.Id,
                CivilizationId = fleet.CivilizationId,
                Name = fleet.Name,
                Role = fleet.Role,
                DesignId = fleet.DesignId,
                X = fleet.Position.X,
                Y = fleet.Position.Y,
                CurrentSystemId = fleet.CurrentSystemId,
                DestinationSystemId = fleet.DestinationSystemId,
                TransitPhase = fleet.TransitPhase,
                TransitOriginSystemId = fleet.TransitOriginSystemId,
                TransitTargetSystemId = fleet.TransitTargetSystemId,
                TransitProgress = fleet.TransitProgress,
                LocalTransitStartX = fleet.LocalTransitStart.X,
                LocalTransitStartY = fleet.LocalTransitStart.Y,
                LocalTransitPositionX = fleet.LocalTransitPosition.X,
                LocalTransitPositionY = fleet.LocalTransitPosition.Y,
                LocalTransitTargetX = fleet.LocalTransitTarget.X,
                LocalTransitTargetY = fleet.LocalTransitTarget.Y,
                PlannedRouteSystemIds = fleet.PlannedRouteSystemIds.ToList(),
                HoldRequested = fleet.HoldRequested,
                ReturnToBaseRequested = fleet.ReturnToBaseRequested,
                ReturnToBaseFailureReason = fleet.ReturnToBaseFailureReason,
                MissionOrderRevision = fleet.MissionOrderRevision,
                DestinationPlanetaryBodyId = fleet.DestinationPlanetaryBodyId,
                SettlementBodyId = fleet.SettlementBodyId,
                PreventAutomaticSettlement = fleet.PreventAutomaticSettlement,
                SettlementDaysCompleted = fleet.SettlementDaysCompleted,
                ReconnaissanceSystemId = fleet.ReconnaissanceSystemId,
                ReconnaissanceDaysCompleted = fleet.ReconnaissanceDaysCompleted,
                FreightTargetOutpostId = fleet.FreightTargetOutpostId,
                FreightHomeColonyId = fleet.FreightHomeColonyId,
                CargoMaterialCapacity = fleet.CargoMaterialCapacity,
                CargoMaterials = fleet.CargoMaterials,
                StrategicSpeed = fleet.StrategicSpeed,
                MaximumLegRangeLightYears = fleet.MaximumLegRangeLightYears,
                FuelCapacityLightYears = fleet.FuelCapacityLightYears,
                FuelRemainingLightYears = fleet.FuelRemainingLightYears,
                SensorRange = fleet.SensorRange,
                IsActive = fleet.IsActive,
                EmbarkedPopulationMillions = population,
                EmbarkedPopulationSpeciesId = population > 0.0
                    ? RequireKnownPopulationSpeciesId(
                        fleet.EmbarkedPopulationSpeciesId,
                        $"fleet {fleet.Id}")
                    : null,
                Combat = new FleetCombatSaveDto
                {
                    ProfileId = combat.ProfileId,
                    Shields = combat.Shields,
                    Armor = combat.Armor,
                    Hull = combat.Hull,
                    WeaponCooldownRemainingDays = combat.WeaponCooldownRemainingDays,
                    Order = combat.Order,
                    TargetFleetId = combat.TargetFleetId,
                    DefendSystemId = combat.DefendSystemId,
                    RetreatProgressDays = combat.RetreatProgressDays,
                    RetreatStarted = combat.RetreatStarted,
                    IsDisengaged = combat.IsDisengaged,
                    DisengagedSystemId = combat.DisengagedSystemId,
                },
                TacticalLoadout = fleet.TacticalLoadout is null ? null : CloneLoadout(fleet.TacticalLoadout),
                TacticalVessel = fleet.TacticalVessel is null ? null : CloneVessel(fleet.TacticalVessel),
            };
        }).ToList();
    }

    private static List<ColonySaveDto> ToColonyDtos(
        IEnumerable<ColonyState> colonies) =>
        colonies.Select(c => new ColonySaveDto
            {
                Id = c.Id,
                CivilizationId = c.CivilizationId,
                SystemId = c.SystemId,
                PlanetaryBodyId = c.PlanetaryBodyId,
                Name = c.Name,
                Kind = c.Kind,
                PopulationSpeciesId = RequireKnownPopulationSpeciesId(
                    c.PopulationSpeciesId,
                    $"colony {c.Id}"),
                PopulationMillions = c.PopulationMillions,
                Infrastructure = c.Infrastructure,
                Stability = c.Stability,
                StoredFoodPopulationDaysMillions = c.StoredFoodPopulationDaysMillions,
                StoredWaterPopulationDaysMillions = c.StoredWaterPopulationDaysMillions,
                StoredExtractedMaterials = c.StoredExtractedMaterials,
                RemainingExtractableMaterials = c.RemainingExtractableMaterials,
                SurfaceHubLevel = c.SurfaceHubLevel,
                SurfaceHubUpgradeDaysRemaining = c.SurfaceHubUpgradeDaysRemaining,
                SurfaceBuildings = c.SurfaceBuildings,
            })
            .ToList();

    private static List<EconomySaveDto> ToEconomyDtos(
        IReadOnlyList<CivilizationEconomyState> economies) =>
        economies.Select(e =>
        {
            if (e.IndustryPriority is not null && !Enum.IsDefined(e.IndustryPriority.Value))
                throw new InvalidDataException($"Civilization {e.CivilizationId} has an unknown industry priority.");
            return new EconomySaveDto
            {
                CivilizationId = e.CivilizationId,
                Credits = e.Credits,
                Industry = e.Industry,
                Science = e.Science,
                LastCreditsPerSecond = e.LastCreditsPerSecond,
                LastIndustryPerSecond = e.LastIndustryPerSecond,
                LastSciencePerSecond = e.LastSciencePerSecond,
                LastResearchSpendingPerDay = e.LastResearchSpendingPerDay,
                LastResearchFundingFraction = e.LastResearchFundingFraction,
                OperatingArrears = e.OperatingArrears,
                LastBaseOperationsFundingFraction = e.LastBaseOperationsFundingFraction,
                IndustryPriority = e.IndustryPriority,
            };
        })
            .ToList();

    private static List<TechnologySaveDto> ToTechnologyDtos(
        IEnumerable<TechnologyState> technologies) =>
        technologies.Select(t => new TechnologySaveDto
            {
                CivilizationId = t.CivilizationId,
                CompletedTechnologyIds = t.CompletedTechnologyIds.OrderBy(id => id).ToList(),
                ActiveResearchId = t.ActiveResearchId,
                ActiveResearchProgress = t.ActiveResearchProgress,
            })
            .ToList();

    private static List<ConstructionSaveDto> ToConstructionDtos(
        IEnumerable<ConstructionState> states) =>
        states.Select(c => new ConstructionSaveDto
            {
                CivilizationId = c.CivilizationId,
                CompletedProjectIds = c.CompletedProjectIds.OrderBy(id => id).ToList(),
                ActiveProjectId = c.ActiveProjectId,
                ActiveProjectProgress = c.ActiveProjectProgress,
                ActiveProjectAuthorizationCredits = c.ActiveProjectAuthorizationCredits,
                QueuedProjects = c.QueuedProjects.Select(order => new QueuedConstructionProjectSaveDto
                {
                    ProjectId = order.ProjectId,
                    AuthorizationCredits = order.AuthorizationCredits,
                }).ToList(),
            })
            .ToList();

    private static List<ShipyardSaveDto> ToShipyardDtos(
        IEnumerable<ShipyardState> states) =>
        states.Select(s =>
        {
            ValidateShipyardStateForSave(s);
            var reservedPopulation = Math.Max(0.0, s.ReservedPopulationMillions);
            return new ShipyardSaveDto
            {
                CivilizationId = s.CivilizationId,
                ActiveDesignId = s.ActiveDesignId,
                NextOrderSequence = s.NextOrderSequence,
                ActiveOrderId = s.ActiveOrderId,
                ActiveBuildProgress = s.ActiveBuildProgress,
                ActiveAuthorizationCredits = s.ActiveAuthorizationCredits,
                ReservedPopulationMillions = reservedPopulation,
                ReservedPopulationSpeciesId = reservedPopulation > 0.0
                    ? RequireKnownPopulationSpeciesId(
                        s.ReservedPopulationSpeciesId,
                        $"shipyard {s.CivilizationId} active reservation")
                    : null,
                ReservedPopulationSourceColonyId = reservedPopulation > 0.0 ? s.ReservedPopulationSourceColonyId : null,
                QueuedBuilds = s.QueuedBuilds
                    .Take(ShipyardState.MaxPendingBuilds)
                    .Select(build =>
                    {
                        var queuedPopulation = Math.Max(0.0, build.ReservedPopulationMillions);
                        return new QueuedShipBuildSaveDto
                        {
                            OrderId = build.OrderId,
                            DesignId = build.DesignId,
                            AuthorizationCredits = build.AuthorizationCredits,
                            ReservedPopulationMillions = queuedPopulation,
                            ReservedPopulationSpeciesId = queuedPopulation > 0.0
                                ? RequireKnownPopulationSpeciesId(
                                    build.ReservedPopulationSpeciesId,
                                    $"shipyard {s.CivilizationId} queued reservation")
                                : null,
                            ReservedPopulationSourceColonyId = queuedPopulation > 0.0 ? build.ReservedPopulationSourceColonyId : null,
                        };
                    })
                    .ToList(),
            };
        }).ToList();

    private static void ValidateLoadedShipyardIdentity(ShipyardState state)
    {
        var identities = state.QueuedBuilds.Select(order => order.OrderId).ToList();
        if (state.ActiveDesignId is not null) identities.Add(state.ActiveOrderId ?? string.Empty);
        ValidateShipyardIdentities(state.CivilizationId, state.NextOrderSequence, identities);
    }

    private static void ValidateShipyardIdentities(int civilizationId, long nextOrderSequence, IReadOnlyList<string> identities)
    {
        if (identities.Any(id => !ShipyardState.IsValidPersistedOrderId(id)) ||
            identities.Distinct(StringComparer.Ordinal).Count() != identities.Count)
            throw new InvalidDataException($"Shipyard {civilizationId} has invalid or duplicate order identities.");
        var canonicalSequences = identities
            .Select(id => ShipyardState.TryReadCanonicalSequence(id, civilizationId, out var sequence) ? sequence : 0)
            .ToArray();
        if (canonicalSequences.Length > 0 && canonicalSequences.Max() >= nextOrderSequence)
            throw new InvalidDataException($"Shipyard {civilizationId} order sequence does not follow its existing identities.");
    }

    private static void ValidateShipyardStateForSave(ShipyardState state)
    {
        if (state.NextOrderSequence <= 0 ||
            !double.IsFinite(state.ActiveBuildProgress) || state.ActiveBuildProgress < 0 ||
            !double.IsFinite(state.ActiveAuthorizationCredits) || state.ActiveAuthorizationCredits < 0 ||
            !double.IsFinite(state.ReservedPopulationMillions) || state.ReservedPopulationMillions < 0 ||
            state.ReservedPopulationSourceColonyId is < 0)
            throw new InvalidOperationException($"Shipyard {state.CivilizationId} has invalid order accounting and cannot be saved.");

        var knownDesigns = ShipDesignRegistry.All.Select(design => design.Id).ToHashSet(StringComparer.Ordinal);
        if (state.ActiveDesignId is null)
        {
            if (state.ActiveBuildProgress != 0 || state.ActiveAuthorizationCredits != 0 || state.ReservedPopulationMillions != 0 || state.ActiveOrderId is not null)
                throw new InvalidOperationException($"Shipyard {state.CivilizationId} has active accounting without an active design.");
        }
        else if (!knownDesigns.Contains(state.ActiveDesignId) &&
                 (state.ActiveBuildProgress != 0 || state.ActiveAuthorizationCredits != 0 || state.ReservedPopulationMillions != 0 || !string.IsNullOrWhiteSpace(state.ActiveOrderId)))
            throw new InvalidOperationException(state.ReservedPopulationMillions > 0
                ? $"Shipyard {state.CivilizationId} has reserved colonists attached to an unknown active design."
                : $"Shipyard {state.CivilizationId} would lose active refund metadata for an unknown design.");
        else if (knownDesigns.Contains(state.ActiveDesignId) &&
                 state.ActiveBuildProgress > ShipDesignRegistry.Get(state.ActiveDesignId).IndustryCost + 0.0001)
            throw new InvalidOperationException($"Shipyard {state.CivilizationId} exceeds its active vessel material requirement.");

        var persistedOrders = new List<ShipBuildOrderState>();
        var queueCapacity = Math.Max(0, ShipyardState.MaxPendingBuilds -
            (knownDesigns.Contains(state.ActiveDesignId ?? string.Empty) ? 1 : 0));
        for (var index = 0; index < state.QueuedBuilds.Count; index++)
        {
            var order = state.QueuedBuilds[index];
            if (!double.IsFinite(order.AuthorizationCredits) || order.AuthorizationCredits < 0 ||
                !double.IsFinite(order.ReservedPopulationMillions) || order.ReservedPopulationMillions < 0 ||
                order.ReservedPopulationSourceColonyId is < 0)
                throw new InvalidOperationException($"Shipyard {state.CivilizationId} has invalid queued order accounting.");
            var carriesRecoverableMetadata = order.AuthorizationCredits != 0 || order.ReservedPopulationMillions != 0 ||
                                             !string.IsNullOrWhiteSpace(order.OrderId);
            if (index >= ShipyardState.MaxPendingBuilds)
            {
                if (carriesRecoverableMetadata)
                    throw new InvalidOperationException($"Shipyard {state.CivilizationId} has queued overflow carrying paid authorization, identity, or population metadata.");
                continue;
            }
            if (!knownDesigns.Contains(order.DesignId) &&
                carriesRecoverableMetadata)
                throw new InvalidOperationException($"Shipyard {state.CivilizationId} would lose queued refund metadata for an unknown design.");
            if (!knownDesigns.Contains(order.DesignId))
                continue;
            if (persistedOrders.Count >= queueCapacity)
            {
                if (carriesRecoverableMetadata)
                    throw new InvalidOperationException($"Shipyard {state.CivilizationId} has queued overflow carrying paid authorization, identity, or population metadata.");
                continue;
            }
            persistedOrders.Add(order);
        }
        var persistedIdentities = persistedOrders
            .Select((order, index) => string.IsNullOrWhiteSpace(order.OrderId)
                ? $"legacy-{state.CivilizationId}-queued-{index + 1}"
                : order.OrderId)
            .ToList();
        if (knownDesigns.Contains(state.ActiveDesignId ?? string.Empty))
            persistedIdentities.Add(string.IsNullOrWhiteSpace(state.ActiveOrderId)
                ? $"legacy-{state.CivilizationId}-active"
                : state.ActiveOrderId);
        ValidateShipyardIdentities(state.CivilizationId, state.NextOrderSequence, persistedIdentities);
    }

    private static List<CivilizationKnowledgeSaveDto> ToKnowledgeDtos(
        CivilizationKnowledgeState knowledge)
    {
        var snapshot = knowledge.Snapshot();
        var ids = snapshot.Systems.Keys
            .Concat(snapshot.Civilizations.Keys)
            .Concat(knowledge.GetGalacticCoreObservers())
            .Distinct()
            .OrderBy(id => id);

        return ids.Select(id => new CivilizationKnowledgeSaveDto
            {
                CivilizationId = id,
                GalacticCoreAccessUnlocked = knowledge.HasGalacticCoreAccess(id),
                GalacticCoreExplored = knowledge.IsGalacticCoreDiscovered(id),
                KnownSystemIds = snapshot.Systems.TryGetValue(id, out var systems)
                    ? systems.ToList()
                    : new List<int>(),
                KnownCivilizationIds = snapshot.Civilizations.TryGetValue(id, out var civilizations)
                    ? civilizations.ToList()
                    : new List<int>(),
                SystemSurveys = knowledge.GetSystemSurveyKnowledge(id)
                    .Select(survey => new SystemSurveySaveDto
                    {
                        SystemId = survey.SystemId,
                        Level = survey.Level,
                        Progress = survey.Progress,
                    })
                    .ToList(),
            })
            .ToList();
    }
}

public sealed class CampaignSaveEnvelope
{
    public int FormatVersion { get; set; }
    public string GameVersion { get; set; } = string.Empty;
    public DateTimeOffset SavedAtUtc { get; set; }
    public double SimulationDays { get; set; }
    public double SimulationSeconds { get; set; }
    public GalaxySaveDto Galaxy { get; set; } = new();
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public int GalaxyFormatVersion { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public DiplomacyStateSnapshot? Diplomacy { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public AdaptiveResearchCampaignSnapshot? AdaptiveResearch { get; set; }
}

public sealed class GalaxySaveDto
{
    public long Seed { get; set; }
    public GalaxyGenerationMetadata? GenerationMetadata { get; set; }
    public GalacticCoreMetadata? GalacticCore { get; set; }
    public List<StarSystemSaveDto> Systems { get; set; } = new();
    public List<PlanetaryBodySaveDto?>? PlanetaryBodies { get; set; }
    public List<CivilizationSaveDto> Civilizations { get; set; } = new();
    public List<FleetSaveDto> Fleets { get; set; } = new();
    public List<ColonySaveDto> Colonies { get; set; } = new();
    public List<EconomySaveDto> Economies { get; set; } = new();
    public List<TechnologySaveDto> Technologies { get; set; } = new();
    public List<ConstructionSaveDto> ConstructionStates { get; set; } = new();
    public List<ShipyardSaveDto> ShipyardStates { get; set; } = new();
    public int PlayerCivilizationId { get; set; }
    public List<CivilizationKnowledgeSaveDto> Knowledge { get; set; } = new();
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public CampaignMassiveEncounter? ActiveCombatEncounter { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public List<FleetPowerObservation>? CombatIntelligence { get; set; }
}

public sealed class PlanetaryBodySaveDto
{
    public required int Id { get; set; }
    public required int SystemId { get; set; }
    public required int? ParentBodyId { get; set; }
    public required int OrbitIndex { get; set; }
    public required string Name { get; set; }
    public required PlanetaryBodyKind Kind { get; set; }
    public required double RadiusEarth { get; set; }
    public required double MassEarth { get; set; }
    public required PlanetaryEnvironmentSaveDto? Environment { get; set; }
    public required bool LegacyColonizationCandidate { get; set; }
    public required bool HasRareResource { get; set; }
    public required bool HasAnomaly { get; set; }
    public required bool HasPreWarpCivilization { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public double OrbitalEccentricity { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public double OrbitalInclinationDegrees { get; set; }
}

public sealed class PlanetaryEnvironmentSaveDto
{
    public required double GravityG { get; set; }
    public required double TemperatureKelvin { get; set; }
    public required double PressureKPa { get; set; }
    public required PlanetaryAtmosphereRegime Atmosphere { get; set; }
    public required PlanetarySolventRegime AvailableSolvent { get; set; }
    public required double RadiationHazard { get; set; }
    public required bool IsImmersedEnvironment { get; set; }
    public required bool HasSolidSurface { get; set; }
}

public sealed class StarSystemSaveDto
{
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public float X { get; set; }
    public float Y { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public double? GalacticDepthLightYears { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public string? StellarCatalogId { get; set; }
    public StarArchetype Archetype { get; set; }
    public bool HasHabitableWorld { get; set; }
    public bool HasAnomaly { get; set; }
    public bool HasRareResource { get; set; }
    public bool HasPreWarpCivilization { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public string? CatalogPresetId { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public StellarPrimaryClass? StellarClass { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public StellarPrimaryClass? SecondaryStellarClass { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public StellarPrimaryClass? TertiaryStellarClass { get; set; }
}

public sealed class CivilizationSaveDto
{
    public Dictionary<string, CivilizationCharacter>? Leadership { get; set; }
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public int HomeSystemId { get; set; }
    public CivilizationArchetype Archetype { get; set; }
    public double Aggression { get; set; }
    public double Territoriality { get; set; }
    public double Greed { get; set; }
    public double ScientificCuriosity { get; set; }
    public double RiskTolerance { get; set; }
    public double SurvivalPriority { get; set; }
    public bool HonorBound { get; set; }
    public bool IsPlayer { get; set; }
    public CivilizationDevelopmentStage DevelopmentStage { get; set; }
    public bool IsSeededAncient { get; set; }
    public bool ExpansionAllowed { get; set; } = true;
    public bool NeutralUnlessProvoked { get; set; }
    public string SpeciesId { get; set; } = string.Empty;
}

public sealed class FleetSaveDto
{
    public int Id { get; set; }
    public int CivilizationId { get; set; }
    public string Name { get; set; } = string.Empty;
    public FleetRole Role { get; set; }
    public string? DesignId { get; set; }
    public float X { get; set; }
    public float Y { get; set; }
    public int? CurrentSystemId { get; set; }
    public int? DestinationSystemId { get; set; }
    public FleetTransitPhase TransitPhase { get; set; }
    public int? TransitOriginSystemId { get; set; }
    public int? TransitTargetSystemId { get; set; }
    public double TransitProgress { get; set; }
    public float LocalTransitStartX { get; set; }
    public float LocalTransitStartY { get; set; }
    public float LocalTransitPositionX { get; set; }
    public float LocalTransitPositionY { get; set; }
    public float LocalTransitTargetX { get; set; }
    public float LocalTransitTargetY { get; set; }
    public List<int>? PlannedRouteSystemIds { get; set; }
    public bool HoldRequested { get; set; }
    public bool ReturnToBaseRequested { get; set; }
    public string? ReturnToBaseFailureReason { get; set; }
    public int MissionOrderRevision { get; set; }
    public int? DestinationPlanetaryBodyId { get; set; }
    public bool PreventAutomaticSettlement { get; set; }
    public int? SettlementBodyId { get; set; }
    public double SettlementDaysCompleted { get; set; }
    public int? ReconnaissanceSystemId { get; set; }
    public double ReconnaissanceDaysCompleted { get; set; }
    public int? FreightTargetOutpostId { get; set; }
    public int? FreightHomeColonyId { get; set; }
    public double CargoMaterialCapacity { get; set; }
    public double CargoMaterials { get; set; }
    public double StrategicSpeed { get; set; }
    public double MaximumLegRangeLightYears { get; set; }
    public double FuelCapacityLightYears { get; set; }
    public double? FuelRemainingLightYears { get; set; }
    public float SensorRange { get; set; }
    public bool IsActive { get; set; } = true;
    public double? EmbarkedPopulationMillions { get; set; }
    public string? EmbarkedPopulationSpeciesId { get; set; }
    public FleetCombatSaveDto? Combat { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public MassiveCombatLoadout? TacticalLoadout { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public MassiveVesselState? TacticalVessel { get; set; }
}

public sealed class FleetCombatSaveDto
{
    public string ProfileId { get; set; } = string.Empty;
    public double Shields { get; set; }
    public double Armor { get; set; }
    public double Hull { get; set; }
    public double WeaponCooldownRemainingDays { get; set; }
    public MilitaryOrderType Order { get; set; }
    public int? TargetFleetId { get; set; }
    public int? DefendSystemId { get; set; }
    public double RetreatProgressDays { get; set; }
    public bool RetreatStarted { get; set; }
    public bool IsDisengaged { get; set; }
    public int? DisengagedSystemId { get; set; }
}

public sealed class ColonySaveDto
{
    public int Id { get; set; }
    public int CivilizationId { get; set; }
    public int SystemId { get; set; }
    public int? PlanetaryBodyId { get; set; }
    public string Name { get; set; } = string.Empty;
    public SettlementKind Kind { get; set; }
    public string? PopulationSpeciesId { get; set; }
    public double PopulationMillions { get; set; }
    public double Infrastructure { get; set; }
    public double Stability { get; set; }
    public double StoredFoodPopulationDaysMillions { get; set; }
    public double StoredWaterPopulationDaysMillions { get; set; }
    public double StoredExtractedMaterials { get; set; }
    public double? RemainingExtractableMaterials { get; set; }
    public int? SurfaceHubLevel { get; set; }
    public double SurfaceHubUpgradeDaysRemaining { get; set; }
    public List<SurfaceBuildingState>? SurfaceBuildings { get; set; }
}

public sealed class EconomySaveDto
{
    public int CivilizationId { get; set; }
    public double Credits { get; set; }
    public double Industry { get; set; }
    public double Science { get; set; }
    public double LastCreditsPerSecond { get; set; }
    public double LastIndustryPerSecond { get; set; }
    public double LastSciencePerSecond { get; set; }
    public double LastResearchSpendingPerDay { get; set; }
    public double LastResearchFundingFraction { get; set; } = 1.0;
    public double OperatingArrears { get; set; }
    public double LastBaseOperationsFundingFraction { get; set; } = 1.0;
    public IndustryPriority? IndustryPriority { get; set; }
}

public sealed class TechnologySaveDto
{
    public int CivilizationId { get; set; }
    public List<string> CompletedTechnologyIds { get; set; } = new();
    public string? ActiveResearchId { get; set; }
    public double ActiveResearchProgress { get; set; }
}

public sealed class ConstructionSaveDto
{
    public int CivilizationId { get; set; }
    public List<string> CompletedProjectIds { get; set; } = new();
    public string? ActiveProjectId { get; set; }
    public double ActiveProjectProgress { get; set; }
    public double ActiveProjectAuthorizationCredits { get; set; }
    public List<QueuedConstructionProjectSaveDto> QueuedProjects { get; set; } = new();
}

public sealed class QueuedConstructionProjectSaveDto
{
    public string ProjectId { get; set; } = string.Empty;
    public double AuthorizationCredits { get; set; }
}

public sealed class ShipyardSaveDto
{
    public int CivilizationId { get; set; }
    public long NextOrderSequence { get; set; } = 1;
    public string? ActiveDesignId { get; set; }
    public string? ActiveOrderId { get; set; }
    public double ActiveBuildProgress { get; set; }
    public double ActiveAuthorizationCredits { get; set; }
    public double ReservedPopulationMillions { get; set; }
    public string? ReservedPopulationSpeciesId { get; set; }
    public int? ReservedPopulationSourceColonyId { get; set; }
    public List<QueuedShipBuildSaveDto> QueuedBuilds { get; set; } = new();
}

public sealed class QueuedShipBuildSaveDto
{
    public string? OrderId { get; set; }
    public string DesignId { get; set; } = string.Empty;
    public double AuthorizationCredits { get; set; }
    public double ReservedPopulationMillions { get; set; }
    public string? ReservedPopulationSpeciesId { get; set; }
    public int? ReservedPopulationSourceColonyId { get; set; }
}

public sealed class CivilizationKnowledgeSaveDto
{
    public int CivilizationId { get; set; }
    public bool GalacticCoreAccessUnlocked { get; set; }
    public bool GalacticCoreExplored { get; set; }
    public List<int> KnownSystemIds { get; set; } = new();
    public List<int> KnownCivilizationIds { get; set; } = new();
    public List<SystemSurveySaveDto> SystemSurveys { get; set; } = new();
}

public sealed class SystemSurveySaveDto
{
    public int SystemId { get; set; }
    public SystemSurveyLevel Level { get; set; }
    public double Progress { get; set; }
}

public sealed record LoadedCampaign(
    GalaxyState Galaxy,
    double SimulationDays,
    string GameVersion,
    DateTimeOffset SavedAtUtc);
