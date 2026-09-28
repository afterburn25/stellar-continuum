using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Godot;
using Game.Diagnostics;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Colonization;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Time;
using Game.Simulation.Shipbuilding;
using Game.Campaign;

namespace Game.Presentation;

public partial class Main : Node2D
{
    private readonly SimulationClock _clock = new();
    private readonly ExplorationSimulation _exploration = new();
    private readonly ColonizationSimulation _colonization = new();
    private readonly EconomySimulation _economy = new();
    private readonly ResearchSimulation _research = new();
    private ConstructionSimulation _construction = new();
    private readonly DiagnosticsBuffer _diagnostics = new();
    private readonly CampaignSaveService _saveService = new();

    private GalaxyState _galaxy = null!;
    private Font _font = null!;
    private int _selectedSystemId = -1;
    private int _researchCandidateIndex;
    private int _constructionCandidateIndex;
    private Godot.Vector2 _pan = Godot.Vector2.Zero;
    private float _zoom = Spatial.SpatialNavigationLayout.StellarRegionScale;
    private bool _panning;
    private bool _leftPanCandidate;
    private bool _leftPanMoved;
    private Godot.Vector2 _leftPanStart;
    private double _performanceLogTimer;
    private bool _integratedExitRequested;
    public ulong UiWindowLifecycleRevision { get; protected set; }
    public string UiLastWindowLifecycle { get; protected set; } = "startup";
    private string _statusText = string.Empty;
    private double _statusTimer;

    private string AutosavePath => ProjectSettings.GlobalizePath("user://saves/autosave.json");
    private CivilizationState PlayerCivilization => _galaxy.Civilizations.First(c => c.Id == _galaxy.PlayerCivilizationId);
    private CivilizationEconomyState PlayerEconomy => _galaxy.Economies.First(e => e.CivilizationId == _galaxy.PlayerCivilizationId);
    private ConstructionState PlayerConstruction => _galaxy.ConstructionStates.First(c => c.CivilizationId == _galaxy.PlayerCivilizationId);
    private FleetState? PlayerScout => _galaxy.Fleets.FirstOrDefault(f => f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId && f.Role == FleetRole.Scout);
    private FleetState? PlayerColonyShip => _galaxy.Fleets.FirstOrDefault(f => f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId &&
        f.Role == FleetRole.Colony && f.DesignId != ShipDesignRegistry.ResourceOutpostShipId);

    public override void _Ready()
    {
        _font = ThemeDB.FallbackFont;
        SupportLogger.Initialize();

        if (File.Exists(AutosavePath))
        {
            try
            {
                var loaded = _saveService.Load(AutosavePath);
                _galaxy = loaded.Galaxy;
                _clock.Restore(loaded.SimulationDays);
                SetStatus($"Loaded autosave from {loaded.SavedAtUtc.LocalDateTime:g}");
                SupportLogger.Log("save", $"Loaded autosave seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(_clock.SimulationDays)} stage={PlayerCivilization.DevelopmentStage} format={CampaignSaveService.CurrentFormatVersion}");
            }
            catch (Exception ex)
            {
                SupportLogger.Log("save-error", ex.ToString());
                GenerateNewGalaxy();
                SetStatus("Autosave could not be loaded; generated a new 2050 campaign.");
            }
        }
        else
        {
            GenerateNewGalaxy();
        }

        QueueRedraw();
    }

    public override void _Process(double delta)
    {
        var simulationDays = _clock.Advance(delta);
        _economy.Advance(_galaxy, simulationDays);
        HandleConstructionEvents(_construction.Advance(_galaxy, simulationDays: simulationDays));
        HandleShipbuildingEvents(_shipbuilding.Advance(_galaxy, simulationDays: simulationDays));
        HandleResearchEvents(_research.Advance(_galaxy));
        HandleExplorationEvents(_exploration.Advance(_galaxy, simulationDays));
        HandleColonizationEvents(_colonization.Advance(_galaxy, simulationDays));

        _performanceLogTimer += delta;
        _statusTimer = Math.Max(0.0, _statusTimer - delta);

        if (_performanceLogTimer >= 5.0)
        {
            _performanceLogTimer = 0.0;
            SupportLogger.Log(
                "performance",
                $"date={CampaignCalendar.FormatDate(_clock.SimulationDays)} fps={Engine.GetFramesPerSecond()} requested={_clock.RequestedMultiplier:0.00}x effective={_clock.EffectiveMultiplier:0.00}x backlogDays={_clock.BacklogDays:0.000} managedMemory={GC.GetTotalMemory(false)} fleets={_galaxy.Fleets.Count(f => f.IsActive)} colonies={_galaxy.Colonies.Count} industry={PlayerEconomy.Industry:0.0} science={PlayerEconomy.Science:0.0}");
        }

        QueueRedraw();
    }

