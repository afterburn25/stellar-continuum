#pragma once
#include <stellar/engine/simulation_calendar.hpp>

namespace stellar::core {
// Campaign-specific policy lives in Core, not the reusable Engine utility.
inline constexpr auto campaign_epoch = std::chrono::sys_days{
    std::chrono::year{2050} / std::chrono::March / 21};
inline auto campaign_date(double days) {
  return stellar::engine::calendar_date(campaign_epoch, days);
}
inline std::string format_campaign_date(double days) {
  return stellar::engine::format_calendar_date(campaign_epoch, days);
}
inline std::string format_campaign_time(double days) { return stellar::engine::format_clock_time(days); }
inline std::string format_campaign_date_short(double days) {
  return stellar::engine::format_calendar_date_short(campaign_epoch, days);
}
inline std::string format_campaign_duration(double days, bool elapsed = false) {
  return stellar::engine::format_duration(days, elapsed);
}
} // namespace stellar::core
