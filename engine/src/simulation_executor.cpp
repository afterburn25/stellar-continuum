#include <stellar/engine/simulation_executor.hpp>

#include <algorithm>
#include <exception>
#include <future>
#include <set>

namespace stellar::engine {

namespace {
constexpr std::size_t kTickHistory = 1024;
}

SimulationExecutor::SimulationExecutor(SimulationTierPolicy policy)
    : scheduler_(policy) {}

void SimulationExecutor::add(Key key, SimulationTask task) {
    if (!task.run)
        throw std::invalid_argument("SimulationExecutor cannot register an empty task");
    if (tasks_.count(key))
        throw std::invalid_argument("SimulationExecutor duplicate key");
    scheduler_.add(key, task.tier);
    tasks_.emplace(key, std::move(task));
}

void SimulationExecutor::remove(Key key) {
    tasks_.erase(key);
    dirty_.erase(key);
    wake_.erase(key);
    scheduler_.remove(key);
}

bool SimulationExecutor::contains(Key key) const { return tasks_.count(key) != 0; }
std::size_t SimulationExecutor::size() const noexcept { return tasks_.size(); }

void SimulationExecutor::clear() {
    tasks_.clear();
    dirty_.clear();
    wake_.clear();
    domain_stats_.clear();
    tier_runs_.fill(0);
    tick_history_.clear();
    total_wakeups_ = 0;
    scheduler_.clear();
}

void SimulationExecutor::set_tier(Key key, SimulationTier tier) {
    const auto it = tasks_.find(key);
    if (it == tasks_.end())
        throw std::invalid_argument("SimulationExecutor unknown key");
    it->second.tier = tier;
    scheduler_.set_tier(key, tier);
}

SimulationTier SimulationExecutor::tier(Key key) const {
    const auto it = tasks_.find(key);
    if (it == tasks_.end())
        throw std::invalid_argument("SimulationExecutor unknown key");
    return it->second.tier;
}

void SimulationExecutor::mark_dirty(Key key) {
    if (!tasks_.count(key))
        throw std::invalid_argument("SimulationExecutor dirty on unknown key");
    dirty_.insert(key);
}

void SimulationExecutor::clear_dirty(Key key) { dirty_.erase(key); }
bool SimulationExecutor::dirty(Key key) const { return dirty_.count(key) != 0; }

void SimulationExecutor::wake(Key key) {
    if (!tasks_.count(key))
        throw std::invalid_argument("SimulationExecutor wake on unknown key");
    wake_.insert(key);
}

void SimulationExecutor::wake_domain(std::string_view domain) {
    for (const auto& [key, task] : tasks_)
        if (task.domain == domain) wake_.insert(key);
}

void SimulationExecutor::wake_all() {
    for (const auto& [key, _] : tasks_) wake_.insert(key);
}

std::uint64_t SimulationExecutor::dormant_elapsed(Key key) const {
    return scheduler_.dormant_elapsed(key);
}

void SimulationExecutor::dormant_consumed(Key key) {
    scheduler_.dormant_consumed(key);
}

std::vector<std::pair<SimulationExecutor::Key, std::uint64_t>>
SimulationExecutor::dormant_items() const {
    std::vector<std::pair<Key, std::uint64_t>> out;
    for (const Key key : scheduler_.dormant_keys())
        out.emplace_back(key, scheduler_.dormant_elapsed(key));
    return out;
}

// Build the tick's ordered work list. Eligibility = cadence-due items
// (sorted) plus dirty and woken items. Dormant items are only ever
// eligible through dirty/wake — an event-driven run that consumes their
// accumulated elapsed without leaving the Dormant tier.
std::vector<SimulationExecutor::WorkItem>
SimulationExecutor::plan_tick() {
    std::vector<WorkItem> items;
    std::unordered_map<Key, std::size_t> index_of;
    for (const auto& d : scheduler_.collect_due()) {
        const auto& task = tasks_.at(d.key);
        const bool dirty_flag = dirty_.count(d.key) != 0;
        const bool event = dirty_flag || wake_.count(d.key);
        index_of.emplace(d.key, items.size());
        items.push_back({d.key, d.elapsed, d.tier, task.priority, event,
                         dirty_flag, false});
    }
    const auto add_wakeup = [&](Key key, bool dirty_flag) {
        if (index_of.count(key)) return; // already cadence-due
        const auto& task = tasks_.at(key);
        const bool dormant = task.tier == SimulationTier::Dormant;
        const std::uint64_t elapsed = dormant
            ? scheduler_.dormant_elapsed(key)
            : scheduler_.elapsed_since_run(key);
        index_of.emplace(key, items.size());
        items.push_back({key, elapsed, task.tier, task.priority, true,
                         dirty_flag, dormant});
    };
    // Sorted iteration for deterministic ordering/stats.
    std::vector<Key> dirty_keys(dirty_.begin(), dirty_.end());
    std::vector<Key> wake_keys(wake_.begin(), wake_.end());
    std::sort(dirty_keys.begin(), dirty_keys.end());
    std::sort(wake_keys.begin(), wake_keys.end());
    for (const Key key : dirty_keys) add_wakeup(key, true);
    for (const Key key : wake_keys) add_wakeup(key, false);

    // Topological order over depends_on edges restricted to this tick's
    // eligible set; ties by (priority, elapsed desc, key). The elapsed
    // tie-break is aging — under a persistent task budget the most
    // overdue item wins instead of the lowest key starving the rest.
    std::vector<std::vector<std::size_t>> dependents(items.size());
    std::vector<std::size_t> pending(items.size(), 0);
    for (std::size_t i = 0; i < items.size(); ++i)
        for (const Key dep : tasks_.at(items[i].key).depends_on) {
            const auto it = index_of.find(dep);
            if (it == index_of.end()) continue; // not due — ordering only
            dependents[it->second].push_back(i);
            ++pending[i];
        }
    const auto rank = [&items](std::size_t a, std::size_t b) {
        const auto& x = items[a];
        const auto& y = items[b];
        if (x.priority != y.priority) return x.priority < y.priority;
        if (x.elapsed != y.elapsed) return x.elapsed > y.elapsed;
        return x.key < y.key;
    };
    std::set<std::size_t, decltype(rank)> ready(rank);
    for (std::size_t i = 0; i < items.size(); ++i)
        if (pending[i] == 0) ready.insert(i);
    std::vector<WorkItem> ordered;
    ordered.reserve(items.size());
    std::vector<char> emitted(items.size(), 0);
    while (!ready.empty()) {
        const std::size_t i = *ready.begin();
        ready.erase(ready.begin());
        emitted[i] = 1;
        ordered.push_back(items[i]);
        for (const std::size_t d : dependents[i])
            if (--pending[d] == 0) ready.insert(d);
    }
    // Dependency cycle among eligible items: append the remainder in key
    // order — deterministic, still runs, owner should fix the graph.
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!emitted[i]) ordered.push_back(items[i]);
    return ordered;
}

