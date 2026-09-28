using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Knowledge;

/// <summary>
/// Stores what each civilization has legitimately discovered. Presentation and AI
/// must query this state rather than treating GalaxyState as automatically known.
/// Star coordinates are part of the common astronomical catalog; system details,
/// fleets and civilizations remain hidden until legitimately observed.
/// </summary>
public sealed class CivilizationKnowledgeState
{
    private readonly Dictionary<int, HashSet<int>> _knownSystems = new();
    private readonly HashSet<int> _coreAccessUnlocked = new();
    private readonly HashSet<int> _coreExplored = new();

    public bool HasGalacticCoreAccess(int civilizationId) => _coreAccessUnlocked.Contains(civilizationId);
    public bool IsGalacticCoreDiscovered(int civilizationId) =>
        HasGalacticCoreAccess(civilizationId) && _coreExplored.Contains(civilizationId);
    public IReadOnlyCollection<int> GetGalacticCoreObservers() => _coreAccessUnlocked.OrderBy(id => id).ToArray();

    // Called by the eventual authoritative unlock and exploration systems, never by map clicks.
    public void UnlockGalacticCoreAccess(int civilizationId)
    {
        if (civilizationId < 0) throw new ArgumentOutOfRangeException(nameof(civilizationId));
        _coreAccessUnlocked.Add(civilizationId);
    }

    public bool RecordGalacticCoreExploration(int civilizationId) =>
        HasGalacticCoreAccess(civilizationId) && _coreExplored.Add(civilizationId);
    private readonly Dictionary<int, HashSet<int>> _knownCivilizations = new();
    private readonly Dictionary<int, Dictionary<int, MutableSystemSurveyKnowledge>> _systemSurveyKnowledge = new();

    public bool IsSystemKnown(int civilizationId, int systemId) =>
        _knownSystems.TryGetValue(civilizationId, out var systems) && systems.Contains(systemId);

    public bool IsSystemFullySurveyed(int civilizationId, int systemId) =>
        GetSystemSurveyLevel(civilizationId, systemId) == SystemSurveyLevel.FullySurveyed;

    public SystemSurveyLevel GetSystemSurveyLevel(int civilizationId, int systemId)
    {
        if (!_systemSurveyKnowledge.TryGetValue(civilizationId, out var systems) ||
            !systems.TryGetValue(systemId, out var knowledge))
        {
            return SystemSurveyLevel.Unknown;
        }

        return knowledge.Level;
    }

    public double GetSystemSurveyProgress(int civilizationId, int systemId)
    {
        if (!_systemSurveyKnowledge.TryGetValue(civilizationId, out var systems) ||
            !systems.TryGetValue(systemId, out var knowledge))
        {
            return 0.0;
        }

        return knowledge.Progress;
    }

    public IReadOnlyList<SystemSurveyKnowledgeView> GetSystemSurveyKnowledge(int civilizationId)
    {
        if (!_systemSurveyKnowledge.TryGetValue(civilizationId, out var systems))
            return Array.Empty<SystemSurveyKnowledgeView>();

        return systems
            .OrderBy(pair => pair.Key)
            .Select(pair => new SystemSurveyKnowledgeView(pair.Key, pair.Value.Level, pair.Value.Progress))
            .ToArray();
    }

    public bool IsCivilizationKnown(int observerCivilizationId, int targetCivilizationId) =>
        observerCivilizationId == targetCivilizationId ||
        (_knownCivilizations.TryGetValue(observerCivilizationId, out var civilizations) &&
         civilizations.Contains(targetCivilizationId));

    public IReadOnlyCollection<int> GetKnownSystems(int civilizationId)
    {
        if (!_knownSystems.TryGetValue(civilizationId, out var systems))
            return Array.Empty<int>();
        return systems.OrderBy(id => id).ToArray();
    }

    public IReadOnlyCollection<int> GetKnownCivilizations(int civilizationId)
    {
        if (!_knownCivilizations.TryGetValue(civilizationId, out var civilizations))
            return Array.Empty<int>();
        return civilizations.OrderBy(id => id).ToArray();
    }

    public bool RevealSystem(int civilizationId, int systemId)
    {
        if (!_knownSystems.TryGetValue(civilizationId, out var systems))
        {
            systems = new HashSet<int>();
            _knownSystems[civilizationId] = systems;
        }

        EnsureSurveyKnowledge(civilizationId, systemId);
        return systems.Add(systemId);
    }

    /// <summary>
    /// Records a fast scout pass. Reconnaissance is useful knowledge, but it is
    /// deliberately insufficient for decisions that require a completed science survey.
    /// </summary>
    public bool RecordReconnaissance(int civilizationId, int systemId, double progressFloor = 0.35)
    {
        RevealSystem(civilizationId, systemId);
        var knowledge = EnsureSurveyKnowledge(civilizationId, systemId);
        if (knowledge.Level == SystemSurveyLevel.FullySurveyed)
            return false;

        var oldLevel = knowledge.Level;
        var oldProgress = knowledge.Progress;
        knowledge.Progress = Math.Clamp(Math.Max(knowledge.Progress, progressFloor), 0.0, 0.999999);
        knowledge.Level = SystemSurveyLevel.PartiallySurveyed;
        return knowledge.Level != oldLevel || Math.Abs(knowledge.Progress - oldProgress) > 0.0000001;
    }

