#include <stellar/engine/history.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

HistoryEvent ev(std::string category, double day, double sig = 0.5) {
    HistoryEvent e;
    e.category = std::move(category);
    e.at_day = day;
    e.significance = sig;
    return e;
}

} // namespace

int main() {
    // --- Record + id assignment ----------------------------------------------
    {
        EventHistory h;
        const auto id1 = h.record(ev("colony.founded", 10.0));
        const auto id2 = h.record(ev("war.battle", 12.0));
        check(id1 == 1 && id2 == 2, "ids are monotonic");
        check(h.event(id1)->category == "colony.founded", "lookup by id");
        check(h.event(99) == nullptr, "missing id returns null");
    }

    // --- Category/actor/time/significance queries ------------------------------
    {
        EventHistory h;
        auto a = ev("war.battle", 10.0, 0.8);
        a.actors = {7, 9};
        h.record(a);
        auto b = ev("colony.founded", 20.0, 0.3);
        b.actors = {7};
        h.record(b);
        h.record(ev("discovery.ruin", 30.0, 0.9));

        HistoryQuery q;
        q.category = "war.battle";
        check(h.query(q).size() == 1, "category filter");

        q = {};
        q.actor = 9;
        check(h.query(q).size() == 1, "actor filter");
        q.actor = 7;
        check(h.query(q).size() == 2, "actor matches all their events");

        q = {};
        q.after_day = 15.0;
        check(h.query(q).size() == 2, "time floor");
        q.min_significance = 0.9;
        check(h.query(q).size() == 1, "significance threshold");

        q = {};
        q.limit = 2;
        const auto recent = h.query(q);
        check(recent.size() == 2 && recent.back()->at_day == 30.0,
              "limit returns most recent");
    }

    // --- Observer filtering -------------------------------------------------------
    {
        EventHistory h;
        auto secret = ev("espionage.op", 5.0, 0.9);
        secret.visible_to = {7}; // only faction 7 sees it
        h.record(secret);
        h.record(ev("war.declared", 6.0, 0.9)); // public
        auto allied = ev("trade.pact", 7.0, 0.4);
        allied.visible_to = {7, 9};
        h.record(allied);

        HistoryQuery q;
        check(h.query(q).size() == 3, "omniscient sees all");
        q.observer = 9;
        check(h.query(q).size() == 2, "faction sees public + own");
        q.observer = 3;
        check(h.query(q).size() == 1, "outsider sees public only");
        check(h.query(q).front()->category == "war.declared",
              "outsider gets the public event");
    }

    // --- News feed ------------------------------------------------------------------
    {
        EventHistory h;
        h.record(ev("colony.founded", 100.0, 0.4));
        h.record(ev("war.battle", 110.0, 0.95));
        auto secret = ev("espionage.op", 120.0, 1.0);
        secret.visible_to = {7};
        h.record(secret);
        const auto f = h.feed(/*observer*/ 9, /*since*/ 105.0, /*sig*/ 0.5);
        check(f.size() == 1 && f.front()->category == "war.battle",
              "feed filters time+significance+observer");
        check(h.feed(7, 105.0, 0.5).size() == 2, "insider sees own ops");
        check(h.feed(std::nullopt, 0.0).size() == 3,
              "omniscient feed sees all");
    }

    // --- Ordering and pruning --------------------------------------------------------
    {
        EventHistory h(5); // capacity 5
        for (int i = 0; i < 7; ++i) h.record(ev("e", double(i)));
        check(h.size() == 5, "capacity bound holds");
        check(h.query({}).front()->at_day == 2.0, "oldest dropped");
        // Prune keeps high-significance records.
        h.clear();
        h.record(ev("minor", 1.0, 0.2));
        h.record(ev("major", 2.0, 0.95));
        h.record(ev("minor2", 3.0, 0.2));
        const auto pruned = h.prune_before(3.0, 0.9);
        check(pruned == 1, "only low-sig old events pruned");
        check(h.size() == 2, "major event retained");
        check(h.event(2)->category == "major", "lookup survives prune");
        check(h.event(1) == nullptr, "pruned id is gone");
    }

    // --- Determinism ----------------------------------------------------------------------
    {
        auto run = [] {
            EventHistory h;
            for (int i = 0; i < 100; ++i) {
                auto e = ev(i % 3 ? "war.battle" : "colony.founded",
                            double(i), double(i % 10) / 10.0);
                e.actors = {std::uint64_t(i % 5)};
                h.record(e);
            }
            std::uint64_t acc = 0;
            HistoryQuery q;
            q.category = "war.battle";
            for (const auto* e : h.query(q)) acc += e->id;
            return acc;
        };
        check(run() == run(), "identical histories bit-equal");
    }

    // --- Scale ------------------------------------------------------------------------------
    {
        EventHistory h(200000);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200000; ++i) {
            auto e = ev("tick.event", double(i), double(i % 7) / 10.0);
            e.actors = {std::uint64_t(i % 50)};
            h.record(std::move(e));
        }
        HistoryQuery q;
        q.min_significance = 0.5;
        const auto hits = h.query(q);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "scale: 200k events + full query in " << ms
                  << " ms; hits=" << hits.size() << "\n";
        check(!hits.empty(), "scale query returns results");
        check(ms < 30000.0, "scale run within budget");
    }

    // --- Persistence -------------------------------------------------------------
    {
        EventHistory a;
        a.record(ev("colony.founded", 10.0, 0.8));
        auto secret = ev("espionage.op", 11.0, 0.9);
        secret.visible_to = {7};
        a.record(secret);
        const auto state = a.capture_state();

        EventHistory b;
        b.restore_state(state);
        check(b.size() == a.size() && b.next_id() == a.next_id(),
              "chronicle restored");
        check(b.feed(7, 0.0).size() == 2 && b.feed(9, 0.0).size() == 1,
              "observer filtering identical after restore");
        // Continuity: new ids follow the snapshot, queries stay ordered.
        const auto id = b.record(ev("war.battle", 20.0));
        check(id == 3 && b.event(3)->category == "war.battle",
              "recording continues after restore");

        // Malformed snapshots are rejected, not silently reordered.
        EventHistory::State bad;
        bad.events.push_back(ev("e", 1.0));
        bad.events.back().id = 5;
        bad.events.push_back(ev("e", 2.0));
        bad.events.back().id = 3; // non-ascending
        bad.next_id = 4;          // also collides with retained id 5
        EventHistory c;
        bool threw = false;
        try {
            c.restore_state(bad);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "non-ascending ids rejected");
        check(c.size() == 0, "failed restore leaves history untouched");
    }

    if (failures == 0) {
        std::cout << "all history tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
