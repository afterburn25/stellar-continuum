#include <stellar/engine/foundation.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>

namespace stellar::engine {
namespace {
std::mutex log_mutex;
constexpr std::size_t max_workers = 64;
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

JobSystem::JobSystem(std::size_t workers) {
    if (workers == 0) workers = std::thread::hardware_concurrency();
    workers = std::clamp(workers == 0 ? std::size_t{1} : workers, std::size_t{1}, max_workers);
    workers_.reserve(workers);
    try {
        for (std::size_t index = 0; index < workers; ++index) workers_.emplace_back([this] { worker_loop(); });
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
    if (!task) throw std::invalid_argument("JobSystem cannot submit an empty task");
    auto packaged = std::make_shared<std::packaged_task<void()>>(std::move(task));
    auto future = packaged->get_future();
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) throw std::runtime_error("JobSystem is shutting down");
        jobs_.emplace_back([packaged] { (*packaged)(); });
    }
    work_ready_.notify_one();
    return future;
}

void JobSystem::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && active_ == 0; });
}

std::size_t JobSystem::worker_count() const noexcept { return workers_.size(); }

void JobSystem::worker_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            work_ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++active_;
        }
        job();
        {
            std::lock_guard lock(mutex_);
            --active_;
            if (jobs_.empty() && active_ == 0) idle_.notify_all();
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
