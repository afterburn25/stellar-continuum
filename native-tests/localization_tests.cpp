#include <stellar/engine/localization.hpp>

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
  LocalizationTable english("en", "en");
  std::string error;
  check(english.load_json(R"({
    "locale": "en",
    "strings": {
      "UI_RESEARCH_COMPLETE": "Research complete: {name}",
      "UI_SHIP_COUNT_one": "{0} ship",
      "UI_SHIP_COUNT_other": "{0} ships",
      "UI_ARGS": "{0} to {1}"
    }
  })",
                          &error),
        error.c_str());

  check(english.translate("UI_ARGS") == "{0} to {1}", "translate raw");
  check(english.translate("MISSING_KEY") == "MISSING_KEY",
        "missing keys surface the key itself");
  const std::string args[]{"alpha", "beta"};
  check(english.format("UI_ARGS", args) == "alpha to beta",
        "positional substitution");
  const std::pair<std::string_view, std::string> named[]{
      {"name", "Fusion"}};
  check(english.format("UI_RESEARCH_COMPLETE", named) ==
            "Research complete: Fusion",
        "named substitution");
  check(english.plural("UI_SHIP_COUNT", 1) == "1 ship", "plural singular");
  check(english.plural("UI_SHIP_COUNT", 3) == "3 ships", "plural other");

  // Malformed documents are rejected without touching the table.
  check(!english.load_json("{not json", &error), "rejects malformed json");
  check(english.size() == 4, "failed load leaves table intact");

  // Locale mismatch is rejected.
  LocalizationTable german("de", "en");
  check(!german.load_json(R"({"locale":"fr","strings":{"K":"v"}})", &error),
        "locale mismatch rejected");
  check(german.load_json(R"({"locale":"de","strings":{"K":"v"}})", &error),
        "matching locale accepted");

  LocalizationService service;
  LocalizationTable en("en", "en");
  en.load_json(R"({"locale":"en","strings":{"HELLO":"Hello","BYE":"Bye"}})");
  LocalizationTable de("de", "en");
  de.load_json(R"({"locale":"de","strings":{"HELLO":"Hallo"}})");
  // German table also loads English strings as its fallback source.
  de.load_json(R"({"locale":"en","strings":{"BYE":"Bye"}})");
  service.add_table(std::move(en));
  service.add_table(std::move(de));
  service.set_locale("de");
  check(service.translate("HELLO") == "Hallo", "service active locale");
  check(service.translate("BYE") == "Bye", "service falls back to en");
  check(service.translate("GONE") == "GONE", "service unknown key passthrough");
  service.set_locale("en");
  check(service.translate("HELLO") == "Hello", "runtime locale switch");
  check(service.loaded_locales().size() == 2, "loaded locales listed");

  if (failures == 0)
    std::cout << "Localization tests passed\n";
  return failures == 0 ? 0 : 1;
}
