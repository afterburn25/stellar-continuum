using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Audio.Voice;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyVoiceRuntimeAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        Input.UseAccumulatedInput = false;
        Require(GetViewport().GetVisibleRect().Size == new Vector2(1280, 720),
            "Voice acceptance must render at 1280x720.");
        var voice = _main.UiVoice ?? throw new InvalidOperationException("Main did not create UiVoice.");
        await WaitUntilAsync(() => voice.Profiles.Count >= 9 && !voice.BackendStatus.Contains("Initializing", StringComparison.Ordinal),
            12, "Offline voice engine did not initialize.");
        Check(voice.Profiles.Count >= 9 && voice.Profiles.Any(p => p.Id == "grey_diplomat"),
            "voice-registry-loaded-core-and-species-profiles");

        var openingBefore = voice.SubtitleLines;
        await VoiceClickNamedAsync(menu, "ResumeCampaign");
        await WaitUntilAsync(() => voice.SubtitleLines > openingBefore && voice.ActiveSubtitle.Length > 0,
            15, "Production opening narration did not reach UiVoice.");
        Check(voice.Diagnostics.Contains("narrator", StringComparison.OrdinalIgnoreCase),
            "production-opening-routed-to-narrator");
        Check(Descendants(voice).OfType<Control>().Single(c => c.Name == "VoiceSubtitle").IsVisibleInTree(),
            "opening-caption-visible-in-real-game");
        Check(voice.PlayedLines > 0 && voice.LastSource is "synthesized" or "cache" or "prerecorded",
            "opening-uses-real-audio-source");
        if (System.Environment.GetEnvironmentVariable("STELLAR_REQUIRE_KOKORO") == "1")
        {
            Check(voice.Diagnostics.Contains("bf_isabella", StringComparison.Ordinal),
                "opening-uses-cast-neural-narrator");
            Check(voice.Profiles.Where(p => p.Id.StartsWith("human_female_", StringComparison.Ordinal))
                    .Select(p => p.NeuralVoice).Distinct().Count() == 4,
                "four-female-roles-use-distinct-neural-voices");
        }
        await SaveViewportAsync("voice-01-opening-caption.png");

        // Capture post-DSP output from the live Voice bus while the actual opening line plays.
        await CaptureVoiceBusAsync("voice-processed-bus.wav");
        var replayBefore = voice.SubtitleLines;
        voice.Stop();
        voice.ReplayLast();
        await WaitUntilAsync(() => voice.SubtitleLines > replayBefore && voice.LastSource == "cache", 10,
            "Replay did not reuse the synthesized cache entry.");
        Check(voice.LastSource == "cache", "replay-uses-deterministic-cache");
        voice.Stop();

        _main.UiOpenMenu();
        await WaitFramesAsync(3);
        await ClickNamedButtonAsync(menu, "Settings");
        await VoiceClickNamedAsync(menu, "SettingsVoice");
        var settings = Descendants(voice).OfType<Control>().Single(c => c.Name == "VoiceSettings");
        var labOpen = Descendants(settings).OfType<Button>().Single(b => b.Name == "VoiceLabOpen");
        Check(settings.IsVisibleInTree() && !labOpen.Visible &&
              Descendants(settings).Any(n => n.Name == "VoiceEnabled") &&
              Descendants(settings).Any(n => n.Name == "VoiceSubtitles") &&
              Descendants(settings).Any(n => n.Name == "VoiceNoInterruptions"),
            "player-voice-settings-complete-and-lab-hidden");
        await SaveViewportAsync("voice-02-player-settings.png");
        await VoiceClickNamedAsync(settings, "VoiceSettingsClose");
        await VoiceClickNamedAsync(menu, "SettingsBack");
        await VoiceClickNamedAsync(menu, "ResumeCampaign");

        // A setting change can race a slow local SAPI request. Muting must cancel that
        // request, retain its accessibility caption and never begin late playback.
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true, Subtitles = true });
        var playedBeforeMute = voice.PlayedLines;
        var pendingMuteBefore = voice.SubtitleLines;
        voice.Speak(new SpeechRequest("human_operations_officer",
            "This intentionally extended synthesis line verifies that disabling speech while local generation is pending cannot begin late audio playback in the game.")
        { Category = "runtime-pending-mute", Priority = 70, DedupeKey = "runtime-pending-mute", CachePolicy = SpeechCachePolicy.Refresh });
        await WaitUntilAsync(() => voice.Diagnostics.Contains("Synthesizing", StringComparison.Ordinal), 3,
            "Pending synthesis state was not observable.");
        voice.ApplySettings(voice.Settings with { EnableVoices = false });
        await WaitUntilAsync(() => voice.SubtitleLines > pendingMuteBefore, 3,
            "Muting a pending synthesis did not preserve its caption.");
        await WaitFramesAsync(10);
        Check(voice.PlayedLines == playedBeforeMute && voice.LastSource == "subtitle",
            "pending-synthesis-mute-prevents-late-playback");

        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true, Subtitles = true });
        var failureBefore = voice.SubtitleLines;
        voice.Speak(new SpeechRequest("human_operations_officer", "Offline synthesis is unavailable for this diagnostic line.")
        { Category = "runtime-failure", Priority = 50, DedupeKey = "runtime-failure", AllowSynthesis = false });
        await WaitUntilAsync(() => voice.SubtitleLines > failureBefore, 4,
            "Engine failure result did not return to the render-thread controller.");
        Check(voice.LastSource == "subtitle" && voice.Diagnostics.Contains("synthesis disabled", StringComparison.OrdinalIgnoreCase),
            "backend-failure-callback-preserves-caption");
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = false, Subtitles = true, NoInterruptions = false });
        var mutedBefore = voice.SubtitleLines;
        voice.Speak(new SpeechRequest("human_female_chief_scientist",
            "Spectral analysis complete. The result is available in the research archive.")
        { Category = "runtime-muted", Priority = 60, DedupeKey = "runtime-muted" });
        await WaitUntilAsync(() => voice.SubtitleLines > mutedBefore, 4,
            "Muted speech did not preserve its caption.");
        Check(voice.LastSource == "subtitle" && voice.ActiveSubtitle.Contains("Spectral analysis", StringComparison.Ordinal),
            "muted-voice-keeps-caption");
        await SaveViewportAsync("voice-03-muted-caption.png");

        // No-interruption policy must leave the urgent line queued behind the active caption.
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = false, Subtitles = true, NoInterruptions = true });
        voice.Speak(new SpeechRequest("human_male_fleet_commander", "Fleet readiness report remains in progress for all task groups.")
        { Category = "runtime-policy-a", Priority = 20, DedupeKey = "runtime-policy-a", Interruptible = true });
        await WaitUntilAsync(() => voice.ActiveSubtitle.Contains("readiness report", StringComparison.Ordinal), 3,
            "No-interruption baseline line did not become active.");
        voice.Speak(new SpeechRequest("ship_computer", "Priority alert. Navigation hazard detected.")
        { Category = "runtime-policy-b", Priority = 100, DedupeKey = "runtime-policy-b", QueueBehavior = SpeechQueueBehavior.InterruptLowerPriority });
        await WaitFramesAsync(4);
        Check(voice.ActiveSubtitle.Contains("readiness report", StringComparison.Ordinal) && voice.PendingCount >= 1,
            "no-interruption-policy-preserves-active-line");
        voice.ResetCampaign();
        await WaitFramesAsync(2);
        Check(voice.PendingCount == 0 && voice.ActiveSubtitle.Length == 0,
            "campaign-reset-clears-voice-state");

        // Enter a Developer campaign through the real menu and its protected confirmation.
        _main.UiOpenMenu(); await WaitFramesAsync(3);
        await VoiceClickNamedAsync(menu, "OpenDevelopment");
        await VoiceClickNamedAsync(menu, "NewDeveloperCampaign");
        Require(dialog.Visible, "Developer campaign confirmation was not shown.");
        await VoiceClickAsync(dialog.GetOkButton());
        await WaitForCampaignLoadingAsync();
        await WaitForRefreshAsync();
        Require(_main.UiIsDeveloperMode && !_main.UiIsMenuOpen, "Developer campaign did not start.");
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true, Subtitles = true, NoInterruptions = false });

        // Start research through the production command surface and advance only through
        // the marked Developer command's ordinary simulation path. Completion is then
        // published by Main and routed by VoiceEventRouter to the scientist profile.
        _main.UiStartResearch("fusion_power");
        Require(_main.UiDashboard.Research.IsActive, "Production research order was not accepted.");
        var routedBefore = voice.SubtitleLines;
        for (var step = 0; step < 18 && _main.UiDashboard.Research.IsActive; step++)
        {
            var outcome = _main.UiRunDeveloperCommand("advance_30_days");
            Require(outcome.Accepted, "Developer time advance failed: " + outcome.Message);
            await WaitFramesAsync(2);
        }
        Require(!_main.UiDashboard.Research.IsActive, "Research did not complete through ordinary simulation advancement.");
        await WaitUntilAsync(() => voice.SubtitleLines > routedBefore &&
            voice.Diagnostics.Contains("scientist", StringComparison.OrdinalIgnoreCase), 15,
            "Research completion notification did not route to the scientist voice.");
        Check(voice.Diagnostics.Contains("scientist", StringComparison.OrdinalIgnoreCase),
            "production-research-completion-routed-to-scientist");
        await SaveViewportAsync("voice-04-science-notification.png");

        voice.Stop();
        // The marked prerequisite tool grants research and capabilities while leaving every
        // infrastructure order untouched. Both orbital projects then complete in simulation.
        _main.UiOpenDeveloperTools(); await WaitFramesAsync(3);
        var tools = _main.GetNode<DeveloperToolsLayer>("DeveloperToolsLayer");
        await VoiceClickNamedAsync(tools, "DeveloperCommand_unlock_research");
        await VoiceClickNamedAsync(tools, "DeveloperCommand_grant_resources");
        await WaitForRefreshAsync();
        await VoiceClickNamedAsync(tools, "DeveloperToolsClose");
        voice.Stop();
        _main.UiStartConstruction("orbital_launch_complex");
        Require(_main.UiDashboard.Construction.IsActive, "Normal Launch Complex order was not accepted.");
        for (var step = 0; step < 18 && _main.UiDashboard.Construction.IsActive; step++)
        {
            var outcome = _main.UiRunDeveloperCommand("advance_30_days");
            Require(outcome.Accepted, "Launch Complex time advance failed: " + outcome.Message);
            await WaitFramesAsync(2);
        }
        Require(!_main.UiDashboard.Construction.IsActive, "Launch Complex did not complete in ordinary simulation.");
        voice.Stop();
        _main.UiStartConstruction("orbital_shipyard");
        Require(_main.UiDashboard.Construction.IsActive, "Normal Orbital Shipyard order was not accepted.");
        Require(!_main.UiHasVoiceMilestone("shipyard"), "Shipyard voice fired at order start instead of completion.");
        for (var step = 0; step < 18 && _main.UiDashboard.Construction.IsActive; step++)
        {
            var outcome = _main.UiRunDeveloperCommand("advance_30_days");
            Require(outcome.Accepted, "Orbital Shipyard time advance failed: " + outcome.Message);
            await WaitFramesAsync(2);
        }
        Check(!_main.UiDashboard.Construction.IsActive && _main.UiHasVoiceMilestone("shipyard"),
            "real-orbital-shipyard-completion-routes-commander-event");
        await WaitUntilAsync(() => voice.IsSpeaking &&
            voice.EventDiagnostics.Contains("construction.orbital_shipyard.completed", StringComparison.Ordinal) &&
            voice.Diagnostics.Contains("human_female_fleet_commander", StringComparison.Ordinal), 15,
            "Completed orbital shipyard did not reach commander audio playback.");
        voice.Stop();
        var fleetBefore = _main.UiDashboard.FleetCount;
        _main.UiBuildShip("warp_scout");
        Require(_main.UiDashboard.Shipyard.IsActive, "Normal shipbuilding command did not begin a Pathfinder Scout.");
        for (var step = 0; step < 18 && _main.UiDashboard.FleetCount == fleetBefore; step++)
        {
            var outcome = _main.UiRunDeveloperCommand("advance_30_days");
            Require(outcome.Accepted, "Shipbuilding time advance failed: " + outcome.Message);
            await WaitFramesAsync(2);
        }
        Check(_main.UiDashboard.FleetCount == fleetBefore + 1 && _main.UiHasVoiceMilestone("ship_launch"),
            "real-ship-completion-routes-commander-event");

        voice.Stop();
        _main.UiSetPaused(true, announce: false);
        var scout = _main.UiOwnedFleets.Single(f => f.Role == FleetRole.Scout);
        _main.UiSelectHomeSystem();
        _main.UiSelectOwnedFleet(scout.FleetId, center: true);
        await WaitFramesAsync(3);
        var origin = _main.UiSelectedSystemId;
        var originPoint = StarPoint(origin);
        var destination = _main.UiSpatialCatalog.Where(s => s.SystemId != origin)
            .Select(s => new { s.SystemId, Point = StarPoint(s.SystemId) })
            .Where(s => new Rect2(90, 150, 850, 470).HasPoint(s.Point) &&
                        s.Point.DistanceTo(originPoint) > 50 &&
                        s.Point.DistanceTo(originPoint) < scout.MaximumLegRangeLightYears * .8)
            .OrderBy(s => s.Point.DistanceTo(originPoint)).First();
        await VoicePointerClickAsync(destination.Point, MouseButton.Right);
        Require(_main.UiOwnedFleets.Single(f => f.FleetId == scout.FleetId).RemainingRouteDistanceLightYears > 0,
            "Real right-click did not issue the scout route.");
        for (var step = 0; step < 18 && !_main.UiHasVoiceMilestone("arrival"); step++)
        {
            var outcome = _main.UiRunDeveloperCommand("advance_30_days");
            Require(outcome.Accepted, "Fleet travel time advance failed: " + outcome.Message);
            await WaitFramesAsync(2);
        }
        Check(_main.UiHasVoiceMilestone("departure") && _main.UiHasVoiceMilestone("arrival") &&
              _main.UiOwnedFleets.Single(f => f.FleetId == scout.FleetId).RemainingRouteDistanceLightYears <= .001,
            "real-fleet-route-emits-departure-and-arrival");

        voice.Stop();
        _main.UiOpenMenu(); await WaitFramesAsync(3);
        await ClickNamedButtonAsync(menu, "Settings");
        await VoiceClickNamedAsync(menu, "SettingsVoice");
        settings = Descendants(voice).OfType<Control>().Single(c => c.Name == "VoiceSettings");
        labOpen = Descendants(settings).OfType<Button>().Single(b => b.Name == "VoiceLabOpen");
        Check(labOpen.Visible, "developer-voice-lab-visible");
        await VoiceClickAsync(labOpen);
        var lab = Descendants(settings).OfType<Control>().Single(c => c.Name == "VoiceLab");
        Require(lab.IsVisibleInTree(), "Developer Voice Lab did not open.");
        var profile = Descendants(lab).OfType<OptionButton>().Single(b => b.Name == "VoiceLabProfile");
        var shipComputer = voice.Profiles.ToList().FindIndex(p => p.Id == "ship_computer");
        Require(shipComputer >= 0, "Ship computer profile is missing.");
        profile.Select(shipComputer);
        await VoiceClickNamedAsync(lab, "VoiceLabPlay");
        await WaitUntilAsync(() => voice.IsSpeaking && voice.Diagnostics.Contains("ship_computer", StringComparison.Ordinal), 15,
            "Developer Voice Lab did not synthesize its selected profile.");
        Check(voice.Diagnostics.Contains("ship_computer", StringComparison.OrdinalIgnoreCase),
            "developer-lab-selects-ship-computer-profile");
        await SaveViewportAsync("voice-05-developer-lab.png");
        voice.Stop();
        var grey = voice.Profiles.ToList().FindIndex(p => p.Id == "grey_diplomat");
        Require(grey >= 0, "Grey diplomat profile is missing.");
        profile.Select(grey);
        await VoiceClickNamedAsync(lab, "VoiceLabPlay");
        await WaitUntilAsync(() => voice.IsSpeaking && voice.Diagnostics.Contains("grey_diplomat", StringComparison.OrdinalIgnoreCase),
            15, "Grey profile did not reach live Godot playback.");
        VerifyProcessedDialogueIsSingleDrySource(voice);
        await CaptureVoiceBusAsync("voice-grey-processed-bus.wav");
        Check(voice.Diagnostics.Contains("grey_diplomat", StringComparison.OrdinalIgnoreCase),
            "grey-profile-live-processed-playback");
        await VerifyGameplayVoiceRolesAsync(voice, lab);
        voice.ResetCampaign();
        await WaitFramesAsync(2);
        Check(!settings.Visible && !lab.Visible && voice.PendingCount == 0,
            "campaign-reset-closes-lab-and-clears-pending");

        GD.Print($"STELLAR_VOICE_RUNTIME_EVIDENCE profiles={voice.Profiles.Count} played={voice.PlayedLines} " +
                 $"subtitles={voice.SubtitleLines} source={voice.LastSource} backend={voice.BackendStatus}");
    }

    private async Task VerifyGameplayVoiceRolesAsync(VoicePlaybackController voice, Control lab)
    {
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true, Subtitles = true, Frequency = VoiceFrequency.Normal, ChatterLevel = 1 });
        Check(Descendants(lab).Any(node => node.Name == "VoiceLabEventTrigger") &&
              Descendants(lab).Any(node => node.Name == "VoiceLabAppoint"), "developer-event-tester-and-office-assignment-present");
        // Hold a non-interruptible line so the event is queued before replacement. Resolving
        // at event emission would incorrectly leave the original scientist on this line.
        voice.Speak(new SpeechRequest("human_female_narrator", "Standing by for the next report. All departments remain ready for your orders.")
        { Priority = 100, Interruptible = false, Category = "voice-role-hold", DedupeKey = "voice-role-hold" });
        await WaitUntilAsync(() => voice.IsSpeaking, 15, "Role replacement holding line did not play.");
        Require(_main.UiTestVoiceEvent("research.completed"), "Developer event tester did not route research.");
        var assignedProfile = Descendants(lab).OfType<OptionButton>().Single(c => c.Name == "VoiceLabProfile");
        assignedProfile.Select(voice.Profiles.ToList().FindIndex(p => p.Id == "human_female_diplomat"));
        var office = Descendants(lab).OfType<OptionButton>().Single(c => c.Name == "VoiceLabOffice");
        office.Select(0); // Chief Scientist is the first editable office.
        Descendants(lab).OfType<LineEdit>().Single(c => c.Name == "VoiceLabCharacterName").Text = "Dr. Selene Vale";
        await VoiceClickNamedAsync(lab, "VoiceLabAppoint");
        await WaitUntilAsync(() => voice.ActiveSpeakerName.Contains("Dr. Selene Vale", StringComparison.Ordinal) && voice.IsSpeaking,
            30, "Queued research did not follow the new current scientist.");
        Check(voice.Diagnostics.Contains("human_female_diplomat", StringComparison.Ordinal) &&
              voice.EventDiagnostics.Contains("research.completed", StringComparison.Ordinal), "queued-event-resolves-successor-and-new-voice-at-playback");
        var labCaption = Descendants(lab).OfType<Label>().Single(c => c.Name == "VoiceLabSubtitle");
        await RevealControlAsync(labCaption);
        await SaveViewportAsync("voice-event-01-successor.png");
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = false });
        var before = voice.SubtitleLines;
        Require(_main.UiTestVoiceEvent("colony.founded"), "Colony sample was not routed.");
        await WaitUntilAsync(() => voice.SubtitleLines > before, 5, "Disabled event voice lost its subtitle.");
        Check(!voice.IsSpeaking && voice.LastSource == "subtitle" && voice.ActiveSubtitle.Contains("Mars", StringComparison.Ordinal),
            "typed-gameplay-event-retains-named-caption-with-audio-disabled");
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true });
        var eventPicker = Descendants(lab).OfType<OptionButton>().Single(c => c.Name == "VoiceLabEvent");
        for (var item = 0; item < eventPicker.ItemCount; item++)
            if (eventPicker.GetItemText(item) == "diplomacy.alien.transmission") eventPicker.Select(item);
        await VoiceClickNamedAsync(lab, "VoiceLabEventTrigger");
        await WaitUntilAsync(() => voice.IsSpeaking && voice.EventDiagnostics.Contains("pelagic", StringComparison.OrdinalIgnoreCase),
            20, "Pelagic transmission did not resolve a nonhuman species voice.");
        Check(!voice.Diagnostics.Contains("human_female", StringComparison.Ordinal) && voice.ActiveSubtitle.Contains("discussions", StringComparison.Ordinal),
            "alien-event-uses-species-translator-through-live-engine");
        await RevealControlAsync(labCaption);
        await SaveViewportAsync("voice-event-02-alien.png");
        await CaptureVoiceBusAsync("voice-event-alien-bus.wav");
        voice.Stop();
    }

    private async Task VoiceClickNamedAsync(Node root, string name) => await VoiceClickAsync(
        Descendants(root).OfType<Button>().Single(button => button.Name == name));

    private async Task VoiceClickAsync(Button button)
    {
        await RevealControlAsync(button);
        Require(button.IsVisibleInTree() && !button.Disabled, $"Voice target hidden or disabled: {button.Name}.");
        AssertInsideViewport(button, "voice " + button.Name);
        var point = GetViewport().GetFinalTransform() * ScreenRect(button).GetCenter();
        var pressed = false;
        void Activated() => pressed = true;
        button.Pressed += Activated;
        try
        {
            InjectPointerEvent(new InputEventMouseMotion { Position = point, GlobalPosition = point });
            InjectPointerEvent(new InputEventMouseButton { Position = point, GlobalPosition = point,
                ButtonIndex = MouseButton.Left, ButtonMask = MouseButtonMask.Left, Pressed = true });
            InjectPointerEvent(new InputEventMouseButton { Position = point, GlobalPosition = point,
                ButtonIndex = MouseButton.Left, ButtonMask = 0, Pressed = false });
            _mouseActions++;
            await WaitFramesAsync(3);
            Require(pressed, $"Voice mouse input did not activate {button.GetPath()} at {point}.");
        }
        finally { if (GodotObject.IsInstanceValid(button)) button.Pressed -= Activated; }
    }

    private async Task VoicePointerClickAsync(Vector2 logicalPoint, MouseButton button)
    {
        Require(GetViewport().GetVisibleRect().HasPoint(logicalPoint), "Voice pointer target is outside the viewport.");
        var point = GetViewport().GetFinalTransform() * logicalPoint;
        var mask = button == MouseButton.Right ? MouseButtonMask.Right : MouseButtonMask.Left;
        InjectPointerEvent(new InputEventMouseMotion { Position = point, GlobalPosition = point });
        InjectPointerEvent(new InputEventMouseButton { Position = point, GlobalPosition = point,
            ButtonIndex = button, ButtonMask = mask, Pressed = true });
        InjectPointerEvent(new InputEventMouseButton { Position = point, GlobalPosition = point,
            ButtonIndex = button, ButtonMask = 0, Pressed = false });
        _mouseActions++;
        await WaitFramesAsync(4);
    }

    private async Task WaitUntilAsync(Func<bool> condition, double seconds, string failure)
    {
        var deadline = Time.GetTicksMsec() + (ulong)(seconds * 1000);
        while (!condition() && Time.GetTicksMsec() < deadline)
            await ToSignal(GetTree().CreateTimer(.05), SceneTreeTimer.SignalName.Timeout);
        Require(condition(), failure);
    }

    private async Task CaptureVoiceBusAsync(string fileName)
    {
        if (!_main.UiVoice!.IsSpeaking)
            await WaitUntilAsync(() => _main.UiVoice!.IsSpeaking, 8, "Opening voice never began audio playback.");
        var bus = AudioServer.GetBusIndex("Voice");
        Require(bus >= 0, "Voice audio bus is missing.");
        var capture = new AudioEffectCapture { BufferLength = 3 };
        AudioServer.AddBusEffect(bus, capture, AudioServer.GetBusEffectCount(bus));
        await ToSignal(GetTree().CreateTimer(1.1), SceneTreeTimer.SignalName.Timeout);
        var frames = capture.GetFramesAvailable();
        Require(frames > 2048, $"Voice bus capture returned only {frames} frames.");
        var samples = capture.GetBuffer(frames);
        AudioServer.RemoveBusEffect(bus, AudioServer.GetBusEffectCount(bus) - 1);
        var path = Path.Combine(_outputDirectory, fileName);
        WriteStereoPcm16(path, samples, 44100);
        Require(new FileInfo(path).Length > 8192, "Processed Voice bus WAV is unexpectedly small.");
        var sum = 0.0; var peak = 0.0f; var nonzero = 0;
        foreach (var frame in samples)
        {
            peak = Math.Max(peak, Math.Max(Math.Abs(frame.X), Math.Abs(frame.Y)));
            sum += frame.X * frame.X + frame.Y * frame.Y;
            if (Math.Abs(frame.X) > .00001f || Math.Abs(frame.Y) > .00001f) nonzero++;
        }
        var rms = Math.Sqrt(sum / Math.Max(1, samples.Length * 2));
        Require(nonzero > samples.Length / 5 && peak > .01f && rms > .001,
            $"Processed Voice bus WAV is silent: frames={samples.Length} nonzero={nonzero} peak={peak} rms={rms}.");
        Require(peak < .99f,
            $"Processed Voice bus exceeds safe PCM headroom: peak={peak:0.0000}; exported WAV would clip.");
        GD.Print($"STELLAR_VOICE_BUS_CAPTURED {fileName} frames={samples.Length} nonzero={nonzero} peak={peak:0.0000} rms={rms:0.0000}");
    }

    /// <summary>Exercises the real active Godot bus, rather than a WAV mock, while a synthetic
    /// speaker is live. Delayed chorus/reverb copies are the audible echo regression this guards.</summary>
    private void VerifyProcessedDialogueIsSingleDrySource(VoicePlaybackController voice)
    {
        var players = Descendants(voice).OfType<AudioStreamPlayer>().ToArray();
        var dialogue = players.SingleOrDefault(player => player.Name == "DialoguePlayer");
        var bus = AudioServer.GetBusIndex("Voice");
        var noSpatialPlayers = !Descendants(voice).Any(node => node is AudioStreamPlayer2D or AudioStreamPlayer3D);
        var noDelayedCopies = bus >= 0 && Enumerable.Range(0, AudioServer.GetBusEffectCount(bus))
            .Select(index => AudioServer.GetBusEffect(bus, index))
            .All(effect => effect is not AudioEffectChorus and not AudioEffectReverb and not AudioEffectDelay);
        Check(players.Length == 1 && dialogue is not null && dialogue.Playing && dialogue.Bus == "Voice" &&
              dialogue.MaxPolyphony == 1 && noSpatialPlayers && noDelayedCopies,
            "voice-processed-dialogue-single-dry-source");
    }

    private static void WriteStereoPcm16(string path, Vector2[] samples, int rate)
    {
        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        var dataBytes = samples.Length * 4;
        writer.Write("RIFF"u8.ToArray()); writer.Write(36 + dataBytes); writer.Write("WAVEfmt "u8.ToArray());
        writer.Write(16); writer.Write((short)1); writer.Write((short)2); writer.Write(rate);
        writer.Write(rate * 4); writer.Write((short)4); writer.Write((short)16); writer.Write("data"u8.ToArray()); writer.Write(dataBytes);
        foreach (var frame in samples)
        {
            writer.Write((short)Math.Clamp((int)(frame.X * short.MaxValue), short.MinValue, short.MaxValue));
            writer.Write((short)Math.Clamp((int)(frame.Y * short.MaxValue), short.MinValue, short.MaxValue));
        }
    }
}
