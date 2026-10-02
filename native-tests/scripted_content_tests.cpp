#include <stellar/engine/scripted_content.hpp>

#include <iostream>
#include <set>
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

// Records every adapter call; the check table maps check-id -> truth.
struct TestAdapter final : ScriptedContentAdapter {
  std::set<std::string> known_checks{"rich", "at_home", "mark"};
  std::set<std::string> known_effects{"grant", "notify"};
  std::vector<std::string> applied;
  std::vector<std::string> fired_log;
  int polls{};

  std::string validate_condition(const ScriptCondition &leaf,
                                 std::string_view, std::string_view) override {
    return known_checks.count(leaf.check)
               ? std::string{}
               : "unknown check '" + leaf.check + "'";
  }
  std::string validate_effect(const ScriptEffect &effect, std::string_view,
                              std::string_view) override {
    return known_effects.count(effect.action)
               ? std::string{}
               : "unknown effect '" + effect.action + "'";
  }
  std::vector<ScriptFiringContext>
  enumerate_contexts(const ScriptedEventDefinition &) override {
    ++polls;
    ScriptFiringContext context;
    context.civilization = 7;
    return {context};
  }
  bool evaluate_condition(const ScriptCondition &leaf,
                          const ScriptFiringContext &) override {
    return leaf.check == "rich" || leaf.check == "mark";
  }
  void apply_effect(const ScriptEffect &effect,
                    const ScriptFiringContext &) override {
    applied.push_back(effect.action);
  }
  void event_fired(const ScriptedEventDefinition &definition,
                   const ScriptFiringContext &) override {
    fired_log.push_back(definition.id);
  }
};

std::string doc(std::string_view body) {
  return "{\"scripted_events\":[" + std::string(body) + "]}";
}
} // namespace