    /// <summary>
    /// Advances a legitimate detailed survey. Returns true only on the transition to
    /// a completed survey so callers can emit a single completion event.
    /// </summary>
    public bool AdvanceSystemSurvey(int civilizationId, int systemId, double progressDelta)
    {
        if (progressDelta <= 0.0)
            return false;

        RevealSystem(civilizationId, systemId);
        var knowledge = EnsureSurveyKnowledge(civilizationId, systemId);
        if (knowledge.Level == SystemSurveyLevel.FullySurveyed)
            return false;

        var wasFullySurveyed = knowledge.Level == SystemSurveyLevel.FullySurveyed;
        knowledge.Progress = Math.Clamp(knowledge.Progress + progressDelta, 0.0, 1.0);
        knowledge.Level = knowledge.Progress >= 1.0
            ? SystemSurveyLevel.FullySurveyed
            : SystemSurveyLevel.PartiallySurveyed;

        return !wasFullySurveyed && knowledge.Level == SystemSurveyLevel.FullySurveyed;
    }

    public bool MarkSystemFullySurveyed(int civilizationId, int systemId)
    {
        RevealSystem(civilizationId, systemId);
        var knowledge = EnsureSurveyKnowledge(civilizationId, systemId);
        var changed = knowledge.Level != SystemSurveyLevel.FullySurveyed || knowledge.Progress < 1.0;
        knowledge.Progress = 1.0;
        knowledge.Level = SystemSurveyLevel.FullySurveyed;
        return changed;
    }

    public bool RevealCivilization(int observerCivilizationId, int targetCivilizationId)
    {
        if (observerCivilizationId == targetCivilizationId)
            return false;

        if (!_knownCivilizations.TryGetValue(observerCivilizationId, out var civilizations))
        {
            civilizations = new HashSet<int>();
            _knownCivilizations[observerCivilizationId] = civilizations;
        }

        return civilizations.Add(targetCivilizationId);
    }

    public int RevealWithinSensorRange(
        int civilizationId,
        int originSystemId,
        IReadOnlyList<StarSystemState> systems,
        float sensorRange)
    {
        var origin = systems.FirstOrDefault(s => s.Id == originSystemId)
            ?? throw new InvalidOperationException($"Unknown sensor origin system {originSystemId}.");

        var revealed = 0;
        var rangeSquared = (double)sensorRange * sensorRange;
        foreach (var system in systems)
        {
            if (InterstellarDistance.SquaredBetween(origin, system) <= rangeSquared &&
                RevealSystem(civilizationId, system.Id))
            {
                revealed++;
            }
        }

        return revealed;
    }

    public KnowledgeSnapshotData Snapshot() => new(
        _knownSystems.ToDictionary(
            pair => pair.Key,
            pair => pair.Value.OrderBy(id => id).ToArray()),
        _knownCivilizations.ToDictionary(
            pair => pair.Key,
            pair => pair.Value.OrderBy(id => id).ToArray()));

    public static CivilizationKnowledgeState CreateInitial(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<CivilizationState> civilizations,
        float sensorRange)
    {
        var knowledge = new CivilizationKnowledgeState();
        foreach (var civilization in civilizations)
        {
            // A civilization begins with detailed knowledge of its own home system.
            knowledge.MarkSystemFullySurveyed(civilization.Id, civilization.HomeSystemId);
            knowledge.RevealWithinSensorRange(civilization.Id, civilization.HomeSystemId, systems, sensorRange);
        }

        return knowledge;
    }

    private MutableSystemSurveyKnowledge EnsureSurveyKnowledge(int civilizationId, int systemId)
    {
        if (!_systemSurveyKnowledge.TryGetValue(civilizationId, out var systems))
        {
            systems = new Dictionary<int, MutableSystemSurveyKnowledge>();
            _systemSurveyKnowledge[civilizationId] = systems;
        }

        if (!systems.TryGetValue(systemId, out var knowledge))
        {
            knowledge = new MutableSystemSurveyKnowledge
            {
                Level = SystemSurveyLevel.Detected,
                Progress = 0.0,
            };
            systems[systemId] = knowledge;
        }

        return knowledge;
    }

    private sealed class MutableSystemSurveyKnowledge
    {
        public SystemSurveyLevel Level { get; set; }
        public double Progress { get; set; }
    }
}

public enum SystemSurveyLevel
{
    Unknown = 0,
    Detected = 1,
    PartiallySurveyed = 2,
    FullySurveyed = 3,
}

public sealed record SystemSurveyKnowledgeView(
    int SystemId,
    SystemSurveyLevel Level,
    double Progress);

public sealed record KnowledgeSnapshotData(
    IReadOnlyDictionary<int, int[]> Systems,
    IReadOnlyDictionary<int, int[]> Civilizations
);
