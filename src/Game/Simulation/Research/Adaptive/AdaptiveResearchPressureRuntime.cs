using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Causal Research Pressure runtime. Owning simulation systems report normalized facts/events;
/// this layer updates only affected pressure IDs and delegates candidate wake-up to the existing
/// pressure-to-node index in AdaptiveResearchRuntime.
/// </summary>
public sealed class AdaptiveResearchPressureRuntime
{
    private readonly AdaptiveResearchRuntime _kernel;
    private readonly AdaptiveResearchPressureCatalog _catalog;
    private readonly ConditionalWeakTable<AdaptiveResearchCivilizationState, AdaptiveResearchPressureState> _states = new();

    public AdaptiveResearchPressureRuntime(
        AdaptiveResearchRuntime kernel,
        AdaptiveResearchPressureCatalog catalog)
    {
        _kernel = kernel ?? throw new ArgumentNullException(nameof(kernel));
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
    }

    public AdaptiveResearchPressureState GetSupportState(AdaptiveResearchCivilizationState state) =>
        _states.GetValue(state ?? throw new ArgumentNullException(nameof(state)), _ => new AdaptiveResearchPressureState());

    public void ReportMetricSignal(
        AdaptiveResearchCivilizationState state,
        string signalId,
        double normalizedValue)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(signalId);
        if (!_catalog.PressuresByMetricSignal.TryGetValue(signalId, out var pressureIds))
            throw new ArgumentException($"Unknown Research Pressure metric signal '{signalId}'.", nameof(signalId));
        if (normalizedValue < 0 || normalizedValue > 1 || double.IsNaN(normalizedValue) || double.IsInfinity(normalizedValue))
            throw new ArgumentOutOfRangeException(nameof(normalizedValue), normalizedValue, "Metric signal must be in 0..1.");

        var support = GetSupportState(state);
        support.SetMetricSignal(signalId, normalizedValue);
        if (normalizedValue > 0)
            foreach (var pressureId in pressureIds)
                support.ActivatePressure(pressureId);
    }

    public void ReportMetricSignals(
        AdaptiveResearchCivilizationState state,
        IReadOnlyDictionary<string, double> normalizedSignals)
    {
        ArgumentNullException.ThrowIfNull(normalizedSignals);
        foreach (var pair in normalizedSignals)
            ReportMetricSignal(state, pair.Key, pair.Value);
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> ReportEventSignal(
        AdaptiveResearchCivilizationState state,
        string eventSignalId,
        double normalizedSeverity,
        string? targetApplicabilityContextId = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(eventSignalId);
        if (!_catalog.PressuresByEventSignal.TryGetValue(eventSignalId, out var pressureIds))
            throw new ArgumentException($"Unknown Research Pressure event signal '{eventSignalId}'.", nameof(eventSignalId));
        if (normalizedSeverity < 0 || normalizedSeverity > 1 || double.IsNaN(normalizedSeverity) || double.IsInfinity(normalizedSeverity))
            throw new ArgumentOutOfRangeException(nameof(normalizedSeverity), normalizedSeverity, "Event severity must be in 0..1.");
        if (normalizedSeverity <= 0)
            return Array.Empty<AdaptiveResearchRuntimeEvent>();

        var support = GetSupportState(state);
        var events = new List<AdaptiveResearchRuntimeEvent>();
        foreach (var pressureId in pressureIds)
        {
            support.ActivatePressure(pressureId);
            var rule = _catalog.Rules[pressureId];
            var pulse = _catalog.RuntimePolicy.EventPulseBasePoints * normalizedSeverity;
            var next = Math.Clamp(Math.Max(rule.MemoryFloor, state.GetPressure(pressureId) + pulse), 0.0, 100.0);
            events.AddRange(_kernel.SetPressure(state, pressureId, next, targetApplicabilityContextId));
        }
        return events;
    }

    /// <summary>
    /// Low-frequency maintenance. Iterates only previously activated pressure IDs, never all
    /// research nodes and never all 59 pressure definitions for an untouched civilization.
    /// </summary>
    public IReadOnlyList<AdaptiveResearchRuntimeEvent> Advance(
        AdaptiveResearchCivilizationState state,
        double elapsedYears,
        string? targetApplicabilityContextId = null)
    {
        if (elapsedYears <= 0 || double.IsNaN(elapsedYears) || double.IsInfinity(elapsedYears))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();

        var support = GetSupportState(state);
        var events = new List<AdaptiveResearchRuntimeEvent>();
        foreach (var pressureId in support.ActivePressureIds.ToArray())
        {
            var rule = _catalog.Rules[pressureId];
            var target = CalculateMetricTarget(support, rule);
            var current = state.GetPressure(pressureId);
            double next;

            if (target > current)
            {
                next = Math.Min(target, current + (_catalog.RuntimePolicy.RiseTowardTargetPerYear * elapsedYears));
            }
            else
            {
                var floor = Math.Max(target, rule.MemoryFloor);
                next = Math.Max(floor, current - (rule.DecayPerYear * elapsedYears));
            }

            next = Math.Clamp(next, 0.0, 100.0);
            if (Math.Abs(next - current) > 0.000001)
                events.AddRange(_kernel.SetPressure(state, pressureId, next, targetApplicabilityContextId));

            var hasMetricSupport = rule.MetricSignalIds.Any(signalId => support.GetMetricSignal(signalId) > 0.0);
            if (!hasMetricSupport && next <= 0.0 && rule.MemoryFloor <= 0.0)
                support.DeactivatePressure(pressureId);
        }
        return events;
    }

    public double GetMetricTarget(AdaptiveResearchCivilizationState state, string pressureId)
    {
        if (!_catalog.Rules.TryGetValue(pressureId, out var rule))
            throw new ArgumentException($"Unknown Research Pressure '{pressureId}'.", nameof(pressureId));
        return CalculateMetricTarget(GetSupportState(state), rule);
    }

    private double CalculateMetricTarget(
        AdaptiveResearchPressureState support,
        ResearchPressureRuleDefinition rule)
    {
        var values = rule.MetricSignalIds
            .Select(support.GetMetricSignal)
            .Where(value => value > 0.0)
            .ToArray();
        if (values.Length == 0)
            return 0.0;

        var strongest = values.Max();
        var mean = values.Average();
        var policy = _catalog.RuntimePolicy;
        return Math.Clamp(100.0 * ((policy.StrongestSignalWeight * strongest) + (policy.MeanSignalWeight * mean)), 0.0, 100.0);
    }
}
