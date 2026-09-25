#include <stellar/engine/strategic_ai.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

UtilityAction act(std::string id, std::string domain, double utility,
                  int* commits = nullptr) {
    UtilityAction a;
    a.id = std::move(id);
    a.domain = std::move(domain);
    a.score = [utility] { return utility; };
    a.commit = [commits] {
        if (commits) ++*commits;
    };
    return a;
}

} // namespace

int main() {
    // --- Argmax selection ----------------------------------------------------
    {
        StrategicMind ai;
        int a = 0, b = 0;
        ai.add_action(act("expand", "strategy", 0.4, &a));
        ai.add_action(act("defend", "strategy", 0.8, &b));
        const auto pick = ai.decide("strategy", 0.0);
        check(pick && *pick == "defend", "highest utility wins");
        check(b == 1 && a == 0, "only winner commits");
        check(ai.incumbent("strategy").value() == "defend", "incumbent set");
        check(ai.journal().size() == 1, "decision journaled");
        check(ai.journal().back().candidates == 2, "candidate count journaled");
    }

    // --- Deterministic tie-break by id ------------------------------------------
    {
        StrategicMind ai;
        ai.add_action(act("zebra", "strategy", 0.5));
        ai.add_action(act("alpha", "strategy", 0.5));
        ai.add_action(act("mike", "strategy", 0.5));
        const auto pick = ai.decide("strategy", 0.0);
        check(pick && *pick == "alpha", "equal utilities resolve to smallest id");
    }

    // --- Hysteresis keeps the incumbent --------------------------------------------
    {
        StrategicMind ai;
        ai.add_action(act("expand", "strategy", 0.40));
        ai.add_action(act("defend", "strategy", 0.42));
        auto pick = ai.decide("strategy", 0.0, 0.0, 1.5);
        check(pick && *pick == "defend", "initial argmax wins");
        // Next tick: expand 0.40×hysteresis(1.5)=0.60 only if expand is
        // incumbent — it isn't; defend stays 0.42×1.5=0.63 > 0.40.
        pick = ai.decide("strategy", 1.0, 0.0, 1.5);
        check(pick && *pick == "defend", "hysteresis keeps incumbent");
        // Raise challenger beyond hysteresis.
        ai.add_action(act("expand", "strategy", 0.70));
        pick = ai.decide("strategy", 2.0, 0.0, 1.5);
        check(pick && *pick == "expand", "challenger beats hysteresis margin");
        check(ai.journal().back().switched, "switch journaled");
    }

    // --- min_utility gate --------------------------------------------------------------
    {
        StrategicMind ai;
        int commits = 0;
        ai.add_action(act("weak", "strategy", 0.2, &commits));
        const auto pick = ai.decide("strategy", 0.0, /*min*/ 0.5);
        check(!pick, "below threshold commits nothing");
        check(commits == 0, "no commit fired");
        check(ai.journal().empty(), "non-decision not journaled");
    }

    // --- Cooldown -------------------------------------------------------------------------
    {
        StrategicMind ai;
        int commits = 0;
        auto a = act("raid", "military", 0.9, &commits);
        a.cooldown_days = 10.0;
        ai.add_action(std::move(a));
        ai.add_action(act("wait", "military", 0.1));
        check(ai.decide("military", 0.0).value() == "raid", "raid picked");
        // Raid on cooldown: wait wins even though raid scores higher.
        check(ai.decide("military", 5.0).value() == "wait",
              "cooldown blocks repeat");
        check(ai.decide("military", 11.0).value() == "raid",
              "cooldown expires");
        check(commits == 2, "raid committed twice");
    }

    // --- Domains are independent -----------------------------------------------------
    {
        StrategicMind ai;
        ai.add_action(act("build", "economy", 0.9));
        ai.add_action(act("raid", "military", 0.9));
        ai.decide("economy", 0.0);
        ai.decide("military", 0.0);
        check(ai.incumbent("economy").value() == "build",
              "economy incumbent");
        check(ai.incumbent("military").value() == "raid",
              "military incumbent");
    }

    // --- Disabled actions skip ---------------------------------------------------------
    {
        StrategicMind ai;
        ai.add_action(act("on", "strategy", 0.9));
        ai.add_action(act("off", "strategy", 0.99));
        ai.set_enabled("off", false);
        check(ai.decide("strategy", 0.0).value() == "on",
              "disabled action skipped");
    }

    // --- Journal bounded --------------------------------------------------------------------
    {
        StrategicMind ai(/*journal_capacity*/ 4);
        ai.add_action(act("tick", "d", 1.0));
        for (int i = 0; i < 10; ++i) ai.decide("d", double(i));
        check(ai.journal().size() == 4, "journal bounded");
        check(ai.journal().front().at_day == 6.0, "oldest entries dropped");
    }

    // --- Determinism ----------------------------------------------------------------------------
    {
        auto run = [] {
            StrategicMind ai;
            ai.add_action(act("x", "d", 0.5));
            ai.add_action(act("y", "d", 0.5));
            ai.add_action(act("z", "d", 0.6));
            std::vector<std::string> picks;
            for (int i = 0; i < 20; ++i)
                if (auto p = ai.decide("d", double(i))) picks.push_back(*p);
            return picks;
        };
        check(run() == run(), "identical runs bit-equal");
    }

    // --- Scale: many actions, many decide cycles -------------------------------------------------
    {
        StrategicMind ai;
        for (int i = 0; i < 200; ++i) {
            UtilityAction a;
            a.id = "a" + std::to_string(i);
            a.domain = "strategy";
            a.score = [i] { return double(i % 17) / 17.0; };
            ai.add_action(std::move(a));
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < 5000; ++t) ai.decide("strategy", double(t));
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "scale: 200 actions x 5000 decisions in " << ms << " ms\n";
        check(ms < 20000.0, "scale run within budget");
    }

    if (failures == 0) {
        std::cout << "all strategic ai tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