void SimulationExecutor::finish_item(const WorkItem& item, std::uint64_t task_ns) {
    if (item.dormant) scheduler_.dormant_consumed(item.key);
    else scheduler_.mark_ran(item.key);
    dirty_.erase(item.key);
    wake_.erase(item.key);
    auto& st = domain_stats_[tasks_.at(item.key).domain];
    ++st.runs;
    st.total_ns += task_ns;
    st.last_tick_ns = task_ns;
    if (task_ns > st.max_ns) st.max_ns = task_ns;
    ++tier_runs_[static_cast<std::size_t>(item.tier)];
    if (item.event_wake) ++total_wakeups_;
}

SimulationStepReport SimulationExecutor::advance(SimulationBudget budget) {
    SimulationStepReport report;
    report.tick = scheduler_.tick();
    if (paused_) {
        report.paused = true;
        return report;
    }
    const auto start = std::chrono::steady_clock::now();
    report.tick = scheduler_.begin_tick();
    const auto ordered = plan_tick();
    report.eligible = ordered.size();
    const std::size_t cap =
        budget.max_tasks == 0 ? ordered.size()
                              : std::min(budget.max_tasks, ordered.size());
    for (std::size_t i = 0; i < cap; ++i) {
        if (budget.max_wall_time.count() > 0 &&
            std::chrono::steady_clock::now() - start > budget.max_wall_time)
            break;
        const auto& item = ordered[i];
        const auto t0 = std::chrono::steady_clock::now();
        tasks_.at(item.key).run(SimulationTickContext{
            report.tick, item.elapsed, item.tier, item.event_wake});
        const auto ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count());
        finish_item(item, ns);
        ++report.ran;
        if (item.dirty_flag) ++report.dirty_wakeups;
        else if (item.event_wake) ++report.event_wakeups;
    }
    report.deferred = report.eligible - report.ran;
    report.wall_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    tick_history_.push_back(report.wall_ns);
    if (tick_history_.size() > kTickHistory) tick_history_.pop_front();
    return report;
}

