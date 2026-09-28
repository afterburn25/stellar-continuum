#include "native_voice.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace stellar::native_voice {
namespace {

using Clock = std::chrono::system_clock;
constexpr std::size_t recent_event_limit = 512;

[[nodiscard]] std::string trim_copy(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

[[nodiscard]] bool blank(std::string_view value) noexcept {
  return trim_copy(value).empty();
}

[[nodiscard]] std::optional<std::string> null_if_blank(
    const std::optional<std::string> &value) {
  if (!value || blank(*value)) return std::nullopt;
  return value;
}

[[nodiscard]] std::string sanitize(std::string_view value) {
  // Reference: control characters become spaces, output is bounded at 256
  // code points, and runs of whitespace collapse to a single space.
  std::string bounded;
  bounded.reserve(value.size());
  for (const char character : value) {
    if (bounded.size() >= 256) break;
    bounded.push_back(
        std::iscntrl(static_cast<unsigned char>(character)) ? ' ' : character);
  }
  std::string collapsed;
  collapsed.reserve(bounded.size());
  bool space = true;
  for (const char character : bounded) {
    if (std::isspace(static_cast<unsigned char>(character))) {
      if (!space) collapsed.push_back(' ');
      space = true;
    } else {
      collapsed.push_back(character);
      space = false;
    }
  }
  if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  return collapsed;
}

[[nodiscard]] bool try_render(std::string_view templ,
                              const std::map<std::string, std::string> &variables,
                              std::string &text) {
  static const std::regex token(R"(\{([a-z0-9_]+)\})");
  bool missing = false;
  const std::string source(templ);
  std::string rendered;
  std::size_t cursor = 0;
  for (std::sregex_iterator it(source.begin(), source.end(), token), end;
       it != end; ++it) {
    rendered += source.substr(cursor, it->position() - cursor);
    const auto variable = variables.find(it->str(1));
    if (variable == variables.end()) missing = true;
    else rendered += sanitize(variable->second);
    cursor = it->position() + it->length();
  }
  rendered += source.substr(cursor);
  text = sanitize(rendered);
  return !missing && !text.empty();
}

[[nodiscard]] int select_variant(const NativeGameplayVoiceEvent &event,
                                 int count) {
  const std::string input = event.event_key + "|" + event.unique_event_id +
                            "|" + std::to_string(event.simulation_tick) + "|" +
                            event.simulation_date;
  std::uint32_t hash = 2166136261u;
  for (const unsigned char character : input) hash = (hash ^ character) * 16777619u;
  return static_cast<int>(hash % static_cast<std::uint32_t>(count));
}

[[nodiscard]] bool is_alien_role(VoiceSpeakerRole role) noexcept {
  return role == VoiceSpeakerRole::AlienDiplomat ||
         role == VoiceSpeakerRole::AlienScientist ||
         role == VoiceSpeakerRole::AlienCommander;
}

[[nodiscard]] bool is_human_species(std::string_view species) noexcept {
  return species.empty() || species == "human" || species == "terran_baseline";
}

[[nodiscard]] bool is_non_human_species(std::string_view species) noexcept {
  return !is_human_species(species);
}

[[nodiscard]] bool species_matches(std::string_view configured,
                                   std::string_view actual) noexcept {
  if (configured == "*") return true;
  if (actual.empty()) return false;
  if (configured.size() != actual.size()) return false;
  for (std::size_t i = 0; i < configured.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(configured[i])) !=
        std::tolower(static_cast<unsigned char>(actual[i])))
      return false;
  return true;
}

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
  return species_matches(a, b) && a != "*";
}

[[nodiscard]] std::optional<VoiceFrequency> parse_frequency(
    std::string_view value) noexcept {
  if (iequals(value, "Minimal")) return VoiceFrequency::Minimal;
  if (iequals(value, "Normal")) return VoiceFrequency::Normal;
  if (iequals(value, "Frequent")) return VoiceFrequency::Frequent;
  return std::nullopt;
}

[[nodiscard]] std::optional<SpeechQueueBehavior> parse_queue_behavior(
    std::string_view value) noexcept {
  if (iequals(value, "Enqueue")) return SpeechQueueBehavior::Enqueue;
  if (iequals(value, "InterruptLowerPriority"))
    return SpeechQueueBehavior::InterruptLowerPriority;
  if (iequals(value, "ReplaceCategory"))
    return SpeechQueueBehavior::ReplaceCategory;
  return std::nullopt;
}

