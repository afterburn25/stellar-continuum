using System;
using System.Collections.Generic;
using Game.Simulation.Combat;
using Game.Simulation.Exploration;

namespace Game.Simulation.Diplomacy;

public sealed record DiplomacyCampaignRuntimeStepResult(
    long Tick,
    int FirstContactEventsProcessed,
    int CombatIncidentsProcessed,
    DiplomacyCampaignMaintenanceResult Maintenance)
{
    public int MaintenanceTransitions =>
        Maintenance.ContactAging.NewlyStaleContacts +
        Maintenance.ProposalLifecycle.NewlyExpiredProposals;

    public int ProcessedDiplomacyEvents =>
        FirstContactEventsProcessed + CombatIncidentsProcessed;
}

/// <summary>
/// Plain-C# campaign runtime composition for Diplomacy.
///
/// This coordinator owns no new diplomatic policy. It composes the already-authoritative
/// directional Exploration handoff, attributable Combat consequences, observer-safe read/command
/// gateway, Combat hostility view, and scheduled bounded maintenance around one campaign-owned
/// DiplomacyState. Presentation and headless tools can therefore bind the same runtime behavior
/// without rebuilding those pieces independently.
/// </summary>
public sealed class DiplomacyCampaignRuntimeCoordinator
{
    private readonly ExplorationDiplomacyBridge _explorationBridge;
    private readonly CombatDiplomacyBridge _combatBridge;
    private readonly DiplomacyCampaignMaintenanceScheduler _maintenance;
    private long _lastProcessedTick = -1;

    public DiplomacyCampaignRuntimeCoordinator(
        DiplomacyState state,
        DiplomacyCampaignMaintenancePolicy? maintenancePolicy = null)
    {
        State = state ?? throw new ArgumentNullException(nameof(state));
        var diplomacy = new DiplomacySimulation(State);
        _explorationBridge = new ExplorationDiplomacyBridge(diplomacy);
        _combatBridge = new CombatDiplomacyBridge(State);
        _maintenance = new DiplomacyCampaignMaintenanceScheduler(State, maintenancePolicy);
        Commands = new ObserverDiplomacyCommandService(State);
        HostilityView = new DiplomacyCombatHostilityView(State);
    }

    public DiplomacyState State { get; }
    public ObserverDiplomacyCommandService Commands { get; }
    public ICombatHostilityView HostilityView { get; }
    public long LastProcessedTick => _lastProcessedTick;
    public long NextMaintenanceReviewTick => _maintenance.NextReviewTick;

    /// <summary>
    /// Preferred campaign Combat composition. Authoritative simulation and read-only order previews
    /// receive this runtime's exact same Diplomacy hostility view, preventing preview/issuance policy drift.
    /// </summary>
    public CombatCommandRuntime CreateCombatCommandRuntime() => new(HostilityView);

    /// <summary>
    /// Compatibility surface for callers that only need authoritative Combat execution and do not
    /// require command preview. New campaign Core composition should prefer CreateCombatCommandRuntime.
    /// </summary>
    public CombatSimulation CreateCombatSimulation() => new(HostilityView);

    public DiplomaticStateView BuildView(int observerCivilizationId) =>
        Commands.BuildView(observerCivilizationId);

    /// <summary>
    /// Rebinds transient campaign cadence after new-game creation or save-v9 load. The durable
    /// diplomatic state remains in DiplomacyState; only the non-persistent maintenance checkpoint
    /// is reset here.
    /// </summary>
    public void Reset(double simulationDays, bool reviewImmediately = true)
    {
        var tick = DiplomacyCampaignClock.FromSimulationDays(simulationDays);
        _maintenance.Reset(tick, reviewImmediately);
        _lastProcessedTick = tick;
    }

    /// <summary>
    /// Applies one campaign step's Diplomacy-owned consequences in the established authoritative
    /// order: legitimate Exploration contacts, attributable Combat incidents, then low-frequency
    /// lifecycle maintenance. Regressing campaign time is rejected rather than rewriting history.
    /// </summary>
    public DiplomacyCampaignRuntimeStepResult Process(
        IReadOnlyList<ExplorationEvent> explorationEvents,
        IReadOnlyList<CombatEvent> combatEvents,
        double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(explorationEvents);
        ArgumentNullException.ThrowIfNull(combatEvents);

        var tick = DiplomacyCampaignClock.FromSimulationDays(simulationDays);
        if (_lastProcessedTick >= 0 && tick < _lastProcessedTick)
        {
            throw new InvalidOperationException(
                "Campaign Diplomacy runtime cannot process an earlier tick after later diplomatic history has been applied.");
        }

        var firstContacts = explorationEvents.Count == 0
            ? 0
            : _explorationBridge.Process(explorationEvents, tick);
        var combatIncidents = combatEvents.Count == 0
            ? 0
            : _combatBridge.Process(combatEvents, tick);
        var maintenance = _maintenance.ReviewIfDue(tick);

        _lastProcessedTick = tick;
        return new DiplomacyCampaignRuntimeStepResult(
            tick,
            firstContacts,
            combatIncidents,
            maintenance);
    }
}
