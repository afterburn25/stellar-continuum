#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

class EventBus;

// Data-driven mission/event chains. Definitions are parsed from JSON:
// {
//   "id": "anomaly_whisper",
//   "triggers": [{"event":"SystemEntered",
//                 "conditions":[{"field":"system_id","equals":"42"}],
//                 "stage":"arrival"}],
//   "stages": {
//     "arrival": {"title_key":"M_W_TITLE","body_key":"M_W_BODY",
//                  "timer_days": 30, "timeout_stage":"expired",
//                  "choices":[{"id":"investigate","next":"dig",
//                              "effects":["spawn_excavation"]}]}
//   }
// }
// Effects and transition notifications reach gameplay through the EventBus
// as MissionEffectEvent payloads — the framework never calls game code
// directly.

struct MissionCondition {
  std::string field;   // dotted path into the event payload, e.g. "fleet.civ"
  std::string equals;  // string comparison of the scalar value
};

struct MissionChoice {
  std::string id;
  std::string next_stage;
  std::vector<std::string> effects;
};

struct MissionStage {
  std::string id;
  std::string title_key;   // localization key
  std::string body_key;    // localization key
  double timer_days{};     // 0 = waits for a choice
  std::string timeout_stage;
  std::vector<MissionChoice> choices;
};

struct MissionTrigger {
  std::string event;
  std::vector<MissionCondition> conditions;
  std::string stage;
};

struct MissionDefinition {
  std::string id;
  std::vector<MissionTrigger> triggers;
  std::unordered_map<std::string, MissionStage> stages;

  static std::optional<MissionDefinition>
  parse(std::string_view json_document, std::string *error = nullptr);
};

// Emitted on the bus whenever a stage transition schedules gameplay work.
struct MissionEffectEvent {
  std::string mission_id;
  std::uint64_t instance_id{};
  std::string stage_id;
  std::vector<std::string> effects;
};

struct MissionInstance {
  std::uint64_t id{};
  std::string mission_id;
  std::string stage_id;
  double days_remaining{}; // meaningful when the stage has a timer
};

// Owns running instances. advance() is called with simulated elapsed days
// (strategic clock time, not wall time) so timers follow game speed and
// pausing. Instance state is serializable for save integration.
class MissionRuntime {
public:
  explicit MissionRuntime(EventBus *bus = nullptr);

  bool add_definition(MissionDefinition definition, std::string *error);
  void clear();

  // Called for every domain event of interest; `payload` is the event's
  // JSON projection. Starts a new instance on the first matching trigger.
  void handle_event(std::string_view event_name, std::string_view payload);

  // Advances timers; transitions timed-out stages and emits effects.
  void advance(double elapsed_days);

  // Applies a player/AI choice. Returns false for unknown instance/choice.
  bool choose(std::uint64_t instance_id, std::string_view choice_id);

  std::vector<MissionInstance> instances() const;
  const MissionDefinition *definition(std::string_view id) const;

  // Snapshot/restore for save compatibility.
  std::string serialize() const;
  bool restore(std::string_view document, std::string *error = nullptr);

private:
  void enter_stage(MissionInstance &instance, const std::string &stage_id);
  bool conditions_match(const MissionTrigger &trigger,
                        std::string_view payload) const;

  EventBus *bus_;
  std::unordered_map<std::string, MissionDefinition> definitions_;
  std::vector<MissionInstance> instances_;
  std::uint64_t next_instance_id_{1};
};

} // namespace stellar::engine