[[nodiscard]] std::vector<std::string> read_lines(const nlohmann::json &node,
                                                 std::string_view key) {
  std::vector<std::string> result;
  const auto found = node.find(std::string(key));
  if (found == node.end() || !found->is_array()) return result;
  for (const auto &line : *found)
    if (line.is_string()) result.push_back(line.get<std::string>());
  return result;
}

[[nodiscard]] std::unordered_map<std::string, std::string> read_terms(
    const nlohmann::json &node, std::string_view key) {
  std::unordered_map<std::string, std::string> result;
  const auto found = node.find(std::string(key));
  if (found == node.end() || !found->is_object()) return result;
  for (const auto &[term, replacement] : found->items())
    if (replacement.is_string())
      result.emplace(term, replacement.get<std::string>());
  return result;
}

[[nodiscard]] std::optional<std::string> read_optional(
    const nlohmann::json &node, std::string_view key) {
  const auto found = node.find(std::string(key));
  if (found == node.end() || !found->is_string() || blank(found->get<std::string>()))
    return std::nullopt;
  return found->get<std::string>();
}

[[nodiscard]] std::string read_string(const nlohmann::json &node,
                                      std::string_view key,
                                      std::string fallback = {}) {
  const auto found = node.find(std::string(key));
  if (found == node.end() || !found->is_string()) return fallback;
  return found->get<std::string>();
}

[[nodiscard]] nlohmann::json read_json_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Voice data is unavailable: " +
                             path.generic_string());
  return nlohmann::json::parse(input, nullptr, true, true);
}

} // namespace

std::optional<VoiceSpeakerRole> parse_speaker_role(std::string_view value) noexcept {
  static constexpr std::pair<std::string_view, VoiceSpeakerRole> table[] = {
      {"Narrator", VoiceSpeakerRole::Narrator},
      {"FleetCommander", VoiceSpeakerRole::FleetCommander},
      {"ChiefScientist", VoiceSpeakerRole::ChiefScientist},
      {"Diplomat", VoiceSpeakerRole::Diplomat},
      {"Governor", VoiceSpeakerRole::Governor},
      {"EconomicAdvisor", VoiceSpeakerRole::EconomicAdvisor},
      {"OperationsOfficer", VoiceSpeakerRole::OperationsOfficer},
      {"ShipComputer", VoiceSpeakerRole::ShipComputer},
      {"ColonyComputer", VoiceSpeakerRole::ColonyComputer},
      {"ExpeditionCommander", VoiceSpeakerRole::ExpeditionCommander},
      {"AlienDiplomat", VoiceSpeakerRole::AlienDiplomat},
      {"AlienScientist", VoiceSpeakerRole::AlienScientist},
      {"AlienCommander", VoiceSpeakerRole::AlienCommander},
  };
  for (const auto &[name, role] : table)
    if (iequals(name, value)) return role;
  return std::nullopt;
}

std::string_view speaker_role_name(VoiceSpeakerRole role) noexcept {
  switch (role) {
  case VoiceSpeakerRole::Narrator: return "Narrator";
  case VoiceSpeakerRole::FleetCommander: return "FleetCommander";
  case VoiceSpeakerRole::ChiefScientist: return "ChiefScientist";
  case VoiceSpeakerRole::Diplomat: return "Diplomat";
  case VoiceSpeakerRole::Governor: return "Governor";
  case VoiceSpeakerRole::EconomicAdvisor: return "EconomicAdvisor";
  case VoiceSpeakerRole::OperationsOfficer: return "OperationsOfficer";
  case VoiceSpeakerRole::ShipComputer: return "ShipComputer";
  case VoiceSpeakerRole::ColonyComputer: return "ColonyComputer";
  case VoiceSpeakerRole::ExpeditionCommander: return "ExpeditionCommander";
  case VoiceSpeakerRole::AlienDiplomat: return "AlienDiplomat";
  case VoiceSpeakerRole::AlienScientist: return "AlienScientist";
  case VoiceSpeakerRole::AlienCommander: return "AlienCommander";
  }
  return "Narrator";
}

NativeVoiceProfileRegistry::NativeVoiceProfileRegistry(
    std::vector<NativeVoiceProfile> profiles) : profiles_(std::move(profiles)) {
  for (std::size_t i = 0; i < profiles_.size(); ++i) {
    const auto &id = profiles_[i].id;
    if (blank(id)) throw std::invalid_argument("A voice profile has no id.");
    std::string lowered = id;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (!index_.emplace(std::move(lowered), i).second)
      throw std::invalid_argument("Duplicate voice profile '" + id + "'.");
  }
}

