// Native voice contract tests — the router's observer-safe authorization,
// deterministic variant selection, cooldown/dedupe/once gates, the speaker
// resolver's human/alien separation, playback queue semantics, speech-text
// normalization, the WAV cache and persisted settings.
#include "native_voice.hpp"
#include "native_voice_playback.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_voice;
namespace {

int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::vector<NativeVoiceCue> cues() {
  NativeVoiceCue research;
  research.event = "research.completed";
  research.profile = "narrator";
  research.category = "research";
  research.lines = {"Research complete: {research_name}.",
                    "Our scientists finished {research_name}."};
  research.first_lines = {"First discovery: {research_name}."};
  research.priority = 20;
  research.cooldown_seconds = 30;
  research.speaker_role = VoiceSpeakerRole::ChiefScientist;
  research.frequency = VoiceFrequency::Normal;

  NativeVoiceCue alert;
  alert.event = "combat.hull.critical";
  alert.category = "combat";
  alert.lines = {"Hull integrity critical on {ship_name}."};
  alert.priority = 80;
  alert.cooldown_seconds = 5;
  alert.speaker_role = VoiceSpeakerRole::FleetCommander;
  alert.frequency = VoiceFrequency::Minimal;

  NativeVoiceCue once;
  once.event = "opening";
  once.category = "narration";
  once.lines = {"A new chapter begins."};
  once.priority = 100;
  once.once = true;
  once.cooldown_seconds = 0;
  once.interruptible = false;
  once.speaker_role = VoiceSpeakerRole::Narrator;
  once.frequency = VoiceFrequency::Minimal;
  return {research, alert, once};
}

NativeGameplayVoiceEvent event(std::string key, int civ,
                               std::string id = {}) {
  NativeGameplayVoiceEvent e;
  e.event_key = std::move(key);
  e.source_civilization_id = civ;
  e.unique_event_id = std::move(id);
  e.variables = {{"research_name", "Ion Drive"},
                 {"ship_name", "SCV-42"},
                 {"system_name", "Sol"},
                 {"enemy_name", "Vask"},
                 {"amount", "12"},
                 {"message", "Open trade?"},
                 {"detail", "survey"}};
  e.simulation_tick = 7;
  e.simulation_date = "2050-03-01";
  e.source_species_id = "terran_baseline";
  return e;
}

NativeVoiceRoutingContext context(
    int player = 0,
    VoiceFrequency frequency = VoiceFrequency::Normal,
    bool observer = false,
    std::chrono::system_clock::time_point time =
        std::chrono::system_clock::time_point{}) {
  NativeVoiceRoutingContext c;
  c.player_civilization_id = player;
  c.frequency = frequency;
  c.observer_mode = observer;
  c.presentation_time = time;
  return c;
}

class FakeBackend final : public INativeSpeechBackend {
public:
  bool available() const noexcept override { return usable; }
  std::string backend_id() const override { return "fake"; }
  std::string detail() const override { return "test backend"; }
  std::vector<std::string> voices() const override { return {"Test Voice"}; }
  std::string resolve_voice_id(const NativeVoiceProfile &,
                               std::string_view) const override {
    return "fake-voice";
  }
  std::future<NativeVoiceResult>
  synthesize(const NativeVoiceProfile &, std::string,
             std::filesystem::path path) override {
    ++synthesized;
    if (ready_immediately) {
      std::promise<NativeVoiceResult> promise;
      promise.set_value(
          NativeVoiceResult{succeed, std::move(path), error, false,
                            "fake-voice"});
      return promise.get_future();
    }
    auto &pending = deferred.emplace_back();
    pending.path = std::move(path);
    return pending.promise.get_future();
  }
  void clear_pending() override { ++cleared; }