// Parallel step: the ordered eligible list is grouped into dependency
// waves (an item's wave = 1 + deepest eligible dependency's wave). Each
// wave submits to the JobSystem and is waited before the next — the
// deterministic boundary. Budget is enforced between waves and by the
// task cap; already-running tasks are never preempted.
SimulationStepReport
SimulationExecutor::advance_parallel(JobSystem& jobs, SimulationBudget budget) {
    SimulationStepReport report;
    report.tick = scheduler_.tick();
    if (paused_) {
        report.paused = true;
        return report;
    }
    const auto start = std::chrono::steady_clock::now();
    report.tick = scheduler_.begin_tick();
    const auto ordered = plan_tick();
    report.eligible = ordered.size();

    // Wave numbers from the already-topo-ordered list.
    std::unordered_map<Key, std::size_t> index_of;
    index_of.reserve(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i)
        index_of.emplace(ordered[i].key, i);
    std::vector<std::size_t> wave_of(ordered.size(), 0);
    std::size_t waves = ordered.empty() ? 0 : 1;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        std::size_t w = 0;
        for (const Key dep : tasks_.at(ordered[i].key).depends_on) {
            const auto it = index_of.find(dep);
            if (it != index_of.end()) w = std::max(w, wave_of[it->second] + 1);
        }
        wave_of[i] = w;
        waves = std::max(waves, w + 1);
    }

    std::vector<std::uint64_t> task_ns(ordered.size(), 0);
    std::vector<std::exception_ptr> failures(ordered.size());
    std::size_t submitted = 0;
    for (std::size_t w = 0; w < waves; ++w) {
        if (budget.max_wall_time.count() > 0 &&
            std::chrono::steady_clock::now() - start > budget.max_wall_time)
            break;
        std::vector<std::pair<std::size_t, std::future<void>>> running;
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (wave_of[i] != w) continue;
            if (budget.max_tasks && submitted >= budget.max_tasks) break;
            const auto& item = ordered[i];
            auto& task = tasks_.at(item.key);
            running.emplace_back(
                i, jobs.submit(
                       task.domain, item.priority, {},
                       [this, i, &item, &task, &task_ns, &failures,
                        tick = report.tick]() {
                           const auto t0 = std::chrono::steady_clock::now();
                           try {
                               task.run(SimulationTickContext{
                                   tick, item.elapsed, item.tier,
                                   item.event_wake});
                           } catch (...) {
                               failures[i] = std::current_exception();
                           }
                           task_ns[i] = static_cast<std::uint64_t>(
                               std::chrono::duration_cast<
                                   std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - t0)
                                   .count());
                       }));
            ++submitted;
        }
        report.jobs_submitted += running.size();
        for (auto& [i, fut] : running) {
            fut.get();
            finish_item(ordered[i], task_ns[i]);
            ++report.ran;
            if (ordered[i].dirty_flag) ++report.dirty_wakeups;
            else if (ordered[i].event_wake) ++report.event_wakeups;
        }
    }
    report.deferred = report.eligible - report.ran;
    report.wall_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    tick_history_.push_back(report.wall_ns);
    if (tick_history_.size() > kTickHistory) tick_history_.pop_front();
    for (const auto& failure : failures)
        if (failure) std::rethrow_exception(failure);
    return report;
}

const SimulationDomainStats*
SimulationExecutor::domain_stats(std::string_view domain) const {
    const auto it = domain_stats_.find(std::string(domain));
    return it == domain_stats_.end() ? nullptr : &it->second;
}

std::vector<std::string> SimulationExecutor::domains() const {
    std::vector<std::string> out;
    out.reserve(domain_stats_.size());
    for (const auto& [domain, _] : domain_stats_) out.push_back(domain);
    std::sort(out.begin(), out.end());
    return out;
}

const std::deque<std::uint64_t>& SimulationExecutor::tick_history() const {
    return tick_history_;
}

std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)>
SimulationExecutor::tier_counts() const {
    return scheduler_.tier_counts();
}

} // namespace stellar::engine
