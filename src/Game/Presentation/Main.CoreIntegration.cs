using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using Godot;
using Game.Diagnostics;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Time;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Construction;
using Game.Simulation.Shipbuilding;
using Game.Campaign;

namespace Game.Presentation;

/// <summary>
/// Godot-facing bridge for the plain-C# strategic runtime. Authoritative state mutation and
/// campaign-level Diplomacy composition remain outside presentation; this partial handles the
/// accepted clock, event presentation, aggregate diagnostics and redraw work.
/// </summary>
public partial class Main
{
    private DiplomacyState _diplomacyState = new();
    private AdaptiveResearchCampaignState? _adaptiveResearch;
    private readonly AdaptiveResearchCampaignSimulation _adaptiveResearchSimulation = new();
    private GalaxySimulationStepCoordinator _coreSimulation = new();
    private DiplomacyCampaignRuntimeCoordinator? _diplomacyRuntime;
    private double _simulationWorkTotalMs;
    private double _simulationWorkPeakMs;
    private int _simulationWorkSamples;

    private void RebuildIntegratedCoreSimulation()
    {
        if (_adaptiveResearch is null)
            throw new InvalidOperationException("Adaptive Research campaign state is not initialized.");
        _lastPlayerIndustryAllocation = null;
        var constructionCapabilities = new AdaptiveResearchConstructionCapabilityView(_adaptiveResearch);
        var shipbuildingCapabilities = new AdaptiveResearchShipbuildingCapabilityView(_adaptiveResearch);
        _construction = new ConstructionSimulation(constructionCapabilities);
        _shipbuilding = new ShipbuildingSimulation(shipbuildingCapabilities);
        _diplomacyRuntime = new DiplomacyCampaignRuntimeCoordinator(_diplomacyState);
        _diplomacyRuntime.Reset(_clock.SimulationDays, reviewImmediately: true);
        var strategicAi = new CivilizationStrategicRuntimeCoordinator(
            director: new CivilizationStrategicDirector(
                new CivilizationStrategicInputBuilder(shipbuildingCapabilities: shipbuildingCapabilities)),
            knowledgeProvider: new DiplomacyStrategicKnowledgeProvider(_diplomacyState));
        _coreSimulation = new GalaxySimulationStepCoordinator(
            construction: _construction,
            shipbuilding: _shipbuilding,
            strategicAi: strategicAi,
            combatRuntime: _diplomacyRuntime.CreateCombatCommandRuntime(),
            advanceLegacyResearch: false);
    }

    protected void RunIntegratedSimulationFrame(double delta)
    {
        if (_galaxy is null)
            return;

        var frameStart = _clock.SimulationDays;
        var steps = UiIsDeveloperMode
            ? PlayableDemoScenario.AdvanceFrame(_clock, delta)
            : new[] { _clock.Advance(delta) };
        var step = SimulationStepResult.Empty;
        var stepDay = frameStart;
        foreach (var simulationDays in steps)
        {
            stepDay += simulationDays;
            step = AdvanceIntegratedStep(simulationDays, stepDay);
        }

        // Save only after every bounded simulation/Diplomacy substep has resolved.
        RunIntegratedScheduledAutosave();

        _performanceLogTimer += delta;
        _statusTimer = Math.Max(0.0, _statusTimer - delta);

        if (_performanceLogTimer >= 5.0)
        {
            _performanceLogTimer = 0.0;
            var playerAllocation = step.IndustryAllocations.FirstOrDefault(allocation => allocation.CivilizationId == _galaxy.PlayerCivilizationId);
            var industryAllocation = playerAllocation is null
                ? "industryAllocation=idle"
                : $"industryAllocation=construction:{playerAllocation.ConstructionAllocated:0.0},shipbuilding:{playerAllocation.ShipbuildingAllocated:0.0}";

            SupportLogger.Log(
                "performance",
                $"date={CampaignCalendar.FormatDate(_clock.SimulationDays)} fps={Engine.GetFramesPerSecond()} requested={_clock.RequestedMultiplier:0.00}x effective={_clock.EffectiveMultiplier:0.00}x backlogDays={_clock.BacklogDays:0.000} simulationMeanMs={_simulationWorkTotalMs / Math.Max(1, _simulationWorkSamples):0.00} simulationPeakMs={_simulationWorkPeakMs:0.00} managedMemory={GC.GetTotalMemory(false)} fleets={_galaxy.Fleets.Count(f => f.IsActive)} colonies={_galaxy.Colonies.Count} industry={PlayerEconomy.Industry:0.0} science={PlayerEconomy.Science:0.0} {industryAllocation}");
            _simulationWorkTotalMs = _simulationWorkPeakMs = 0;
            _simulationWorkSamples = 0;
        }

        QueueRedraw();
    }

