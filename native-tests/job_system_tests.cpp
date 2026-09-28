#include <stellar/engine/foundation.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using namespace stellar::engine;

    // Priorities: a saturated single worker drains High before Low even when
    // the Low job was submitted first.
    {
        JobSystem jobs{1};
        JobCancelSource gate;
        std::atomic<int> stage{0};
        auto hold = jobs.submit([&] {
            while (stage.load() == 0) std::this_thread::yield();
        });
        std::vector<int> order;
        std::mutex order_mutex;
        jobs.submit(JobPriority::Low, [&] { std::lock_guard l(order_mutex); order.push_back(1); });
        jobs.submit(JobPriority::High, [&] { std::lock_guard l(order_mutex); order.push_back(2); });
        jobs.submit(JobPriority::Normal, [&] { std::lock_guard l(order_mutex); order.push_back(3); });
        stage.store(1);
        jobs.wait_idle();
        check(order.size() == 3 && order[0] == 2 && order[1] == 3 && order[2] == 1,
              "priority order High,Normal,Low expected");
    }

    // Cancellation: a token cancelled before dequeue produces JobCancelledError
    // and never runs the task body.
    {
        JobSystem jobs{1};
        std::atomic<bool> ran{false};
        JobCancelSource source;
        auto first = jobs.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); });
        auto cancelled = jobs.submit("cancellable", JobPriority::Normal, source.token(),
                                     [&] { ran.store(true); });
        source.cancel();
        bool threw_cancelled = false;
        try {
            cancelled.get();
        } catch (const JobCancelledError&) {
            threw_cancelled = true;
        }
        jobs.wait_idle();
        check(threw_cancelled, "cancelled job must complete future with JobCancelledError");
        check(!ran.load(), "cancelled job must not execute its body");
    }

    // Dependency graph: dependents run only after all dependencies succeed.
    {
        JobSystem jobs{4};
        std::vector<int> order;
        std::mutex order_mutex;
        std::atomic<int> done{0};
        auto push = [&](int value) {
            std::lock_guard l(order_mutex);
            order.push_back(value);
            done.fetch_add(1);
        };
        std::vector<JobSystem::Node> nodes;
        nodes.push_back({"a", JobPriority::Normal, {}, [&, v = 0] { push(v); }, {}});
        nodes.push_back({"b", JobPriority::Normal, {}, [&, v = 1] { push(v); }, {0}});
        nodes.push_back({"c", JobPriority::Normal, {}, [&, v = 2] { push(v); }, {0}});
        nodes.push_back({"d", JobPriority::Normal, {}, [&, v = 3] { push(v); }, {1, 2}});
        auto futures = jobs.submit_graph(std::move(nodes));
        for (auto& future : futures) future.get();
        check(done.load() == 4, "all graph nodes must run");
        auto pos = [&](int v) {
            return std::find(order.begin(), order.end(), v) - order.begin();
        };
        check(pos(0) < pos(1) && pos(0) < pos(2) && pos(1) < pos(3) && pos(2) < pos(3),
              "graph ordering violated");
    }

    // A failed dependency fails its dependent without running it.
    {
        JobSystem jobs{2};
        std::atomic<bool> ran{false};
        std::vector<JobSystem::Node> nodes;
        nodes.push_back({"fail", JobPriority::Normal, {},
                         [] { throw std::runtime_error("boom"); }, {}});
        nodes.push_back({"dependent", JobPriority::Normal, {},
                         [&] { ran.store(true); }, {0}});
        auto futures = jobs.submit_graph(std::move(nodes));
        bool dep_failed = false;
        try {
            futures[1].get();
        } catch (const JobDependencyError&) {
            dep_failed = true;
        }
        check(dep_failed, "dependent must receive JobDependencyError");
        check(!ran.load(), "dependent of failed node must not run");
    }

    // Cycles are rejected before any work queues.
    {
        JobSystem jobs{2};
        std::vector<JobSystem::Node> nodes;
        nodes.push_back({"x", JobPriority::Normal, {}, [] {}, {1}});
        nodes.push_back({"y", JobPriority::Normal, {}, [] {}, {0}});
        bool threw = false;
        try {
            jobs.submit_graph(std::move(nodes));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "dependency cycle must throw invalid_argument");
    }

    // Stats track submitted/completed/failed/cancelled plus per-tag rollups.
    {
        JobSystem jobs{2};
        jobs.submit("decode", JobPriority::Normal, {}, [] {}).get();
        jobs.submit("decode", JobPriority::Normal, {}, [] {}).get();
        jobs.submit("save", JobPriority::Normal, {}, [] { throw std::runtime_error("x"); });
        jobs.wait_idle();
        const auto stats = jobs.stats();
        check(stats.submitted == 3 && stats.completed == 2 && stats.failed == 1,
              "totals must count submitted/completed/failed");
        check(stats.workers == 2, "stats must report worker count");
        const auto decode = stats.tags.find("decode");
        check(decode != stats.tags.end() && decode->second.completed == 2,
              "per-tag completed count expected");
        const auto save = stats.tags.find("save");
        check(save != stats.tags.end() && save->second.failed == 1,
              "per-tag failed count expected");
        check(stats.queue_high_water >= 1, "queue high-water must record depth");
    }

    // Cooperative cancellation inside a running job via throw_if_cancelled.
    {
        JobSystem jobs{1};
        JobCancelSource source;
        std::atomic<int> iterations{0};
        auto future = jobs.submit("loop", JobPriority::Normal, source.token(), [&] {
            for (int i = 0; i < 100000; ++i) {
                source.token().throw_if_cancelled();
                iterations.fetch_add(1);
            }
        });
        source.cancel();
        bool threw = false;
        try {
            future.get();
        } catch (const JobCancelledError&) {
            threw = true;
        }
        check(threw, "cooperative cancellation must surface JobCancelledError");
        check(iterations.load() < 100000, "cooperative cancellation must stop the loop early");
    }

    // Backward compatibility: bare submit still returns a working future and
    // exceptions propagate.
    {
        JobSystem jobs{0};
        std::atomic<int> value{0};
        jobs.submit([&] { value.store(7); }).get();
        check(value.load() == 7, "plain submit must execute");
        bool threw = false;
        try {
            jobs.submit([] { throw std::runtime_error("plain"); }).get();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "plain submit must propagate exceptions");
    }

    if (failures != 0) {
        std::cerr << failures << " job system checks failed\n";
        return 1;
    }
    std::cout << "JobSystem priority/cancellation/graph/stats tests passed\n";
    return 0;
}
