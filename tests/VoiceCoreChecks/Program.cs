using System.Text;
using System.Text.Json;
using System.Diagnostics;
using Game.Presentation.Audio.Voice;

return await RunSafelyAsync();

static async Task<int> RunSafelyAsync()
{
    var passed = 0;
    try
    {
        var root = Directory.GetCurrentDirectory();
        var output = Path.Combine(root, "work", "voice-core-proof");
        if (Directory.Exists(output)) Directory.Delete(output, true);
        Directory.CreateDirectory(output);
        var registry = VoiceProfileRegistry.Load(Path.Combine(root, "data", "voice_profiles", "human.json"));
        Require(registry.All.Count == 13, "Profile registry must load baseline and species-specific translator voices.");
        foreach (var id in new[] { "human_female_fleet_commander", "human_female_chief_scientist", "human_female_diplomat",
                     "human_female_narrator", "human_male_fleet_commander", "human_male_governor", "ship_computer",
                     "human_operations_officer", "grey_diplomat" }) Require(registry.Resolve(id).Id == id, $"Missing profile {id}.");
        var grey = registry.Resolve("grey_diplomat");
        Require(grey.Dsp.AlienAmount > .5f && grey.Dsp.HarmonicLayer is > 0 and < .25f && grey.Dsp.Distortion < .05f,
            "Grey processing must be recognizable, restrained and intelligible.");
        Require(new[] { "human_female_fleet_commander", "human_female_chief_scientist", "human_female_diplomat", "human_female_narrator" }
                .Select(id => registry.Resolve(id).Rate).Distinct().Count() == 4,
            "Female roles need distinct SAPI cadence even when Windows has only one installed female voice.");
        var scientist = registry.Resolve("human_female_chief_scientist");
        Require(registry.All.Where(p => p.Id.StartsWith("human_female_", StringComparison.Ordinal))
                .Select(p => p.NeuralVoice).Distinct().Count() == 4,
            "The four female production roles must retain distinct neural voices.");
        Require(scientist.Sex == "female" && scientist.Culture == "en-GB" && scientist.NeuralVoice == "bf_emma" &&
                scientist.PreferredVoice == "Microsoft Hazel" && scientist.RequirePreferredBackend,
            "Chief scientist must use the configured female British English voice route.");
        Pass("profiles-and-grey-dsp", ref passed);

        var normalized = SpeechText.Normalize("FTL-01 approaching Alpha Centauri at 0.42 c, coordinates -3.5, 8.2. Output 42% at 14:30.");
        Require(normalized.Contains("F T L zero one") && normalized.Contains("Sen-tor-eye") &&
            normalized.Contains("0.42 times the speed of light") && normalized.Contains("-3.5 by 8.2") &&
            normalized.Contains("42 percent") && normalized.Contains("14 30 hours"), "Astronomical normalization failed: " + normalized);
        Require(SpeechText.ApplyPronunciations("Sol solution SOL", new Dictionary<string, string> { ["Sol"] = "Sohl" }) ==
            "Sohl solution Sohl", "Pronunciation replacement must use word boundaries.");
        Pass("normalization-and-word-boundaries", ref passed);

        var settingsPath = Path.Combine(output, "settings.json");
        File.WriteAllText(settingsPath, "{ malformed");
        Require(VoiceSettings.Load(settingsPath) == new VoiceSettings(), "Malformed settings must fall back safely.");
        var dirty = new VoiceSettings(Volume: 4, SubtitleSize: 200, Opacity: -2, ChatterLevel: float.NaN, CommsIntensity: 8);
        dirty.Save(settingsPath); var clean = VoiceSettings.Load(settingsPath);
        Require(clean.Volume == 1 && clean.SubtitleSize == 42 && clean.Opacity == 0 && clean.ChatterLevel == 1 && clean.CommsIntensity == 1,
            "Settings were not sanitized across persistence.");
        Pass("settings-corrupt-fallback-and-sanitize", ref passed);

        var cache = new VoiceCache(Path.Combine(output, "cache"), 600);
        var commander = registry.Resolve("human_female_fleet_commander");
        var key1 = cache.PathFor(commander, "ready", "fake", "m", "1", "a", "en-US");
        var key2 = cache.PathFor(commander, "ready", "fake", "m", "1", "a", "en-US");
        var key3 = cache.PathFor(commander, "ready", "fake", "m", "2", "a", "en-US");
        Require(key1 == key2 && key1 != key3, "Cache key must be stable and include backend version.");
        File.WriteAllText(key1, "not wave"); Require(!cache.TryGetValid(key1) && !File.Exists(key1), "Invalid cached WAV was accepted.");
        for (var index = 0; index < 4; index++) WavTest.Write(Path.Combine(cache.DirectoryPath, $"trim-{index}.wav"), 200 + index * 10);
        cache.Trim(); Require(new DirectoryInfo(cache.DirectoryPath).GetFiles("*.wav").Sum(file => file.Length) <= 600, "Cache size limit was not enforced.");
        Pass("cache-identity-validation-and-pruning", ref passed);

        await VerifyQueueAsync(registry, output); Pass("priority-expiry-dedupe-clear-and-failure", ref passed);
        await VerifyOfflineFallbacksAsync(registry, output); Pass("cache-and-prerecorded-work-without-backend", ref passed);
        await VerifyVoiceIdFallbackIndexAsync(registry, output); Pass("voice-id-cache-fallback-index", ref passed);
        await VerifyRequestPoliciesAsync(registry, output); Pass("offline-policy-cache-policy-and-pronunciation-precedence", ref passed);
        await VerifyNeuralPackBoundariesAsync(registry, output); Pass("neural-pack-unavailable-and-optional-runtime", ref passed);
        VerifyDialogueRouting(registry, root); Pass("authored-routing-priority-cooldown-reset-and-variation", ref passed);
        VerifyTypedEventRouting(registry, root); Pass("typed-event-role-template-frequency-privacy-and-live-character-routing", ref passed);
        using (var installed = new WindowsSapiSpeechBackend())
        {
            var female = registry.Resolve("human_female_chief_scientist");
            var male = registry.Resolve("human_male_governor");
            var femaleVoice = installed.ResolveVoiceId(female, female.Culture);
            var maleVoice = installed.ResolveVoiceId(male, male.Culture);
            var distinct = !string.IsNullOrWhiteSpace(femaleVoice) && !string.IsNullOrWhiteSpace(maleVoice) && femaleVoice != maleVoice;
            if (installed.Capabilities.Available && (distinct || Environment.GetEnvironmentVariable("STELLAR_REQUIRE_SAPI") == "1"))
            { await VerifySapiAsync(registry, output); Pass("real-sapi-male-female-pcm", ref passed); }
            else
            {
                Require(Environment.GetEnvironmentVariable("STELLAR_REQUIRE_SAPI") != "1", installed.Capabilities.Detail ?? "SAPI is required.");
                Console.WriteLine($"VOICE_SAPI_SKIPPED installed={installed.Capabilities.Voices.Count} female={femaleVoice} male={maleVoice} detail={installed.Capabilities.Detail}");
            }
        }

        Console.WriteLine($"VOICE_CORE_CHECKS_COMPLETE passed={passed} output={output}");
        return 0;
    }
    catch (Exception error)
    {
        Console.Error.WriteLine("VOICE_CORE_CHECKS_FAILED"); Console.Error.WriteLine(error); return 1;
    }
}