    private SimulationStepResult AdvanceIntegratedStep(double simulationDays, double stepDay)
    {
        var started = Stopwatch.GetTimestamp();
        var playerEconomy = _galaxy.Economies.First(state =>
            state.CivilizationId == _galaxy.PlayerCivilizationId);
        var previousOperatingFunding = playerEconomy.LastBaseOperationsFundingFraction;
        var step = _coreSimulation.Advance(_galaxy, simulationDays);
        if (simulationDays > 0)
            foreach (var civilization in _galaxy.Civilizations.OrderBy(x => x.Id))
                FleetCombatPower.RecordSensorContacts(_galaxy, civilization.Id, stepDay, HasCombatScanner(civilization.Id));
        if (simulationDays > 0.0)
            _lastPlayerIndustryAllocation = step.IndustryAllocations.FirstOrDefault(
                allocation => allocation.CivilizationId == _galaxy.PlayerCivilizationId);
        PublishOperatingFundingTransition(previousOperatingFunding,
            playerEconomy.LastBaseOperationsFundingFraction);
        if (_adaptiveResearch is not null)
        {
            var researchEvents = _adaptiveResearchSimulation.Advance(
                _galaxy, _adaptiveResearch, simulationDays, stepDay);
            HandleAdaptiveResearchEvents(researchEvents);
        }
        if (_diplomacyRuntime is not null)
        {
            var diplomacyStep = _diplomacyRuntime.Process(
                step.ExplorationEvents,
                step.CombatEvents,
                stepDay);
            HandleIntegratedDiplomacyRuntimeResult(diplomacyStep);
        }

        HandleConstructionEvents(step.ConstructionEvents);
        HandleShipbuildingEvents(step.ShipbuildingEvents);
        HandleResearchEvents(step.ResearchEvents);
        HandleExplorationEvents(step.ExplorationEvents);
        HandleCombatEvents(step.CombatEvents);
        HandleColonizationEvents(step.ColonizationEvents);
        // Observe each completed simulation step, including developer fast-forward steps.
        // Voice does not control the simulation clock or await speech generation.
        ObserveVoiceMilestones();
        var workMs = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
        _simulationWorkTotalMs += workMs;
        _simulationWorkPeakMs = Math.Max(_simulationWorkPeakMs, workMs);
        _simulationWorkSamples++;
        return step;
    }

    private void PublishOperatingFundingTransition(double previous, double current)
    {
        var wasFunded = previous >= 0.999999;
        var isFunded = current >= 0.999999;
        if (wasFunded == isFunded) return;

        var message = isFunded
            ? "Operating funding restored. Industrial production and fleet missions have resumed at full capacity."
            : $"Operating shortfall: only {current:P0} of current services are funded. Production and fleet missions are reduced until revenue recovers.";
        SetStatus(message, 7.0);
        PublishPlayerNotification("Economy", message);
        if (!isFunded) RouteEconomyVoice();
        SupportLogger.Log("economy-funding", $"funding={current:0.000} message={message}");
    }

    private void HandleAdaptiveResearchEvents(IReadOnlyList<AdaptiveResearchCampaignEvent> events)
    {
        var playerId = _galaxy.PlayerCivilizationId;
        foreach (var researchEvent in events.Where(value => value.CivilizationId == playerId))
        {
            SupportLogger.Log(researchEvent.IsOutcome ? "research-outcome" : "adaptive-research",
                $"node={researchEvent.NodeId} message={researchEvent.Message}");
            PublishPlayerNotification("Research", researchEvent.Message);
            RouteAdaptiveResearchVoice(researchEvent.NodeId);
        }
    }

    private void AdvanceDeveloperDays(double days)
    {
        // Developer command authorization occurs before this callback. Calendar, AI, resources,
        // construction, combat and diplomacy use exactly the ordinary simulation step path.
        for (var remaining = days; remaining > 0;)
        {
            var step = Math.Min(.25, remaining);
            _clock.Restore(_clock.SimulationDays + step);
            AdvanceIntegratedStep(step, _clock.SimulationDays);
            remaining -= step;
        }
        // The command boundary writes one checkpoint after the entire action.
        // A second immediate save would replace the pre-command recovery backup.
    }

    protected void RefreshIntegratedShipbuildingPresentation()
    {
        if (_galaxy is null)
            return;

        UpdateShipbuildingSummary();
    }

    private static void HandleIntegratedDiplomacyRuntimeResult(
        DiplomacyCampaignRuntimeStepResult result)
    {
        if (!result.Maintenance.Ran || result.MaintenanceTransitions <= 0)
            return;

        // Aggregate-only diagnostics preserve observer information boundaries while still
        // making scheduled state transitions debuggable.
        SupportLogger.Log(
            "diplomacy-maintenance",
            $"tick={result.Maintenance.ReviewTick} staleContacts={result.Maintenance.ContactAging.NewlyStaleContacts} expiredProposals={result.Maintenance.ProposalLifecycle.NewlyExpiredProposals}");
    }

    private void HandleCombatEvents(IReadOnlyList<CombatEvent> events)
    {
        var playerId = _galaxy.PlayerCivilizationId;
        foreach (var combatEvent in events)
        {
            // Presentation deliberately receives only player-involved combat for now.
            // Hidden third-party battles must not leak through status text or diagnostics.
            var playerInvolved = combatEvent.ActorCivilizationId == playerId ||
                                 combatEvent.TargetCivilizationId == playerId;
            if (!playerInvolved)
                continue;

            SupportLogger.Log(
                "combat",
                $"type={combatEvent.Type} system={combatEvent.SystemId?.ToString() ?? "none"} actorCiv={combatEvent.ActorCivilizationId} actorFleet={combatEvent.ActorFleetId} targetCiv={combatEvent.TargetCivilizationId?.ToString() ?? "none"} targetFleet={combatEvent.TargetFleetId?.ToString() ?? "none"} message={combatEvent.Message}");

            if (combatEvent.Type is CombatEventType.EngagementStarted or
                CombatEventType.FleetRetreatInitiated or
                CombatEventType.FleetEscaped or
                CombatEventType.FleetDestroyed or
                CombatEventType.EngagementEnded)
            {
                SetStatus(combatEvent.Message, 6.0);
                PublishPlayerNotification("Combat", combatEvent.Message);
                RouteCombatVoice(combatEvent);
            }
        }
    }
}
