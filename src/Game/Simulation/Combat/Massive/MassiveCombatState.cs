using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text.Json.Serialization;

namespace Game.Simulation.Combat.Massive;

public sealed class MassiveCombatBattleState
{
    public Guid BattleId { get; set; } = Guid.NewGuid();
    public ulong Seed { get; set; } = 1;
    public long Tick { get; set; }
    public double SimulatedSeconds { get; set; }
    public double PendingSeconds { get; set; }
    public long NextEventSequence { get; set; } = 1;
    public long NextSalvoId { get; set; } = 1;
    public List<MassiveFormationState> Formations { get; set; } = new();
    public List<MassiveCombatEvent> Events { get; set; } = new();
    public List<MassiveMissileSalvoState> ActiveSalvos { get; set; } = new();
    [JsonIgnore] public MassiveCombatMetrics LastMetrics { get; internal set; } = new(0, 0, 0, 0, 0, 0, 0);
    [JsonIgnore] public bool IsComplete => Formations.Where(x => x.Active).Select(x => x.CivilizationId).Distinct().Take(2).Count() < 2;

    public static MassiveCombatBattleState Create(ulong seed, IEnumerable<MassiveFormationState> formations)
    {
        var state = new MassiveCombatBattleState
        {
            BattleId = DeterministicBattleId(seed),
            Seed = seed == 0 ? 1UL : seed,
            Formations = formations.OrderBy(x => x.Id).ToList(),
        };
        foreach (var formation in state.Formations)
        {
            formation.InitialShipCount = formation.ActiveShipCount;
            formation.ShieldPool = formation.Loadout.ShieldPerShip * formation.ActiveShipCount;
            formation.ArmorPool = formation.Loadout.ArmorPerShip * formation.ActiveShipCount;
            formation.HullPool = formation.Loadout.HullPerShip * formation.ActiveShipCount;
        }
        state.Validate();
        return state;
    }

    private static Guid DeterministicBattleId(ulong seed)
    {
        Span<byte> bytes = stackalloc byte[16];
        BitConverter.TryWriteBytes(bytes, seed);
        BitConverter.TryWriteBytes(bytes[8..], seed ^ 0x9E3779B97F4A7C15UL);
        return new Guid(bytes);
    }

    public void Validate()
    {
        if (BattleId == Guid.Empty) throw new InvalidOperationException("A massive battle requires a stable identity.");
        if (!double.IsFinite(PendingSeconds) || PendingSeconds < 0 || !double.IsFinite(SimulatedSeconds) || SimulatedSeconds < 0)
            throw new InvalidOperationException("Battle time must be finite and nonnegative.");
        if (Formations.Count > MassiveCombatLimits.MaxFormations) throw new InvalidOperationException("Battle formation limit exceeded.");
        if (Formations.Select(x => x.Id).Distinct().Count() != Formations.Count) throw new InvalidOperationException("Formation identities must be unique.");
        foreach (var formation in Formations) formation.Validate();
        var importantIds = Formations.SelectMany(x => x.ImportantVessels).Select(x => x.Id).ToArray();
        if (importantIds.Distinct().Count() != importantIds.Length) throw new InvalidOperationException("Important-vessel identities must be globally unique within a battle.");
        if (Formations.Sum(x => (long)x.InitialShipCount) > MassiveCombatLimits.MaxShips)
            throw new InvalidOperationException("Battle ship limit exceeded.");
        if (Events.Count > MassiveCombatLimits.MaxRetainedEvents || Events.Any(x => !Enum.IsDefined(x.Type) || x.Sequence <= 0 || x.Tick < 0 || !x.Position.IsFinite))
            throw new InvalidOperationException("Battle event stream is invalid or unbounded.");
        var formationIds = Formations.Select(x => x.Id).ToHashSet();
        if (ActiveSalvos.Count > MassiveCombatLimits.MaxActiveSalvos || ActiveSalvos.Any(x => !x.IsValid ||
                !formationIds.Contains(x.SourceFormationId) || !formationIds.Contains(x.TargetFormationId)) ||
            ActiveSalvos.Select(x => x.Id).Distinct().Count() != ActiveSalvos.Count)
            throw new InvalidOperationException("Active missile salvo state is invalid or unbounded.");
        if (NextEventSequence <= Events.Select(x => x.Sequence).DefaultIfEmpty(0).Max() ||
            NextSalvoId <= ActiveSalvos.Select(x => x.Id).DefaultIfEmpty(0).Max())
            throw new InvalidOperationException("Battle sequence counters do not follow persisted state.");
    }
}