static async Task VerifyQueueAsync(VoiceProfileRegistry registry, string output)
{
    var backend = new ControlledBackend();
    await using var engine = new VoiceEngine(registry, backend, new VoiceCache(Path.Combine(output, "queue-cache")));
    var blocker = engine.EnqueueAsync(new("human_operations_officer", "blocker") { DedupeKey = "blocker", Priority = 10 });
    await backend.Started.Task.WaitAsync(TimeSpan.FromSeconds(3));
    var low = engine.EnqueueAsync(new("human_operations_officer", "low") { DedupeKey = "low", Priority = 5 });
    var high = engine.EnqueueAsync(new("human_operations_officer", "high") { DedupeKey = "high", Priority = 80 });
    var expiring = engine.EnqueueAsync(new("human_operations_officer", "stale")
        { DedupeKey = "stale", Priority = 20, ExpiresAt = DateTimeOffset.UtcNow.AddMilliseconds(40) });
    var duplicate = await engine.EnqueueAsync(new("human_operations_officer", "low") { DedupeKey = "low", Priority = 5 });
    Require(!duplicate.Succeeded && duplicate.Error == "cooldown", "Duplicate suppression failed.");
    await Task.Delay(80); backend.Release.TrySetResult(true);
    Require((await blocker).Succeeded && (await high).Succeeded && (await low).Succeeded, "Queued synthesis did not complete.");
    Require(!(await expiring).Succeeded && (await expiring).Error == "expired", "Stale queued line was spoken.");
    Require(backend.Order.Take(3).SequenceEqual(new[] { "blocker", "high", "low" }), "Priority order failed: " + string.Join(',', backend.Order));

    var cancelling = new ControlledBackend();
    await using var cancelEngine = new VoiceEngine(registry, cancelling, new VoiceCache(Path.Combine(output, "cancel-cache")));
    var active = cancelEngine.EnqueueAsync(new("human_operations_officer", "cancel me") { DedupeKey = "cancel-me" });
    await cancelling.Started.Task.WaitAsync(TimeSpan.FromSeconds(3)); cancelEngine.ClearPending();
    var cancelled = await active.WaitAsync(TimeSpan.FromSeconds(3));
    Require(!cancelled.Succeeded && cancelled.Error == "cancelled", "ClearPending did not cancel active synthesis.");
}

static async Task VerifyOfflineFallbacksAsync(VoiceProfileRegistry registry, string output)
{
    var unavailable = new UnavailableBackend();
    var cache = new VoiceCache(Path.Combine(output, "offline-cache"));
    var profile = registry.Resolve("human_female_narrator");
    var text = SpeechText.Normalize("A cached line remains available offline.", profile.Pronunciations, null);
    var cachedPath = cache.PathFor(profile, text, unavailable.BackendId, unavailable.Model, unavailable.Version,
        unavailable.ResolveVoiceId(profile, profile.Culture), profile.Culture);
    WavTest.Write(cachedPath, 320);
    await using var engine = new VoiceEngine(registry, unavailable, cache);
    var cached = await engine.EnqueueAsync(new(profile.Id, "A cached line remains available offline."));
    Require(cached.Succeeded && cached.CacheHit, "Valid cached line was blocked by unavailable backend.");
    var prerecordedPath = Path.Combine(output, "prerecorded.wav"); WavTest.Write(prerecordedPath, 480);
    var prerecorded = await engine.EnqueueAsync(new(profile.Id, "A bespoke cinematic line.")
        { DedupeKey = "prerecorded", PrerecordedPath = prerecordedPath });
    Require(prerecorded.Succeeded && prerecorded.Prerecorded, "Prerecorded line was not preferred.");
    var absent = await engine.EnqueueAsync(new(profile.Id, "No source exists.") { DedupeKey = "absent" });
    Require(!absent.Succeeded && absent.Error!.Contains("unavailable"), "Backend-unavailable path did not fail gracefully.");
    var missingProfile = await engine.EnqueueAsync(new("does_not_exist", "Subtitle survives."));
    Require(!missingProfile.Succeeded && missingProfile.SubtitleText == "Subtitle survives.", "Profile failure escaped or lost subtitle.");
}

static async Task VerifyVoiceIdFallbackIndexAsync(VoiceProfileRegistry registry, string output)
{
    var directory = Path.Combine(output, "voice-id-fallback");
    var cache = new VoiceCache(directory);
    var profile = registry.Resolve("human_female_narrator");
    const string line = "Previously synthesized audio remains available without an installed voice.";
    var available = new VoiceIdentityBackend(true, "voice-id-a");
    VoiceResult created;
    await using (var engine = new VoiceEngine(registry, available, cache))
        created = await engine.EnqueueAsync(new(profile.Id, line) { DedupeKey = "voice-id-create" });
    var normalized = SpeechText.NormalizeForProfile(line, profile, null);
    var actualVoicePath = cache.PathFor(profile, normalized, available.BackendId, available.Model,
        available.Version, "voice-id-a", profile.Culture);
    Require(created.Succeeded && !created.CacheHit && created.WavePath == actualVoicePath && available.Calls == 1,
        "Available backend did not preserve its actual voice in the primary cache identity.");

    var unavailable = new VoiceIdentityBackend(false, "");
    await using (var engine = new VoiceEngine(registry, unavailable, cache))
    {
        var fallback = await engine.EnqueueAsync(new(profile.Id, line) { DedupeKey = "voice-id-fallback" });
        Require(fallback.Succeeded && fallback.CacheHit && fallback.WavePath == actualVoicePath && unavailable.Calls == 0,
            "Unavailable backend could not resolve the validated WAV created with an actual voice ID.");
    }

    const string transientLine = "A transient line must never enter the fallback index.";
    var noCacheBackend = new VoiceIdentityBackend(true, "voice-id-a");
    await using (var engine = new VoiceEngine(registry, noCacheBackend, cache))
    {
        var transient = await engine.EnqueueAsync(new(profile.Id, transientLine)
            { DedupeKey = "voice-id-transient", CachePolicy = SpeechCachePolicy.NoCache });
        Require(transient.Succeeded && !transient.CacheHit, "NoCache synthesis did not run.");
    }
    await using (var engine = new VoiceEngine(registry, unavailable, cache))
    {
        var absent = await engine.EnqueueAsync(new(profile.Id, transientLine) { DedupeKey = "voice-id-transient-fallback" });
        Require(!absent.Succeeded && unavailable.Calls == 0, "NoCache output leaked into the fallback index.");
    }

    var outside = Path.Combine(output, "outside-cache.wav"); WavTest.Write(outside, 320);
    cache.RememberFallback(profile, "outside identity", available.BackendId, available.Model, available.Version,
        profile.Culture, outside);
    Require(!cache.TryGetFallback(profile, "outside identity", available.BackendId, available.Model,
        available.Version, profile.Culture, out _), "Fallback index accepted an arbitrary path outside its cache directory.");

    File.Delete(actualVoicePath);
    Require(!cache.TryGetFallback(profile, normalized, available.BackendId, available.Model, available.Version,
        profile.Culture, out _), "Fallback index retained a pruned WAV entry.");
    var indexText = File.ReadAllText(Path.Combine(directory, "fallback-index.json"));
    Require(!indexText.Contains(Path.GetFileName(actualVoicePath), StringComparison.Ordinal),
        "Stale fallback filename remained in the bounded index.");
}

