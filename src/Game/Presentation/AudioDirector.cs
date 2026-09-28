using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;
using Godot;

namespace Game.Presentation;

public sealed record AudioSettings(float Master = .78f, float Music = .64f, float Sfx = .82f);

/// <summary>Presentation-only music, UI and event audio. Settings live outside campaign saves.</summary>
public partial class AudioDirector : Node
{
    private const string SettingsPath = "user://audio-settings.json";
    private const string MusicPath = "res://assets/audio/music/claimed-by-the-void-loop.mp3";
    private static readonly string[] RequiredSoundPaths =
    [
        "res://assets/audio/sfx/ui-hover.wav",
        "res://assets/audio/sfx/ui-confirm.wav",
        "res://assets/audio/sfx/discovery-reveal.wav",
        "res://assets/audio/sfx/construction-complete.wav",
        "res://assets/audio/sfx/ship-launch.wav",
        "res://assets/audio/sfx/strategic-alert.wav",
    ];
    private static AudioDirector? _instance;
    private readonly Dictionary<string, AudioStream> _streams = new(StringComparer.Ordinal);
    private AudioStreamPlayer _music = null!;
    private AudioStreamPlayer _sfx = null!;
    private double _lastHoverAt = -1;
    private float _voiceDuck = 1, _voiceDuckTarget = 1;
    private bool _shutdownStarted;
    private bool _startupReady;
    public static AudioDirector? Instance => IsInstanceValid(_instance) ? _instance : null;
    public AudioSettings Settings { get; private set; } = new();
    public bool IsMenuContext { get; private set; } = true;
    internal bool IsMusicPlaying => _music is not null && _music.Playing;
    public bool HasRequiredAudio => _music?.Stream is AudioStreamMP3 &&
        Array.TrueForAll(RequiredSoundPaths, _streams.ContainsKey);

    public override void _Ready()
    {
        _instance = this;
        Settings = LoadSettings();
        // Keep one managed wrapper for every reused stream for the director's full lifetime.
        // Repeated GD.Load calls can recover a cache-owned native resource after its prior C#
        // wrapper was collected, leaving Godot's script bridge with a stale GCHandle.
        LoadStream(MusicPath);
        foreach (var path in RequiredSoundPaths) LoadStream(path);
        _music = MusicPlayer("Music", MusicPath);
        _sfx = new AudioStreamPlayer { Name = "SoundEffects", MaxPolyphony = 8 };
        AddChild(_sfx);
        ApplyVolumes();
    }

    public override void _ExitTree()
    {
        StopPlayers();
        if (ReferenceEquals(_instance, this)) _instance = null;
    }

    /// <summary>Lets the audio server retire active playback resources before tree teardown.</summary>
    public static async Task ShutdownAndQuitAsync(SceneTree tree, int exitCode = 0)
    {
        var director = Instance;
        if (director is null)
        {
            tree.Quit(exitCode);
            return;
        }
        if (director._shutdownStarted)
            return;

        director._shutdownStarted = true;
        // Freeze simulation and input after the final save while the audio mixer releases playback.
        tree.Paused = true;
        var playbackHandles = StopTreeAudio(tree.Root);

        const ulong maximumDrainMilliseconds = 2000;
        var drainDeadline = Time.GetTicksMsec() + maximumDrainMilliseconds;
        var drained = playbackHandles.Count == 0;
        while (!drained && Time.GetTicksMsec() < drainDeadline)
        {
            // Stop marks playback for fade-out; the audio server drops its reference after a mix.
            await director.ToSignal(tree, SceneTree.SignalName.ProcessFrame);
            drained = playbackHandles.TrueForAll(playback => playback.GetReferenceCount() <= 1);
        }

        foreach (var playback in playbackHandles)
            playback.Dispose();

        if (!drained)
        {
            GD.PushError($"Audio playback did not drain within {maximumDrainMilliseconds} ms.");
            if (exitCode == 0) exitCode = 1;
        }

        tree.Quit(exitCode);
    }

    private static List<AudioStreamPlayback> StopTreeAudio(Node root)
    {
        var handles = new List<AudioStreamPlayback>();
        StopTreeAudio(root, handles);
        return handles;
    }

    private static void StopTreeAudio(Node node, List<AudioStreamPlayback> handles)
    {
        if (node is AudioStreamPlayer player)
        {
            if (player.HasStreamPlayback())
                handles.Add(player.GetStreamPlayback());
            player.Stop();
            player.Stream = null;
        }

        foreach (var child in node.GetChildren())
            StopTreeAudio(child, handles);
    }

