#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Data-driven gameplay scripting platform. Definitions arrive as JSON inside
// content packages; this engine layer owns parsing, strict validation,
// scheduling, deterministic weighted selection and instance state. All access
// to authoritative game state goes through ScriptedContentAdapter — the game
// (Core) implements leaf checks and effect application over its own
// authoritative services, so nothing here mutates or mirrors simulation
// state.
//
// Document shape:
// {
//   "scripted_events": [{
//     "id": "anomaly_whisper",
//     "on": "SurveyCompleted",                 // event-driven trigger, or
//     "poll_days": 30,                          // cadence-poll trigger, or
//     "trigger": {"all":[{"check":"treasury_at_least",
//                          "scope":{"kind":"civilization","id":-1},
//                          "args":{"amount":"500"}}]},
//     "weight": 10, "once_per_campaign": true, "cooldown_days": 0,
//     "scope": {"kind":"system","id":-1},
//     "effects": [{"do":"grant_modifier","scope":{...},"args":{...}}],
//     "follow_ups":[{"id":"whisper_dig","delay_days":5,"weight":3}]
//   }]
// }
// Scope id -1 means "contextual" — resolved from the firing context (the
// entity the trigger ran for / the payload's origin). Concrete ids address
// authoritative objects directly.
//
// Reserved leaf checks are evaluated by the runtime itself rather than the
// adapter so their randomness rides the persisted deterministic roll stream:
//   "random_chance"  args: probability (required, 0..1)

enum class ScriptScopeKind {
  global, civilization, colony, system, body, fleet, origin
};

struct ScriptScope {
  ScriptScopeKind kind{ScriptScopeKind::global};
  std::int64_t id{-1};
};

// Recursive condition tree: leaf {check,scope,args} or group
// {all|any:[children]} / {not:{child}}.
struct ScriptCondition {
  enum class Op { leaf, all, any, negation };
  Op op{Op::leaf};
  std::string check;
  ScriptScope scope;
  std::vector<std::pair<std::string, std::string>> args;
  std::vector<ScriptCondition> children;
};

struct ScriptEffect {
  std::string action;
  ScriptScope scope;
  std::vector<std::pair<std::string, std::string>> args;
};

struct ScriptFollowUp {
  std::string event_id;
  double delay_days{};
  double weight{1.0};
};

struct ScriptedEventDefinition {
  std::string id;
  // Exactly one firing mode: "on" names a bus/domain event, poll_days>0 asks
  // the runtime to evaluate `trigger` on that cadence for each eligible
  // context the adapter enumerates, and neither means manual firing only.
  std::string on;
  double poll_days{};
  ScriptCondition trigger;
  double weight{1.0};
  bool once_per_campaign{};
  // Once per resolved definition scope (e.g. one anomaly outcome per body)
  // rather than once for the whole campaign. The fired key records the
  // resolved scope id; a scope resolving to nothing behaves campaign-wide.
  bool once_per_scope{};
  double cooldown_days{};
  ScriptScope scope;
  std::vector<ScriptEffect> effects;
  std::vector<ScriptFollowUp> follow_ups;
  bool has_trigger{};
  std::string file; // provenance for diagnostics

  static std::optional<ScriptedEventDefinition>
  parse(std::string_view json_object, std::string_view file, std::string *error);
};

struct ScriptLoadError {
  std::string file;
  std::string object_id;
  std::string message;
};

// Resolved firing context: contextual scopes bind to these ids.
struct ScriptFiringContext {
  std::int64_t civilization{-1};
  std::int64_t colony{-1};
  std::int64_t system{-1};
  std::int64_t body{-1};
  std::int64_t fleet{-1};
  std::int64_t origin{-1};
  std::string_view event_name{};
  std::string_view event_payload{};
};

// Implemented by the game layer. Every leaf check and effect resolves here;
// the adapter answers over authoritative Core state and routes effects into
// real command services. Validation hooks let the adapter reject unknown
// checks/effects and malformed arguments (e.g. unknown technology ids) at
// load time rather than first fire.
class ScriptedContentAdapter {
public:
  virtual ~ScriptedContentAdapter() = default;

