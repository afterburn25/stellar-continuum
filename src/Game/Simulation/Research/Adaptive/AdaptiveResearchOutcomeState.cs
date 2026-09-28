using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchOutcomeNodeSummary(
    string NodeId,
    int Attempts,
    int Setbacks,
    int PartialSuccesses,
    int Refinements,
    int Disproofs,
    int Anomalies,
    int Hazards,
    int SideDiscoveries,
    string? LastOutcomeId,
    double LastOutcomeYear);

public sealed record ResearchOutcomeHistoryRecord(
    long Sequence,
    string NodeId,
    string CheckpointId,
    int AttemptIndex,
    ResearchOutcomeKind Outcome,
    string? SideDiscoveryNodeId,
    double Year,
    string Explanation);

/// <summary>
/// Sparse research-outcome history. Only projects that actually resolved uncertainty receive records;
/// old detailed events are bounded while per-node counters remain compact.
/// </summary>
public sealed class AdaptiveResearchOutcomeState
{
    private readonly Dictionary<string, ResearchOutcomeNodeSummary> _summaries = new(StringComparer.Ordinal);
    private readonly List<ResearchOutcomeHistoryRecord> _recent = new();

    public long Revision { get; private set; }
    public long NextSequence { get; private set; } = 1;
    public IReadOnlyDictionary<string, ResearchOutcomeNodeSummary> Summaries => new ReadOnlyDictionary<string, ResearchOutcomeNodeSummary>(_summaries);
    public IReadOnlyList<ResearchOutcomeHistoryRecord> RecentRecords => _recent.AsReadOnly();

    public ResearchOutcomeNodeSummary GetSummary(string nodeId) =>
        _summaries.TryGetValue(nodeId, out var summary)
            ? summary
            : new ResearchOutcomeNodeSummary(nodeId, 0, 0, 0, 0, 0, 0, 0, 0, null, double.NegativeInfinity);

    internal void Record(
        string nodeId,
        string checkpointId,
        int attemptIndex,
        ResearchOutcomeKind outcome,
        string? sideDiscoveryNodeId,
        double year,
        string explanation,
        int maxRecent,
        int maxPerNodeRecent)
    {
        var old = GetSummary(nodeId);
        var updated = old with
        {
            Attempts = Math.Max(old.Attempts, attemptIndex + 1),
            Setbacks = old.Setbacks + (outcome == ResearchOutcomeKind.Setback ? 1 : 0),
            PartialSuccesses = old.PartialSuccesses + (outcome == ResearchOutcomeKind.PartialSuccess ? 1 : 0),
            Refinements = old.Refinements + (outcome == ResearchOutcomeKind.HypothesisRefined ? 1 : 0),
            Disproofs = old.Disproofs + (outcome == ResearchOutcomeKind.HypothesisDisproven ? 1 : 0),
            Anomalies = old.Anomalies + (outcome == ResearchOutcomeKind.AnomalousResult ? 1 : 0),
            Hazards = old.Hazards + (outcome == ResearchOutcomeKind.HazardIncident ? 1 : 0),
            SideDiscoveries = old.SideDiscoveries + (sideDiscoveryNodeId is not null ? 1 : 0),
            LastOutcomeId = AdaptiveResearchOutcomeCatalog.OutcomeId(outcome),
            LastOutcomeYear = year,
        };
        _summaries[nodeId] = updated;
        _recent.Add(new ResearchOutcomeHistoryRecord(
            NextSequence++, nodeId, checkpointId, attemptIndex, outcome, sideDiscoveryNodeId, year, explanation));

        while (_recent.Count > maxRecent)
            _recent.RemoveAt(0);
        while (_recent.Count(record => string.Equals(record.NodeId, nodeId, StringComparison.Ordinal)) > maxPerNodeRecent)
        {
            var index = _recent.FindIndex(record => string.Equals(record.NodeId, nodeId, StringComparison.Ordinal));
            if (index < 0)
                break;
            _recent.RemoveAt(index);
        }
        Revision++;
    }

    internal void RestoreSummary(ResearchOutcomeNodeSummary summary)
    {
        _summaries[summary.NodeId] = summary;
        Revision++;
    }

    internal void RestoreRecord(ResearchOutcomeHistoryRecord record)
    {
        _recent.Add(record);
        NextSequence = Math.Max(NextSequence, record.Sequence + 1);
        Revision++;
    }
}
