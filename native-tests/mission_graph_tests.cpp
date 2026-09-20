#include <stellar/engine/event_bus.hpp>
#include <stellar/engine/mission_graph.hpp>

#include <iostream>
#include <string>
#include <vector>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  std::string error;
  const auto definition = MissionDefinition::parse(R"({
    "id": "anomaly_whisper",
    "triggers": [
      {"event": "SystemEntered",
       "conditions": [{"field": "system_id", "equals": "42"}],
       "stage": "arrival"}
    ],
    "stages": {
      "arrival": {
        "title_key": "M_W_TITLE",
        "body_key": "M_W_BODY",
        "timer_days": 30,
        "timeout_stage": "expired",
        "choices": [
          {"id": "investigate", "next": "dig", "effects": ["spawn_excavation"]},
          {"id": "leave", "next": "", "effects": ["log_skipped"]}
        ]
      },
      "dig": {"title_key": "M_W_DIG"},
      "expired": {"title_key": "M_W_EXPIRED"}
    }
  })",
                                                   &error);
  check(definition.has_value(), error.c_str());
  check(definition->stages.size() == 3, "three stages parsed");
  check(definition->stages.at("arrival").choices.size() == 2,
        "choices parsed");

  // Broken definitions are rejected.
  check(!MissionDefinition::parse(
            R"({"id":"bad","triggers":[{"event":"E","stage":"nowhere"}],
             "stages":{}})",
            &error),
        "unknown trigger stage rejected");

  EventBus bus;
  std::vector<MissionEffectEvent> emitted;
  auto sub = bus.subscribe<MissionEffectEvent>(
      [&](const MissionEffectEvent &e) { emitted.push_back(e); });

  MissionRuntime runtime(&bus);
  check(runtime.add_definition(*definition, &error), error.c_str());

  // Non-matching events do nothing.
  runtime.handle_event("SystemEntered", R"({"system_id":7})");
  check(runtime.instances().empty(), "condition filters events");
  runtime.handle_event("OtherEvent", R"({"system_id":42})");
  check(runtime.instances().empty(), "event name filters");

  // A matching event starts an instance and announces the stage.
  runtime.handle_event("SystemEntered", R"({"system_id":42})");
  check(runtime.instances().size() == 1, "instance started");
  check(emitted.size() == 1 && emitted[0].stage_id == "arrival",
        "stage entry emitted");

  // Timer expiry transitions to the timeout stage.
  runtime.advance(31.0);
  check(runtime.instances()[0].stage_id == "expired",
        "timer fires timeout_stage");
  check(emitted.size() == 3, "timeout + entry events emitted");

  // Snapshot/restore round-trips instance state.
  MissionRuntime restored(nullptr);
  restored.add_definition(*definition, nullptr);
  check(restored.restore(runtime.serialize(), &error), error.c_str());
  check(restored.instances().size() == 1 &&
            restored.instances()[0].stage_id == "expired",
        "restore preserves stage");

  // Choice dispatch: a fresh instance picks "investigate".
  MissionRuntime choosing(&bus);
  choosing.add_definition(*definition, nullptr);
  choosing.handle_event("SystemEntered", R"({"system_id":42})");
  const auto id2 = choosing.instances()[0].id;
  check(!choosing.choose(id2, "bogus"), "unknown choice rejected");
  check(choosing.choose(id2, "investigate"), "choice applies");
  check(choosing.instances()[0].stage_id == "dig",
        "choice transitions stage");
  check(emitted.size() >= 2 &&
            emitted[emitted.size() - 2].effects.size() == 1 &&
            emitted[emitted.size() - 2].effects[0] == "spawn_excavation",
        "choice effects reach the bus");

  // Terminal choice (empty next) completes and removes the instance.
  MissionRuntime ending(nullptr);
  ending.add_definition(*definition, nullptr);
  ending.handle_event("SystemEntered", R"({"system_id":42})");
  const auto id3 = ending.instances()[0].id;
  check(ending.choose(id3, "leave"), "terminal choice applies");
  check(ending.instances().empty(), "terminal choice completes instance");

  if (failures == 0)
    std::cout << "MissionGraph tests passed\n";
  return failures == 0 ? 0 : 1;
}