NativeVoiceProfileRegistry NativeVoiceProfileRegistry::load(
    const std::filesystem::path &path) {
  const auto document = read_json_file(path);
  if (!document.is_array())
    throw std::invalid_argument("Voice profile data is empty.");
  std::vector<NativeVoiceProfile> profiles;
  for (const auto &node : document) {
    NativeVoiceProfile profile;
    profile.id = read_string(node, "id");
    profile.display_name = read_string(node, "displayName");
    profile.role = read_string(node, "role");
    profile.presentation = read_string(node, "presentation");
    profile.civilization = read_string(node, "civilization", "human");
    profile.species = read_string(node, "species", "human");
    profile.character = read_string(node, "character");
    profile.sex = read_string(node, "sex", "neutral");
    profile.culture = read_string(node, "culture", "en-US");
    profile.subtitle_name = read_string(node, "subtitleName");
    profile.rate = node.value("rate", 0.f);
    profile.pitch = node.value("pitch", 0.f);
    profile.resonance = node.value("resonance", 0.f);
    profile.radio = node.value("radio", false);
    profile.synthetic = node.value("synthetic", false);
    profile.enabled = node.value("enabled", true);
    profile.preferred_voice = read_optional(node, "preferredVoice");
    profile.fallback_profile = read_optional(node, "fallbackProfile");
    profile.portrait = read_optional(node, "portrait");
    profile.pronunciations = read_terms(node, "pronunciations");
    profiles.push_back(std::move(profile));
  }
  return NativeVoiceProfileRegistry(std::move(profiles));
}

const NativeVoiceProfile *NativeVoiceProfileRegistry::find(
    std::string_view id) const noexcept {
  std::string lowered(id);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  const auto found = index_.find(lowered);
  return found == index_.end() ? nullptr : &profiles_[found->second];
}

const NativeVoiceProfile *NativeVoiceProfileRegistry::try_resolve(
    std::string_view id) const {
  const NativeVoiceProfile *profile = find(id);
  std::unordered_set<std::string> visited;
  while (profile && !profile->enabled) {
    if (!visited.insert(profile->id).second || !profile->fallback_profile) {
      profile = nullptr;
      break;
    }
    profile = find(*profile->fallback_profile);
  }
  return profile;
}

NativeCharacterVoiceResolver::NativeCharacterVoiceResolver(
    const NativeVoiceProfileRegistry *profiles,
    std::vector<NativeVoiceRoleMapping> mappings)
    : profiles_(profiles), mappings_(std::move(mappings)) {
  if (!profiles_) throw std::invalid_argument("Voice profiles are required.");
}

NativeCharacterVoiceResolver NativeCharacterVoiceResolver::load(
    const NativeVoiceProfileRegistry *profiles,
    const std::filesystem::path &path) {
  const auto document = read_json_file(path);
  std::vector<NativeVoiceRoleMapping> mappings;
  if (document.is_array()) {
    for (const auto &node : document) {
      NativeVoiceRoleMapping mapping;
      mapping.role = read_string(node, "role");
      mapping.profile = read_string(node, "profile");
      mapping.species = read_string(node, "species", "*");
      if (node.contains("civilization") && node["civilization"].is_number())
        mapping.civilization_id = node["civilization"].get<int>();
      if (!blank(mapping.role) && !blank(mapping.profile))
        mappings.push_back(std::move(mapping));
    }
  }
  return NativeCharacterVoiceResolver(profiles, std::move(mappings));
}

void NativeCharacterVoiceResolver::set_current_character(
    CurrentCharacter current) {
  current_character_ = std::move(current);
}

