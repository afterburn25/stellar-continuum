using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Godot;
using Game.Diagnostics;
using Game.Presentation;

namespace Game.Presentation.Audio.Voice;

/// <summary>Owns presentation speech and sentence captions. All Godot objects stay on the
/// render thread; backend work is polled as a Task and never awaited by simulation.</summary>
public partial class VoicePlaybackController : CanvasLayer
{
    private Main _main = null!;
    private VoiceProfileRegistry? _profiles;
    private VoiceEngine? _engine;
    private Task<VoiceEngine>? _initialization;
    private Task<VoiceResult>? _synthesis;
    private CancellationTokenSource? _lineCancellation;
    private readonly List<(SpeechRequest Request, double Added)> _queue = new();
    private SpeechRequest? _active, _last;
    private AudioStreamPlayer _player = null!;
    private PanelContainer _caption = null!;
    private Label _speaker = null!, _text = null!;
    private TextureRect _captionPortrait = null!;
    private StyleBoxFlat _captionStyle = null!;
    private double _time, _remaining;
    private string _captionMeasureKey = string.Empty;
    private ulong _captionMeasureAfterFrame;
    private readonly Dictionary<string,double> _recent = new();
    private int _voiceBus;
    private bool _alive;
    public VoiceSettings Settings { get; private set; } = new();
    public IReadOnlyList<VoiceProfile> Profiles => _profiles?.All ?? Array.Empty<VoiceProfile>();
    public string BackendStatus => _engine?.Capabilities.Detail ?? (_engine?.Capabilities.Available == true ? "Windows offline speech" : "Initializing speech");
    public string Diagnostics { get; private set; } = "Idle";
    public string EventDiagnostics { get; private set; } = "No gameplay event presented.";
    public string ActiveSpeakerName => _speaker?.Text ?? "";
    public int PlayedLines { get; private set; }
    public int SubtitleLines { get; private set; }
    public int PendingCount => _queue.Count + (_synthesis is null ? 0 : 1);
    public bool IsSpeaking => _player?.Playing == true;
    public string ActiveSubtitle => _text?.Text ?? "";
    public bool HasActiveSubtitle => _active is not null && _synthesis is null && Settings.Subtitles;
    public string LastSource { get; private set; } = "";
    public Rect2 UiCaptionBounds => _caption?.GetGlobalRect() ?? new Rect2();
    public bool UiCaptionVisible => _caption?.IsVisibleInTree() == true;
    private string SettingsPath => ProjectSettings.GlobalizePath("user://voice-settings.json");

    public override void _Ready()
    {
        _main = GetParent<Main>(); Layer = 50; _alive = true;
        Settings = VoiceSettings.Load(SettingsPath);
        _voiceBus = EnsureBus("Voice", "Master");
        EnsureBus("Communications", "Voice");
        // Dialogue is intentionally a single, non-spatial source. A second voice stream or
        // delayed wet voice reads as an echo, especially with the local neural voices.
        _player = new AudioStreamPlayer { Name = "DialoguePlayer", Bus = "Voice", MaxPolyphony = 1 }; AddChild(_player);
        BuildCaptions(); BuildSettingsWindow();
        try
        {
            _profiles = VoiceProfileRegistry.FromJson(Godot.FileAccess.GetFileAsString("res://data/voice_profiles/human.json"));
            var profiles = _profiles;
            var cachePath = ProjectSettings.GlobalizePath("user://voice-cache/v1");
            _initialization = Task.Run(() => new VoiceEngine(profiles, VoiceBackendFactory.CreateOffline(), new VoiceCache(cachePath)));
        }
        catch (Exception exception) { Diagnostics = "Speech initialization failed: " + exception.Message; SupportLogger.Log("voice-fallback", Diagnostics); }
    }

