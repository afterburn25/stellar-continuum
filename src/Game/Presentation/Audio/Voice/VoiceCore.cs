using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Game.Presentation.Audio.Voice;

public enum VoicePriority { Ambient = 0, Informational = 20, Important = 40, Urgent = 60, Critical = 80, Cinematic = 100 }
public enum SpeechQueueBehavior { Enqueue, InterruptLowerPriority, ReplaceCategory }
public enum SpeechCachePolicy { UseCache, Refresh, NoCache }

public sealed record VoiceDspProfile
{
    public float FormantShift { get; init; }
    public float Reverb { get; init; }
    public float Chorus { get; init; }
    public float HarmonicLayer { get; init; }
    public float WhisperLayer { get; init; }
    public float LowFrequencyResonance { get; init; }
    public float HighFrequencyShimmer { get; init; }
    public float Distortion { get; init; }
    public float RadioAmount { get; init; }
    public float SyntheticAmount { get; init; }
    public float AlienAmount { get; init; }
}

// The original positional arguments remain source-compatible with the first integration.
public sealed record VoiceProfile(string Id, string DisplayName, string Role, string Presentation,
    float Rate = 0, float Pitch = 0, bool Radio = false, bool Synthetic = false,
    string? PreferredVoice = null, string? FallbackProfile = null, bool Enabled = true)
{
    public string? NeuralVoice { get; init; }
    public string Civilization { get; init; } = "human";
    public string Species { get; init; } = "human";
    public string Character { get; init; } = string.Empty;
    public string Sex { get; init; } = "neutral";
    public string AgeImpression { get; init; } = "adult";
    public string Accent { get; init; } = "neutral";
    public string EmotionalTone { get; init; } = "controlled";
    public float Authority { get; init; } = .5f;
    public float Warmth { get; init; } = .5f;
    public float Intensity { get; init; } = .5f;
    public string PreferredBackend { get; init; } = "windows-sapi";
    public bool RequirePreferredBackend { get; init; }
    public string PreferredModel { get; init; } = "system-installed";
    public string Culture { get; init; } = "en-US";
    public string SubtitleName { get; init; } = string.Empty;
    public string? Portrait { get; init; }
    public float Resonance { get; init; }
    public float Chorus { get; init; }
    public float Reverb { get; init; }
    public IReadOnlyDictionary<string, string>? Pronunciations { get; init; }
    public IReadOnlyDictionary<string, string>? CivilizationPronunciations { get; init; }
    public IReadOnlyDictionary<string, string>? SpeciesPronunciations { get; init; }
    public IReadOnlyDictionary<string, string>? CharacterPronunciations { get; init; }
    public VoiceDspProfile Dsp { get; init; } = new();
}

public sealed record SpeechRequest(string ProfileId, string Text)
{
    public int Priority { get; init; } = (int)VoicePriority.Informational;
    public string? DedupeKey { get; init; }
    public DateTimeOffset? ExpiresAt { get; init; }
    public DateTimeOffset CreatedAt { get; init; } = DateTimeOffset.UtcNow;
    public TimeSpan? Cooldown { get; init; }
    public string? Context { get; init; }
    public string Category { get; init; } = "general";
    public string Emotion { get; init; } = "neutral";
    public float Urgency { get; init; }
    public string? EventId { get; init; }
    public string? LocalizationKey { get; init; }
    public string? SubtitleText { get; init; }
    public bool Interruptible { get; init; } = true;
    public SpeechQueueBehavior QueueBehavior { get; init; }
    public SpeechCachePolicy CachePolicy { get; init; } = SpeechCachePolicy.UseCache;
    public bool AllowSynthesis { get; init; } = true;
    public bool CommunicationsFilter { get; init; }
    public bool? CommunicationsFilterOverride { get; init; }
    public bool Spatial { get; init; }
    public string? PrerecordedPath { get; init; }
    public string Culture { get; init; } = string.Empty;
    public IReadOnlyDictionary<string, string>? Pronunciations { get; init; }
    public VoiceSpeakerContext? SpeakerContext { get; init; }
    public Func<VoiceSpeakerContext, ResolvedVoiceSpeaker?>? SpeakerResolver { get; init; }
    public string? SpeakerName { get; init; }
    public VoiceSpeakerRole? SpeakerRole { get; init; }
    public string? SpeakerCharacterId { get; init; }
    public string? SpeakerPortrait { get; init; }
}

public sealed record VoiceResult(bool Succeeded, string? WavePath, string? Error,
    bool CacheHit = false, string? SelectedVoice = null, string? SubtitleText = null,
    string? ProfileId = null)
{
    public bool Prerecorded { get; init; }
    public string? BackendId { get; init; }
    public TimeSpan SynthesisDuration { get; init; }
}