std::optional<NativeResolvedSpeaker> NativeCharacterVoiceResolver::resolve(
    const NativeVoiceSpeakerContext &context) const {
  const bool require_non_human = is_alien_role(context.role) ||
      is_non_human_species(context.source_species_id);
  const auto try_profile = [&](const std::optional<std::string> &id)
      -> const NativeVoiceProfile * {
    if (!id || blank(*id)) return nullptr;
    const auto *profile = profiles_->try_resolve(*id);
    if (!profile) return nullptr;
    if (require_non_human && is_human_species(profile->species)) return nullptr;
    return profile;
  };
  const auto from_profile = [&](const NativeVoiceProfile &profile,
                                const std::optional<std::string> &character_id,
                                bool fallback) {
    return NativeResolvedSpeaker{profile.id,
                                 blank(profile.subtitle_name)
                                     ? profile.display_name
                                     : profile.subtitle_name,
                                 context.role, character_id,
                                 profile.portrait, fallback};
  };
  const auto named = [&](const NativeVoiceCharacter &character,
                         const NativeVoiceProfile &profile, bool fallback) {
    return NativeResolvedSpeaker{profile.id, character.display_name,
                                 context.role, character.id,
                                 character.portrait ? character.portrait
                                                    : profile.portrait,
                                 fallback};
  };
  const auto character = current_character_ ? current_character_(context)
                                            : std::optional<NativeVoiceCharacter>{};
  const auto character_matches = [&] {
    return character && (!context.exact_character_id ||
                         iequals(*context.exact_character_id, character->id));
  };

  if (const auto *exact = try_profile(context.exact_voice_profile_id))
    return character_matches() ? named(*character, *exact, false)
                               : from_profile(*exact, context.exact_character_id,
                                              false);

  if (character_matches())
    if (const auto *character_profile =
            try_profile(character->voice_profile_id))
      return named(*character, *character_profile, false);

  std::vector<const NativeVoiceRoleMapping *> ranked;
  for (const auto &mapping : mappings_) {
    const auto role = parse_speaker_role(mapping.role);
    if (!role || *role != context.role) continue;
    if (mapping.civilization_id &&
        *mapping.civilization_id != context.source_civilization_id)
      continue;
    if (!species_matches(mapping.species, context.source_species_id)) continue;
    ranked.push_back(&mapping);
  }
  std::ranges::stable_sort(ranked, [&](const auto *a, const auto *b) {
    const auto specificity = [](const NativeVoiceRoleMapping &mapping) {
      return (mapping.civilization_id ? 2 : 0) +
             (mapping.species != "*" ? 1 : 0);
    };
    return specificity(*a) > specificity(*b);
  });
  for (const auto *mapping : ranked)
    if (const auto *profile = try_profile(mapping->profile))
      return character_matches() ? named(*character, *profile, true)
                                 : from_profile(*profile, std::nullopt, true);
  return std::nullopt;
}

NativeVoiceRouter::NativeVoiceRouter(std::vector<NativeVoiceCue> cues,
                                     Submit submit,
                                     const NativeCharacterVoiceResolver *speakers)
    : submit_(std::move(submit)), speakers_(speakers) {
  if (!submit_) throw std::invalid_argument("Voice router requires a submit sink.");
  for (auto &cue : cues)
    if (!blank(cue.event)) cues_.emplace(cue.event, std::move(cue));
}

NativeVoiceRouter NativeVoiceRouter::from_file(
    const std::filesystem::path &path, Submit submit,
    const NativeCharacterVoiceResolver *speakers) {
  const auto document = read_json_file(path);
  std::vector<NativeVoiceCue> cues;
  if (document.is_array()) {
    for (const auto &node : document) {
      NativeVoiceCue cue;
      cue.event = read_string(node, "event");
      cue.profile = read_string(node, "profile");
      cue.dialogue_key = read_string(node, "dialogueKey");
      cue.category = read_string(node, "category", "general");
      cue.emotion = read_string(node, "emotion", "neutral");
      cue.lines = read_lines(node, "lines");
      cue.first_lines = read_lines(node, "firstLines");
      cue.priority = node.value("priority", 40);
      cue.once = node.value("once", false);
      cue.interruptible = node.value("interruptible", true);
      cue.communications_filter = node.value("communicationsFilter", false);
      cue.cooldown_seconds = node.value("cooldownSeconds", 12.0);
      if (const auto role = parse_speaker_role(read_string(node, "speakerRole")))
        cue.speaker_role = *role;
      if (const auto frequency = parse_frequency(read_string(node, "frequency")))
        cue.frequency = *frequency;
      if (const auto behavior =
              parse_queue_behavior(read_string(node, "queueBehavior")))
        cue.queue_behavior = *behavior;
      cue.prerecorded_path = read_optional(node, "prerecordedPath");
      cue.subtitle_text = read_optional(node, "subtitleText");
      if (!blank(cue.event)) cues.push_back(std::move(cue));
    }
  }
  return NativeVoiceRouter(std::move(cues), std::move(submit), speakers);
}

void NativeVoiceRouter::reset() {
  counts_.clear();
  last_category_.clear();
  recent_events_.clear();
}

