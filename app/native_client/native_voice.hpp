#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::native_voice {

// Presentation-only voice contracts ported from the reference
// Game.Presentation.Audio.Voice layer (VoiceCore / VoiceEventRouter /
// CharacterVoiceResolver). None of these types read hidden simulation state,
// mutate the campaign, or touch an audio backend.

enum class VoiceFrequency { Minimal, Normal, Frequent };
enum class VoiceAudience { OwnCivilization, Observable, DirectCommunication, ObserverSafe };
enum class VoicePriority { Ambient = 0, Informational = 20, Important = 40,
                           Urgent = 60, Critical = 80, Cinematic = 100 };
enum class SpeechQueueBehavior { Enqueue, InterruptLowerPriority, ReplaceCategory };
enum class VoiceSpeakerRole {
  Narrator,
  FleetCommander,
  ChiefScientist,
  Diplomat,
  Governor,
  EconomicAdvisor,
  OperationsOfficer,
  ShipComputer,
  ColonyComputer,
  ExpeditionCommander,
  AlienDiplomat,
  AlienScientist,
  AlienCommander,
};

[[nodiscard]] std::optional<VoiceSpeakerRole> parse_speaker_role(std::string_view) noexcept;
[[nodiscard]] std::string_view speaker_role_name(VoiceSpeakerRole) noexcept;

// VoiceProfile — the profile fields the native presentation consumes.
struct NativeVoiceProfile {
  std::string id, display_name, role, presentation, character{"human"},
      species{"human"}, civilization{"human"}, sex{"neutral"}, culture{"en-US"},
      subtitle_name;
  float rate{}, pitch{}, resonance{};
  bool radio{}, synthetic{}, enabled{true};
  std::optional<std::string> preferred_voice, fallback_profile, portrait;
  std::unordered_map<std::string, std::string> pronunciations;
};

class NativeVoiceProfileRegistry final {
public:
  NativeVoiceProfileRegistry() = default;
  explicit NativeVoiceProfileRegistry(std::vector<NativeVoiceProfile> profiles);
  [[nodiscard]] static NativeVoiceProfileRegistry load(const std::filesystem::path &);
  [[nodiscard]] const NativeVoiceProfile *try_resolve(std::string_view id) const;
  [[nodiscard]] const NativeVoiceProfile *find(std::string_view id) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return profiles_.size(); }

private:
  std::vector<NativeVoiceProfile> profiles_;
  std::map<std::string, std::size_t, std::less<>> index_; // case-insensitive
};

struct NativeVoiceCharacter {
  std::string id, display_name, office;
  std::optional<std::string> voice_profile_id, portrait;
};

struct NativeVoiceSpeakerContext {
  VoiceSpeakerRole role{VoiceSpeakerRole::Narrator};
  int source_civilization_id{};
  std::string source_species_id;
  std::optional<std::string> exact_character_id, exact_voice_profile_id;
};

struct NativeResolvedSpeaker {
  std::string profile_id, display_name;
  VoiceSpeakerRole role{VoiceSpeakerRole::Narrator};
  std::optional<std::string> character_id, portrait;
  bool is_fallback{};
};

struct NativeVoiceRoleMapping {
  std::string role, profile;
  std::optional<int> civilization_id;
  std::string species{"*"};
};

// CharacterVoiceResolver port: role → roster character → civ/species/generic
// profile fallback. The optional current-character callback is supplied by the
// caller; without it every call resolves through the ranked mappings.
class NativeCharacterVoiceResolver final {
public:
  using CurrentCharacter =
      std::function<std::optional<NativeVoiceCharacter>(
          const NativeVoiceSpeakerContext &)>;
  NativeCharacterVoiceResolver(const NativeVoiceProfileRegistry *profiles,
                               std::vector<NativeVoiceRoleMapping> mappings);
  [[nodiscard]] static NativeCharacterVoiceResolver load(
      const NativeVoiceProfileRegistry *profiles, const std::filesystem::path &);
  void set_current_character(CurrentCharacter current);
  [[nodiscard]] std::optional<NativeResolvedSpeaker>
  resolve(const NativeVoiceSpeakerContext &) const;

private:
  const NativeVoiceProfileRegistry *profiles_;
  std::vector<NativeVoiceRoleMapping> mappings_;
  CurrentCharacter current_character_;
};

struct NativeVoiceOverrides {
  std::optional<VoiceSpeakerRole> speaker_role;
  std::optional<std::string> character_id, voice_profile_id, dialogue_key,
      exact_line, emotion, category;
  std::optional<int> priority;
  std::optional<bool> communications_filter, interruptible;
  std::optional<SpeechQueueBehavior> queue_behavior;
  std::optional<double> cooldown_seconds;
};

