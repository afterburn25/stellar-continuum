#include <stellar/engine/localization.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

#if defined(STELLAR_SOURCE_DIR) && defined(STELLAR_LOCALE_DIR)
  // Source-reference audit: every ALL_CAPS_WITH_UNDERSCORES literal in the
  // native client is a localization key reference (resolver argument, *_key
  // field value, *_keys table entry, or *_key mapper return). Each one must
  // exist in the baseline catalog — a missing key silently renders the
  // English fallback in every shipped locale. Concatenation fragments
  // ("PREFIX_" + value) are dynamic key families; the handful of intentional
  // non-key identifiers (input contexts, env vars, dataset tokens) are
  // allowlisted explicitly.
  {
    const std::filesystem::path locale_dir{STELLAR_LOCALE_DIR};
    const std::filesystem::path src_root{STELLAR_SOURCE_DIR};
    const auto slurp = [](const std::filesystem::path &path) {
      std::ifstream stream(path, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(stream),
                         std::istreambuf_iterator<char>());
    };
    const auto en_doc = nlohmann::json::parse(slurp(locale_dir / "en.json"));
    const auto &catalog = en_doc.at("strings");
    const std::set<std::string> allowlist{
        "GALAXY_PAD",                 // InputMapper axis context name
        "STELLAR_CONTINUUM_DEVTOOLS", // environment variable
        "MAJOR_FLARE",                // eruption dataset classification token
        "SMALL_PROMINENCE",           // eruption dataset classification token
        "SETTINGS_ACTION_",           // dynamic prefix for controls-rebind keys
    };
    const auto key_shaped = [](const std::string &text) {
      if (text.size() < 5 || text.find('_') == std::string::npos)
        return false;
      return std::ranges::all_of(text, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      });
    };
    std::vector<std::filesystem::path> sources;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             src_root / "app" / "native_client")) {
      if (!entry.is_regular_file()) continue;
      const auto ext = entry.path().extension().string();
      if (ext == ".cpp" || ext == ".hpp") sources.push_back(entry.path());
    }
    std::sort(sources.begin(), sources.end());
    std::size_t references = 0;
    for (const auto &file : sources) {
      const std::string src = slurp(file);
      char prev_char = 0;
      int line = 1;
      std::size_t i = 0;
      while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') {
          ++line;
          ++i;
          continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
          i = src.find('\n', i);
          if (i == std::string::npos) break;
          continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
          const auto end = src.find("*/", i + 2);
          const std::size_t stop =
              end == std::string::npos ? src.size() : end;
          for (std::size_t j = i; j < stop; ++j)
            if (src[j] == '\n') ++line;
          i = end == std::string::npos ? src.size() : end + 2;
          continue;
        }
        if (c == '\'') {
          // Digit separator (1'000) is not a character literal.
          const bool separator =
              i > 0 && i + 1 < src.size() && src[i - 1] >= '0' &&
              src[i - 1] <= '9' && src[i + 1] >= '0' && src[i + 1] <= '9';
          if (!separator) {
            ++i;
            while (i < src.size() && src[i] != '\'')
              i += src[i] == '\\' ? 2 : 1;
            if (i < src.size()) ++i;
            continue;
          }
        }
        if (c == '"') {
          // Raw string literal: R"delim(...)delim" — an identifier ending in
          // R immediately before the quote (only legal in raw prefixes).
          if (prev_char == 'R') {
            const auto paren = src.find('(', i + 1);
            if (paren != std::string::npos && paren - i - 1 <= 16) {
              const std::string close =
                  ")" + src.substr(i + 1, paren - i - 1) + "\"";
              const auto end = src.find(close, paren);
              const std::size_t stop =
                  end == std::string::npos ? src.size() : end;
              for (std::size_t j = i; j < stop; ++j)
                if (src[j] == '\n') ++line;
              i = end == std::string::npos ? src.size() : end + close.size();
              prev_char = '"';
              continue;
            }
          }
          const int literal_line = line;
          std::string text;
          std::size_t j = i + 1;
          while (j < src.size() && src[j] != '"') {
            if (src[j] == '\\') ++j;
            if (j < src.size()) text += src[j++];
          }
          ++j; // consume closing quote
          std::size_t k = j;
          while (k < src.size() &&
                 (src[k] == ' ' || src[k] == '\t' || src[k] == '\n'))
            ++k;
          const char next = k < src.size() ? src[k] : 0;
          if (key_shaped(text) && next != '+' && next != '"' &&
              prev_char != '+' && prev_char != '"' &&
              !allowlist.contains(text)) {
            ++references;
            check(catalog.contains(text),
                  (file.filename().string() + ":" +
                   std::to_string(literal_line) +
                   " references missing localization key " + text)
                      .c_str());
          }
          prev_char = '"';
          i = j;
          continue;
        }
        // Whitespace (besides newlines, handled above) does not update
        // prev_char, so `x + "KEY"` still sees the preceding '+'.
        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
          ++i;
          continue;
        }
        prev_char = c;
        ++i;
      }
    }
    check(references > 1500, "audit scanned source key references");

    // Derived-key audit: the controls-rebind view builds
    // SETTINGS_ACTION_<UPPER(name)> from every InputMapper action declared
    // in the embedded context JSON — an un-cataloged action renders raw
    // English in every non-English locale.
    for (const auto &file : sources) {
      const std::string src = slurp(file);
      std::size_t at = 0;
      while ((at = src.find("R\"json(", at)) != std::string::npos) {
        const std::size_t close = src.find(")json\"", at + 7);
        const std::string blob = src.substr(
            at + 7, close == std::string::npos ? close : close - at - 7);
        at = close == std::string::npos ? src.size() : close;
        const auto doc = nlohmann::json::parse(blob, nullptr, false);
        if (doc.is_discarded() || !doc.contains("contexts")) continue;
        for (const auto &context : doc["contexts"])
          for (const auto &action : context.value(
                   "actions", nlohmann::json::array())) {
            const auto name = action.value("name", std::string{});
            if (name.empty()) continue;
            std::string key{"SETTINGS_ACTION_"};
            for (const char ch : name)
              key += static_cast<char>(
                  std::toupper(static_cast<unsigned char>(ch)));
            check(catalog.contains(key),
                  (file.filename().string() + " declares input action \"" +
                   name + "\" with no catalog key " + key)
                      .c_str());
          }
      }

      // The feed/chronicle resolve NOTIFY_CATEGORY_<UPPER(label)> for every
      // label category_label() returns — a new prefix added without a
      // catalog entry silently shows English in non-English locales.
      if (const std::size_t fn =
              src.find("category_label(std::string_view");
          fn != std::string::npos) {
        const std::size_t body_end = src.find("\n}", fn);
        const std::string body = src.substr(fn, body_end - fn);
        std::size_t cursor = 0;
        while ((cursor = body.find("return \"", cursor)) !=
               std::string::npos) {
          const std::size_t q = body.find('"', cursor + 8);
          const std::string label = body.substr(cursor + 8, q - cursor - 8);
          cursor = q;
          std::string key{"NOTIFY_CATEGORY_"};
          for (const char ch : label)
            key += static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch)));
          check(catalog.contains(key),
                (file.filename().string() + " maps a chronicle category to "
                 "\"" + label + "\" with no catalog key " + key)
                    .c_str());
        }
      }
    }
  }
#endif

  if (failures == 0)
    std::cout << "Localization tests passed\n";
  return failures == 0 ? 0 : 1;
}