bool NativeVoiceRouter::has_emitted(std::string_view key) const {
  static const std::unordered_map<std::string, std::string> aliases{
      {"research", "research.completed"},
      {"construction", "construction.completed"},
      {"shipyard", "construction.orbital_shipyard.completed"},
      {"ship_launch", "ship.completed"},
      {"departure", "ship.interstellar.first_launch"},
      {"arrival", "exploration.system.reached"},
      {"discovery", "exploration.anomaly.discovered"},
      {"survey", "exploration.survey.completed"},
      {"colony", "colony.founded"},
      {"unknown_contact", "contact.unknown.detected"},
      {"first_contact", "contact.first"},
      {"alien_transmission", "diplomacy.alien.transmission"},
      {"critical_hull", "combat.hull.critical"},
      {"treasury", "economy.treasury.critical"},
  };
  const std::string lookup(key);
  if (const auto count = counts_.find(lookup);
      count != counts_.end() && count->second > 0)
    return true;
  const auto alias = aliases.find(lookup);
  return alias != aliases.end() &&
         counts_.find(alias->second) != counts_.end() &&
         counts_.at(alias->second) > 0;
}

std::vector<std::string> NativeVoiceRouter::event_keys() const {
  std::vector<std::string> keys;
  keys.reserve(cues_.size());
  for (const auto &[key, cue] : cues_) keys.push_back(key);
  std::ranges::sort(keys);
  return keys;
}

bool NativeVoiceRouter::emit(std::string_view key, std::string_view detail) {
  const int sequence = counts_[std::string(key)];
  NativeGameplayVoiceEvent event;
  event.event_key = std::string(key);
  event.source_civilization_id = 0;
  event.unique_event_id =
      "legacy:" + std::string(key) + ":" + std::to_string(sequence);
  event.variables.emplace("detail", std::string(detail));
  event.simulation_tick = sequence;
  event.simulation_date = std::to_string(sequence);
  return emit_core(event, NativeVoiceRoutingContext{0, VoiceFrequency::Frequent},
                   sequence);
}

bool NativeVoiceRouter::emit(const NativeGameplayVoiceEvent &event,
                             const NativeVoiceRoutingContext &context) {
  return emit_core(event, context, std::nullopt);
}

