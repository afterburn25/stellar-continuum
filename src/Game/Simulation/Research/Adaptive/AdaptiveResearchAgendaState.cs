using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchAgendaOrientationState(
    double BasicVsAppliedOrientation,
    double CompetencePreservationPolicy,
    double PortfolioDiversityPolicy,
    double ForeignScienceEngagement);

/// <summary>
/// Sparse high-level research agenda plus the small fixed scientific-culture vector.
/// Default Routine priorities are omitted from sparse maps.
/// </summary>
public sealed class AdaptiveResearchAgendaState
{
    private readonly Dictionary<string, string> _domainPriorities = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _fieldPriorities = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _problemPriorities = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _capabilityPriorities = new(StringComparer.Ordinal);
    private readonly Dictionary<string, double> _cultureAxes = new(StringComparer.Ordinal);

    internal AdaptiveResearchAgendaState(AdaptiveResearchAgendaCatalog catalog)
    {
        var defaults = catalog.RuntimePolicy;
        Orientations = new ResearchAgendaOrientationState(
            defaults.DefaultBasicVsAppliedOrientation,
            defaults.DefaultCompetencePreservation,
            defaults.DefaultPortfolioDiversity,
            defaults.DefaultForeignScienceEngagement);
        foreach (var axisId in catalog.CultureAxes.Keys)
            _cultureAxes.Add(axisId, defaults.DefaultCultureAxis);
    }

    public long Revision { get; private set; }
    public ResearchAgendaOrientationState Orientations { get; private set; }
    public double LastMajorReviewYear { get; private set; } = double.NegativeInfinity;
    public string PolicyProvenance { get; private set; } = "default";

    public IReadOnlyDictionary<string, string> DomainPriorities => new ReadOnlyDictionary<string, string>(_domainPriorities);
    public IReadOnlyDictionary<string, string> FieldPriorities => new ReadOnlyDictionary<string, string>(_fieldPriorities);
    public IReadOnlyDictionary<string, string> ProblemPriorities => new ReadOnlyDictionary<string, string>(_problemPriorities);
    public IReadOnlyDictionary<string, string> CapabilityPriorities => new ReadOnlyDictionary<string, string>(_capabilityPriorities);
    public IReadOnlyDictionary<string, double> CultureAxes => new ReadOnlyDictionary<string, double>(_cultureAxes);

    public string GetDomainPriority(string domainId, string defaultPriorityId) => GetPriority(_domainPriorities, domainId, defaultPriorityId);
    public string GetFieldPriority(string fieldId, string defaultPriorityId) => GetPriority(_fieldPriorities, fieldId, defaultPriorityId);
    public string GetProblemPriority(string pressureId, string defaultPriorityId) => GetPriority(_problemPriorities, pressureId, defaultPriorityId);
    public string GetCapabilityPriority(string capabilityId, string defaultPriorityId) => GetPriority(_capabilityPriorities, capabilityId, defaultPriorityId);
    public double GetCultureAxis(string axisId) => _cultureAxes.TryGetValue(axisId, out var value) ? value : 50.0;

    internal bool SetDomainPriority(string key, string priorityId, string defaultPriorityId) =>
        SetPriority(_domainPriorities, key, priorityId, defaultPriorityId);

    internal bool SetFieldPriority(string key, string priorityId, string defaultPriorityId) =>
        SetPriority(_fieldPriorities, key, priorityId, defaultPriorityId);

    internal bool SetProblemPriority(string key, string priorityId, string defaultPriorityId) =>
        SetPriority(_problemPriorities, key, priorityId, defaultPriorityId);

    internal bool SetCapabilityPriority(string key, string priorityId, string defaultPriorityId) =>
        SetPriority(_capabilityPriorities, key, priorityId, defaultPriorityId);

    internal bool SetCultureAxis(string axisId, double value)
    {
        value = Validate100(value, nameof(value));
        if (_cultureAxes.TryGetValue(axisId, out var existing) && Math.Abs(existing - value) < 0.000001)
            return false;
        _cultureAxes[axisId] = value;
        Revision++;
        return true;
    }

    internal void SetOrientations(ResearchAgendaOrientationState value)
    {
        value = new ResearchAgendaOrientationState(
            Validate100(value.BasicVsAppliedOrientation, nameof(value.BasicVsAppliedOrientation)),
            Validate100(value.CompetencePreservationPolicy, nameof(value.CompetencePreservationPolicy)),
            Validate100(value.PortfolioDiversityPolicy, nameof(value.PortfolioDiversityPolicy)),
            Validate100(value.ForeignScienceEngagement, nameof(value.ForeignScienceEngagement)));
        if (Orientations == value)
            return;
        Orientations = value;
        Revision++;
    }

    internal void MarkReviewed(double currentYear, string provenance)
    {
        if (double.IsNaN(currentYear) || double.IsInfinity(currentYear))
            throw new ArgumentOutOfRangeException(nameof(currentYear));
        LastMajorReviewYear = currentYear;
        PolicyProvenance = string.IsNullOrWhiteSpace(provenance) ? "unspecified" : provenance;
        Revision++;
    }

    internal bool SetPriority(
        Dictionary<string, string> map,
        string key,
        string priorityId,
        string defaultPriorityId)
    {
        if (string.Equals(priorityId, defaultPriorityId, StringComparison.Ordinal))
        {
            if (!map.Remove(key))
                return false;
            Revision++;
            return true;
        }
        if (map.TryGetValue(key, out var existing) && string.Equals(existing, priorityId, StringComparison.Ordinal))
            return false;
        map[key] = priorityId;
        Revision++;
        return true;
    }

    private static string GetPriority(Dictionary<string, string> map, string key, string defaultPriorityId) =>
        map.TryGetValue(key, out var priority) ? priority : defaultPriorityId;

    private static double Validate100(double value, string name)
    {
        if (value < 0 || value > 100 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(name, value, "Value must be in 0..100.");
        return value;
    }
}
