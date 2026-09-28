using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Sparse causal support state for Research Pressure. Zero metric signals are absent and a pressure
/// only enters ActivePressureIds after real metric/event support has activated it.
/// </summary>
public sealed class AdaptiveResearchPressureState
{
    private readonly Dictionary<string, double> _metricSignals = new(StringComparer.Ordinal);
    private readonly HashSet<string> _activePressureIds = new(StringComparer.Ordinal);

    public long Revision { get; private set; }

    public IReadOnlyDictionary<string, double> MetricSignals =>
        new ReadOnlyDictionary<string, double>(_metricSignals);

    public IReadOnlyCollection<string> ActivePressureIds => _activePressureIds;

    public double GetMetricSignal(string signalId) =>
        _metricSignals.TryGetValue(signalId, out var value) ? value : 0.0;

    internal bool SetMetricSignal(string signalId, double normalizedValue)
    {
        if (normalizedValue < 0 || normalizedValue > 1 || double.IsNaN(normalizedValue) || double.IsInfinity(normalizedValue))
            throw new ArgumentOutOfRangeException(nameof(normalizedValue), normalizedValue, "Pressure metric signal must be in 0..1.");

        if (normalizedValue <= 0.0)
        {
            if (!_metricSignals.Remove(signalId))
                return false;
            Revision++;
            return true;
        }

        if (_metricSignals.TryGetValue(signalId, out var existing) && Math.Abs(existing - normalizedValue) < 0.0000001)
            return false;
        _metricSignals[signalId] = normalizedValue;
        Revision++;
        return true;
    }

    internal bool ActivatePressure(string pressureId)
    {
        if (!_activePressureIds.Add(pressureId))
            return false;
        Revision++;
        return true;
    }

    internal bool DeactivatePressure(string pressureId)
    {
        if (!_activePressureIds.Remove(pressureId))
            return false;
        Revision++;
        return true;
    }
}