    public override void _Process(double delta)
    {
        _time += delta;
        if (_initialization?.IsCompleted == true)
        {
            try
            {
                _engine = _initialization.GetAwaiter().GetResult(); Diagnostics = BackendStatus;
                SupportLogger.Log("voice-backend",
                    $"backend={_engine.Capabilities.BackendId} available={_engine.Capabilities.Available} detail={BackendStatus}");
            }
            catch (Exception exception) { Diagnostics = "Speech unavailable: " + exception.Message; SupportLogger.Log("voice-fallback", Diagnostics); }
            _initialization = null;
        }
        if (_synthesis?.IsCompleted == true)
        {
            var task = _synthesis; _synthesis = null;
            try { PresentResult(task.GetAwaiter().GetResult()); }
            catch (Exception exception) { PresentFallback(exception.GetType().Name + ": " + exception.Message); }
        }
        if (_active is not null && _synthesis is null)
        {
            _remaining -= delta;
            if (_remaining <= 0 && !_player.Playing) FinishLine();
        }
        if (_active is null && _initialization is null)
        {
            _queue.RemoveAll(p => _time - p.Added > 40 || p.Request.ExpiresAt <= DateTimeOffset.UtcNow);
            if (_queue.Count > 0)
            {
                var next = _queue.OrderByDescending(p => p.Request.Priority).ThenBy(p => p.Added).First();
                _queue.Remove(next); BeginLine(next.Request);
            }
        }
        var master = AudioDirector.Instance?.Settings.Master ?? .78f;
        _player.VolumeDb = Mathf.LinearToDb(Math.Max(.0001f, Settings.EnableVoices ? Settings.Volume * master : 0));
        _caption.Visible = HasActiveSubtitle && !_main.UiIsMenuOpen && !_main.UiIsDiplomacyOpen && !_voiceWindow.Visible;
        LayoutCaptions();
        if (_voiceWindow.Visible && _labDiagnostics is not null) { _labDiagnostics.Text = WrapDiagnostic(Diagnostics) + "\n" + WrapDiagnostic(BackendStatus) + "\n" + WrapDiagnostic(EventDiagnostics); if (_labSubtitle is not null) _labSubtitle.Text = _active is null ? "" : _speaker.Text + "\n" + _text.Text; }
    }

    public void Speak(SpeechRequest request)
    {
        if (!_alive || string.IsNullOrWhiteSpace(request.Text)) return;
        if (Settings.ChatterLevel <= 0 && request.Priority < (int)VoicePriority.Important) return;
        var key = request.DedupeKey ?? request.ProfileId + "|" + request.Text;
        foreach (var old in _recent.Where(p => _time - p.Value > 15).Select(p => p.Key).ToArray()) _recent.Remove(old);
        if (_recent.ContainsKey(key)) return;
        if (_recent.Count >= 128) _recent.Remove(_recent.MinBy(p => p.Value).Key);
        _recent[key] = _time;
        if (_active is not null && request.QueueBehavior != SpeechQueueBehavior.Enqueue &&
            request.Priority > _active.Priority && !Settings.NoInterruptions && _active.Interruptible)
            StopCurrent();
        if (request.QueueBehavior == SpeechQueueBehavior.ReplaceCategory)
            _queue.RemoveAll(p => p.Request.Category == request.Category);
        if (_queue.Count >= 8)
        {
            var lowest = _queue.MinBy(p => p.Request.Priority);
            if (lowest.Request.Priority >= request.Priority) return;
            _queue.Remove(lowest);
        }
        if (_queue.Count(p => p.Request.Category == request.Category) >= 2) return;
        _queue.Add((request, _time));
    }