bool NativeVoiceRouter::emit_core(
    const NativeGameplayVoiceEvent &event,
    const NativeVoiceRoutingContext &context,
    std::optional<int> legacy_sequence) {
  // Authorization — identical to the reference IsAuthorized.
  bool authorized = event.source_civilization_id == context.player_civilization_id;
  if (!authorized) {
    switch (event.audience) {
    case VoiceAudience::DirectCommunication:
      authorized = event.recipient_civilization_id &&
                   *event.recipient_civilization_id ==
                       context.player_civilization_id;
      break;
    case VoiceAudience::Observable:
      authorized = event.observer_evidence;
      break;
    case VoiceAudience::ObserverSafe:
      authorized = context.observer_mode && event.observer_evidence;
      break;
    case VoiceAudience::OwnCivilization:
      break;
    }
  }
  if (!authorized || blank(event.unique_event_id)) return false;

  const auto *overrides = event.overrides ? &*event.overrides : nullptr;
  const auto override_dialogue =
      overrides ? null_if_blank(overrides->dialogue_key) : std::nullopt;
  const std::string dialogue_key =
      override_dialogue ? *override_dialogue : event.event_key;
  const NativeVoiceCue *cue = nullptr;
  NativeVoiceCue exact_cue;
  if (const auto found = cues_.find(dialogue_key); found != cues_.end()) {
    cue = &found->second;
  } else {
    const auto exact_line =
        overrides ? null_if_blank(overrides->exact_line) : std::nullopt;
    if (!exact_line || !overrides || !overrides->speaker_role) return false;
    exact_cue.event = dialogue_key;
    exact_cue.lines = {*exact_line};
    exact_cue.priority =
        overrides->priority.value_or(static_cast<int>(VoicePriority::Important));
    exact_cue.cooldown_seconds = 0;
    exact_cue.dialogue_key = dialogue_key;
    exact_cue.speaker_role = *overrides->speaker_role;
    exact_cue.frequency = VoiceFrequency::Minimal;
    cue = &exact_cue;
  }
  if (cue->lines.empty() || context.frequency < cue->frequency) return false;

  const auto now = context.presentation_time.value_or(Clock::now());
  prune_recent(now);
  if (recent_events_.contains(event.unique_event_id)) return false;

  const std::string count_key = std::to_string(event.source_civilization_id) +
                                ":" + dialogue_key;
  const int count = counts_[count_key];
  if (cue->once && count > 0) return false;

  const std::string category =
      (overrides ? null_if_blank(overrides->category) : std::nullopt)
          .value_or(!blank(cue->category) ? cue->category : dialogue_key);
  const double cooldown = std::clamp(
      (overrides && overrides->cooldown_seconds ? *overrides->cooldown_seconds
                                                : cue->cooldown_seconds),
      0., 3600.);
  const std::string cooldown_key =
      std::to_string(event.source_civilization_id) + ":" + category;
  if (const auto previous = last_category_.find(cooldown_key);
      previous != last_category_.end() &&
      now - previous->second < std::chrono::duration<double>(cooldown))
    return false;

  const bool use_first =
      event.first_occurrence || (legacy_sequence && count == 0);
  const auto &candidates =
      use_first && !cue->first_lines.empty() ? cue->first_lines : cue->lines;
  const int selected = legacy_sequence.value_or(select_variant(
      event, static_cast<int>(candidates.size())));
  const std::string &templ =
      (overrides ? null_if_blank(overrides->exact_line) : std::nullopt)
          .value_or(candidates[selected % candidates.size()]);
  std::string text;
  if (!try_render(templ, event.variables, text)) return false;

  const VoiceSpeakerRole role =
      (overrides && overrides->speaker_role) ? *overrides->speaker_role
                                             : cue->speaker_role;
  NativeSpeechRequest request;
  request.profile_id =
      (overrides ? null_if_blank(overrides->voice_profile_id) : std::nullopt)
          .value_or(legacy_sequence ? cue->profile
                                    : std::string("voice_profile_unresolved"));
  request.text = text;
  request.priority = std::clamp(
      (overrides && overrides->priority ? *overrides->priority : cue->priority),
      0, 100);
  request.category = category;
  request.event_id = event.unique_event_id;
  request.localization_key =
      "voice." + (!blank(cue->dialogue_key) ? cue->dialogue_key : dialogue_key) +
      "." + std::to_string(selected % candidates.size());
  request.prerecorded_path = cue->prerecorded_path;
  request.subtitle_text = cue->subtitle_text;
  request.dedupe_key = std::to_string(event.source_civilization_id) + ":" +
                       category + ":" + text;
  request.interruptible =
      overrides && overrides->interruptible ? *overrides->interruptible
                                            : cue->interruptible;
  request.queue_behavior =
      overrides && overrides->queue_behavior ? *overrides->queue_behavior
                                             : cue->queue_behavior;
  request.emotion =
      (overrides ? null_if_blank(overrides->emotion) : std::nullopt)
          .value_or(cue->emotion);
  request.communications_filter =
      overrides && overrides->communications_filter
          ? *overrides->communications_filter
          : cue->communications_filter;
  request.created_at = now;
  request.expires_at =
      now + std::chrono::seconds(
                request.priority >= static_cast<int>(VoicePriority::Critical)
                    ? 15
                    : 40);
  NativeVoiceSpeakerContext speaker_context;
  speaker_context.role = role;
  speaker_context.source_civilization_id = event.source_civilization_id;
  speaker_context.source_species_id = event.source_species_id;
  speaker_context.exact_character_id =
      overrides ? overrides->character_id : std::nullopt;
  speaker_context.exact_voice_profile_id =
      overrides ? overrides->voice_profile_id : std::nullopt;
  request.speaker_context = speaker_context;
  request.speaker_role = role;

  counts_[count_key] = count + 1;
  ++counts_[dialogue_key];
  last_category_[cooldown_key] = now;
  recent_events_[event.unique_event_id] = now;
  submit_(std::move(request));
  return true;
}

void NativeVoiceRouter::prune_recent(Clock::time_point now) {
  for (auto it = recent_events_.begin(); it != recent_events_.end();) {
    if (now - it->second > std::chrono::hours(1))
      it = recent_events_.erase(it);
    else
      ++it;
  }
  while (recent_events_.size() > recent_event_limit) {
    const auto oldest = std::ranges::min_element(
        recent_events_, [](const auto &a, const auto &b) {
          return a.second < b.second;
        });
    recent_events_.erase(oldest);
  }
}

// --- SpeechText.Normalize subset -----------------------------------------

namespace {
[[nodiscard]] std::string speak_digits(std::string_view digits) {
  std::string out;
  for (const char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) continue;
    if (!out.empty()) out += ' ';
    out += c;
  }
  return out;
}

[[nodiscard]] std::string roman_to_words(std::string_view numeral) {
  static const std::unordered_map<std::string, std::string> words{
      {"I", "one"},   {"II", "two"},   {"III", "three"}, {"IV", "four"},
      {"V", "five"},  {"VI", "six"},   {"VII", "seven"}, {"VIII", "eight"},
      {"IX", "nine"}, {"X", "ten"},    {"XI", "eleven"}, {"XII", "twelve"},
  };
  const auto found = words.find(std::string(numeral));
  return found == words.end() ? std::string(numeral) : found->second;
}