    public override void _Notification(int what)
    {
        if (what == NotificationWMCloseRequest && _galaxy is not null)
        {
            TryAutosave();
            UiVoice?.Stop();
            _ = AudioDirector.ShutdownAndQuitAsync(GetTree());
        }
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (ShouldBlockGameplayInput())
            return;

        if (@event is InputEventKey key && key.Pressed && !key.Echo)
        {
            switch (key.Keycode)
            {
                case Key.Space: UiTogglePause(); break;
                case Key.Key1: _clock.SetSpeed(SimulationClock.SpeedLevel.Normal); break;
                case Key.Key2: _clock.SetSpeed(SimulationClock.SpeedLevel.Fast); break;
                case Key.Key3: _clock.SetSpeed(SimulationClock.SpeedLevel.VeryFast); break;
                case Key.Key4: _clock.SetSpeed(SimulationClock.SpeedLevel.Maximum); break;
                case Key.T: CycleResearchCandidate(); break;
                case Key.R: StartSelectedResearch(); break;
                case Key.C: CycleConstructionCandidate(); break;
                case Key.B: StartSelectedConstruction(); break;
                case Key.N: UiNewCampaign(); break;
                case Key.Escape: UiOpenMenu(); break;
                case Key.F6: UiSave(); break;
                case Key.F8:
                    UiExportDiagnostics();
                    break;
            }
            QueueRedraw();
        }

        if (@event is InputEventMouseButton mouseButton)
        {
            if (mouseButton.ButtonIndex == MouseButton.WheelUp && mouseButton.Pressed)
                ZoomSpatialAt(1.22f, mouseButton.Position);
            else if (mouseButton.ButtonIndex == MouseButton.WheelDown && mouseButton.Pressed)
                ZoomSpatialAt(1f / 1.22f, mouseButton.Position);
            else if (mouseButton.ButtonIndex == MouseButton.Middle)
                _panning = mouseButton.Pressed;
            else if (mouseButton.ButtonIndex == MouseButton.Left)
            {
                if (mouseButton.Pressed)
                {
                    _leftPanCandidate = true;
                    _leftPanMoved = false;
                    _leftPanStart = mouseButton.Position;
                }
                else if (_leftPanCandidate)
                {
                    if (!_leftPanMoved)
                    {
                        if (IsInsideGalacticCoreMarker(mouseButton.Position))
                            ExplainUnavailableGalacticCore();
                        else if (UiOverviewBlend > 0.5f && mouseButton.Position.DistanceTo(UiMapOriginScreen) <= 48)
                            UiShowStellarRegion();
                        else
                            SelectNearestCatalogSystem(mouseButton.Position);
                    }
                    _leftPanCandidate = false;
                    _leftPanMoved = false;
                }
            }
            else if (mouseButton.ButtonIndex == MouseButton.Right && mouseButton.Pressed)
            {
                if (IsInsideGalacticCoreMarker(mouseButton.Position)) ExplainUnavailableGalacticCore();
                else IssueSelectedFleetOrderAt(mouseButton.Position);
            }
            QueueRedraw();
        }

        if (@event is InputEventMouseMotion hoverMotion && !_leftPanCandidate && !_panning)
            _hoverDestinationId = IsInsideGalacticCoreMarker(hoverMotion.Position)
                ? null : FindNearestCatalogSystem(hoverMotion.Position, 18)?.Id;

        if (@event is InputEventMouseMotion motion && _leftPanCandidate)
        {
            if (!_leftPanMoved && motion.Position.DistanceTo(_leftPanStart) >= 5)
                _leftPanMoved = true;
            if (_leftPanMoved)
            {
                PanRegionalCamera(motion.Relative);
                QueueRedraw();
            }
        }
        else if (@event is InputEventMouseMotion regionalMotion && _panning)
        {
            PanRegionalCamera(regionalMotion.Relative);
            QueueRedraw();
        }
    }

