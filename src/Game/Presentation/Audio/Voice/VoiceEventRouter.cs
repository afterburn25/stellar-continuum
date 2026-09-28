using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace Game.Presentation.Audio.Voice;

public enum VoiceFrequency { Minimal, Normal, Frequent }
public enum VoiceAudience { OwnCivilization, Observable, DirectCommunication, ObserverSafe }

public sealed record VoiceEventOverrides
{
    public VoiceSpeakerRole? SpeakerRole { get; init; }
    public string? CharacterId { get; init; }
    public string? VoiceProfileId { get; init; }
    public string? DialogueKey { get; init; }
    public string? ExactLine { get; init; }
    public int? Priority { get; init; }
    public string? Emotion { get; init; }
    public bool? CommunicationsFilter { get; init; }
    public bool? Interruptible { get; init; }
    public SpeechQueueBehavior? QueueBehavior { get; init; }
    public string? Category { get; init; }
    public double? CooldownSeconds { get; init; }
}

/// <summary>Observer-filterable presentation contract with no engine, backend, cache,
/// audio, Godot, or authoritative simulation types.</summary>
public sealed record GameplayVoiceEvent(string EventKey, int SourceCivilizationId, string UniqueEventId,
    IReadOnlyDictionary<string, string> Variables, long SimulationTick, string SimulationDate)
{
    public string SourceSpeciesId { get; init; } = string.Empty;
    public VoiceAudience Audience { get; init; } = VoiceAudience.OwnCivilization;
    public int? RecipientCivilizationId { get; init; }
    public bool ObserverEvidence { get; init; }
    public bool FirstOccurrence { get; init; }
    public VoiceEventOverrides? Overrides { get; init; }
}

public sealed record VoiceRoutingContext(int PlayerCivilizationId,
    VoiceFrequency Frequency = VoiceFrequency.Normal, bool ObserverMode = false)
{
    public DateTimeOffset? PresentationTime { get; init; }
}

// Positional members remain compatible with the original voice integration.
public sealed record VoiceEventCue(string Event, string Profile, string[] Lines, int Priority = 40,
    bool Once = false, double CooldownSeconds = 12, bool Interruptible = true, string? PrerecordedPath = null)
{
    public string DialogueKey { get; init; } = string.Empty;
    public VoiceSpeakerRole SpeakerRole { get; init; } = VoiceSpeakerRole.Narrator;
    public string[] FirstLines { get; init; } = Array.Empty<string>();
    public VoiceFrequency Frequency { get; init; } = VoiceFrequency.Normal;
    public string Category { get; init; } = string.Empty;
    public string Emotion { get; init; } = "neutral";
    public bool CommunicationsFilter { get; init; }
    public SpeechQueueBehavior QueueBehavior { get; init; } = SpeechQueueBehavior.ReplaceCategory;
}