static async Task VerifyNeuralPackBoundariesAsync(VoiceProfileRegistry registry, string output)
{
    var originalPackEnvironment = Environment.GetEnvironmentVariable("STELLAR_VOICE_PACK");
    try
    {
        Environment.SetEnvironmentVariable("STELLAR_VOICE_PACK", null);
        using var defaultPack = new OfflineNeuralSpeechBackend();
        Environment.SetEnvironmentVariable("STELLAR_VOICE_PACK", "   ");
        using var whitespacePack = new OfflineNeuralSpeechBackend();
        Require(whitespacePack.Capabilities.Available == defaultPack.Capabilities.Available &&
            whitespacePack.Capabilities.Detail == defaultPack.Capabilities.Detail &&
            whitespacePack.Version == defaultPack.Version &&
            whitespacePack.Capabilities.Voices.SequenceEqual(defaultPack.Capabilities.Voices),
            "A blank STELLAR_VOICE_PACK override did not use normal default-pack discovery.");
    }
    finally
    {
        Environment.SetEnvironmentVariable("STELLAR_VOICE_PACK", originalPackEnvironment);
    }

    var absent = new OfflineNeuralSpeechBackend(Path.Combine(output, "missing-pack.json"));
    Require(!absent.Capabilities.Available && !string.IsNullOrWhiteSpace(absent.Capabilities.Detail), "Missing neural pack must be reported, not guessed.");
    var path = Path.Combine(output, "bad-pack.json"); File.WriteAllText(path, "{ malformed");
    using var malformed = new OfflineNeuralSpeechBackend(path);
    Require(!malformed.Capabilities.Available, "Malformed neural manifest must fail closed.");
    var nullPath = Path.Combine(output, "null-pack.json");
    File.WriteAllText(nullPath, "{\"schemaVersion\":1,\"pythonPath\":null,\"voices\":[null]}");
    using var nullManifest = new OfflineNeuralSpeechBackend(nullPath);
    Require(!nullManifest.Capabilities.Available, "Null neural manifest values did not fail closed.");

    var rebased = CreateFakeNeuralPack(Path.Combine(output, "downloaded-pack"), staleAbsolutePaths: true);
    await using (var downloadedBackend = new OfflineNeuralSpeechBackend(rebased.Manifest))
    {
        var wav = Path.Combine(rebased.Directory, "downloaded-path.wav");
        await downloadedBackend.SynthesizeAsync(registry.Resolve("human_female_chief_scientist"),
            "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.",
            wav, CancellationToken.None);
        Require(downloadedBackend.Capabilities.Available && VoiceCache.IsValidWave(wav),
            "A standalone game could not rebase a pack manifest installed by its packaged host.");
    }

    var fake = CreateFakeNeuralPack(output);
    await using (var backend = new OfflineNeuralSpeechBackend(fake.Manifest, TimeSpan.FromSeconds(15), TimeSpan.FromSeconds(2)))
    {
        Require(backend.Capabilities.Available && backend.ResolveVoiceId(
            registry.Resolve("human_female_narrator") with { NeuralVoice = "af_bella" }, "en-US") == "af_bella",
            "Canonical neural voice did not resolve.");
        Require(backend.ResolveVoiceId(registry.Resolve("human_female_narrator") with { Culture = "fr-FR" }, "fr-FR") == "",
            "Unsupported neural culture did not fail closed.");
        var unsupported = false;
        try { await backend.SynthesizeAsync(registry.Resolve("human_female_narrator") with { Culture = "fr-FR" },
            "unsupported", Path.Combine(fake.Directory, "unsupported.wav"), CancellationToken.None); }
        catch (NotSupportedException) { unsupported = true; }
        Require(unsupported, "Unsupported culture reached the worker.");

        var wav = Path.Combine(fake.Directory, "success.wav");
        await backend.SynthesizeAsync(registry.Resolve("human_female_narrator") with { NeuralVoice = "af_heart" },
            "normal", wav, CancellationToken.None);
        Require(VoiceCache.IsValidWave(wav), "Fake worker did not produce valid PCM.");
        Require(File.Exists(Path.Combine(fake.Directory, "working-directory.ok")), "Worker did not use the pack working directory.");
        // stdout readiness/output and stderr are independent pipes. Linux may finish the
        // tiny WAV before the background diagnostic drain receives its final line.
        var drainDeadline = Stopwatch.StartNew();
        while (drainDeadline.Elapsed < TimeSpan.FromSeconds(2) &&
               backend.StderrTail.LastOrDefault()?.Contains("stderr-299", StringComparison.Ordinal) != true)
            await Task.Delay(10);
        var stderrTail = backend.StderrTail;
        Require(stderrTail.Count == 128 && stderrTail[^1].Contains("stderr-299", StringComparison.Ordinal),
            $"Diagnostic drain did not retain its bounded tail: count={stderrTail.Count}, last={stderrTail.LastOrDefault()}");
    }

    await using (var badJson = new OfflineNeuralSpeechBackend(fake.Manifest, requestTimeout: TimeSpan.FromSeconds(2)))
    {
        var failed = false;
        try { await badJson.SynthesizeAsync(registry.Resolve("human_female_narrator"), "bad-json",
            Path.Combine(fake.Directory, "bad-json.wav"), CancellationToken.None); }
        catch (JsonException) { failed = true; }
        Require(failed, "Malformed worker JSON was accepted.");
    }

    await using (var longJson = new OfflineNeuralSpeechBackend(fake.Manifest, requestTimeout: TimeSpan.FromSeconds(2)))
    {
        var failed = false;
        try { await longJson.SynthesizeAsync(registry.Resolve("human_female_narrator"), "long-json",
            Path.Combine(fake.Directory, "long-json.wav"), CancellationToken.None); }
        catch (InvalidDataException error) when (error.Message.Contains("exceeded", StringComparison.Ordinal)) { failed = true; }
        Require(failed, "Oversized neural worker protocol line was accepted or read without a bound.");
    }

    await using (var timeout = new OfflineNeuralSpeechBackend(fake.Manifest, requestTimeout: TimeSpan.FromMilliseconds(200)))
    {
        var timedOut = false;
        try { await timeout.SynthesizeAsync(registry.Resolve("human_female_narrator"), "timeout",
            Path.Combine(fake.Directory, "timeout.wav"), CancellationToken.None); }
        catch (TimeoutException) { timedOut = true; }
        Require(timedOut, "Unresponsive worker request was not bounded.");
    }

    await using (var cancelling = new OfflineNeuralSpeechBackend(fake.Manifest, requestTimeout: TimeSpan.FromSeconds(5)))
    {
        using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(250));
        var cancelled = false;
        try { await cancelling.SynthesizeAsync(registry.Resolve("human_female_narrator"), "timeout",
            Path.Combine(fake.Directory, "cancel.wav"), cancellation.Token); }
        catch (OperationCanceledException) { cancelled = true; }
        Require(cancelled, "Neural request cancellation did not stop the worker.");
    }

    File.WriteAllText(Path.Combine(fake.Directory, "delay-startup.once"), "delay the next worker only");
    await using (var startupCancellation = new OfflineNeuralSpeechBackend(fake.Manifest,
        startupTimeout: TimeSpan.FromSeconds(15), requestTimeout: TimeSpan.FromSeconds(2)))
    {
        using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(250));
        var cancelled = false;
        try
        {
            await startupCancellation.SynthesizeAsync(registry.Resolve("human_female_narrator"), "normal",
                Path.Combine(fake.Directory, "cancel-startup.wav"), cancellation.Token);
        }
        catch (OperationCanceledException) { cancelled = true; }
        Require(cancelled && startupCancellation.Capabilities.Available,
            "Caller cancellation during startup permanently disabled a validated neural pack.");

        var recovered = Path.Combine(fake.Directory, "recovered-after-startup-cancel.wav");
        await startupCancellation.SynthesizeAsync(registry.Resolve("human_female_narrator"), "normal",
            recovered, CancellationToken.None);
        Require(startupCancellation.Capabilities.Available && VoiceCache.IsValidWave(recovered),
            "A new explicit request did not recover after startup cancellation.");
    }

    var busy = new OfflineNeuralSpeechBackend(fake.Manifest, requestTimeout: TimeSpan.FromSeconds(5));
    var busyTask = busy.SynthesizeAsync(registry.Resolve("human_female_narrator"), "timeout",
        Path.Combine(fake.Directory, "dispose.wav"), CancellationToken.None);
    await Task.Delay(200); await busy.DisposeAsync();
    var disposedSafely = false;
    try { await busyTask; }
    catch (Exception error) when (error is OperationCanceledException or IOException or InvalidDataException) { disposedSafely = true; }
    Require(disposedSafely, "Dispose while synthesizing did not terminate the active request safely.");

    var invalidVoiceManifest = CreateFakeNeuralPack(Path.Combine(output, "invalid-neural"), "invented_voice").Manifest;
    using var invalidVoice = new OfflineNeuralSpeechBackend(invalidVoiceManifest);
    Require(!invalidVoice.Capabilities.Available, "Noncanonical neural voice ID was accepted.");

    var required = Environment.GetEnvironmentVariable("STELLAR_REQUIRE_KOKORO") == "1";
    if (!required) return;
    var neural = new OfflineNeuralSpeechBackend();
    Require(neural.Capabilities.Available, neural.Capabilities.Detail ?? "Required neural pack unavailable.");
    var auditions = new[] { ("human_female_narrator", "bf_isabella", "neural-narrator.wav"),
        ("human_female_fleet_commander", "af_kore", "neural-kore.wav"),
        ("human_female_chief_scientist", "bf_emma", "neural-scientist-british.wav"),
        ("human_female_diplomat", "af_bella", "neural-bella.wav"),
        ("pelagic_translator", "bf_isabella", "neural-pelagic.wav"),
        ("compact_translator", "am_puck", "neural-compact.wav"),
        ("cryogenic_translator", "af_aoede", "neural-cryogenic.wav") };
    var neuralRegistry = new VoiceProfileRegistry(auditions.Select(item =>
        registry.Resolve(item.Item1) with { NeuralVoice = item.Item2, PreferredBackend = "offline-neural" }));
    var hashes = new HashSet<string>();
    var neuralCache = Path.Combine(output, "neural-cache");
    if (Directory.Exists(neuralCache)) Directory.Delete(neuralCache, true);
    await using var engine = new VoiceEngine(neuralRegistry, neural,
        new VoiceCache(neuralCache));
    foreach (var (profileId, voice, file) in auditions)
    {
        Require(neural.Capabilities.Voices.Contains(voice), $"Required neural pack lacks {voice}.");
        var auditionText = profileId == "human_female_chief_scientist"
            ? "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach."
            : "Sensors confirm a stable exoplanet atmosphere.";
        var request = new SpeechRequest(profileId, auditionText)
            { DedupeKey = "neural-" + voice };
        var result = await engine.EnqueueAsync(request);
        Console.WriteLine($"NEURAL_RESULT requested={voice} succeeded={result.Succeeded} cache={result.CacheHit} selected={result.SelectedVoice ?? "<null>"} path={result.WavePath ?? "<null>"} error={result.Error ?? "<null>"}");
        Require(result.Succeeded && !result.CacheHit && result.SelectedVoice == voice && VoiceCache.IsValidWave(result.WavePath),
            $"Neural voice {voice} did not produce a valid voice-identified WAV: {result.Error}");
        var samples = ReadPcmSampleCount(result.WavePath!);
        Require(samples > 4000, $"Neural voice {voice} produced only {samples} PCM samples.");
        File.Copy(result.WavePath!, Path.Combine(output, file), true);
        hashes.Add(Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(result.WavePath!))));
        var cached = await engine.EnqueueAsync(request with { DedupeKey = "neural-cache-" + profileId });
        Require(cached.Succeeded && cached.CacheHit && cached.WavePath == result.WavePath,
            $"Neural voice {voice} did not round-trip through its voice-specific cache entry.");
        Console.WriteLine($"NEURAL_PROOF voice={voice} samples={samples} cache={cached.CacheHit} path={result.WavePath}");
    }
    Require(hashes.Count == auditions.Length, "Requested Human and alien neural auditions were not acoustically distinct files.");
}

