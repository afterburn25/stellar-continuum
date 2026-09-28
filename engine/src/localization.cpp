#include <stellar/engine/localization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <utility>

namespace stellar::engine {
namespace {

std::string substitute(std::string_view text,
                       std::span<const std::string> positional,
                       std::span<const std::pair<std::string_view, std::string>>
                           named) {
  std::string out;
  out.reserve(text.size() + 16);
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      const auto close = text.find('}', i);
      if (close != std::string_view::npos) {
        const auto token = text.substr(i + 1, close - i - 1);
        bool replaced = false;
        if (!token.empty() &&
            token.find_first_not_of("0123456789") == std::string_view::npos) {
          const auto index = std::stoull(std::string(token));
          if (index < positional.size()) {
            out += positional[index];
            replaced = true;
          }
        } else {
          for (const auto &[name, value] : named)
            if (name == token) {
              out += value;
              replaced = true;
              break;
            }
        }
        if (replaced) {
          i = close + 1;
          continue;
        }
      }
    }
    out += text[i];
    ++i;
  }
  return out;
}

} // namespace

LocalizationTable::LocalizationTable() = default;
LocalizationTable::LocalizationTable(std::string locale,
                                     std::string fallback_locale)
    : locale_(std::move(locale)),
      fallback_locale_(std::move(fallback_locale)) {}
LocalizationTable::LocalizationTable(LocalizationTable &&other) noexcept {
  std::lock_guard lock(other.mutex_);
  locale_ = std::move(other.locale_);
  fallback_locale_ = std::move(other.fallback_locale_);
  strings_ = std::move(other.strings_);
  fallback_strings_ = std::move(other.fallback_strings_);
}
LocalizationTable &
LocalizationTable::operator=(LocalizationTable &&other) noexcept {
  if (this == &other)
    return *this;
  std::scoped_lock lock(mutex_, other.mutex_);
  locale_ = std::move(other.locale_);
  fallback_locale_ = std::move(other.fallback_locale_);
  strings_ = std::move(other.strings_);
  fallback_strings_ = std::move(other.fallback_strings_);
  return *this;
}

const std::string &LocalizationTable::locale() const noexcept {
  return locale_;
}
const std::string &LocalizationTable::fallback_locale() const noexcept {
  return fallback_locale_;
}

bool LocalizationTable::load_json(std::string_view document,
                                  std::string *error) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return false;
  }
  if (!parsed.is_object() || !parsed.value("strings", nlohmann::json{})
                                   .is_object()) {
    if (error != nullptr)
      *error = "locale document requires a 'strings' object";
    return false;
  }
  const auto doc_locale = parsed.value("locale", std::string{});
  if (!doc_locale.empty() && !locale_.empty() && doc_locale != locale_ &&
      doc_locale != fallback_locale_) {
    if (error != nullptr)
      *error = "locale '" + doc_locale + "' does not match table locale '" +
               locale_ + "'";
    return false;
  }
  std::unordered_map<std::string, std::string> loaded;
  for (const auto &[key, value] : parsed.at("strings").items()) {
    if (!value.is_string()) {
      if (error != nullptr)
        *error = "value for key '" + key + "' is not a string";
      return false;
    }
    loaded.emplace(key, value.get<std::string>());
  }
  std::lock_guard lock(mutex_);
  // Documents tagged with the fallback locale feed the fallback map.
  if (!doc_locale.empty() && doc_locale == fallback_locale_ &&
      doc_locale != locale_)
    fallback_strings_.merge(std::move(loaded));
  else
    strings_.merge(std::move(loaded));
  return true;
}

bool LocalizationTable::load_file(const std::string &path,
                                  std::string *error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    if (error != nullptr)
      *error = "cannot open locale file '" + path + "'";
    return false;
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return load_json(contents.str(), error);
}

void LocalizationTable::clear() {
  std::lock_guard lock(mutex_);
  strings_.clear();
  fallback_strings_.clear();
}

