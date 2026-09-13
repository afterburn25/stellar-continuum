#pragma once

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
#include <string_view>
#include <thread>
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

private:
    struct Slot { std::uint32_t generation{1}; bool alive{}; bool retired{}; };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t size_{};
};

class FixedClock {
public:
    explicit FixedClock(std::chrono::nanoseconds step);
    void set_paused(bool paused) noexcept;
    void set_speed(std::uint32_t speed);
    [[nodiscard]] std::uint64_t advance(std::chrono::nanoseconds elapsed, std::uint64_t max_ticks = 4096);
    [[nodiscard]] Tick tick() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds backlog() const noexcept;

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

class JobSystem {
public:
    explicit JobSystem(std::size_t workers = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::future<void> submit(std::function<void()> task);
    void wait_idle();
    [[nodiscard]] std::size_t worker_count() const noexcept;

private:
    void worker_loop();
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable idle_;
    std::size_t active_{};
    bool accepting_{true};
    bool stopping_{};
};

void log(std::string_view level, std::string_view message);
void require(bool condition, std::string_view message);

} // namespace stellar::engine
