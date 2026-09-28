using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Game.Presentation.Audio.Voice;

/// <summary>Manifest-gated local neural worker. It never downloads assets, invokes a shell,
/// or silently substitutes another provider after it is selected.</summary>
public sealed class OfflineNeuralSpeechBackend : IVoiceSpeechBackend, IDisposable, IAsyncDisposable
{
    private const int MaximumProtocolLineCharacters = 64 * 1024;
    private const int MaximumStderrLineCharacters = 4096;
    private const int MaximumStderrTailLines = 128;
    private static readonly HashSet<string> CanonicalVoices = new(StringComparer.Ordinal)
    {
        "af_heart", "af_bella", "af_kore", "af_sarah", "af_aoede", "af_nova", "af_nicole", "af_sky",
        "am_adam", "am_fenrir", "am_michael", "am_puck",
        "bf_emma", "bf_isabella", "bm_george", "bm_lewis",
    };

    private sealed record Pack
    {
        public int SchemaVersion { get; init; }
        public string? PythonPath { get; init; }
        public string? WorkerPath { get; init; }
        public string? ModelPath { get; init; }
        public string? VoicesPath { get; init; }
        public string? ModelSha256 { get; init; }
        public string? VoicesSha256 { get; init; }
        public string? Version { get; init; }
        public string[]? Voices { get; init; }
        public string WorkingDirectory { get; init; } = string.Empty;
    }

    private readonly Pack? _pack;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly object _processLock = new();
    private readonly object _stderrLock = new();
    private readonly Queue<string> _stderrTail = new();
    private readonly TimeSpan _startupTimeout;
    private readonly TimeSpan _requestTimeout;
    private Process? _process;
    private StreamWriter? _stdin;
    private BoundedLineReader? _stdout;
    private CancellationTokenSource? _stderrCancellation;
    private Task? _stderrTask;
    private int _disposed;

    public SpeechBackendCapabilities Capabilities { get; private set; }
    public string BackendId => "offline-neural";
    public string Model => "kokoro";
    public string Version => _pack?.Version ?? "unavailable";
    public IReadOnlyList<string> StderrTail { get { lock (_stderrLock) return _stderrTail.ToArray(); } }

    public OfflineNeuralSpeechBackend(string? manifestPath = null,
        TimeSpan? startupTimeout = null, TimeSpan? requestTimeout = null)
    {
        _startupTimeout = BoundedTimeout(startupTimeout, TimeSpan.FromSeconds(45));
        _requestTimeout = BoundedTimeout(requestTimeout, TimeSpan.FromSeconds(45));
        _pack = Load(manifestPath, out var error);
        Capabilities = _pack is null
            ? Unavailable(error)
            : new(true, _pack.Voices!, "Local neural pack validated; worker starts on first synthesis.")
            {
                BackendId = BackendId,
                Offline = true,
                Languages = new[] { "en-US", "en-GB" },
                SampleRate = 24000,
                OutputFormat = "PCM WAV",
                CpuRequirement = "local Python voice pack",
            };
    }

    public string ResolveVoiceId(VoiceProfile profile, string culture)
    {
        if (_pack is null || !TryCulture(culture, out var prefix)) return string.Empty;
        var desired = profile.NeuralVoice;
        if (!string.IsNullOrWhiteSpace(desired) && CanonicalVoices.Contains(desired) &&
            desired.StartsWith(prefix, StringComparison.Ordinal) && _pack.Voices!.Contains(desired, StringComparer.Ordinal))
            return desired;
        var sex = string.Equals(profile.Sex, "male", StringComparison.OrdinalIgnoreCase) ? 'm' : 'f';
        var preferred = prefix + sex + (prefix == "a" ? (sex == 'm' ? "_michael" : "_heart") :
            (sex == 'm' ? "_george" : "_emma"));
        if (_pack.Voices!.Contains(preferred, StringComparer.Ordinal)) return preferred;
        return _pack.Voices!.FirstOrDefault(voice => voice.StartsWith(prefix + sex, StringComparison.Ordinal)) ?? string.Empty;
    }

