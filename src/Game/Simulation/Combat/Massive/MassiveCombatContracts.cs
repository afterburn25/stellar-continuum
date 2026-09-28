using System;
using System.Collections.Generic;
using System.Numerics;

namespace Game.Simulation.Combat.Massive;

public enum MassiveCombatOrderType
{
    Engage, Hold, Defend, Advance, AdvanceCautiously, StandoffAttack, Screen,
    ProtectCriticalAsset, FocusFire, FlankLeft, FlankRight, Intercept, Pursue,
    BreakContact, Disengage, Retreat, EmergencyRetreat, Breakout, Surrender,
}

public enum MassiveFormationShape { Screen, Line, Wedge, Standoff, Dispersed, Escort, RetreatColumn, Breakout }
public enum MassiveWeaponKind { Beam, Kinetic, Missile, PointDefense, ElectronicWarfare }
public enum MassiveModuleKind { Reactor, Engine, Sensor, WarpDrive, WeaponControl, PointDefense, ElectronicWarfare, WarpInterdictor }
public enum InterdictorProtectionPolicy { Low, Standard, High, Absolute }
public enum MassiveCombatEventType { Engagement, BeamVolley, KineticVolley, MissileSalvo, MissileIntercepted, Damage, FormationDestroyed, WarpSpooling, WarpBlocked, Escaped, Surrendered, OrderChanged }

public readonly record struct MassivePoint(float X, float Y)
{
    public Vector2 Vector => new(X, Y);
    public bool IsFinite => float.IsFinite(X) && float.IsFinite(Y);
    public static implicit operator Vector2(MassivePoint value) => value.Vector;
    public static implicit operator MassivePoint(Vector2 value) => new(value.X, value.Y);
}

public sealed record MassiveCombatOrder(
    long FormationId,
    MassiveCombatOrderType Type,
    long? TargetFormationId = null,
    Vector2? Objective = null,
    MassiveFormationShape? Shape = null);

public sealed record MassiveCombatOrderResult(bool Accepted, string Message);

public sealed record MassiveCombatEvent(
    long Sequence,
    long Tick,
    MassiveCombatEventType Type,
    int ActorCivilizationId,
    long ActorFormationId,
    int? TargetCivilizationId,
    long? TargetFormationId,
    int Magnitude,
    MassivePoint Position,
    string Message);

/// <summary>An observer-filtered event. Nullable fields were not established by that observer's sensors.</summary>
public sealed record MassiveObservedCombatEvent(
    long Sequence,
    long Tick,
    MassiveCombatEventType Type,
    int? ActorCivilizationId,
    long? ActorFormationId,
    int? TargetCivilizationId,
    long? TargetFormationId,
    int? Magnitude,
    MassivePoint? Position,
    string Message,
    bool DetailsKnown,
    MassivePoint? ImpactPosition = null);

public interface IMassiveCombatHostilityView
{
    bool AreHostile(int firstCivilizationId, int secondCivilizationId);
}

public sealed class DistinctCivilizationsHostilityView : IMassiveCombatHostilityView
{
    public static DistinctCivilizationsHostilityView Instance { get; } = new();
    private DistinctCivilizationsHostilityView() { }
    public bool AreHostile(int firstCivilizationId, int secondCivilizationId) => firstCivilizationId != secondCivilizationId;
}

public interface IMassiveCombatSensorView
{
    float Confidence(int observerCivilizationId, long formationId);
    bool IdentifiesCohorts(int observerCivilizationId, long formationId);
    bool IdentifiesImportantVessels(int observerCivilizationId, long formationId);
    bool CanEstimateCombatPower(int observerCivilizationId, long formationId);
}

/// <summary>A bounded observer-safe grouping. Unidentified contacts collapse into one opaque group.</summary>
public sealed record MassiveObservedCohort(
    long CohortId,
    string DisplayClass,
    int CountLow,
    int CountHigh,
    bool Identified);

public sealed record MassiveObservedFormation(
    long FormationId,
    int CivilizationId,
    string DisplayName,
    Vector2 Position,
    Vector2 Velocity,
    MassiveFormationShape Shape,
    int ShipCountLow,
    int ShipCountHigh,
    float? StrengthLow,
    float? StrengthHigh,
    bool IsExact,
    bool IsInterdicting,
    bool IsWarpBlocked,
    float WarpSpoolProgress,
    float? PerShipCombatPower,
    IReadOnlyList<MassiveObservedCohort> Cohorts,
    IReadOnlyList<MassiveObservedVessel> ImportantVessels,
    float HeadingRadians);

public sealed record MassiveObservedVessel(long VesselId, string DisplayName, string DesignId, float? CombatPower,
    bool IsFlagship, bool IsCarrier, bool IsInterdictor, bool IsCriticallyDamaged);

/// <summary>
/// Observer-filtered in-flight missile state. Null source/current fields mean the observer
/// knows an impact is approaching an owned target but has not identified its origin or path.
/// </summary>
public sealed record MassiveObservedMissileSalvo(
    long SalvoId,
    long? SourceFormationId,
    MassivePoint? SourcePosition,
    long? TargetFormationId,
    MassivePoint? TargetPosition,
    MassivePoint? CurrentPosition,
    float RemainingSeconds,
    float? Progress01,
    int? CountLow,
    int? CountHigh,
    bool IncomingToOwn);

public sealed record MassiveCombatSnapshot(
    Guid BattleId,
    long Tick,
    double SimulatedSeconds,
    int ExactOwnShips,
    IReadOnlyList<MassiveObservedFormation> Formations,
    IReadOnlyList<MassiveObservedCombatEvent> Events,
    IReadOnlyList<MassiveObservedMissileSalvo> ActiveMissileSalvos);

public sealed record MassiveFleetCombatOutcome(int FleetId, int CivilizationId, int SurvivingShips,
    int DestroyedShips, bool Escaped, bool Surrendered, float ShieldFraction, float ArmorFraction, float HullFraction,
    IReadOnlyList<MassiveVesselState> ImportantVessels);

public sealed record MassiveCombatAftermath(Guid BattleId, long FinalTick, bool IsComplete,
    IReadOnlyList<MassiveFleetCombatOutcome> Fleets);

public sealed record MassiveCombatMetrics(
    long Tick,
    int ActiveFormations,
    int ActiveShips,
    int SpatialCells,
    int TargetCandidatesExamined,
    int WeaponGroupsResolved,
    int EventsRetained);