    private void BeginLine(SpeechRequest request)
    {
        if (request.SpeakerContext is { } speakerContext && request.SpeakerResolver is { } resolveSpeaker)
        {
            ResolvedVoiceSpeaker? resolved = null;
            try { resolved = resolveSpeaker(speakerContext); }
            catch (Exception error) { SupportLogger.Log("voice-fallback", "Character resolution failed: " + error.Message); }
            request = request with
            {
                ProfileId = resolved?.ProfileId ?? "",
                SpeakerName = resolved?.DisplayName ?? speakerContext.Role.ToString(),
                SpeakerRole = speakerContext.Role, SpeakerCharacterId = resolved?.CharacterId,
                SpeakerPortrait = resolved?.Portrait,
            };
            EventDiagnostics = $"Event: {request.EventId}\nCivilization: {speakerContext.SourceCivilizationId}\n" +
                $"Role: {speakerContext.Role}\nCharacter: {resolved?.CharacterId ?? "role fallback"}\n" +
                $"Voice: {resolved?.ProfileId ?? "subtitles only"}\nDialogue: {request.LocalizationKey}\nText: {request.Text}";
            if (_main.UiIsDeveloperMode) SupportLogger.Log("voice-event", EventDiagnostics);
        }
        if (request.PrerecordedPath is { } asset && (asset.StartsWith("res://", StringComparison.Ordinal) || asset.StartsWith("user://", StringComparison.Ordinal)))
            request = request with { PrerecordedPath = ProjectSettings.GlobalizePath(asset) };
        _active = _last = request;
        _lineCancellation?.Dispose(); _lineCancellation = new();
        if (Settings.EnableVoices && _engine is not null)
        {
            _synthesis = _engine.EnqueueAsync(request, _lineCancellation.Token);
            Diagnostics = $"Synthesizing {request.ProfileId} · queued {_queue.Count}";
        }
        else PresentFallback(Settings.EnableVoices ? "Backend unavailable" : "Voice disabled");
    }

    private void PresentResult(VoiceResult result)
    {
        if (_active is null) return;
        if (!Settings.EnableVoices)
        {
            _player.Stop();
            AudioDirector.Instance?.SetVoiceDucking(false);
            PresentFallback("Voice disabled while synthesis was pending");
            return;
        }
        if (!result.Succeeded || result.WavePath is null) { PresentFallback(result.Error ?? "Speech unavailable"); return; }
        try
        {
            var wav = AudioStreamWav.LoadFromFile(result.WavePath);
            if (wav is null || wav.GetLength() <= 0) { PresentFallback("Invalid audio returned by backend"); return; }
            ConfigureProcessing(_profiles?.Resolve(_active.ProfileId), _active);
            _player.Stream = wav; _player.Play();
            LastSource = result.Prerecorded ? "prerecorded" : result.CacheHit ? "cache" : "synthesized";
            Diagnostics = $"{_active.ProfileId} · {result.SelectedVoice ?? "installed voice"} · {LastSource} · {wav.GetLength():0.0}s";
            _remaining = Math.Max(wav.GetLength() + .25, ReadSeconds(_active));
            PlayedLines++; ShowCaption();
            AudioDirector.Instance?.SetVoiceDucking(true);
            SupportLogger.Log("voice-playback", Diagnostics);
        }
        catch (Exception exception) { PresentFallback(exception.GetType().Name + ": " + exception.Message); }
    }