    public override void _Draw()
    {
        // The opaque system canvas covers this entire pass; avoid traversing the stellar catalog
        // and drawing strategic fleets at the wrong spatial scale while it is active.
        if (UiIsSystemSpatialView)
            return;

        var viewport = GetViewportRect();
        var center = viewport.Size * 0.5f + _pan;
        var player = PlayerCivilization;
        var economy = PlayerEconomy;
        var home = _galaxy.Systems.First(s => s.Id == player.HomeSystemId);
        var knownIds = _galaxy.Knowledge.GetKnownSystems(player.Id);

        DrawCircle(ToScreen(home.Position, center), 95.0f * _zoom, new Color(0.28f, 0.62f, 0.95f, 0.12f), false, 1.0f);

        foreach (var system in _galaxy.Systems)
        {
            var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(player.Id, system.Id);
            var known = surveyLevel != SystemSurveyLevel.Unknown;
            var position = ToScreen(system.Position, center);
            var isHome = system.Id == player.HomeSystemId;
            var radius = system.Id == _selectedSystemId ? 6.0f : isHome ? 5.0f : known ? 3.3f : 2.0f;
            var starColor = surveyLevel switch
            {
                SystemSurveyLevel.FullySurveyed => GetStarColor(system.Archetype),
                SystemSurveyLevel.PartiallySurveyed => new Color(0.58f, 0.67f, 0.78f, 0.82f),
                SystemSurveyLevel.Detected => new Color(0.48f, 0.54f, 0.64f, 0.72f),
                _ => new Color(0.38f, 0.42f, 0.50f, 0.55f),
            };
            DrawCircle(position, radius, starColor);
            if (isHome) DrawCircle(position, 10.5f, new Color(0.30f, 0.76f, 1.0f, 0.75f), false, 2.0f);
            if (system.Id == _selectedSystemId) DrawCircle(position, 13.0f, new Color(0.95f, 0.95f, 1.0f, 0.38f), false, 1.5f);
        }

        DrawKnownColonies(center, player.Id);
        DrawKnownCivilizationHomes(center, player.Id);
        if (PlayerScout is { } scout) DrawPlayerFleet(center, scout, new Color(0.38f, 0.88f, 1.0f));
        if (PlayerColonyShip is { } colonyShip) DrawPlayerFleet(center, colonyShip, new Color(0.45f, 1.0f, 0.55f));

        DrawString(_font, new Godot.Vector2(18, 26), $"STELLAR CONTINUUM {GameVersion.Display}  |  {CampaignCalendar.FormatDate(_clock.SimulationDays)}", HorizontalAlignment.Left, -1, 18, Colors.White);
        DrawString(_font, new Godot.Vector2(18, 49), $"{player.Name} | {player.Archetype} | Stage: {player.DevelopmentStage} | Colonies: {_galaxy.Colonies.Count(c => c.CivilizationId == player.Id)} | Known systems: {knownIds.Count}/{_galaxy.Systems.Count}", HorizontalAlignment.Left, -1, 15, new Color(0.78f, 0.83f, 0.92f));
        var researchCapacity = BuildPlayerAdaptiveResearchView().DirectedProgramCapacity;
        var totalLabs = _adaptiveResearch!.GetCivilization(player.Id).TotalEffectiveResearchLabs;
        DrawString(_font, new Godot.Vector2(18, 70), $"{UiCurrency.Code} {UiFormatMoney(economy.Credits)} ({UiFormatMoneyRate(economy.LastCreditsPerSecond)}) | Industry {economy.Industry:0.0} (+{economy.LastIndustryPerSecond:0.00}/day) | Labs {researchCapacity.FreeEffectiveLabs:0.#}/{totalLabs:0.#} free", HorizontalAlignment.Left, -1, 13, new Color(0.72f, 0.82f, 0.72f));
        DrawResearchLine(92);
        DrawConstructionLine(112);

        var operations = player.DevelopmentStage == CivilizationDevelopmentStage.PreWarp
            ? "Pre-warp era | T/R research | C/B construction | build infrastructure and achieve experimental interstellar transit"
            : "Select a ship, then right-click its destination | Left-drag to pan | Scroll to zoom";
        DrawString(_font, new Godot.Vector2(18, 134), operations, HorizontalAlignment.Left, -1, 13, new Color(0.68f, 0.75f, 0.87f));
        DrawString(_font, new Godot.Vector2(18, 154), $"Speed {_clock.RequestedMultiplier:0}x ({_clock.EffectiveMultiplier:0.00}x effective) | Space pause | Keys 1-4 choose speed | Wheel zoom | Left-drag pan | N new 2050 campaign | F6 save | F8 diagnostics", HorizontalAlignment.Left, -1, 12, new Color(0.58f, 0.65f, 0.75f));

        DrawSelectionDetails(viewport, player);
    }

