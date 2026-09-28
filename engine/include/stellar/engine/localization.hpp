#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Engine-level localization table. Keys are stable content ids such as
// "UI_RESEARCH_COMPLETE"; values may contain {0},{1},... or {name}
// placeholders substituted by format(). Plural selection uses the
// "<key>_one" / "<key>_other" convention when a count argument is supplied.
//
// Locale files are JSON: {"locale":"en","fallback":"en",
//   "strings":{"KEY":"text","KEY_one":"one item","KEY_other":"{0} items"}}
// The table is deliberately engine-owned: gameplay/UI code asks for keys,
// never embeds display strings.
class LocalizationTable {
public:
  struct Entry {
    std::string key;
    std::string text;
  };

  LocalizationTable();
  // `fallback_locale` is consulted when `locale` lacks a key.
  explicit LocalizationTable(std::string locale, std::string fallback_locale);
  LocalizationTable(LocalizationTable &&) noexcept;
  LocalizationTable &operator=(LocalizationTable &&) noexcept;
  LocalizationTable(const LocalizationTable &) = delete;
  LocalizationTable &operator=(const LocalizationTable &) = delete;

  const std::string &locale() const noexcept;
  const std::string &fallback_locale() const noexcept;

  // Parses and merges a JSON table. Returns false (and leaves the table
  // unchanged) when the document is malformed or its locale mismatches.
  bool load_json(std::string_view document, std::string *error = nullptr);
  bool load_file(const std::string &path, std::string *error = nullptr);
  void clear();

  bool contains(std::string_view key) const;
  // Returns the localized text, falling back through the chain and finally
  // to the key itself so missing strings are visible rather than blank.
  std::string_view translate(std::string_view key) const;
  // Substitutes {0},{1},... positional and {name} named placeholders.
  std::string format(std::string_view key,
                     std::span<const std::string> args) const;
  std::string format(std::string_view key,
                     std::span<const std::pair<std::string_view, std::string>>
                         named_args) const;
  // Chooses "<key>_one" when count == 1, else "<key>_other", substituting
  // {0} with the count.
  std::string plural(std::string_view key, std::int64_t count) const;

  std::size_t size() const noexcept;

private:
  std::string locale_;
  std::string fallback_locale_;
  std::unordered_map<std::string, std::string> strings_;
  // Strings loaded under the fallback locale, consulted second.
  std::unordered_map<std::string, std::string> fallback_strings_;
  mutable std::mutex mutex_;

  friend class LocalizationService;
};

// Owns all loaded locale tables and answers lookups with a fallback chain.
// Thread-safe; intended to be populated at startup and reloaded on demand.
class LocalizationService {
public:
  // Registers `table`. If the service already holds a table for that locale
  // it is replaced (runtime language reload).
  void add_table(LocalizationTable table);
  bool load_file(const std::string &path, std::string *error = nullptr);
  void clear();

  void set_locale(std::string locale);
  const std::string &locale() const noexcept;
  void set_fallback_locale(std::string locale);
  const std::string &fallback_locale() const noexcept;

  bool contains(std::string_view key) const;
  std::string translate(std::string_view key) const;
  std::string format(std::string_view key,
                     std::span<const std::string> args) const;
  std::string plural(std::string_view key, std::int64_t count) const;

  std::vector<std::string> loaded_locales() const;

private:
  std::unordered_map<std::string, LocalizationTable> tables_;
  std::string locale_{"en"};
  std::string fallback_locale_{"en"};
  mutable std::mutex mutex_;
};

} // namespace stellar::engine