    public async Task SynthesizeAsync(VoiceProfile profile, string text, string wavPath, CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        if (_pack is null) throw new InvalidOperationException(Capabilities.Detail);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _lifetime.Token);
        var token = linked.Token;
        await _gate.WaitAsync(token).ConfigureAwait(false);
        string? workerOutput = null;
        try
        {
            ThrowIfDisposed();
            var voice = ResolveVoiceId(profile, profile.Culture);
            if (voice.Length == 0)
                throw new NotSupportedException($"Neural voice pack does not support culture '{profile.Culture}' and profile '{profile.Id}'.");
            await EnsureStartedAsync(token).ConfigureAwait(false);
            var id = Guid.NewGuid().ToString("N", CultureInfo.InvariantCulture);
            var destination = Path.GetFullPath(wavPath);
            Directory.CreateDirectory(Path.GetDirectoryName(destination) ?? throw new InvalidDataException("WAV output has no directory."));
            // The worker accepts WAV paths only. VoiceEngine deliberately gives backends a .tmp
            // destination so an interrupted render can never be mistaken for a cache entry.
            workerOutput = destination + ".worker-" + id + ".wav";
            var request = JsonSerializer.Serialize(new
            {
                id,
                text,
                voice,
                speed = Math.Clamp(1 + profile.Rate * .04f, .7f, 1.35f),
                outputPath = workerOutput
            });
            await _stdin!.WriteLineAsync(request).WaitAsync(_requestTimeout, token).ConfigureAwait(false);
            await _stdin.FlushAsync(token).WaitAsync(_requestTimeout, token).ConfigureAwait(false);
            var line = await _stdout!.ReadLineAsync(MaximumProtocolLineCharacters, false, token)
                .WaitAsync(_requestTimeout, token).ConfigureAwait(false);
            using var result = JsonDocument.Parse(line ?? throw WorkerFailure("Neural worker closed stdout."));
            var root = result.RootElement;
            var validId = root.TryGetProperty("id", out var responseId) && responseId.ValueKind == JsonValueKind.String && responseId.GetString() == id;
            var succeeded = root.TryGetProperty("ok", out var ok) && ok.ValueKind is JsonValueKind.True or JsonValueKind.False && ok.GetBoolean();
            if (!validId || !succeeded)
            {
                var reason = root.TryGetProperty("error", out var workerError) && workerError.ValueKind == JsonValueKind.String
                    ? workerError.GetString() : null;
                throw WorkerFailure(reason ?? "Malformed neural worker response.");
            }
            if (!VoiceCache.IsValidWave(workerOutput)) throw WorkerFailure("Neural worker produced invalid WAV.");
            File.Move(workerOutput, destination, true);
            workerOutput = null;
        }
        catch (Exception) when (_lifetime.IsCancellationRequested)
        {
            await StopAsync().ConfigureAwait(false);
            throw new OperationCanceledException(_lifetime.Token);
        }
        catch
        {
            await StopAsync().ConfigureAwait(false);
            throw;
        }
        finally
        {
            if (workerOutput is not null)
                try { File.Delete(workerOutput); } catch (IOException) { }
            _gate.Release();
        }
    }

    private async Task EnsureStartedAsync(CancellationToken token)
    {
        lock (_processLock) if (_process is { HasExited: false }) return;
        if (_pack is null) throw new InvalidOperationException(Capabilities.Detail);
        var start = new ProcessStartInfo(_pack.PythonPath!)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardInputEncoding = new UTF8Encoding(false),
            StandardOutputEncoding = new UTF8Encoding(false),
            StandardErrorEncoding = new UTF8Encoding(false),
            WorkingDirectory = _pack.WorkingDirectory,
        };
        start.ArgumentList.Add("-u"); start.ArgumentList.Add(_pack.WorkerPath!);
        start.ArgumentList.Add("--model"); start.ArgumentList.Add(_pack.ModelPath!);
        start.ArgumentList.Add("--voices"); start.ArgumentList.Add(_pack.VoicesPath!);
        var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start neural worker.");
        var stdin = process.StandardInput;
        var stdout = new BoundedLineReader(process.StandardOutput);
        var stderrCancellation = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
        var stderrTask = DrainStderrAsync(new BoundedLineReader(process.StandardError), stderrCancellation.Token);
        var rejected = false;
        lock (_processLock)
        {
            rejected = Volatile.Read(ref _disposed) != 0;
            if (!rejected)
            {
                _process = process; _stdin = stdin; _stdout = stdout;
                _stderrCancellation = stderrCancellation; _stderrTask = stderrTask;
            }
        }
        if (rejected)
        {
            stderrCancellation.Cancel();
            try { if (!process.HasExited) process.Kill(true); } catch { }
            try { process.StandardInput.Dispose(); process.StandardOutput.Dispose(); process.StandardError.Dispose(); } catch { }
            try { await stderrTask.WaitAsync(TimeSpan.FromSeconds(2)).ConfigureAwait(false); } catch { }
            stderrCancellation.Dispose();
            process.Dispose();
            throw new ObjectDisposedException(nameof(OfflineNeuralSpeechBackend));
        }
        try
        {
            var line = await stdout.ReadLineAsync(MaximumProtocolLineCharacters, false, token)
                .WaitAsync(_startupTimeout, token).ConfigureAwait(false);
            using var ready = JsonDocument.Parse(line ?? throw WorkerFailure("Neural worker closed before readiness."));
            if (!ready.RootElement.TryGetProperty("ready", out var value) || value.ValueKind != JsonValueKind.True)
                throw WorkerFailure("Malformed neural worker readiness response.");
            Capabilities = Capabilities with { Detail = "Local neural worker ready." };
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            // Cancellation belongs to this request (or disposal), not to the validated pack.
            // Keeping it available lets the next explicit request start a fresh worker.
            if (!_lifetime.IsCancellationRequested)
                Capabilities = Capabilities with { Detail = "Neural worker startup was canceled; the local pack remains available." };
            throw;
        }
        catch (Exception error)
        {
            Capabilities = Unavailable("Neural worker startup failed: " + error.Message + TailSuffix());
            throw;
        }
    }

    private async Task DrainStderrAsync(BoundedLineReader stderr, CancellationToken token)
    {
        try
        {
            while (await stderr.ReadLineAsync(MaximumStderrLineCharacters, true, token).ConfigureAwait(false) is { } line)
            {
                lock (_stderrLock)
                {
                    if (_stderrTail.Count == MaximumStderrTailLines) _stderrTail.Dequeue();
                    _stderrTail.Enqueue(line);
                }
            }
        }
        catch (Exception error) when (error is OperationCanceledException or IOException or ObjectDisposedException) { }
    }

    private sealed class BoundedLineReader(StreamReader reader)
    {
        private const int BufferSize = 4096;
        private readonly char[] _buffer = new char[BufferSize];
        private int _offset;
        private int _count;

        public async Task<string?> ReadLineAsync(int maximum, bool truncate, CancellationToken token)
        {
            var builder = new StringBuilder(Math.Min(maximum, BufferSize));
            var overflow = false;
            while (true)
            {
                if (_offset == _count)
                {
                    _count = await reader.ReadAsync(_buffer.AsMemory(), token).ConfigureAwait(false);
                    _offset = 0;
                    if (_count == 0)
                    {
                        if (overflow && !truncate) throw TooLong(maximum);
                        return builder.Length == 0 && !overflow ? null : Finish(builder, overflow);
                    }
                }

                var newline = Array.IndexOf(_buffer, '\n', _offset, _count - _offset);
                var end = newline >= 0 ? newline : _count;
                var available = end - _offset;
                var remaining = Math.Max(0, maximum - builder.Length);
                var take = Math.Min(available, remaining);
                if (take > 0) builder.Append(_buffer, _offset, take);
                if (take < available) overflow = true;
                _offset = newline >= 0 ? newline + 1 : end;

                if (newline < 0) continue;
                if (overflow && !truncate) throw TooLong(maximum);
                return Finish(builder, overflow);
            }
        }

        private static string Finish(StringBuilder builder, bool overflow) =>
            builder.ToString().TrimEnd('\r') + (overflow ? "…[truncated]" : string.Empty);

        private static InvalidDataException TooLong(int maximum) =>
            new($"Neural worker protocol line exceeded {maximum} characters.");
    }

    private async Task StopAsync()
    {
        Process? process; CancellationTokenSource? stderrCancellation; Task? stderrTask;
        lock (_processLock)
        {
            process = _process; stderrCancellation = _stderrCancellation; stderrTask = _stderrTask;
            _process = null; _stdin = null; _stdout = null; _stderrCancellation = null; _stderrTask = null;
        }
        try { stderrCancellation?.Cancel(); } catch (ObjectDisposedException) { }
        if (process is not null)
        {
            try { if (!process.HasExited) process.Kill(true); }
            catch (Exception error) when (error is InvalidOperationException or System.ComponentModel.Win32Exception) { }
            try { process.StandardInput.Dispose(); process.StandardOutput.Dispose(); process.StandardError.Dispose(); }
            catch (Exception error) when (error is IOException or InvalidOperationException or ObjectDisposedException) { }
        }
        if (stderrTask is not null)
            try { await stderrTask.WaitAsync(TimeSpan.FromSeconds(2)).ConfigureAwait(false); }
            catch (Exception error) when (error is TimeoutException or OperationCanceledException or IOException or ObjectDisposedException) { }
        stderrCancellation?.Dispose(); process?.Dispose();
    }

    private static Pack? Load(string? path, out string error)
    {
        if (path is null)
        {
            var configuredPath = Environment.GetEnvironmentVariable("STELLAR_VOICE_PACK");
            path = string.IsNullOrWhiteSpace(configuredPath)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "StellarContinuum", "voice-packs", "kokoro-v1", "pack.json")
                : configuredPath;
        }
        try
        {
            var fullManifest = Path.GetFullPath(path);
            var pack = JsonSerializer.Deserialize<Pack>(File.ReadAllText(fullManifest),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            if (pack is null || pack.SchemaVersion != 1 || string.IsNullOrWhiteSpace(pack.PythonPath) ||
                string.IsNullOrWhiteSpace(pack.WorkerPath) || string.IsNullOrWhiteSpace(pack.ModelPath) ||
                string.IsNullOrWhiteSpace(pack.VoicesPath) || string.IsNullOrWhiteSpace(pack.ModelSha256) ||
                string.IsNullOrWhiteSpace(pack.VoicesSha256) || string.IsNullOrWhiteSpace(pack.Version) ||
                pack.Voices is not { Length: > 0 })
                throw new InvalidDataException("Voice pack manifest has missing or unsupported values.");
            if (pack.Voices.Length > CanonicalVoices.Count || pack.Voices.Distinct(StringComparer.Ordinal).Count() != pack.Voices.Length ||
                pack.Voices.Any(voice => !CanonicalVoices.Contains(voice)))
                throw new InvalidDataException("Voice pack manifest contains a noncanonical or duplicate voice ID.");
            var packDirectory = Path.GetDirectoryName(fullManifest)!;
            // Prefer files beside the manifest. Packs installed by the packaged Codex host used
            // absolute LocalCache paths that a separately downloaded game cannot necessarily
            // open, even though the complete pack also exists in its public LocalAppData folder.
            pack = pack with
            {
                PythonPath = ResolvePackFile(packDirectory, pack.PythonPath,
                    OperatingSystem.IsWindows() ? "python/Scripts/python.exe" : "python/bin/python"),
                WorkerPath = ResolvePackFile(packDirectory, pack.WorkerPath, "kokoro_worker.py"),
                ModelPath = ResolvePackFile(packDirectory, pack.ModelPath, "kokoro-v1.0.onnx"),
                VoicesPath = ResolvePackFile(packDirectory, pack.VoicesPath, "voices-v1.0.bin"),
            };
            foreach (var file in new[] { pack.PythonPath, pack.WorkerPath, pack.ModelPath, pack.VoicesPath })
                if (!Path.IsPathFullyQualified(file) || !File.Exists(file)) throw new FileNotFoundException("Voice pack file unavailable.", file);
            if (!FixedTimeEquals(Hash(pack.ModelPath), pack.ModelSha256) || !FixedTimeEquals(Hash(pack.VoicesPath), pack.VoicesSha256))
                throw new InvalidDataException("Voice pack checksum mismatch.");
            error = string.Empty;
            return pack with { WorkingDirectory = packDirectory };
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or
                                           InvalidDataException or ArgumentException or NotSupportedException)
        {
            error = "Offline neural voice pack unavailable: " + exception.Message; return null;
        }
    }

    private static string ResolvePackFile(string packDirectory, string configuredPath, string installedRelativePath)
    {
        var installed = Path.GetFullPath(Path.Combine(packDirectory,
            installedRelativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (File.Exists(installed)) return installed;
        var configured = Path.IsPathFullyQualified(configuredPath)
            ? Path.GetFullPath(configuredPath)
            : Path.GetFullPath(Path.Combine(packDirectory, configuredPath));
        return configured;
    }

    private static string Hash(string file)
    {
        using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.SequentialScan);
        using var sha = SHA256.Create();
        return Convert.ToHexString(sha.ComputeHash(stream)).ToLowerInvariant();
    }
    private static bool FixedTimeEquals(string actual, string expected)
    {
        if (actual.Length != 64 || expected.Length != 64) return false;
        try { return CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actual), Convert.FromHexString(expected)); }
        catch (FormatException) { return false; }
    }
    private static bool TryCulture(string culture, out string prefix)
    {
        if (string.Equals(culture, "en-US", StringComparison.OrdinalIgnoreCase)) { prefix = "a"; return true; }
        if (string.Equals(culture, "en-GB", StringComparison.OrdinalIgnoreCase)) { prefix = "b"; return true; }
        prefix = string.Empty; return false;
    }
    private InvalidDataException WorkerFailure(string message) => new(message + TailSuffix());
    private string TailSuffix() { var tail = StderrTail; return tail.Count == 0 ? string.Empty : " Worker stderr: " + string.Join(" | ", tail.TakeLast(8)); }
    private SpeechBackendCapabilities Unavailable(string detail) => new(false, Array.Empty<string>(), detail)
    {
        BackendId = BackendId,
        Offline = true,
        Languages = new[] { "en-US", "en-GB" },
        SampleRate = 24000,
        OutputFormat = "PCM WAV",
        CpuRequirement = "local Python voice pack"
    };
    private static TimeSpan BoundedTimeout(TimeSpan? value, TimeSpan fallback) =>
        value is { } candidate && candidate >= TimeSpan.FromMilliseconds(100) && candidate <= TimeSpan.FromMinutes(2) ? candidate : fallback;
    private void ThrowIfDisposed() { if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(OfflineNeuralSpeechBackend)); }

    public void Dispose() { try { DisposeAsync().AsTask().Wait(TimeSpan.FromSeconds(5)); } catch (AggregateException) { } }
    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        _lifetime.Cancel(); await StopAsync().ConfigureAwait(false);
        await _gate.WaitAsync().ConfigureAwait(false); _gate.Release();
        _gate.Dispose(); _lifetime.Dispose();
    }
}