    private void DrawResearchLine(float y)
    {
        var view = BuildPlayerAdaptiveResearchView();
        string line;
        if (view.ActiveProjects.FirstOrDefault() is { } active)
        {
            var definition = _adaptiveResearch!.Runtime.Authority.Catalog.GetNode(active.NodeId);
            var funding = ResearchFundingQuote(active.NodeId, active.AssignedEffectiveLabs);
            var milestoneRemaining = _adaptiveResearch.GetProjectFunding(_galaxy.PlayerCivilizationId)
                .TryGetValue(active.NodeId, out var projectFunding)
                    ? Math.Max(0.0, projectFunding.ReservedMilestoneCredits -
                        projectFunding.ConsumedMilestoneCredits)
                    : 0.0;
            line = $"Research: {definition.Name} — {active.Stage} {active.StageProgress * 100:0.0}% · " +
                $"{active.AssignedEffectiveLabs:0.#} labs · {UiFormatMoneyRate(-funding.OperatingCreditsPerDay)} · " +
                $"{UiFormatMoney(milestoneRemaining)} milestones · {PlayerEconomy.LastResearchFundingFraction:P0} funded";
        }
        else
        {
            var candidate = GetResearchCandidate();
            line = candidate is null ? "Research: waiting on prerequisites or free lab capacity" : $"Research candidate: {candidate.DisplayName} ({candidate.RecommendedLabs ?? candidate.MinimumLabs} labs) — T cycle, R begin";
        }
        DrawString(_font, new Godot.Vector2(18, y), line, HorizontalAlignment.Left, -1, 13, new Color(0.78f, 0.70f, 0.95f));
    }

    private void DrawConstructionLine(float y)
    {
        string line;
        if (PlayerConstruction.ActiveProjectId is { } activeId)
        {
            var project = ConstructionRegistry.Get(activeId);
            var percent = project.IndustryCost <= 0.0 ? 100.0 : PlayerConstruction.ActiveProjectProgress / project.IndustryCost * 100.0;
            line = $"Construction: {project.Name} — {PlayerConstruction.ActiveProjectProgress:0}/{project.IndustryCost:0} ({percent:0.0}%)";
        }
        else
        {
            var candidate = GetConstructionCandidate();
            line = candidate is null ? "Construction: no project currently available" : $"Construction candidate: {candidate.Name} ({candidate.IndustryCost:0}) — C cycle, B begin";
        }
        DrawString(_font, new Godot.Vector2(18, y), line, HorizontalAlignment.Left, -1, 13, new Color(0.92f, 0.70f, 0.48f));
    }

    private void CycleResearchCandidate()
    {
        var available = GetAdaptiveResearchCandidates();
        if (available.Count == 0) { SetStatus("No research choices are currently available. A construction prerequisite may be missing."); return; }
        _researchCandidateIndex = (_researchCandidateIndex + 1) % available.Count;
        SetStatus($"Research candidate: {available[_researchCandidateIndex].DisplayName}");
    }

    private void StartSelectedResearch()
    {
        var candidate = GetResearchCandidate();
        if (candidate is null) { SetStatus("No available research project selected. Check construction prerequisites."); return; }
        var result = StartAdaptiveResearch(candidate.NodeId);
        SetStatus(result.Message, 6.0);
        SupportLogger.Log("research-order", $"technology={candidate.NodeId} accepted={result.Accepted} message={result.Message}");
        if (result.Accepted)
            PublishPlayerNotification("Research", result.Message);
    }

