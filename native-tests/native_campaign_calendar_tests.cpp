#include "native_campaign_calendar.hpp"
#include <stellar/core/strategic_clock.hpp>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace stellar::native_campaign;
void require(bool pass, const char* message) {
  if (!pass) throw std::runtime_error(message);
}
int main() {
  try {
    require(format_campaign_date(0) == "2050-03-21", "Wrong campaign epoch");
    require(format_campaign_date_short(0) == "21 Mar 2050", "Wrong HUD date");
    require(format_campaign_date(.999) == "2050-03-21", "Fraction advanced date early");
    require(format_campaign_date(1) == "2050-03-22", "Day rollover");
    require(format_campaign_date(10) == "2050-03-31", "March length");
    require(format_campaign_date(11) == "2050-04-01", "Month rollover");
    require(format_campaign_date(41) == "2050-05-01", "April length");
    require(format_campaign_date(286) == "2051-01-01", "Year rollover");
    require(format_campaign_date(710) == "2052-02-29", "Leap day");
    require(format_campaign_date(711) == "2052-03-01", "Leap month rollover");
    require(format_campaign_date(-1) == "2050-03-21", "Negative time");
    require(format_campaign_date(std::numeric_limits<double>::quiet_NaN()) == "2050-03-21", "NaN time");
    require(format_campaign_date(std::numeric_limits<double>::infinity()) == "9999-12-31", "Date overflow");
    require(format_campaign_duration(0,true) == "0 days", "Zero elapsed");
    require(format_campaign_duration(1,true) == "1 day", "Singular day");
    require(format_campaign_duration(29.999,true) == "29 days", "Month threshold early");
    require(format_campaign_duration(30,true) == "1 month", "Month threshold");
    require(format_campaign_duration(60,true) == "2 months", "Plural months");
    require(format_campaign_duration(359.999,true) == "11 months", "Year threshold early");
    require(format_campaign_duration(360,true) == "1 year", "Year threshold");
    require(format_campaign_duration(720,true) == "2 years", "Plural years");
    require(format_campaign_duration(.2) == "<1 day", "Short estimate");
    require(format_campaign_duration(45) == "1.5 months", "Fractional estimate");
    require(format_campaign_duration(std::numeric_limits<double>::infinity()) == "Unknown", "Unknown estimate");
    require(format_campaign_time(0)=="00:00"&&format_campaign_time(13.5/24)=="13:30"&&format_campaign_time(23./24)=="23:00"&&format_campaign_time(1)=="00:00","24-hour clock rollover");
    double midnight=0;for(int tick=0;tick<96;++tick)midnight+=1./96.;
    require(format_campaign_time(midnight)=="00:00"&&format_campaign_date(midnight)=="2050-03-22","Fractional-hour midnight disagrees with date");
    stellar::core::StrategicClock hourly;hourly.set_days_per_second(1./24.);
    (void)hourly.advance(1.);require(std::abs(hourly.simulation_days()-1./24.)<1e-12&&format_campaign_time(hourly.simulation_days())=="01:00"&&std::abs(hourly.effective_multiplier()-1)<1e-12,"1x must advance one hour per real second");
    hourly.set_speed(stellar::core::StrategicSpeed::Fast);(void)hourly.advance(1.);
    require(format_campaign_time(hourly.simulation_days())=="03:00","2x must advance two hours per second");
    hourly.set_speed(stellar::core::StrategicSpeed::Paused);(void)hourly.advance(5);require(format_campaign_time(hourly.simulation_days())=="03:00","Pause changed military time");
    stellar::core::StrategicClock clock;
    clock.restore(29.75);
    clock.set_speed(stellar::core::StrategicSpeed::Paused);
    (void)clock.advance(10);
    require(format_campaign_date(clock.simulation_days()) == "2050-04-19", "Pause advanced date");
    clock.set_speed(stellar::core::StrategicSpeed::Fast);
    (void)clock.advance(1., .25);
    const auto saved = clock.simulation_days();
    stellar::core::StrategicClock loaded;
    loaded.restore(saved);
    require(saved > 29.75 && format_campaign_date(loaded.simulation_days()) == format_campaign_date(saved)
        && format_campaign_duration(loaded.simulation_days(),true) == format_campaign_duration(saved,true),
        "Clock restore/speed changed calendar basis");
    std::cout << "Campaign calendar and duration checks passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
