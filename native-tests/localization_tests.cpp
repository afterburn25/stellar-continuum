#include <stellar/engine/localization.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

#ifdef STELLAR_LOCALE_DIR
  // Shipped catalogs: every data/locale/<id>.json is a selectable language.
  // Non-baseline tables must cover every baseline key with the same
  // placeholders — a deliberately partial locale has to be justified here,
  // not silently shipped with missing strings.
  {
    const std::filesystem::path dir{STELLAR_LOCALE_DIR};
    LocalizationTable baseline{"en", "en"};
    check(baseline.load_file((dir / "en.json").string(), &error),
          "shipped en.json loads");
    const auto read_text = [](const std::filesystem::path &path) {
      std::ifstream stream(path, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(stream),
                         std::istreambuf_iterator<char>());
    };
    const auto en_doc = nlohmann::json::parse(read_text(dir / "en.json"));
    const auto &en_strings = en_doc.at("strings");
    check(en_strings.size() > 1000, "baseline catalog is populated");
    const auto placeholders = [](const std::string &text) {
      std::vector<std::string> out;
      for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '{') {
          const auto close = text.find('}', i);
          if (close != std::string::npos) {
            out.push_back(text.substr(i, close - i + 1));
            i = close;
          }
        }
      std::sort(out.begin(), out.end());
      return out;
    };
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
        continue;
      const auto id = entry.path().stem().string();
      if (id == "en") continue;
      LocalizationTable table{id, "en"};
      check(table.load_file(entry.path().string(), &error),
            (std::string("shipped ") + id + ".json loads: " + error).c_str());
      check(table.load_file((dir / "en.json").string(), &error),
            (std::string("baseline loads into ") + id + " fallback").c_str());
      const auto doc = nlohmann::json::parse(read_text(entry.path()));
      check(doc.value("locale", std::string{}) == id,
            (id + " declares its own locale").c_str());
      const auto &strings = doc.at("strings");
      for (const auto &item : en_strings.items()) {
        const auto &key = item.key();
        check(strings.contains(key) && strings.at(key).is_string(),
              (id + " covers " + key).c_str());
        if (!strings.contains(key)) continue;
        check(placeholders(item.value().get<std::string>()) ==
                  placeholders(strings.at(key).get<std::string>()),
              (id + " placeholders match for " + key).c_str());
        check(std::string(table.translate(key)) != key,
              (id + " translates " + key).c_str());
      }
    }
    // Spot-check the shipped German surface.
    LocalizationTable german_ui{"de", "en"};
    check(german_ui.load_file((dir / "de.json").string(), &error),
          "de.json present");
    check(german_ui.translate("MENU_SAVE") == "SPEICHERN",
          "German menu string translated");
  }
#endif

  if (failures == 0)
    std::cout << "Localization tests passed\n";
  return failures == 0 ? 0 : 1;
}
