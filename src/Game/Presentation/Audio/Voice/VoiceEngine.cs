using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Game.Presentation.Audio.Voice;

public sealed class VoiceEngine : IAsyncDisposable
{
    private const int QueueLimit = 32, CategoryLimit = 6, RecentLimit = 256;
    private readonly VoiceProfileRegistry _profiles;
    private readonly IVoiceSpeechBackend _backend;
    private readonly VoiceCache _cache;
    private readonly VoiceSettings _settings;
    private readonly object _gate = new();
    private readonly List<Pending> _pending = new();
    private readonly Dictionary<string, (DateTimeOffset At, TimeSpan Cooldown)> _recent = new(StringComparer.Ordinal);
    private readonly SemaphoreSlim _available = new(0);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly Task _worker;
    private Pending? _active;
    private CancellationTokenSource? _activeCancellation;
    private long _sequence;
    private bool _disposed;
    private sealed record Pending(SpeechRequest Request, long Sequence, string Key, CancellationToken CallerToken,
        TaskCompletionSource<VoiceResult> Completion);

    public SpeechBackendCapabilities Capabilities => _backend.Capabilities;
    public int PendingCount { get { lock (_gate) return _pending.Count; } }
    public SpeechRequest? ActiveRequest { get { lock (_gate) return _active?.Request; } }

    public VoiceEngine(VoiceProfileRegistry profiles, IVoiceSpeechBackend backend, VoiceCache cache)
        : this(profiles, backend, cache, new VoiceSettings()) { }
    public VoiceEngine(VoiceProfileRegistry profiles, IVoiceSpeechBackend backend, VoiceCache cache, VoiceSettings settings)
    {
        _profiles = profiles ?? throw new ArgumentNullException(nameof(profiles));
        _backend = backend ?? throw new ArgumentNullException(nameof(backend));
        _cache = cache ?? throw new ArgumentNullException(nameof(cache));
        _settings = (settings ?? new()).Sanitize();
        _worker = Task.Run(ProcessQueueAsync);
    }

    public Task<VoiceResult> EnqueueAsync(SpeechRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (cancellationToken.IsCancellationRequested) return Task.FromCanceled<VoiceResult>(cancellationToken);
        var completion = new TaskCompletionSource<VoiceResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        Pending? evicted = null;
        lock (_gate)
        {
            if (_disposed) return Task.FromResult(Failure(request, "disposed"));
            var now = DateTimeOffset.UtcNow; PruneRecent(now);
            if (request.ExpiresAt is { } expiry && expiry <= now) return Task.FromResult(Failure(request, "expired"));
            var key = request.DedupeKey ?? $"{request.ProfileId}|{SpeechText.Normalize(request.Text)}";
            var cooldown = request.Cooldown ?? TimeSpan.FromSeconds(2);
            if ((_recent.TryGetValue(key, out var last) && now - last.At < cooldown) ||
                _pending.Any(item => item.Key == key) || _active?.Key == key)
                return Task.FromResult(Failure(request, "cooldown"));
            if (request.QueueBehavior == SpeechQueueBehavior.ReplaceCategory)
                foreach (var item in _pending.Where(item => string.Equals(item.Request.Category, request.Category,
                             StringComparison.OrdinalIgnoreCase)).ToArray())
                { _pending.Remove(item); item.Completion.TrySetResult(Failure(item.Request, "replaced")); }
            if (_pending.Count(item => string.Equals(item.Request.Category, request.Category, StringComparison.OrdinalIgnoreCase)) >= CategoryLimit)
                return Task.FromResult(Failure(request, "category queue limit"));
            if (_pending.Count >= QueueLimit)
            {
                evicted = _pending.OrderBy(item => item.Request.Priority).ThenByDescending(item => item.Sequence).First();
                if (evicted.Request.Priority >= request.Priority) return Task.FromResult(Failure(request, "queue full"));
                _pending.Remove(evicted);
            }
            var pending = new Pending(request, ++_sequence, key, cancellationToken, completion);
            _pending.Add(pending); _recent[key] = (now, cooldown);
            if (!_settings.NoInterruptions && request.QueueBehavior == SpeechQueueBehavior.InterruptLowerPriority &&
                _active is { Request.Interruptible: true } active && active.Request.Priority < request.Priority)
                _activeCancellation?.Cancel();
            _available.Release();
        }
        evicted?.Completion.TrySetResult(Failure(evicted.Request, "evicted by higher priority"));
        return cancellationToken.CanBeCanceled ? AwaitCallerAsync(completion.Task, cancellationToken) : completion.Task;
    }

