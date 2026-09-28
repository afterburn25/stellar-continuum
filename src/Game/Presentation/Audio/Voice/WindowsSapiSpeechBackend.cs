using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace Game.Presentation.Audio.Voice;

/// <summary>Offline Windows SAPI adapter. All COM access is confined to STA threads and every
/// RCW is explicitly released. Output is 22.05 kHz, 16-bit mono PCM WAV.</summary>
public sealed class WindowsSapiSpeechBackend : IVoiceSpeechBackend, IDisposable
{
    private sealed record InstalledVoice(string Id, string Description, string Gender, string Culture);
    private sealed record Work(VoiceProfile Profile, string Text, string Path, string VoiceId,
        CancellationToken Token, TaskCompletionSource<bool> Completion);
    private readonly BlockingCollection<Work> _work = new(8);
    private readonly IReadOnlyList<InstalledVoice> _voices;
    private readonly Thread? _thread;
    private int _disposed;
    public SpeechBackendCapabilities Capabilities { get; }
    public string BackendId => "windows-sapi";
    public string Model => "SAPI.SpVoice";
    public string Version => Environment.OSVersion.VersionString;

    public WindowsSapiSpeechBackend(string? fallbackDetail = null)
    {
        if (!OperatingSystem.IsWindows())
        {
            _voices = Array.Empty<InstalledVoice>();
            Capabilities = new(false, Array.Empty<string>(), "Windows SAPI is unavailable on this platform.") { BackendId = BackendId };
            return;
        }
        _voices = DiscoverVoicesOnSta();
        Capabilities = new(_voices.Count > 0, _voices.Select(voice => voice.Description).ToArray(),
            _voices.Count == 0 ? "No installed SAPI voices were found." : fallbackDetail)
        {
            BackendId = BackendId, Offline = true, Languages = _voices.Select(voice => voice.Culture)
                .Where(value => !string.IsNullOrWhiteSpace(value)).Distinct(StringComparer.OrdinalIgnoreCase).ToArray(),
            SampleRate = 22050, OutputFormat = "PCM WAV", CpuRequirement = "Windows SAPI 5", EstimatedLatencyMilliseconds = 180,
        };
        if (!Capabilities.Available) return;
        _thread = new Thread(Worker) { IsBackground = true, Name = "Stellar SAPI speech worker" };
        _thread.SetApartmentState(ApartmentState.STA); _thread.Start();
    }

    public string ResolveVoiceId(VoiceProfile profile, string culture)
    {
        if (_voices.Count == 0) return string.Empty;
        var preferred = profile.PreferredVoice;
        if (!string.IsNullOrWhiteSpace(preferred))
        {
            var match = _voices.FirstOrDefault(voice => string.Equals(voice.Id, preferred, StringComparison.OrdinalIgnoreCase) ||
                string.Equals(voice.Description, preferred, StringComparison.OrdinalIgnoreCase) ||
                voice.Description.Contains(preferred, StringComparison.OrdinalIgnoreCase));
            if (match is not null) return match.Id;
        }
        var desiredGender = profile.Sex.Length > 0 ? profile.Sex : profile.Presentation.Contains("female", StringComparison.OrdinalIgnoreCase) ? "female" :
            profile.Presentation.Contains("male", StringComparison.OrdinalIgnoreCase) ? "male" : string.Empty;
        return _voices.OrderByDescending(voice => !string.IsNullOrEmpty(desiredGender) && string.Equals(voice.Gender, desiredGender, StringComparison.OrdinalIgnoreCase))
            .ThenByDescending(voice => !string.IsNullOrEmpty(culture) && voice.Culture.StartsWith(culture, StringComparison.OrdinalIgnoreCase))
            .ThenBy(voice => voice.Description, StringComparer.OrdinalIgnoreCase).First().Id;
    }

    public async Task SynthesizeAsync(VoiceProfile profile, string text, string wavPath, CancellationToken cancellationToken)
    {
        if (!Capabilities.Available) throw new PlatformNotSupportedException(Capabilities.Detail);
        if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(WindowsSapiSpeechBackend));
        cancellationToken.ThrowIfCancellationRequested();
        var completion = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        try { _work.Add(new(profile, text, wavPath, ResolveVoiceId(profile, profile.Culture), cancellationToken, completion), cancellationToken); }
        catch (InvalidOperationException) { throw new ObjectDisposedException(nameof(WindowsSapiSpeechBackend)); }
        await completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private void Worker()
    {
        try
        {
            foreach (var item in _work.GetConsumingEnumerable())
            {
                try
                {
                    item.Token.ThrowIfCancellationRequested(); SynthesizeOnSta(item); item.Token.ThrowIfCancellationRequested();
                    item.Completion.TrySetResult(true);
                }
                catch (OperationCanceledException)
                {
                    try { if (File.Exists(item.Path)) File.Delete(item.Path); } catch (IOException) { }
                    item.Completion.TrySetCanceled(item.Token);
                }
                catch (Exception error) { item.Completion.TrySetException(error); }
            }
        }
        finally { _work.Dispose(); }
    }

    [SupportedOSPlatform("windows")]
    private static IReadOnlyList<InstalledVoice> DiscoverVoicesOnSta()
    {
        IReadOnlyList<InstalledVoice> result = Array.Empty<InstalledVoice>();
        var thread = new Thread(() => result = DiscoverVoices()) { IsBackground = true, Name = "Stellar SAPI discovery" };
        thread.SetApartmentState(ApartmentState.STA); thread.Start();
        return thread.Join(TimeSpan.FromSeconds(10)) ? result : Array.Empty<InstalledVoice>();
    }

