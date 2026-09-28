using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Research.Adaptive;

public sealed record AdaptiveResearchCivilizationStart(
    int CivilizationId,
    string SpeciesId,
    string ReferenceProfileId,
    string ApplicabilityContextId);

public sealed record AdaptiveResearchProjectFundingState(
    string NodeId,
    double ReservedMilestoneCredits,
    double ConsumedMilestoneCredits,
    double AuthorizationCredits = 0.0);

public sealed record AdaptiveResearchProjectFundingSnapshot(
    string NodeId,
    double ReservedMilestoneCredits,
    double ConsumedMilestoneCredits,
    double AuthorizationCredits = 0.0);

public sealed record AdaptiveResearchCampaignCivilizationSnapshot(
    int CivilizationId,
    string SpeciesId,
    string ReferenceProfileId,
    string ApplicabilityContextId,
    AdaptiveResearchStateSnapshotV5 Research,
    IReadOnlyList<AdaptiveResearchProjectFundingSnapshot>? ProjectFunding = null);

public sealed record AdaptiveResearchCampaignSnapshot(
    int SchemaVersion,
    string CatalogId,
    IReadOnlyList<AdaptiveResearchCampaignCivilizationSnapshot> Civilizations);

/// <summary>
/// Campaign owner for each civilization's authoritative Adaptive Research state. Static catalog
/// data remains shared by the runtime; only bounded civilization state belongs to the campaign.
/// </summary>
public sealed class AdaptiveResearchCampaignState
{
    private readonly IReadOnlyDictionary<int, AdaptiveResearchCivilizationState> _civilizations;
    private readonly IReadOnlyDictionary<int, AdaptiveResearchCivilizationStart> _starts;
    private readonly Dictionary<int, Dictionary<string, AdaptiveResearchProjectFundingState>> _projectFunding;

    internal AdaptiveResearchCampaignState(
        AdaptiveResearchStrategicRuntime runtime,
        IReadOnlyDictionary<int, AdaptiveResearchCivilizationState> civilizations,
        IReadOnlyDictionary<int, AdaptiveResearchCivilizationStart> starts)
    {
        Runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _civilizations = new ReadOnlyDictionary<int, AdaptiveResearchCivilizationState>(
            new Dictionary<int, AdaptiveResearchCivilizationState>(civilizations));
        _starts = new ReadOnlyDictionary<int, AdaptiveResearchCivilizationStart>(
            new Dictionary<int, AdaptiveResearchCivilizationStart>(starts));
        _projectFunding = civilizations.Keys.ToDictionary(
            civilizationId => civilizationId,
            _ => new Dictionary<string, AdaptiveResearchProjectFundingState>(StringComparer.Ordinal));
    }

    public AdaptiveResearchStrategicRuntime Runtime { get; }
    public IReadOnlyDictionary<int, AdaptiveResearchCivilizationState> Civilizations => _civilizations;
    public IReadOnlyDictionary<int, AdaptiveResearchCivilizationStart> Starts => _starts;

    public AdaptiveResearchCivilizationState GetCivilization(int civilizationId) =>
        _civilizations.TryGetValue(civilizationId, out var state)
            ? state
            : throw new KeyNotFoundException($"Campaign has no Adaptive Research state for civilization {civilizationId}.");

    public IReadOnlyDictionary<string, AdaptiveResearchProjectFundingState> GetProjectFunding(int civilizationId) =>
        new ReadOnlyDictionary<string, AdaptiveResearchProjectFundingState>(
            GetProjectFundingMutable(civilizationId));

    internal void ReserveProjectMilestones(
        int civilizationId,
        string nodeId,
        double authorizationCredits,
        double milestoneCredits)
    {
        if (!double.IsFinite(authorizationCredits) || authorizationCredits < 0.0)
            throw new ArgumentOutOfRangeException(nameof(authorizationCredits));
        if (!double.IsFinite(milestoneCredits) || milestoneCredits < 0.0)
            throw new ArgumentOutOfRangeException(nameof(milestoneCredits));
        var funding = GetProjectFundingMutable(civilizationId);
        if (!funding.TryAdd(nodeId, new AdaptiveResearchProjectFundingState(
                nodeId, milestoneCredits, 0.0, authorizationCredits)))
            throw new InvalidOperationException($"Research project '{nodeId}' already has milestone funding.");
    }

    internal bool ConsumeProjectMilestone(
        int civilizationId,
        string nodeId,
        bool finalMilestone,
        out double consumedCredits,
        out double remainingCredits)
    {
        var funding = GetProjectFundingMutable(civilizationId);
        if (!funding.TryGetValue(nodeId, out var current))
        {
            consumedCredits = 0.0;
            remainingCredits = 0.0;
            return false;
        }

        remainingCredits = Math.Max(0.0,
            current.ReservedMilestoneCredits - current.ConsumedMilestoneCredits);
        consumedCredits = finalMilestone
            ? remainingCredits
            : Math.Min(current.ReservedMilestoneCredits / 3.0, remainingCredits);
        remainingCredits = Math.Max(0.0, remainingCredits - consumedCredits);
        if (finalMilestone || remainingCredits <= 0.000001)
            funding.Remove(nodeId);
        else
            funding[nodeId] = current with
            {
                ConsumedMilestoneCredits = current.ConsumedMilestoneCredits + consumedCredits,
            };
        return true;
    }

