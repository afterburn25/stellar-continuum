#include <stellar/engine/event_bus.hpp>

#include <atomic>
#include <iostream>
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

struct PlanetColonized {
    int system_id{};
    int body_id{};
};

struct ResearchCompleted {
    std::string field;
};

struct WarDeclared {
    int aggressor{};
    int target{};
};

} // namespace

int main() {
    using namespace stellar::engine;

    // Typed publish: only subscribers of the event type receive it, in
    // registration order.
    {
        EventBus bus;
        std::vector<std::string> order;
        auto a = bus.subscribe<PlanetColonized>([&](const PlanetColonized& e) {
            order.push_back("a:" + std::to_string(e.system_id));
        });
        auto b = bus.subscribe<PlanetColonized>([&](const PlanetColonized& e) {
            order.push_back("b:" + std::to_string(e.system_id));
        });
        auto other = bus.subscribe<WarDeclared>([&](const WarDeclared&) {
            order.push_back("war");
        });
        bus.publish(PlanetColonized{7, 3});
        check(order.size() == 2 && order[0] == "a:7" && order[1] == "b:7",
              "typed dispatch in registration order");
    }

    // Subscription lifetime: RAII unsubscribe stops delivery; unsubscribing
    // during dispatch completes the current dispatch safely.
    {
        EventBus bus;
        int calls = 0;
        {
            auto sub = bus.subscribe<ResearchCompleted>([&](const ResearchCompleted&) { ++calls; });
            bus.publish(ResearchCompleted{"fusion"});
            check(calls == 1, "subscriber receives published event");
        } // sub destroyed here
        bus.publish(ResearchCompleted{"shields"});
        check(calls == 1, "destroyed subscription stops delivery");

        Subscription self;
        std::vector<int> seen;
        self = bus.subscribe<PlanetColonized>([&](const PlanetColonized& e) {
            seen.push_back(e.system_id);
            self.unsubscribe();
        });
        bus.publish(PlanetColonized{1, 0});
        bus.publish(PlanetColonized{2, 0});
        check(seen.size() == 1, "mid-dispatch unsubscribe stops future delivery");
    }

    // Deferred queue: events dispatch at drain() in publish order; later ticks
    // stay queued.
    {
        EventBus bus;
        std::vector<int> order;
        auto sub = bus.subscribe<PlanetColonized>([&](const PlanetColonized& e) {
            order.push_back(e.system_id);
        });
        bus.defer(10, PlanetColonized{1, 0});
        bus.defer(10, PlanetColonized{2, 0});
        bus.defer(50, PlanetColonized{3, 0});
        check(bus.stats().pending == 3, "deferred events are queued");
        check(order.empty(), "deferred events do not dispatch immediately");
        const auto dispatched = bus.drain(10);
        check(dispatched == 2, "drain dispatches only due events");
        check(order.size() == 2 && order[0] == 1 && order[1] == 2,
              "deferred dispatch preserves publish order");
        check(bus.drain(50) == 1 && order[2] == 3, "later tick drains remaining");
        check(bus.stats().pending == 0, "queue empties after drain");
    }

    // Cross-type isolation + stats.
    {
        EventBus bus;
        int planets = 0, wars = 0;
        auto p = bus.subscribe<PlanetColonized>([&](const PlanetColonized&) { ++planets; });
        auto w = bus.subscribe<WarDeclared>([&](const WarDeclared&) { ++wars; });
        bus.publish(PlanetColonized{5, 0});
        bus.publish(WarDeclared{1, 2});
        check(planets == 1 && wars == 1, "types are isolated");
        const auto stats = bus.stats();
        check(stats.published == 2 && stats.dispatched == 2 && stats.subscriptions == 2,
              "stats track publish/dispatch/subscriptions");
    }

    // Owner-thread enforcement: foreign-thread publish throws.
    {
        EventBus bus;
        std::atomic<bool> threw{false};
        std::thread foreign([&] {
            try {
                bus.publish(PlanetColonized{1, 1});
            } catch (const std::logic_error&) {
                threw.store(true);
            }
        });
        foreign.join();
        check(threw.load(), "foreign-thread publish throws logic_error");
    }

    if (failures != 0) {
        std::cerr << failures << " event bus checks failed\n";
        return 1;
    }
    std::cout << "EventBus typed/deferred/lifecycle tests passed\n";
    return 0;
}