    [SupportedOSPlatform("windows")]
    private static IReadOnlyList<InstalledVoice> DiscoverVoices()
    {
        object? voiceObject = null, voicesObject = null;
        var result = new List<InstalledVoice>();
        try
        {
            voiceObject = Activator.CreateInstance(Type.GetTypeFromProgID("SAPI.SpVoice")!);
            dynamic voice = voiceObject!; voicesObject = voice.GetVoices(); dynamic voices = voicesObject;
            for (var index = 0; index < (int)voices.Count; index++)
            {
                object? tokenObject = null;
                try
                {
                    tokenObject = voices.Item(index); dynamic token = tokenObject;
                    result.Add(new((string)token.Id, (string)token.GetDescription(), SafeAttribute(token, "Gender"),
                        ParseLanguage(SafeAttribute(token, "Language"))));
                }
                finally { Release(tokenObject); }
            }
        }
        catch { return Array.Empty<InstalledVoice>(); }
        finally { Release(voicesObject); Release(voiceObject); }
        return result.OrderBy(item => item.Description, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    private static string SafeAttribute(dynamic token, string name) { try { return (string)token.GetAttribute(name); } catch { return string.Empty; } }

    private static string ParseLanguage(string value)
    {
        var first = value.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).FirstOrDefault();
        if (string.IsNullOrWhiteSpace(first)) return string.Empty;
        try { return CultureInfo.GetCultureInfo(Convert.ToInt32(first, 16)).Name; }
        catch (Exception) when (first.Length > 0) { return value; }
    }

    private static void SynthesizeOnSta(Work item)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
        object? voiceObject = null, streamObject = null, formatObject = null, voicesObject = null, tokenObject = null;
        // SpFileStream still rejects long Win32 paths. Generate at a short WAV path and
        // move only after SAPI has closed it; the cache itself may live under a deep user://.
        var sapiPath = Path.Combine(Path.GetTempPath(), "stellar-voice-" + Guid.NewGuid().ToString("N") + ".wav");
        try
        {
            voiceObject = Activator.CreateInstance(Type.GetTypeFromProgID("SAPI.SpVoice")!);
            streamObject = Activator.CreateInstance(Type.GetTypeFromProgID("SAPI.SpFileStream")!);
            formatObject = Activator.CreateInstance(Type.GetTypeFromProgID("SAPI.SpAudioFormat")!);
            dynamic voice = voiceObject!, stream = streamObject!, format = formatObject!;
            voicesObject = voice.GetVoices(); dynamic voices = voicesObject;
            for (var index = 0; index < (int)voices.Count; index++)
            {
                object candidateObject = voices.Item(index); dynamic candidate = candidateObject;
                if (string.Equals((string)candidate.Id, item.VoiceId, StringComparison.OrdinalIgnoreCase)) { tokenObject = candidateObject; break; }
                Release(candidateObject);
            }
            if (tokenObject is not null) voice.Voice = tokenObject;
            voice.Rate = Math.Clamp((int)Math.Round(item.Profile.Rate), -10, 10);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(item.Path))!);
            format.Type = 22; // SAFT22kHz16BitMono
            stream.Format = format; stream.Open(sapiPath, 3, false); voice.AudioOutputStream = stream;
            // SAPI must remain on this STA until asynchronous synthesis has drained into
            // the file stream. IsNotXml prevents authored '<' characters from becoming SSML.
            voice.Speak(item.Text, 1 | 16); // SVSFlagsAsync | SVSFIsNotXML
            var elapsed = Stopwatch.StartNew();
            while (!(bool)voice.WaitUntilDone(50))
            {
                if (item.Token.IsCancellationRequested)
                {
                    try { voice.Speak(string.Empty, 1 | 2 | 16); } catch { } // async + purge
                    item.Token.ThrowIfCancellationRequested();
                }
                if (elapsed.Elapsed >= TimeSpan.FromSeconds(30))
                {
                    try { voice.Speak(string.Empty, 1 | 2 | 16); } catch { }
                    throw new TimeoutException("Windows SAPI synthesis exceeded 30 seconds.");
                }
            }
            stream.Close();
            if (!VoiceCache.IsValidWave(sapiPath)) throw new InvalidDataException("SAPI did not produce a valid PCM WAV file.");
            File.Move(sapiPath, item.Path, true);
        }
        finally
        {
            if (streamObject is not null) { try { ((dynamic)streamObject).Close(); } catch { } }
            Release(tokenObject); Release(voicesObject); Release(formatObject); Release(streamObject); Release(voiceObject);
            try { if (File.Exists(sapiPath)) File.Delete(sapiPath); } catch (IOException) { }
        }
    }

    private static void Release(object? value)
    {
        if (!OperatingSystem.IsWindows()) return;
        if (value is not null && Marshal.IsComObject(value)) try { Marshal.FinalReleaseComObject(value); } catch { }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        while (_work.TryTake(out var pending))
            pending.Completion.TrySetException(new ObjectDisposedException(nameof(WindowsSapiSpeechBackend)));
        _work.CompleteAdding();
        if (_thread?.IsAlive == true) _thread.Join(TimeSpan.FromSeconds(10));
        if (_thread is null) _work.Dispose();
    }
}

public static class VoiceBackendFactory
{
    public static IVoiceSpeechBackend CreateOffline()
    {
        var neural = new OfflineNeuralSpeechBackend();
        if (neural.Capabilities.Available) return neural;
        var neuralFailure = neural.Capabilities.Detail ?? "The local neural voice pack is unavailable.";
        neural.Dispose();
        return new WindowsSapiSpeechBackend("Neural voice unavailable: " + neuralFailure +
            " Windows system speech is active only for profiles that permit fallback.");
    }
}