    private AdaptiveResearchNodeView? GetResearchCandidate()
    {
        var available = GetAdaptiveResearchCandidates();
        if (available.Count == 0) return null;
        _researchCandidateIndex = Math.Clamp(_researchCandidateIndex, 0, available.Count - 1);
        return available[_researchCandidateIndex];
    }

    private AdaptiveResearchView BuildPlayerAdaptiveResearchView()
    {
        if (_adaptiveResearch is null)
            throw new InvalidOperationException("Adaptive Research campaign state is not initialized.");
        return _adaptiveResearch.Runtime.Authority.Kernel.BuildView(
            _adaptiveResearch.GetCivilization(_galaxy.PlayerCivilizationId),
            $"species:{PlayerCivilization.SpeciesId}");
    }

    private IReadOnlyList<AdaptiveResearchNodeView> GetAdaptiveResearchCandidates()
    {
        if (_galaxy is null || _adaptiveResearch is null) return Array.Empty<AdaptiveResearchNodeView>();
        var view = BuildPlayerAdaptiveResearchView();
        var active = view.ActiveProjects.Select(value => value.NodeId).ToHashSet(StringComparer.Ordinal);
        var capacityAvailable = view.DirectedProgramCapacity.LabCapacityOnly ||
            view.DirectedProgramCapacity.MaximumDirectedPrograms is null ||
            view.DirectedProgramCapacity.ActiveProgramCount < view.DirectedProgramCapacity.MaximumDirectedPrograms;
        return view.VisibleNodes
            .Where(value => value.State >= ResearchMaturity.Investigable && value.State < ResearchMaturity.Mature &&
                !active.Contains(value.NodeId) && value.Blockers.Count == 0 && value.MinimumLabs is int minimum &&
                minimum <= view.DirectedProgramCapacity.FreeEffectiveLabs + 0.000001 && capacityAvailable)
            .OrderBy(value => EarlyCampaignResearchPlan.Rank(value.NodeId))
            .ThenBy(value => _adaptiveResearch.Runtime.Authority.Catalog.GetNode(value.NodeId).GraphDepth)
            .ThenBy(value => value.DisplayName, StringComparer.Ordinal)
            .ToArray();
    }

    private AdaptiveResearchCommandResult StartAdaptiveResearch(string nodeId)
    {
        if (_adaptiveResearch is null || _galaxy is null)
            return AdaptiveResearchCommandResult.Rejected("Adaptive Research is not initialized.");
        var state = _adaptiveResearch.GetCivilization(_galaxy.PlayerCivilizationId);
        var node = _adaptiveResearch.Runtime.Authority.Catalog.GetNode(nodeId);
        var labs = Math.Min(node.ProjectRequirements.RecommendedLabs, state.FreeEffectiveLabs);
        return AdaptiveResearchCampaignCommands.StartDirectedResearch(
            _galaxy, _adaptiveResearch, _galaxy.PlayerCivilizationId,
            nodeId, labs, $"species:{PlayerCivilization.SpeciesId}");
    }

    private void CycleConstructionCandidate()
    {
        if (PlayerConstruction.ActiveProjectId is not null) { SetStatus("Complete the current construction project before selecting another."); return; }
        var available = _construction.GetAvailableProjects(_galaxy, _galaxy.PlayerCivilizationId);
        if (available.Count == 0) { SetStatus("No construction choices are currently available. Research may be required."); return; }
        _constructionCandidateIndex = (_constructionCandidateIndex + 1) % available.Count;
        SetStatus($"Construction candidate: {available[_constructionCandidateIndex].Name}");
    }

    private void StartSelectedConstruction()
    {
        var candidate = GetConstructionCandidate();
        if (candidate is null) { SetStatus("No available construction project selected."); return; }
        var result = _construction.StartProject(_galaxy, _galaxy.PlayerCivilizationId, candidate.Id);
        SetStatus(result.Message, 6.0);
        SupportLogger.Log("construction-order", $"project={candidate.Id} accepted={result.Accepted} message={result.Message}");
        if (result.Accepted)
            PublishPlayerNotification("Construction", result.Message);
    }