int main() {
  TestAdapter adapter;
  ScriptedContentRuntime runtime(42);
  runtime.attach_adapter(&adapter);

  // --- Validation -------------------------------------------------------
  {
    std::vector<ScriptLoadError> errors;
    check(runtime.load_document("not json", "bad.json", &errors) == false,
          "malformed document rejected");
    check(!errors.empty() && errors[0].file == "bad.json",
          "load error carries file provenance");
  }
  {
    std::vector<ScriptLoadError> errors;
    const auto rejected = runtime.load_document(
        doc("{\"id\":\"a\"},{\"id\":\"a\"}"), "dups.json", &errors);
    check(!rejected && !errors.empty(), "duplicate ids rejected");
  }
  {
    std::vector<ScriptLoadError> errors;
    const auto rejected = runtime.load_document(
        doc("{\"id\":\"x\",\"on\":\"Evt\",\"trigger\":{\"check\":\"nope\"}}"),
        "checks.json", &errors);
    check(!rejected && !errors.empty() &&
              errors[0].object_id == "x" &&
              errors[0].message.find("unknown check") != std::string::npos,
          "unknown check rejected with object id");
  }
  {
    std::vector<ScriptLoadError> errors;
    const auto rejected = runtime.load_document(
        doc("{\"id\":\"x\",\"effects\":[{\"do\":\"bogus\"}]}"),
        "effects.json", &errors);
    check(!rejected, "unknown effect rejected");
  }
  {
    std::vector<ScriptLoadError> errors;
    const auto rejected = runtime.load_document(
        doc("{\"id\":\"a\",\"follow_ups\":[{\"id\":\"b\"}]},"
            "{\"id\":\"b\",\"follow_ups\":[{\"id\":\"a\"}]}"),
        "loop.json", &errors);
    check(!rejected && !errors.empty() &&
              errors[0].message.find("recursive") != std::string::npos,
          "recursive follow-up loop rejected");
  }
  {
    std::vector<ScriptLoadError> errors;
    check(!runtime.load_document(
              doc("{\"id\":\"x\",\"follow_ups\":[{\"id\":\"ghost\"}]}"),
              "ref.json", &errors),
          "unknown follow-up reference rejected");
    check(!runtime.load_document(
              doc("{\"id\":\"x\",\"on\":\"E\",\"poll_days\":5}"),
              "modes.json", &errors),
          "mutually exclusive firing modes rejected");
    check(!runtime.load_document(
              doc("{\"id\":\"x\",\"poll_days\":5}"), "notrigger.json", &errors),
          "poll without trigger rejected");
    check(!runtime.load_document(
              doc("{\"id\":\"x\",\"scope\":{\"kind\":\"moon\"}}"),
              "scope.json", &errors),
          "unknown scope kind rejected");
    check(runtime.definition_count() == 0, "rejected loads mutate nothing");
  }

  // --- Poll-driven firing ------------------------------------------------
  {
    check(runtime.load_document(
              doc("{\"id\":\"rich_event\",\"poll_days\":10,"
                  "\"trigger\":{\"all\":[{\"check\":\"rich\"},"
                  "{\"not\":{\"check\":\"at_home\"}}]},"
                  "\"once_per_campaign\":true,"
                  "\"effects\":[{\"do\":\"grant\",\"args\":{\"what\":\"alloy\"}}]}"),
              "events.json"),
          "valid document loads");
    runtime.advance(0);
    check(adapter.fired_log.size() == 1 && adapter.fired_log[0] == "rich_event",
          "polled trigger fired on eligible context");
    check(adapter.applied.size() == 1 && adapter.applied[0] == "grant",
          "effect dispatched through adapter");
    runtime.advance(50);
    check(adapter.fired_log.size() == 1,
          "once_per_campaign suppresses second fire");
  }

  // --- Event-driven firing -----------------------------------------------
  {
    ScriptedContentRuntime events(7);
    events.attach_adapter(&adapter);
    check(events.load_document(
              doc("{\"id\":\"survey_a\",\"on\":\"SurveyDone\",\"weight\":5,"
                  "\"trigger\":{\"check\":\"mark\"}},"
                  "{\"id\":\"survey_b\",\"on\":\"SurveyDone\",\"weight\":5,"
                  "\"trigger\":{\"check\":\"at_home\"}}"),
              "survey.json"),
          "event document loads");
    ScriptFiringContext context;
    context.system = 12;
    events.handle_event("SurveyDone", context, 100.0);
    check(adapter.fired_log.size() == 2 &&
              adapter.fired_log.back() == "survey_a",
          "only trigger-true event eligible for the weighted roll");
    // Determinism: same seed + same sequence -> same winner.
    TestAdapter second_adapter;
    ScriptedContentRuntime replayed(7);
    replayed.attach_adapter(&second_adapter);
    replayed.load_document(
        doc("{\"id\":\"survey_a\",\"on\":\"SurveyDone\",\"weight\":5,"
            "\"trigger\":{\"check\":\"mark\"}},"
            "{\"id\":\"survey_b\",\"on\":\"SurveyDone\",\"weight\":5,"
            "\"trigger\":{\"check\":\"at_home\"}}"),
        "survey.json");
    replayed.handle_event("SurveyDone", context, 100.0);
    check(second_adapter.fired_log.size() == 1 &&
              second_adapter.fired_log[0] == "survey_a",
          "same seed produces the same winner");
  }

  // --- Delayed follow-ups -------------------------------------------------
  {
    TestAdapter chain_adapter;
    ScriptedContentRuntime chains(3);
    chains.attach_adapter(&chain_adapter);
    check(chains.load_document(
              doc("{\"id\":\"chain_start\",\"once_per_campaign\":true,"
                  "\"follow_ups\":[{\"id\":\"chain_next\",\"delay_days\":5}]},"
                  "{\"id\":\"chain_next\",\"effects\":[{\"do\":\"notify\"}]}"),
              "chain.json"),
          "chain document loads");
    ScriptFiringContext context;
    check(chains.fire("chain_start", context, /*force=*/true),
          "manual force-fire works");
    check(chains.scheduled().size() == 1,
          "follow-up scheduled");
    chains.advance(4.0);
    check(chain_adapter.fired_log.size() == 1 &&
              chain_adapter.fired_log[0] == "chain_start",
          "follow-up does not fire before its delay");
    chains.advance(6.0);
    check(chain_adapter.fired_log.size() == 2 &&
              chain_adapter.fired_log[1] == "chain_next",
          "delayed follow-up fired on schedule");
    check(chains.fired("chain_start"), "fired flag recorded");
  }

  // --- Persistence ---------------------------------------------------------
  {
    TestAdapter save_adapter;
    ScriptedContentRuntime saving(9);
    saving.attach_adapter(&save_adapter);
    saving.load_document(
        doc("{\"id\":\"once_e\",\"once_per_campaign\":true},"
            "{\"id\":\"chain_e\",\"follow_ups\":[{\"id\":\"once_e\",\"delay_days\":2}]}"),
        "save.json");
    ScriptFiringContext context;
    context.fleet = 3;
    saving.fire("once_e", context, /*force=*/true);
    saving.advance(10.0); // stamps now_ = 10
    saving.fire("chain_e", context, /*force=*/true); // follow-up due at 12
    const auto state = saving.serialize();

    ScriptedContentRuntime restored(1);
    check(restored.restore(state), "state restores");
    check(restored.fired("once_e"), "once flag survives restore");
    check(restored.scheduled().size() == 1 &&
              restored.scheduled()[0].fire_day == 12.0 &&
              restored.scheduled()[0].context.fleet == 3,
          "scheduled follow-up survives restore with context");
    std::string error;
    check(!restored.restore("{\"roll_state\":0}", &error),
          "corrupt state rejected");
  }

  if (failures == 0) std::cout << "Scripted content tests passed\n";
  return failures == 0 ? 0 : 1;
}