/// <summary>Maps already-authorized gameplay presentation events to finite authored dialogue.
/// It never reads hidden state, mutates simulation, or invokes a speech provider.</summary>
public sealed class VoiceEventRouter
{
    private const int RecentEventLimit = 512;
    private static readonly Regex TemplateToken = new("\\{([a-z0-9_]+)\\}",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static readonly IReadOnlyDictionary<string, string> LegacyMilestoneAliases =
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["research"] = "research.completed",
            ["construction"] = "construction.completed",
            ["shipyard"] = "construction.orbital_shipyard.completed",
            ["ship_launch"] = "ship.completed",
            ["departure"] = "ship.interstellar.first_launch",
            ["arrival"] = "exploration.system.reached",
            ["discovery"] = "exploration.anomaly.discovered",
            ["survey"] = "exploration.survey.completed",
            ["colony"] = "colony.founded",
            ["unknown_contact"] = "contact.unknown.detected",
            ["first_contact"] = "contact.first",
            ["alien_transmission"] = "diplomacy.alien.transmission",
            ["critical_hull"] = "combat.hull.critical",
            ["treasury"] = "economy.treasury.critical",
        };
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        Converters = { new JsonStringEnumConverter() },
    };

    private readonly Dictionary<string, VoiceEventCue> _cues;
    private readonly Dictionary<string, int> _counts = new(StringComparer.Ordinal);
    private readonly Dictionary<string, DateTimeOffset> _lastCategory = new(StringComparer.Ordinal);
    private readonly Dictionary<string, DateTimeOffset> _recentEvents = new(StringComparer.Ordinal);
    private readonly Action<SpeechRequest> _submit;
    private readonly CharacterVoiceResolver? _speakers;
    public IReadOnlyList<string> EventKeys => _cues.Keys.OrderBy(key => key, StringComparer.Ordinal).ToArray();

    public VoiceEventRouter(IEnumerable<VoiceEventCue> cues, Action<SpeechRequest> submit,
        CharacterVoiceResolver? speakers = null)
    {
        ArgumentNullException.ThrowIfNull(cues);
        _submit = submit ?? throw new ArgumentNullException(nameof(submit));
        _speakers = speakers;
        _cues = cues.ToDictionary(cue => cue.Event, StringComparer.Ordinal);
    }

    public static VoiceEventRouter FromJson(string json, Action<SpeechRequest> submit,
        CharacterVoiceResolver? speakers = null) => new(
        JsonSerializer.Deserialize<VoiceEventCue[]>(json, JsonOptions) ?? Array.Empty<VoiceEventCue>(), submit, speakers);

    public void Reset()
    {
        _counts.Clear();
        _lastCategory.Clear();
        _recentEvents.Clear();
    }

    public bool HasEmitted(string key) => _counts.GetValueOrDefault(key) > 0 ||
        (LegacyMilestoneAliases.TryGetValue(key, out var canonical) && _counts.GetValueOrDefault(canonical) > 0);

    public bool Emit(string key, string detail = "")
    {
        var sequence = _counts.GetValueOrDefault(key);
        var gameplayEvent = new GameplayVoiceEvent(key, 0, $"legacy:{key}:{sequence}",
            new Dictionary<string, string>(StringComparer.Ordinal) { ["detail"] = detail }, sequence, sequence.ToString());
        return EmitCore(gameplayEvent, new VoiceRoutingContext(0, VoiceFrequency.Frequent), sequence);
    }

    public bool Emit(GameplayVoiceEvent gameplayEvent, VoiceRoutingContext context)
    {
        ArgumentNullException.ThrowIfNull(gameplayEvent);
        ArgumentNullException.ThrowIfNull(context);
        return EmitCore(gameplayEvent, context, null);
    }

    private bool EmitCore(GameplayVoiceEvent gameplayEvent, VoiceRoutingContext context, int? legacySequence)
    {
        if (!IsAuthorized(gameplayEvent, context) || string.IsNullOrWhiteSpace(gameplayEvent.UniqueEventId)) return false;
        var dialogueKey = gameplayEvent.Overrides?.DialogueKey.NullIfWhiteSpace() ?? gameplayEvent.EventKey;
        if (!_cues.TryGetValue(dialogueKey, out var cue))
        {
            var exactLine = gameplayEvent.Overrides?.ExactLine.NullIfWhiteSpace();
            var exactRole = gameplayEvent.Overrides?.SpeakerRole;
            if (exactLine is null || exactRole is null) return false;
            cue = new VoiceEventCue(dialogueKey, string.Empty, new[] { exactLine },
                gameplayEvent.Overrides?.Priority ?? (int)VoicePriority.Important, CooldownSeconds: 0)
            {
                DialogueKey = dialogueKey,
                SpeakerRole = exactRole.Value,
                Frequency = VoiceFrequency.Minimal,
            };
        }
        if (cue.Lines is not { Length: > 0 } || context.Frequency < cue.Frequency) return false;

        var now = context.PresentationTime ?? DateTimeOffset.UtcNow;
        PruneRecent(now);
        if (_recentEvents.ContainsKey(gameplayEvent.UniqueEventId)) return false;

        var countKey = $"{gameplayEvent.SourceCivilizationId}:{dialogueKey}";
        var count = _counts.GetValueOrDefault(countKey);
        if (cue.Once && count > 0) return false;

        var category = gameplayEvent.Overrides?.Category.NullIfWhiteSpace() ?? cue.Category.NullIfWhiteSpace() ?? dialogueKey;
        var cooldown = Math.Clamp(gameplayEvent.Overrides?.CooldownSeconds ?? cue.CooldownSeconds, 0, 3600);
        var cooldownKey = $"{gameplayEvent.SourceCivilizationId}:{category}";
        if (_lastCategory.TryGetValue(cooldownKey, out var previous) && now - previous < TimeSpan.FromSeconds(cooldown)) return false;

        var useFirstLine = gameplayEvent.FirstOccurrence || (legacySequence.HasValue && count == 0);
        var candidates = useFirstLine && cue.FirstLines is { Length: > 0 }
            ? cue.FirstLines
            : cue.Lines;
        var selected = legacySequence ?? SelectVariant(gameplayEvent, candidates.Length);
        var template = gameplayEvent.Overrides?.ExactLine ?? candidates[selected % candidates.Length];
        if (!TryRender(template, gameplayEvent.Variables, out var text)) return false;

        var role = gameplayEvent.Overrides?.SpeakerRole ?? cue.SpeakerRole;
        var speakerContext = new VoiceSpeakerContext(role, gameplayEvent.SourceCivilizationId,
            gameplayEvent.SourceSpeciesId, gameplayEvent.Overrides?.CharacterId, gameplayEvent.Overrides?.VoiceProfileId);
        var priority = Math.Clamp(gameplayEvent.Overrides?.Priority ?? cue.Priority, 0, 100);
        var initialProfile = gameplayEvent.Overrides?.VoiceProfileId.NullIfWhiteSpace() ??
                             (legacySequence.HasValue ? cue.Profile : "voice_profile_unresolved");
        var request = new SpeechRequest(initialProfile, text)
        {
            Priority = priority,
            Category = category,
            EventId = gameplayEvent.UniqueEventId,
            LocalizationKey = "voice." + (cue.DialogueKey.NullIfWhiteSpace() ?? dialogueKey) + "." + selected % candidates.Length,
            PrerecordedPath = cue.PrerecordedPath,
            DedupeKey = $"{gameplayEvent.SourceCivilizationId}:{category}:{text}",
            Interruptible = gameplayEvent.Overrides?.Interruptible ?? cue.Interruptible,
            QueueBehavior = gameplayEvent.Overrides?.QueueBehavior ?? cue.QueueBehavior,
            Emotion = gameplayEvent.Overrides?.Emotion.NullIfWhiteSpace() ?? cue.Emotion,
            CommunicationsFilter = gameplayEvent.Overrides?.CommunicationsFilter ?? cue.CommunicationsFilter,
            CommunicationsFilterOverride = gameplayEvent.Overrides?.CommunicationsFilter,
            ExpiresAt = now.AddSeconds(priority >= (int)VoicePriority.Critical ? 15 : 40),
            SpeakerContext = speakerContext,
            SpeakerResolver = _speakers is null ? null : _speakers.Resolve,
            SpeakerRole = role,
        };

        _counts[countKey] = count + 1;
        _counts[dialogueKey] = _counts.GetValueOrDefault(dialogueKey) + 1;
        _lastCategory[cooldownKey] = now;
        _recentEvents[gameplayEvent.UniqueEventId] = now;
        _submit(request);
        return true;
    }

    private static bool IsAuthorized(GameplayVoiceEvent gameplayEvent, VoiceRoutingContext context)
    {
        if (gameplayEvent.SourceCivilizationId == context.PlayerCivilizationId) return true;
        return gameplayEvent.Audience switch
        {
            VoiceAudience.DirectCommunication => gameplayEvent.RecipientCivilizationId == context.PlayerCivilizationId,
            VoiceAudience.Observable => gameplayEvent.ObserverEvidence,
            VoiceAudience.ObserverSafe => context.ObserverMode && gameplayEvent.ObserverEvidence,
            _ => false,
        };
    }

    private static int SelectVariant(GameplayVoiceEvent gameplayEvent, int count)
    {
        var input = $"{gameplayEvent.EventKey}|{gameplayEvent.UniqueEventId}|{gameplayEvent.SimulationTick}|{gameplayEvent.SimulationDate}";
        var hash = 2166136261u;
        foreach (var character in Encoding.UTF8.GetBytes(input)) hash = (hash ^ character) * 16777619u;
        return (int)(hash % (uint)count);
    }

    private static bool TryRender(string template, IReadOnlyDictionary<string, string>? variables, out string text)
    {
        var missing = false;
        text = TemplateToken.Replace(template ?? string.Empty, match =>
        {
            if (variables is null || !variables.TryGetValue(match.Groups[1].Value, out var value))
            {
                missing = true;
                return string.Empty;
            }
            return Sanitize(value);
        });
        text = Sanitize(text);
        return !missing && text.Length > 0;
    }

    private static string Sanitize(string value)
    {
        var bounded = new string((value ?? string.Empty).Take(256)
            .Select(character => char.IsControl(character) ? ' ' : character).ToArray());
        return string.Join(' ', bounded.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }

    private void PruneRecent(DateTimeOffset now)
    {
        foreach (var key in _recentEvents.Where(pair => now - pair.Value > TimeSpan.FromHours(1))
                     .Select(pair => pair.Key).ToArray()) _recentEvents.Remove(key);
        foreach (var key in _recentEvents.OrderBy(pair => pair.Value).Take(Math.Max(0, _recentEvents.Count - RecentEventLimit))
                     .Select(pair => pair.Key).ToArray()) _recentEvents.Remove(key);
    }
}

internal static class VoiceEventStringExtensions
{
    public static string? NullIfWhiteSpace(this string? value) => string.IsNullOrWhiteSpace(value) ? null : value;
}