static (string Directory, string Manifest) CreateFakeNeuralPack(
    string root, string? onlyVoice = null, bool staleAbsolutePaths = false)
{
    var directory = Path.Combine(root, "fake-neural-pack"); Directory.CreateDirectory(directory);
    var python = FindPython();
    var worker = Path.Combine(directory, "kokoro_worker.py");
    File.WriteAllText(worker, """
import json, os, struct, sys, time, wave
startup_marker = "delay-startup.once"
if os.path.exists(startup_marker):
    os.remove(startup_marker)
    time.sleep(2)
for i in range(300):
    sys.stderr.write(f"stderr-{i}:" + ("x" * 300) + "\n")
sys.stderr.flush()
open("working-directory.ok", "w", encoding="utf-8").write(os.getcwd())
print(json.dumps({"ready": True}), flush=True)
for line in sys.stdin:
    request = json.loads(line)
    if request["text"] == "timeout":
        time.sleep(5)
        continue
    if request["text"] == "bad-json":
        print("{ malformed", flush=True)
        continue
    if request["text"] == "long-json":
        print("{" + ("x" * 70000), flush=True)
        continue
    with wave.open(request["outputPath"], "wb") as output:
        output.setnchannels(1); output.setsampwidth(2); output.setframerate(24000)
        value = 800 + sum(ord(c) for c in request["voice"])
        output.writeframes(b"".join(struct.pack("<h", value if i % 2 else -value) for i in range(1200)))
    print(json.dumps({"id": request["id"], "ok": True}), flush=True)
""", new UTF8Encoding(false));
    var model = Path.Combine(directory, "kokoro-v1.0.onnx"); File.WriteAllText(model, "fake-model");
    var voices = Path.Combine(directory, "voices-v1.0.bin"); File.WriteAllText(voices, "fake-voices");
    static string Sha(string path) => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
    var voiceIds = onlyVoice is null ? new[] { "af_heart", "af_bella", "af_nicole", "af_sarah", "am_michael", "bf_emma", "bm_george" } : new[] { onlyVoice };
    var manifest = Path.Combine(directory, "pack.json");
    var stale = Path.Combine(directory, "packaged-host-is-unavailable");
    File.WriteAllText(manifest, System.Text.Json.JsonSerializer.Serialize(new { schemaVersion = 1, pythonPath = python,
        workerPath = staleAbsolutePaths ? Path.Combine(stale, "kokoro_worker.py") : worker,
        modelPath = staleAbsolutePaths ? Path.Combine(stale, "kokoro-v1.0.onnx") : model,
        voicesPath = staleAbsolutePaths ? Path.Combine(stale, "voices-v1.0.bin") : voices,
        modelSha256 = Sha(model), voicesSha256 = Sha(voices),
        version = "fake-1", voices = voiceIds }));
    return (directory, manifest);
}

