#pragma once

#include <array>
#include <vector>

namespace stellar::core {

enum class StrategicSpeed { Paused = 0, Normal = 1, Fast = 2, VeryFast = 3,
                            Maximum = 4, Demo = 5 };

class StrategicClock {
 public:
  StrategicSpeed speed() const noexcept { return speed_; }
  StrategicSpeed resume_speed() const noexcept { return last_running_speed_; }
  double simulation_days() const noexcept { return simulation_days_; }
  double effective_multiplier() const noexcept { return effective_multiplier_; }
  double requested_multiplier() const;
  double backlog_days() const noexcept { return backlog_days_; }

  void set_speed(StrategicSpeed speed) noexcept;
  void resume() noexcept;
  void select_resume_speed(StrategicSpeed speed);
  void restore(double simulation_days) noexcept;
  double advance(double real_delta_seconds, double maximum_step_days = .25);
  double advance_bounded_frame(double real_delta_seconds, double maximum_days = 1.,
                               double maximum_backlog_days = 2.);

 private:
  static constexpr std::array<double, 6> multipliers_{0., 1., 2., 3., 8., 24.};
  StrategicSpeed speed_ = StrategicSpeed::Normal;
  StrategicSpeed last_running_speed_ = StrategicSpeed::Normal;
  double simulation_days_ = 0.;
  double effective_multiplier_ = 1.;
  double backlog_days_ = 0.;
};

class CampaignAutosavePolicy {
 public:
  CampaignAutosavePolicy(double interval_days = 30., double failure_retry_days = 1.);
  double interval_days() const noexcept { return interval_days_; }
  double failure_retry_days() const noexcept { return failure_retry_days_; }

 private:
  double interval_days_;
  double failure_retry_days_;
};

// Source-equivalent PlayableDemoScenario.AdvanceFrame policy: one bounded clock
// acceptance split into no more than four .25-day integrated substeps.
std::vector<double> advance_developer_frame(StrategicClock &clock,
                                            double real_delta_seconds);

class CampaignAutosaveScheduler {
 public:
  explicit CampaignAutosaveScheduler(CampaignAutosavePolicy policy = {});
  static CampaignAutosaveScheduler developer_demo();
  double next_due_day() const noexcept { return next_due_day_; }
  void reset(double current_simulation_days);
  bool is_due(double current_simulation_days) const;
  void mark_success(double current_simulation_days);
  void mark_failure(double current_simulation_days);

 private:
  static void validate_days(double days);
  static double add_bounded(double current, double delay) noexcept;
  CampaignAutosavePolicy policy_;
  double next_due_day_;
};

}  // namespace stellar::core
