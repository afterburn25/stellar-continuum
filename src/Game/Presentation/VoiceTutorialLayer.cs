using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Game.Diagnostics;
using Game.Presentation.Audio.Voice;
using Godot;

namespace Game.Presentation;

public sealed record VoiceTutorialLesson(string Id, string Title, string Role, string Profile,
    string Screen, string Action, string Instruction, string Text);

/// <summary>Optional, player-paced lessons. Navigation never grants campaign progress or issues orders.</summary>
public partial class VoiceTutorialLayer : CanvasLayer
{
    private sealed record Progress(string LessonId = "welcome", bool Completed = false);
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true, WriteIndented = true };
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    private ResearchWorkspaceView? _researchWorkspace;
    private PanelContainer _card = null!;
    private Label _heading = null!, _instruction = null!, _transcript = null!, _status = null!;
    private Button _back = null!, _next = null!, _show = null!;
    private ScrollContainer _transcriptScroll = null!;
    private IReadOnlyList<VoiceTutorialLesson> _lessons = Array.Empty<VoiceTutorialLesson>();
    private int _index;
    private bool _active, _blocked, _completed;
    private bool _layoutQueued;
    private ulong _campaignRevision;
    private long _narrationRevision;
    private string ProgressPath => ProjectSettings.GlobalizePath("user://voice-tutorial-progress.json");
    public bool IsActive => _active;
    public bool IsAvailable => _lessons.Count > 0;
    public int LessonIndex => _index;
    public int LessonCount => _lessons.Count;
    public string LessonId => IsAvailable ? _lessons[_index].Id : "";
    public string LessonText => IsAvailable ? _lessons[_index].Text : "";
    public Rect2 UiBounds => _card?.GetGlobalRect() ?? new Rect2();

    public override void _Ready()
    {
        _main = GetParent<Main>();
        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        _researchWorkspace = _main.GetNodeOrNull<ResearchWorkspaceView>("PlayerControls/ResearchWorkspace");
        Layer = 8;
        _campaignRevision = _main.UiCampaignApplicationRevision;
        try
        {
            var lessons = JsonSerializer.Deserialize<VoiceTutorialLesson[]>(
                Godot.FileAccess.GetFileAsString("res://data/voice_profiles/tutorial.json"), JsonOptions) ?? Array.Empty<VoiceTutorialLesson>();
            var screens = new HashSet<string> { "demo", "home", "map", "economy", "industry", "research", "ships", "explore", "colonies", "relations", "menu" };
            if (lessons.Length == 0 || lessons.Select(l => l.Id).Distinct().Count() != lessons.Length ||
                lessons.Any(l => string.IsNullOrWhiteSpace(l.Id) || string.IsNullOrWhiteSpace(l.Text) ||
                    !Enum.TryParse<VoiceSpeakerRole>(l.Role, out _) || !screens.Contains(l.Screen)))
                throw new InvalidDataException("Invalid voice tutorial lesson catalogue.");
            _lessons = lessons;
            if (File.Exists(ProgressPath))
            {
                try
                {
                    var progress = JsonSerializer.Deserialize<Progress>(File.ReadAllText(ProgressPath), JsonOptions);
                    _index = Math.Max(0, Array.FindIndex(lessons, l => l.Id == progress?.LessonId));
                    _completed = progress?.Completed == true;
                }
                catch (Exception error) { SupportLogger.Log("tutorial-progress", "Starting at the first lesson: " + error.Message); }
            }
        }
        catch (Exception error) { SupportLogger.Log("tutorial-unavailable", error.Message); }
        BuildCard();
    }

    private void BuildCard()
    {
        _card = new PanelContainer { Name = "VoiceTutorialCard", Visible = false };
        _card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 14));
        VisualUi.ContainPointerInput(_card);
        var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 8); _card.AddChild(body);
        var top = new HBoxContainer(); body.AddChild(top);
        var label = VisualUi.Text("OFFICER TUTORIAL", 11, VisualUi.Accent);
        label.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill; top.AddChild(label);
        var close = VisualUi.Button("Close", "Stop tutorial speech and keep your place. Resume from Guide.", Close);
        close.Name = "TutorialClose"; top.AddChild(close);
        _heading = VisualUi.Text("", 19, wrap: true); body.AddChild(_heading);
        _instruction = VisualUi.Text("", 14, wrap: true); _instruction.Name = "TutorialInstruction"; body.AddChild(_instruction);
        _show = VisualUi.Button("Show screen", "Open the relevant page without issuing a game order.", ShowScreen);
        _show.Name = "TutorialShowScreen"; body.AddChild(_show);
        var navigation = new HBoxContainer(); body.AddChild(navigation);
        _back = VisualUi.Button("Back", "Return to the previous lesson.", Previous); _back.Name = "TutorialBack";
        _next = VisualUi.Button("Next lesson", "Continue when you are ready; campaign objectives are unchanged.", Next); _next.Name = "TutorialNext";
        _back.SizeFlagsHorizontal = _next.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        navigation.AddChild(_back); navigation.AddChild(_next);
        var audio = new HBoxContainer(); body.AddChild(audio);
        var replay = VisualUi.Button("Replay", "Hear this lesson again.", Replay); replay.Name = "TutorialReplay";
        var stop = VisualUi.Button("Stop voice", "Stop this lesson's speech and keep the written guidance.", StopSpeech); stop.Name = "TutorialStopVoice";
        replay.SizeFlagsHorizontal = stop.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        audio.AddChild(replay); audio.AddChild(stop);
        var read = new CheckButton { Text = "Read full lesson", Name = "TutorialReadText" };
        body.AddChild(read);
        _transcriptScroll = new ScrollContainer { Visible = false, HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, CustomMinimumSize = new Vector2(0, 120) };
        _transcript = VisualUi.Text("", 14, wrap: true); _transcript.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        _transcriptScroll.AddChild(_transcript); body.AddChild(_transcriptScroll);
        read.Toggled += visible =>
        {
            _transcriptScroll.Visible = visible; _instruction.Visible = !visible;
            if (visible) StopSpeech();
            UpdateBounds();
        };
        _status = VisualUi.Text("", 12, VisualUi.Muted, wrap: true); body.AddChild(_status);
        AddChild(_card);
        _card.MinimumSizeChanged += ScheduleLayout;
        GetViewport().SizeChanged += ScheduleLayout;
        UpdateBounds();
    }

    public override void _ExitTree() => GetViewport().SizeChanged -= ScheduleLayout;

    public override void _Process(double delta)
    {
        _ = delta;
        if (_campaignRevision != _main.UiCampaignApplicationRevision)
        {
            _campaignRevision = _main.UiCampaignApplicationRevision;
            Close(); // Progress is optional learning preference, never an active queue carried into a new campaign.
        }
        var blocked = _main.UiIsMenuOpen || _main.UiIsDeveloperToolsOpen || _main.UiIsSurfaceOpen;
        if (blocked && !_blocked && _active) StopSpeech();
        _blocked = blocked;
        _card.Visible = _active && !blocked;
        _sidebar.SetTutorialActive(_active && !blocked);
        var viewport = GetViewport().GetVisibleRect().Size;
        var bottom = _main.UiVoice?.UiCaptionVisible == true
            ? viewport.Y - _main.UiVoice.UiCaptionBounds.Position.Y + 12 : 0;
        _researchWorkspace?.SetTutorialSafeArea(_active && !blocked,
            viewport.X - _card.Position.X + 12, bottom);
        if (_active)
            _status.Text = (_main.UiIsPaused ? "Game paused · Space resumes." : "Game running · Space pauses.") +
                (_main.UiVoice?.Settings.EnableVoices == false ? " Voice is muted; written lessons remain available." : "");
    }

    public void Start(bool restart = false)
    {
        if (!IsAvailable || _main.UiIsMenuOpen || _main.UiIsDeveloperToolsOpen || _main.UiIsSurfaceOpen) return;
        if (restart || _completed) { _index = 0; _completed = false; }
        _campaignRevision = _main.UiCampaignApplicationRevision;
        _active = true; _blocked = false;
        _main.UiSetPaused(true);
        // Keep the drawer from hiding the map on entry. The lesson's Show screen is always explicit.
        _sidebar.CloseDrawer();
        Present();
    }

    private void Present()
    {
        var lesson = _lessons[_index];
        _heading.Text = $"{_index + 1}/{_lessons.Count} · {lesson.Title}";
        _instruction.Text = lesson.Instruction;
        _transcript.Text = lesson.Text;
        _transcriptScroll.ScrollVertical = 0;
        _show.Text = lesson.Action;
        _back.Disabled = _index == 0;
        _next.Text = _index == _lessons.Count - 1 ? "Finish tutorial" : "Next lesson";
        _card.Visible = true;
        UpdateBounds(); ScheduleLayout(); SaveProgress(); Replay();
    }

    public void Next()
    {
        if (!_active || _blocked) return;
        if (_index == _lessons.Count - 1) { _completed = true; Close(); return; }
        _index++; Present();
    }
    public void Previous() { if (_active && !_blocked && _index > 0) { _index--; Present(); } }
    public void Replay()
    {
        if (!_active || _blocked) return;
        StopSpeech();
        _main.UiSpeakTutorialLesson(_lessons[_index], ++_narrationRevision);
    }
    public void StopSpeech() => _main.UiVoice?.CancelCategory("tutorial");
    public void Close()
    {
        if (!_active) return;
        StopSpeech(); _active = false; _card.Hide(); SaveProgress();
        // Never resume time on close: the player's current pause/speed choice remains authoritative.
    }
    public void ShowScreen()
    {
        if (!_active || _blocked) return;
        var screen = _lessons[_index].Screen;
        if (screen is "home" or "map")
        {
            _sidebar.CloseDrawer(); _main.UiShowStellarRegion();
            if (screen == "home") _main.UiSelectHomeSystem();
        }
        else if (_sidebar.ActiveSection != screen) _sidebar.ShowSection(screen);
    }

    private void UpdateBounds()
    {
        if (_card is null) return;
        var viewport = GetViewport().GetVisibleRect().Size;
        var width = Math.Min(350, Math.Max(280, viewport.X - 920));
        _card.Position = new Vector2(viewport.X - width - 18, 112);
        _card.Size = new Vector2(width, 0);
    }
    private void ScheduleLayout()
    {
        if (_layoutQueued) return;
        _layoutQueued = true;
        Callable.From(() =>
        {
            _layoutQueued = false;
            if (IsInsideTree()) UpdateBounds();
        }).CallDeferred();
    }
    private void SaveProgress()
    {
        if (!IsAvailable) return;
        try
        {
            var temporary = ProgressPath + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(new Progress(LessonId, _completed), JsonOptions));
            File.Move(temporary, ProgressPath, true);
        }
        catch (Exception error) { SupportLogger.Log("tutorial-progress", "Could not save optional tutorial position: " + error.Message); }
    }
}