  struct Pending {
    std::promise<NativeVoiceResult> promise;
    std::filesystem::path path;
  };
  bool usable{true}, succeed{true}, ready_immediately{true};
  std::string error;
  int synthesized{}, cleared{};
  std::deque<Pending> deferred;
};

// A minimal valid 22.05 kHz 16-bit mono PCM WAV with a short data chunk.
void write_wav(const std::filesystem::path &path, std::uint32_t frames = 64) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::uint32_t data_size = frames * 2;
  out.write("RIFF", 4);
  const std::uint32_t riff_size = 36 + data_size;
  out.write(reinterpret_cast<const char *>(&riff_size), 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  const std::uint32_t fmt_size = 16;
  const std::uint16_t pcm = 1, channels = 1, bits = 16, align = 2;
  const std::uint32_t rate = 22050, byte_rate = rate * align;
  out.write(reinterpret_cast<const char *>(&fmt_size), 4);
  out.write(reinterpret_cast<const char *>(&pcm), 2);
  out.write(reinterpret_cast<const char *>(&channels), 2);
  out.write(reinterpret_cast<const char *>(&rate), 4);
  out.write(reinterpret_cast<const char *>(&byte_rate), 4);
  out.write(reinterpret_cast<const char *>(&align), 2);
  out.write(reinterpret_cast<const char *>(&bits), 2);
  out.write("data", 4);
  out.write(reinterpret_cast<const char *>(&data_size), 4);
  for (std::uint32_t i = 0; i < frames; ++i) {
    const std::int16_t sample = static_cast<std::int16_t>(i % 1000);
    out.write(reinterpret_cast<const char *>(&sample), 2);
  }
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path temp =
      argc > 1 ? std::filesystem::path(argv[1])
               : std::filesystem::temp_directory_path() / "stellar-voice-tests";
  std::filesystem::create_directories(temp);

  // --- Router: authorization -------------------------------------------
  {
    std::vector<NativeSpeechRequest> submitted;
    NativeVoiceRouter router(cues(), [&](NativeSpeechRequest r) {
      submitted.push_back(std::move(r));
    });
    auto own = event("research.completed", 0, "e:own:1");
    check(router.emit(own, context()), "own-civilization cue must emit");
    check(submitted.size() == 1, "one request expected");

    // Foreign own-civilization event is denied.
    auto foreign = event("research.completed", 7, "e:foreign:1");
    check(!router.emit(foreign, context()),
          "foreign own-civ event must be denied");

    // Direct communication reaches the player only as the recipient.
    auto direct = event("research.completed", 7, "e:direct:1");
    direct.audience = VoiceAudience::DirectCommunication;
    direct.recipient_civilization_id = 0;
    direct.observer_evidence = false;
    // Router lookup keyed on research.completed; category cooldown was
    // consumed by the first emit, so bump the cue category via override.
    NativeVoiceOverrides override_direct;
    override_direct.category = "direct-test";
    direct.overrides = override_direct;
    check(router.emit(direct, context()),
          "direct communication to the player must emit");
    auto blocked = event("research.completed", 7, "e:direct:2");
    blocked.audience = VoiceAudience::DirectCommunication;
    blocked.recipient_civilization_id = 4;
    blocked.overrides = override_direct;
    check(!router.emit(blocked, context()),
          "direct communication for another civ must be denied");

    // Observable needs explicit evidence; ObserverSafe also needs the mode.
    // Distinct override categories keep the cue cooldown from denying these.
    auto observable = event("research.completed", 7, "e:obs:1");
    observable.audience = VoiceAudience::Observable;
    NativeVoiceOverrides override_observable;
    override_observable.category = "observable-test";
    observable.overrides = override_observable;
    check(!router.emit(observable, context()),
          "observable without evidence must be denied");
    observable.unique_event_id = "e:obs:2";
    observable.observer_evidence = true;
    check(router.emit(observable, context()),
          "observable with evidence must emit");

    auto safe = event("research.completed", 7, "e:safe:1");
    safe.audience = VoiceAudience::ObserverSafe;
    safe.observer_evidence = true;
    NativeVoiceOverrides override_safe;
    override_safe.category = "observer-safe-test";
    safe.overrides = override_safe;
    check(!router.emit(safe, context()),
          "observer-safe without observer mode must be denied");
    check(router.emit(safe, context(0, VoiceFrequency::Normal, true)),
          "observer-safe with mode and evidence must emit");
  }

  // --- Router: dedupe, once, cooldown, frequency, variants -------------
  {
    std::vector<NativeSpeechRequest> submitted;
    NativeVoiceRouter router(cues(), [&](NativeSpeechRequest r) {
      submitted.push_back(std::move(r));
    });
    const auto base = std::chrono::system_clock::time_point{} +
                      std::chrono::hours(1000);
    check(router.emit(event("combat.hull.critical", 0, "h:1"),
                      context(0, VoiceFrequency::Normal, false, base)),
          "first hull alert must emit");
    check(!router.emit(event("combat.hull.critical", 0, "h:1"),
                       context(0, VoiceFrequency::Normal, false, base)),
          "duplicate unique id must dedupe");
    check(!router.emit(event("combat.hull.critical", 0, "h:2"),
                       context(0, VoiceFrequency::Normal, false, base)),
          "same category within cooldown must be denied");
    check(router.emit(event("combat.hull.critical", 0, "h:2"),
                      context(0, VoiceFrequency::Normal, false,
                              base + std::chrono::seconds(6))),
          "after cooldown the category emits again");
    check(submitted.size() == 2, "two hull requests expected");
    check(submitted[0].priority == 80, "cue priority propagates");
    check(submitted[0].text == "Hull integrity critical on SCV-42.",
          "template must render variables");

    // Frequency gate: the combat cue is Minimal so it survives every level;
    // research is Normal so Minimal context denies it.
    NativeVoiceRouter quiet(cues(), [&](NativeSpeechRequest r) {
      submitted.push_back(std::move(r));
    });
    check(!quiet.emit(event("research.completed", 0, "q:1"),
                      context(0, VoiceFrequency::Minimal)),
          "normal-frequency cue must be denied at Minimal");

    // once=true emits exactly one line ever.
    check(router.emit(event("opening", 0, "o:1"),
                      context(0, VoiceFrequency::Normal, false,
                              base + std::chrono::seconds(7))),
          "opening must emit once");
    check(!router.emit(event("opening", 0, "o:2"),
                       context(0, VoiceFrequency::Normal, false,
                               base + std::chrono::seconds(8))),
          "opening must not emit twice");

    // Deterministic variant: identical events pick identical lines.
    std::vector<std::string> lines_a, lines_b;
    NativeVoiceRouter pa(cues(), [&](NativeSpeechRequest r) {
      lines_a.push_back(r.text);
    });
    NativeVoiceRouter pb(cues(), [&](NativeSpeechRequest r) {
      lines_b.push_back(r.text);
    });
    for (int i = 0; i < 8; ++i) {
      auto e1 = event("research.completed", 0, "v:" + std::to_string(i));
      pa.emit(e1, context(0, VoiceFrequency::Normal, false,
                          base + std::chrono::seconds(40 * (i + 1))));
      auto e2 = event("research.completed", 0, "v:" + std::to_string(i));
      pb.emit(e2, context(0, VoiceFrequency::Normal, false,
                          base + std::chrono::seconds(40 * (i + 1))));
    }
    check(lines_a == lines_b && lines_a.size() >= 2,
          "variant selection must be deterministic");
    check(std::ranges::any_of(lines_a, [](const std::string &line) {
            return line.find("Our scientists") != std::string::npos;
          }),
          "variant selection must reach the second line");

    // first_occurrence prefers the first-line pool.
    std::vector<std::string> first_lines;
    NativeVoiceRouter pf(cues(), [&](NativeSpeechRequest r) {
      first_lines.push_back(r.text);
    });
    auto first = event("research.completed", 0, "f:1");
    first.first_occurrence = true;
    pf.emit(first, context());
    check(first_lines.size() == 1 &&
              first_lines[0].find("First discovery") == 0,
          "first occurrence must use the first_lines pool");

    // Missing variable rejects rather than leaking a raw template.
    auto missing = event("research.completed", 0, "m:1");
    missing.variables.clear();
    check(!pf.emit(missing, context(0, VoiceFrequency::Normal, false,
                                    base + std::chrono::minutes(1))),
          "missing template variable must be denied");

    // Exact-line overrides work without a catalogue cue.
    auto exact = event("unmapped.key", 0, "x:1");
    NativeVoiceOverrides ov;
    ov.exact_line = "War has been declared against {enemy_name}.";
    ov.speaker_role = VoiceSpeakerRole::Diplomat;
    ov.category = "war";
    exact.overrides = ov;
    check(pf.emit(exact, context()), "exact-line override must emit");
  }

  // --- Resolver --------------------------------------------------------
  {
    std::vector<NativeVoiceProfile> profiles;
    NativeVoiceProfile narrator;
    narrator.id = "narrator";
    narrator.display_name = "Narrator";
    narrator.subtitle_name = "NARRATOR";
    profiles.push_back(narrator);
    NativeVoiceProfile alien;
    alien.id = "alien_diplomat";
    alien.display_name = "Envoy";
    alien.species = "vask";
    profiles.push_back(alien);
    NativeVoiceProfile disabled;
    disabled.id = "retired";
    disabled.enabled = false;
    disabled.fallback_profile = "narrator";
    profiles.push_back(disabled);
    NativeVoiceProfileRegistry registry(profiles);

    std::vector<NativeVoiceRoleMapping> mappings{
        {"Narrator", "narrator", std::nullopt, "*"},
        {"AlienDiplomat", "alien_diplomat", std::nullopt, "vask"},
        {"AlienDiplomat", "narrator", std::nullopt, "*"},
    };
    NativeCharacterVoiceResolver resolver(&registry, mappings);

    NativeVoiceSpeakerContext own;
    own.role = VoiceSpeakerRole::Narrator;
    own.source_species_id = "terran_baseline";
    const auto resolved = resolver.resolve(own);
    check(resolved && resolved->profile_id == "narrator",
          "narrator role must resolve to the mapped profile");
    check(resolved->display_name == "NARRATOR",
          "subtitle name must win over display name");

    NativeVoiceSpeakerContext envoy;
    envoy.role = VoiceSpeakerRole::AlienDiplomat;
    envoy.source_species_id = "vask";
    envoy.source_civilization_id = 9;
    const auto alien_resolved = resolver.resolve(envoy);
    check(alien_resolved && alien_resolved->profile_id == "alien_diplomat",
          "alien diplomat must resolve to the alien profile");

    // An alien role with a human source species must never land on a human
    // profile — a human exact-voice request is rejected and resolution falls
    // through to the species-safe mapping.
    NativeVoiceSpeakerContext mismatched;
    mismatched.role = VoiceSpeakerRole::AlienDiplomat;
    mismatched.source_species_id = "vask";
    mismatched.exact_voice_profile_id = "narrator";
    const auto safe_fallback = resolver.resolve(mismatched);
    check(safe_fallback && safe_fallback->profile_id == "alien_diplomat",
          "alien role must reject a human profile");

    // Disabled profiles resolve through their fallback chain.
    check(registry.try_resolve("retired") != nullptr &&
              registry.try_resolve("retired")->id == "narrator",
          "disabled profile must resolve through fallback");
  }

  // --- Speech text -------------------------------------------------------
  {
    check(normalize_speech_text("SCV-42 departed") == "S C V 4 2 departed",
          "ship identifiers must expand");
    check(normalize_speech_text("coverage at 45%") == "coverage at 45 percent",
          "percentages must expand");
    check(normalize_speech_text("Sol") == "Sohl",
          "global pronunciations must apply");
    std::unordered_map<std::string, std::string> terms{{"Vask", "Vahsk"}};
    check(normalize_speech_text("The Vask signal", &terms) ==
              "The Vahsk signal",
          "profile pronunciations must apply");
    check(normalize_speech_text("a   b\n\nc") == "a b c",
          "whitespace must collapse");
  }

  // --- Settings ---------------------------------------------------------
  {
    NativeVoiceSettings settings;
    settings.volume = 2.f;
    settings.subtitle_size = 200;
    const auto clean = settings.sanitized();
    check(clean.volume == 1.f && clean.subtitle_size == 42,
          "settings must sanitize bounds");
    settings = {};
    const auto path = temp / "voice-settings.json";
    settings.save(path);
    const auto loaded = NativeVoiceSettings::load(path);
    check(loaded.enable_voices && loaded.subtitles &&
              loaded.frequency == VoiceFrequency::Normal,
          "settings must round-trip");
    NativeVoiceSettings quiet;
    quiet.chatter_level = 0;
    check(quiet.effective_frequency() == VoiceFrequency::Minimal,
          "zero chatter must clamp to Minimal");
  }

  // --- Cache -------------------------------------------------------------
  {
    NativeVoiceCache cache(temp / "cache");
    NativeVoiceProfile profile;
    profile.id = "narrator";
    const auto path = cache.path_for(profile, "hello");
    check(path.parent_path() == cache.directory() &&
              path.extension() == ".wav",
          "cache path must live in the cache directory");
    check(cache.path_for(profile, "hello") == path &&
              cache.path_for(profile, "goodbye") != path,
          "cache names must be deterministic per line");
    check(!NativeVoiceCache::is_valid_wave(path),
          "missing wav must be invalid");
    write_wav(path);
    check(NativeVoiceCache::is_valid_wave(path),
          "well-formed wav must validate");
    check(cache.try_get_valid(path), "valid cache entry must be served");
  }

  // --- Playback: queue, dedupe, interrupt, subtitle fallback -------------
  {
    std::vector<NativeVoiceProfile> profiles{[] {
      NativeVoiceProfile p;
      p.id = "narrator";
      p.display_name = "Narrator";
      return p;
    }()};
    NativeVoiceProfileRegistry registry(profiles);
    std::vector<NativeVoiceRoleMapping> mappings{
        {"Narrator", "narrator", std::nullopt, "*"}};
    NativeCharacterVoiceResolver resolver(&registry, mappings);
    NativeVoiceCache playback_cache(temp / "playback-cache");
    NativeVoicePlayback playback({}, &registry, &resolver, &playback_cache);
    auto backend = std::make_unique<FakeBackend>();
    auto *fake = backend.get();
    playback.attach_backend(std::move(backend));

    std::vector<std::pair<int, double>> played;
    playback.bind(
        [&](const std::filesystem::path &path) {
          // Synthesize a 0.25 s stereo stream for every wav path.
          auto stream =
              std::make_shared<stellar::native_audio::PcmData>();
          stream->sample_rate = 48000;
          stream->channels = 2;
          stream->frames.resize(48000 / 4 * 2, .5f);
          (void)path;
          return stream;
        },
        [&](NativeVoicePlayback::Stream, double seconds) {
          played.emplace_back(1, seconds);
        },
        [] {});

    NativeSpeechRequest request;
    request.text = "A new chapter begins.";
    request.profile_id = "narrator";
    request.priority = 100;
    request.dedupe_key = "opening";
    NativeVoiceSpeakerContext speaker;
    speaker.role = VoiceSpeakerRole::Narrator;
    request.speaker_context = speaker;
    playback.speak(request);
    playback.update(.016);
    check(fake->synthesized == 1, "playback must dispatch synthesis");
    check(playback.is_speaking(), "playback must report an active line");
    // The second update consumes the completed synthesis future.
    playback.update(.016);
    check(played.size() == 1, "the decoded stream must reach the mixer");
    check(playback.ducking(), "active speech must drive ducking");
    check(playback.played_lines() == 1, "played line must count");
    check(playback.active_speaker_name() == "Narrator",
          "resolved speaker name must surface");
    check(!playback.active_subtitle().empty(),
          "the caption text must surface while playing");

    // Dedupe: the same key within 15 s is dropped.
    playback.speak(request);
    check(playback.pending_count() == 0 && fake->synthesized == 1,
          "duplicate request must dedupe");

    // A higher-priority request interrupts a non-interruptible=false line.
    NativeSpeechRequest urgent;
    urgent.text = "Hull integrity critical.";
    urgent.profile_id = "narrator";
    urgent.priority = 60;
    urgent.dedupe_key = "hull";
    urgent.interruptible = true;
    playback.speak(urgent);
    check(playback.pending_count() == 1, "queued urgent line expected");
    // Finish the current line, then the queue must drain.
    for (int i = 0; i < 600 && playback.is_speaking(); ++i)
      playback.update(.05);
    check(played.size() >= 2 || playback.played_lines() +
                                    playback.subtitle_lines() >= 2,
          "queued line must play after the active line");

    // Subtitle fallback when the backend is unavailable.
    NativeVoicePlayback silent({}, &registry, &resolver, nullptr);
    auto disabled = std::make_unique<FakeBackend>();
    disabled->usable = false;
    silent.attach_backend(std::move(disabled));
    silent.bind([](const std::filesystem::path &) { return nullptr; },
                [](NativeVoicePlayback::Stream, double) {}, [] {});
    NativeSpeechRequest fallback_request;
    fallback_request.text = "Subtitle only line.";
    fallback_request.profile_id = "narrator";
    fallback_request.priority = 40;
    fallback_request.dedupe_key = "fallback";
    silent.speak(fallback_request);
    silent.update(.016);
    check(silent.has_active_subtitle(), "unavailable backend must subtitle");
    check(silent.subtitle_lines() == 1, "subtitle line must count");
    check(silent.last_source() == "subtitle",
          "the fallback source must be reported");

    if (failures == 0)
      std::cout << "native voice contract tests passed\n";
  }

  return failures == 0 ? 0 : 1;
}