bool LocalizationTable::contains(std::string_view key) const {
  std::lock_guard lock(mutex_);
  return strings_.contains(std::string(key)) ||
         fallback_strings_.contains(std::string(key));
}

std::string_view LocalizationTable::translate(std::string_view key) const {
  std::lock_guard lock(mutex_);
  const auto found = strings_.find(std::string(key));
  if (found != strings_.end())
    return found->second;
  const auto fallback = fallback_strings_.find(std::string(key));
  if (fallback != fallback_strings_.end())
    return fallback->second;
  // Returning a view into `key` — keys outlive the call in practice because
  // callers use string literals or persistent content ids.
  return key;
}

std::string
LocalizationTable::format(std::string_view key,
                          std::span<const std::string> args) const {
  const auto text = translate(key);
  return substitute(text, args, {});
}

std::string LocalizationTable::format(
    std::string_view key,
    std::span<const std::pair<std::string_view, std::string>> named_args)
    const {
  const auto text = translate(key);
  return substitute(text, {}, named_args);
}

std::string LocalizationTable::plural(std::string_view key,
                                      std::int64_t count) const {
  const std::string suffixed =
      std::string(key) + (count == 1 ? "_one" : "_other");
  const auto text =
      contains(suffixed) ? translate(suffixed) : translate(key);
  const std::string args[]{std::to_string(count)};
  return substitute(text, args, {});
}

std::size_t LocalizationTable::size() const noexcept {
  std::lock_guard lock(mutex_);
  return strings_.size() + fallback_strings_.size();
}

void LocalizationService::add_table(LocalizationTable table) {
  std::lock_guard lock(mutex_);
  tables_[table.locale()] = std::move(table);
}

bool LocalizationService::load_file(const std::string &path,
                                    std::string *error) {
  LocalizationTable table;
  if (!table.load_file(path, error))
    return false;
  // The document's locale (or the fallback default) selects the slot.
  std::string doc_locale;
  {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    try {
      doc_locale = nlohmann::json::parse(contents.str())
                       .value("locale", fallback_locale());
    } catch (...) {
      doc_locale = fallback_locale();
    }
  }
  table = LocalizationTable(doc_locale, fallback_locale());
  if (!table.load_file(path, error))
    return false;
  add_table(std::move(table));
  return true;
}

void LocalizationService::clear() {
  std::lock_guard lock(mutex_);
  tables_.clear();
}

void LocalizationService::set_locale(std::string locale) {
  std::lock_guard lock(mutex_);
  locale_ = std::move(locale);
}
const std::string &LocalizationService::locale() const noexcept {
  return locale_;
}
void LocalizationService::set_fallback_locale(std::string locale) {
  std::lock_guard lock(mutex_);
  fallback_locale_ = std::move(locale);
}
const std::string &LocalizationService::fallback_locale() const noexcept {
  return fallback_locale_;
}

bool LocalizationService::contains(std::string_view key) const {
  std::lock_guard lock(mutex_);
  const auto table = tables_.find(locale_);
  return table != tables_.end() && table->second.contains(key);
}

std::string LocalizationService::translate(std::string_view key) const {
  std::lock_guard lock(mutex_);
  const auto table = tables_.find(locale_);
  if (table == tables_.end())
    return std::string(key);
  return std::string(table->second.translate(key));
}

std::string
LocalizationService::format(std::string_view key,
                            std::span<const std::string> args) const {
  std::lock_guard lock(mutex_);
  const auto table = tables_.find(locale_);
  if (table == tables_.end())
    return std::string(key);
  return table->second.format(key, args);
}

std::string LocalizationService::plural(std::string_view key,
                                        std::int64_t count) const {
  std::lock_guard lock(mutex_);
  const auto table = tables_.find(locale_);
  if (table == tables_.end())
    return std::string(key);
  return table->second.plural(key, count);
}

std::vector<std::string> LocalizationService::loaded_locales() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> result;
  result.reserve(tables_.size());
  for (const auto &[name, ignored] : tables_) {
    (void)ignored;
    result.push_back(name);
  }
  return result;
}

} // namespace stellar::engine