    internal void RestoreProjectFunding(
        int civilizationId,
        IEnumerable<AdaptiveResearchProjectFundingSnapshot> snapshots)
    {
        var funding = GetProjectFundingMutable(civilizationId);
        foreach (var snapshot in snapshots)
        {
            if (string.IsNullOrWhiteSpace(snapshot.NodeId) ||
                !double.IsFinite(snapshot.ReservedMilestoneCredits) || snapshot.ReservedMilestoneCredits < 0.0 ||
                !double.IsFinite(snapshot.ConsumedMilestoneCredits) || snapshot.ConsumedMilestoneCredits < 0.0 ||
                snapshot.ConsumedMilestoneCredits > snapshot.ReservedMilestoneCredits + 0.000001 ||
                !double.IsFinite(snapshot.AuthorizationCredits) || snapshot.AuthorizationCredits < 0.0)
                throw new InvalidDataException("Adaptive Research contains invalid project milestone funding.");
            if (!GetCivilization(civilizationId).ActiveProjects.ContainsKey(snapshot.NodeId))
                throw new InvalidDataException(
                    $"Adaptive Research milestone funding references inactive project '{snapshot.NodeId}'.");
            if (!funding.TryAdd(snapshot.NodeId, new AdaptiveResearchProjectFundingState(
                    snapshot.NodeId,
                    snapshot.ReservedMilestoneCredits,
                    snapshot.ConsumedMilestoneCredits,
                    snapshot.AuthorizationCredits)))
                throw new InvalidDataException(
                    $"Adaptive Research duplicates milestone funding for '{snapshot.NodeId}'.");
        }
    }

    private Dictionary<string, AdaptiveResearchProjectFundingState> GetProjectFundingMutable(int civilizationId) =>
        _projectFunding.TryGetValue(civilizationId, out var funding)
            ? funding
            : throw new KeyNotFoundException(
                $"Campaign has no Adaptive Research funding state for civilization {civilizationId}.");
}

/// <summary>
/// Deterministically composes research history from species physiology without creating a
/// species-specific future technology tree. Every civilization continues through one shared graph.
/// </summary>
public sealed class AdaptiveResearchCampaignFactory
{
    public const string TerranProfileId = "reference_humanlike_solar_2050";
    public const string PelagicProfileId = "reference_pelagic_high_pressure_early_space";
    public const string HighGravityProfileId = "reference_high_gravity_metabolic_early_space";
    public const string CryogenicHydrocarbonProfileId = "reference_cryogenic_hydrocarbon_early_space";

    private readonly AdaptiveResearchStrategicRuntime _runtime;

    public AdaptiveResearchCampaignFactory(AdaptiveResearchStrategicRuntime runtime) =>
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));

    public AdaptiveResearchCampaignState Create(GalaxyState galaxy)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var states = new Dictionary<int, AdaptiveResearchCivilizationState>();
        var starts = new Dictionary<int, AdaptiveResearchCivilizationStart>();

        foreach (var civilization in galaxy.Civilizations.OrderBy(value => value.Id))
        {
            if (!states.TryAdd(civilization.Id, null!))
                throw new InvalidOperationException($"Duplicate civilization ID {civilization.Id} cannot own research state.");

            _ = SpeciesCatalog.Get(civilization.SpeciesId);
            var profileId = SelectReferenceProfile(civilization.SpeciesId);
            var contextId = $"species:{civilization.SpeciesId}";
            var composition = _runtime.Authority.ComposeReferenceProfile(
                $"civilization:{civilization.Id}",
                profileId,
                contextId);
            states[civilization.Id] = composition.State;
            starts.Add(civilization.Id, new AdaptiveResearchCivilizationStart(
                civilization.Id,
                civilization.SpeciesId,
                profileId,
                contextId));
        }

        return new AdaptiveResearchCampaignState(_runtime, states, starts);
    }

    public static string SelectReferenceProfile(string speciesId) => speciesId switch
    {
        SpeciesCatalog.TerranBaselineId => TerranProfileId,
        SpeciesCatalog.PelagicHighPressureId => PelagicProfileId,
        SpeciesCatalog.CompactHighGravityId => HighGravityProfileId,
        SpeciesCatalog.CryogenicHydrocarbonId => CryogenicHydrocarbonProfileId,
        _ => throw new KeyNotFoundException($"No Adaptive Research starting profile is registered for species '{speciesId}'."),
    };
}

public sealed class AdaptiveResearchCampaignSnapshotCodec
{
    public const int CurrentSchemaVersion = 2;
    private readonly AdaptiveResearchStrategicRuntime _runtime;
    private readonly AdaptiveResearchOutcomeSnapshotCodec _civilizationCodec;