    private ConstructionProjectDefinition? GetConstructionCandidate()
    {
        if (PlayerConstruction.ActiveProjectId is not null) return null;
        var available = _construction.GetAvailableProjects(_galaxy, _galaxy.PlayerCivilizationId);
        if (available.Count == 0) return null;
        _constructionCandidateIndex = Math.Clamp(_constructionCandidateIndex, 0, available.Count - 1);
        return available[_constructionCandidateIndex];
    }

    private void HandleResearchEvents(IReadOnlyList<ResearchEvent> events)
    {
        foreach (var e in events)
        {
            SupportLogger.Log("research", $"civilization={e.CivilizationId} technology={e.TechnologyId} message={e.Message}");
            if (e.CivilizationId != _galaxy.PlayerCivilizationId) continue;
            _researchCandidateIndex = 0;
            _constructionCandidateIndex = 0;
            SetStatus(e.Message, e.Message.Contains("warp-capable", StringComparison.OrdinalIgnoreCase) ? 10.0 : 6.0);
            PublishPlayerNotification("Research", e.Message);
            RouteResearchVoice(e);
        }
    }

    private void HandleConstructionEvents(IReadOnlyList<ConstructionEvent> events)
    {
        foreach (var e in events)
        {
            SupportLogger.Log("construction", $"civilization={e.CivilizationId} project={e.ProjectId} message={e.Message}");
            if (e.CivilizationId != _galaxy.PlayerCivilizationId) continue;
            _constructionCandidateIndex = 0;
            _researchCandidateIndex = 0;
            SetStatus(e.Message, 6.0);
            PublishPlayerNotification("Construction", e.Message);
            RouteConstructionVoice(e);
        }
    }

    private void DrawKnownColonies(Godot.Vector2 center, int playerId)
    {
        foreach (var colony in _galaxy.Colonies)
        {
            var own = colony.CivilizationId == playerId;
            if (!own && (!_galaxy.Knowledge.IsSystemFullySurveyed(playerId, colony.SystemId) || !_galaxy.Knowledge.IsCivilizationKnown(playerId, colony.CivilizationId))) continue;
            var system = _galaxy.Systems.First(s => s.Id == colony.SystemId);
            var color = own ? new Color(0.32f, 0.92f, 0.62f, 0.78f) : new Color(0.96f, 0.42f, 0.38f, 0.72f);
            DrawCircle(ToScreen(system.Position, center), 12.0f, color, false, 2.0f);
        }
    }

    private void DrawKnownCivilizationHomes(Godot.Vector2 center, int playerId)
    {
        foreach (var civilization in _galaxy.Civilizations)
        {
            if (civilization.Id == playerId ||
                !_galaxy.Knowledge.IsCivilizationKnown(playerId, civilization.Id) ||
                !_galaxy.Knowledge.IsSystemFullySurveyed(playerId, civilization.HomeSystemId)) continue;
            var home = _galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
            var color = civilization.IsSeededAncient ? new Color(0.92f, 0.72f, 0.28f, 0.70f) : new Color(0.95f, 0.36f, 0.36f, 0.55f);
            DrawCircle(ToScreen(home.Position, center), 16.0f, color, false, 1.5f);
        }
    }

    private void DrawPlayerFleet(Godot.Vector2 center, FleetState fleet, Color color)
    {
        var position = ToScreen(fleet.Position, center);
        if (fleet.DestinationSystemId is not null)
        {
            var routeIds = fleet.PlannedRouteSystemIds.Count > 0
                ? fleet.PlannedRouteSystemIds
                : new List<int> { fleet.DestinationSystemId.Value };
            var routeStart = position;
            foreach (var routeSystemId in routeIds)
            {
                var destination = _galaxy.Systems.First(s => s.Id == routeSystemId);
                var routeEnd = ToScreen(destination.Position, center);
                DrawDashedLine(routeStart, routeEnd, new Color(color.R, color.G, color.B, 0.50f), 1.0f, 6.0f);
                routeStart = routeEnd;
            }
        }
        DrawCircle(position, fleet.Role == FleetRole.Colony ? 5.5f : 5.0f, color);
        DrawCircle(position, 9.0f, new Color(color.R, color.G, color.B, 0.30f), false, 1.5f);
    }

