#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stellar::engine {

using Tick = std::uint64_t;

struct EntityId {
    std::uint32_t index{};
    std::uint32_t generation{};
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return (static_cast<std::uint64_t>(generation) << 32) | index; }
    friend constexpr bool operator==(EntityId, EntityId) = default;
};

class EntityRegistry {
public:
    [[nodiscard]] EntityId create();
    [[nodiscard]] bool destroy(EntityId id);
    [[nodiscard]] bool contains(EntityId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    // Container-storage footprint for MemoryTracker::report — vector
    // capacities, not allocator truth.
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return slots_.capacity()*sizeof(Slot)+free_.capacity()*sizeof(std::uint32_t);
    }

private:
    struct Slot { std::uint32_t generation{1}; bool alive{}; bool retired{}; };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t size_{};
};

struct FixedClockSnapshot {
    std::chrono::nanoseconds step{}, backlog{};
    Tick tick{};
    std::uint32_t speed{1};
    bool paused{};
    bool operator==(const FixedClockSnapshot &) const = default;
};
class FixedClock {
public:
    explicit FixedClock(std::chrono::nanoseconds step);
    void set_paused(bool paused) noexcept;
    void set_speed(std::uint32_t speed);
    // Explicit debugger step; consumes pending time first and retains pause/speed.
    void step_once();
    [[nodiscard]] std::uint64_t advance(std::chrono::nanoseconds elapsed, std::uint64_t max_ticks = 4096);
    [[nodiscard]] Tick tick() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds backlog() const noexcept;
    [[nodiscard]] FixedClockSnapshot snapshot() const noexcept;
    void restore(const FixedClockSnapshot &);

private:
    std::uint64_t step_{};
    std::uint64_t accumulated_{};
    Tick tick_{};
    std::uint32_t speed_{1};
    bool paused_{};
};

class DeterministicRandom {
public:
    explicit constexpr DeterministicRandom(std::uint64_t seed) noexcept : state_(seed) {}
    [[nodiscard]] std::uint64_t next_u64() noexcept;
    [[nodiscard]] double unit_double() noexcept;

private:
    std::uint64_t state_{};
};

template <class T>
class EventQueue {
public:
    EventQueue() : owner_(std::this_thread::get_id()) {}
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    void publish(T event) {
        require_owner();
        events_.push_back(std::move(event));
    }
    [[nodiscard]] std::vector<T> drain() {
        require_owner();
        std::vector<T> batch;
        batch.swap(events_);
        return batch;
    }

private:
    void require_owner() const {
        if (std::this_thread::get_id() != owner_) throw std::logic_error("EventQueue may only be used by its simulation owner thread");
    }
    std::thread::id owner_;
    std::vector<T> events_;
};

enum class JobPriority : std::size_t { High = 0, Normal = 1, Low = 2, Count = 3 };

class JobCancelledError : public std::runtime_error {
public:
    JobCancelledError() : std::runtime_error("job cancelled before execution") {}
};

class JobDependencyError : public std::runtime_error {
public:
    JobDependencyError() : std::runtime_error("job dependency did not complete successfully") {}
};

class JobCancelToken {
public:
    JobCancelToken() = default;
    explicit JobCancelToken(std::shared_ptr<std::atomic_bool> state) : state_(std::move(state)) {}
    [[nodiscard]] bool cancelled() const noexcept { return state_ && state_->load(std::memory_order_relaxed); }
    void throw_if_cancelled() const { if (cancelled()) throw JobCancelledError{}; }
private:
    std::shared_ptr<std::atomic_bool> state_;
};

class JobCancelSource {
public:
    JobCancelSource() : state_(std::make_shared<std::atomic_bool>(false)) {}
    [[nodiscard]] JobCancelToken token() const { return JobCancelToken{state_}; }
    void cancel() noexcept { state_->store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool cancelled() const noexcept { return state_->load(std::memory_order_relaxed); }
private:
    std::shared_ptr<std::atomic_bool> state_;
};

struct JobTagStats {
    std::uint64_t submitted{};
    std::uint64_t completed{};
    std::uint64_t failed{};
    std::uint64_t cancelled{};
    std::uint64_t wait_nanoseconds{};
    std::uint64_t run_nanoseconds{};
};

struct JobStats {
    std::uint64_t submitted{};
    std::uint64_t completed{};
    std::uint64_t failed{};
    std::uint64_t cancelled{};
    std::uint64_t queued{};
    std::uint64_t queue_high_water{};
    std::size_t workers{};
    std::unordered_map<std::string, JobTagStats> tags;
};

class JobSystem {
public:
    explicit JobSystem(std::size_t workers = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::future<void> submit(std::function<void()> task);
    [[nodiscard]] std::future<void> submit(JobPriority priority, std::function<void()> task);
    [[nodiscard]] std::future<void> submit(std::string_view tag, JobPriority priority,
                                           JobCancelToken cancel, std::function<void()> task);

    struct Node {
        std::string tag;
        JobPriority priority{JobPriority::Normal};
        JobCancelToken cancel;
        std::function<void()> run;
        std::vector<std::size_t> depends_on;
    };
    // Submits a dependency graph. A node queues once every dependency finished
    // successfully; a failed or cancelled dependency fails the dependent's future
    // with JobDependencyError without running it. Cycles and out-of-range
    // dependencies throw std::invalid_argument before anything is queued.
    [[nodiscard]] std::vector<std::shared_future<void>> submit_graph(std::vector<Node> nodes);

    void wait_idle();
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] JobStats stats() const;
    void reset_stats();

private:
    struct QueuedJob;
    void enqueue_locked(std::shared_ptr<QueuedJob> job);
    void finish_dependents(const QueuedJob& job, bool success);
    void worker_loop(std::size_t worker_index);
    std::vector<std::thread> workers_;
    std::array<std::deque<std::shared_ptr<QueuedJob>>, static_cast<std::size_t>(JobPriority::Count)> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable idle_;
    std::size_t active_{};
    std::uint64_t queue_high_water_{};
    std::unordered_map<std::string, JobTagStats> tag_stats_;
    JobStats totals_;
    bool accepting_{true};
    bool stopping_{};
};

void log(std::string_view level, std::string_view message);
void require(bool condition, std::string_view message);

} // namespace stellar::engine
