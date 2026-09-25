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
  // Source-reference audit: every catalog-shaped literal the native client
  // passes to a localization resolver (tr/mt/resolve/format/translate/…),
  // returns from a *_key mapper, or lists in a *_keys table must exist in the
  // baseline catalog — a missing key silently renders the English fallback in
  // every shipped locale. Concatenation fragments ("PREFIX_" + value) are
  // dynamic key families and are intentionally skipped.
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
    const std::set<std::string> resolvers{
        "tr",        "trf",      "mt",       "mtf",    "mtfn",
        "resolve",   "resolved", "format",   "plural", "translate",
        "contains",  "tr_at",    "quality_name"};
    const auto key_shaped = [](const std::string &text) {
      // Taxonomy ids are ALL_CAPS_WITH_UNDERSCORES and at least two segments.
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
      std::vector<std::string> callees;   // paren stack: callee identifier
      std::vector<bool> key_braces;       // brace stack: *_key/_keys initializer
      std::string ident, prev_word;
      char prev_char = 0;
      int line = 1;
      std::size_t i = 0;
      const auto flush = [&] {
        if (!ident.empty()) {
          prev_word = ident;
          ident.clear();
        }
      };
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
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
          ident += c;
          prev_char = c;
          ++i;
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
        flush();
        if (c == '(') {
          callees.push_back(prev_word);
          prev_word.clear();
          prev_char = '(';
          ++i;
          continue;
        }
        if (c == ')') {
          if (!callees.empty()) callees.pop_back();
          prev_char = ')';
          ++i;
          continue;
        }
        if (c == '{') {
          const bool keys =
              (!key_braces.empty() && key_braces.back()) ||
              prev_word.ends_with("_key") || prev_word.ends_with("_keys");
          key_braces.push_back(keys);
          prev_char = '{';
          ++i;
          continue;
        }
        if (c == '}') {
          if (!key_braces.empty()) key_braces.pop_back();
          prev_char = '}';
          ++i;
          continue;
        }
        if (c == '"') {
          // Raw string literal: R"delim(...)delim"
          if (!prev_word.empty() && prev_word.ends_with('R')) {
            const auto paren = src.find('(', i + 1);
            const std::size_t span =
                paren == std::string::npos ? 0 : paren - i - 1;
            if (paren != std::string::npos && span <= 16) {
              const std::string close =
                  ")" + src.substr(i + 1, span) + "\"";
              const auto end = src.find(close, paren);
              const std::size_t stop =
                  end == std::string::npos ? src.size() : end;
              for (std::size_t j = i; j < stop; ++j)
                if (src[j] == '\n') ++line;
              i = end == std::string::npos ? src.size() : end + close.size();
              prev_word.clear();
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
              prev_char != '+' && prev_char != '"') {
            const bool referenced =
                (!callees.empty() && resolvers.contains(callees.back())) ||
                prev_word == "return" ||
                (!key_braces.empty() && key_braces.back());
            if (referenced) {
              ++references;
              check(catalog.contains(text),
                    (file.filename().string() + ":" +
                     std::to_string(literal_line) +
                     " references missing localization key " + text)
                        .c_str());
            }
          }
          prev_word.clear();
          prev_char = '"';
          i = j;
          continue;
        }
        prev_char = c;
        ++i;
      }
    }
    check(references > 500, "audit scanned source key references");
  }
#endif

  if (failures == 0)
    std::cout << "Localization tests passed\n";
  return failures == 0 ? 0 : 1;
}
