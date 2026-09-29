#pragma once

// Internal sub-phase timers for ExplorationSimulation::advance and the
// mission-selection helpers it calls — enabled only with
// STELLAR_EXPL_PROF=1; prints a totals summary on process exit.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is intentional here
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace stellar::core::detail {

struct ExplProf {
  ExplProf() : enabled(std::getenv("STELLAR_EXPL_PROF") != nullptr) {}
  std::atomic<bool> enabled;
  std::atomic<long long> svc_ns{0}, survey_ns{0}, select_ns{0},
      recovery_ns{0}, transit_ns{0}, hops{0}, selects{0}, surveys{0};
  // select_mission internals (exploration_planning.cpp)
  std::atomic<long long> select_build_ns{0}, select_loop_ns{0},
      select_finish_ns{0};
  std::atomic<long long> work_entries{0}, work_rebuilds{0}, pops{0},
      assess_calls{0}, drains{0};
  // handle_inbound internals (exploration_advance.cpp)
  std::atomic<long long> inbound_prep_ns{0}, inbound_reveal_ns{0},
      inbound_tail_ns{0};
  std::atomic<long long> inbound_contacts_ns{0}, inbound_return_ns{0},
      return_hops{0};
  std::atomic<long long> band_candidates{0}, reveal_attempts{0};
  ~ExplProf() {
    if (!enabled.load()) return;
    std::fprintf(stderr,
                 "EXPL-PROF svc=%lldms survey=%lldms select=%lldms "
                 "recovery=%lldms transit=%lldms | hops=%lld selects=%lld "
                 "surveys=%lld\n",
                 svc_ns.load() / 1000000, survey_ns.load() / 1000000,
                 select_ns.load() / 1000000, recovery_ns.load() / 1000000,
                 transit_ns.load() / 1000000, hops.load(), selects.load(),
                 surveys.load());
    std::fprintf(stderr,
                 "EXPL-PROF select: build=%lldms loop=%lldms finish=%lldms "
                 "| work=%lld rebuilds=%lld pops=%lld assess=%lld "
                 "drains=%lld\n",
                 select_build_ns.load() / 1000000,
                 select_loop_ns.load() / 1000000,
                 select_finish_ns.load() / 1000000, work_entries.load(),
                 work_rebuilds.load(), pops.load(), assess_calls.load(),
                 drains.load());
    std::fprintf(stderr,
                 "EXPL-PROF inbound: prep=%lldms reveal=%lldms tail=%lldms "
                 "| band=%lld attempts=%lld\n",
                 inbound_prep_ns.load() / 1000000,
                 inbound_reveal_ns.load() / 1000000,
                 inbound_tail_ns.load() / 1000000, band_candidates.load(),
                 reveal_attempts.load());
    std::fprintf(stderr,
                 "EXPL-PROF inbound-tail: contacts=%lldms return=%lldms "
                 "| return_hops=%lld\n",
                 inbound_contacts_ns.load() / 1000000,
                 inbound_return_ns.load() / 1000000, return_hops.load());
  }
};

inline ExplProf &expl_prof() {
  static ExplProf prof;
  return prof;
}

struct ExplProfScope {
  std::atomic<long long> &slot;
  std::chrono::steady_clock::time_point t0;
  explicit ExplProfScope(std::atomic<long long> &slot) : slot(slot) {
    t0 = std::chrono::steady_clock::now();
  }
  ~ExplProfScope() {
    slot += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
  }
};

#define EXPL_PROF(slot)                                                  \
  std::optional<::stellar::core::detail::ExplProfScope> expl_scope_;     \
  if (::stellar::core::detail::expl_prof().enabled.load(                 \
          std::memory_order_relaxed))                                    \
    expl_scope_.emplace(::stellar::core::detail::expl_prof().slot);

} // namespace stellar::core::detail

#ifdef _MSC_VER
#pragma warning(pop)
#endif