    private void StopPlayers()
    {
        // Stop playback and detach only this director's player references. Loaded streams remain cache-owned.
        if (_music is not null)
        {
            _music.Stop();
            _music.Stream = null;
        }
        if (_sfx is not null)
        {
            _sfx.Stop();
            _sfx.Stream = null;
        }
    }

    public void SetVoiceDucking(bool active)
    {
        if (!_shutdownStarted) _voiceDuckTarget = active ? .55f : 1;
    }

    public override void _Process(double delta)
    {
        var next = Mathf.MoveToward(_voiceDuck, _voiceDuckTarget, (float)delta * 1.6f);
        if (Math.Abs(next - _voiceDuck) < .00001f) return;
        _voiceDuck = next; ApplyVolumes();
    }

    private AudioStreamPlayer MusicPlayer(string name, string path)
    {
        var stream = _streams[path];
        if (stream is AudioStreamWav wav) wav.LoopMode = AudioStreamWav.LoopModeEnum.Forward;
        if (stream is AudioStreamMP3 mp3) mp3.Loop = true;
        var player = new AudioStreamPlayer { Name = name, Stream = stream, MaxPolyphony = 1 };
        AddChild(player);
        return player;
    }

    public void SetMenuContext(bool menu)
    {
        if (_shutdownStarted) return;
        IsMenuContext = menu;
        if (_startupReady && !_music.Playing) _music.Play();
    }

    public void CompleteStartupLoading()
    {
        if (_shutdownStarted) return;
        _startupReady = true;
        SetMenuContext(true);
    }

    public void SetVolumes(float master, float music, float sfx)
    {
        Settings = new(Math.Clamp(master, 0, 1), Math.Clamp(music, 0, 1), Math.Clamp(sfx, 0, 1));
        ApplyVolumes();
        SaveSettings();
    }

    private void ApplyVolumes()
    {
        var music = Mathf.LinearToDb(Math.Max(.0001f, Settings.Master * Settings.Music * _voiceDuck));
        _music.VolumeDb = music;
        _sfx.VolumeDb = Mathf.LinearToDb(Math.Max(.0001f, Settings.Master * Settings.Sfx));
    }

    public static void PlayHover()
    {
        var director = Instance;
        if (director is null || Time.GetTicksMsec() / 1000.0 - director._lastHoverAt < .06) return;
        director._lastHoverAt = Time.GetTicksMsec() / 1000.0;
        director.Play("res://assets/audio/sfx/ui-hover.wav");
    }

    public static void PlayConfirm() => Instance?.Play("res://assets/audio/sfx/ui-confirm.wav");

    /// <summary>Add the shared hover/commit response to bespoke graphical buttons.</summary>
    public static T Bind<T>(T button) where T : Button
    {
        button.MouseEntered += PlayHover;
        button.Pressed += PlayConfirm;
        return button;
    }

    public static void PlayEvent(string category) => Instance?.Play(category.ToLowerInvariant() switch
    {
        "research" or "exploration" or "colony" => "res://assets/audio/sfx/discovery-reveal.wav",
        "industry" or "construction" => "res://assets/audio/sfx/construction-complete.wav",
        "ships" => "res://assets/audio/sfx/ship-launch.wav",
        "combat" => "res://assets/audio/sfx/strategic-alert.wav",
        _ => "res://assets/audio/sfx/ui-confirm.wav",
    });

    private void Play(string path)
    {
        if (_shutdownStarted) return;
        if (!_streams.TryGetValue(path, out var stream)) return;
        _sfx.Stream = stream;
        _sfx.Play();
    }

    private void LoadStream(string path)
    {
        if (_streams.ContainsKey(path)) return;
        var stream = GD.Load<AudioStream>(path) ??
            throw new InvalidOperationException($"Required audio stream could not be loaded: {path}");
        _streams.Add(path, stream);
    }

    private static AudioSettings LoadSettings()
    {
        try
        {
            var path = ProjectSettings.GlobalizePath(SettingsPath);
            return File.Exists(path)
                ? JsonSerializer.Deserialize<AudioSettings>(File.ReadAllText(path)) ?? new()
                : new();
        }
        catch (Exception) { return new(); }
    }

    private void SaveSettings()
    {
        try
        {
            var path = ProjectSettings.GlobalizePath(SettingsPath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(Settings));
        }
        catch (Exception exception) { GD.PrintErr($"Audio settings could not be saved: {exception.Message}"); }
    }
}
