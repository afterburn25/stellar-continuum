using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Game.Presentation.Audio.Voice;

public sealed class VoiceCache
{
    public const string ProcessingVersion = "voice-dsp-v2";
    private readonly string _directory;
    private readonly long _limitBytes;
    private readonly object _indexGate = new();
    private string FallbackIndexPath => Path.Combine(_directory, "fallback-index.json");
    public string DirectoryPath => _directory;
    public VoiceCache(string directory, long limitBytes = 256L * 1024 * 1024)
    {
        if (limitBytes < 48) throw new ArgumentOutOfRangeException(nameof(limitBytes));
        _directory = Path.GetFullPath(directory); _limitBytes = limitBytes; Directory.CreateDirectory(_directory);
    }
    public string PathFor(VoiceProfile profile, string normalized) =>
        PathFor(profile, normalized, "legacy", "", "1", profile.PreferredVoice ?? "", profile.Culture);
    public string PathFor(VoiceProfile profile, string normalized, string backend, string model,
        string version, string voice, string culture)
    {
        return Path.Combine(_directory, Hash(Identity(profile, normalized, backend, model, version, voice, culture)) + ".wav");
    }

    /// <summary>Records a voice-independent lookup only for an engine-created, validated WAV.
    /// The index stores a hash filename, never an arbitrary or absolute path.</summary>
    public void RememberFallback(VoiceProfile profile, string normalized, string backend, string model,
        string version, string culture, string wavPath)
    {
        try
        {
            if (!TrySafeCacheFile(wavPath, out var safePath, out var fileName) || !IsValidWave(safePath)) return;
            lock (_indexGate)
            {
                var index = LoadIndex();
                index[FallbackKey(profile, normalized, backend, model, version, culture)] = fileName;
                PruneIndex(index); SaveIndex(index);
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
    }

    public bool TryGetFallback(VoiceProfile profile, string normalized, string backend, string model,
        string version, string culture, out string path)
    {
        path = string.Empty;
        try
        {
            lock (_indexGate)
            {
                var index = LoadIndex();
                var key = FallbackKey(profile, normalized, backend, model, version, culture);
                if (!index.TryGetValue(key, out var fileName) || !TrySafeCacheFile(fileName, out var candidate, out _))
                    return false;
                if (!TryGetValid(candidate)) { index.Remove(key); SaveIndex(index); return false; }
                path = candidate; return true;
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { return false; }
    }
    public bool TryGetValid(string path)
    {
        if (!IsValidWave(path)) { try { if (File.Exists(path)) File.Delete(path); } catch (IOException) { } return false; }
        try { File.SetLastAccessTimeUtc(path, DateTime.UtcNow); } catch (IOException) { }
        return true;
    }
    public static bool IsValidWave(string? path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path) || new FileInfo(path).Length < 48) return false;
            using var stream = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var reader = new BinaryReader(stream);
            if (Encoding.ASCII.GetString(reader.ReadBytes(4)) != "RIFF") return false;
            var length = (long)reader.ReadUInt32() + 8;
            if (length > stream.Length || Encoding.ASCII.GetString(reader.ReadBytes(4)) != "WAVE") return false;
            var formatValid = false; var dataValid = false; ushort alignment = 0;
            while (stream.Position + 8 <= length)
            {
                var id = Encoding.ASCII.GetString(reader.ReadBytes(4)); var size = reader.ReadUInt32();
                var next = stream.Position + size;
                if (next > length) return false;
                if (id == "fmt ")
                {
                    if (size < 16) return false;
                    var format = reader.ReadUInt16(); var channels = reader.ReadUInt16(); var rate = reader.ReadUInt32();
                    var byteRate = reader.ReadUInt32(); alignment = reader.ReadUInt16(); var bits = reader.ReadUInt16();
                    formatValid = format == 1 && channels is 1 or 2 && rate is >= 8000 and <= 192000 &&
                        bits is 8 or 16 && alignment == channels * bits / 8 && byteRate == rate * alignment;
                }
                if (id == "data") dataValid = size > 0 && alignment > 0 && size % alignment == 0;
                stream.Position = next + (size & 1);
            }
            return formatValid && dataValid;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { return false; }
    }
    public void Trim()
    {
        lock (_indexGate)
        {
            FileInfo[] files; try { files = new DirectoryInfo(_directory).GetFiles("*.wav"); } catch (IOException) { return; }
            long total = files.Sum(file => file.Length);
            foreach (var file in files.OrderBy(file => file.LastAccessTimeUtc).ThenBy(file => file.Name, StringComparer.Ordinal))
            {
                if (total <= _limitBytes) break;
                try { total -= file.Length; file.Delete(); } catch (IOException) { }
            }
            try { var index = LoadIndex(); if (PruneIndex(index)) SaveIndex(index); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
        }
    }

    private static string Identity(VoiceProfile profile, string normalized, string backend, string model,
        string version, string voice, string culture)
    {
        var dsp = profile.Dsp;
        return FormattableString.Invariant($"v4|{ProcessingVersion}|{profile.Id}|{backend}|{model}|{version}|{voice}|{culture}|{profile.Rate:R}|{profile.Pitch:R}|{profile.Radio}|{profile.Synthetic}|{profile.Resonance:R}|{profile.Chorus:R}|{profile.Reverb:R}|{dsp.FormantShift:R}|{dsp.Reverb:R}|{dsp.Chorus:R}|{dsp.HarmonicLayer:R}|{dsp.WhisperLayer:R}|{dsp.LowFrequencyResonance:R}|{dsp.HighFrequencyShimmer:R}|{dsp.Distortion:R}|{dsp.RadioAmount:R}|{dsp.SyntheticAmount:R}|{dsp.AlienAmount:R}|{normalized}");
    }
    private static string Hash(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value))).ToLowerInvariant();
    private static string FallbackKey(VoiceProfile profile, string normalized, string backend, string model,
        string version, string culture) => Hash(Identity(profile, normalized, backend, model, version, "", culture));
    private Dictionary<string,string> LoadIndex()
    {
        try
        {
            if (!File.Exists(FallbackIndexPath) || new FileInfo(FallbackIndexPath).Length > 1024 * 1024)
                return new(StringComparer.Ordinal);
            return JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(FallbackIndexPath)) is { Count: <= 4096 } values
                ? new(values, StringComparer.Ordinal) : new(StringComparer.Ordinal);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { return new(StringComparer.Ordinal); }
    }
    private void SaveIndex(Dictionary<string,string> index)
    {
        var temporary = FallbackIndexPath + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(index.OrderBy(pair => pair.Key)
                .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal)));
            File.Move(temporary, FallbackIndexPath, true);
        }
        finally { try { if (File.Exists(temporary)) File.Delete(temporary); } catch (IOException) { } }
    }
    private bool PruneIndex(Dictionary<string,string> index)
    {
        var stale = index.Where(pair => !TrySafeCacheFile(pair.Value, out var path, out _) || !IsValidWave(path))
            .Select(pair => pair.Key).ToArray();
        foreach (var key in stale) index.Remove(key);
        return stale.Length > 0;
    }
    private bool TrySafeCacheFile(string value, out string path, out string fileName)
    {
        fileName = Path.GetFileName(value); path = string.Empty;
        if (!string.Equals(value, fileName, StringComparison.Ordinal) &&
            !string.Equals(Path.GetFullPath(value), Path.Combine(_directory, fileName), StringComparison.OrdinalIgnoreCase)) return false;
        if (fileName.Length != 68 || !fileName.EndsWith(".wav", StringComparison.OrdinalIgnoreCase) ||
            !fileName.Take(64).All(Uri.IsHexDigit)) return false;
        path = Path.Combine(_directory, fileName); return true;
    }
}
