#pragma once

#include <stellar/core/sovereign_currency.hpp>
#include <stellar/engine/localization.hpp>

#include <format>
#include <string>
#include <string_view>

namespace stellar::native_currency_format {

// Core `SovereignCurrencyDefinition::format_rate` hardcodes the English
// "/day" unit suffix. The value itself is authoritative and locale-neutral,
// so re-skin the suffix at the client boundary rather than touching core.
[[nodiscard]] inline std::string format_rate_localized(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::SovereignCurrencyDefinition &currency,
    const double value) {
  std::string text = currency.format_rate(value);
  constexpr std::string_view suffix = "/day";
  if (locale && locale->contains("ECONOMY_UNIT_PER_DAY") &&
      text.size() >= suffix.size() &&
      text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
    text.replace(text.size() - suffix.size(), suffix.size(),
                 locale->translate("ECONOMY_UNIT_PER_DAY"));
  }
  return text;
}

// Material rates share the same problem: "{:+.2f} / DAY" is composed in the
// client, but the unit word itself should still come from the catalog.
[[nodiscard]] inline std::string format_material_rate_localized(
    const stellar::engine::LocalizationTable *locale, const double value) {
  std::string unit = " / DAY";
  if (locale && locale->contains("ECONOMY_UNIT_DAY"))
    unit = std::string(locale->translate("ECONOMY_UNIT_DAY"));
  return std::format("{:+.2f}{}", value, unit);
}

}  // namespace stellar::native_currency_format
