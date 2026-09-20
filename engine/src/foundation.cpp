#include <stellar/engine/foundation.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace stellar::engine {
namespace {
std::mutex log_mutex;
constexpr std::size_t max_workers = 64;

void set_current_thread_name(const std::string& name) {
#if defined(_WIN32)
    std::wstring wide{name.begin(), name.end()};
    SetThreadDescription(GetCurrentThread(), wide.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name.c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#else
    (void)name;
#endif
}
}

EntityId EntityRegistry::create() {
    std::uint32_t index{};
    if (!free_.empty()) {
        index = free_.back();
        free_.pop_back();
    } else {
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("EntityRegistry index space exhausted");
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back({});
    }
    auto& slot = slots_[index];
    slot.alive = true;
    ++size_;
    return {index, slot.generation};
}

bool EntityRegistry::destroy(EntityId id) {
    if (id.index >= slots_.size()) return false;
    auto& slot = slots_[id.index];
    if (!slot.alive || slot.generation != id.generation) return false;
    if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
        slot.alive = false;
        --size_;
        slot.retired = true;
    } else {
        // Reserve the reusable slot before changing observable identity state. If this
        // allocation fails, the entity remains alive and the registry stays coherent.
        free_.push_back(id.index);
        slot.alive = false;
        --size_;
        ++slot.generation;
    }
    return true;
}

bool EntityRegistry::contains(EntityId id) const noexcept {
    return id.index < slots_.size() && slots_[id.index].alive && slots_[id.index].generation == id.generation;
}

std::size_t EntityRegistry::size() const noexcept { return size_; }

FixedClock::FixedClock(std::chrono::nanoseconds step) {
    if (step.count() <= 0) throw std::invalid_argument("FixedClock step must be positive");
    step_ = static_cast<std::uint64_t>(step.count());
}

void FixedClock::set_paused(bool paused) noexcept { paused_ = paused; }

void FixedClock::set_speed(std::uint32_t speed) {
    if (speed == 0 || speed > 64) throw std::out_of_range("FixedClock speed must be between 1 and 64");
    speed_ = speed;
}

std::uint64_t FixedClock::advance(std::chrono::nanoseconds elapsed, std::uint64_t max_ticks) {
    if (elapsed.count() < 0) throw std::invalid_argument("FixedClock elapsed time must not be negative");
    if (max_ticks == 0) throw std::invalid_argument("FixedClock max_ticks must be positive");
    if (paused_) return 0;
    const auto elapsed_count = static_cast<std::uint64_t>(elapsed.count());
    constexpr auto maximum_backlog = static_cast<std::uint64_t>(std::numeric_limits<std::chrono::nanoseconds::rep>::max());
    if (elapsed_count > (maximum_backlog - accumulated_) / speed_)
        throw std::overflow_error("FixedClock accumulated time overflow");
    const auto accumulated = accumulated_ + elapsed_count * speed_;
    const auto available = accumulated / step_;
    const auto advanced = std::min(available, max_ticks);
    if (advanced > std::numeric_limits<Tick>::max() - tick_)
        throw std::overflow_error("FixedClock tick overflow");
    accumulated_ = accumulated - advanced * step_;
    tick_ += advanced;
    return advanced;
}

Tick FixedClock::tick() const noexcept { return tick_; }
std::chrono::nanoseconds FixedClock::backlog() const noexcept { return std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(accumulated_)}; }

std::uint64_t DeterministicRandom::next_u64() noexcept {
    state_ += 0x9E3779B97F4A7C15ULL;
    auto value = state_;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

double DeterministicRandom::unit_double() noexcept {
    return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
}

struct JobSystem::QueuedJob {
    std::function<void()> run;
    std::promise<void> promise;
    std::string tag;
    JobCancelToken cancel;
    JobPriority priority{JobPriority::Normal};
    std::chrono::steady_clock::time_point submitted_at;
    std::atomic<std::size_t> pending_dependencies{0};
    std::atomic<bool> dependency_failed{false};
    std::vector<std::shared_ptr<QueuedJob>> dependents;
};

JobSystem::JobSystem(std::size_t workers) {
    if (workers == 0) workers = std::thread::hardware_concurrency();
    workers = std::clamp(workers == 0 ? std::size_t{1} : workers, std::size_t{1}, max_workers);
    workers_.reserve(workers);
    try {
        for (std::size_t index = 0; index < workers; ++index)
            workers_.emplace_back([this, index] { worker_loop(index); });
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            stopping_ = true;
        }
        work_ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        throw;
    }
}