    private void DrawSelectionDetails(Rect2 viewport, CivilizationState player)
    {
        if (_selectedSystemId < 0) return;
        var selected = _galaxy.Systems.First(s => s.Id == _selectedSystemId);
        var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(player.Id, selected.Id);
        string text;
        if (surveyLevel == SystemSurveyLevel.Unknown)
        {
            text = player.DevelopmentStage == CivilizationDevelopmentStage.PreWarp
                ? "Unknown | Interstellar travel not yet available"
                : "Unknown | Select a ship, then right-click to send it";
        }
        else if (surveyLevel != SystemSurveyLevel.FullySurveyed)
        {
            var progress = _galaxy.Knowledge.GetSystemSurveyProgress(player.Id, selected.Id);
            text = $"{selected.Name} | {surveyLevel} | Survey {progress:P0} | Detailed planet/resource/native data unavailable until science survey completes";
        }
        else
        {
            var colony = _galaxy.Colonies.FirstOrDefault(c => c.SystemId == selected.Id);
            var visibleColony = colony is not null && (colony.CivilizationId == player.Id || _galaxy.Knowledge.IsCivilizationKnown(player.Id, colony.CivilizationId));
            var ownerLabel = visibleColony ? $" | Colony: {colony!.Name} | Pop {colony.PopulationMillions:0.0}M" : string.Empty;
            var preWarpLabel = selected.HasPreWarpCivilization ? " | NATIVE PRE-WARP CIVILIZATION" : string.Empty;
            text = $"{selected.Name} | {selected.Archetype}{ownerLabel}{preWarpLabel} | Habitable: {YesNo(selected.HasHabitableWorld)} | Anomaly: {YesNo(selected.HasAnomaly)} | Rare: {YesNo(selected.HasRareResource)}";
        }
        DrawString(_font, new Godot.Vector2(18, viewport.Size.Y - 24), text, HorizontalAlignment.Left, Math.Max(300, viewport.Size.X - 36), 15, new Color(0.88f, 0.90f, 0.96f));
    }

    private void HandleExplorationEvents(IReadOnlyList<ExplorationEvent> events)
    {
        foreach (var e in events)
        {
            SupportLogger.Log("exploration", $"civilization={e.CivilizationId} fleet={e.FleetId} system={e.SystemId} type={e.Type} message={e.Message}");
            if (e.CivilizationId != _galaxy.PlayerCivilizationId) continue;
            SetStatus(e.Message, e.Type == ExplorationEventType.FirstContact ? 9.0 : 4.0);
            RouteExplorationVoice(e);
            PublishPlayerNotification("Exploration", e.Message);
        }
    }

    private void HandleColonizationEvents(IReadOnlyList<ColonizationEvent> events)
    {
        foreach (var e in events)
        {
            SupportLogger.Log("colonization", $"civilization={e.CivilizationId} fleet={e.FleetId} system={e.SystemId} colony={e.ColonyId} message={e.Message}");
            if (e.CivilizationId == _galaxy.PlayerCivilizationId)
            {
                SetStatus(e.Message, 8.0);
                PublishPlayerNotification("Colony", e.Message);
                RouteColonizationVoice(e);
            }
        }
    }

    private void SelectNearestCatalogSystem(Godot.Vector2 mousePosition)
    {
        if (TrySelectFleetAt(mousePosition)) return;
        _selectedSystemId = FindNearestCatalogSystem(mousePosition, 14)?.Id ?? -1;
        UiClearFleetSelection();
    }

    private StarSystemState? FindNearestCatalogSystem(Godot.Vector2 mousePosition, float threshold)
    {
        var center = GetViewportRect().Size * 0.5f + _pan;
        var nearest = _galaxy.Systems.Select(system => new { System = system, Distance = mousePosition.DistanceTo(ToScreen(system.Position, center)) }).OrderBy(x => x.Distance).FirstOrDefault();
        if (nearest is null) return null;
        // Catalogue stars can be inspected closely; pointer selection follows the drawn disc
        // instead of retaining the former fixed 14/18 pixel target as the star grows.
        var hitRadius = Math.Max(threshold, UiCatalogStarRadius(nearest.System.Id) * 1.22f + 3.0f);
        return nearest.Distance <= hitRadius ? nearest.System : null;
    }

