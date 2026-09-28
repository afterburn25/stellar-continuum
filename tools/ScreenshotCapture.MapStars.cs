using System;
using System.Linq;
using System.Diagnostics;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    // Bounded visual receipt: it uses the same real controls as immersive evidence, but
    // deliberately stops before surface descent so the runner returns a numeric exit code.
    private async Task VerifyMapStarVisualsAsync(bool observeFullSolarCycle = true)
    {
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        await ClickNamedButtonAsync(menu, "NewDeveloperCampaign");
        await ClickControlAsync(Descendants(menu).OfType<ConfirmationDialog>().Single().GetOkButton());
        await WaitForCampaignLoadingAsync();
        Require(_main.UiIsDeveloperMode, "map-star gate fixture did not start its disposable Developer campaign");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialOverview"));
        await WaitForCameraAsync();
        Require(_main.UiOverviewBlend > .95f, "map-star capture did not reach the galaxy overview");
        await SaveViewportAsync("map-stars-01-galaxy.png", 0, 0);
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        Require(_main.UiIsSystemSpatialView && _main.UiSystemMeshBodyCount > 8,
            "map-star capture did not enter the restored 2D orbital system");
        await SaveViewportAsync("map-stars-02-system.png", 0, 0);
        var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "system-scale")
        {
            await VerifyFreeSystemZoomAsync(canvas);
            return;
        }
        await ClickPositionAsync(canvas.GetStarScreenPosition()!.Value, MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Require(canvas.IsStarFocused, "double-clicking Sol did not enter native stellar focus");
        RequireSinglePrimaryStar(canvas, "Sol");
        RequirePrimaryPixel(canvas, color => color.R > color.G && color.G > color.B,
            "Sol primary did not retain its orange-gold spectral ordering");
        var defaultDistance = canvas.Scene.TargetDistance;
        await SaveViewportAsync("map-stars-02a-sol-close.png", 0, 0);
        await WheelAsync(true, new Vector2(620, 390));
        Require(canvas.IsStarFocused && canvas.Scene.TargetDistance < defaultDistance,
            "stellar focus did not allow a closer radius-bounded view");
        // Shader TIME keeps advancing while the simulation clock is paused. Sample a
        // bounded wall-clock interval long enough to include a guaranteed real quiet,
        // eruption, and fade cycle; keep every frame for visual review rather than
        // inferring a flare from hashes while the photosphere itself is also moving.
        var flareObservation = Stopwatch.StartNew();
        if (observeFullSolarCycle)
        {
            for (var sample = 0; sample < 18; sample++)
            {
                await ToSignal(GetTree().CreateTimer(2.0), SceneTreeTimer.SignalName.Timeout);
                await SaveViewportAsync($"map-stars-sol-flare-{sample:00}.png", 0, 0);
                GD.Print($"STELLAR_SOL_FLARE_SAMPLE index={sample:00} elapsed={flareObservation.Elapsed.TotalSeconds:0.00}s");
            }
            Require(flareObservation.Elapsed.TotalSeconds >= 35,
                "stellar eruption observation did not span its real-time quiet/event/fade window");
        }
        else await WaitFramesAsync(12);
        await SaveViewportAsync("map-stars-02b-sol-surface-motion.png", 0, 0);
        await WheelAsync(false, new Vector2(620, 390));
        Require(canvas.IsStarFocused && canvas.Scene.TargetDistance >= defaultDistance - .1f,
            "the first outward stellar zoom step exited instead of retaining close context");
        for (var step = 0; step < 4 && canvas.IsStarFocused; step++)
            await WheelAsync(false, new Vector2(620, 390));
        await WaitForCameraAsync();
        Require(!canvas.IsDetailedFocus && _main.UiIsSystemSpatialView,
            "continued outward stellar zoom did not smoothly restore Sol's orbital system");
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Require(_main.UiFocusedPlanetBodyId == 3, "map-star capture did not enter Earth orbital focus");
        await SaveViewportAsync("map-stars-03-orbital-close.png", 0, 0);
        await ClickButtonAsync(_dock, "Home");
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        var initialLanes = canvas.GetLocalLanes!.Invoke();
        Require(canvas.SystemBoundaryScreenRadius > 0f && initialLanes.All(lane =>
                canvas.GetLaneMarkerBoundaryClearance(lane.DestinationSystemId) is > 1f),
            "outer-system delimiter did not clear every visible triangular lane gate and label");
        Require(initialLanes.All(lane => canvas.GetLaneMarkerBodySize(lane.DestinationSystemId) is { } size &&
                size.X is >= 39.9f and <= 40.1f && size.Y is >= 33.9f and <= 34.1f),
            "lane gates did not retain the compact 40 by 34 constant-screen-size silhouette");
        var laneBounds = initialLanes.Select(lane =>
            (lane.DestinationSystemId, Bounds: canvas.GetLaneMarkerBounds(lane.DestinationSystemId))).ToArray();
        Require(laneBounds.All(item => item.Bounds.HasValue) &&
                laneBounds.SelectMany((first, index) => laneBounds.Skip(index + 1)
                    .Select(second => (first, second)))
                    .All(pair => !pair.first.Bounds!.Value.Intersects(pair.second.Bounds!.Value, true)),
            "outward lane-label staggering left overlapping triangle or full-label bounds");
        var visibleEvidenceArea = new Rect2(120f, 175f, GetViewport().GetVisibleRect().Size.X - 450f,
            GetViewport().GetVisibleRect().Size.Y - 235f);
        var unknown = initialLanes.Where(lane => !lane.IsKnown &&
                canvas.GetLaneMarkerBounds(lane.DestinationSystemId) is { } bounds &&
                Encloses(visibleEvidenceArea, bounds))
            .OrderByDescending(lane => initialLanes.Where(other => other.DestinationSystemId != lane.DestinationSystemId)
                .Min(other => canvas.GetLaneScreenPosition(other.DestinationSystemId)!.Value.DistanceTo(
                    canvas.GetLaneScreenPosition(lane.DestinationSystemId)!.Value)))
            .First();
        var unknownGate = canvas.GetLaneScreenPosition(unknown.DestinationSystemId);
        Require(unknownGate.HasValue, "unknown local lane did not expose its visible marker body");
        var unknownPoint = unknownGate.GetValueOrDefault();
        var beforeSystem = _main.UiSelectedSystemId;
        var beforeSurvey = _main.UiSpatialCatalog.Single(item => item.SystemId == unknown.DestinationSystemId).SurveyLevel;
        var beforeCamera = (canvas.Camera.Scale, canvas.Camera.OriginX, canvas.Camera.OriginY,
            canvas.Camera.TargetScale, canvas.Camera.TargetOriginX, canvas.Camera.TargetOriginY);
        var unknownNative = GetViewport().GetFinalTransform() * unknownPoint;
        HoldVisiblePointer(unknownNative);
        InjectPointerEvent(new InputEventMouseMotion { Position = unknownNative, GlobalPosition = unknownNative });
        FlushPointerEvents(); await WaitFramesAsync(2);
        Require(canvas.HoveredLaneDestinationId == unknown.DestinationSystemId,
            "unknown lane marker did not accept real hover input");
        Require(canvas.TooltipText == "????", "unknown lane hover leaked its undiscovered catalog name");
        RequireLaneBodyIsOrange(canvas, unknown.DestinationSystemId, "unknown");
        GD.Print($"STELLAR_LANE_EVIDENCE unknown={unknown.DestinationSystemId} point={unknownPoint} bounds={canvas.GetLaneMarkerBounds(unknown.DestinationSystemId)}");
        await EnsureMapStarCaptureWindowAsync();
        await SaveViewportAsync("map-stars-04-unknown-hover.png", 0, 0);
        HoldVisiblePointer(null);
        var requireScientistAudio = System.Environment.GetEnvironmentVariable("STELLAR_REQUIRE_KOKORO") == "1";
        var advisoryVoice = _main.UiVoice!;
        var advisoryPlayedBefore = advisoryVoice.PlayedLines;
        if (requireScientistAudio) advisoryVoice.Stop();
        await ClickPositionAsync(unknownPoint, MouseButton.Left);
        var afterCamera = (canvas.Camera.Scale, canvas.Camera.OriginX, canvas.Camera.OriginY,
            canvas.Camera.TargetScale, canvas.Camera.TargetOriginX, canvas.Camera.TargetOriginY);
        Require(_main.UiSelectedSystemId == beforeSystem && beforeCamera == afterCamera &&
            _main.UiSpatialCatalog.Single(item => item.SystemId == unknown.DestinationSystemId).SurveyLevel == beforeSurvey &&
            _main.UiStatusMessage == "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.",
            "unknown gate changed selection, camera, survey state, or exact reconnaissance guidance");
        if (requireScientistAudio)
        {
            await WaitUntilAsync(() => advisoryVoice.PlayedLines > advisoryPlayedBefore &&
                advisoryVoice.Diagnostics.Contains("human_female_chief_scientist", StringComparison.Ordinal) &&
                advisoryVoice.Diagnostics.Contains("bf_emma", StringComparison.Ordinal), 20,
                "The actual unknown-lane click did not play the British female scientist.");
            VerifyProcessedDialogueIsSingleDrySource(advisoryVoice);
            await CaptureVoiceBusAsync("scientist-lane-british-bus.wav");
            Check(true, "unknown-lane-click-plays-british-scientist");
        }
        await SaveViewportAsync("map-stars-05-unknown-advisory.png", 0, 0);
        if (requireScientistAudio)
        {
            advisoryVoice.Stop();
            await WaitFramesAsync(3);
        }

        var reveal = _main.UiRunDeveloperCommand("reveal_galaxy");
        Require(reveal.Accepted, "Developer reconnaissance fixture failed to establish actual neighbor knowledge");
        await WaitForRefreshAsync();
        var known = canvas.GetLocalLanes!.Invoke().Single(lane => lane.DestinationSystemId == unknown.DestinationSystemId);
        Require(known.IsKnown && known.Label != "????", "reconnaissance did not replace the unknown gate with its catalog name");
        var knownGate = canvas.GetLaneScreenPosition(known.DestinationSystemId);
        Require(knownGate.HasValue, "known local lane did not expose its visible marker body");
        var knownPoint = knownGate.GetValueOrDefault();
        var awayNative = GetViewport().GetFinalTransform() * new Vector2(180f, 650f);
        InjectPointerEvent(new InputEventMouseMotion { Position = awayNative, GlobalPosition = awayNative });
        FlushPointerEvents(); await WaitFramesAsync(2);
        var knownNative = GetViewport().GetFinalTransform() * knownPoint;
        HoldVisiblePointer(knownNative);
        InjectPointerEvent(new InputEventMouseMotion { Position = knownNative, GlobalPosition = knownNative });
        FlushPointerEvents(); await WaitFramesAsync(2);
        Require(canvas.HoveredLaneDestinationId == known.DestinationSystemId,
            "known lane marker did not accept real hover input");
        Require(canvas.TooltipText == known.Label, "known lane hover did not expose its full catalog name");
        RequireLaneBodyIsOrange(canvas, known.DestinationSystemId, "known");
        GD.Print($"STELLAR_LANE_EVIDENCE known={known.DestinationSystemId} label={known.Label} point={knownPoint} bounds={canvas.GetLaneMarkerBounds(known.DestinationSystemId)}");
        await EnsureMapStarCaptureWindowAsync();
        await SaveViewportAsync("map-stars-06-known-hover.png", 0, 0);
        HoldVisiblePointer(null);
        await ClickPositionAsync(knownPoint, MouseButton.Left);
        Require(_main.UiSelectedSystemId == known.DestinationSystemId && _main.UiIsSystemSpatialView &&
            canvas.SystemName == known.Label,
            "known adjacent gate did not open its actual connected orbital system");
        await SaveViewportAsync("map-stars-07-known-system.png", 0, 0);
        await CaptureKnownSpectralSystemAsync(
            label => label is "M-type red dwarf" or "Red/orange giant",
            "map-stars-08-red-star-close.png", "red");
        await CaptureKnownSpectralSystemAsync(
            label => label is "A-type white star" or "Hot blue B/O star" or "White dwarf",
            "map-stars-09-blue-white-star-close.png", "blue/white");
    }

    private async Task CaptureKnownSpectralSystemAsync(Func<string, bool> matches,
        string fileName, string evidenceLabel)
    {
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialOverview"));
        await WaitForCameraAsync();
        Require(_main.UiOverviewBlend > .95f, $"{evidenceLabel} spectral proof did not reach the galaxy overview");
        var safeMap = new Rect2(120f, 175f, GetViewport().GetVisibleRect().Size.X - 450f,
            GetViewport().GetVisibleRect().Size.Y - 235f);
        foreach (var entry in _main.UiSpatialCatalog)
        {
            var point = StarPoint(entry.SystemId);
            if (!safeMap.HasPoint(point)) continue;
            await ClickPositionAsync(point, MouseButton.Left);
            if (_main.UiSelectedSystemId != entry.SystemId) continue;
            var primary = _main.UiSelectedSystemIntelligence.Facts
                .FirstOrDefault(fact => fact.Label == "PRIMARY STAR")?.Value;
            if (primary is null || !matches(primary)) continue;
            await ClickButtonAsync(_dock, "Open System");
            await WaitForCameraAsync();
            var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
            var starPoint = canvas.GetStarScreenPosition();
            Require(starPoint.HasValue,
                $"{evidenceLabel} spectral system did not expose its stellar marker");
            await ClickPositionAsync(starPoint.GetValueOrDefault(), MouseButton.Left, doubleClick: true);
            await WaitForCameraAsync();
            Require(canvas.IsStarFocused, $"{evidenceLabel} spectral star did not enter close focus");
            RequireSinglePrimaryStar(canvas, evidenceLabel);
            RequirePrimaryPixel(canvas,
                evidenceLabel == "red" ? color => color.R > color.B : color => color.B >= color.R * .98f,
                $"{evidenceLabel} primary rendered with the wrong spectral channel ordering");
            await SaveViewportAsync(fileName, 0, 0);
            GD.Print($"STELLAR_SPECTRAL_EVIDENCE family={evidenceLabel} class={primary} system={entry.SystemId}");
            return;
        }
        throw new InvalidOperationException($"No visible surveyed {evidenceLabel} stellar system was available for close proof.");
    }

    private static void RequireSinglePrimaryStar(SystemSpatialCanvas canvas, string state)
    {
        Require(canvas.Scene.StarRootCount == 1 && canvas.Scene.PrimaryStarCount == 1 &&
                canvas.Scene.PrimaryCoronaCount == 1,
            $"{state} retained or omitted stellar renderer nodes: roots={canvas.Scene.StarRootCount}, " +
            $"primaries={canvas.Scene.PrimaryStarCount}, coronas={canvas.Scene.PrimaryCoronaCount}");
    }

    private void RequirePrimaryPixel(SystemSpatialCanvas canvas, Func<Color, bool> predicate, string message)
    {
        var point = canvas.GetStarScreenPosition();
        Require(point.HasValue, "focused stellar primary did not expose its screen center");
        using var image = GetViewport().GetTexture().GetImage();
        var sample = point.GetValueOrDefault();
        var color = image.GetPixel((int)sample.X, (int)sample.Y);
        Require(predicate(color), $"{message}: point={sample}, color={color}");
    }

    private void RequireLaneBodyIsOrange(SystemSpatialCanvas canvas, int destinationSystemId, string state)
    {
        var sample = canvas.GetLaneBodyColorSamplePosition(destinationSystemId);
        Require(sample.HasValue, $"{state} lane did not expose a visible body sample");
        var samplePoint = sample.GetValueOrDefault();
        using var image = GetViewport().GetTexture().GetImage();
        var color = image.GetPixel((int)samplePoint.X, (int)samplePoint.Y);
        Require(color.R > .8f && color.G is > .45f and < .75f && color.B < .35f,
            $"{state} lane hover body was not orange at {sample}: {color}");
    }

    private async Task EnsureMapStarCaptureWindowAsync()
    {
        if (GetWindow().Mode == Window.ModeEnum.Minimized)
            GetWindow().Mode = Window.ModeEnum.Windowed;
        await WaitFramesAsync(5);
        Require(GetWindow().Mode == Window.ModeEnum.Windowed, "map-star evidence window was minimized before capture");
    }
}
