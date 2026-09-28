using System;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Territory;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    // Focus protocol: run STELLAR_CAPTURE_FOCUS=territory in an isolated Developer profile.
    // This is intentionally a visual/input receipt; performance sampling remains in the
    // established performance focus so it can use the renderer's normal benchmark fixture.
    private async Task VerifyTerritorialInfluenceAsync(MainMenuLayer menu)
    {
        const string seedText = "territorial-influence-capture";
        var metadata = GalaxyGenerationMetadata.FullGalaxy500(seedText, CampaignSeed.Parse(seedText), systemCount: 500);
        var bootstrap = await _main.UiPrepareNewCampaignAsync(metadata, _ => { });
        Require(_main.UiCommitPreparedNewCampaign(bootstrap, seedText), "territory fixture did not commit a 500-system campaign");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        await WaitForRefreshAsync();
        var galaxy = (GalaxyState)typeof(Main).GetField("_galaxy", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(_main)!;
        Require(galaxy.Systems.Count == 500, "territory fixture must use the supported 500-system galaxy");
        var player = galaxy.PlayerCivilizationId;
        var freshFogPerformance = System.Environment.GetEnvironmentVariable("STELLAR_TERRITORY_PERFORMANCE_FRESH") == "1";
        if (!freshFogPerformance)
            foreach (var system in galaxy.Systems) galaxy.Knowledge.MarkSystemFullySurveyed(player, system.Id);
        galaxy.Economies.Single(economy => economy.CivilizationId == player).Credits = 5_000;
        galaxy.Economies.Single(economy => economy.CivilizationId == player).Industry = 5_000;
        var home = galaxy.Civilizations.Single(civilization => civilization.Id == player).HomeSystemId;
        var contestedSystem = galaxy.Systems.Where(system => system.Id != home && !galaxy.Colonies.Any(colony => colony.SystemId == system.Id))
            .OrderBy(system => System.Numerics.Vector2.DistanceSquared(system.Position, galaxy.Systems.Single(item => item.Id == home).Position)).First().Id;
        var nearby = galaxy.Systems.Where(system => system.Id != home && system.Id != contestedSystem)
            .OrderBy(system => System.Numerics.Vector2.DistanceSquared(system.Position, galaxy.Systems.Single(item => item.Id == home).Position)).First().Id;
        var lanes = new InterstellarLaneNetwork().Build(galaxy.Systems);
        int[] ConnectedHoldings(int origin, int count, System.Collections.Generic.IReadOnlySet<int> excluded)
        {
            var positions = galaxy.Systems.ToDictionary(system => system.Id, system => system.Position);
            var claimed = galaxy.Colonies.GroupBy(colony => colony.SystemId)
                .ToDictionary(group => group.Key, group => group.Select(colony => colony.CivilizationId).ToHashSet());
            var selected = new System.Collections.Generic.List<int> { origin };
            var visited = new System.Collections.Generic.HashSet<int> { origin };
            var frontier = new System.Collections.Generic.Queue<int>();
            frontier.Enqueue(origin);
            while (frontier.Count > 0 && selected.Count < count)
            {
                var current = frontier.Dequeue();
                foreach (var next in lanes.Where(lane => lane.Connects(current)).Select(lane => lane.Other(current))
                             .Where(id => !visited.Contains(id) && !excluded.Contains(id))
                             .OrderBy(id => System.Numerics.Vector2.DistanceSquared(positions[origin], positions[id])))
                {
                    visited.Add(next);
                    if (claimed.TryGetValue(next, out var owners) && owners.Any(owner => owner != galaxy.PlayerCivilizationId))
                        continue;
                    selected.Add(next);
                    frontier.Enqueue(next);
                    if (selected.Count == count) break;
                }
            }
            Require(selected.Count == count, $"territory fixture could not find {count} connected holdings from {origin}");
            return selected.ToArray();
        }
        var playerTechnology = galaxy.Technologies.Single(technology => technology.CivilizationId == player);
        playerTechnology.CompletedTechnologyIds.Add("orbital_industry");
        galaxy.Fleets.Add(new FleetState
        {
            Id = galaxy.Fleets.Select(fleet => fleet.Id).DefaultIfEmpty(0).Max() + 1, CivilizationId = player,
            Name = "Territory Logistics", Role = FleetRole.Logistics, Position = galaxy.Systems.Single(system => system.Id == home).Position,
            CurrentSystemId = home, TransitPhase = FleetTransitPhase.None, CargoMaterials = 0,
        });
        galaxy.Territory ??= new TerritorialState();
        var sites = galaxy.Territory.Installations;
        var nextId = sites.Select(site => site.Id).DefaultIfEmpty(0).Max() + 1;
        sites.Add(new TerritorialInstallation { Id = nextId++, CivilizationId = player, SystemId = home, Kind = TerritorialInstallationKind.Relay, RequiredDays = 15, CompletedDays = 15, PaidCredits = 45, PaidIndustry = 90 });
        sites.Add(new TerritorialInstallation { Id = nextId++, CivilizationId = player, SystemId = home, Kind = TerritorialInstallationKind.SupplyDepot, RequiredDays = 25, CompletedDays = 25, PaidCredits = 80, PaidIndustry = 160 });
        sites.Add(new TerritorialInstallation { Id = nextId++, CivilizationId = player, SystemId = nearby, Kind = TerritorialInstallationKind.ResearchStation, RequiredDays = 28, CompletedDays = 8, PaidCredits = 90, PaidIndustry = 180, BuilderFleetId = -1 });
        var rival = galaxy.Civilizations.First(civilization => civilization.Id != player);
        var playerHoldingSystems = ConnectedHoldings(home, 6, new System.Collections.Generic.HashSet<int> { contestedSystem, rival.HomeSystemId });
        var nextColonyId = galaxy.Colonies.Select(colony => colony.Id).DefaultIfEmpty(0).Max() + 1;
        foreach (var systemId in playerHoldingSystems.Skip(1))
            galaxy.Colonies.Add(new ColonyState
            {
                Id = nextColonyId++, CivilizationId = player, SystemId = systemId,
                Name = $"Player lane holding {systemId}", Kind = SettlementKind.Colony,
                PopulationSpeciesId = galaxy.Civilizations.Single(civilization => civilization.Id == player).SpeciesId,
                PopulationMillions = 850, Infrastructure = 2, Stability = 1, SurfaceHubLevel = 2,
            });
        var rivalHoldingSystems = ConnectedHoldings(rival.HomeSystemId, 5,
            playerHoldingSystems.Append(contestedSystem).ToHashSet());
        foreach (var systemId in rivalHoldingSystems.Skip(1))
            galaxy.Colonies.Add(new ColonyState
            {
                Id = nextColonyId++, CivilizationId = rival.Id, SystemId = systemId,
                Name = $"Rival lane holding {systemId}", Kind = SettlementKind.Colony,
                PopulationSpeciesId = rival.SpeciesId, PopulationMillions = 800,
                Infrastructure = 2, Stability = 1, SurfaceHubLevel = 2,
            });
        galaxy.Knowledge.RevealCivilization(player, rival.Id);
        galaxy.Knowledge.MarkSystemFullySurveyed(player, rival.HomeSystemId);
        galaxy.Knowledge.MarkSystemFullySurveyed(player, contestedSystem);
        galaxy.Colonies.Add(new ColonyState
        {
            Id = nextColonyId++,
            CivilizationId = rival.Id, SystemId = contestedSystem, Name = "Rival frontier station",
            Kind = SettlementKind.Colony, PopulationSpeciesId = rival.SpeciesId, PopulationMillions = 600,
            Infrastructure = 2, Stability = 1, SurfaceHubLevel = 2,
        });
        TerritorialRuntime.Initialize(galaxy).Recompute(galaxy);
        Require(galaxy.Colonies.Where(colony => colony.CivilizationId == player)
                .Select(colony => colony.SystemId).Distinct().Count() >= 6,
            "territory fixture did not create six legitimate player-controlled systems");

        await ResizeResponsiveWindowAsync(new Vector2I(2560, 1440));
        await WaitForCameraAsync();
        await ClickPositionAsync(StarPoint(home), MouseButton.Left);
        if (!_main.UiTerritoryMapVisible) await ClickNamedButtonAsync(_sidebar, "MapTerritory");
        Require(_main.UiTerritoryMapVisible, "territory overlay did not enable from its compact map control");
        await SaveViewportAsync("territory-01-map-1440.png", 2560, 1440);
        _main.UiShowGalaxyOverview();
        await WaitForCameraAsync();
        await SaveViewportAsync("territory-01a-connected-overview-1440.png", 2560, 1440);
        _main.UiSelectHomeSystem();
        await WaitForCameraAsync();
        var mapCenter = GetViewport().GetVisibleRect().Size * .5f;
        await WheelAsync(true, mapCenter);
        await DragAsync(mapCenter, mapCenter + new Vector2(320, 120), MouseButton.Left);
        await WaitForCameraAsync();
        await SaveViewportAsync("territory-01b-connected-pan-1440.png", 2560, 1440);
        await WheelAsync(true, mapCenter);
        await WheelAsync(true, mapCenter);
        await DragAsync(mapCenter, mapCenter + new Vector2(900, 0), MouseButton.Left);
        await WaitForCameraAsync();
        await SaveViewportAsync("territory-01c-boundary-cull-pan-1440.png", 2560, 1440);
        _main.UiSelectHomeSystem();
        await WaitForCameraAsync();
        await ClickNamedButtonAsync(_sidebar, "NavInspect");
        await WaitForRefreshAsync();
        var panel = ActivePanel();
        Require(Descendants(panel).OfType<Control>().Any(control => control.Name == "RegionalInfrastructure"),
            "selected-system inspection did not render regional infrastructure");
        var targetKindIndex = Array.FindIndex(_main.UiTerritorialInstallationOptions, option => option.Available);
        Require(targetKindIndex >= 0, "territory fixture did not offer a constructible regional site");
        var selectedAvailableSite = false;
        for (var attempt = 0; attempt < 3 && !selectedAvailableSite; attempt++)
        {
            var selector = Descendants(ActivePanel()).OfType<OptionButton>()
                .Single(option => option.Name == "TerritorialInstallationSelector");
            await ClickControlAsync(selector);
            await WaitFramesAsync(3);
            await PressKeyAsync(Key.Home);
            for (var index = 0; index < targetKindIndex; index++) await PressKeyAsync(Key.Down);
            await PressKeyAsync(Key.Enter);
            await WaitFramesAsync(3);
            // Some native popup hosts forward Enter to the newly focused Start action. That
            // changes the site structure and correctly replaces this selector instance.
            var currentPanel = ActivePanel();
            if (Descendants(currentPanel).OfType<Button>().Any(button => button.Text == "Cancel construction"))
                selectedAvailableSite = true;
            else
            {
                var currentSelector = Descendants(currentPanel).OfType<OptionButton>()
                    .Single(option => option.Name == "TerritorialInstallationSelector");
                selectedAvailableSite = currentSelector.Selected == targetKindIndex;
            }
        }
        Require(selectedAvailableSite, "real keyboard input did not select an available regional site");
        await WaitForRefreshAsync();
        // Keyboard selection can immediately activate the newly focused action on some
        // native hosts. In either case, prove the same real start/cancel interaction path.
        panel = ActivePanel();
        var cancel = Descendants(panel).OfType<Button>()
            .FirstOrDefault(button => string.Equals(button.Text, "Cancel construction", StringComparison.Ordinal));
        if (cancel is null)
        {
            var start = Descendants(panel).OfType<Button>()
                .First(button => string.Equals(button.Text, "Start project", StringComparison.Ordinal));
            await RevealControlAsync(start);
            await ClickControlAsync(start);
            await WaitForRefreshAsync();
            cancel = Descendants(ActivePanel()).OfType<Button>()
                .First(button => string.Equals(button.Text, "Cancel construction", StringComparison.Ordinal));
        }
        await RevealControlAsync(cancel);
        await ClickControlAsync(cancel);
        await WaitForRefreshAsync();
        Check(_main.UiSelectedTerritorialSites.Count(site => site.Complete) >= 2,
            "territory-mixed-phase-sites-render-and-cancel-through-real-input");
        await SaveViewportAsync("territory-02-inspection-1440.png", 2560, 1440);
        await ClickNamedButtonAsync(_main, "DrawerClose");
        await ClickPositionAsync(StarPoint(contestedSystem), MouseButton.Left);
        await ClickNamedButtonAsync(_sidebar, "NavInspect");
        await WaitForRefreshAsync();
        Require(_main.UiSelectedSystemIntelligence.Facts.Any(fact => fact.Label == "KNOWN INFLUENCE SHARES"),
            "known rival competition was not exposed through the observer-safe territory view");
        await SaveViewportAsync("territory-02b-competition-1440.png", 2560, 1440);
        await ClickNamedButtonAsync(_main, "DrawerClose");
        await ClickPositionAsync(StarPoint(home), MouseButton.Left);
        await ClickNamedButtonAsync(_sidebar, "NavInspect");
        await WaitForRefreshAsync();

        await ResizeResponsiveWindowAsync(new Vector2I(1280, 720));
        await WaitForRefreshAsync();
        // These actions intentionally exceed the short drawer. They are not overflow: each
        // must be brought through the real panel scroll chain before its bounds are checked.
        var reachable = Descendants(ActivePanel()).OfType<Control>()
            .Where(control => control.IsVisibleInTree() && control is Button or OptionButton).ToArray();
        foreach (var control in reachable)
        {
            await RevealControlAsync(control);
            AssertInsideViewport(control, "territory 720p reachable " + control.Name);
        }
        Check(reachable.Length >= 4, "territory-720p-scroll-reaches-every-site-action");
        await SaveViewportAsync("territory-03-inspection-720.png", 1280, 720);
        // Run the live-clock benchmark only after every screenshot and real-input proof has
        // been recorded. A performance failure must not discard coverage for the UI itself.
        if (System.Environment.GetEnvironmentVariable("STELLAR_TERRITORY_PERFORMANCE") == "1")
        {
            await ClickNamedButtonAsync(_main, "DrawerClose");
            await SampleTerritoryPerformanceAsync(galaxy, home);
        }
        GD.Print($"STELLAR_TERRITORY_FOCUS_PROTOCOL systems=500 runtimeRecomputeCount={TerritorialRuntime.Peek(galaxy)?.RecomputeCount} " +
            "captures=map1440,inspection1440,inspection720 pending=run-native-territory-focus");
    }

    private async Task SampleTerritoryPerformanceAsync(GalaxyState galaxy, int home)
    {
        await ResizeTerritoryPerformanceCaptureAsync();
        var seconds = double.TryParse(System.Environment.GetEnvironmentVariable("STELLAR_TERRITORY_PERFORMANCE_SECONDS"), out var configured)
            ? Math.Clamp(configured, .5, 5) : 5;
        async Task Sample(string view, bool overlay, bool running)
        {
            if (_main.UiTerritoryMapVisible != overlay) _main.UiToggleTerritoryMap();
            _main.UiSetPaused(!running, announce: false);
            await WaitFramesAsync(30);
            var runtime = TerritorialRuntime.Peek(galaxy);
            var recomputes = runtime?.RecomputeCount ?? 0;
            var watch = Stopwatch.StartNew(); var frames = 0;
            while (watch.Elapsed.TotalSeconds < seconds)
            {
                await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                frames++;
            }
            var fps = frames / watch.Elapsed.TotalSeconds;
            GD.Print($"STELLAR_TERRITORY_FPS view={view} overlay={overlay} running={running} seconds={watch.Elapsed.TotalSeconds:F2} fps={fps:F1} recomputeCount={TerritorialRuntime.Peek(galaxy)?.RecomputeCount} recomputeDelta={(TerritorialRuntime.Peek(galaxy)?.RecomputeCount ?? 0) - recomputes}");
        }
        _main.UiShowGalaxyOverview(); await WaitForCameraAsync();
        await Sample("overview", false, true); await Sample("overview", true, true);
        _main.UiSelectHomeSystem(); await WaitForCameraAsync();
        await Sample("regional", false, true); await Sample("regional", true, true);
        _main.UiOpenSelectedSystem(); await WaitForCameraAsync();
        await Sample("system", false, true); await Sample("system", true, true);
        if (System.Environment.GetEnvironmentVariable("STELLAR_TERRITORY_PERFORMANCE_PAUSED") == "1")
        {
            await Sample("system-paused", false, false);
            await Sample("system-paused", true, false);
        }
        _main.UiNavigateBack(); await WaitForCameraAsync();
        _main.UiSelectHomeSystem();
        if (!_main.UiTerritoryMapVisible) _main.UiToggleTerritoryMap();
    }

    private async Task ResizeTerritoryPerformanceCaptureAsync()
    {
        var target = new Vector2I(2560, 1440);
        var requested = target;
        var observed = Vector2I.Zero;
        // The native swapchain can lag a responsive logical-size update. Let both the
        // window and delayed video application settle, then adjust the client request from
        // the observed ratio if a DPI host still reports a different texture.
        for (var attempt = 1; attempt <= 3; attempt++)
        {
            await ResizeResponsiveWindowAsync(requested);
            await WaitFramesAsync(120);
            using var image = GetViewport().GetTexture().GetImage();
            observed = image?.GetSize() ?? Vector2I.Zero;
            GD.Print($"STELLAR_TERRITORY_RENDER_TARGET attempt={attempt} requested={requested} actual={observed}");
            if (observed == target) return;
            if (observed.X <= 0 || observed.Y <= 0) continue;
            requested = new Vector2I(
                Math.Clamp((int)Math.Round(requested.X * target.X / (double)observed.X), 1280, 3840),
                Math.Clamp((int)Math.Round(requested.Y * target.Y / (double)observed.Y), 720, 2160));
        }
        throw new InvalidOperationException($"Territory performance requires native {target}; observed {observed} after responsive video settling.");
    }
}