  // Leaf validation at load time; return an error string to reject.
  virtual std::string validate_condition(const ScriptCondition &leaf,
                                         std::string_view file,
                                         std::string_view object_id) = 0;
  virtual std::string validate_effect(const ScriptEffect &effect,
                                      std::string_view file,
                                      std::string_view object_id) = 0;

  // Enumerate firing contexts for cadence-polled definitions. The adapter
  // knows which scope universe applies (e.g. every player-visible system).
  virtual std::vector<ScriptFiringContext>
  enumerate_contexts(const ScriptedEventDefinition &definition) = 0;

  virtual bool evaluate_condition(const ScriptCondition &leaf,
                                  const ScriptFiringContext &context) = 0;
  virtual void apply_effect(const ScriptEffect &effect,
                            const ScriptFiringContext &context) = 0;
  // Notification hook for fired events (UI feed, chronicle, mission launch).
  virtual void event_fired(const ScriptedEventDefinition &definition,
                           const ScriptFiringContext &context) = 0;
};

struct ScheduledScriptedEvent {
  std::string event_id;
  ScriptFiringContext context;
  double fire_day{};
};

// Registry + runtime: definitions are immutable after load; runtime state
// (fired-once flags, cooldowns, scheduled follow-ups, roll cursor) is
// serializable and deterministic — the weighted pick consumes a splitmix64
// stream seeded per campaign, persisted across save/load.
class ScriptedContentRuntime {
public:
  ScriptedContentRuntime() = default;
  explicit ScriptedContentRuntime(std::uint64_t seed) : roll_state_(seed ? seed : 1) {}

  void attach_adapter(ScriptedContentAdapter *adapter) noexcept {
    adapter_ = adapter;
  }

  // Loads one JSON document. `file` labels diagnostics. Existing ids are
  // preserved — a reload clears and re-adds atomically only on success.
  bool load_document(std::string_view json_document, std::string_view file,
                     std::vector<ScriptLoadError> *errors = nullptr);
  void clear();

  [[nodiscard]] std::size_t definition_count() const noexcept {
    return definitions_.size();
  }
  [[nodiscard]] const ScriptedEventDefinition *definition(std::string_view id) const;

  // Event-driven firing: dispatch to definitions whose `on` matches.
  // `simulation_day` stamps follow-up scheduling; defaults to the last
  // advanced day for callers that fire between advance() steps.
  void handle_event(std::string_view event_name,
                    const ScriptFiringContext &context,
                    double simulation_day = -1.0);

  // Advances simulation time: fires due scheduled follow-ups first, then
  // evaluates cadence-poll triggers whose window arrived.
  void advance(double simulation_day);

  // Manual/developer firing path — runs the same trigger/effect pipeline
  // with an explicit context. `force` skips the trigger check.
  bool fire(std::string_view event_id, const ScriptFiringContext &context,
            bool force = false);

  [[nodiscard]] const std::vector<ScheduledScriptedEvent> &scheduled() const noexcept {
    return scheduled_;
  }
  [[nodiscard]] bool fired(std::string_view event_id) const;
  [[nodiscard]] double cooldown_remaining(std::string_view event_id,
                                          double simulation_day) const;

  std::string serialize() const;
  bool restore(std::string_view document, std::string *error = nullptr);

private:
  bool evaluate(const ScriptCondition &condition,
                const ScriptFiringContext &context);
  void run_effects(const ScriptedEventDefinition &definition,
                   const ScriptFiringContext &context);
  std::int64_t resolve_scope_id(const ScriptScope &scope,
                                const ScriptFiringContext &context) const;
  // Fired-set key: plain id for once_per_campaign, "id@<resolved scope>"
  // for once_per_scope (falls back to the plain id when nothing resolves).
  std::string once_key(const ScriptedEventDefinition &definition,
                       const ScriptFiringContext &context) const;
  bool fired_key(std::string_view key) const;
  std::uint64_t next_roll() noexcept;
  double roll_unit() noexcept; // [0,1) from the deterministic stream

  ScriptedContentAdapter *adapter_{};
  std::vector<ScriptedEventDefinition> definitions_;
  std::vector<ScheduledScriptedEvent> scheduled_;
  std::vector<std::string> fired_once_;
  std::vector<std::pair<std::string, double>> cooldown_until_;
  std::vector<std::pair<std::string, double>> poll_due_;
  std::uint64_t roll_state_{1};
  double now_{};
};

} // namespace stellar::engine