JobSystem::~JobSystem() {
    { std::lock_guard lock(mutex_); accepting_ = false; }
    wait_idle();
    { std::lock_guard lock(mutex_); stopping_ = true; }
    work_ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

std::future<void> JobSystem::submit(std::function<void()> task) {
    return submit({}, JobPriority::Normal, {}, std::move(task));
}

std::future<void> JobSystem::submit(JobPriority priority, std::function<void()> task) {
    return submit({}, priority, {}, std::move(task));
}

std::future<void> JobSystem::submit(std::string_view tag, JobPriority priority,
                                    JobCancelToken cancel, std::function<void()> task) {
    if (!task) throw std::invalid_argument("JobSystem cannot submit an empty task");
    if (priority >= JobPriority::Count) throw std::invalid_argument("JobSystem priority out of range");
    auto job = std::make_shared<QueuedJob>();
    job->run = std::move(task);
    job->tag = std::string{tag};
    job->cancel = std::move(cancel);
    job->priority = priority;
    job->submitted_at = std::chrono::steady_clock::now();
    auto future = job->promise.get_future();
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) throw std::runtime_error("JobSystem is shutting down");
        ++totals_.submitted;
        ++tag_stats_[job->tag].submitted;
        enqueue_locked(std::move(job));
    }
    work_ready_.notify_one();
    return future;
}

std::vector<std::shared_future<void>> JobSystem::submit_graph(std::vector<Node> nodes) {
    const std::size_t count = nodes.size();
    std::vector<std::shared_ptr<QueuedJob>> slots(count);
    std::vector<std::shared_future<void>> futures(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& node = nodes[i];
        if (!node.run) throw std::invalid_argument("JobSystem graph node has an empty task");
        if (node.priority >= JobPriority::Count) throw std::invalid_argument("JobSystem graph node priority out of range");
        auto slot = std::make_shared<QueuedJob>();
        slot->run = std::move(node.run);
        slot->tag = std::move(node.tag);
        slot->cancel = std::move(node.cancel);
        slot->priority = node.priority;
        slot->submitted_at = std::chrono::steady_clock::now();
        futures[i] = slot->promise.get_future().share();
        slots[i] = std::move(slot);
    }
    std::vector<std::size_t> pending(count, 0);
    std::vector<std::vector<std::size_t>> dependents(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t dependency : nodes[i].depends_on) {
            if (dependency >= count) throw std::invalid_argument("JobSystem graph dependency index out of range");
            slots[dependency]->dependents.push_back(slots[i]);
            dependents[dependency].push_back(i);
            ++pending[i];
        }
        slots[i]->pending_dependencies.store(pending[i], std::memory_order_relaxed);
    }
    // Reject cycles before anything runs: repeatedly drop zero-pending nodes.
    {
        std::vector<std::size_t> remaining = pending;
        std::vector<std::size_t> frontier;
        for (std::size_t i = 0; i < count; ++i) if (remaining[i] == 0) frontier.push_back(i);
        std::size_t resolved = 0;
        while (!frontier.empty()) {
            const std::size_t node = frontier.back();
            frontier.pop_back();
            ++resolved;
            for (std::size_t dependent : dependents[node])
                if (--remaining[dependent] == 0) frontier.push_back(dependent);
        }
        if (resolved != count) throw std::invalid_argument("JobSystem graph contains a dependency cycle");
    }
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) throw std::runtime_error("JobSystem is shutting down");
        totals_.submitted += count;
        for (std::size_t i = 0; i < count; ++i) {
            ++tag_stats_[slots[i]->tag].submitted;
            if (pending[i] == 0) enqueue_locked(std::move(slots[i]));
        }
    }
    work_ready_.notify_all();
    return futures;
}

void JobSystem::enqueue_locked(std::shared_ptr<QueuedJob> job) {
    auto& queue = jobs_[static_cast<std::size_t>(job->priority)];
    queue.push_back(std::move(job));
    std::size_t queued = 0;
    for (const auto& q : jobs_) queued += q.size();
    queue_high_water_ = std::max(queue_high_water_, static_cast<std::uint64_t>(queued));
}

