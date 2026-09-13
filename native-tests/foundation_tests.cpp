#include <stellar/engine/foundation.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace stellar::engine;
using namespace std::chrono_literals;

namespace {
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}

template <class F>
void expect_throw(F&& operation, std::string_view message) {
    try { operation(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(std::string{message});
}

void entity_registry_rejects_stale_and_double_destroy() {
    EntityRegistry registry;
    const auto first = registry.create();
    check(registry.contains(first) && registry.size() == 1, "fresh entity was not registered");
    check(registry.destroy(first), "initial destroy failed");
    check(!registry.destroy(first) && !registry.contains(first), "stale or double destroy was accepted");
    const auto replacement = registry.create();
    check(replacement.index == first.index && replacement.generation != first.generation, "reused slot did not advance its generation");
    check(first.value() != replacement.value(), "generational entity value was not stable");
}

void clock_preserves_partition_speed_pause_and_backlog() {
    FixedClock clock{10ns};
    check(clock.advance(7ns) == 0 && clock.backlog() == 7ns, "clock lost partial elapsed time");
    check(clock.advance(3ns) == 1 && clock.tick() == 1 && clock.backlog() == 0ns, "clock partition changed tick result");
    clock.set_speed(2);
    check(clock.advance(15ns) == 3 && clock.tick() == 4, "clock speed multiplier was not integer deterministic");
    clock.set_paused(true);
    check(clock.advance(100ns) == 0 && clock.tick() == 4 && clock.backlog() == 0ns, "paused clock retained wall time");
    clock.set_paused(false);
    check(clock.advance(100ns, 4) == 4 && clock.backlog() == 160ns, "clock did not retain bounded catch-up backlog");
    check(clock.advance(0ns, 64) == 16 && clock.backlog() == 0ns, "clock did not consume retained backlog");
    expect_throw([&] { clock.set_speed(65); }, "invalid speed did not throw");
    expect_throw([&] { (void)clock.advance(-1ns); }, "negative elapsed time did not throw");
    FixedClock overflow{1ns};
    overflow.set_speed(64);
    expect_throw([&] { (void)overflow.advance(std::chrono::nanoseconds::max()); }, "overflowing elapsed time did not throw");
}

void deterministic_rng_matches_splitmix_vector() {
    DeterministicRandom random{0};
    constexpr std::uint64_t expected[] = {
        0xE220A8397B1DCDAFULL, 0x6E789E6AA1B965F4ULL, 0x06C45D188009454FULL,
        0xF88BB8A8724C81ECULL, 0x1B39896A51A8749BULL,
    };
    for (const auto value : expected) check(random.next_u64() == value, "SplitMix64 known vector changed");
    const auto unit = random.unit_double();
    check(unit >= 0.0 && unit < 1.0, "random unit double escaped [0,1)");
}

void event_queue_drains_snapshot_in_order() {
    EventQueue<int> queue;
    queue.publish(4); queue.publish(9);
    const auto first = queue.drain();
    queue.publish(16);
    const auto second = queue.drain();
    check((first == std::vector<int>{4, 9}) && (second == std::vector<int>{16}), "event drain did not preserve publication batches");
    std::atomic<bool> rejected{};
    std::thread foreign([&] { try { queue.publish(25); } catch (const std::logic_error&) { rejected = true; } });
    foreign.join();
    check(rejected, "event queue accepted foreign-thread publication");
}

void jobs_run_once_complete_and_propagate_errors() {
    std::atomic<int> completed{};
    {
        JobSystem jobs{3};
        check(jobs.worker_count() >= 1 && jobs.worker_count() <= 64, "job worker count escaped bounds");
        std::vector<std::future<void>> futures;
        for (int index = 0; index < 96; ++index)
            futures.push_back(jobs.submit([&completed] { completed.fetch_add(1, std::memory_order_relaxed); }));
        auto failing = jobs.submit([] { throw std::runtime_error("expected worker failure"); });
        jobs.wait_idle();
        for (auto& future : futures) future.get();
        expect_throw([&] { failing.get(); }, "worker exception did not propagate through future");
        check(completed == 96, "jobs did not execute exactly once");
    }
    std::atomic<int> drained{};
    { JobSystem jobs{2}; for (int index = 0; index < 24; ++index) (void)jobs.submit([&drained] { drained.fetch_add(1, std::memory_order_relaxed); }); }
    check(drained == 24, "job destructor did not drain accepted work");
}
}

int main() {
    try {
        entity_registry_rejects_stale_and_double_destroy();
        clock_preserves_partition_speed_pause_and_backlog();
        deterministic_rng_matches_splitmix_vector();
        event_queue_drains_snapshot_in_order();
        jobs_run_once_complete_and_propagate_errors();
        std::cout << "foundation_tests: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "foundation_tests failed: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "foundation_tests failed: unknown exception\n";
    }
    return 1;
}