struct NativeGameplayVoiceEvent {
  std::string event_key;
  int source_civilization_id{};
  std::string unique_event_id;
  std::map<std::string, std::string> variables;
  std::int64_t simulation_tick{};
  std::string simulation_date;
  std::string source_species_id;
  VoiceAudience audience{VoiceAudience::OwnCivilization};
  std::optional<int> recipient_civilization_id;
  bool observer_evidence{}, first_occurrence{};
  std::optional<NativeVoiceOverrides> overrides;
};

struct NativeVoiceRoutingContext {
  int player_civilization_id{};
  VoiceFrequency frequency{VoiceFrequency::Normal};
  bool observer_mode{};
  std::optional<std::chrono::system_clock::time_point> presentation_time;
};

struct NativeVoiceCue {
  std::string event, profile, dialogue_key, category{"general"},
      emotion{"neutral"};
  std::vector<std::string> lines, first_lines;
  int priority{40};
  bool once{}, interruptible{true}, communications_filter{};
  double cooldown_seconds{12};
  VoiceSpeakerRole speaker_role{VoiceSpeakerRole::Narrator};
  VoiceFrequency frequency{VoiceFrequency::Normal};
  SpeechQueueBehavior queue_behavior{SpeechQueueBehavior::ReplaceCategory};
  std::optional<std::string> prerecorded_path;
};

struct NativeSpeechRequest {
  std::string profile_id, text;
  int priority{static_cast<int>(VoicePriority::Informational)};
  std::string dedupe_key, category{"general"}, emotion{"neutral"},
      event_id, localization_key;
  bool interruptible{true}, communications_filter{};
  SpeechQueueBehavior queue_behavior{SpeechQueueBehavior::Enqueue};
  std::chrono::system_clock::time_point expires_at{}, created_at{};
  std::optional<std::string> prerecorded_path, subtitle_text;
  std::optional<NativeVoiceSpeakerContext> speaker_context;
  std::optional<VoiceSpeakerRole> speaker_role;
  // Speaker resolution output (filled by the playback controller).
  std::optional<std::string> speaker_name, speaker_character_id, speaker_portrait;
};

// VoiceEventRouter port: authorization → cue lookup → once/cooldown/dedupe →
// deterministic variant selection → template render → SpeechRequest.
class NativeVoiceRouter final {
public:
  using Submit = std::function<void(NativeSpeechRequest)>;
  NativeVoiceRouter(std::vector<NativeVoiceCue> cues, Submit submit,
                    const NativeCharacterVoiceResolver *speakers = nullptr);
  [[nodiscard]] static NativeVoiceRouter from_file(
      const std::filesystem::path &, Submit,
      const NativeCharacterVoiceResolver *speakers = nullptr);
  void reset();
  [[nodiscard]] bool has_emitted(std::string_view key) const;
  [[nodiscard]] std::vector<std::string> event_keys() const;
  bool emit(const NativeGameplayVoiceEvent &, const NativeVoiceRoutingContext &);
  // Legacy single-argument emit used by the reference's developer console.
  bool emit(std::string_view key, std::string_view detail = "");

private:
  [[nodiscard]] bool emit_core(const NativeGameplayVoiceEvent &,
                               const NativeVoiceRoutingContext &,
                               std::optional<int> legacy_sequence);
  void prune_recent(std::chrono::system_clock::time_point now);

  std::unordered_map<std::string, NativeVoiceCue> cues_;
  std::unordered_map<std::string, int> counts_;
  std::unordered_map<std::string, std::chrono::system_clock::time_point>
      last_category_, recent_events_;
  Submit submit_;
  const NativeCharacterVoiceResolver *speakers_{};
};

// SpeechText.Normalize subset: whitespace collapse, profile/request
// pronunciations, percent/coordinate/ship-id/light-speed expansion, km/K,
// roman numerals and ISO dates rendered for a speech backend.
[[nodiscard]] std::string normalize_speech_text(
    std::string_view text,
    const std::unordered_map<std::string, std::string> *profile_terms = nullptr,
    const std::unordered_map<std::string, std::string> *request_terms = nullptr);

// VoiceSettings port — persisted next to the campaign save directory.
struct NativeVoiceSettings {
  bool enable_voices{true}, subtitles{true}, speaker_labels{true},
      no_interruptions{};
  float volume{1.f}, opacity{1.f}, chatter_level{1.f}, comms_intensity{1.f};
  int subtitle_size{18};
  VoiceFrequency frequency{VoiceFrequency::Normal};
  [[nodiscard]] VoiceFrequency effective_frequency() const noexcept {
    return chatter_level <= 0 ? VoiceFrequency::Minimal : frequency;
  }
  [[nodiscard]] NativeVoiceSettings sanitized() const noexcept;
  [[nodiscard]] static NativeVoiceSettings load(const std::filesystem::path &);
  void save(const std::filesystem::path &) const;
};

} // namespace stellar::native_voice