void JobSystem::finish_dependents(const QueuedJob& job, bool success) {
    std::vector<std::shared_ptr<QueuedJob>> ready;
    for (const auto& dependent : job.dependents) {
        if (!success) dependent->dependency_failed.store(true, std::memory_order_relaxed);
        if (dependent->pending_dependencies.fetch_sub(1, std::memory_order_acq_rel) == 1)
            ready.push_back(dependent);
    }
    if (ready.empty()) return;
    std::lock_guard lock(mutex_);
    for (auto& dependent : ready) {
        if (dependent->dependency_failed.load(std::memory_order_relaxed)) {
            dependent->promise.set_exception(std::make_exception_ptr(JobDependencyError{}));
            ++totals_.failed;
            ++tag_stats_[dependent->tag].failed;
        } else {
            enqueue_locked(std::move(dependent));
        }
    }
    work_ready_.notify_all();
}

void JobSystem::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] {
        for (const auto& queue : jobs_) if (!queue.empty()) return false;
        return active_ == 0;
    });
}

std::size_t JobSystem::worker_count() const noexcept { return workers_.size(); }

JobStats JobSystem::stats() const {
    std::lock_guard lock(mutex_);
    JobStats snapshot = totals_;
    snapshot.tags = tag_stats_;
    snapshot.workers = workers_.size();
    for (const auto& queue : jobs_) snapshot.queued += queue.size();
    snapshot.queue_high_water = queue_high_water_;
    return snapshot;
}

void JobSystem::reset_stats() {
    std::lock_guard lock(mutex_);
    totals_ = JobStats{};
    tag_stats_.clear();
    queue_high_water_ = 0;
}

void JobSystem::worker_loop(std::size_t worker_index) {
    set_current_thread_name(std::string{"stellar-job-"} + std::to_string(worker_index));
    for (;;) {
        std::shared_ptr<QueuedJob> job;
        {
            std::unique_lock lock(mutex_);
            work_ready_.wait(lock, [this] {
                if (stopping_) return true;
                for (const auto& queue : jobs_) if (!queue.empty()) return true;
                return false;
            });
            std::size_t picked = static_cast<std::size_t>(JobPriority::Count);
            for (std::size_t i = 0; i < jobs_.size(); ++i) {
                if (!jobs_[i].empty()) { picked = i; break; }
            }
            if (picked == static_cast<std::size_t>(JobPriority::Count)) {
                if (stopping_) return;
                continue;
            }
            job = std::move(jobs_[picked].front());
            jobs_[picked].pop_front();
            ++active_;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto wait_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(started - job->submitted_at).count());
        if (job->cancel.cancelled()) {
            job->promise.set_exception(std::make_exception_ptr(JobCancelledError{}));
            {
                std::lock_guard lock(mutex_);
                ++totals_.cancelled;
                ++tag_stats_[job->tag].cancelled;
                tag_stats_[job->tag].wait_nanoseconds += wait_ns;
            }
            finish_dependents(*job, false);
        } else {
            bool success = false;
            try {
                job->run();
                job->promise.set_value();
                success = true;
            } catch (...) {
                try {
                    job->promise.set_exception(std::current_exception());
                } catch (...) {}
            }
            const auto run_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
            {
                std::lock_guard lock(mutex_);
                auto& tag = tag_stats_[job->tag];
                tag.wait_nanoseconds += wait_ns;
                tag.run_nanoseconds += run_ns;
                if (success) {
                    ++totals_.completed;
                    ++tag.completed;
                } else {
                    ++totals_.failed;
                    ++tag.failed;
                }
            }
            finish_dependents(*job, success);
        }
        {
            std::lock_guard lock(mutex_);
            --active_;
            bool empty = true;
            for (const auto& queue : jobs_) if (!queue.empty()) { empty = false; break; }
            if (empty && active_ == 0) idle_.notify_all();
        }
    }
}

void log(std::string_view level, std::string_view message) {
    std::lock_guard lock(log_mutex);
    std::cerr << '[' << level << "] " << message << '\n';
}

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}

} // namespace stellar::engine