public sealed record VoiceSettings(bool EnableVoices = true, float Volume = 1, bool Subtitles = true,
    int SubtitleSize = 18, float Opacity = 1, bool SpeakerLabels = true, float ChatterLevel = 1,
    bool NoInterruptions = false, float CommsIntensity = 1, bool OfflineOnly = true)
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
        Converters = { new System.Text.Json.Serialization.JsonStringEnumConverter() },
    };
    public VoiceFrequency Frequency { get; init; } = VoiceFrequency.Normal;
    public VoiceFrequency EffectiveFrequency => ChatterLevel <= 0 ? VoiceFrequency.Minimal : Frequency;
    public VoiceSettings Sanitize() => this with
    {
        Volume = float.IsFinite(Volume) ? Math.Clamp(Volume, 0, 1) : 1,
        SubtitleSize = Math.Clamp(SubtitleSize, 12, 42),
        Opacity = float.IsFinite(Opacity) ? Math.Clamp(Opacity, 0, 1) : 1,
        ChatterLevel = float.IsFinite(ChatterLevel) ? Math.Clamp(ChatterLevel, 0, 1) : 1,
        CommsIntensity = float.IsFinite(CommsIntensity) ? Math.Clamp(CommsIntensity, 0, 1) : 1,
        Frequency = Enum.IsDefined(Frequency) ? Frequency : VoiceFrequency.Normal,
    };
    public static VoiceSettings Load(string path)
    {
        try { return File.Exists(path) ? (JsonSerializer.Deserialize<VoiceSettings>(File.ReadAllText(path), Options) ?? new()).Sanitize() : new(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { return new(); }
    }
    public void Save(string path)
    {
        var directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(Sanitize(), Options));
        File.Move(temporary, path, true);
    }
}

public sealed record SpeechBackendCapabilities(bool Available, IReadOnlyList<string> Voices, string? Detail = null)
{
    public string BackendId { get; init; } = string.Empty;
    public bool Offline { get; init; } = true;
    public IReadOnlyList<string> Languages { get; init; } = Array.Empty<string>();
    public bool Streaming { get; init; }
    public bool EmotionStyles { get; init; }
    public int SampleRate { get; init; } = 22050;
    public string OutputFormat { get; init; } = "PCM WAV";
    public string CpuRequirement { get; init; } = "Windows SAPI";
    public int EstimatedLatencyMilliseconds { get; init; } = 250;
}

public interface IVoiceSpeechBackend
{
    SpeechBackendCapabilities Capabilities { get; }
    string BackendId => string.IsNullOrWhiteSpace(Capabilities.BackendId) ? GetType().Name : Capabilities.BackendId;
    string Model => "system-installed";
    string Version => "1";
    string ResolveVoiceId(VoiceProfile profile, string culture) => profile.PreferredVoice ?? string.Empty;
    Task SynthesizeAsync(VoiceProfile profile, string text, string wavPath, CancellationToken cancellationToken);
}

public sealed class VoiceProfileRegistry
{
    private readonly Dictionary<string, VoiceProfile> _profiles;
    public IReadOnlyCollection<VoiceProfile> Profiles => _profiles.Values;
    public IReadOnlyList<VoiceProfile> All => _profiles.Values.OrderBy(profile => profile.Id, StringComparer.Ordinal).ToArray();
    public VoiceProfileRegistry(IEnumerable<VoiceProfile> profiles)
    {
        ArgumentNullException.ThrowIfNull(profiles);
        _profiles = new(StringComparer.OrdinalIgnoreCase);
        foreach (var profile in profiles)
        {
            if (string.IsNullOrWhiteSpace(profile.Id)) throw new InvalidDataException("A voice profile has no id.");
            if (!_profiles.TryAdd(profile.Id, profile)) throw new InvalidDataException($"Duplicate voice profile '{profile.Id}'.");
        }
    }
    public VoiceProfile Resolve(string id)
    {
        if (!_profiles.TryGetValue(id, out var profile)) throw new InvalidOperationException($"Voice profile '{id}' is unavailable.");
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (!profile.Enabled)
        {
            if (!visited.Add(profile.Id) || string.IsNullOrWhiteSpace(profile.FallbackProfile) ||
                !_profiles.TryGetValue(profile.FallbackProfile, out profile))
                throw new InvalidOperationException($"Voice profile '{id}' is unavailable and has no enabled fallback.");
        }
        return profile;
    }
    public bool TryResolve(string id, out VoiceProfile profile)
    {
        try { profile = Resolve(id); return true; }
        catch (InvalidOperationException) { profile = null!; return false; }
    }
    public static VoiceProfileRegistry Load(string path) => new(JsonSerializer.Deserialize<VoiceProfile[]>(
        File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
        ?? throw new InvalidDataException("Voice profile data is empty."));
    public static VoiceProfileRegistry FromJson(string json) => new(JsonSerializer.Deserialize<VoiceProfile[]>(
        json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
        ?? throw new InvalidDataException("Voice profile data is empty."));
}
