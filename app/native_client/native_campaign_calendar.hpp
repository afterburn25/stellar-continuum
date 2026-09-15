#pragma once

// CampaignCalendar.FormatDate: epoch 2050-01-01 plus whole/fractional days.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

namespace stellar::native_campaign {

[[nodiscard]] inline std::string format_campaign_date(double simulation_days) {
  const double clamped = std::clamp(simulation_days, 0., 2932896.);
  const std::int64_t z = static_cast<std::int64_t>(std::floor(clamped)) + 719468 +
                         (2050 - 1970) * 365 + 20;  // 2050-01-01 as days-from-epoch
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t year = static_cast<std::int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t out_year = month <= 2 ? year + 1 : year;
  std::ostringstream out;
  out << out_year << '-';
  if (month < 10) out << '0';
  out << month << '-';
  if (day < 10) out << '0';
  out << day;
  return out.str();
}

}  // namespace stellar::native_campaign