public sealed class MassiveMissileSalvoState
{
    public long Id { get; set; }
    public long SourceFormationId { get; set; }
    public long TargetFormationId { get; set; }
    public int MissileCount { get; set; }
    public float Damage { get; set; }
    public float RemainingSeconds { get; set; }
    /// <summary>Launch point in battle-space kilometres. Null marks a legacy salvo without presentation history.</summary>
    public MassivePoint? LaunchPosition { get; set; }
    /// <summary>Authoritative flight duration assigned at launch. Zero marks legacy state without presentation history.</summary>
    public float InitialFlightSeconds { get; set; }
    [JsonIgnore] public bool IsValid => Id > 0 && SourceFormationId > 0 && TargetFormationId > 0 && MissileCount >= 0 &&
        float.IsFinite(Damage) && Damage >= 0 && float.IsFinite(RemainingSeconds) && RemainingSeconds >= 0 &&
        float.IsFinite(InitialFlightSeconds) && InitialFlightSeconds >= 0 &&
        (LaunchPosition is null
            ? InitialFlightSeconds == 0
            : LaunchPosition.Value.IsFinite && InitialFlightSeconds > 0 && RemainingSeconds <= InitialFlightSeconds + .0001f);
}

public sealed class MassiveFormationState
{
    public long Id { get; set; }
    public int CivilizationId { get; set; }
    public int FleetId { get; set; }
    public int TaskForceId { get; set; }
    public string Name { get; set; } = string.Empty;
    /// <summary>Battle-space position in kilometres.</summary>
    public MassivePoint Position { get; set; }
    /// <summary>Battle-space velocity in kilometres per second.</summary>
    public MassivePoint Velocity { get; set; }
    public MassivePoint Heading { get; set; } = new(1, 0);
    public MassivePoint Objective { get; set; }
    public MassiveFormationShape Shape { get; set; } = MassiveFormationShape.Line;
    public MassiveCombatOrderType Order { get; set; } = MassiveCombatOrderType.Hold;
    public long? TargetFormationId { get; set; }
    public long? ProtectedFormationId { get; set; }
    public InterdictorProtectionPolicy InterdictorProtection { get; set; } = InterdictorProtectionPolicy.Standard;
    public float Cohesion { get; set; } = 1f;
    public float Morale { get; set; } = 1f;
    public float ShieldPool { get; set; }
    public float ArmorPool { get; set; }
    public float HullPool { get; set; }
    /// <summary>Battle-entry hull carried by each damage-compatible cohort member. Zero uses the design hull for legacy saves.</summary>
    public float HullLossThresholdPerShip { get; set; }
    public float Heat { get; set; }
    public float PowerReserve { get; set; } = 1f;
    public float WarpSpoolProgress { get; set; }
    public bool WarpBlocked { get; set; }
    public bool Escaped { get; set; }
    public bool Surrendered { get; set; }
    public int InitialShipCount { get; set; }
    public int DestroyedShips { get; set; }
    public float HullDamageRemainder { get; set; }
    public MassiveCombatLoadout Loadout { get; set; } = new();
    public List<MassiveCohortState> Cohorts { get; set; } = new();
    public List<MassiveVesselState> ImportantVessels { get; set; } = new();

    [JsonIgnore] public int SurvivingShipCount => Cohorts.Sum(x => Math.Max(0, x.ActiveCount)) + ImportantVessels.Count(x => !x.Destroyed);
    [JsonIgnore] public int ActiveShipCount => Escaped || Surrendered ? 0 : SurvivingShipCount;
    [JsonIgnore] public bool Active => !Escaped && !Surrendered && ActiveShipCount > 0 && HullPool > 0f;