static string FindPython()
{
    foreach (var candidate in new[] { Environment.GetEnvironmentVariable("PYTHON"), "/usr/bin/python3", "/usr/local/bin/python3" })
        if (!string.IsNullOrWhiteSpace(candidate) && Path.IsPathFullyQualified(candidate) && File.Exists(candidate)) return candidate;
    var finder = OperatingSystem.IsWindows() ? "where.exe" : "which";
    foreach (var name in new[] { "python", "python3" })
    {
        using var process = Process.Start(new ProcessStartInfo(finder, name) { RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true });
        var candidate = process?.StandardOutput.ReadLine(); process?.WaitForExit(3000);
        if (!string.IsNullOrWhiteSpace(candidate) && Path.IsPathFullyQualified(candidate) && File.Exists(candidate)) return candidate;
    }
    throw new InvalidOperationException("Python is required for the neural worker protocol checks.");
}

static async Task VerifySapiAsync(VoiceProfileRegistry registry, string output)
{
    var sapi = new WindowsSapiSpeechBackend();
    Require(sapi.Capabilities.Available, sapi.Capabilities.Detail ?? "SAPI unavailable.");
    var female = registry.Resolve("human_female_fleet_commander"); var male = registry.Resolve("human_male_governor");
    var femaleVoice = sapi.ResolveVoiceId(female, female.Culture); var maleVoice = sapi.ResolveVoiceId(male, male.Culture);
    Require(!string.IsNullOrWhiteSpace(femaleVoice) && !string.IsNullOrWhiteSpace(maleVoice) && femaleVoice != maleVoice,
        $"Distinct installed female/male SAPI voices were not resolved: {femaleVoice} / {maleVoice}");
    await using var engine = new VoiceEngine(registry, sapi, new VoiceCache(Path.Combine(output, "sapi-cache")));
    var femaleResult = await engine.EnqueueAsync(new(female.Id, "Long range sensors confirm a stable exoplanet atmosphere.") { DedupeKey = "sapi-female-1" });
    var maleResult = await engine.EnqueueAsync(new(male.Id, "Colony administration reports all essential systems operational.") { DedupeKey = "sapi-male-1" });
    Require(femaleResult.Succeeded && maleResult.Succeeded && femaleResult.SelectedVoice == femaleVoice && maleResult.SelectedVoice == maleVoice,
        "VoiceEngine did not return the installed voice used for synthesis.");
    var femalePath = femaleResult.WavePath!; var malePath = maleResult.WavePath!;
    var femaleSamples = ReadPcmSampleCount(femalePath); var maleSamples = ReadPcmSampleCount(malePath);
    Require(femaleSamples > 4000 && maleSamples > 4000, $"SAPI WAV data is empty: {femaleSamples}/{maleSamples} samples.");
    var cached = await engine.EnqueueAsync(new(female.Id, "Long range sensors confirm a stable exoplanet atmosphere.") { DedupeKey = "sapi-female-cache" });
    Require(cached.Succeeded && cached.CacheHit && cached.WavePath == femalePath, "Real SAPI output did not round-trip through cache.");
    Console.WriteLine($"SAPI_PROOF femaleVoice={femaleVoice} samples={femaleSamples} path={femalePath}");
    Console.WriteLine($"SAPI_PROOF maleVoice={maleVoice} samples={maleSamples} path={malePath}");
}

static int ReadPcmSampleCount(string path)
{
    Require(VoiceCache.IsValidWave(path), "Invalid WAV: " + path);
    using var reader = new BinaryReader(File.OpenRead(path)); reader.ReadBytes(12);
    ushort channels = 1, bits = 16; int data = 0;
    while (reader.BaseStream.Position + 8 <= reader.BaseStream.Length)
    {
        var id = Encoding.ASCII.GetString(reader.ReadBytes(4)); var size = reader.ReadInt32();
        if (id == "fmt ") { var format = reader.ReadInt16(); channels = reader.ReadUInt16(); reader.ReadInt32(); reader.ReadInt32(); reader.ReadInt16(); bits = reader.ReadUInt16(); Require(format == 1, "WAV is not PCM."); reader.BaseStream.Position += size - 16; }
        else if (id == "data") { data = size; break; }
        else reader.BaseStream.Position += size;
        if ((size & 1) != 0) reader.BaseStream.Position++;
    }
    return data * 8 / Math.Max(1, channels * bits);
}

static void Pass(string name, ref int count) { count++; Console.WriteLine("VOICE_CORE_CHECK_PASS " + name); }
static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }


static async Task VerifyRequestPoliciesAsync(VoiceProfileRegistry registry, string output)
{
    Require(SpeechText.Normalize("I approve 1,000 units for Sector IV.").Contains("I approve 1,000 units for Sector four."),
        "Speech normalization corrupted a pronoun, thousands separator or Roman sector number.");
    Require(SpeechText.Normalize("2050-01-02").Contains("January 2, 2050"), "ISO date normalization failed.");
    var strictScientist = registry.Resolve("human_female_chief_scientist");
    var backend = new PolicyBackend();
    await using (var wrongBackend = new VoiceEngine(registry, backend,
        new VoiceCache(Path.Combine(output, "required-backend")), new VoiceSettings(OfflineOnly: false)))
    {
        var result = await wrongBackend.EnqueueAsync(new(strictScientist.Id, "Do not speak this in a US system voice."));
        Require(!result.Succeeded && backend.Calls == 0 &&
                result.Error?.Contains("Required voice backend 'offline-neural'", StringComparison.Ordinal) == true,
            "Scientist silently crossed from required British neural speech to another backend.");
    }
    var profile = strictScientist with {
        RequirePreferredBackend = false,
        CivilizationPronunciations = new Dictionary<string,string> { ["Sol"] = "civilization" },
        SpeciesPronunciations = new Dictionary<string,string> { ["Sol"] = "species" },
        Pronunciations = new Dictionary<string,string> { ["Sol"] = "profile" },
        CharacterPronunciations = new Dictionary<string,string> { ["Sol"] = "character" } };
    Require(SpeechText.NormalizeForProfile("Sol solution", profile, null) == "character solution", "Character dictionary did not override the profile/species/civilization/global dictionaries.");
    Require(SpeechText.NormalizeForProfile("Sol", profile, new Dictionary<string,string> { ["Sol"] = "request" }) == "request",
        "Request pronunciation override was lost to an earlier replacement.");
    var policyRegistry = new VoiceProfileRegistry(registry.All.Select(candidate => candidate.Id == profile.Id ? profile : candidate));
    await using (var engine = new VoiceEngine(policyRegistry, backend, new VoiceCache(Path.Combine(output, "offline-policy"))))
    {
        var result = await engine.EnqueueAsync(new(profile.Id, "Private local dialogue."));
        Require(!result.Succeeded && backend.Calls == 0 && result.SubtitleText == "Private local dialogue.",
            "Offline Only permitted an online synthesis call or lost the subtitle.");
    }
    await using (var enabled = new VoiceEngine(policyRegistry, backend, new VoiceCache(Path.Combine(output, "cache-policy")), new VoiceSettings(OfflineOnly: false)))
    {
        var first = await enabled.EnqueueAsync(new(profile.Id, "Reusable line.") { DedupeKey = "cache-first" });
        var second = await enabled.EnqueueAsync(new(profile.Id, "Reusable line.") { DedupeKey = "cache-second" });
        var uncached = await enabled.EnqueueAsync(new(profile.Id, "Reusable line.") { DedupeKey = "cache-bypass", CachePolicy = SpeechCachePolicy.NoCache });
        Require(first.Succeeded && second.CacheHit && uncached.Succeeded && !uncached.CacheHit && uncached.WavePath != first.WavePath && backend.Calls == 2,
            "NoCache did not bypass the existing cache entry.");
        var tooLong = await enabled.EnqueueAsync(new(profile.Id, new string('a', 4001)));
        Require(!tooLong.Succeeded && backend.Calls == 2, "Unbounded text reached synthesis.");
    }
}

