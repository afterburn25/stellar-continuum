#include <stellar/core/strategic_clock.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stellar::core {
namespace {
bool finite(double value) { return std::isfinite(value); }
// Mirrors System.Math double overloads, including NaN and signed-zero selection.
double dotnet_min(double first, double second) {
  if (first != second) {
    if (!std::isnan(first)) return first < second ? first : second;
    return first;
  }
  return std::signbit(first) ? first : second;
}
double dotnet_max(double first, double second) {
  if (first != second) {
    if (!std::isnan(first)) return second < first ? first : second;
    return first;
  }
  return std::signbit(second) ? first : second;
}
void validate_positive_finite(double value, const char *message) {
  if (!finite(value) || value <= 0.) throw std::invalid_argument(message);
}
}

double StrategicClock::requested_multiplier() const {
  return multipliers_.at(static_cast<int>(speed_));
}
void StrategicClock::set_speed(StrategicSpeed speed) noexcept {
  if (speed != StrategicSpeed::Paused) last_running_speed_ = speed;
  speed_ = speed;
}
void StrategicClock::resume() noexcept { speed_ = last_running_speed_; }
void StrategicClock::select_resume_speed(StrategicSpeed speed) {
  if (speed == StrategicSpeed::Paused)
    throw std::invalid_argument("Resume speed cannot be paused.");
  last_running_speed_ = speed;
}
void StrategicClock::restore(double simulation_days) noexcept {
  simulation_days_ = dotnet_max(0., simulation_days);
  backlog_days_ = 0.;
}
double StrategicClock::advance(double real_delta_seconds, double maximum_step_days) {
  const auto requested = requested_multiplier();
  if (requested <= 0.) { effective_multiplier_ = 0.; return 0.; }
  const auto requested_days = real_delta_seconds * requested;
  auto accepted = dotnet_min(requested_days, maximum_step_days);
  backlog_days_ = dotnet_max(0., backlog_days_ + requested_days - accepted);
  const auto drain = dotnet_min(backlog_days_, maximum_step_days * .20);
  accepted += drain;
  backlog_days_ -= drain;
  simulation_days_ += accepted;
  effective_multiplier_ = real_delta_seconds > 0. ? accepted / real_delta_seconds : requested;
  return accepted;
}
double StrategicClock::advance_bounded_frame(double real_delta_seconds, double maximum_days,
                                             double maximum_backlog_days) {
  if (!finite(real_delta_seconds) || real_delta_seconds < 0. || !finite(maximum_days) ||
      maximum_days <= 0. || !finite(maximum_backlog_days) || maximum_backlog_days < 0.)
    throw std::invalid_argument("Frame time and budgets must be finite and nonnegative; step budget must be positive.");
  const auto requested = requested_multiplier();
  if (requested <= 0. || real_delta_seconds == 0.) { effective_multiplier_ = 0.; return 0.; }
  const auto requested_days = dotnet_min(real_delta_seconds,
      (maximum_days + maximum_backlog_days) / requested) * requested;
  const auto available = requested_days + dotnet_min(backlog_days_, maximum_backlog_days);
  const auto accepted = dotnet_min(available, maximum_days);
  backlog_days_ = dotnet_min(maximum_backlog_days, dotnet_max(0., available - accepted));
  simulation_days_ += accepted;
  effective_multiplier_ = accepted / real_delta_seconds;
  return accepted;
}

std::vector<double> advance_developer_frame(StrategicClock &clock,
                                            double real_delta_seconds) {
  constexpr double maximum_step_days = .25;
  constexpr int maximum_steps_per_frame = 4;
  auto accepted = clock.advance_bounded_frame(real_delta_seconds);
  if (accepted <= 0.) return {};
  // The source's cast of a poisoned NaN acceptance cannot produce usable steps.
  // Reject before C++'s floating-to-integer conversion would be undefined.
  if (!finite(accepted))
    throw std::overflow_error("Developer frame accepted a non-finite simulation duration.");
  const auto count = std::min(maximum_steps_per_frame,
                              static_cast<int>(std::ceil(accepted / maximum_step_days)));
  std::vector<double> steps;
  steps.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    const auto step = dotnet_min(maximum_step_days, accepted);
    steps.push_back(step);
    accepted -= step;
  }
  return steps;
}

CampaignAutosavePolicy::CampaignAutosavePolicy(double interval_days, double failure_retry_days)
    : interval_days_(interval_days), failure_retry_days_(failure_retry_days) {
  validate_positive_finite(interval_days_, "Autosave interval must be finite and positive.");
  validate_positive_finite(failure_retry_days_, "Autosave retry delay must be finite and positive.");
}
CampaignAutosaveScheduler::CampaignAutosaveScheduler(CampaignAutosavePolicy policy)
    : policy_(policy), next_due_day_(std::numeric_limits<double>::infinity()) {}
CampaignAutosaveScheduler CampaignAutosaveScheduler::developer_demo() {
  return CampaignAutosaveScheduler(CampaignAutosavePolicy(720., 48.));
}
void CampaignAutosaveScheduler::validate_days(double days) {
  if (!finite(days) || days < 0.)
    throw std::invalid_argument("Simulation days must be finite and non-negative.");
}
double CampaignAutosaveScheduler::add_bounded(double current, double delay) noexcept {
  const auto next = current + delay;
  return finite(next) ? next : std::numeric_limits<double>::max();
}
void CampaignAutosaveScheduler::reset(double current_simulation_days) {
  validate_days(current_simulation_days); next_due_day_ = add_bounded(current_simulation_days, policy_.interval_days());
}
bool CampaignAutosaveScheduler::is_due(double current_simulation_days) const {
  validate_days(current_simulation_days); return current_simulation_days >= next_due_day_;
}
void CampaignAutosaveScheduler::mark_success(double current_simulation_days) {
  validate_days(current_simulation_days); next_due_day_ = add_bounded(current_simulation_days, policy_.interval_days());
}
void CampaignAutosaveScheduler::mark_failure(double current_simulation_days) {
  validate_days(current_simulation_days); next_due_day_ = add_bounded(current_simulation_days, policy_.failure_retry_days());
}
}  // namespace stellar::core