    private void PresentFallback(string reason)
    {
        if (_active is null) return;
        LastSource = "subtitle";
        Diagnostics = $"{_active.ProfileId} · subtitle only · {reason}";
        _remaining = ReadSeconds(_active); ShowCaption();
        SupportLogger.Log("voice-fallback", Diagnostics);
    }
    private static double ReadSeconds(SpeechRequest request) => Math.Clamp((request.SubtitleText ?? request.Text).Split(' ').Length / 2.7 + 1, 3, 30);
    private void ShowCaption()
    {
        if (_active is null) return;
        VoiceProfile? profile = null;
        try { profile = _profiles?.Resolve(_active.ProfileId); } catch (Exception) { }
        var displayName = _active.SpeakerName ?? (string.IsNullOrWhiteSpace(profile?.SubtitleName) ? profile?.DisplayName ?? "Announcement" : profile.SubtitleName);
        var role = _active.SpeakerRole is { } speakerRole
            ? System.Text.RegularExpressions.Regex.Replace(speakerRole.ToString(), "([a-z])([A-Z])", "$1 $2") : null;
        var duplicateRole = role is not null && string.Equals(displayName.Trim(), role.Trim(), StringComparison.OrdinalIgnoreCase);
        _speaker.Text = Settings.SpeakerLabels ? displayName + (role is null || duplicateRole ? "" : " — " + role) : "";
        _captionPortrait.Texture = null;
        var portraitPath = _active.SpeakerPortrait ?? profile?.Portrait;
        if (portraitPath?.StartsWith("res://assets/visual/", StringComparison.Ordinal) == true && ResourceLoader.Exists(portraitPath))
            _captionPortrait.Texture = GD.Load<Texture2D>(portraitPath);
        _captionPortrait.Visible = _captionPortrait.Texture is not null;
        _text.Text = _active.SubtitleText ?? _active.Text;
        _text.AddThemeFontSizeOverride("font_size", Settings.SubtitleSize);
        SubtitleLines++;
    }
    private void FinishLine() { _active = null; _caption.Hide(); AudioDirector.Instance?.SetVoiceDucking(false); }
    private void StopCurrent()
    {
        _lineCancellation?.Cancel(); _synthesis = null; _player.Stop();
        _engine?.ClearPending(); FinishLine();
    }
    public void Stop() { StopCurrent(); _queue.Clear(); }
    public void ResetCampaign() { Stop(); _recent.Clear(); _last = null; _text.Text = ""; HideVoiceSettings(); }
    public void ReplayLast()
    {
        if (_last is null) return;
        var last = _last; Stop(); _recent.Clear(); _engine?.ClearPending();
        Speak(last with { DedupeKey = "replay:" + _time.ToString(System.Globalization.CultureInfo.InvariantCulture), ExpiresAt = DateTimeOffset.UtcNow.AddSeconds(40) });
    }
    public void ApplySettings(VoiceSettings settings)
    {
        var voicesWereEnabled = Settings.EnableVoices;
        Settings = settings.Sanitize();
        _captionStyle.BgColor = new Color(_captionStyle.BgColor, Settings.Opacity);
        if (!Settings.EnableVoices)
        {
            _player.Stop(); AudioDirector.Instance?.SetVoiceDucking(false);
            if (voicesWereEnabled && _active is not null && _synthesis is not null)
            {
                _lineCancellation?.Cancel(); _synthesis = null; _engine?.ClearPending();
                PresentFallback("Voice disabled while synthesis was pending");
            }
        }
        try { Settings.Save(SettingsPath); }
        catch (Exception exception) { Diagnostics = "Could not save voice settings: " + exception.Message; SupportLogger.Log("voice-settings", Diagnostics); }
        if (_active is not null) { _text.AddThemeFontSizeOverride("font_size", Settings.SubtitleSize); _speaker.Visible = Settings.SpeakerLabels; }
    }
    public override void _ExitTree()
    {
        _alive = false; _lineCancellation?.Cancel(); _engine?.ClearPending();
        AudioDirector.Instance?.SetVoiceDucking(false);
        if (_engine is not null) _ = _engine.DisposeAsync();
        if (_initialization is not null) _ = _initialization.ContinueWith(t => { if (t.Status == TaskStatus.RanToCompletion) _ = t.Result.DisposeAsync(); });
    }
    private void BuildCaptions()
    {
        _caption = new PanelContainer { Name = "VoiceSubtitle", MouseFilter = Control.MouseFilterEnum.Ignore, Visible = false };
        _captionStyle = (StyleBoxFlat)VisualUi.Surface(false, 14);
        _captionStyle.BgColor = new Color(_captionStyle.BgColor, Settings.Opacity);
        _caption.AddThemeStyleboxOverride("panel", _captionStyle); AddChild(_caption);
        var row = new HBoxContainer { MouseFilter = Control.MouseFilterEnum.Ignore }; _caption.AddChild(row);
        _captionPortrait = new TextureRect { CustomMinimumSize = new(56, 56), ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered, MouseFilter = Control.MouseFilterEnum.Ignore, Visible = false };
        row.AddChild(_captionPortrait);
        var column = new VBoxContainer { MouseFilter = Control.MouseFilterEnum.Ignore, SizeFlagsHorizontal = Control.SizeFlags.ExpandFill }; row.AddChild(column);
        _speaker = VisualUi.Text("", 12, VisualUi.Gold, true);
        _text = VisualUi.Text("", Settings.SubtitleSize, Colors.White, true);
        _speaker.MouseFilter = _text.MouseFilter = Control.MouseFilterEnum.Ignore;
        column.AddChild(_speaker); column.AddChild(_text);
    }
    private void LayoutCaptions()
    {
        var size = GetViewport().GetVisibleRect().Size;
        var width = Math.Min(740, size.X - 160);
        // Keep a stable two-line reservation through a drawer session. A newly started
        // line may wrap to a taller caption, but first give Godot's containers a frame to
        // apply the new width. Reading their minimum height in the same frame as a width
        // change can use the old zero-width wrap and permanently over-reserve the drawer.
        var baseline = Math.Max(56, Settings.SubtitleSize * 2 + 32);
        var frame = Engine.GetProcessFrames();
        var measureKey = $"{width}:{Settings.SubtitleSize}:{_captionPortrait.Visible}:{_speaker.Visible}:{_speaker.Text}:{_text.Text}";
        if (!string.Equals(measureKey, _captionMeasureKey, StringComparison.Ordinal))
        {
            _captionMeasureKey = measureKey;
            _captionMeasureAfterFrame = frame + 1;
        }
        var canMeasure = frame > _captionMeasureAfterFrame;
        var height = Math.Max(_active is not null && canMeasure ? _caption.GetCombinedMinimumSize().Y : 0, baseline);
        _caption.Size = new(width, height);
        if (Settings.Subtitles)
            _main.GetNodeOrNull<CampaignSidebar>("CampaignSidebar")?.SetCaptionSafeArea(height);
        else
            _main.GetNodeOrNull<CampaignSidebar>("CampaignSidebar")?.SetCaptionSafeArea(0);
        _caption.Position = new((size.X - width) / 2, size.Y - height - 24);
    }
    private static int EnsureBus(string name, string send)
    {
        int index = AudioServer.GetBusIndex(name);
        if (index >= 0) return index;
        index = AudioServer.BusCount; AudioServer.AddBus(); AudioServer.SetBusName(index, name); AudioServer.SetBusSend(index, send); return index;
    }
    private void ConfigureProcessing(VoiceProfile? profile, SpeechRequest request)
    {
        while (AudioServer.GetBusEffectCount(_voiceBus) > 0) AudioServer.RemoveBusEffect(_voiceBus, 0);
        if (profile is null) return;
        var radio = request.CommunicationsFilterOverride ?? (profile.Radio || request.CommunicationsFilter);
        var eq = new AudioEffectEQ6();
        eq.SetBandGainDb(0, radio ? -12 * Settings.CommsIntensity : 0);
        eq.SetBandGainDb(1, profile.Resonance * 3);
        eq.SetBandGainDb(3, profile.Resonance * 2);
        eq.SetBandGainDb(5, radio ? -8 * Settings.CommsIntensity : profile.Synthetic ? -2 : 0);
        AudioServer.AddBusEffect(_voiceBus, eq);
        if (Math.Abs(profile.Pitch) > .01f)
            AudioServer.AddBusEffect(_voiceBus, new AudioEffectPitchShift { PitchScale = MathF.Pow(2, Math.Clamp(profile.Pitch, -4, 4) / 12) });
        // Do not add chorus or reverb here. The previous two 18/27 ms delayed chorus voices,
        // combined with profile reverb, made the dry neural WAV sound doubled and enclosed.
        // Character identity remains in the selected Kokoro voice, cadence, communications EQ,
        // resonance, and mild pitch adjustment without an audible repeat of the dialogue.
        // Pitch shifting can overshoot a normalized neural WAV, so reserve output headroom at
        // the end of this bus. It only catches peaks and leaves the player's volume control and
        // ordinary dialogue loudness unchanged.
        AudioServer.AddBusEffect(_voiceBus, new AudioEffectHardLimiter
        {
            PreGainDb = 0.0f,
            CeilingDb = -1.0f,
            Release = 0.08f,
        });
    }
}