static void VerifyDialogueRouting(VoiceProfileRegistry registry, string root)
{
    var lines = new List<SpeechRequest>();
    var router = VoiceEventRouter.FromJson(File.ReadAllText(Path.Combine(root, "data", "voice_profiles", "events.json")), lines.Add);
    foreach (var key in new[] { "opening", "research", "construction", "shipyard", "ship_launch", "departure",
        "arrival", "discovery", "colony", "unknown_contact", "alien_transmission", "critical_hull",
        "exploration.system.reconnaissance_required" })
    {
        Require(router.Emit(key, "Observer-visible event."), "Missing required gameplay cue " + key);
        Require(!router.Emit(key, "Duplicate event."), "Cue cooldown did not suppress burst " + key);
        Require(registry.Resolve(lines[^1].ProfileId).Enabled && !string.IsNullOrWhiteSpace(lines[^1].LocalizationKey),
            "Cue uses missing speaker or localization identity.");
    }
    Require(lines.Single(l => l.Category == "critical_hull").Priority > lines.Single(l => l.Category == "research").Priority,
        "Research would outrank a critical combat warning.");
    Require(!lines.Single(l => l.Category == "opening").Interruptible, "Opening cinematic is interruptible by routine chatter.");
    router.Reset(); Require(!router.HasEmitted("opening") && router.Emit("opening"), "Campaign reset retained stale cue history.");
    var variants = new VoiceEventRouter(new[] { new VoiceEventCue("test", "ship_computer", new[] { "One", "Two" }, CooldownSeconds: 0) }, lines.Add);
    variants.Emit("test"); variants.Emit("test");
    Require(lines[^2].Text == "One" && lines[^1].Text == "Two", "Authored variation is not deterministic.");
}