    private Godot.Vector2 ToScreen(System.Numerics.Vector2 position, Godot.Vector2 center) =>
        _regionalCameraReady && ReferenceEquals(_regionalCameraCampaign, _galaxy)
            ? new(_regionalCamera.ProjectX((double)position.X * UiCatalogVisualCoordinateScale),
                _regionalCamera.ProjectY((double)position.Y * UiCatalogVisualCoordinateScale))
            : center + new Godot.Vector2(position.X, position.Y) * (_zoom * UiCatalogVisualCoordinateScale);

    private void GenerateNewGalaxy()
    {
        var seed = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        _galaxy = new GalaxyGenerator().Generate(seed);
        _clock.Restore(0.0);
        _selectedSystemId = -1;
        _researchCandidateIndex = 0;
        _constructionCandidateIndex = 0;
        _pan = Godot.Vector2.Zero;
        _zoom = 0.55f;
        SupportLogger.Log("startup", $"Generated 2050 campaign seed={seed} systems={_galaxy.Systems.Count} prewarp={_galaxy.Civilizations.Count(c => c.DevelopmentStage == CivilizationDevelopmentStage.PreWarp)} ancient={_galaxy.Civilizations.Count(c => c.IsSeededAncient)} player={PlayerCivilization.Name}");
    }

    private void TryAutosave()
    {
        try
        {
            _saveService.Save(AutosavePath, _galaxy, _clock.SimulationDays);
            SupportLogger.Log("save", $"Autosaved seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(_clock.SimulationDays)}");
            SetStatus("Autosave complete.");
        }
        catch (Exception ex)
        {
            SupportLogger.Log("save-error", ex.ToString());
            SetStatus("Autosave failed. See logs.", 8.0);
        }
    }

    private void SetStatus(string text, double seconds = 4.0) { _statusText = text; _statusTimer = seconds; }
    private static string YesNo(bool value) => value ? "yes" : "no";
    private static Color GetStarColor(StarArchetype archetype) => archetype switch
    {
        StarArchetype.ResourceRich => new Color(0.93f, 0.74f, 0.31f), StarArchetype.HabitableRich => new Color(0.38f, 0.87f, 0.55f),
        StarArchetype.BarrenFrontier => new Color(0.62f, 0.60f, 0.58f), StarArchetype.Nebula => new Color(0.67f, 0.43f, 0.91f),
        StarArchetype.NeutronPulsar => new Color(0.48f, 0.76f, 1.0f), StarArchetype.BlackHole => new Color(0.78f, 0.30f, 0.34f),
        StarArchetype.AncientRuin => new Color(0.95f, 0.55f, 0.26f), StarArchetype.Dangerous => new Color(0.95f, 0.27f, 0.27f),
        StarArchetype.Legendary => new Color(0.98f, 0.91f, 0.42f), _ => new Color(0.82f, 0.86f, 0.95f),
    };

    private static Color GetStarColor(StellarPrimaryClass? stellarClass, StarArchetype fallback) => stellarClass.HasValue
        ? GetSpectralStarColor(stellarClass)
        : GetStarColor(fallback);

    private static Color GetSpectralStarColor(StellarPrimaryClass? stellarClass) => stellarClass switch
    {
        StellarPrimaryClass.MRedDwarf => new Color("ef705a"),
        StellarPrimaryClass.KOrangeDwarf => new Color("ff9e55"),
        StellarPrimaryClass.GYellowDwarf => new Color("ffd879"),
        StellarPrimaryClass.FYellowWhiteDwarf => new Color("fff1c7"),
        StellarPrimaryClass.AWhiteStar => new Color("e8f3ff"),
        StellarPrimaryClass.HotBlueStar => new Color("88bfff"),
        StellarPrimaryClass.Giant => new Color("ff765c"),
        StellarPrimaryClass.WhiteDwarf => new Color("d9edff"),
        StellarPrimaryClass.NeutronStar => new Color("79cfff"),
        StellarPrimaryClass.Pulsar => new Color("67dcff"),
        StellarPrimaryClass.BlackHole => new Color("9b87d9"),
        StellarPrimaryClass.Protostar => new Color("ffb065"),
        _ => new Color(0.82f, 0.86f, 0.95f),
    };
}
