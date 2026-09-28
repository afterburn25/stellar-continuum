using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Audio.Voice;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyVoiceTutorialAsync(MainMenuLayer menu)
    {
        Input.UseAccumulatedInput = false;
        var tutorial = _main.UiTutorial ?? throw new InvalidOperationException("Tutorial was not initialized.");
        var voice = _main.UiVoice ?? throw new InvalidOperationException("Voice was not initialized.");
        await WaitUntilAsync(() => voice.Profiles.Count == 13 && !voice.BackendStatus.Contains("Initializing"), 15,
            "Voice pack failed to initialize.");
        Check(tutorial.IsAvailable && tutorial.LessonCount == 13 && !tutorial.IsActive, "tutorial-opt-in-with-thirteen-lessons");
        await VoiceClickNamedAsync(menu, "ResumeCampaign");
        voice.Stop();
        _sidebar.ShowSection("demo");
        await WaitFramesAsync(3);
        await VoiceClickNamedAsync(_main, "VoiceTutorialStart");
        var before = _main.UiSimulationDays;
        Check(tutorial.IsActive && _main.UiIsPaused && tutorial.LessonIndex == 0, "tutorial-start-pauses-without-new-campaign");
        await WaitUntilAsync(() => voice.IsSpeaking && voice.ActiveSubtitle == tutorial.LessonText, 25,
            "Commander tutorial audio did not start.");
        Check(voice.Diagnostics.Contains("af_kore"), "tutorial-welcome-uses-current-commander-neural-voice");
        AssertTutorialBounds(tutorial);
        await SaveViewportAsync("tutorial-01-welcome-720.png");
        await CaptureVoiceBusAsync("tutorial-commander-live.wav");
        await VoiceClickNamedAsync(tutorial, "TutorialShowScreen");
        Check(_sidebar.ActiveSection == "demo", "tutorial-show-screen-navigates-without-order");
        Check(!tutorial.UiBounds.Intersects(_sidebar.UiDrawerBounds), "tutorial-card-clear-of-operations-drawer");

        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        await VoiceClickNamedAsync(tutorial, "TutorialShowScreen");
        Check(tutorial.LessonId == "map" && !_sidebar.IsDrawerOpen, "tutorial-home-lesson-opens-map");
        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        await VoiceClickNamedAsync(tutorial, "TutorialShowScreen");
        Check(_sidebar.ActiveSection == "economy", "tutorial-economy-lesson-opens-correct-page");
        await VoiceClickNamedAsync(tutorial, "TutorialClose");
        Check(!tutorial.IsActive, "tutorial-close-stops-guide");
        var afterClose = voice.PlayedLines;
        await WaitFramesAsync(20);
        Check(voice.PlayedLines == afterClose && voice.PendingCount == 0, "tutorial-close-cancels-pending-speech");
        _sidebar.ShowSection("demo");
        await WaitFramesAsync(3);
        await VoiceClickNamedAsync(_main, "VoiceTutorialStart");
        Check(tutorial.LessonId == "economy", "tutorial-resumes-last-lesson");
        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        await VoiceClickNamedAsync(tutorial, "TutorialShowScreen");
        await WaitUntilAsync(() => voice.IsSpeaking && voice.ActiveSubtitle == tutorial.LessonText, 25,
            "Scientist tutorial audio did not start.");
        Check(tutorial.LessonId == "research" && _sidebar.ActiveSection == "research" &&
            voice.Diagnostics.Contains(voice.Profiles.Single(p => p.Id == "human_female_chief_scientist").NeuralVoice!),
            "tutorial-research-uses-scientist-and-correct-workspace");
        await WaitFramesAsync(6);
        AssertTutorialBounds(tutorial);
        Check(!tutorial.UiBounds.Intersects(voice.UiCaptionBounds), "tutorial-card-clear-of-research-caption");
        var researchWorkspace = _main.GetNode<ResearchWorkspaceView>("PlayerControls/ResearchWorkspace");
        Check(!researchWorkspace.GetGlobalRect().Intersects(tutorial.UiBounds) &&
            !researchWorkspace.GetGlobalRect().Intersects(voice.UiCaptionBounds),
            "tutorial-research-controls-clear-of-card-and-caption");
        await SaveViewportAsync("tutorial-02-research-720.png");
        await CaptureVoiceBusAsync("tutorial-scientist-live.wav");
        await VoiceClickNamedAsync(tutorial, "TutorialReadText");
        await WaitFramesAsync(4);
        AssertTutorialBounds(tutorial);
        Check(!voice.IsSpeaking, "tutorial-reading-stops-lesson-speech");
        await SaveViewportAsync("tutorial-03-transcript-720.png");
        await VoiceClickNamedAsync(tutorial, "TutorialReadText");
        voice.ApplySettings(voice.Settings with { EnableVoices = false });
        await VoiceClickNamedAsync(tutorial, "TutorialReplay");
        await WaitUntilAsync(() => voice.LastSource == "subtitle" && voice.ActiveSubtitle == tutorial.LessonText, 5,
            "Muted tutorial lost its caption.");
        Check(tutorial.IsActive, "tutorial-remains-usable-with-voice-muted");
        voice.CancelCategory("tutorial");
        voice.Speak(new SpeechRequest("ship_computer", "Critical alert remains available.")
            { Category = "test-alert", Priority = 80, DedupeKey = "tutorial-independent-alert" });
        await WaitUntilAsync(() => voice.ActiveSubtitle == "Critical alert remains available.", 5, "Alert failed to present.");
        await VoiceClickNamedAsync(tutorial, "TutorialStopVoice");
        Check(voice.ActiveSubtitle == "Critical alert remains available." && voice.UiCaptionVisible,
            "tutorial-stop-preserves-unrelated-alert");
        voice.Stop();
        voice.ApplySettings(voice.Settings with { EnableVoices = true });
        await VoiceClickNamedAsync(tutorial, "TutorialReplay");
        _main.UiOpenMenu();
        await WaitFramesAsync(5);
        Check(tutorial.IsActive && !Descendants(tutorial).OfType<Control>().Single(c => c.Name == "VoiceTutorialCard").Visible &&
            voice.PendingCount == 0, "tutorial-menu-suspends-pending-speech-and-hides-card");
        await VoiceClickNamedAsync(menu, "ResumeCampaign");
        _main.UiSetPaused(true, announce: false);
        await WaitFramesAsync(5);
        Check(tutorial.IsActive && tutorial.LessonId == "research", "tutorial-returns-from-menu-at-same-lesson");

        voice.ApplySettings(voice.Settings with { EnableVoices = false });
        while (tutorial.LessonIndex < tutorial.LessonCount - 1)
        {
            await VoiceClickNamedAsync(tutorial, "TutorialNext");
            await VoiceClickNamedAsync(tutorial, "TutorialShowScreen");
            AssertTutorialBounds(tutorial);
        }
        Check(tutorial.LessonId == "save" && _sidebar.ActiveSection == "menu", "tutorial-reaches-save-lesson");
        GetWindow().Size = new Vector2I(1920, 1080);
        await WaitFramesAsync(12);
        AssertTutorialBounds(tutorial);
        await SaveViewportAsync("tutorial-04-save-1080.png", 1920, 1080);
        await VoiceClickNamedAsync(tutorial, "TutorialNext");
        Check(!tutorial.IsActive, "tutorial-finish-closes-without-granting-objectives");
        Check(_main.UiSimulationDays == before, "tutorial-lessons-did-not-advance-paused-simulation");
        var progressPath = ProjectSettings.GlobalizePath("user://voice-tutorial-progress.json");
        using (var progress = JsonDocument.Parse(File.ReadAllText(progressPath)))
            Check(progress.RootElement.GetProperty("LessonId").GetString() == "save" &&
                progress.RootElement.GetProperty("Completed").GetBoolean(), "tutorial-completion-persisted-separately-from-campaign");
        _sidebar.ShowSection("demo");
        await WaitFramesAsync(3);
        await VoiceClickNamedAsync(_main, "VoiceTutorialRestart");
        Check(tutorial.IsActive && tutorial.LessonId == "welcome", "tutorial-explicit-restart-preserves-campaign");
        _main.UiCreateNewCampaignConfirmed("tutorial-reset-fixture");
        await WaitFramesAsync(5);
        Check(!tutorial.IsActive, "tutorial-campaign-replacement-stops-active-guide");
        File.WriteAllText(Path.Combine(_outputDirectory, "tutorial-checks.json"),
            JsonSerializer.Serialize(new { checks = _checks, mouseActions = _mouseActions, captures = _captureRecords },
                new JsonSerializerOptions { WriteIndented = true }));
    }

    private void AssertTutorialBounds(VoiceTutorialLayer tutorial)
    {
        var viewport = GetViewport().GetVisibleRect();
        var bounds = tutorial.UiBounds;
        Require(bounds.Position.X >= 0 && bounds.Position.Y >= 0 && bounds.End.X <= viewport.Size.X && bounds.End.Y <= viewport.Size.Y,
            $"Tutorial outside viewport: {bounds} in {viewport}.");
        foreach (var control in Descendants(tutorial).OfType<Button>().Where(c => c.IsVisibleInTree()))
            AssertInsideViewport(control, "tutorial " + control.Name);
    }
}
