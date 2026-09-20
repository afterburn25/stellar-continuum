#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace stellar::engine {

// Pure simulation-time presentation. The caller supplies its campaign epoch;
// this utility neither reads wall time nor owns/advances a simulation clock.
[[nodiscard]] inline std::chrono::year_month_day calendar_date(std::chrono::sys_days epoch, double days) {
  constexpr auto last_date = std::chrono::sys_days{
      std::chrono::year{9999} / std::chrono::December / 31};
  constexpr auto first_date = std::chrono::sys_days{
      std::chrono::year{1} / std::chrono::January / 1};
  epoch = std::clamp(epoch, first_date, last_date);
  const auto maximum_days = (last_date - epoch).count();
  const auto whole_days = static_cast<long long>(std::floor(std::clamp(
      std::isnan(days) ? 0. : days+1e-10, 0., static_cast<double>(maximum_days))));
  return std::chrono::year_month_day{epoch + std::chrono::days{whole_days}};
}

[[nodiscard]] inline std::string format_calendar_date(std::chrono::sys_days epoch, double days) {
  const auto date = calendar_date(epoch, days);
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setfill('0') << std::setw(4) << int(date.year()) << '-'
      << std::setw(2) << unsigned(date.month()) << '-'
      << std::setw(2) << unsigned(date.day());
  return out.str();
}

// 24-hour simulation time, independent of the computer timezone.
[[nodiscard]] inline std::string format_clock_time(double days) {
  if(!std::isfinite(days) || days<0) days=0;
  days+=1e-10; // Match date rounding after repeated fractional-hour ticks.
  const auto minute=static_cast<int>(std::floor((days-std::floor(days))*1440.));
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setfill('0') << std::setw(2) << (minute/60)%24 << ':' << std::setw(2) << minute%60;
  return out.str();
}

[[nodiscard]] inline std::string format_calendar_date_short(std::chrono::sys_days epoch, double days) {
  constexpr std::array months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const auto date = calendar_date(epoch, days);
  return std::to_string(unsigned(date.day())) + " " +
      months[unsigned(date.month()) - 1] + " " + std::to_string(int(date.year()));
}

// Compact durations use 30-day months and 12-month years. These presentation
// units do not change Gregorian dates or the underlying simulation rates.
[[nodiscard]] inline std::string format_duration(double days,
                                                          bool elapsed = false) {
  if (!std::isfinite(days)) return "Unknown";
  days = std::max(0., days);
  const double divisor = days >= 360. ? 360. : days >= 30. ? 30. : 1.;
  const char* unit = days >= 360. ? "year" : days >= 30. ? "month" : "day";
  if (!elapsed && days > 0. && days < 1.) return "<1 day";
  // Do not round into the next unit before its threshold. Elapsed labels
  // count completed units; estimates retain a tenth of a unit.
  const double value = elapsed ? std::floor(days / divisor)
                               : std::floor(days / divisor * 10.) / 10.;
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1)
      << value << ' ' << unit;
  if (value != 1.) out << 's';
  return out.str();
}

}  // namespace stellar::engine