static void VerifyTypedEventRouting(VoiceProfileRegistry registry, string root)
{
    var mappingsJson = File.ReadAllText(Path.Combine(root, "data", "voice_profiles", "roles.json"));
    VoiceCharacter? current = new("scientist-a", "Dr. Mira Chen", "ChiefScientist", "human_female_chief_scientist", "mira.png");
    var resolver = CharacterVoiceResolver.FromJson(registry, mappingsJson, _ => current);
    var requests = new List<SpeechRequest>();
    var eventsJson = File.ReadAllText(Path.Combine(root, "data", "voice_profiles", "events.json"));
    var router = VoiceEventRouter.FromJson(eventsJson, requests.Add, resolver);
    var now = DateTimeOffset.Parse("2050-01-02T00:00:00Z");
    var context = new VoiceRoutingContext(7, VoiceFrequency.Normal) { PresentationTime = now };
    var research = new GameplayVoiceEvent("research.completed", 7, "research:orbital-industry:1",
        new Dictionary<string, string> { ["research_name"] = "Orbital\nIndustry" }, 42, "2050-01-02")
        { SourceSpeciesId = "terran_baseline", FirstOccurrence = true };
    Require(router.Emit(research, context), "Typed research completion was not routed.");
    Require(router.HasEmitted("research.completed") && router.HasEmitted("research"),
        "Typed milestone did not preserve the legacy HasEmitted query alias.");
    var queued = requests[^1];
    Require(queued.ProfileId == "voice_profile_unresolved" && queued.Text.Contains("Orbital Industry") &&
        queued.Text.Contains("first research", StringComparison.OrdinalIgnoreCase) &&
        queued.SpeakerRole == VoiceSpeakerRole.ChiefScientist && queued.SpeakerResolver is not null,
        "Typed research mapping, safe substitution, or delayed speaker contract failed.");

    current = new("scientist-b", "Dr. Imani Okafor", "ChiefScientist", "human_female_diplomat", "imani.png");
    var replaced = queued.SpeakerResolver!(queued.SpeakerContext!);
    Require(replaced is { CharacterId: "scientist-b", DisplayName: "Dr. Imani Okafor", ProfileId: "human_female_diplomat" },
        "Queued dialogue retained the previous office holder instead of resolving at presentation time.");

    current = new("scientist-c", "Dr. Noa Rao", "ChiefScientist", "missing-profile", "noa.png");
    var fallback = queued.SpeakerResolver(queued.SpeakerContext!);
    Require(fallback is { CharacterId: "scientist-c", DisplayName: "Dr. Noa Rao", ProfileId: "human_female_chief_scientist", IsFallback: true },
        "Fallback timbre discarded the current character identity.");

    current = new("scientist-exact", "Dr. Asha Bell", "ChiefScientist", "human_female_chief_scientist", "asha.png");
    var exactTimbre = resolver.Resolve(queued.SpeakerContext! with
        { ExactCharacterId = "scientist-exact", ExactVoiceProfileId = "human_female_diplomat" });
    Require(exactTimbre is { CharacterId: "scientist-exact", DisplayName: "Dr. Asha Bell", ProfileId: "human_female_diplomat" },
        "Exact voice override discarded the resolved character identity metadata.");

    current = null;
    var pelagic = resolver.Resolve(new VoiceSpeakerContext(VoiceSpeakerRole.AlienScientist, 19, "pelagic_high_pressure"));
    Require(pelagic?.ProfileId == "pelagic_translator" && registry.Resolve(pelagic.ProfileId).Species != "human",
        "Alien role fell through to a Human profile instead of its species translator.");
    var unknownAlien = resolver.Resolve(new VoiceSpeakerContext(VoiceSpeakerRole.AlienCommander, 20, "unknown_nonhuman"));
    Require(unknownAlien?.ProfileId == "grey_diplomat",
        "Unknown alien species did not use the non-Human generic alien fallback.");

    Require(!router.Emit(research, context with { PresentationTime = now.AddMinutes(1) }),
        "Duplicate unique event ID was voiced twice.");
    var missingVariable = research with { UniqueEventId = "research:missing", Variables = new Dictionary<string, string>() };
    Require(!router.Emit(missingVariable, context with { PresentationTime = now.AddMinutes(2) }),
        "A template with a missing required field produced partial dialogue.");

    var construction = new GameplayVoiceEvent("construction.completed", 7, "construction:1",
        new Dictionary<string, string> { ["project_name"] = "Research Network" }, 45, "2050-01-02")
        { SourceSpeciesId = "terran_baseline" };
    Require(router.Emit(construction, context with { PresentationTime = now.AddMinutes(3) }) &&
        !router.Emit(construction with { UniqueEventId = "construction:2" },
            context with { PresentationTime = now.AddMinutes(3).AddSeconds(1) }),
        "Per-category cooldown did not suppress a distinct construction event in the configured window.");

    var deterministicA = new List<SpeechRequest>(); var deterministicB = new List<SpeechRequest>();
    var firstRouter = VoiceEventRouter.FromJson(eventsJson, deterministicA.Add, resolver);
    var secondRouter = VoiceEventRouter.FromJson(eventsJson, deterministicB.Add, resolver);
    var repeatable = construction with { UniqueEventId = "construction:repeatable", FirstOccurrence = false };
    Require(firstRouter.Emit(repeatable, context) && secondRouter.Emit(repeatable, context) &&
        deterministicA[0].Text == deterministicB[0].Text,
        "Dialogue selection was not deterministic for replay-identical event data.");
    var loadedSaveLines = new List<SpeechRequest>();
    var loadedSaveRouter = VoiceEventRouter.FromJson(eventsJson, loadedSaveLines.Add, resolver);
    Require(loadedSaveRouter.Emit(research with { UniqueEventId = "research:after-load", FirstOccurrence = false }, context) &&
        !loadedSaveLines[0].Text.Contains("first research", StringComparison.OrdinalIgnoreCase),
        "A progressed save inferred a first-use line from empty presentation history.");

    var frequent = new GameplayVoiceEvent("expedition.navigation.correction", 7, "nav:1",
        new Dictionary<string, string> { ["destination_name"] = "Andromeda" }, 43, "2050-01-02");
    Require(!router.Emit(frequent, context) && router.Emit(frequent with { UniqueEventId = "nav:2" },
        context with { Frequency = VoiceFrequency.Frequent }), "Voice frequency did not filter routine expedition chatter.");
    var minimalLines = new List<SpeechRequest>();
    var minimalRouter = VoiceEventRouter.FromJson(eventsJson, minimalLines.Add, resolver);
    var minimalContext = context with { Frequency = VoiceFrequency.Minimal };
    var opening = new GameplayVoiceEvent("opening", 7, "opening:minimal", new Dictionary<string, string>(), 1, "2050-01-01");
    var normalResearch = research with { UniqueEventId = "research:minimal-filter", FirstOccurrence = false };
    var majorResearch = research with { EventKey = "research.breakthrough.major", UniqueEventId = "research:major",
        FirstOccurrence = false };
    Require(minimalRouter.Emit(opening, minimalContext) && !minimalRouter.Emit(normalResearch, minimalContext) &&
        minimalRouter.Emit(majorResearch, minimalContext with { PresentationTime = now.AddMinutes(1) }),
        "Minimal frequency did not retain opening/major research while filtering routine research.");

    var hiddenAi = new GameplayVoiceEvent("research.completed", 99, "hidden:1",
        new Dictionary<string, string> { ["research_name"] = "Secret Weapons" }, 44, "2050-01-02")
        { SourceSpeciesId = "compact_high_gravity" };
    Require(!router.Emit(hiddenAi, context with { PresentationTime = now.AddMinutes(3) }),
        "A foreign civilization's hidden internal event leaked to the player.");
    var observable = hiddenAi with { UniqueEventId = "observable:1", Audience = VoiceAudience.Observable, ObserverEvidence = true };
    Require(router.Emit(observable, context with { PresentationTime = now.AddMinutes(4) }),
        "An explicitly observable foreign event with player evidence was rejected.");
    var direct = hiddenAi with { EventKey = "diplomacy.alien.transmission", UniqueEventId = "direct:1",
        Variables = new Dictionary<string, string> { ["message"] = "Your presence was anticipated." },
        Audience = VoiceAudience.DirectCommunication, RecipientCivilizationId = 7 };
    Require(router.Emit(direct, context with { PresentationTime = now.AddMinutes(5) }) &&
        requests[^1].CommunicationsFilter && requests[^1].SpeakerRole == VoiceSpeakerRole.AlienDiplomat,
        "Authorized foreign communication did not route through the alien speaker path.");
    Require(!router.Emit(hiddenAi with { UniqueEventId = "observer:1", Audience = VoiceAudience.ObserverSafe, ObserverEvidence = true },
        context with { PresentationTime = now.AddMinutes(6) }), "Observer-safe evidence leaked outside observer mode.");

    var overrideEvent = research with { UniqueEventId = "story:1", Overrides = new VoiceEventOverrides
        { ExactLine = "Commander, the field remained stable.", SpeakerRole = VoiceSpeakerRole.ExpeditionCommander,
          VoiceProfileId = "human_female_fleet_commander", Priority = 100, Interruptible = false, CommunicationsFilter = true } };
    Require(router.Emit(overrideEvent, context with { PresentationTime = now.AddMinutes(7) }) &&
        requests[^1] is { Text: "Commander, the field remained stable.", Priority: 100, Interruptible: false,
            CommunicationsFilter: true, ProfileId: "human_female_fleet_commander" },
        "Event-specific exact dialogue and metadata overrides were not honored.");
    Require(router.Emit(overrideEvent with { UniqueEventId = "story:dry", Overrides = overrideEvent.Overrides with
        { CommunicationsFilter = false } }, context with { PresentationTime = now.AddMinutes(7.5) }) &&
        requests[^1].CommunicationsFilterOverride == false,
        "An explicit dry-channel override was lost before playback.");
    var uniqueCinematic = new GameplayVoiceEvent("story.unregistered", 7, "story:unregistered:1",
        new Dictionary<string, string> { ["planet_name"] = "Europa" }, 60, "2050-01-02")
    {
        SourceSpeciesId = "terran_baseline",
        Overrides = new VoiceEventOverrides
        {
            ExactLine = "Commander, the signal beneath {planet_name} is responding.",
            SpeakerRole = VoiceSpeakerRole.ChiefScientist,
            Priority = 100,
        },
    };
    Require(router.Emit(uniqueCinematic, context with { Frequency = VoiceFrequency.Minimal, PresentationTime = now.AddMinutes(8) }) &&
        requests[^1].Text == "Commander, the signal beneath Europa is responding." &&
        requests[^1].SpeakerRole == VoiceSpeakerRole.ChiefScientist,
        "An explicitly authored cinematic line incorrectly required a pre-registered routine cue.");
    Require(!router.Emit(uniqueCinematic with { UniqueEventId = "story:unsafe:1", Overrides = uniqueCinematic.Overrides with
        { SpeakerRole = null } }, context with { PresentationTime = now.AddMinutes(9) }),
        "An unregistered exact line without an explicit speaker role was accepted.");

    var definitions = JsonSerializer.Deserialize<VoiceEventCue[]>(eventsJson,
        new JsonSerializerOptions { PropertyNameCaseInsensitive = true, Converters = { new System.Text.Json.Serialization.JsonStringEnumConverter() } })!;
    var milestoneRouter = VoiceEventRouter.FromJson(eventsJson, requests.Add, resolver);
    var milestone = research with { EventKey = "construction.orbital_launch_complex.completed", UniqueEventId = "milestone:launch" };
    Require(milestoneRouter.Emit(milestone, context) && milestoneRouter.Emit(milestone with
        { EventKey = "construction.orbital_shipyard.completed", UniqueEventId = "milestone:shipyard" }, context),
        "Distinct major infrastructure completions suppressed one another in the same presentation interval.");
    foreach (var key in new[] { "research.completed", "research.breakthrough.major", "construction.completed",
        "construction.orbital_launch_complex.completed", "construction.orbital_shipyard.completed", "ship.completed",
        "ship.launched", "ship.interstellar.first_launch", "exploration.system.reached", "exploration.survey.completed",
        "exploration.anomaly.discovered", "colony.founded", "contact.unknown.detected", "contact.first",
        "diplomacy.alien.transmission", "diplomacy.war.declared", "combat.fleet.attacked", "combat.hull.critical",
        "logistics.critical", "economy.treasury.critical", "expedition.reactor.problem",
        "expedition.navigation.correction", "expedition.resource.shortage", "expedition.crew.issue",
        "expedition.unknown.signal", "expedition.rogue_planet", "expedition.intergalactic_object",
        "expedition.ship.damage", "expedition.major.discovery", "expedition.destination.approach" })
        Require(definitions.Any(cue => cue.Event == key), "Missing required typed dialogue cue " + key);
    var reconnaissanceCue = definitions.Single(cue => cue.Event == "exploration.system.reconnaissance_required");
    Require(reconnaissanceCue.Profile == "human_female_chief_scientist" &&
        reconnaissanceCue.Frequency == VoiceFrequency.Minimal && reconnaissanceCue.CooldownSeconds == 8 &&
        reconnaissanceCue.Lines.Single() == "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.",
        "Reconnaissance gate advisory lost its authored role, minimal frequency, cooldown, or observer-safe text.");

    var warCue = definitions.Single(cue => cue.Event == "diplomacy.war.declared");
    var economyCue = definitions.Single(cue => cue.Event == "economy.treasury.critical");
    Require(!warCue.Once && warCue.FirstLines.Length > 0 && warCue.QueueBehavior == SpeechQueueBehavior.InterruptLowerPriority &&
        warCue.Lines.All(line => line.Contains("with {enemy_name}", StringComparison.Ordinal)) &&
        economyCue.Lines.All(line => !line.Contains("credit", StringComparison.OrdinalIgnoreCase)),
        "War recurrence/direction or pre-currency economy dialogue semantics regressed.");

    var mappings = JsonSerializer.Deserialize<VoiceRoleMapping[]>(mappingsJson,
        new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
    foreach (var role in Enum.GetValues<VoiceSpeakerRole>())
        Require(mappings.Any(mapping => Enum.TryParse<VoiceSpeakerRole>(mapping.Role, true, out var mapped) && mapped == role),
            "Missing fallback mapping for speaker role " + role);
    var missingResolver = new CharacterVoiceResolver(registry,
        new[] { new VoiceRoleMapping("ChiefScientist", "profile-does-not-exist") });
    Require(missingResolver.Resolve(new VoiceSpeakerContext(VoiceSpeakerRole.ChiefScientist, 7, "terran_baseline")) is null,
        "Missing profiles should yield caption-safe unresolved speaker metadata rather than throw.");
    var civilizationResolver = new CharacterVoiceResolver(registry, new[]
    {
        new VoiceRoleMapping("FleetCommander", "human_male_fleet_commander", CivilizationId: 7),
        new VoiceRoleMapping("FleetCommander", "human_female_fleet_commander"),
    });
    Require(civilizationResolver.Resolve(new VoiceSpeakerContext(VoiceSpeakerRole.FleetCommander, 7))?.ProfileId ==
            "human_male_fleet_commander" &&
        civilizationResolver.Resolve(new VoiceSpeakerContext(VoiceSpeakerRole.FleetCommander, 8))?.ProfileId ==
            "human_female_fleet_commander", "Civilization-specific role mapping did not outrank the generic fallback.");
    foreach (var cue in definitions)
        Require(registry.TryResolve(cue.Profile, out _), "Dialogue cue references missing fallback profile " + cue.Profile);

    var invalidSettings = Path.Combine(root, "work", "voice-core-proof", "invalid-frequency.json");
    File.WriteAllText(invalidSettings, "{\"frequency\":99,\"chatterLevel\":1}");
    Require(VoiceSettings.Load(invalidSettings).EffectiveFrequency == VoiceFrequency.Normal &&
        (new VoiceSettings { Frequency = VoiceFrequency.Frequent, ChatterLevel = 0 }).EffectiveFrequency == VoiceFrequency.Minimal,
        "Voice frequency settings did not sanitize invalid data or preserve muted chatter semantics.");
}

sealed class PolicyBackend : IVoiceSpeechBackend
{
    public SpeechBackendCapabilities Capabilities { get; } = new(true, new[] { "test" }) { Offline = false, BackendId = "online-policy-test" };
    public int Calls { get; private set; }
    public Task SynthesizeAsync(VoiceProfile profile, string text, string path, CancellationToken token)
    { Calls++; WavTest.Write(path, 500); return Task.CompletedTask; }
}

sealed class ControlledBackend : IVoiceSpeechBackend, IDisposable
{
    public SpeechBackendCapabilities Capabilities { get; } = new(true, new[] { "controlled" }) { BackendId = "controlled" };
    public string BackendId => "controlled"; public string Model => "test"; public string Version => "1";
    public TaskCompletionSource<bool> Started { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public TaskCompletionSource<bool> Release { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public List<string> Order { get; } = new();
    public async Task SynthesizeAsync(VoiceProfile profile, string text, string path, CancellationToken token)
    {
        lock (Order) Order.Add(text); Started.TrySetResult(true);
        if (text is "blocker" or "cancel me") await Release.Task.WaitAsync(token);
        token.ThrowIfCancellationRequested(); WavTest.Write(path, 500);
    }
    public void Dispose() => Release.TrySetResult(true);
}

sealed class UnavailableBackend : IVoiceSpeechBackend
{
    public SpeechBackendCapabilities Capabilities { get; } = new(false, Array.Empty<string>(), "backend unavailable") { BackendId = "offline-test" };
    public string BackendId => "offline-test"; public string Model => "none"; public string Version => "7";
    public string ResolveVoiceId(VoiceProfile profile, string culture) => "none";
    public Task SynthesizeAsync(VoiceProfile profile, string text, string wavPath, CancellationToken cancellationToken) => throw new InvalidOperationException();
}

sealed class VoiceIdentityBackend(bool available, string voice) : IVoiceSpeechBackend
{
    public SpeechBackendCapabilities Capabilities { get; } = new(available, available ? new[] { voice } : Array.Empty<string>(),
        available ? null : "voice temporarily unavailable") { BackendId = "voice-id-test", Offline = true };
    public string BackendId => "voice-id-test"; public string Model => "same-model"; public string Version => "3";
    public int Calls { get; private set; }
    public string ResolveVoiceId(VoiceProfile profile, string culture) => voice;
    public Task SynthesizeAsync(VoiceProfile profile, string text, string wavPath, CancellationToken cancellationToken)
    { Calls++; WavTest.Write(wavPath, 640); return Task.CompletedTask; }
}

static class WavTest
{
    public static void Write(string path, int samples)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!); var dataSize = samples * 2;
        using var writer = new BinaryWriter(File.Create(path)); writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(36 + dataSize);
        writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16); writer.Write((short)1); writer.Write((short)1);
        writer.Write(22050); writer.Write(44100); writer.Write((short)2); writer.Write((short)16);
        writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(dataSize);
        for (var index = 0; index < samples; index++) writer.Write((short)(Math.Sin(index * .08) * 4000));
    }
}