    public void ClearPending()
    {
        Pending[] pending;
        lock (_gate)
        {
            pending = _pending.ToArray(); _pending.Clear(); _recent.Clear(); _activeCancellation?.Cancel();
        }
        foreach (var item in pending) item.Completion.TrySetResult(Failure(item.Request, "cleared"));
    }

    private async Task ProcessQueueAsync()
    {
        while (true)
        {
            try { await _available.WaitAsync(_lifetime.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
            Pending? item; CancellationTokenSource cancellation;
            lock (_gate)
            {
                item = _pending.OrderByDescending(candidate => candidate.Request.Priority).ThenBy(candidate => candidate.Sequence).FirstOrDefault();
                if (item is null) continue;
                _pending.Remove(item); _active = item;
                cancellation = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token, item.CallerToken); _activeCancellation = cancellation;
            }
            VoiceResult result;
            try { result = await ProcessOneAsync(item.Request, cancellation.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) { result = Failure(item.Request, "cancelled"); }
            catch (Exception error) { result = Failure(item.Request, error.ToString()); }
            item.Completion.TrySetResult(result);
            lock (_gate) { if (ReferenceEquals(_active, item)) _active = null; if (ReferenceEquals(_activeCancellation, cancellation)) _activeCancellation = null; }
            cancellation.Dispose();
        }
    }

    private async Task<VoiceResult> ProcessOneAsync(SpeechRequest request, CancellationToken token)
    {
        if (request.ExpiresAt is { } expiry && expiry <= DateTimeOffset.UtcNow) return Failure(request, "expired");
        VoiceProfile profile;
        try { profile = _profiles.Resolve(request.ProfileId); }
        catch (Exception error) { return Failure(request, error.Message); }
        var subtitle = string.IsNullOrWhiteSpace(request.SubtitleText) ? request.Text : request.SubtitleText;
        var text = SpeechText.NormalizeForProfile(request.Text, profile, request.Pronunciations);
        if (text.Length == 0) return Failure(request, "empty", profile.Id, subtitle);
        if (text.Length > 4000) return Failure(request, "Speech line exceeds the 4000-character limit.", profile.Id, subtitle);
        if (VoiceCache.IsValidWave(request.PrerecordedPath))
            return new(true, Path.GetFullPath(request.PrerecordedPath!), null, SubtitleText: subtitle, ProfileId: profile.Id)
                { Prerecorded = true, BackendId = "prerecorded" };

        var culture = string.IsNullOrWhiteSpace(request.Culture) ? profile.Culture : request.Culture;
        if (profile.RequirePreferredBackend &&
            !string.Equals(profile.PreferredBackend, _backend.BackendId, StringComparison.OrdinalIgnoreCase))
            return Failure(request,
                $"Required voice backend '{profile.PreferredBackend}' is unavailable; active backend is '{_backend.BackendId}'. " +
                (_backend.Capabilities.Detail ?? ""), profile.Id, subtitle);
        // Keep the backend's effective voice/rate identical to its cache identity.
        var emphasis = request.Emotion is "urgent" or "concerned" ? 1 : request.Emotion is "calm" or "diplomatic" ? -1 : 0;
        profile = profile with { Culture = culture, Rate = Math.Clamp(profile.Rate + emphasis, -10, 10) };
        var voice = _backend.ResolveVoiceId(profile, culture);
        var path = _cache.PathFor(profile, text, _backend.BackendId, _backend.Model, _backend.Version, voice, culture);
        if (request.CachePolicy == SpeechCachePolicy.UseCache && _cache.TryGetValid(path))
            return new(true, path, null, true, voice, subtitle, profile.Id) { BackendId = _backend.BackendId };
        if (request.CachePolicy == SpeechCachePolicy.UseCache && !_backend.Capabilities.Available &&
            _cache.TryGetFallback(profile, text, _backend.BackendId, _backend.Model, _backend.Version, culture, out var fallback))
            return new(true, fallback, null, true, null, subtitle, profile.Id) { BackendId = _backend.BackendId };
        if (!request.AllowSynthesis || !_settings.EnableVoices) return Failure(request, "synthesis disabled", profile.Id, subtitle);
        if (_settings.OfflineOnly && !_backend.Capabilities.Offline) return Failure(request, "Online synthesis is disabled by Offline Only settings.", profile.Id, subtitle);
        if (!_backend.Capabilities.Available) return Failure(request, _backend.Capabilities.Detail ?? "speech unavailable", profile.Id, subtitle);

        token.ThrowIfCancellationRequested();
        var started = DateTime.UtcNow;
        var temporary = path + "." + Guid.NewGuid().ToString("N", CultureInfo.InvariantCulture) + ".tmp";
        try
        {
            await _backend.SynthesizeAsync(profile, text, temporary, token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            if (!VoiceCache.IsValidWave(temporary)) throw new InvalidDataException("Speech backend produced an invalid WAV file.");
            var output = path;
            if (request.CachePolicy == SpeechCachePolicy.NoCache) output = Path.Combine(_cache.DirectoryPath, "transient-" + Guid.NewGuid().ToString("N") + ".wav");
            File.Move(temporary, output, true);
            if (request.CachePolicy != SpeechCachePolicy.NoCache)
                _cache.RememberFallback(profile, text, _backend.BackendId, _backend.Model, _backend.Version, culture, output);
            _cache.Trim();
            return new(true, output, null, false, voice, subtitle, profile.Id)
                { BackendId = _backend.BackendId, SynthesisDuration = DateTime.UtcNow - started };
        }
        finally { try { if (File.Exists(temporary)) File.Delete(temporary); } catch (IOException) { } }
    }

    private static async Task<VoiceResult> AwaitCallerAsync(Task<VoiceResult> task, CancellationToken token)
    {
        var signal = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var registration = token.Register(() => signal.TrySetResult(true));
        if (task != await Task.WhenAny(task, signal.Task).ConfigureAwait(false)) throw new OperationCanceledException(token);
        return await task.ConfigureAwait(false);
    }
    private static VoiceResult Failure(SpeechRequest request, string error, string? profile = null, string? subtitle = null) =>
        new(false, null, error, SubtitleText: subtitle ?? request.SubtitleText ?? request.Text, ProfileId: profile ?? request.ProfileId);
    private void PruneRecent(DateTimeOffset now)
    {
        foreach (var key in _recent.Where(pair => now - pair.Value.At > pair.Value.Cooldown).Select(pair => pair.Key).ToArray()) _recent.Remove(key);
        foreach (var key in _recent.OrderBy(pair => pair.Value.At).Take(Math.Max(0, _recent.Count - RecentLimit)).Select(pair => pair.Key).ToArray()) _recent.Remove(key);
    }
    public async ValueTask DisposeAsync()
    {
        Pending[] pending;
        lock (_gate)
        {
            if (_disposed) return;
            _disposed = true; pending = _pending.ToArray(); _pending.Clear(); _activeCancellation?.Cancel(); _lifetime.Cancel();
        }
        foreach (var item in pending) item.Completion.TrySetResult(Failure(item.Request, "disposed"));
        try { await _worker.ConfigureAwait(false); } catch (OperationCanceledException) { }
        if (_backend is IAsyncDisposable asyncBackend) await asyncBackend.DisposeAsync().ConfigureAwait(false);
        else if (_backend is IDisposable backend) backend.Dispose();
        _available.Dispose(); _lifetime.Dispose();
    }
}