[[nodiscard]] std::string collapse(std::string_view value) {
  static const std::regex spaces(R"(\s+)");
  return std::regex_replace(std::string(value), spaces, " ");
}

[[nodiscard]] std::unordered_map<std::string, std::string> merge_terms(
    std::initializer_list<const std::unordered_map<std::string, std::string> *>
        sources) {
  std::unordered_map<std::string, std::string> merged;
  for (const auto *source : sources)
    if (source) merged.insert(source->begin(), source->end());
  return merged;
}

// std::regex_replace cannot take a mapping callback, so matches are rebuilt
// manually. A nullopt keeps the matched text unchanged.
template <typename Map>
[[nodiscard]] std::string regex_map(std::string_view source,
                                    const std::regex &pattern, Map &&map) {
  const std::string input(source);
  std::string output;
  std::size_t cursor = 0;
  for (std::sregex_iterator it(input.begin(), input.end(), pattern), end;
       it != end; ++it) {
    const auto replacement = map(*it, input);
    output += input.substr(cursor, it->position() - cursor);
    output += replacement ? *replacement : it->str();
    cursor = it->position() + it->length();
  }
  output += input.substr(cursor);
  return output;
}

[[nodiscard]] bool word_at(const std::string &input, std::size_t at) noexcept {
  return at < input.size() &&
         (std::isalnum(static_cast<unsigned char>(input[at])) ||
          input[at] == '_');
}
} // namespace

std::string normalize_speech_text(
    std::string_view text,
    const std::unordered_map<std::string, std::string> *profile_terms,
    const std::unordered_map<std::string, std::string> *request_terms) {
  static const std::unordered_map<std::string, std::string> global_terms{
      {"Stellar Continuum", "Stellar Continuum"}, {"Sol", "Sohl"},
      {"Alpha Centauri", "Alpha Sen-tor-eye"},
      {"Proxima Centauri", "Proxima Sen-tor-eye"},
      {"Thalori", "Tha-lor-ee"}, {"FTL", "F T L"},
      {"AU", "astronomical units"},
  };
  static const std::regex ship_id(R"(\b([A-Z]{2,6})-(\d{1,6})\b)");
  // The reference's lookbehind/lookahead are folded into boundary checks.
  static const std::regex coordinates(
      R"((-?\d+(?:\.\d+)?),\s+(-?\d+(?:\.\d+)?))");
  static const std::regex percent(R"((-?\d+(?:\.\d+)?)\s*%)");
  static const std::regex clock_time(R"(\b(\d{1,2}):(\d{2})\b)");
  static const std::regex roman(R"(\b([IVX]{2,5})\b)");
  static const std::regex light_speed(R"(\b(-?\d+(?:\.\d+)?)\s*c\b)",
                                      std::regex_constants::icase);
  static const std::regex kilometers(R"(\bkm\b)", std::regex_constants::icase);
  static const std::regex kelvin(R"(\bK\b)");
  static const std::regex roman_suffix(R"(\b(planet|mark|sector|class) I\b)",
                                       std::regex_constants::icase);
  static const std::regex iso_date(R"(\b(\d{4})-(\d{2})-(\d{2})\b)");

  std::string value;
  {
    std::string source(text);
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    std::replace(source.begin(), source.end(), '\n', ' ');
    value = collapse(source);
  }
  value = regex_map(value, ship_id, [](const std::smatch &match, const std::string &) {
    std::string letters;
    for (const char c : match[1].str()) {
      if (!letters.empty()) letters += ' ';
      letters += c;
    }
    return std::optional<std::string>(letters + " " +
                                    speak_digits(match[2].str()));
  });

  // Pronunciations — case-insensitive whole-phrase replacement.
  const auto merged = merge_terms({&global_terms, profile_terms, request_terms});
  for (const auto &[term, replacement] : merged) {
    std::string lowered = value;
    std::string needle = term;
    const auto lower = [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    };
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), lower);
    std::transform(needle.begin(), needle.end(), needle.begin(), lower);
    std::string out;
    std::size_t cursor = 0;
    for (;;) {
      const auto at = lowered.find(needle, cursor);
      if (at == std::string::npos) break;
      const bool left_ok = at == 0 || !word_at(lowered, at - 1);
      const auto right = at + needle.size();
      const bool right_ok = right >= lowered.size() || !word_at(lowered, right);
      if (!left_ok || !right_ok) { cursor = at + 1; continue; }
      out += value.substr(cursor, at - cursor);
      out += replacement;
      cursor = right;
    }
    out += value.substr(cursor);
    value = std::move(out);
  }

  value = regex_map(value, coordinates,
                    [](const std::smatch &match, const std::string &input) {
                      const auto begin = static_cast<std::size_t>(match.position(0));
                      const auto end = begin + match.length();
                      if ((begin > 0 && word_at(input, begin - 1)) ||
                          word_at(input, end))
                        return std::optional<std::string>{};
                      return std::optional<std::string>(match[1].str() + " by " +
                                                        match[2].str());
                    });
  value = regex_map(value, percent,
                    [](const std::smatch &match, const std::string &input) {
                      const auto begin = static_cast<std::size_t>(match.position(0));
                      if (begin > 0 &&
                          std::isalnum(static_cast<unsigned char>(input[begin - 1])))
                        return std::optional<std::string>{};
                      return std::optional<std::string>(match[1].str() + " percent");
                    });
  value = std::regex_replace(value, clock_time, "$1 $2 hours");
  value = std::regex_replace(value, light_speed, "$1 times the speed of light");
  value = std::regex_replace(value, kilometers, "kilometers");
  value = std::regex_replace(value, kelvin, "kelvin");
  value = regex_map(value, roman, [](const std::smatch &match, const std::string &) {
    return std::optional<std::string>(roman_to_words(match[1].str()));
  });
  value = std::regex_replace(value, roman_suffix, "$1 one");
  value = regex_map(value, iso_date, [](const std::smatch &match, const std::string &) {
    static const char *months[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    const auto month = std::stoi(match[2].str());
    if (month < 1 || month > 12)
      return std::optional<std::string>(match.str());
    return std::optional<std::string>(
        std::string(months[month]) + " " +
        std::to_string(std::stoi(match[3].str())) + ", " + match[1].str());
  });
  return trim_copy(collapse(value));
}