    public AdaptiveResearchCampaignSnapshotCodec(AdaptiveResearchStrategicRuntime runtime)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _civilizationCodec = new AdaptiveResearchOutcomeSnapshotCodec(runtime);
    }

    public AdaptiveResearchCampaignSnapshot Capture(AdaptiveResearchCampaignState campaign)
    {
        ArgumentNullException.ThrowIfNull(campaign);
        if (!ReferenceEquals(campaign.Runtime, _runtime))
            throw new InvalidOperationException("Adaptive Research campaign belongs to a different runtime catalog instance.");
        return new AdaptiveResearchCampaignSnapshot(
            CurrentSchemaVersion,
            _runtime.Authority.Catalog.Metadata.CatalogId,
            campaign.Civilizations.Keys.OrderBy(id => id).Select(id =>
            {
                var start = campaign.Starts[id];
                return new AdaptiveResearchCampaignCivilizationSnapshot(
                    id,
                    start.SpeciesId,
                    start.ReferenceProfileId,
                    start.ApplicabilityContextId,
                    _civilizationCodec.Capture(campaign.Civilizations[id]),
                    campaign.GetProjectFunding(id).Values
                        .OrderBy(value => value.NodeId, StringComparer.Ordinal)
                        .Select(value => new AdaptiveResearchProjectFundingSnapshot(
                            value.NodeId,
                            value.ReservedMilestoneCredits,
                            value.ConsumedMilestoneCredits,
                            value.AuthorizationCredits))
                        .ToArray());
            }).ToArray());
    }

    public AdaptiveResearchCampaignState Restore(GalaxyState galaxy, AdaptiveResearchCampaignSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.SchemaVersion is not (1 or CurrentSchemaVersion))
            throw new InvalidDataException($"Unsupported Adaptive Research campaign schema {snapshot.SchemaVersion}.");
        var catalogId = _runtime.Authority.Catalog.Metadata.CatalogId;
        if (!string.Equals(snapshot.CatalogId, catalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research campaign catalog '{snapshot.CatalogId}' does not match runtime catalog '{catalogId}'.");

        var galaxyCivilizations = galaxy.Civilizations.ToDictionary(value => value.Id);
        if (snapshot.Civilizations.Count != galaxyCivilizations.Count)
            throw new InvalidDataException("Adaptive Research campaign must contain exactly one state for every civilization.");
        var states = new Dictionary<int, AdaptiveResearchCivilizationState>();
        var starts = new Dictionary<int, AdaptiveResearchCivilizationStart>();
        foreach (var entry in snapshot.Civilizations)
        {
            if (!galaxyCivilizations.TryGetValue(entry.CivilizationId, out var civilization))
                throw new InvalidDataException($"Adaptive Research references unknown civilization {entry.CivilizationId}.");
            if (!states.TryAdd(entry.CivilizationId, null!))
                throw new InvalidDataException($"Adaptive Research duplicates civilization {entry.CivilizationId}.");
            var expectedProfile = AdaptiveResearchCampaignFactory.SelectReferenceProfile(civilization.SpeciesId);
            var expectedContext = $"species:{civilization.SpeciesId}";
            if (!string.Equals(entry.SpeciesId, civilization.SpeciesId, StringComparison.Ordinal) ||
                !string.Equals(entry.ReferenceProfileId, expectedProfile, StringComparison.Ordinal) ||
                !string.Equals(entry.ApplicabilityContextId, expectedContext, StringComparison.Ordinal))
                throw new InvalidDataException($"Adaptive Research identity metadata does not match civilization {entry.CivilizationId}.");
            var state = _civilizationCodec.Restore(entry.Research);
            if (!string.Equals(state.CivilizationId, $"civilization:{entry.CivilizationId}", StringComparison.Ordinal))
                throw new InvalidDataException($"Adaptive Research state identity does not match civilization {entry.CivilizationId}.");
            states[entry.CivilizationId] = state;
            starts.Add(entry.CivilizationId, new AdaptiveResearchCivilizationStart(
                entry.CivilizationId,
                entry.SpeciesId,
                entry.ReferenceProfileId,
                entry.ApplicabilityContextId));
        }
        var campaign = new AdaptiveResearchCampaignState(_runtime, states, starts);
        if (snapshot.SchemaVersion >= 2)
            foreach (var entry in snapshot.Civilizations)
                campaign.RestoreProjectFunding(
                    entry.CivilizationId,
                    entry.ProjectFunding ?? Array.Empty<AdaptiveResearchProjectFundingSnapshot>());
        return campaign;
    }
}

public static class AdaptiveResearchDataLocator
{
    public static string FindDataRoot(string? startDirectory = null)
    {
        foreach (var start in new[] { startDirectory, Directory.GetCurrentDirectory(), AppContext.BaseDirectory }
                     .Where(value => !string.IsNullOrWhiteSpace(value))
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var directory = new DirectoryInfo(Path.GetFullPath(start!));
            while (directory is not null)
            {
                var candidate = Path.Combine(directory.FullName, "data", "research", "v1");
                if (File.Exists(Path.Combine(candidate, "index.json")))
                    return candidate;
                directory = directory.Parent;
            }
        }
        throw new DirectoryNotFoundException(
            "Adaptive Research data/research/v1 could not be located from the working or application directory.");
    }
}
