#pragma once
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/engine/localization.hpp>
#include <span>
#include <string>
#include <string_view>
namespace stellar::native_campaign {
using stellar::core::campaign_epoch;
using stellar::core::campaign_date;
using stellar::core::format_campaign_time;
using stellar::core::format_campaign_date;
using stellar::core::format_campaign_date_short;
using stellar::core::format_campaign_duration;
// The engine formatter emits stable English skeletons ("23.3 days",
// "<1 day", "Unknown"); recompose each through the locale table.
[[nodiscard]] inline std::string format_campaign_duration_localized(
    const stellar::engine::LocalizationTable *locale, double days,
    bool elapsed = false) {
  const std::string text = format_campaign_duration(days, elapsed);
  if (!locale || text.empty()) return text;
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return locale->contains(key) ? std::string(locale->translate(key))
                                 : std::string(fallback);
  };
  const auto trf = [&](std::string_view key, std::string_view arg,
                       std::string_view fallback) {
    if (locale->contains(key)) {
      const std::string values{arg};
      return locale->format(key, std::span<const std::string>(&values, 1));
    }
    std::string out{fallback};
    if (const auto at = out.find("{0}"); at != std::string::npos)
      out.replace(at, 3, arg);
    return out;
  };
  if (text == "Unknown") return tr("DURATION_UNKNOWN", "Unknown");
  if (text == "<1 day") return tr("DURATION_LT_DAY", "<1 day");
  const auto split = text.find(' ');
  if (split == std::string::npos) return text;
  const std::string_view view{text};
  const std::string number{view.substr(0, split)};
  const auto unit = view.substr(split + 1);
  if (unit == "day") return trf("DURATION_DAY", number, "{0} day");
  if (unit == "days") return trf("DURATION_DAYS", number, "{0} days");
  if (unit == "month") return trf("DURATION_MONTH", number, "{0} month");
  if (unit == "months") return trf("DURATION_MONTHS", number, "{0} months");
  if (unit == "year") return trf("DURATION_YEAR", number, "{0} year");
  if (unit == "years") return trf("DURATION_YEARS", number, "{0} years");
  return text;
}
} // namespace stellar::native_campaign