NativeVoiceSettings NativeVoiceSettings::sanitized() const noexcept {
  auto copy = *this;
  const auto clamp01 = [](float v) {
    return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 1.f;
  };
  copy.volume = clamp01(volume);
  copy.subtitle_size = std::clamp(subtitle_size, 12, 42);
  copy.opacity = clamp01(opacity);
  copy.chatter_level = clamp01(chatter_level);
  copy.comms_intensity = clamp01(comms_intensity);
  if (frequency != VoiceFrequency::Minimal && frequency != VoiceFrequency::Normal &&
      frequency != VoiceFrequency::Frequent)
    copy.frequency = VoiceFrequency::Normal;
  return copy;
}

NativeVoiceSettings NativeVoiceSettings::load(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return {};
  try {
    const auto document = read_json_file(path);
    NativeVoiceSettings settings;
    settings.enable_voices = document.value("enableVoices", true);
    settings.volume = document.value("volume", 1.f);
    settings.subtitles = document.value("subtitles", true);
    settings.subtitle_size = document.value("subtitleSize", 18);
    settings.opacity = document.value("opacity", 1.f);
    settings.speaker_labels = document.value("speakerLabels", true);
    settings.chatter_level = document.value("chatterLevel", 1.f);
    settings.no_interruptions = document.value("noInterruptions", false);
    settings.comms_intensity = document.value("commsIntensity", 1.f);
    if (const auto frequency =
            parse_frequency(read_string(document, "frequency")))
      settings.frequency = *frequency;
    return settings.sanitized();
  } catch (const std::exception &) {
    return {};
  }
}

void NativeVoiceSettings::save(const std::filesystem::path &path) const {
  const auto settings = sanitized();
  std::error_code error;
  const auto directory = path.parent_path();
  if (!directory.empty()) std::filesystem::create_directories(directory, error);
  const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
  nlohmann::json document;
  document["enableVoices"] = settings.enable_voices;
  document["volume"] = settings.volume;
  document["subtitles"] = settings.subtitles;
  document["subtitleSize"] = settings.subtitle_size;
  document["opacity"] = settings.opacity;
  document["speakerLabels"] = settings.speaker_labels;
  document["chatterLevel"] = settings.chatter_level;
  document["noInterruptions"] = settings.no_interruptions;
  document["commsIntensity"] = settings.comms_intensity;
  document["frequency"] = settings.frequency == VoiceFrequency::Minimal
                              ? "Minimal"
                              : settings.frequency == VoiceFrequency::Frequent
                                    ? "Frequent"
                                    : "Normal";
  {
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    output << document.dump(2);
  }
  std::filesystem::rename(tmp, path, error);
}

} // namespace stellar::native_voice