    public void Validate()
    {
        if (Id <= 0 || CivilizationId < 0 || FleetId < 0 || TaskForceId < 0 || !Enum.IsDefined(Shape) || !Enum.IsDefined(Order) || !Enum.IsDefined(InterdictorProtection)) throw new InvalidOperationException("Formation hierarchy or tactical enums are invalid.");
        if (string.IsNullOrWhiteSpace(Name) || !Finite(Position) || !Finite(Velocity) || !Finite(Heading) || !Finite(Objective)) throw new InvalidOperationException("Formation presentation-independent geometry is invalid.");
        if (Cohorts.Count > MassiveCombatLimits.MaxCohortsPerFormation || ImportantVessels.Count > MassiveCombatLimits.MaxImportantVesselsPerFormation) throw new InvalidOperationException("Formation detail limit exceeded.");
        if (Cohorts.Select(x => x.Id).Distinct().Count() != Cohorts.Count || ImportantVessels.Select(x => x.Id).Distinct().Count() != ImportantVessels.Count) throw new InvalidOperationException("Cohort and important-vessel identities must be unique.");
        foreach (var cohort in Cohorts) cohort.Validate();
        foreach (var vessel in ImportantVessels) vessel.Validate();
        Loadout.Validate();
        if (InitialShipCount <= 0) InitialShipCount = ActiveShipCount + Math.Max(0, DestroyedShips);
        if (DestroyedShips < 0 || InitialShipCount != SurvivingShipCount + DestroyedShips) throw new InvalidOperationException("Formation ship accounting is inconsistent.");
        if (!float.IsFinite(ShieldPool) || !float.IsFinite(ArmorPool) || !float.IsFinite(HullPool) ||
            !float.IsFinite(HullLossThresholdPerShip) || ShieldPool < 0 || ArmorPool < 0 || HullPool < 0 || HullLossThresholdPerShip < 0)
            throw new InvalidOperationException("Formation durability is invalid.");
        foreach (var value in new[] { Cohesion, Morale, Heat, PowerReserve, WarpSpoolProgress, HullDamageRemainder })
            if (!float.IsFinite(value) || value < 0) throw new InvalidOperationException("Formation tactical state is invalid.");
        if (Cohesion > 1 || Morale > 1 || PowerReserve > 1 || WarpSpoolProgress > 1)
            throw new InvalidOperationException("Formation normalized tactical state is out of range.");
    }

    private static bool Finite(MassivePoint value) => value.IsFinite;
}

public sealed class MassiveCohortState
{
    public long Id { get; set; }
    public string DesignId { get; set; } = string.Empty;
    public int InitialCount { get; set; }
    public int ActiveCount { get; set; }
    public float Experience { get; set; } = .5f;
    public void Validate()
    {
        if (Id <= 0 || string.IsNullOrWhiteSpace(DesignId) || InitialCount < 0 || ActiveCount < 0 || ActiveCount > InitialCount || !float.IsFinite(Experience)) throw new InvalidOperationException("Cohort state is invalid.");
    }
}

public sealed class MassiveVesselState
{
    public long Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public string DesignId { get; set; } = string.Empty;
    public bool IsFlagship { get; set; }
    public bool IsCarrier { get; set; }
    public bool IsInterdictor { get; set; }
    public bool IsStoryShip { get; set; }
    public float HullFraction { get; set; } = 1f;
    public float EngineFraction { get; set; } = 1f;
    public float SensorFraction { get; set; } = 1f;
    public float WarpDriveFraction { get; set; } = 1f;
    public float ReactorFraction { get; set; } = 1f;
    public float InterdictorFraction { get; set; } = 1f;
    public int BattlesFought { get; set; }
    public int ConfirmedKills { get; set; }
    public bool Destroyed { get; set; }
    public bool Escaped { get; set; }
    public void Validate()
    {
        if (Id <= 0 || string.IsNullOrWhiteSpace(Name) || string.IsNullOrWhiteSpace(DesignId)) throw new InvalidOperationException("Important vessel identity is invalid.");
        foreach (var value in new[] { HullFraction, EngineFraction, SensorFraction, WarpDriveFraction, ReactorFraction, InterdictorFraction })
            if (!float.IsFinite(value) || value < 0 || value > 1) throw new InvalidOperationException("Important vessel condition is invalid.");
    }
}

public static class MassiveCombatLimits
{
    public const int MaxShips = 200_000;
    public const int MaxFormations = 4_096;
    public const int MaxCohortsPerFormation = 128;
    public const int MaxImportantVesselsPerFormation = 256;
    public const int MaxRetainedEvents = 256;
    public const int MaxEventsPerTick = 32;
    public const int MaxActiveSalvos = 256;
    public const int MaxCatchUpTicks = 600;
}
