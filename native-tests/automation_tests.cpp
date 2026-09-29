#include <stellar/engine/automation.hpp>

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

UtilityAction act(std::string id, std::string domain, std::string target,
                  double utility, bool routine, int* commits = nullptr) {
    UtilityAction a;
    a.id = std::move(id);
    a.domain = std::move(domain);
    a.target = std::move(target);
    a.routine = routine;
    a.score = [utility] { return utility; };
    a.commit = [commits] {
        if (commits) ++*commits;
    };
    return a;
}

AutomationDomainPolicy policy(std::string domain, AutomationMode mode) {
    AutomationDomainPolicy p;
    p.domain = std::move(domain);
    p.mode = mode;
    return p;
}

} // namespace

int main() {
    // --- Off mode does nothing ------------------------------------------------
    {
        AutomationController c;
        int commits = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Off));
        c.add_action(act("build", "colonies", "colony:1", 0.9, true, &commits));
        const auto pick = c.decide("colonies", 1.0);
        check(!pick, "off mode decides nothing");
        check(commits == 0, "off mode commits nothing");
        check(c.journal().empty(), "off mode journals nothing");
    }

    // --- No policy at all behaves as Off ---------------------------------------
    {
        AutomationController c;
        int commits = 0;
        c.add_action(act("build", "colonies", "colony:1", 0.9, true, &commits));
        check(!c.decide("colonies", 1.0), "unconfigured domain is off");
        check(commits == 0, "unconfigured domain commits nothing");
    }

    // --- Advisory proposes but never commits ------------------------------------
    {
        AutomationController c;
        int commits = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Advisory));
        c.add_action(act("build", "colonies", "colony:1", 0.9, true, &commits),
                     [] { return std::string("colony needs power"); });
        const auto pick = c.decide("colonies", 1.0);
        check(!pick, "advisory returns no commit");
        check(commits == 0, "advisory never commits");
        check(c.journal().size() == 1, "advisory journals one proposal");
        const auto& entry = c.journal().back();
        check(!entry.executed, "proposal flagged not-executed");
        check(entry.action_id == "build", "proposal records pick");
        check(entry.reason == "colony needs power", "proposal carries reason");
        check(entry.mode == AutomationMode::Advisory, "proposal records mode");
        // Same advice next tick → no journal spam.
        c.decide("colonies", 2.0);
        check(c.journal().size() == 1, "unchanged proposal not re-journaled");
        // Changed candidate set → new proposal.
        c.add_action(act("expand", "colonies", "colony:1", 0.95, true));
        c.decide("colonies", 3.0);
        check(c.journal().size() == 2, "changed proposal re-journaled");
        check(c.journal().back().action_id == "expand",
              "new proposal records new pick");
    }

    // --- Assisted commits routine only ------------------------------------------
    {
        AutomationController c;
        int routine = 0, strategic = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Assisted));
        c.add_action(
            act("maintain", "colonies", "colony:1", 0.3, true, &routine));
        c.add_action(
            act("war", "colonies", "colony:1", 0.9, false, &strategic));
        const auto pick = c.decide("colonies", 1.0);
        check(pick && *pick == "maintain",
              "assisted picks best routine action");
        check(routine == 1 && strategic == 0,
              "assisted commits routine not strategic");
        check(c.journal().back().executed, "commit journaled");
        check(c.journal().back().mode == AutomationMode::Assisted,
              "mode journaled");
    }

    // --- Automatic commits the argmax winner -------------------------------------
    {
        AutomationController c;
        int low = 0, high = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Automatic));
        c.add_action(act("weak", "colonies", "colony:1", 0.3, true, &low));
        c.add_action(act("strong", "colonies", "colony:1", 0.9, false, &high));
        const auto pick = c.decide("colonies", 1.0);
        check(pick && *pick == "strong", "automatic commits argmax");
        check(high == 1 && low == 0, "automatic commits winner only");
    }

    // --- Locked targets are skipped, resume after expiry --------------------------
    {
        AutomationController c;
        int locked_commits = 0, free_commits = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Automatic));
        c.add_action(
            act("auto-a", "colonies", "colony:1", 0.9, true, &locked_commits));
        c.add_action(
            act("auto-b", "colonies", "colony:2", 0.5, true, &free_commits));
        c.record_operator_override("colonies", "colony:1", 10.0,
                                   /*lock_days*/ 5.0);
        const auto pick = c.decide("colonies", 11.0);
        check(pick && *pick == "auto-b", "locked target skipped");
        check(locked_commits == 0 && free_commits == 1,
              "only unlocked target commits");
        const auto later = c.decide("colonies", 16.0);
        check(later && *later == "auto-a",
              "expired lock resumes automation");
    }

    // --- target_locked reports state ----------------------------------------------
    {
        AutomationController c;
        c.record_operator_override("colonies", "colony:7", 2.0, 10.0);
        check(c.target_locked("colonies", "colony:7", 5.0),
              "lock active before expiry");
        check(!c.target_locked("colonies", "colony:7", 13.0),
              "lock inactive after expiry");
        check(!c.target_locked("colonies", "colony:8", 5.0),
              "unrelated target unlocked");
        c.clear_override("colonies", "colony:7");
        check(!c.target_locked("colonies", "colony:7", 5.0),
              "cleared lock inactive");
    }

    // --- evaluate() is pure advisory ------------------------------------------------
    {
        AutomationController c;
        int commits = 0;
        c.set_domain_policy(policy("colonies", AutomationMode::Automatic));
        c.add_action(act("a", "colonies", "colony:1", 0.4, true, &commits));
        c.add_action(act("b", "colonies", "colony:1", 0.7, true, &commits));
        const auto ranked = c.evaluate("colonies", 1.0);
        check(ranked.size() == 2, "evaluate ranks all candidates");
        check(ranked.front().action_id == "b", "evaluate orders by utility");
        check(commits == 0, "evaluate commits nothing");
        check(c.journal().empty(), "evaluate journals nothing");
    }

    // --- Journal bounded -----------------------------------------------------------------
    {
        AutomationController c(/*journal_capacity*/ 4);
        c.set_domain_policy(policy("d", AutomationMode::Automatic));
        c.add_action(act("tick", "d", "t", 1.0, true));
        for (int i = 0; i < 10; ++i) c.decide("d", double(i));
        check(c.journal().size() == 4, "journal bounded");
        check(c.journal().front().at_day == 6.0, "oldest entries dropped");
        check(c.executed_count() == 10, "executed count survives eviction");
    }

    // --- Determinism -------------------------------------------------------------------------
    {
        auto run = [] {
            AutomationController c;
            c.set_domain_policy(policy("d", AutomationMode::Automatic));
            c.add_action(act("x", "d", "t1", 0.5, true));
            c.add_action(act("y", "d", "t2", 0.5, true));
            c.add_action(act("z", "d", "t3", 0.6, true));
            std::vector<std::string> picks;
            for (int i = 0; i < 20; ++i)
                if (auto p = c.decide("d", double(i))) picks.push_back(*p);
            return picks;
        };
        check(run() == run(), "identical runs bit-equal");
    }

    // --- State round-trip preserves policies, locks, mind, journal --------------------------
    {
        AutomationController c;
        c.set_domain_policy(policy("colonies", AutomationMode::Automatic));
        c.add_action(act("build", "colonies", "colony:1", 0.9, true));
        c.record_operator_override("colonies", "colony:9", 4.0, 30.0);
        c.decide("colonies", 5.0);
        const auto state = c.capture_state();

        AutomationController restored;
        restored.restore_state(state);
        check(restored.mode("colonies") == AutomationMode::Automatic,
              "policy restored");
        check(restored.target_locked("colonies", "colony:9", 10.0),
              "lock restored");
        check(restored.journal().size() == 1, "journal restored");
        check(restored.mind().incumbent("colonies").value() == "build",
              "incumbent restored");
        // Cooldown survived: re-register the action with a cooldown and
        // a rival — the incumbent stamp keeps the rhythm.
        auto rearmed = act("build", "colonies", "colony:1", 0.9, true);
        rearmed.cooldown_days = 10.0;
        restored.add_action(std::move(rearmed));
        restored.add_action(act("alt", "colonies", "colony:1", 0.4, true));
        const auto pick = restored.decide("colonies", 6.0);
        check(pick && *pick == "alt", "restored cooldown blocks re-commit");
    }

    // --- Knobs: policy and constraint lookups -----------------------------------------------
    {
        AutomationController c;
        AutomationDomainPolicy p = policy("colonies", AutomationMode::Automatic);
        p.policies.push_back({"industry_weight", 1.5});
        p.policies.push_back({"expansion_bias", 0.2});
        p.constraints.push_back({"reserve_credits", 250.0});
        c.set_domain_policy(std::move(p));
        check(c.policy_knob("colonies", "industry_weight") == 1.5,
              "policy knob read");
        check(c.constraint_value("colonies", "reserve_credits") == 250.0,
              "constraint value read");
        check(c.policy_knob("colonies", "missing", 0.7) == 0.7,
              "missing knob returns fallback");
        check(c.policy_knob("other", "industry_weight", 3.0) == 3.0,
              "unconfigured domain returns fallback");
    }

    // --- Advisory alternatives bounded ---------------------------------------------------------
    {
        AutomationController c;
        c.set_domain_policy(policy("d", AutomationMode::Advisory));
        for (int i = 0; i < 10; ++i)
            c.add_action(act("a" + std::to_string(i), "d", "t",
                             double(i) / 10.0, true));
        c.decide("d", 1.0);
        check(!c.journal().empty(), "advisory journaled");
        check(c.journal().back().alternatives.size() <= 4,
              "alternatives bounded");
    }

    if (failures == 0) {
        std::cout << "all automation tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
